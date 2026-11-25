// SPDX-License-Identifier: MIT
namespace
{
	using GakuMotion::Core;
	using GakuMotion::PlaybackState;

	struct RecordingSession
	{
		uint64_t lease{};
		uint64_t generation{};
		uint32_t numerator{}, denominator{};
		std::string owner;
		bool active{}, waiting{}, touched{}, prepared{};
		uint64_t frame{};
		double start{}, duration{};
		float originalTime{};
		PlaybackState originalState{};
	};

	RecordingSession recording;
	uint64_t nextRecordingLease{};
	uint64_t evaluatedRecordingSerial{};
	unsigned captureDepth{};

	void ResetRecording()
	{
		recording = {};
		captureDepth = 0;
	}

	bool PrepareRecordingPose(Core& core, bool newFrame, float& delta)
	{
		auto& player = core.player;
		if (recording.generation != core.loadedMotionGeneration ||
			(recording.touched && player.state != PlaybackState::Paused && player.state != PlaybackState::Preview))
		{
			recording.active = false;
			recording.touched = false;
		}
		if (!recording.active)
		{
			if (recording.touched)
			{
				recording.touched = false;
				player.Seek(recording.originalTime);
				if (recording.originalState == PlaybackState::Playing) player.Resume();
			}
			return captureDepth != 0;
		}
		if (!recording.touched)
		{
			recording.touched = true;
			recording.originalTime = player.currentTime;
			recording.originalState = player.state;
			player.Pause();
			player.Seek(static_cast<float>(recording.start));
		}
		if (newFrame && !recording.waiting && captureDepth == 0)
		{
			const double step = static_cast<double>(recording.denominator) / recording.numerator;
			player.currentTime = static_cast<float>(recording.start + static_cast<double>(recording.frame) * step);
			recording.prepared = true;
			delta = recording.frame ? static_cast<float>(step) : 0.0f;
		}
		else delta = 0.0f;
		return true;
	}

	void FinishRecordingPose(Core& core)
	{
		if (!recording.active || !recording.prepared || recording.waiting || captureDepth != 0 || !core.InjectionAllowed()) return;
		const auto binding = core.player.controller.bindings.find(core.player.controller.targetActor);
		if (binding == core.player.controller.bindings.end() || !binding->second.bindPoseCaptured ||
			binding->second.boundBoneCount == 0) return;
		recording.prepared = false;
		recording.waiting = true;
		++evaluatedRecordingSerial;
	}

	template<class Function>
	GakuStatus WithRecordingCore(void* context, Function function)
	{
		if (!context) return GAKU_E_INVALID_ARGUMENT;
		auto& core = *static_cast<Core*>(context);
		std::lock_guard guard(core.stateMutex);
		return function(core);
	}

