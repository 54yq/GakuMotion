#pragma once

namespace ExternalStage
{
    constexpr int extraCrepe = 11;
    struct HeldProp
    {
        Handle transform{};
        UnityVector3 position{}, travel{};
        UnityQuaternion rotation{};
    };
    inline std::array<HeldProp, extraCrepe> heldProps{};

    inline UnityVector3 DisplayPosition(const HeldProp& prop)
    {
        return {prop.position.x + stageTravel.x - prop.travel.x, prop.position.y,
                prop.position.z + stageTravel.z - prop.travel.z};
    }

    inline void ReleaseHeldProp(HeldProp& prop)
    {
        if (!prop.transform) return;
        if (void* transform = SceneOwnerRuntime::handleTarget(prop.transform))
        {
            g_setWorldPosNative(transform, DisplayPosition(prop));
            g_setWorldRotNative(transform, prop.rotation);
        }
        StageLoader::freeHandle(prop.transform);
        prop = {};
    }

    inline void ReleaseHeldProp()
    {
        for (auto& prop : heldProps) ReleaseHeldProp(prop);
    }

    inline bool BindHeldProp(HeldProp& prop, int selection)
    {
        bool inactive = false;
        void* args[]{StageLoader::goType, &inactive};
        void* list = StageLoader::Invoke(StageLoader::findAll, nullptr, args);
        Handle pin = list ? StageLoader::newHandle(list, false) : nullptr;
        if (!pin) return false;
        const auto name = selection == extraCrepe ? std::string("StageProp_pfb_toon_prop1108_00_B") :
            std::string("StageProp_") + std::string(profile.propNames[selection - 1]);
        for (uintptr_t i = 0, count = StageLoader::arrayLength(list); i < count; ++i)
        {
            auto** items = reinterpret_cast<void**>(static_cast<char*>(SceneOwnerRuntime::handleTarget(pin)) + 32);
            void* object = items[i];
            if (!object || StageLoader::SceneOfFreshGo(object) != mountedScene ||
                Il2CppStringToUtf8(StageLoader::Invoke(StageLoader::objectName, object)) != name) continue;
            if (void* transform = StageLoader::Invoke(StageLoader::goTransform, object))
            {
                prop.transform = StageLoader::newHandle(transform, false);
                prop.position = g_getWorldPosNative(transform);
                prop.travel = stageTravel;
                prop.rotation = g_getWorldRotNative(transform);
            }
            break;
        }
        StageLoader::freeHandle(pin);
        if (prop.transform) GakuMotion::host::Info("[GakuMotion][ExternalStage] hand prop=%s bound=1", name.c_str());
        return !!prop.transform;
    }

    inline int MotionPlace(std::string_view directory)
    {
        const auto slash = directory.find_last_of("/\\");
        const auto name = slash == std::string_view::npos ? directory : directory.substr(slash + 1);
        constexpr std::string_view prefix = "cutt_son1156_Place";
        if (!name.starts_with(prefix) || name.size() != prefix.size() + 2) return 0;
        const char a = name[prefix.size()], b = name[prefix.size() + 1];
        if (a < '0' || a > '1' || b < '0' || b > '9') return 0;
        const int place = (a - '0') * 10 + b - '0';
        return place >= 1 && place <= 10 ? place : 0;
    }

    inline UnityVector3 SampleThrow(float frame)
    {
        const auto& keys = ExternalStageProfiles::meniDogThrow;
        const auto* end = std::end(keys);
        const auto* next = std::upper_bound(std::begin(keys), end, frame,
            [](float f, const auto& key) { return f < key.frame; });
        if (next == std::begin(keys)) return {};
        if (next == end) return (end - 1)->offset;
        const auto* previous = next - 1;
        const float t = (frame - previous->frame) / (next->frame - previous->frame);
        return {std::lerp(previous->offset.x, next->offset.x, t),
            std::lerp(previous->offset.y, next->offset.y, t),
            std::lerp(previous->offset.z, next->offset.z, t)};
    }

