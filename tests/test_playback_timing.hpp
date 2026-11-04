#pragma once

// Included after the core fixture helpers in test_core.cpp.
namespace PlaybackTimingTest
{
	struct Transform { Quaternion local; Transform* parent{nullptr}; };
	inline Transform head{}, face{}, left{}, right{};
	inline float morphWeight{0};
	inline Quaternion World(Transform* transform)
	{
		return transform->parent ? World(transform->parent) * transform->local : transform->local;
	}
	inline int plays{0}, resumes{0}, seeks{0}, pausedSeeks{0}, pauses{0}, stops{0};
	inline float position{-1};
	inline void Reset()
	{
		plays = resumes = seeks = pausedSeeks = pauses = stops = 0;
		position = -1;
	}
	inline void Prepare(MotionPlayer& player)
	{
		SetupLongClipPlayer(player);
		player.fnPlayAudio = [](const std::string&) { ++plays; position = 0; };
		player.fnResumeAudio = [](float time) { ++resumes; position = time; };
		player.fnPauseAudio = []() { ++pauses; };
		player.fnStopAudio = []() { ++stops; position = -1; };
		player.fnSeekAudio = [](float time) { ++seeks; position = time; };
		player.fnSeekPausedAudio = [](float time) { ++pausedSeeks; position = time; };
	}
	inline bool TimingNear(float value, float expected) { return std::fabs(value - expected) < 1e-4f; }
	inline std::vector<char> WavFixture(bool dataFirst = false, bool oddUnknownChunk = false)
	{
		std::vector<char> bytes;
		const auto text = [&](const char* value, size_t size) { bytes.insert(bytes.end(), value, value + size); };
		const auto u32 = [&](uint32_t value) {
			for (unsigned shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<char>(value >> shift));
		};
		const auto u16 = [&](uint16_t value) {
			bytes.push_back(static_cast<char>(value)); bytes.push_back(static_cast<char>(value >> 8));
		};
		text("RIFF", 4); u32(0); text("WAVE", 4);
		if (oddUnknownChunk) { text("JUNK", 4); u32(1); bytes.push_back('x'); bytes.push_back(0); }
		const auto format = [&]() {
			text("fmt ", 4); u32(16); u16(1); u16(1); u32(8000); u32(8000); u16(1); u16(8);
		};
		const auto data = [&]() { text("data", 4); u32(4000); bytes.insert(bytes.end(), 4000, 0); };
		if (dataFirst) { data(); format(); } else { format(); data(); }
		const uint32_t riffSize = static_cast<uint32_t>(bytes.size() - 8);
		for (unsigned i = 0; i < 4; ++i) bytes[4 + i] = static_cast<char>(riffSize >> (i * 8));
		return bytes;
	}
	inline void WriteWavFixture(const std::filesystem::path& path, const std::vector<char>& bytes)
	{
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	}
}

