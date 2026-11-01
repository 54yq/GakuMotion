#pragma once

#include <cstdint>

namespace GakuMotion::audio
{
	// Backend observations, not requests or the existence of a play callback.
	// The host refreshes this after Play/Resume/Seek and before BGM policy sync.
	enum class PlaybackState : uint8_t { Stopped, Starting, Playing, Paused, Failed };

	struct Observation
	{
		PlaybackState state{PlaybackState::Stopped};
		bool sourceOpen{false};
		bool confirmed{false};
		uint64_t sourceEpoch{0}; // Nonzero identity of an actual opened audio source.
		uint32_t errorCode{0};

		bool EffectivePlaying() const noexcept
		{
			return state == PlaybackState::Playing && sourceOpen && confirmed && sourceEpoch != 0 && errorCode == 0;
		}
	};

	inline const char* StateName(PlaybackState state) noexcept
	{
		switch (state)
		{
		case PlaybackState::Stopped: return "stopped";
		case PlaybackState::Starting: return "starting";
		case PlaybackState::Playing: return "playing";
		case PlaybackState::Paused: return "paused";
		case PlaybackState::Failed: return "failed";
		}
		return "unknown";
	}
}
