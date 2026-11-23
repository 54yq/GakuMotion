#pragma once

namespace ExternalStage
{
    constexpr int extraCrepe = 11;
    struct HeldProp
    {
        Handle transform{};
        UnityVector3 position{}, travel{};
        UnityQuaternion rotation{};
        bool hidden{};
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
            if (prop.hidden) HelperGameObjectSetActive(StageLoader::Invoke(StageLoader::componentGo, transform), true);
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

    struct GripPose { UnityVector3 position; UnityQuaternion rotation; };

    inline std::optional<GripPose> ReadGripPose(int selection, GakuMotion::MotionPlayer& player, bool left,
        const GakuMotion::stage_prop_timeline::Cue* cue)
    {
        auto& controller = player.controller;
        void* actor = controller.targetActor;
        if (!actor || !controller.fnGetBoneTransform) return std::nullopt;
        void* object = StageLoader::Invoke(StageLoader::componentGo, actor);
        if (!object || StageLoader::SceneOfFreshGo(object) != owner.sceneHandle) return std::nullopt;
        if (cue && cue->bone >= 0)
        {
            void* bone = controller.fnGetBoneTransform(actor, cue->bone);
            if (!bone) return std::nullopt;
            const auto origin = g_getWorldPosNative(bone);
            const auto rotation = g_getWorldRotNative(bone);
            using GakuMotion::Quaternion;
            const Quaternion basis{rotation.x, rotation.y, rotation.z, rotation.w};
            const auto offset = basis.Rotate(cue->position);
            const auto r = (basis * Quaternion::FromEulerDegreesXYZ(cue->rotation.x,
                cue->rotation.y, cue->rotation.z)).Normalized();
            return GripPose{{origin.x + offset.x, origin.y + offset.y, origin.z + offset.z}, {r.x,r.y,r.z,r.w}};
        }
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
            // Seat a feet-pivot plush against the palms. Use the two-hand
            // span for the main dog so wrist roll cannot swing it out of the hold.
            using GakuMotion::Quaternion;
            const auto wrist = g_getWorldRotNative(hand);
            const auto forward = Quaternion{wrist.x, wrist.y, wrist.z, wrist.w}.Rotate({0, 0, 1});
            float dx = gripDirection.x, dz = gripDirection.z;
            if (selection == 1)
                if (void* other = controller.fnGetBoneTransform(actor, left ? 18 : 17))
                {
                    auto support = g_getWorldPosNative(other);
                    if (void* otherFinger = controller.fnGetBoneTransform(actor, left ? 42 : 27))
                    {
                        const auto finger = g_getWorldPosNative(otherFinger);
                        support = {(support.x + finger.x) * .5f, (support.y + finger.y) * .5f, (support.z + finger.z) * .5f};
                    }
                    const float spanX = support.x - position.x, spanZ = support.z - position.z;
                    if (std::hypot(spanX, support.y - position.y, spanZ) < .42f)
                    {
                        position = {(position.x + support.x) * .5f, (position.y + support.y) * .5f,
                            (position.z + support.z) * .5f};
                        if (std::hypot(spanX, spanZ) > .04f)
                        {
                            dx = -spanZ; dz = spanX;
                            if (dx * gripDirection.x + dz * gripDirection.z < 0.f) { dx = -dx; dz = -dz; }
                        }
                    }
                }
            if (std::hypot(dx, dz) < .01f) { dx = forward.x; dz = forward.z; }
            const float yaw = std::hypot(dx, dz) > .01f ? std::atan2(dx, dz) : stageYaw;
            const float facing = yaw + (selection == 1 ? 0.f : 3.14159265f);
            grip = {0, std::sin(facing * .5f), 0, std::cos(facing * .5f)};
            const float inset = selection == 1 ? .045f : .025f;
            position.x += std::sin(yaw) * inset;
            position.z += std::cos(yaw) * inset;
            position.y -= selection >= 5 && selection <= 7 ? .13f : .11f;
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

    inline std::optional<GripPose> ResolveCuePose(int selection, GakuMotion::MotionPlayer* player,
        bool left, const GakuMotion::stage_prop_timeline::Cue* cue,
        const GakuMotion::CameraController& camera, const GakuMotion::TrackTiming& cameraTiming, float frame)
    {
        using namespace GakuMotion;
        using namespace stage_prop_timeline;
        if (!cue) return player ? ReadGripPose(selection, *player, left, nullptr) : std::nullopt;
        Cue sample = *cue;
        if (cue->movement == Movement::Linear || cue->movement == Movement::Smooth || cue->movement == Movement::Throw)
        {
            const float t = Progress(*cue, frame);
            const auto lerp = [t](Vector3 a, Vector3 b) {
                return Vector3{std::lerp(a.x,b.x,t), std::lerp(a.y,b.y,t), std::lerp(a.z,b.z,t)};
            };
            sample.position = lerp(cue->position, cue->endPosition);
            sample.rotation = lerp(cue->rotation, cue->endRotation);
            sample.position.y += ArcOffset(*cue, t);
        }
        if (cue->anchor == Anchor::Grip)
            return player ? ReadGripPose(selection, *player, left, &sample) : std::nullopt;
        Quaternion basis{0,0,0,1};
        Vector3 origin{stageAnchor.x, stageAnchor.y, stageAnchor.z};
        auto offset = sample.position;
        if (cue->anchor == Anchor::Camera || cue->anchor == Anchor::CameraToStage)
        {
            if (!camera.owned) return std::nullopt;
            CameraFrame view = camera.desired;
            if (cue->anchor == Anchor::CameraToStage &&
                !camera.SampleStaticTrack(TrackSourceTime(cue->first / 30.f, cameraTiming), view))
                return std::nullopt;
            origin = view.position;
            basis = view.rotation;
            const float verticalRadius = (view.flags & 1u) ? view.orthographicSize :
                offset.z * std::tan(view.fov * 0.00872664626f);
            offset.x *= verticalRadius;
            offset.y *= verticalRadius;
        }
        auto p = origin + basis.Rotate(offset);
        if (cue->anchor == Anchor::CameraToStage)
        {
            const auto released = StageTravelAtFrame(static_cast<float>(cue->first));
            const auto current = StageTravelAtFrame(frame);
            p = p + Vector3{current.x - released.x, current.y - released.y, current.z - released.z};
        }
        const auto r = (basis * Quaternion::FromEulerDegreesXYZ(
            sample.rotation.x, sample.rotation.y, sample.rotation.z)).Normalized();
        return GripPose{{p.x,p.y,p.z}, {r.x,r.y,r.z,r.w}};
    }

    inline void SetHeldHidden(HeldProp& prop, void* transform, bool hidden)
    {
        if (prop.hidden != hidden &&
            HelperGameObjectSetActive(StageLoader::Invoke(StageLoader::componentGo, transform), !hidden))
            prop.hidden = hidden;
    }

    inline void PlaceHeldProp(HeldProp& prop, int selection, GakuMotion::MotionPlayer* player,
        bool left, float weight, const GakuMotion::stage_prop_timeline::Cue* cue,
        GakuMotion::MotionPlayer* outgoing, const GakuMotion::stage_prop_timeline::Cue* outgoingCue,
        const GakuMotion::CameraController& camera, const GakuMotion::TrackTiming& cameraTiming, float stageFrame, bool hidden)
    {
        const auto target = hidden ? std::nullopt : ResolveCuePose(selection, player, left, cue, camera, cameraTiming, stageFrame);
        // A missing anchor must not make a visible prop jump back to the display stand.
        if (!prop.transform && !BindHeldProp(prop, selection)) return;
        void* transform = SceneOwnerRuntime::handleTarget(prop.transform);
        if (!transform) return;
        if (!target) { SetHeldHidden(prop, transform, true); return; }

        auto display = target->position;
        auto r = target->rotation;
        if (outgoingCue)
        {
            const auto source = outgoing ? ReadGripPose(selection, *outgoing, outgoingCue->left, outgoingCue)
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
        SetHeldHidden(prop, transform, false);
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
                core.stagePropSamples = {};
                ReleaseHeldProp();
            }
            return;
        }
        struct Grip { MotionPlayer* player{}; bool left{}; float weight{}; const stage_prop_timeline::Cue* cue{}; MotionPlayer* outgoing{}; const stage_prop_timeline::Cue* outgoingCue{}; bool hidden{}; };
        std::array<Grip, extraCrepe> grips{};
        const float time = externalStageTime.load();
        const float frame = stage_prop_timeline::FrameAtTime(time);
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
        else if (follows || core.stagePropPreview)
        {
            using namespace stage_prop_timeline;
            std::array<MotionPlayer*, 11> players{};
            if (primaryPlace) players[primaryPlace] = &core.player;
            for (const auto& follower : core.ensemble.Followers())
                if (follower.active && follower.player)
                    if (int place = MotionPlace(follower.player->currentMotionDir); place && !players[place])
                        players[place] = follower.player.get();
            const auto& cues = core.stagePropPreview ? *core.stagePropPreview : core.stagePropCues;
            std::array<const Cue*, extraCrepe> previous{};
            for (const auto& cue : cues)
            {
                auto& grip = grips[cue.prop-1];
                auto& prior = previous[cue.prop-1];
                if (frame >= cue.last && (!prior || prior->last < cue.last)) prior = &cue;
                if (frame < cue.first || frame >= cue.last) continue;
                grip = {cue.anchor == Anchor::Grip ? players[cue.place] : nullptr, cue.left, 1.f, &cue};
                grip.hidden = cue.anchor == Anchor::Hidden || (cue.anchor == Anchor::Grip && !grip.player);
            }
            for (size_t i = 0; i < grips.size(); ++i)
            {
                auto& grip = grips[i];
                const auto* prior = previous[i];
                if (!grip.cue) grip.hidden = prior && prior->hideAfter;
                else if (grip.cue->movement == Movement::Handoff && prior && prior->last == grip.cue->first &&
                    prior->anchor == Anchor::Grip && players[prior->place])
                {
                    grip.weight = Progress(*grip.cue, frame);
                    grip.outgoing = players[prior->place];
                    grip.outgoingCue = prior;
                }
            }
        }
        for (size_t i = 0; i < grips.size(); ++i)
        {
            const auto& grip = grips[i];
            const bool selected = grip.cue || grip.player || grip.hidden;
            void* poseActor = grip.player ? grip.player->controller.targetActor : nullptr;
            // A camera-relative prop is evaluated after the main actor updates the
            // camera track, so the prop and camera use the same frame.
            if (grip.cue && (grip.cue->anchor == stage_prop_timeline::Anchor::Camera ||
                grip.cue->anchor == stage_prop_timeline::Anchor::CameraToStage))
                poseActor = core.player.controller.targetActor;
            auto& prop = heldProps[i];
            if (selected && poseActor == onlyActor)
            {
                PlaceHeldProp(prop, static_cast<int>(i)+1, grip.player, grip.left, grip.weight,
                    grip.cue, grip.outgoing, grip.outgoingCue, core.camera, core.player.timing.camera, frame, grip.hidden);
                core.stagePropSamples[i].reset();
                if (void* transform = prop.transform && !prop.hidden ? SceneOwnerRuntime::handleTarget(prop.transform) : nullptr)
                {
                    const auto p = g_getWorldPosNative(transform);
                    core.stagePropSamples[i] = Core::StagePropSample{static_cast<int>(std::lround(time * 30.f)),
                        {p.x - stageAnchor.x, p.y - stageAnchor.y, p.z - stageAnchor.z}};
                }
            }
            else if (!onlyActor && !selected)
            {
                ReleaseHeldProp(prop);
                core.stagePropSamples[i].reset();
            }
        }
    }
}
