#pragma once
#include "GkLiveEndLoopContract.hpp"

namespace LiveEndLoopHarness
{
    using namespace GakuMotion::live_end_loop;
    struct Mock { int calls{}; RestartResult result{}; Guard* guard{}; Evidence evidence{}; Decision nested{Decision::SuppressEnd}; };
    inline RestartResult Restart(void* p, const Evidence& e) {
        auto& m = *static_cast<Mock*>(p); ++m.calls;
        if (m.guard) m.nested = m.guard->OnEnd(e, p, Restart);
        return m.result;
    }
    inline RestartResult Throws(void*, const Evidence&) { throw 1; }
    inline void Test()
    {
        auto check = [](bool ok, const char* s) { StageRuntimeHarness::Check(ok, s); };
        check(TerminalEvidence(true, 155, 150, false, -1), "terminal callback accepts clock overshoot");
        check(TerminalEvidence(true, 0, 150, true, 0.2), "fresh pre-end evidence covers a reset clock");
        check(!TerminalEvidence(true, 0, 150, true, 4) && !TerminalEvidence(true, 20, 150, false, 0),
            "stale end evidence and early exit do not qualify for replay");
		Reentry reentry;
		check(reentry.Begin(2) == Entry::Entered && reentry.Begin(2) == Entry::NestedSame &&
		      reentry.Begin(3) == Entry::NestedOther, "only the same presenter's recursive callback is deferred");
		reentry.End(); check(reentry.Begin(3) == Entry::Entered, "reentry guard releases after the outer call");
        Evidence e{1, 2, 3, true, false, false, false, false};
        Guard guard; Mock m{}; m.evidence = e; m.result = {true, true, true, true, 0.02f};
        check(!guard.Arm({1, 2, 3, true, true, false, false, false}), "guard never arms after end begins");
        check(guard.Arm(e), "end-loop guard arms only for a confirmed Live owner");
        check(guard.OnEnd(e, &m, Restart) == Decision::ForwardEnd && !m.calls, "ordinary updates never suppress end");
        e.ending = true; m.guard = &guard;
        check(guard.OnEnd(e, &m, Restart) == Decision::SuppressEnd && m.calls == 1 && m.nested == Decision::ForwardEnd,
            "verified restart alone suppresses the end; recursion forwards");
        m.guard = nullptr; m.result.observed = false;
        check(guard.OnEnd(e, &m, Restart) == Decision::ForwardEnd && guard.Faulted(),
            "unobserved seek forwards end and disables the guard");
        check(guard.OnEnd(e, &m, Restart) == Decision::ForwardEnd && m.calls == 2,
            "failed restart cannot repeatedly suppress end");
        Guard stale; check(stale.Arm({1, 2, 3, true, false, false, false, false}), "second guard arms");
        e.owner = 8;
        check(stale.OnEnd(e, &m, Restart) == Decision::ForwardEnd && m.calls == 2,
            "another owner always forwards the end");
        e.owner = 1; e.captureBusy = true;
        check(stale.OnEnd(e, &m, Restart) == Decision::ForwardEnd, "capture busy forwards the end");
        e.captureBusy = false; m.result = {true, true, true, true, 100.0f};
        check(stale.OnEnd(e, &m, Restart) == Decision::ForwardEnd && stale.Faulted(),
            "seek dispatched but time not rewound must forward the end");
        Guard exception; check(exception.Arm({1, 2, 3, true, false, false, false, false}), "exception case arms");
        check(exception.OnEnd(e, nullptr, Throws) == Decision::ForwardEnd && exception.Faulted(),
            "restart exception must never suppress the end");
    }
}
