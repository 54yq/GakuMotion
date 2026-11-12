#pragma once
#include "GkLiveObservation.hpp"
#include <limits>
#include <string_view>

namespace LiveObservationHarness
{
    using namespace GakuMotion::live_takeover;
    struct Object { void* klass{}; Object* live{}; Object* player{}; };
    inline int sceneClass, presenterClass, playerClass, executionClass;
    inline int pauseMethod, resumeMethod, executionMethod, isPausedMethod, timeMethod, durationMethod, seekMethod;
    struct Field { void* klass; uint32_t flags{}; };
    inline Field liveField{&presenterClass}, playerField{&playerClass};
    inline Object live{&presenterClass}, player{&playerClass}, execution{&executionClass};
    inline Object scene{&sceneClass, &live, &player};
    inline uint8_t paused{};
    inline float time{}, duration{};
    inline const void* throwMethod{};
    inline int invocationCount{};
    inline void* Class(void* obj) { return static_cast<Object*>(obj)->klass; }
    inline void* FindField(void* klass, const char* name)
    {
        if (klass != &sceneClass) return nullptr;
        if (std::string_view(name) == "_live") return &liveField;
        if (std::string_view(name) == "_livePlayer") return &playerField;
        return nullptr;
    }
    inline void* FieldType(void* field) { return field; }
    inline uint32_t FieldFlags(void* field) { return static_cast<Field*>(field)->flags; }
    inline int TypeCode(void*) { return 0x12; }
    inline void* TypeClass(void* type) { return static_cast<Field*>(type)->klass; }
    inline void Read(void* obj, void* field, void* result)
    {
        auto* s = static_cast<Object*>(obj);
        *static_cast<void**>(result) = field == &liveField ? s->live : s->player;
    }
    inline void* Invoke(const void* method, void*, void**, void** exception)
    {
        ++invocationCount;
        if (method == throwMethod) { *exception = &sceneClass; return nullptr; }
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
    }
}
