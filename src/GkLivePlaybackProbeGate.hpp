#pragma once
#include <cmath>
#include <cstdint>
namespace GakuMotion::live_takeover
{
    // Delay the process-wide read-only probe until the Live timeline has
    // actually advanced for half a second. Reject seeks and paused frames.
    class PlaybackProbeGate
    {
        uintptr_t owner_{}; int scene_{};
        double wall_{}; float time_{}; bool anchored_{}, ready_{};
    public:
        void Reset() noexcept { owner_ = 0; scene_ = 0; anchored_ = false; ready_ = false; }
        bool Observe(uintptr_t owner, int scene, bool eligible, float liveTime, double wallSeconds) noexcept
        {
            if (!eligible || !owner || !scene || !std::isfinite(liveTime) || !std::isfinite(wallSeconds))
            { Reset(); return false; }
            if (!anchored_ || owner != owner_ || scene != scene_ || wallSeconds < wall_)
            { owner_ = owner; scene_ = scene; wall_ = wallSeconds; time_ = liveTime; anchored_ = true; ready_ = false; return false; }
            const double elapsed = wallSeconds - wall_;
            const double advance = static_cast<double>(liveTime - time_);
            if (advance < -0.02 || advance > elapsed * 2.0 + 0.25)
            { wall_ = wallSeconds; time_ = liveTime; ready_ = false; return false; }
            if (elapsed < 0.5) return ready_;
            ready_ = advance >= 0.1;
            wall_ = wallSeconds; time_ = liveTime; return ready_;
        }
    };
}
