#pragma once

// Performance Clock v0: transport coordinates only. No Unity, audio output,
// renderer, editor, file format, or wall-clock ownership belongs in this module.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace GakuMotion::performance
{
    struct PerformanceTime
    {
        double seconds{0.0};
        explicit constexpr PerformanceTime(double value = 0.0) noexcept : seconds(value) {}
        bool Valid() const noexcept { return std::isfinite(seconds) && seconds >= 0.0; }
    };

    enum class ClockKind { AudioDriven, Manual, FixedStep };

    // In-process, single-owner interface, not a plugin ABI. Reads never advance.
    // Finite negative seeks clamp to zero; backend-specific upper bounds apply.
    // Rates use the existing transport range [0.1, 5]; non-finite input fails.
    class Clock
    {
    public:
        virtual ~Clock() = default;
        virtual ClockKind Kind() const noexcept = 0;
        virtual PerformanceTime Time() const noexcept = 0;
        virtual bool Seek(PerformanceTime time) = 0;
        virtual void Pause() = 0;
        virtual void Resume() = 0;
        virtual bool Paused() const noexcept = 0;
        virtual double PlaybackRate() const noexcept = 0;
        virtual bool SetPlaybackRate(double rate) = 0;
    };

    inline double ClampPlaybackRate(double rate) noexcept { return std::clamp(rate, 0.1, 5.0); }

    class Manual final : public Clock
    {
        PerformanceTime time_;
        double rate_{1.0};
        bool paused_{true};

    public:
        ClockKind Kind() const noexcept override { return ClockKind::Manual; }
        PerformanceTime Time() const noexcept override { return time_; }
        bool Seek(PerformanceTime time) noexcept override
        {
            if (!std::isfinite(time.seconds)) return false;
            time_ = PerformanceTime{(std::max)(0.0, time.seconds)};
            return true;
        }
        bool SetTime(PerformanceTime time) noexcept { return Seek(time); }
        void Pause() noexcept override { paused_ = true; }
        void Resume() noexcept override { paused_ = false; }
        bool Paused() const noexcept override { return paused_; }
        double PlaybackRate() const noexcept override { return rate_; }
        bool SetPlaybackRate(double rate) noexcept override
        {
            if (!std::isfinite(rate)) return false;
            rate_ = ClampPlaybackRate(rate);
            return true;
        }
        // Explicit caller-supplied elapsed time, never a hidden wall-clock read.
        // SetTime/Seek remain available while paused and do not resume playback.
        bool Advance(double elapsedSeconds) noexcept
        {
            if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0) return false;
            if (paused_) return true;
            const double next = time_.seconds + elapsedSeconds * rate_;
            if (!std::isfinite(next)) return false;
            time_ = PerformanceTime{next};
            return true;
        }
    };

    struct FrameRate
    {
        uint32_t numerator{60}, denominator{1};
        bool Valid() const noexcept { return numerator != 0 && denominator != 0; }
    };

    class FixedStep final : public Clock
    {
        FrameRate fps_;
        int64_t frame_{0}, anchorFrame_{0};
        PerformanceTime time_, anchorTime_;
        double rate_{1.0};
        bool paused_{true};

        void Reanchor() noexcept { anchorFrame_ = frame_; anchorTime_ = time_; }

    public:
        ClockKind Kind() const noexcept override { return ClockKind::FixedStep; }
        PerformanceTime Time() const noexcept override { return time_; }
        int64_t FrameIndex() const noexcept { return frame_; }
        FrameRate Fps() const noexcept { return fps_; }
        bool SetFrameRate(FrameRate fps) noexcept
        {
            if (!fps.Valid()) return false;
            if (fps.numerator == fps_.numerator && fps.denominator == fps_.denominator) return true;
            Reanchor();
            fps_ = fps;
            return true;
        }
        bool Seek(PerformanceTime time) noexcept override
        {
            if (!std::isfinite(time.seconds)) return false;
            time_ = PerformanceTime{(std::max)(0.0, time.seconds)};
            Reanchor();
            return true;
        }
        void Pause() noexcept override { paused_ = true; }
        void Resume() noexcept override { paused_ = false; }
        bool Paused() const noexcept override { return paused_; }
        double PlaybackRate() const noexcept override { return rate_; }
        bool SetPlaybackRate(double rate) noexcept override
        {
            if (!std::isfinite(rate)) return false;
            rate = ClampPlaybackRate(rate);
            if (rate == rate_) return true;
            Reanchor();
            rate_ = rate;
            return true;
        }
        // frame is an output sample index, NOT a source gkclip frame. At the
        // default anchor/rate, P(n)=n*fps.denominator/fps.numerator. After seek or
        // rate/FPS changes, P(n)=anchorP+(n-anchorN)*den/num*rate.
        // Explicit positioning works while paused; it never resumes playback.
        bool SetFrameIndex(int64_t frame) noexcept
        {
            if (frame < 0) return false;
            const double next = anchorTime_.seconds +
                static_cast<double>(frame - anchorFrame_) *
                fps_.denominator / fps_.numerator * rate_;
            if (!std::isfinite(next) || next < 0.0) return false;
            frame_ = frame;
            time_ = PerformanceTime{next};
            return true;
        }
        bool AdvanceFrames(int64_t count = 1) noexcept
        {
            if (count < 0) return false;
            if (paused_) return true;
            if (count > (std::numeric_limits<int64_t>::max)() - frame_) return false;
            return SetFrameIndex(frame_ + count);
        }
    };

    // The backend is a non-owning view of the existing authoritative transport.
    // It must outlive this clock; all access stays on its owning thread. The
    // host advances it exactly as before. No second Tick or audio sync state.
    // Read callbacks must not throw, must return valid time / finite rate, and
    // must not advance the transport. Paused is logical transport state, not
    // proof that a device is silent. Transport commands may be denied by host
    // policy; callers observe the resulting state rather than assuming success.
    struct AudioClockBackend
    {
        void* context{};
        PerformanceTime (*time)(void*){};
        bool (*seek)(void*, PerformanceTime){};
        void (*pause)(void*){};
        void (*resume)(void*){};
        bool (*paused)(void*){};
        double (*rate)(void*){};
        bool (*setRate)(void*, double){};
    };

    class AudioDriven final : public Clock
    {
        AudioClockBackend backend_;

    public:
        explicit AudioDriven(AudioClockBackend backend) : backend_(backend)
        {
            if (!backend.time || !backend.seek || !backend.pause || !backend.resume ||
                !backend.paused || !backend.rate || !backend.setRate)
                throw std::invalid_argument("AudioDriven requires a complete backend");
        }
        ClockKind Kind() const noexcept override { return ClockKind::AudioDriven; }
        PerformanceTime Time() const noexcept override { return backend_.time(backend_.context); }
        bool Seek(PerformanceTime time) override
        {
            if (!std::isfinite(time.seconds)) return false;
            return backend_.seek(backend_.context, PerformanceTime{(std::max)(0.0, time.seconds)});
        }
        void Pause() override { backend_.pause(backend_.context); }
        void Resume() override { backend_.resume(backend_.context); }
        bool Paused() const noexcept override { return backend_.paused(backend_.context); }
        double PlaybackRate() const noexcept override { return backend_.rate(backend_.context); }
        bool SetPlaybackRate(double rate) override
        {
            return std::isfinite(rate) && backend_.setRate(backend_.context, ClampPlaybackRate(rate));
        }
    };

    enum class LoopResult { BeforeEnd, Restarted, InvalidDuration, HostOwned, SeekFailed };

    // Explicit local transport policy matching the legacy reset-to-zero rule:
    // discard overshoot and retain pause/rate/frame index. AudioDriven must use
    // the host's loop, which also stops/reopens audio and manages finished state.
    inline LoopResult RestartLoop(Clock& clock, PerformanceTime duration)
    {
        if (!duration.Valid() || duration.seconds <= 0.0) return LoopResult::InvalidDuration;
        if (clock.Kind() == ClockKind::AudioDriven) return LoopResult::HostOwned;
        if (clock.Time().seconds < duration.seconds) return LoopResult::BeforeEnd;
        return clock.Seek(PerformanceTime{}) ? LoopResult::Restarted : LoopResult::SeekFailed;
    }
}