static void TestPlaybackTiming()
{
	using namespace PlaybackTimingTest;
	auto actor = reinterpret_cast<void*>(0x1);
	BeginTest("PCM WAV duration scans chunk headers without decoding samples");
	{
		const auto wavPath = std::filesystem::temp_directory_path() / "gkmotion_playback_timing_duration.wav";
		WriteWavFixture(wavPath, WavFixture());
		Check(TimingNear(audio::PcmWavDuration(wavPath), 0.5f), "PCM byte count yields the actual half-second music duration");
		WriteWavFixture(wavPath, WavFixture(false, true));
		Check(TimingNear(audio::PcmWavDuration(wavPath), 0.5f), "odd unknown chunks include their RIFF padding byte");
		WriteWavFixture(wavPath, WavFixture(true));
		Check(TimingNear(audio::PcmWavDuration(wavPath), 0.5f), "data before format chunk still produces the correct duration");
		auto truncated = WavFixture();
		truncated.pop_back();
		WriteWavFixture(wavPath, truncated);
		Check(audio::PcmWavDuration(wavPath) == 0, "truncated advertised sample payload is rejected");
		WriteWavFixture(wavPath, std::vector<char>{'R', 'I', 'F', 'F'});
		Check(audio::PcmWavDuration(wavPath) == 0, "truncated RIFF header has no duration");
		std::filesystem::remove(wavPath);
		Check(audio::PcmWavDuration(wavPath) == 0, "absent music file contributes no duration");
	}
	{
		const auto directory = std::filesystem::temp_directory_path() /
			("gkmotion_playback_timing_adopt_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directory(directory);
		WriteWavFixture(directory / "song.wav", WavFixture());
		WriteTextFile(directory / "info.json", R"({"track_timing":{"motion":{"speed":0.5,"offset_seconds":2},"audio":{"speed":1,"offset_seconds":50}}})");
		MotionPlayer player;
		Prepare(player);
		player.AdoptLoadedMotion(directory.string(), (directory / "motion.gkclip").string(), std::move(player.clip));
		Check(TimingNear(player.timing.motion.speed, 0.5f) && TimingNear(player.timing.motion.offsetSeconds, 2) &&
			TimingNear(player.audioSourceDuration, 0.5f) && TimingNear(player.TimelineDuration(), 50.5f),
			"adopting a loaded clip reads this song's metadata and actual WAV duration together");
		MotionClip nextClip = player.clip;
		player.AdoptLoadedMotion((directory / "absent").string(), "motion.gkclip", std::move(nextClip));
		Check(TimingNear(player.timing.motion.speed, 1) && TimingNear(player.timing.motion.offsetSeconds, 0) &&
			player.audioSourceDuration == 0 && TimingNear(player.TimelineDuration(), 20),
			"switching to a legacy song resets timing and music duration instead of retaining the prior song");
		std::filesystem::remove(directory / "song.wav");
		std::filesystem::remove(directory / "info.json");
		std::filesystem::remove(directory);
	}
	BeginTest("playback selector rates and independent source clocks");
	for (float rate : {0.1f, 0.2f, 0.5f, 0.8f, 1.0f, 1.2f, 1.5f, 2.0f, 3.0f})
	{
		MotionPlayer player;
		Prepare(player);
		player.SetPlaybackSpeed(rate);
		player.AdvanceClock(1.0f);
		Check(TimingNear(player.currentTime, rate), "every selector rate advances the shared timeline");
	}
	{
		MotionPlayer player;
		Prepare(player);
		player.currentTime = 3.0f;
		player.timing.motion = {2.0f, 1.0f};
		player.timing.facial = {0.5f, -1.0f};
		player.timing.camera = {1.2f, 2.0f};
		Check(TimingNear(player.MotionSourceTime(), 4.0f), "positive body offset delays source playback");
		Check(TimingNear(player.FacialSourceTime(), 2.0f), "negative facial offset advances source playback");
		Check(TimingNear(player.CameraSourceTime(), 1.2f), "camera has its own source speed and offset");
		player.currentTime = 0;
		Check(TimingNear(player.MotionSourceTime(), 0), "a delayed body holds its first source frame");
		player.timing.motion.speed = 0;
		player.currentTime = 10;
		Check(TimingNear(player.MotionSourceTime(), 0), "zero track speed freezes the source at frame zero");
		player.timing.motion = {1.5f, 0};
		player.currentTime = 0;
		player.SetPlaybackSpeed(2.0f);
		player.AdvanceClock(1.0f);
		Check(TimingNear(player.MotionSourceTime(), 3.0f), "file speed multiplies transport rate");
	}
	{
		MotionPlayer player;
		Prepare(player);
		player.timing.motion = {0.5f, 2.0f};
		Check(TimingNear(player.TimelineDuration(), 42.0f), "slowed delayed body extends the shared timeline");
		player.timing.motion = {2.0f, -1.0f};
		Check(TimingNear(player.TimelineDuration(), 9.0f), "advanced accelerated body shortens its timeline");
		player.audioSourceDuration = 20;
		player.timing.audio = {1, 30};
		Check(TimingNear(player.TimelineDuration(), 50), "music delayed past the body endpoint still extends the timeline through its tail");
		player.timing.audio = {0.5f, 3};
		Check(TimingNear(player.TimelineDuration(), 43), "music duration uses its independent speed and offset");
	}

	BeginTest("unsupported audio rates never start or seek the music");
	for (float rate : {0.1f, 0.2f, 0.5f, 0.8f, 1.2f, 1.5f, 2.0f, 3.0f})
	{
		MotionPlayer player;
		Prepare(player);
		player.SetPlaybackSpeed(rate);
		Reset();
		Check(!player.AudioRateSupported(), "unsupported effective audio rate is recognized");
		player.Play();
		player.Pause();
		player.Resume();
		player.Seek(2.0f);
		player.SetMuted(true);
		player.SetMuted(false);
		player.currentTime = player.TimelineDuration() - 0.01f;
		player.Tick(0.25f, actor);
		Check(plays == 0 && resumes == 0 && seeks == 0 && pausedSeeks == 0 && !player.audioStarted,
			"play, resume, seek, unmute and loop stay silent without audio stretching");
	}
	{
		MotionPlayer player;
		Prepare(player);
		player.timing.audio.speed = 0;
		Reset();
		player.Play();
		Check(!player.AudioRateSupported() && plays == 0, "zero audio speed is silent");
		player.timing.audio.speed = 0.5f;
		player.SetPlaybackSpeed(2.0f);
		Check(player.AudioRateSupported(), "multiplying audio and transport speeds can restore native audio rate");
	}

	BeginTest("rate changes and audio offsets preserve the selected position");
	{
		MotionPlayer player;
		Prepare(player);
		Reset();
		player.Play();
		Check(plays == 1 && player.audioStarted, "normal rate starts native music");
		player.currentTime = 4.0f;
		player.SetPlaybackSpeed(2.0f);
		Check(stops > 0 && !player.audioStarted, "switching to an unsupported rate stops existing music");
		player.AdvanceClock(0.5f);
		player.SetPlaybackSpeed(1.0f);
		Check(player.audioStarted && plays == 2 && TimingNear(position, 5.0f),
			"returning to normal rate reopens music at the current timeline position");
	}
	{
		MotionPlayer player;
		Prepare(player);
		player.timing.audio.offsetSeconds = 2.0f;
		Reset();
		player.Play();
		Check(plays == 0 && !player.audioStarted, "positive audio offset waits before opening music");
		player.Tick(1.0f, actor);
		Check(plays == 0, "delayed music stays silent before its start");
		player.Tick(1.1f, actor);
		Check(plays == 1 && player.audioStarted && TimingNear(position, 0.1f),
			"crossing a positive offset starts music at its matching source position");
		player.Seek(1.0f);
		Check(!player.audioStarted && stops > 0, "seeking before the music start stops the current source");
	}
	{
		MotionPlayer player;
		Prepare(player);
		player.timing.audio.offsetSeconds = -2.0f;
		Reset();
		player.Play();
		Check(plays == 1 && TimingNear(position, 2.0f), "negative audio offset begins part way through the source");
	}
	{
		MotionPlayer player;
		Prepare(player);
		player.timing.audio = {0.5f, 1.0f};
		player.SetPlaybackSpeed(2.0f);
		player.fnGetAudioTime = []() { return position; };
		player.state = PlaybackState::Playing;
		player.audioStarted = true;
		player.currentTime = 2.0f;
		player.audioSyncSettleFrames = 0;
		player.audioSeekOffset = 0;
		position = 1.0f;
		player.AdvanceClock(0.5f);
		Check(TimingNear(player.currentTime, 3.0f),
			"native audio clock is inverted through its own speed and offset into shared timeline time");
	}

	BeginTest("source frame navigation honors motion timing");
	{
		MotionPlayer player;
		Prepare(player);
		player.timing.motion = {2.0f, 1.0f};
		player.SeekFrame(60);
		Check(player.IsPreviewing() && TimingNear(player.currentTime, 2.0f) && TimingNear(player.MotionSourceTime(), 2.0f),
			"source frame seek inverts speed and offset into shared timeline time");
		player.StepFrame(1);
		Check(player.navigationFrame == 61 && TimingNear(player.MotionSourceTime(), 61.0f / 30.0f),
			"one frame step advances one stored source sample at a nondefault track speed");
	}

	BeginTest("actual pose consumer samples body, morphs and eyes at separate file times");
	{
		MotionClip clip;
		clip.format = MotionClip::Format::kGkClip;
		clip.version = 5;
		clip.fps = 1;
		clip.total_frames = 2;
		clip.duration_seconds = 2;
		clip.bone_count = 3;
		clip.bones = {{10, "Head", true}, {21, "LeftEye", true, true}, {22, "RightEye", true, true}};
		clip.bone_parents = BuildBoneParentIndex(clip.bones);
		clip.bone_parent_hids = BuildBoneParentHids(clip.bones);
		clip.root_positions.resize(2);
		clip.bone_rotations = {Quaternion{}, Quaternion{}, Quaternion{},
			Quaternion::FromEulerDegreesXYZ(0, 0, 30),
			Quaternion::FromEulerDegreesXYZ(15, 0, 0),
			Quaternion::FromEulerDegreesXYZ(0, 20, 0)};
		clip.blendshape_names = {"mouth"};
		clip.blendshape_weights = {0, 1};
		head = {Quaternion{}, nullptr}; face = {Quaternion{}, &head};
		left = {Quaternion{}, &face}; right = {Quaternion{}, &face};
		morphWeight = 0;
		IdolController controller;
		controller.eyePoseRelativeGain = false;
		controller.fnGetBoneTransform = [](void*, int hid) -> void* {
			return hid == 10 ? &head : hid == 21 ? &left : hid == 22 ? &right : nullptr;
		};
		controller.fnGetTransformParent = [](void* p) -> void* { return static_cast<Transform*>(p)->parent; };
		controller.fnGetWorldRotation = [](void* p, float* q) {
			const auto rotation = World(static_cast<Transform*>(p));
			q[0] = rotation.x; q[1] = rotation.y; q[2] = rotation.z; q[3] = rotation.w;
		};
		controller.fnSetLocalRotation = [](void* p, float x, float y, float z, float w) {
			static_cast<Transform*>(p)->local = {x, y, z, w};
		};
		controller.faceApi = {
			[](void*) -> void* { return &morphWeight; },
			[](void*) { return 1; },
			[](void*, int) { return std::string("mouth"); },
			[](void*, int) { return morphWeight; },
			[](void*, int, float weight) { morphWeight = weight; }
		};
		controller.faceApi.getActorName = [](void*) { return std::string("mdl_chr_hski-base-0000"); };
		IdolBoneBinding binding;
		binding.actor = actor;
		binding.bindPoseCaptured = true;
		binding.boneTransforms = {&head, &left, &right};
		binding.boneHids = {10, 21, 22};
		binding.restRotations.resize(3);
		binding.restLocals.resize(3);
		controller.ApplyMotionToBinding(binding, clip, 0.25f, 0.75f);
		Check(QuatNear(head.local, Quaternion::FromEulerDegreesXYZ(0, 0, 7.5f)),
			"body pose is sampled at motion file time");
		Check(QuatNear(left.local, Quaternion::FromEulerDegreesXYZ(11.25f, 0, 0)) &&
			QuatNear(right.local, Quaternion::FromEulerDegreesXYZ(0, 15, 0)),
			"imported gaze bones use facial file time together with expressions");
		Check(TimingNear(morphWeight, 75.0f), "facial morphs use facial file time at the native face consumer");
	}
}