    inline void PlaceHeldProp(HeldProp& prop, int selection, GakuMotion::MotionPlayer& player, bool left, float weight, float throwFrame)
    {
        auto& controller = player.controller;
        void* actor = controller.targetActor;
        if (!actor || !controller.fnGetBoneTransform) { ReleaseHeldProp(prop); return; }
        void* object = StageLoader::Invoke(StageLoader::componentGo, actor);
        if (!object || StageLoader::SceneOfFreshGo(object) != owner.sceneHandle) { ReleaseHeldProp(prop); return; }
        void* hand = controller.fnGetBoneTransform(actor, left ? 17 : 18);
        if (!hand) { ReleaseHeldProp(prop); return; }
        if (!prop.transform && !BindHeldProp(prop, selection)) return;
        void* transform = SceneOwnerRuntime::handleTarget(prop.transform);
        if (!transform) return;
        auto position = g_getWorldPosNative(hand);
        UnityVector3 gripDirection{};
        if (void* finger = controller.fnGetBoneTransform(actor, left ? 27 : 42))
        {
            const auto grip = g_getWorldPosNative(finger);
            gripDirection = {grip.x - position.x, grip.y - position.y, grip.z - position.z};
            position = {(position.x + grip.x) * 0.5f, (position.y + grip.y) * 0.5f, (position.z + grip.z) * 0.5f};
        }
        auto grip = g_getWorldRotNative(hand);
        if (throwFrame >= 0)
        {
            const auto delta = SampleThrow(throwFrame);
            const auto world = GakuMotion::Quaternion{grip.x, grip.y, grip.z, grip.w}.Rotate({delta.x, delta.y, delta.z});
            position.x += world.x;
            position.y += world.y;
            position.z += world.z;
        }
        if (selection == 10 || selection == extraCrepe)
        {
            // The crepe's long axis is model +Z. Keep it upright on both palms.
            const float horizontal = std::hypot(gripDirection.x, gripDirection.z);
            const float yaw = horizontal > 0.01f ? std::atan2(gripDirection.x, gripDirection.z) : stageYaw;
            if (horizontal > 0.01f)
            {
                position.x += gripDirection.x * (0.045f / horizontal);
                position.z += gripDirection.z * (0.045f / horizontal);
            }
            position.y += 0.02f;
            constexpr float halfRoot = 0.70710678118f;
            const float s = std::sin(yaw * 0.5f) * halfRoot;
            const float c = std::cos(yaw * 0.5f) * halfRoot;
            grip = {-c, s, s, c};
        }
        const auto display = DisplayPosition(prop);
        g_setWorldPosNative(transform, {std::lerp(display.x, position.x, weight),
            std::lerp(display.y, position.y, weight), std::lerp(display.z, position.z, weight)});
        const auto& r = prop.rotation;
        if (r.x * grip.x + r.y * grip.y + r.z * grip.z + r.w * grip.w < 0)
            grip = {-grip.x, -grip.y, -grip.z, -grip.w};
        UnityQuaternion q{std::lerp(r.x, grip.x, weight), std::lerp(r.y, grip.y, weight),
            std::lerp(r.z, grip.z, weight), std::lerp(r.w, grip.w, weight)};
        const float norm = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
        if (norm > 0) g_setWorldRotNative(transform, {q.x/norm, q.y/norm, q.z/norm, q.w/norm});
    }

    inline void UpdateHeldProps(GakuMotion::Core& core, void* onlyActor = nullptr)
    {
        using namespace GakuMotion;
        if (status.load() != 1 || !mountedScene || !StageLoader::goType ||
            !g_getWorldPosNative || !g_getWorldRotNative || !g_setWorldPosNative || !g_setWorldRotNative)
        {
            if (!onlyActor) ReleaseHeldProp();
            return;
        }
        struct Grip { MotionPlayer* player{}; bool left{}; float weight{}; };
        std::array<Grip, extraCrepe> grips{};
        const float time = externalStageTime.load();
        const int manual = externalStageHeldProp.load();
        const int primaryPlace = MotionPlace(core.player.currentMotionDir);
        const bool follows = externalStageFollow.load();
        if (!onlyActor)
        {
            static int lastScene{}, lastPlace{}, lastManual{};
            static bool lastFollow{};
            if (lastScene != mountedScene || lastPlace != primaryPlace ||
                lastManual != manual || lastFollow != follows)
            {
                GakuMotion::host::Info("[GakuMotion][ExternalStage] prop cue mode scene=%d place=%d follow=%d manual=%d stage_time=%.3f motion_time=%.3f",
                    mountedScene, primaryPlace, follows, manual, time, core.player.currentTime);
                lastScene = mountedScene; lastPlace = primaryPlace;
                lastManual = manual; lastFollow = follows;
            }
        }
        if (manual > 0 && manual <= static_cast<int>(profile.propNames.size()))
        {
            const float start = externalStagePropStart.load(), end = externalStagePropEnd.load();
            const float transition = std::min(0.20f, (end-start) * 0.5f);
            if (transition > 0 && time >= start && time < end)
            {
                const float t = std::min({1.0f, (time-start)/transition, (end-time)/transition});
                grips[manual-1] = {&core.player, externalStageLeftHand.load(), t*t*(3-2*t)};
            }
        }
        else if (follows && primaryPlace)
        {
            std::array<MotionPlayer*, 11> players{};
            players[primaryPlace] = &core.player;
            for (const auto& follower : core.ensemble.Followers())
                if (follower.active && follower.player)
                    if (int place = MotionPlace(follower.player->currentMotionDir); place && !players[place])
                        players[place] = follower.player.get();
            const float frame = time * 30.0f;
            for (const auto& cue : ExternalStageProfiles::meniPropCues)
                if (frame >= cue.first && frame < cue.last && players[cue.place])
                {
                    const float t = cue.prop == 1 ? std::clamp((frame - cue.first) / 9.0f, 0.0f, 1.0f) : 1.0f;
                    grips[cue.prop-1] = {players[cue.place], cue.left, t*t*(3-2*t)};
                }
        }
        for (size_t i = 0; i < grips.size(); ++i)
        {
            const auto& grip = grips[i];
            if (grip.player && (!onlyActor || grip.player->controller.targetActor == onlyActor))
                PlaceHeldProp(heldProps[i], static_cast<int>(i)+1, *grip.player, grip.left, grip.weight, !manual && i == 0 ? time * 30.0f : -1.0f);
            else if (!onlyActor) ReleaseHeldProp(heldProps[i]);
        }
    }
}
