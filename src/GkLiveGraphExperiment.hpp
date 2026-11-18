#pragma once
#include <cmath>
#include <cstdint>

namespace GakuMotion::live_graph_experiment
{
    // Independent lifecycle contract. The runtime must supply verified graph
    // ownership and an actual, reversible physics-state snapshot; no Unity
    // objects or calls are retained by this class.
    struct Evidence
    {
        uint64_t owner{}, actor{}, graph{}, session{};
        bool livePaused{}, graphValid{}, graphExclusive{}, physicsRestorable{};
    };
    struct Adapter
    {
        void* context{};
        bool (*snapshot)(void*, const Evidence&){};
        bool (*evaluate)(void*, const Evidence&, float){};
        bool (*restore)(void*, const Evidence&){};
    };
    enum class State { Off, Active, RestorePending };
    class Session
    {
        Adapter adapter_{};
        Evidence origin_{};
        State state_{State::Off};
        uint64_t lastFrame_{};
        bool haveFrame_{};
        static bool Ready(const Evidence& e)
        {
            return e.owner && e.actor && e.graph && e.session && e.livePaused && e.graphValid &&
                e.graphExclusive && e.physicsRestorable;
        }
        bool Same(const Evidence& e) const
        {
            return Ready(e) && e.owner == origin_.owner && e.actor == origin_.actor &&
                e.graph == origin_.graph && e.session == origin_.session;
        }
    public:
        explicit Session(Adapter a = {}) : adapter_(a) {}
        State Current() const { return state_; }
        bool Start(const Evidence& e)
        {
            if (state_ != State::Off || !Ready(e) || !adapter_.snapshot || !adapter_.evaluate || !adapter_.restore)
                return false;
            if (!adapter_.snapshot(adapter_.context, e)) return false;
            origin_ = e; haveFrame_ = false; state_ = State::Active; return true;
        }
        bool Step(const Evidence& e, uint64_t frame, float dt)
        {
            if (state_ != State::Active) return false;
            if (!Same(e)) { state_ = State::RestorePending; return false; }
            if (!frame || (haveFrame_ && frame <= lastFrame_) || !std::isfinite(dt) || dt <= 0 || dt > 0.05f)
                return false;
            // Mark before calling: recursive callbacks cannot evaluate twice.
            lastFrame_ = frame; haveFrame_ = true;
            if (adapter_.evaluate(adapter_.context, e, dt)) return true;
            state_ = State::RestorePending; return false;
        }
        bool Stop(const Evidence& e)
        {
            if (state_ == State::Off) return true;
            // Never dereference an old actor or graph after ownership changes.
            if (e.owner != origin_.owner || e.actor != origin_.actor || e.graph != origin_.graph ||
                e.session != origin_.session || !e.graphValid)
            { state_ = State::RestorePending; return false; }
            state_ = State::RestorePending;
            if (!adapter_.restore(adapter_.context, e)) return false;
            state_ = State::Off; origin_ = {}; haveFrame_ = false; return true;
        }
    };
}
