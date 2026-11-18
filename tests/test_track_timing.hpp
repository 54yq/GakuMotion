#pragma once

static void TestTrackTimingMetadata()
{
	BeginTest("track_timing_metadata_and_time_mapping");
	PerformanceTiming timing;
	Check(timing.motion.speed == 1 && timing.facial.offsetSeconds == 0 && ValidPerformanceTiming(timing),
		"legacy and new tracks default to unit speed and zero offset");
	timing.motion = {0.5f, 2.0f};
	timing.facial = {1.5f, -1.0f};
	timing.camera = {0.0f, -3.0f};
	timing.audio = {2.0f, 0.25f};
	jsonlite::Value info;
	std::string error;
	Check(jsonlite::ParseText("{\"track_timing\":" + PerformanceTimingJson(timing) + "}", info, error),
		"all four track timing settings serialize as valid locale-independent JSON");
	const auto restored = ReadPerformanceTiming(info);
	Check(restored.motion.speed == 0.5f && restored.motion.offsetSeconds == 2.0f &&
		restored.facial.speed == 1.5f && restored.facial.offsetSeconds == -1.0f &&
		restored.camera.speed == 0.0f && restored.camera.offsetSeconds == -3.0f &&
		restored.audio.speed == 2.0f && restored.audio.offsetSeconds == 0.25f,
		"independent track values survive metadata round trip including zero speed and negative offsets");
	Check(TrackSourceTime(1, timing.motion) == 0 && TrackSourceTime(6, timing.motion) == 2 &&
		TrackSourceTime(1, timing.facial) == 3 && TrackSourceTime(20, timing.camera) == 0,
		"positive offsets delay, negative offsets advance, and zero speed holds the first frame");
	Check(TrackTimelineDuration(10, timing.motion) == 22 && TrackTimelineDuration(12, timing.facial) == 7 &&
		TrackTimelineDuration(10, timing.camera) == 10,
		"performance duration transforms inversely with track speed and remains finite at zero speed");
	Check(jsonlite::ParseText("{\"track_timing\":{\"motion\":{\"speed\":-2,\"offset_seconds\":\"bad\"}}}", info, error),
		"malformed-field metadata fixture parses");
	const auto invalid = ReadPerformanceTiming(info);
	Check(invalid.motion.speed == 1 && invalid.motion.offsetSeconds == 0 && invalid.audio.speed == 1,
		"malformed metadata fields and missing tracks use safe defaults");
	timing.audio.speed = -1;
	Check(!ValidPerformanceTiming(timing), "negative track speed is rejected before import");
	timing.audio.speed = (std::numeric_limits<float>::infinity)();
	Check(!ValidPerformanceTiming(timing), "non-finite track speed is rejected before import");
	timing.audio.speed = 1;
	timing.audio.offsetSeconds = (std::numeric_limits<float>::quiet_NaN)();
	Check(!ValidPerformanceTiming(timing), "non-finite offset is rejected before import");
}
