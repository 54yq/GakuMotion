#pragma once
#include <cmath>
#include <cstdint>

namespace GakuMotion::live_end_loop
{
    // Rewinding and a transient playing flag do not undo the surrounding
    // Live completion sequence. The in-game regression left a black screen.
    inline constexpr bool CallbackReplayEnabled = false;
    inline const char* StatusText(int status) {
        switch (status) {
        case 0: return "尚未触发结束循环";
        case 1: return "结束回调不在 Unity 主线程，已放行退出";
        case 2: return "接管、拍照或场景状态不允许重播，已放行退出";
        case 3: case 41: return "Live 对象归属未确认，已放行退出";
        case 42: return "Live 执行数据已不可用，已放行退出";
        case 43: return "无法读取暂停状态，已放行退出";
        case 44: return "Live 处于暂停，已放行退出";
        case 45: return "无法取得有效时长，已放行退出";
        case 46: return "结束时间读取失败且没有近期结尾证据，已放行退出";
        case 47: return "结束时间已回退且没有近期结尾证据，已放行退出";
        case 5: return "已尝试重播，但未确认开头及播放状态，已放行退出";
        case 6: return "只观察到时间回退与播放标志，未证明舞台或交互恢复";
        default: return "重播发生异常，已放行退出";
        }
    }
    // Offline contract only: no game callback or suppression is installed here.
    // The adapter must carry out and verify a synchronous restart; dispatching
    // a Seek request by itself is not evidence that a Live has restarted.
    struct Evidence
    {
        uintptr_t owner{}, presenter{};
        int scene{};
        bool viewing{}, ending{}, paused{}, takeover{}, captureBusy{};
    };
    struct RestartResult
    {
        bool accepted{}, observed{}, sameOwner{}, playing{};
        float time{};
    };
    using Restart = RestartResult (*)(void*, const Evidence&);
    enum class Decision { ForwardEnd, SuppressEnd };
    inline bool TerminalEvidence(bool timeKnown, float time, float duration, bool recentNearEnd, double recentAge)
    {
        if (!std::isfinite(duration) || duration <= 0) return false;
        if (timeKnown && std::isfinite(time) && time >= duration - 0.25f) return true;
        return recentNearEnd && std::isfinite(recentAge) && recentAge >= 0 && recentAge <= 3.0;
    }
    enum class Entry { Entered, NestedSame, NestedOther };
    class Reentry
    {
        uintptr_t active_{};
    public:
        Entry Begin(uintptr_t presenter) {
            if (active_) return active_ == presenter ? Entry::NestedSame : Entry::NestedOther;
            if (!presenter) return Entry::NestedOther;
            active_ = presenter; return Entry::Entered;
        }
        void End() { active_ = 0; }
    };
    class Guard
    {
        uintptr_t owner_{}, presenter_{};
        int scene_{};
        bool enabled_{}, busy_{}, faulted_{};
    public:
        bool Arm(const Evidence& e)
        {
            if (!e.owner || !e.presenter || !e.scene || !e.viewing || e.ending || e.paused || e.takeover || e.captureBusy) return false;
            owner_ = e.owner; presenter_ = e.presenter; scene_ = e.scene;
            enabled_ = true; busy_ = faulted_ = false; return true;
        }
        void Disarm() { enabled_ = busy_ = false; owner_ = presenter_ = 0; scene_ = 0; }
        bool Faulted() const { return faulted_; }
        Decision OnEnd(const Evidence& e, void* context, Restart restart)
        {
            if (!enabled_ || busy_ || faulted_ || !restart || e.owner != owner_ || e.presenter != presenter_ ||
                e.scene != scene_ || !e.viewing || !e.ending || e.paused || e.takeover || e.captureBusy)
                return Decision::ForwardEnd;
            busy_ = true;
            RestartResult result{};
            try { result = restart(context, e); } catch (...) { result = {}; }
            busy_ = false;
            if (!result.accepted || !result.observed || !result.sameOwner || !result.playing ||
                !std::isfinite(result.time) || result.time < 0.0f || result.time > 0.5f) {
                faulted_ = true; enabled_ = false; return Decision::ForwardEnd;
            }
            return Decision::SuppressEnd;
        }
    };
}
