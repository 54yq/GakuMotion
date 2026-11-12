#pragma once

#include "GkPerformanceClock.hpp"
#include "GkMotionPlayer.hpp"

namespace GakuMotion::performance
{
    // Default Windows AudioDriven backend: reuse the current MCI/local-fallback
    // MotionPlayer. This factory does not rewire production playback. The player
    // must outlive the returned view, and the host remains its sole Tick owner.
    inline AudioDriven MakeMotionPlayerAudioDrivenClock(MotionPlayer& player)
    {
        return AudioDriven{AudioClockBackend{
            &player,
            [](void* ctx) { return PerformanceTime{static_cast<MotionPlayer*>(ctx)->currentTime}; },
            [](void* ctx, PerformanceTime time) {
                auto& p = *static_cast<MotionPlayer*>(ctx);
                if (p.clip.total_frames == 0) return false;
                const double bounded = (std::min)(time.seconds, static_cast<double>(p.TimelineDuration()));
                p.Seek(static_cast<float>(bounded));
                return true;
            },
            [](void* ctx) { static_cast<MotionPlayer*>(ctx)->Pause(); },
            [](void* ctx) {
                auto& p = *static_cast<MotionPlayer*>(ctx);
                if (p.state == PlaybackState::Paused) p.Resume();
                else if (p.state != PlaybackState::Playing) p.Play();
            },
            [](void* ctx) { return static_cast<MotionPlayer*>(ctx)->state != PlaybackState::Playing; },
            [](void* ctx) { return static_cast<double>(static_cast<MotionPlayer*>(ctx)->playbackSpeedMultiplier); },
            [](void* ctx, double rate) {
                static_cast<MotionPlayer*>(ctx)->SetPlaybackSpeed(static_cast<float>(rate));
                return true;
            }
        }};
    }

    // Keep the existing gkclip/source sampling semantics and float boundary.
    // offsetSeconds is a delay in PERFORMANCE seconds, not a source-time addend.
    inline float LegacySourceTime(PerformanceTime time, const TrackTiming& timing) noexcept
    {
        if (!time.Valid()) return 0.0f;
        const double bounded = (std::min)(time.seconds, static_cast<double>((std::numeric_limits<float>::max)()));
        return TrackSourceTime(static_cast<float>(bounded), timing);
    }
}
