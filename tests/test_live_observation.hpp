#pragma once
#include "GkLiveObservation.hpp"
#include "GkLiveViewingGate.hpp"
#include "GkLivePlaybackProbeGate.hpp"
#include <limits>
#include <string_view>

namespace LiveObservationHarness
{
    using namespace GakuMotion::live_takeover;
    struct Object { void* klass{}; Object* live{}; Object* player{}; Object* model{}; };
    inline int sceneClass, presenterClass, playerClass, executionClass, modelClass, fixedClass, enumClass;
    inline int pauseMethod, resumeMethod, executionMethod, isPausedMethod, timeMethod, durationMethod, seekMethod;
    inline int fixedMethod, typeMethod, fromType;
    struct Field { void* klass; uint32_t flags{}; };
    inline Field liveField{&presenterClass}, playerField{&playerClass}, modelField{&modelClass};
    inline Object live{&presenterClass}, player{&playerClass}, execution{&executionClass};
    inline Object model{&modelClass}, fixed{&fixedClass};
    inline Object scene{&sceneClass, &live, &player, &model};
    inline uint8_t paused{};
    inline float time{}, duration{};
    inline const void* throwMethod{};
    inline int invocationCount{};
    inline int pauseCalls{}, resumeCalls{};
    inline int seekCalls{};
    inline void* Class(void* obj) { return static_cast<Object*>(obj)->klass; }
    inline void* FindField(void* klass, const char* name)
    {
        if (klass != &sceneClass) return nullptr;
        if (std::string_view(name) == "_live") return &liveField;
        if (std::string_view(name) == "_livePlayer") return &playerField;
        if (std::string_view(name) == "_liveModel") return &modelField;
        return nullptr;
    }
    inline void* FieldType(void* field) { return field; }
    inline uint32_t FieldFlags(void* field) { return static_cast<Field*>(field)->flags; }
    inline int TypeCode(void*) { return 0x12; }
    inline void* TypeClass(void* type) { return static_cast<Field*>(type)->klass; }
    inline void Read(void* obj, void* field, void* result)
    {
        auto* s = static_cast<Object*>(obj);
        *static_cast<void**>(result) = field == &liveField ? s->live : field == &playerField ? s->player : s->model;
    }
    inline void* Invoke(const void* method, void*, void** args, void** exception)
    {
        ++invocationCount;
        if (method == throwMethod) { *exception = &sceneClass; return nullptr; }
        if (method == &pauseMethod) { ++pauseCalls; paused = 1; return nullptr; }
        if (method == &resumeMethod) { ++resumeCalls; paused = 0; return nullptr; }
        if (method == &seekMethod) { ++seekCalls; time = *static_cast<float*>(args[0]); return nullptr; }
        if (method == &fixedMethod) return &fixed;
        if (method == &typeMethod) return &fromType;
        if (method == &executionMethod) return &execution;
        if (method == &isPausedMethod) return &paused;
        if (method == &timeMethod) return &time;
        if (method == &durationMethod) return &duration;
        return nullptr;
    }
    inline void* Unbox(void* obj) { return obj; }
    inline void Test()
    {
        auto check = [](bool ok, const char* what) { StageRuntimeHarness::Check(ok, what); };
        PlaybackProbeGate probeGate;
        check(!probeGate.Observe(1, 2, true, 0, 10.0) && !probeGate.Observe(1, 2, true, 0, 10.6),
            "read-only probe waits while unpaused Live time is stalled");
        check(!probeGate.Observe(1, 2, true, 10, 10.7) && !probeGate.Observe(1, 2, true, 10.2f, 11.0) &&
              probeGate.Observe(1, 2, true, 10.6f, 11.3),
            "read-only probe waits after a seek and starts only with sustained advancement");
        check(!probeGate.Observe(1, 2, false, 10.6f, 11.4) && !probeGate.Observe(1, 2, true, 11, 12),
            "pause resets the advancement evidence");
        check(probeGate.Observe(1, 2, true, 11.6f, 12.6) && !probeGate.Observe(1, 2, true, 11.6f, 13.2),
            "sampling stops if an apparently unpaused timeline stalls");
        Api api{&sceneClass, &presenterClass, &playerClass, &executionClass,
            &pauseMethod, &resumeMethod, &executionMethod, &isPausedMethod,
            &timeMethod, &durationMethod, &seekMethod};
        RuntimeAccess access{Class, FindField, FieldType, FieldFlags, TypeCode, TypeClass, Read, Invoke, Unbox};
        paused = 1; time = 12; duration = 150; invocationCount = 0; throwMethod = nullptr;
        const auto result = Observe(api, access, &scene);
        check(result.confirmed && result.paused && result.time == 12 && result.duration == 150 && invocationCount == 4,
            "fresh Live observation reads exact presenter state without mutations");
        scene.player = nullptr;
        check(!Observe(api, access, &scene).confirmed, "missing Live player is rejected");
        scene.player = &player;
        liveField.flags = 0x10;
        check(!Observe(api, access, &scene).confirmed, "static presenter field is rejected");
        liveField.flags = 0;
        throwMethod = &timeMethod;
        check(!Observe(api, access, &scene).confirmed, "getter exception is rejected");
        throwMethod = nullptr; time = std::numeric_limits<float>::quiet_NaN();
        check(!Observe(api, access, &scene).confirmed, "non-finite playback time is rejected");
        time = 12;
        paused = 0; pauseCalls = resumeCalls = 0;
        check(SetPaused(api, access, &scene, true) && paused && pauseCalls == 1,
            "explicit Live pause invokes the presenter and verifies paused state");
        check(SetPaused(api, access, &scene, true) && pauseCalls == 1,
            "already paused Live does not receive another pause call");
        check(SetPaused(api, access, &scene, false) && !paused && resumeCalls == 1,
            "explicit Live resume invokes and verifies the original presenter");
        throwMethod = &pauseMethod;
        check(!SetPaused(api, access, &scene, true) && !paused && pauseCalls == 1,
            "failed native pause does not report success");
        throwMethod = nullptr;
        seekCalls = 0; paused = 1; time = 12; duration = 150;
        check(Seek(api, access, &scene, 80) && time == 80 && paused && seekCalls == 1,
            "forward seek dispatches without changing pause state");
        check(Seek(api, access, &scene, 4) && time == 4 && paused && seekCalls == 2,
            "backward seek dispatches without changing pause state");
        check(!Seek(api, access, &scene, -1) && !Seek(api, access, &scene, 150) &&
            !Seek(api, access, &scene, std::numeric_limits<float>::quiet_NaN()) && seekCalls == 2,
            "out of range and nonfinite seek targets are rejected");
        throwMethod = &seekMethod;
        check(!Seek(api, access, &scene, 20) && seekCalls == 2, "native seek exception is not reported as success");
        throwMethod = nullptr;
        time = 155; duration = 150; paused = 0;
        check(!Observe(api, access, &scene).confirmed, "ordinary playback still rejects a terminal overshoot");
        const auto terminal = InspectEnd(api, access, &scene, &live);
        check(terminal.identity && terminal.execution && terminal.timeKnown && terminal.time == 155 && terminal.duration == 150,
            "end inspection preserves terminal getter values independently");
        check(SeekAtEnd(api, access, &scene, &live) && time == 0, "verified end seek handles overshoot without ordinary time gating");
        time = 155; paused = 1;
        check(!SeekAtEnd(api, access, &scene, &live) && time == 155, "end seek never overrides an explicit pause");
        paused = 0; throwMethod = &timeMethod;
        const auto missingTime = InspectEnd(api, access, &scene, &live);
        check(!missingTime.timeKnown && missingTime.durationKnown && missingTime.pauseKnown,
            "one failed getter does not hide other end diagnostics");
        throwMethod = nullptr; time = 12;
        ViewingGate gate{&modelClass, &fixedClass, &enumClass, &fixedMethod, &typeMethod, 2, 3, true};
        fromType = 2;
        check(gate.Allows(api, access, &scene), "confirmed PictureBook playback is admitted as a viewing mode");
        fromType = 3;
        check(gate.Allows(api, access, &scene), "confirmed RosterLive playback is admitted as a viewing mode");
        fromType = 1;
        check(!gate.Allows(api, access, &scene), "Produce Live is not admitted for viewing controls");
        throwMethod = &typeMethod;
        check(!gate.Allows(api, access, &scene), "viewing mode getter exception denies control");
        throwMethod = nullptr;
    }
}