	GakuStatus PublishRecordingClock()
	{
		static const GakuMotionClockServiceV1 service{
			GAKU_MOTION_CLOCK_VERSION, sizeof(GakuMotionClockServiceV1), &Core::Instance(),
			[](void* ctx, GakuMotionClockSnapshotV1* out) -> GakuStatus {
				if (!out) return GAKU_E_INVALID_ARGUMENT;
				if (out->structSize < sizeof(*out)) return GAKU_E_SIZE;
				return WithRecordingCore(ctx, [&](Core& core) {
					const auto& player = core.player;
					const bool active = recording.active && recording.generation == core.loadedMotionGeneration;
					*out = {};
					out->abiVersion = GAKU_MOTION_CLOCK_VERSION;
					out->structSize = sizeof(*out);
					out->runMode = active ? GAKUMOTION_RUN_RECORDING : GAKUMOTION_RUN_REALTIME;
					out->configuredSource = active ? GAKUMOTION_CLOCK_FIXED_STEP : GAKUMOTION_CLOCK_AUDIO;
					out->effectiveSource = active ? GAKUMOTION_CLOCK_FIXED_STEP :
						player.audioStarted ? GAKUMOTION_CLOCK_AUDIO : GAKUMOTION_CLOCK_ENGINE_DELTA;
					out->playbackState = static_cast<uint32_t>(player.state);
					out->fpsNumerator = recording.numerator;
					out->fpsDenominator = recording.denominator;
					out->currentTimeSeconds = player.currentTime;
					out->timelineDurationSeconds = player.TimelineDuration();
					out->fixedFrameIndex = static_cast<int64_t>(recording.frame);
					out->waitingForRenderComplete = active && recording.waiting;
					out->evaluatedFrameSerial = evaluatedRecordingSerial;
					out->leaseId = recording.lease;
					out->audioOffsetSeconds = player.timing.audio.offsetSeconds;
					out->audioSpeed = player.timing.audio.speed;
					out->playbackSpeed = active ? 1.0f : player.playbackSpeedMultiplier;
					out->musicVolume = player.muted ? 0.0f : player.musicVolume;
					out->hasAudio = player.audioSourceDuration > 0 && !player.muted;
					snprintf(out->leaseOwner, sizeof(out->leaseOwner), "%s", recording.owner.c_str());
					if (out->hasAudio)
						snprintf(out->audioFilePath, sizeof(out->audioFilePath), "%s", player.AudioPath().c_str());
					if (core.currentMotionIndex >= 0 && static_cast<size_t>(core.currentMotionIndex) < core.motions.size())
						snprintf(out->trackTitle, sizeof(out->trackTitle), "%s", core.motions[core.currentMotionIndex].title.c_str());
					const auto effect = out->hasAudio && g_audioPlugin.Available() ?
						g_audioPlugin.ActivePresetId() : std::string("off");
					snprintf(out->activeAudioEffectId, sizeof(out->activeAudioEffectId), "%s", effect.c_str());
					return GAKU_OK;
				});
			},
			[](void*, uint32_t) -> GakuStatus { return GAKU_E_UNAVAILABLE; },
			[](void*, uint32_t, uint32_t, uint32_t) -> GakuStatus { return GAKU_E_UNAVAILABLE; },
			[](void* ctx, const char* owner, uint32_t num, uint32_t den, uint64_t* id) -> GakuStatus {
				if (!owner || !*owner || !id || !num || !den || double(num) / den > 240 ||
					double(num) / den < 1) return GAKU_E_INVALID_ARGUMENT;
				*id = 0;
				return WithRecordingCore(ctx, [&](Core&) {
					if (recording.lease || recording.touched) return GAKU_E_CONFLICT;
					recording = {};
					recording.lease = ++nextRecordingLease;
					recording.numerator = num;
					recording.denominator = den;
					recording.owner.assign(owner, strnlen(owner, 63));
					*id = recording.lease;
					return GAKU_OK;
				});
			},
			[](void* ctx, uint64_t id) -> GakuStatus {
				return WithRecordingCore(ctx, [&](Core&) {
					if (!id || id != recording.lease) return GAKU_E_CONFLICT;
					recording.lease = 0;
					recording.active = recording.waiting = recording.prepared = false;
					return GAKU_OK;
				});
			},
			[](void* ctx, uint64_t id, double start) -> GakuStatus {
				if (!std::isfinite(start) || start < 0) return GAKU_E_INVALID_ARGUMENT;
				return WithRecordingCore(ctx, [&](Core& core) {
					if (!id || id != recording.lease || recording.active || recording.touched) return GAKU_E_CONFLICT;
					const double duration = core.player.TimelineDuration();
					if (!core.InjectionAllowed() || core.captureSuspended || core.player.state == PlaybackState::Stopped ||
						core.player.scrubbing || !core.player.controller.targetActor || start >= duration)
						return GAKU_E_UNAVAILABLE;
					recording.generation = core.loadedMotionGeneration;
					recording.start = start;
					recording.duration = duration;
					recording.frame = 0;
					recording.active = true;
					return GAKU_OK;
				});
			},
			[](void* ctx, uint64_t id) -> GakuStatus {
				return WithRecordingCore(ctx, [&](Core&) {
					if (!id || id != recording.lease) return GAKU_E_CONFLICT;
					recording.active = recording.waiting = recording.prepared = false;
					return GAKU_OK;
				});
			},
			[](void* ctx, uint64_t id, uint64_t serial, uint32_t count) -> GakuStatus {
				if (count != 1) return GAKU_E_INVALID_ARGUMENT;
				return WithRecordingCore(ctx, [&](Core&) {
					if (!id || id != recording.lease || !recording.active || !recording.waiting ||
						serial != evaluatedRecordingSerial) return GAKU_E_CONFLICT;
					++recording.frame;
					recording.waiting = false;
					if (recording.start + static_cast<double>(recording.frame) * recording.denominator /
						recording.numerator >= recording.duration) recording.active = false;
					return GAKU_OK;
				});
			},
			[](void* ctx) -> GakuStatus {
				return WithRecordingCore(ctx, [](Core&) { ++captureDepth; return GAKU_OK; });
			},
			[](void* ctx) -> GakuStatus {
				return WithRecordingCore(ctx, [](Core&) {
					if (!captureDepth) return GAKU_E_CONFLICT;
					--captureDepth;
					return GAKU_OK;
				});
			}
		};
		auto& host = GakuMotion::host::api;
		return host.registerService(host.hostContext, GAKU_SERVICE_MOTION_CLOCK, sizeof(service), &service);
	}
	GakuStatus PublishCaptureStageState()
	{
		static const GakuMotionStageStateServiceV1 service{
			GAKU_MOTION_STAGE_STATE_VERSION, sizeof(GakuMotionStageStateServiceV1), &Core::Instance(),
			[](void* ctx, GakuMotionStageStateV1* out) -> GakuStatus {
				if (!out) return GAKU_E_INVALID_ARGUMENT;
				if (out->structSize < sizeof(*out)) return GAKU_E_SIZE;
				return WithRecordingCore(ctx, [&](Core& core) {
					std::lock_guard stageLock(core.stageManager.stageMutex);
					const auto& stage = core.stageManager.settings;
					*out = {};
					out->abiVersion = GAKU_MOTION_STAGE_STATE_VERSION;
					out->structSize = sizeof(*out);
					out->sceneAllowed = core.InjectionAllowed();
					out->injectionActive = out->sceneAllowed && !core.captureSuspended;
					out->overrideCameraClear = stage.overrideCameraClear;
					out->greenScreenActive = out->injectionActive && stage.greenScreenActive && stage.overrideCameraClear;
					out->hideBackground = stage.hideBackground;
					std::copy_n(stage.greenScreenColor, 4, out->greenScreenColor);
					return GAKU_OK;
				});
			}
		};
		auto& host = GakuMotion::host::api;
		return host.registerService(host.hostContext, GAKU_SERVICE_MOTION_STAGE_STATE, sizeof(service), &service);
	}
}
