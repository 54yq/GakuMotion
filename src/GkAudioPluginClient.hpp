// SPDX-License-Identifier: MIT
#pragma once

#include "../include/GakuAudio.h"
#include "../include/GakuPlugin.h"
#include "GkAudioPlayback.hpp"
#include "GkJsonLite.hpp"
#include "GkMotionData.hpp"

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace GakuMotion::audio
{
	// GakuAudio playback and preset adapter for the existing Motion transport.
	class AudioPluginClient
	{
	public:
		void BindHost(const GakuHostApi* hostApi)
		{
			std::lock_guard<std::mutex> lock(mutex_);
			hostApi_ = hostApi;
			directService_ = nullptr;
			cachedService_ = nullptr;
			queriedService_ = GakuAudioServiceV1{};
		}

		void BindDirectService(const GakuAudioServiceV1* service)
		{
			std::lock_guard<std::mutex> lock(mutex_);
			directService_ = IsValidService(service) ? service : nullptr;
			cachedService_ = directService_;
			queriedService_ = GakuAudioServiceV1{};
		}

		void SetConfigRoot(const std::filesystem::path& configRoot)
		{
			std::lock_guard<std::mutex> lock(mutex_);
			configRoot_ = configRoot;
			desiredPresetId_ = "off";
			hasPendingOverride_ = false;
			LoadPersistedPresetLocked();
		}

		bool Available()
		{
			std::lock_guard<std::mutex> lock(mutex_);
			return ResolveLocked() != nullptr;
		}

		bool OpenAndPlay(const std::string& pathUtf8, double startSeconds)
		{
			const GakuAudioServiceV1* svc = Resolve();
			if (!svc || !svc->openAndPlay) return false;
			ApplyPendingPreset(svc);
			return svc->openAndPlay(svc->context, pathUtf8.c_str(), startSeconds) == GAKU_OK;
		}

		bool Pause()
		{
			const GakuAudioServiceV1* svc = Resolve();
			if (!svc || !svc->pause) return false;
			return svc->pause(svc->context) == GAKU_OK;
		}

		bool ResumeAt(double seconds)
		{
			const GakuAudioServiceV1* svc = Resolve();
			if (!svc || !svc->resumeAt) return false;
			ApplyPendingPreset(svc);
			return svc->resumeAt(svc->context, seconds) == GAKU_OK;
		}

		bool Seek(double seconds, bool whilePlaying)
		{
			const GakuAudioServiceV1* svc = Resolve();
			if (!svc || !svc->seek) return false;
			return svc->seek(svc->context, seconds, whilePlaying ? 1u : 0u) == GAKU_OK;
		}

		bool Stop()
		{
			const GakuAudioServiceV1* svc = Resolve();
			if (!svc || !svc->stop) return false;
			return svc->stop(svc->context) == GAKU_OK;
		}

		bool Observe(Observation& out, double* outPositionSeconds = nullptr, double* outDurationSeconds = nullptr)
		{
			const GakuAudioServiceV1* svc = Resolve();
			if (!svc || !svc->observe) return false;
			GakuAudioObservationV1 raw{};
			raw.abiVersion = GAKU_AUDIO_VERSION;
			raw.structSize = sizeof(raw);
			if (svc->observe(svc->context, &raw) != GAKU_OK) return false;
			out.state = MapState(raw.state);
			out.sourceOpen = raw.sourceOpen != 0;
			out.confirmed = raw.confirmed != 0;
			out.sourceEpoch = raw.sourceEpoch;
			out.errorCode = raw.errorCode < 0 ? static_cast<uint32_t>(-raw.errorCode) : static_cast<uint32_t>(raw.errorCode);
			if (outPositionSeconds) *outPositionSeconds = raw.positionSeconds;
			if (outDurationSeconds) *outDurationSeconds = raw.durationSeconds;
			return true;
		}

		bool SetVolume(float volume)
		{
			const GakuAudioServiceV1* svc = Resolve();
			if (!svc || !svc->setVolume) return false;
			return svc->setVolume(svc->context, volume) == GAKU_OK;
		}

		std::string ActivePresetId()
		{
			std::lock_guard<std::mutex> lock(mutex_);
			const GakuAudioServiceV1* svc = ResolveLocked();
			if (svc && svc->getActivePreset)
			{
				if (hasPendingOverride_ && !desiredPresetId_.empty() && svc->setActivePreset)
				{
					svc->setActivePreset(svc->context, desiredPresetId_.c_str());
					hasPendingOverride_ = false;
				}
				char idBuf[64]{};
				if (svc->getActivePreset(svc->context, idBuf, sizeof(idBuf)) == GAKU_OK && idBuf[0] != '\0')
				{
					desiredPresetId_ = idBuf;
					return desiredPresetId_;
				}
			}
			return desiredPresetId_.empty() ? std::string("off") : desiredPresetId_;
		}

	private:
		static bool IsValidService(const GakuAudioServiceV1* svc)
		{
			return svc != nullptr &&
				svc->abiVersion == GAKU_AUDIO_VERSION &&
				svc->structSize >= offsetof(GakuAudioServiceV1, renderOfflineWav) + sizeof(svc->renderOfflineWav);
		}

		static PlaybackState MapState(uint32_t raw)
		{
			switch (raw)
			{
			case GAKU_AUDIO_STARTING: return PlaybackState::Starting;
			case GAKU_AUDIO_PLAYING: return PlaybackState::Playing;
			case GAKU_AUDIO_PAUSED: return PlaybackState::Paused;
			case GAKU_AUDIO_FAILED: return PlaybackState::Failed;
			default: return PlaybackState::Stopped;
			}
		}

		const GakuAudioServiceV1* Resolve()
		{
			std::lock_guard<std::mutex> lock(mutex_);
			return ResolveLocked();
		}

		const GakuAudioServiceV1* ResolveLocked()
		{
			if (directService_) return directService_;
			if (cachedService_) return cachedService_;
			if (!hostApi_ || !hostApi_->queryService || !hostApi_->hostContext) return nullptr;

			GakuAudioServiceV1 candidate{};
			const GakuStatus st = hostApi_->queryService(
				hostApi_->hostContext,
				GAKU_SERVICE_AUDIO_CORE,
				GAKU_AUDIO_VERSION,
				sizeof(candidate),
				&candidate);
			if (st == GAKU_OK && IsValidService(&candidate))
			{
				queriedService_ = candidate;
				cachedService_ = &queriedService_;
				if (hasPendingOverride_ && !desiredPresetId_.empty() && cachedService_->setActivePreset)
				{
					cachedService_->setActivePreset(cachedService_->context, desiredPresetId_.c_str());
					hasPendingOverride_ = false;
				}
			}
			return cachedService_;
		}

		void ApplyPendingPreset(const GakuAudioServiceV1* svc)
		{
			std::lock_guard<std::mutex> lock(mutex_);
			if (hasPendingOverride_ && !desiredPresetId_.empty() && svc && svc->setActivePreset)
			{
				svc->setActivePreset(svc->context, desiredPresetId_.c_str());
				hasPendingOverride_ = false;
			}
		}

		std::filesystem::path PresetFilePathLocked() const
		{
			if (configRoot_.empty()) return {};
			return configRoot_ / "audio_effects.json";
		}

		void LoadPersistedPresetLocked()
		{
			const auto path = PresetFilePathLocked();
			if (path.empty()) return;
			std::error_code ec;
			if (!std::filesystem::exists(path, ec)) return;
			std::ifstream in(path, std::ios::binary);
			if (!in) return;
			std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			jsonlite::Value root;
			std::string err;
			if (!jsonlite::ParseText(text, root, err) || root.type != jsonlite::Type::Object) return;
			const std::string saved = root.StringPath("activeEffectId", "");
			if (!saved.empty())
			{
				desiredPresetId_ = saved;
				hasPendingOverride_ = true;
			}
		}

		std::mutex mutex_;
		const GakuHostApi* hostApi_{nullptr};
		const GakuAudioServiceV1* directService_{nullptr};
		const GakuAudioServiceV1* cachedService_{nullptr};
		GakuAudioServiceV1 queriedService_{};
		std::filesystem::path configRoot_;
		std::string desiredPresetId_{"off"};
		bool hasPendingOverride_{false};
	};
} // namespace GakuMotion::audio
