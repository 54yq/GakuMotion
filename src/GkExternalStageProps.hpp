#pragma once

namespace ExternalStage
{
    // The object's transform remains owned by the additive scene during hand following.
    inline Handle heldTransform{};
    inline UnityVector3 propPosition{};
    inline UnityVector3 propTravel{};
    inline UnityQuaternion propRotation{};
    inline int heldSelection{};
    inline int attemptedProp{};

    inline void ReleaseHeldProp()
    {
        if (heldTransform)
        {
            if (void* transform = SceneOwnerRuntime::handleTarget(heldTransform))
            {
                auto position = propPosition;
                position.x += stageTravel.x - propTravel.x;
                position.z += stageTravel.z - propTravel.z;
                g_setWorldPosNative(transform, position);
                g_setWorldRotNative(transform, propRotation);
            }
            StageLoader::freeHandle(heldTransform);
            heldTransform = {};
        }
        heldSelection = 0;
        attemptedProp = 0;
    }

    inline bool BindHeldProp(int selection)
    {
        attemptedProp = selection;
        bool inactive = false;
        void* args[]{StageLoader::goType, &inactive};
        void* list = StageLoader::Invoke(StageLoader::findAll, nullptr, args);
        Handle pin = list ? StageLoader::newHandle(list, false) : nullptr;
        if (!pin) return false;
        const auto name = std::string("StageProp_") + std::string(profile.propNames[selection - 1]);
        auto** items = reinterpret_cast<void**>(static_cast<char*>(list) + 32);
        for (uintptr_t i = 0, count = StageLoader::arrayLength(list); i < count; ++i)
        {
            void* object = items[i];
            if (!object || StageLoader::SceneOfFreshGo(object) != mountedScene ||
                Il2CppStringToUtf8(StageLoader::Invoke(StageLoader::objectName, object)) != name) continue;
            if (void* transform = StageLoader::Invoke(StageLoader::goTransform, object))
            {
                heldTransform = StageLoader::newHandle(transform, false);
                propPosition = g_getWorldPosNative(transform);
                propTravel = stageTravel;
                propRotation = g_getWorldRotNative(transform);
            }
            break;
        }
        StageLoader::freeHandle(pin);
        if (heldTransform) heldSelection = selection;
        GakuMotion::host::Info("[GakuMotion][ExternalStage] hand prop=%s bound=%d", name.c_str(), !!heldTransform);
        return !!heldTransform;
    }

    inline void UpdateHeldProp(void* actor, GakuMotion::IdolController& controller)
    {
        const int selection = GakuMotion::externalStageHeldProp.load();
        const float time = GakuMotion::externalStageTime.load();
        const float start = GakuMotion::externalStagePropStart.load();
        const float end = GakuMotion::externalStagePropEnd.load();
        const float transition = std::min(0.20f, (end - start) * 0.5f);
        if (status.load() != 1 || !mountedScene || selection <= 0 ||
            transition <= 0 || time < start || time >= end ||
            selection > static_cast<int>(profile.propNames.size()))
        {
            if (heldTransform || attemptedProp) ReleaseHeldProp();
            return;
        }
        if (!actor || !StageLoader::goType || !controller.fnGetBoneTransform ||
            !g_getWorldPosNative || !g_getWorldRotNative || !g_setWorldPosNative || !g_setWorldRotNative) return;
        void* object = StageLoader::Invoke(StageLoader::componentGo, actor);
        if (!object || StageLoader::SceneOfFreshGo(object) != owner.sceneHandle) return;
        const bool left = GakuMotion::externalStageLeftHand.load();
        void* hand = controller.fnGetBoneTransform(actor, left ? 17 : 18);
        void* finger = controller.fnGetBoneTransform(actor, left ? 27 : 42);
        if (!hand) return;
        if (attemptedProp != selection)
        {
            ReleaseHeldProp();
            if (!BindHeldProp(selection)) return;
        }
        if (heldSelection != selection) return;
        void* transform = SceneOwnerRuntime::handleTarget(heldTransform);
        if (!transform) return;
        auto position = g_getWorldPosNative(hand);
        if (finger)
        {
            const auto grip = g_getWorldPosNative(finger);
            position.x = (position.x + grip.x) * 0.5f;
            position.y = (position.y + grip.y) * 0.5f;
            position.z = (position.z + grip.z) * 0.5f;
        }
        // Evaluate from the playhead so scrubbing reconstructs exactly the same handoff.
        const float ramp = std::min({1.0f, (time - start) / transition, (end - time) / transition});
        const float weight = ramp * ramp * (3 - 2 * ramp);
        const UnityVector3 display{propPosition.x + stageTravel.x - propTravel.x, propPosition.y,
            propPosition.z + stageTravel.z - propTravel.z};
        g_setWorldPosNative(transform, {std::lerp(display.x, position.x, weight),
            std::lerp(display.y, position.y, weight), std::lerp(display.z, position.z, weight)});
        auto gripRotation = g_getWorldRotNative(hand);
        const float dot = propRotation.x * gripRotation.x + propRotation.y * gripRotation.y +
            propRotation.z * gripRotation.z + propRotation.w * gripRotation.w;
        if (dot < 0) gripRotation = {-gripRotation.x, -gripRotation.y, -gripRotation.z, -gripRotation.w};
        UnityQuaternion rotation{std::lerp(propRotation.x, gripRotation.x, weight),
            std::lerp(propRotation.y, gripRotation.y, weight), std::lerp(propRotation.z, gripRotation.z, weight),
            std::lerp(propRotation.w, gripRotation.w, weight)};
        const float norm = std::sqrt(rotation.x * rotation.x + rotation.y * rotation.y +
            rotation.z * rotation.z + rotation.w * rotation.w);
        if (norm > 0) g_setWorldRotNative(transform, {rotation.x / norm, rotation.y / norm, rotation.z / norm, rotation.w / norm});
    }
}
