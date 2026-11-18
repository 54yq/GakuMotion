#pragma once
#include "GkLiveApi.hpp"
#include <cmath>
#include <cstdint>

namespace GakuMotion::live_takeover
{
    struct RuntimeAccess
    {
        void* (*objectClass)(void*){};
        void* (*findField)(void*, const char*){};
        void* (*fieldType)(void*){};
        uint32_t (*fieldFlags)(void*){};
        int (*typeCode)(void*){};
        void* (*classFromType)(void*){};
        void (*readField)(void*, void*, void*){};
        void* (*invoke)(const void*, void*, void**, void**){};
        void* (*unbox)(void*){};
    };

    struct Observation
    {
        bool confirmed{};
        bool paused{};
        float time{};
        float duration{};
    };
    struct EndInspection
    {
        bool identity{}, execution{}, pauseKnown{}, timeKnown{}, durationKnown{}, paused{};
        float time{}, duration{};
    };

    inline void* SceneField(const Api& api, const RuntimeAccess& a, void* scene,
                            const char* name, void* expected)
    {
        if (!scene || !expected || !a.objectClass || !a.findField || !a.fieldType ||
            !a.fieldFlags || !a.typeCode || !a.classFromType || !a.readField ||
            a.objectClass(scene) != api.sceneClass) return nullptr;
        void* field = a.findField(api.sceneClass, name);
        void* type = field ? a.fieldType(field) : nullptr;
        if (!type || (a.fieldFlags(field) & 0x10) || a.typeCode(type) != 0x12 ||
            a.classFromType(type) != expected) return nullptr;
        void* value = nullptr;
        a.readField(scene, field, &value);
        return value && a.objectClass(value) == expected ? value : nullptr;
    }

    // End callbacks can run after ordinary timeline validity has expired.
    // Preserve each getter result independently so a reset/overshoot is visible.
    inline EndInspection InspectEnd(const Api& api, const RuntimeAccess& a, void* scene, void* presenter)
    {
        EndInspection out;
        if (!api.Ready() || !a.invoke || !a.unbox || !a.objectClass || !presenter) return out;
        void* live = SceneField(api, a, scene, "_live", api.presenterClass);
        void* player = SceneField(api, a, scene, "_livePlayer", api.playerClass);
        out.identity = live == presenter && player;
        if (!out.identity) return out;
        void* ex = nullptr; void* execution = a.invoke(api.execution, player, nullptr, &ex);
        out.execution = execution && !ex && a.objectClass(execution) == api.executionClass;
        auto raw = [&](const void* method, void* instance) -> void* {
            void* error = nullptr; void* boxed = a.invoke(method, instance, nullptr, &error);
            return boxed && !error ? a.unbox(boxed) : nullptr;
        };
        if (out.execution) if (void* p = raw(api.isPaused, execution)) { out.pauseKnown = true; out.paused = *static_cast<uint8_t*>(p) != 0; }
        if (void* p = raw(api.currentTime, player)) { out.timeKnown = true; out.time = *static_cast<float*>(p); }
        if (void* p = raw(api.duration, player)) { out.durationKnown = true; out.duration = *static_cast<float*>(p); }
        return out;
    }
    // Only from a verified, armed end callback. No ordinary timeline-time
    // precondition is imposed; identity, execution and pause remain required.
    inline bool SeekAtEnd(const Api& api, const RuntimeAccess& a, void* scene, void* presenter)
    {
        const auto s = InspectEnd(api, a, scene, presenter);
        if (!s.identity || !s.execution || !s.pauseKnown || s.paused || !s.durationKnown ||
            !std::isfinite(s.duration) || s.duration <= 0) return false;
        float zero = 0; void* args[]{&zero}; void* exception = nullptr;
        a.invoke(api.seek, presenter, args, &exception); return !exception;
    }

    // Runs on the Unity thread with a freshly confirmed Live scene presenter.
    // Returns copied values only; it never retains a managed object or changes
    // playback. Any unexpected metadata, exception or value is rejected.
    inline Observation Observe(const Api& api, const RuntimeAccess& a, void* scene)
    {
        if (!api.Ready() || !scene || !a.objectClass || !a.findField || !a.fieldType ||
            !a.fieldFlags || !a.typeCode || !a.classFromType || !a.readField ||
            !a.invoke || !a.unbox || a.objectClass(scene) != api.sceneClass) return {};
        void* live = SceneField(api, a, scene, "_live", api.presenterClass);
        void* player = SceneField(api, a, scene, "_livePlayer", api.playerClass);
        if (!live || !player) return {};
        auto call = [&](const void* method, void* instance) -> void* {
            void* exception = nullptr;
            void* result = a.invoke(method, instance, nullptr, &exception);
            return exception ? nullptr : result;
        };
        void* execution = call(api.execution, player);
        if (!execution || a.objectClass(execution) != api.executionClass) return {};
        void* boxed = call(api.isPaused, execution);
        auto* pauseValue = boxed ? static_cast<const uint8_t*>(a.unbox(boxed)) : nullptr;
        if (!pauseValue) return {};
        const bool paused = *pauseValue != 0;
        boxed = call(api.currentTime, player);
        auto* timeValue = boxed ? static_cast<const float*>(a.unbox(boxed)) : nullptr;
        if (!timeValue) return {};
        const float time = *timeValue;
        boxed = call(api.duration, player);
        auto* durationValue = boxed ? static_cast<const float*>(a.unbox(boxed)) : nullptr;
        if (!durationValue) return {};
        const float duration = *durationValue;
        if (!std::isfinite(time) || !std::isfinite(duration) || time < 0 || duration <= 0 ||
            time > duration + 1.0f) return {};
        return {true, paused, time, duration};
    }

    // Only a user command on the Unity thread may call this. The caller must
    // independently verify that scene still matches the command's owner token.
    inline bool SetPaused(const Api& api, const RuntimeAccess& a, void* scene, bool desired)
    {
        const auto before = Observe(api, a, scene);
        if (!before.confirmed) return false;
        if (before.paused == desired) return true;
        void* presenter = SceneField(api, a, scene, "_live", api.presenterClass);
        if (!presenter) return false;
        void* exception = nullptr;
        a.invoke(desired ? api.pause : api.resume, presenter, nullptr, &exception);
        if (exception) return false;
        const auto after = Observe(api, a, scene);
        return after.confirmed && after.paused == desired;
    }

    // Reports dispatch only: the game may update asynchronously. The caller
    // must confirm ownership and observe the resulting time independently.
    inline bool Seek(const Api& api, const RuntimeAccess& a, void* scene, float seconds)
    {
        if (!std::isfinite(seconds) || !api.seek || !a.invoke) return false;
        const auto before = Observe(api, a, scene);
        if (!before.confirmed || seconds < 0.0f || seconds >= before.duration) return false;
        void* presenter = SceneField(api, a, scene, "_live", api.presenterClass);
        if (!presenter) return false;
        void* args[]{&seconds}; void* exception = nullptr;
        a.invoke(api.seek, presenter, args, &exception);
        return !exception;
    }
}
