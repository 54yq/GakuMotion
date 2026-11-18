#pragma once
#include <cstdint>
#include "GkSwingCheckpointReport.hpp"
#include "GkRealtimeCounter.hpp"
namespace GakuMotion
{
    enum class CostumeCaptureStatus { Idle, Requested, Recording, Complete, Interrupted, NoSamples, FileError };

    struct PhysicsMonitor
    {
        unsigned codeCaptureFiles{}, codeCaptureFailures{};
        RealtimeCounter liveEvalMeter,liveSwingMeter;
        bool liveTelemetryActive{},liveTelemetryClockTrial{};
        int liveTelemetryPhase{-1};double liveTelemetryTime{};
        SwingCheckpointReport checkpointReport;
        uint64_t checkpointReportOwner{};
        bool observed{}, live{}, fault{};
        uintptr_t actor{}; uint64_t session{};
        bool graphSampled{}, graphPlayerFound{}, graphGetterFound{};
        bool photoGraphSampled{}, photoGraphPlayerFound{}, photoGraphGetterFound{};
        int photoGraphValid{-1}, photoGraphPlaying{-1}, photoGraphUpdateMode{-1};
        int graphPhase{-1}, graphValid{-1}, graphPlaying{-1}, graphUpdateMode{-1};
        int graphOutputs{-1}, graphActorTarget{-1};
        uint64_t graphOwner{}, graphSession{};
        int liveObserveState{}; // 0 not sampled, 1 observing, 2 completed, 3 interrupted, -1 unavailable
        uint64_t liveObserveOwner{}, liveDeltaCalls{}, liveSwingCalls{};
        float liveObserveSeconds{}, liveTimeStart{}, liveTimeEnd{}, liveSwingMinDt{}, liveSwingMaxDt{};
        bool liveSwingKnown{}, liveDeltaKnown{};
        int liveActorProbeResult{1};
        uint64_t liveActorToken{}, liveActorMatches{}, liveActorOther{}, liveActorBad{};
        bool scheduleEvalDeltaHook{}, scheduleEvalPlainHook{}, scheduleEndCheckHook{}, scheduleEndHook{};
        uint64_t scheduleEvalDelta[3]{}, scheduleEvalPlain[3]{}, scheduleChecks[3]{}, scheduleEnds[3]{};
        float scheduleLastDelta{}, scheduleLastEndTime{};
        uint32_t scheduleTimingSamples{}, scheduleTimingCompleted{};
        int endLoopStatus{}; uint64_t endLoopAttempts{};
        int requested{}, actual{}, publishResult{};
        uint64_t swingCalls{}, matched{}, identitySkips{}, expired{}, failed{}, writes{};
    };
    enum class PhysicsMonitorState { NoSample, Stale, Off, Builtin, Native, Waiting, Fault };
    inline PhysicsMonitorState ClassifyPhysicsMonitor(const PhysicsMonitor& m, bool fresh)
    {
        if (!m.observed) return PhysicsMonitorState::NoSample;
        if (!fresh) return PhysicsMonitorState::Stale;
        if (m.fault || (m.requested == 2 && m.publishResult < 0)) return PhysicsMonitorState::Fault;
        if (m.requested == 0) return PhysicsMonitorState::Off;
        if (m.actual == 2) return PhysicsMonitorState::Native;
        if (m.requested == 2) return PhysicsMonitorState::Waiting;
        return PhysicsMonitorState::Builtin;
    }
}
