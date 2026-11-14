#pragma once
#include <algorithm>
#include <array>
#include <intrin.h>
// Included in the plugin anonymous namespace. Timing probes forward original
// calls. The end callback is forwarded unless the explicit loop interceptor
// confirms a restart; it is never suppressed by timing observation alone.
namespace LiveSchedulingProbe
{
    using EvalDelta = void (*)(void*, float, const void*);
    using EvalPlain = void (*)(void*, const void*);
    using EndBool = bool (*)(void*, float, const void*);
    using EndVoid = void (*)(void*, float, const void*);
    using EndCallback = void (*)(void*, const void*);
    using EndInterceptor = bool (*)(void*);
    inline EvalDelta originalEvalDelta{};
    inline EvalPlain originalEvalPlain{};
    inline EndBool originalEndBool{};
    inline EndVoid originalEndVoid{};
    inline EndCallback originalEndCallback{};
    inline void *evalDeltaTarget{}, *evalPlainTarget{}, *checkTarget{}, *endTarget{};
    inline std::atomic<uintptr_t> actorPlayer{}, presenter{};
    inline std::atomic<EndInterceptor> endInterceptor{};
    inline std::atomic<int> phase{3}; // 0 playing, 1 paused, 2 takeover, 3 inactive
    inline std::atomic<uint64_t> evalDelta[3]{}, evalPlain[3]{}, checks[3]{}, ends[3]{};
    inline std::atomic<uintptr_t> evalCaller{}, checkCaller{}, endCaller{};
    inline std::atomic<uint32_t> lastDeltaBits{}, lastCheckBits{};
    inline std::atomic<uint64_t> restartAttempts{};
    inline std::atomic<int> restartStatus{}; // 0 none, 1 thread, 2 context, 3 presenter, 4 time, 5 failed, 6 accepted, -1 exception
    struct EvalTiming { std::atomic<uint64_t> epoch{}, start{}, end{}; std::atomic<DWORD> thread{}; };
    inline std::array<EvalTiming, 64> evalTimings{};
    inline std::atomic<bool> timingActive{};
    inline std::atomic<uint64_t> timingEpoch{};
    inline std::atomic<unsigned> timingCount{};
    inline uint64_t Qpc() { LARGE_INTEGER value{}; return QueryPerformanceCounter(&value) ? static_cast<uint64_t>(value.QuadPart) : 0; }
    inline void BeginTiming()
    {
        timingActive.store(false, std::memory_order_release);
        timingEpoch.fetch_add(1, std::memory_order_acq_rel); timingCount.store(0, std::memory_order_relaxed);
        for (auto& row : evalTimings) { row.epoch.store(0); row.start.store(0); row.end.store(0); row.thread.store(0); }
        timingActive.store(true, std::memory_order_release);
    }
    inline void StopTiming() { timingActive.store(false, std::memory_order_release); }
    inline void LogTimings()
    {
        if (!g_hostContext.logInfo) return;
        LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
        const auto epoch = timingEpoch.load(); const auto count = (std::min)(timingCount.load(), static_cast<unsigned>(evalTimings.size()));
        g_hostContext.logInfo("[GakuMotion][LiveEvalTiming] pid=%lu count=%u epoch=%llu qpc_frequency=%lld; matched player, bounded sample", GetCurrentProcessId(), count,
            static_cast<unsigned long long>(epoch), frequency.QuadPart);
        for (unsigned i = 0; i < count; ++i) {
            const auto& row = evalTimings[i]; if (row.epoch.load() != epoch) continue;
            g_hostContext.logInfo("[GakuMotion][LiveEvalTiming] index=%u start_qpc=%llu end_qpc=%llu thread=%lu", i,
                static_cast<unsigned long long>(row.start.load()), static_cast<unsigned long long>(row.end.load()), row.thread.load());
        }
    }
    inline bool evalAttempted{}, endAttempted{};
    inline uint32_t Bits(float value) { uint32_t bits{}; std::memcpy(&bits, &value, sizeof(bits)); return bits; }
    inline float Float(uint32_t bits) { float value{}; std::memcpy(&value, &bits, sizeof(value)); return value; }
    inline int Match(std::atomic<uintptr_t>& expected, void* instance)
    {
        const int p = phase.load(std::memory_order_acquire);
        return p >= 0 && p < 3 && instance && reinterpret_cast<uintptr_t>(instance) == expected.load(std::memory_order_acquire) ? p : -1;
    }
    inline void EvalDeltaDetour(void* self, float dt, const void* method)
    {
        const int p = Match(actorPlayer, self);
        if (p >= 0) { evalDelta[p].fetch_add(1, std::memory_order_relaxed); lastDeltaBits.store(Bits(dt), std::memory_order_relaxed); evalCaller.store(reinterpret_cast<uintptr_t>(_ReturnAddress()), std::memory_order_relaxed); }
        const auto epoch = timingEpoch.load(std::memory_order_acquire);
        const unsigned index = p == 0 && timingActive.load(std::memory_order_acquire) ? timingCount.fetch_add(1) : static_cast<unsigned>(evalTimings.size());
        if (index < evalTimings.size()) {
            auto& row = evalTimings[index]; row.start.store(Qpc()); row.end.store(0); row.thread.store(GetCurrentThreadId()); row.epoch.store(epoch, std::memory_order_release);
        }
        originalEvalDelta(self, dt, method);
        if (index < evalTimings.size() && timingEpoch.load(std::memory_order_acquire) == epoch && evalTimings[index].epoch.load(std::memory_order_acquire) == epoch)
            evalTimings[index].end.store(Qpc(), std::memory_order_release);
    }
    inline void EvalPlainDetour(void* self, const void* method)
    {
        const int p = Match(actorPlayer, self);
        if (p >= 0) { evalPlain[p].fetch_add(1, std::memory_order_relaxed); evalCaller.store(reinterpret_cast<uintptr_t>(_ReturnAddress()), std::memory_order_relaxed); }
        originalEvalPlain(self, method);
    }
    inline bool EndBoolDetour(void* self, float time, const void* method)
    {
        const int p = Match(presenter, self);
        if (p >= 0) { checks[p].fetch_add(1, std::memory_order_relaxed); lastCheckBits.store(Bits(time), std::memory_order_relaxed); checkCaller.store(reinterpret_cast<uintptr_t>(_ReturnAddress()), std::memory_order_relaxed); }
        return originalEndBool(self, time, method);
    }
    inline void EndVoidDetour(void* self, float time, const void* method)
    {
        const int p = Match(presenter, self);
        if (p >= 0) { checks[p].fetch_add(1, std::memory_order_relaxed); lastCheckBits.store(Bits(time), std::memory_order_relaxed); checkCaller.store(reinterpret_cast<uintptr_t>(_ReturnAddress()), std::memory_order_relaxed); }
        originalEndVoid(self, time, method);
    }
    inline void EndCallbackDetour(void* self, const void* method)
    {
        const int p = Match(presenter, self);
        if (p >= 0) { ends[p].fetch_add(1, std::memory_order_relaxed); endCaller.store(reinterpret_cast<uintptr_t>(_ReturnAddress()), std::memory_order_relaxed); }
        const auto interceptor = endInterceptor.load(std::memory_order_acquire);
        if (GakuMotion::live_end_loop::CallbackReplayEnabled && p == 0 && interceptor && interceptor(self)) return;
        originalEndCallback(self, method);
    }
    inline bool Acquire(const void* method, void* detour, void** original, void*& target)
    {
        if (!method || !GakuMotion::host::active || !GakuMotion::host::hooks.acquire) return false;
        void* address = GroundShadowProbeBootstrap::MethodPointer(method);
        if (!address || address == evalDeltaTarget || address == evalPlainTarget || address == checkTarget || address == endTarget) return false;
        const auto rc = GakuMotion::host::hooks.acquire(GakuMotion::host::hooks.context, address, detour, original);
        if (rc != GAKU_OK) return false;
        if (!*original) { if (GakuMotion::host::hooks.release) GakuMotion::host::hooks.release(GakuMotion::host::hooks.context, address); return false; }
        target = address; return true;
    }
    inline void InstallActor(void* motionClass, const GakuMotion::ExactMethodMetadata& metadata)
    {
        if (evalAttempted || !motionClass) return;
        evalAttempted = true;
        const void* delta = metadata.Find(motionClass, "Evaluate", false, {0x01}, {{0x0c}});
        const void* plain = metadata.Find(motionClass, "Evaluate", false, {0x01}, {});
        Acquire(delta, reinterpret_cast<void*>(&EvalDeltaDetour), reinterpret_cast<void**>(&originalEvalDelta), evalDeltaTarget);
        Acquire(plain, reinterpret_cast<void*>(&EvalPlainDetour), reinterpret_cast<void**>(&originalEvalPlain), evalPlainTarget);
        if (g_hostContext.logInfo) g_hostContext.logInfo("[GakuMotion][LiveScheduleHooks] evaluate_float=%d evaluate_noarg=%d; forwarding-only", int(evalDeltaTarget != nullptr), int(evalPlainTarget != nullptr));
    }
    inline void InstallEnd(void* presenterClass, const GakuMotion::ExactMethodMetadata& metadata)
    {
        if (endAttempted || !presenterClass) return;
        endAttempted = true;
        const void* cb = metadata.Find(presenterClass, "OnEndLiveSequence", false, {0x01}, {});
        const void* b = metadata.Find(presenterClass, "CheckLiveEnd", false, {0x02}, {{0x0c}});
        const void* v = metadata.Find(presenterClass, "CheckLiveEnd", false, {0x01}, {{0x0c}});
        if (b && !v) Acquire(b, reinterpret_cast<void*>(&EndBoolDetour), reinterpret_cast<void**>(&originalEndBool), checkTarget);
        if (v && !b) Acquire(v, reinterpret_cast<void*>(&EndVoidDetour), reinterpret_cast<void**>(&originalEndVoid), checkTarget);
        Acquire(cb, reinterpret_cast<void*>(&EndCallbackDetour), reinterpret_cast<void**>(&originalEndCallback), endTarget);
        if (g_hostContext.logInfo) g_hostContext.logInfo("[GakuMotion][LiveScheduleHooks] check_end=%d on_end=%d; end-loop is opt-in", int(checkTarget != nullptr), int(endTarget != nullptr));
    }
    inline void SetTargets(void* player, void* livePresenter, int currentPhase)
    {
        phase.store(3, std::memory_order_release);
        actorPlayer.store(reinterpret_cast<uintptr_t>(player), std::memory_order_release);
        presenter.store(reinterpret_cast<uintptr_t>(livePresenter), std::memory_order_release);
        phase.store(currentPhase >= 0 && currentPhase < 3 ? currentPhase : 3, std::memory_order_release);
    }
    inline void Shutdown()
    {
        StopTiming();
        endInterceptor.store(nullptr, std::memory_order_release);
        SetTargets(nullptr, nullptr, 3);
        for (void* target : {evalDeltaTarget, evalPlainTarget, checkTarget, endTarget})
            if (target && GakuMotion::host::active && GakuMotion::host::hooks.release)
                GakuMotion::host::hooks.release(GakuMotion::host::hooks.context, target);
        evalDeltaTarget = evalPlainTarget = checkTarget = endTarget = nullptr;
        evalAttempted = endAttempted = false;
    }
}
