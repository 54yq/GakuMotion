#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include "GakuPlugin.h"
#include "GkExactMethod.hpp"
#include "GkLiveEndLoopContract.hpp"
namespace GakuMotion::host { inline bool active{}; inline GakuHookService hooks{}; }
namespace GroundShadowProbeBootstrap { inline void* MethodPointer(const void*) { return nullptr; } }
struct TestLog { void (*logInfo)(const char*, ...){}; } g_hostContext;
#include "GkLiveSchedulingProbe.hpp"
#define CHECK(x) do { if (!(x)) { std::printf("FAIL: %s (line %d)\n", #x, __LINE__); return 1; } } while (0)
static int deltaCalls{}, plainCalls{}, boolCalls{}, voidCalls{}, callbackCalls{};
static void Delta(void*, float, const void*) { ++deltaCalls; }
static void Plain(void*, const void*) { ++plainCalls; }
static bool CheckBool(void*, float, const void*) { ++boolCalls; return true; }
static void CheckVoid(void*, float, const void*) { ++voidCalls; }
static void Callback(void*, const void*) { ++callbackCalls; }
static bool Suppress(void*) { return true; }
static bool Forward(void*) { return false; }
int main()
{
    using namespace LiveSchedulingProbe;
    originalEvalDelta = Delta; originalEvalPlain = Plain; originalEndBool = CheckBool;
    originalEndVoid = CheckVoid; originalEndCallback = Callback;
    void* actor = reinterpret_cast<void*>(0x1000); void* live = reinterpret_cast<void*>(0x2000);
    SetTargets(actor, live, 0);
    EvalDeltaDetour(actor, 0.02f, nullptr); EvalPlainDetour(actor, nullptr);
    EvalDeltaDetour(live, 0.03f, nullptr);
    CHECK(deltaCalls == 2 && plainCalls == 1 && evalDelta[0] == 1 && evalPlain[0] == 1);
    CHECK(Float(lastDeltaBits.load()) == 0.02f);
    CHECK(EndBoolDetour(live, 142.0f, nullptr)); EndVoidDetour(live, 143.0f, nullptr);
    EndCallbackDetour(live, nullptr); EndCallbackDetour(actor, nullptr);
    CHECK(boolCalls == 1 && voidCalls == 1 && callbackCalls == 2 && checks[0] == 2 && ends[0] == 1);
    CHECK(Float(lastCheckBits.load()) == 143.0f);
    SetTargets(actor, live, 1); EvalDeltaDetour(actor, 0.01f, nullptr);
    SetTargets(actor, live, 2); EvalPlainDetour(actor, nullptr); EndCallbackDetour(live, nullptr);
    SetTargets(nullptr, nullptr, 3); EvalDeltaDetour(actor, 0.1f, nullptr); EndCallbackDetour(live, nullptr);
    CHECK(evalDelta[1] == 1 && evalPlain[2] == 1 && ends[2] == 1 && deltaCalls == 4 && callbackCalls == 4);
    CHECK(checks[1] == 0 && checks[2] == 0 && ends[1] == 0);
    SetTargets(actor, live, 0); BeginTiming(); EvalDeltaDetour(actor, 0.02f, nullptr); StopTiming();
    CHECK(timingCount == 1 && evalTimings[0].epoch == timingEpoch && evalTimings[0].start > 0 &&
        evalTimings[0].end >= evalTimings[0].start && evalTimings[0].thread == GetCurrentThreadId());
    SetTargets(actor, live, 0); endInterceptor.store(Suppress);
    EndCallbackDetour(live, nullptr); CHECK(callbackCalls == 5 && ends[0] == 2);
    endInterceptor.store(Forward); EndCallbackDetour(live, nullptr); CHECK(callbackCalls == 6 && ends[0] == 3);
    SetTargets(actor, live, 1); endInterceptor.store(Suppress); EndCallbackDetour(live, nullptr);
    CHECK(callbackCalls == 7 && ends[1] == 1);
    endInterceptor.store(nullptr);
    std::puts("Live scheduling probes: target filtering, phase counters and exactly-once forwarding passed.");
}
