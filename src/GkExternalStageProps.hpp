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
        bool includeInactive = false;
        void* args[]{StageLoader::goType, &includeInactive};
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

    struct GripPose { UnityVector3 position; UnityQuaternion rotation; };

    inline std::optional<GripPose> ReadGripPose(int selection, GakuMotion::MotionPlayer& player, bool left,
        float throwFrame, const GakuMotion::stage_prop_timeline::Cue* cue)
    {
        auto& controller = player.controller;
        void* actor = controller.targetActor;
        if (!actor || !controller.fnGetBoneTransform) return std::nullopt;
        void* object = StageLoader::Invoke(StageLoader::componentGo, actor);
        if (!object || StageLoader::SceneOfFreshGo(object) != owner.sceneHandle) return std::nullopt;
        void* hand = controller.fnGetBoneTransform(actor, left ? 17 : 18);
        if (!hand) return std::nullopt;
        auto position = g_getWorldPosNative(hand);
        UnityVector3 gripDirection{};
        if (void* finger = controller.fnGetBoneTransform(actor, left ? 27 : 42))
        {
            const auto grip = g_getWorldPosNative(finger);
            gripDirection = {grip.x - position.x, grip.y - position.y, grip.z - position.z};
            position = {(position.x + grip.x) * 0.5f, (position.y + grip.y) * 0.5f, (position.z + grip.z) * 0.5f};
        }
        auto grip = g_getWorldRotNative(hand);
        const float launch = ExternalStageProfiles::meniDogThrow[0].frame;
        if (throwFrame >= launch)
        {
            const auto world = RotateStageOffset(SampleThrow(throwFrame));
            const float t = std::clamp((throwFrame - launch) / 3.f, 0.f, 1.f);
            position = {std::lerp(position.x, stageAnchor.x + world.x, t),
                        std::lerp(position.y, stageAnchor.y + world.y, t),
                        std::lerp(position.z, stageAnchor.z + world.z, t)};
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
        else
        {
            // The plush pivot is at its feet; a wrist roll must not tip it over.
            const float horizontal = std::hypot(gripDirection.x, gripDirection.z);
            const float yaw = horizontal > 0.01f ? std::atan2(gripDirection.x, gripDirection.z) : stageYaw;
            grip = {0, std::sin(yaw * .5f), 0, std::cos(yaw * .5f)};
            if (selection != 1) grip = {0, grip.w, 0, -grip.y};
            if (selection == 1 && throwFrame < launch)
            {
                if (void* other = controller.fnGetBoneTransform(actor, left ? 18 : 17))
                {
                    const auto support = g_getWorldPosNative(other);
                    if (std::hypot(support.x-position.x, support.y-position.y, support.z-position.z) < 0.42f)
                    {
                        position.x = std::lerp(position.x, support.x, 0.35f);
                        position.z = std::lerp(position.z, support.z, 0.35f);
                    }
                }
            }
            if (throwFrame < launch) position.y -= selection >= 5 && selection <= 7 ? 0.11f : 0.09f;
        }
        if (cue)
        {
            using GakuMotion::Quaternion;
            const Quaternion basis{grip.x, grip.y, grip.z, grip.w};
            const auto offset = basis.Rotate(cue->position);
            position = {position.x + offset.x, position.y + offset.y, position.z + offset.z};
            const auto adjusted = (basis * Quaternion::FromEulerDegreesXYZ(
                cue->rotation.x, cue->rotation.y, cue->rotation.z)).Normalized();
            grip = {adjusted.x, adjusted.y, adjusted.z, adjusted.w};
        }
        return GripPose{position, grip};
    }

    inline void ApplyCameraPropHold(GripPose& pose, const GakuMotion::CameraController& camera, float frame)
    {
        const auto& hold = ExternalStageProfiles::meniRightCrepeHold;
        if (!camera.owned || camera.settings.mode != GakuMotion::CameraMode::Track ||
            frame < hold.releaseStart || frame >= hold.exitEnd) return;

        const float amount = std::clamp((frame - hold.releaseStart) / (hold.releaseEnd - hold.releaseStart), 0.f, 1.f);
        const float blend = amount * amount * (3.f - 2.f * amount);
        const auto& view = camera.desired;
        const float exit = std::clamp((frame - hold.exitStart) / (hold.exitEnd - hold.exitStart), 0.f, 1.f);
        const float verticalRadius = (view.flags & 1u) ? view.orthographicSize :
            hold.cameraOffset.z * std::tan(view.fov * 0.00872664626f);
        const float exitBlend = exit * exit * (3.f - 2.f * exit);
        const auto anchor = view.position + view.rotation.Rotate(
            {(hold.cameraOffset.x - 5.f * exitBlend) * verticalRadius,
             hold.cameraOffset.y * verticalRadius, hold.cameraOffset.z});
        pose.position = {std::lerp(pose.position.x, anchor.x, blend),
            std::lerp(pose.position.y, anchor.y, blend), std::lerp(pose.position.z, anchor.z, blend)};

        const auto facing = view.rotation.Rotate({0.f, 0.f, 1.f});
        const float yaw = std::atan2(facing.x, facing.z) * 57.2957795131f;
        using GakuMotion::Quaternion;
        const auto parked = Quaternion::FromEulerDegreesXYZ(0.f, yaw, 0.f) *
            Quaternion::FromEulerDegreesXYZ(-90.f, 0.f, 0.f);
        const auto& r = pose.rotation;
        const auto q = Quaternion::Slerp({r.x, r.y, r.z, r.w}, parked, blend);
        pose.rotation = {q.x, q.y, q.z, q.w};
    }

    inline void PlaceHeldProp(HeldProp& prop, int selection, GakuMotion::MotionPlayer& player,
        bool left, float weight, float throwFrame, const GakuMotion::stage_prop_timeline::Cue* cue,
        GakuMotion::MotionPlayer* outgoing, const GakuMotion::stage_prop_timeline::Cue* outgoingCue,
        const GakuMotion::CameraController& camera, float stageFrame)
    {
        auto target = ReadGripPose(selection, player, left, throwFrame, cue);
        if (!target) { ReleaseHeldProp(prop); return; }
        const bool rightCrepe = cue && !left && cue->place == ExternalStageProfiles::meniRightCrepeHold.place &&
            (selection == 10 || selection == extraCrepe);
        if (rightCrepe) ApplyCameraPropHold(*target, camera, stageFrame);
        if (!prop.transform && !BindHeldProp(prop, selection)) return;
        void* transform = SceneOwnerRuntime::handleTarget(prop.transform);
        if (!transform) return;

        const bool legacyReturn = cue && selection == extraCrepe && cue->place == 4 && cue->first == 1690 && cue->last == 1710;
        const bool consumed = (rightCrepe || legacyReturn) &&
            stageFrame >= ExternalStageProfiles::meniRightCrepeHold.exitEnd;
        if (consumed)
        {
            auto parked = DisplayPosition(prop);
            parked.y = std::min(parked.y, stageAnchor.y - 4.f);
            g_setWorldPosNative(transform, parked);
            return;
        }

        auto display = target->position;
        auto r = target->rotation;
        if (outgoingCue)
        {
            const auto source = outgoing ? ReadGripPose(selection, *outgoing, outgoingCue->left, -1.f, outgoingCue)
                : std::nullopt;
            if (source)
            {
                display = source->position;
                r = source->rotation;
            }
            else weight = 1.f;
        }
        const auto& position = target->position;
        auto grip = target->rotation;
        g_setWorldPosNative(transform, {std::lerp(display.x, position.x, weight),
            std::lerp(display.y, position.y, weight), std::lerp(display.z, position.z, weight)});
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
            if (!onlyActor)
            {
                core.stagePropPreview.reset();
                ReleaseHeldProp();
            }
            return;
        }
        struct Grip { MotionPlayer* player{}; bool left{}; float weight{}; const stage_prop_timeline::Cue* cue{}; MotionPlayer* outgoing{}; const stage_prop_timeline::Cue* outgoingCue{}; };
        std::array<Grip, extraCrepe> grips{};
        const float time = externalStageTime.load();
        const int manual = core.stagePropPreview ? 0 : externalStageHeldProp.load();
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
            if (time >= start && time < end)
                grips[manual-1] = {&core.player, externalStageLeftHand.load(), 1.f};
        }
        else if ((follows || core.stagePropPreview) && primaryPlace)
        {
            std::array<MotionPlayer*, 11> players{};
            players[primaryPlace] = &core.player;
            for (const auto& follower : core.ensemble.Followers())
                if (follower.active && follower.player)
                    if (int place = MotionPlace(follower.player->currentMotionDir); place && !players[place])
                        players[place] = follower.player.get();
            const float frame = time * 30.0f;
            const auto& cues = core.stagePropPreview ? *core.stagePropPreview : core.stagePropCues;
            constexpr int handoffGap = 2;
            constexpr float handoffBlend = 10.f;
            for (const auto& cue : cues)
            {
                if (!players[cue.place] || frame < cue.first || frame >= cue.last) continue;
                auto& grip = grips[cue.prop-1];
                grip = {players[cue.place], cue.left, 1.f, &cue};
                const auto prior = std::find_if(cues.begin(), cues.end(), [&](const auto& other) {
                    return other.prop == cue.prop && other.last <= cue.first && cue.first - other.last <= handoffGap;
                });
                const float elapsed = frame - cue.first;
                if (prior != cues.end() && elapsed < handoffBlend)
                {
                    const float t = elapsed / handoffBlend;
                    grip.weight = t*t*(3-2*t);
                    grip.outgoing = players[prior->place];
                    grip.outgoingCue = &*prior;
                }
            }
            for (const auto& cue : cues)
            {
                auto& grip = grips[cue.prop-1];
                if (grip.player || !players[cue.place] || frame < cue.last || frame >= cue.last + handoffGap) continue;
                const auto next = std::find_if(cues.begin(), cues.end(), [&](const auto& other) {
                    return other.prop == cue.prop && other.first >= cue.last && other.first - cue.last <= handoffGap &&
                        frame < other.first && players[other.place];
                });
                if (next != cues.end()) grip = {players[cue.place], cue.left, 1.f, &cue};
            }
        }
        for (size_t i = 0; i < grips.size(); ++i)
        {
            const auto& grip = grips[i];
            if (grip.player && onlyActor && grip.player->controller.targetActor == onlyActor)
                PlaceHeldProp(heldProps[i], static_cast<int>(i)+1, *grip.player, grip.left, grip.weight, grip.cue && i == 0 && grip.cue->place == 1 ? time * 30.0f : -1.0f, grip.cue, grip.outgoing, grip.outgoingCue, core.camera, time * 30.0f);
            else if (!onlyActor && (!grip.player || !grip.player->controller.targetActor)) ReleaseHeldProp(heldProps[i]);
        }
    }
}
