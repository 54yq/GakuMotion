#pragma once

namespace ExternalStage
{
    inline constexpr size_t maxShadowContacts = 60;

    inline void UpdateContactShadows(GakuMotion::Core& core)
    {
        auto& controller = core.player.controller;
        static int countId{}, sizeId{};
        static Handle contacts{};
        const bool eligible = status.load() == 1 && alignedBackground && controller.fnGetBoneTransform &&
            g_getWorldPosNative;
        if (!eligible)
        {
            if (contacts)
            {
                SetStageShaderFloat("_GakuExternalStageContactCount", 0, countId);
                StageLoader::freeHandle(contacts);
                contacts = {};
            }
            return;
        }
        static void* shader = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Shader");
        static void* vector = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Vector4");
        if (!shader || !vector) return;
        static const auto arrayClass = reinterpret_cast<void* (*)(void*, uint32_t)>(
            GetProcAddress(g_gameAssembly, "il2cpp_array_class_get"));
        static const auto arrayNew = reinterpret_cast<void* (*)(void*, uintptr_t)>(
            GetProcAddress(g_gameAssembly, "il2cpp_array_new"));
        static void* vectors = arrayClass ? arrayClass(vector, 1) : nullptr;
        if (!vectors || !arrayNew) return;
        auto& api = StageLoader::metadata;
        static const void* setter = api.Find(shader, "SetGlobalVectorArray", true, {0x01}, {{0x08}, {0x1d, vectors}});
        static const void* propertyId = api.Find(shader, "PropertyToID", true, {0x08}, {{0x0e}});
        uint32_t alignment = 0;
        if (!setter || !propertyId || !StageLoader::valueSize ||
            StageLoader::valueSize(vector, &alignment) != 4 * sizeof(float)) return;
        static int id = [&]() {
            void* name = StageLoader::stringNew("_GakuExternalStageContacts");
            void* args[]{name};
            return name ? StageLoader::ReadInt(propertyId, nullptr, 0, args) : 0;
        }();
        if (!id) return;
        if (!contacts)
        {
            if (void* array = arrayNew(vector, maxShadowContacts))
                contacts = StageLoader::newHandle(array, false);
        }
        if (!contacts) return;
        std::array<std::array<float, 4>, maxShadowContacts> measurements{};
        size_t count = 0;
        const auto add = [&](void* actor) {
            if (!actor || count + 3 > measurements.size()) return;
            void* object = StageLoader::Invoke(StageLoader::componentGo, actor);
            if (!object || StageLoader::SceneOfFreshGo(object) != owner.sceneHandle) return;
            if (void* hips = controller.fnGetBoneTransform(actor, 0))
            {
                const auto p = g_getWorldPosNative(hips);
                if (std::isfinite(p.x) && std::isfinite(p.z))
                    measurements[count++] = {p.x, alignedFloor, p.z, -0.58f};
            }
            for (const int bone : {5, 6})
                if (void* foot = controller.fnGetBoneTransform(actor, bone))
                {
                    const auto p = g_getWorldPosNative(foot);
                    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) continue;
                    const float strength = std::clamp(1.f - (p.y - alignedFloor) / 0.55f, 0.f, 1.f);
                    measurements[count++] = {p.x, alignedFloor, p.z, strength * 0.18f};
                }
        };
        add(controller.targetActor);
        for (const auto& entry : core.sceneActorRegistry.Entries())
            if (entry.actor != controller.targetActor) add(entry.actor);
        void* array = SceneOwnerRuntime::handleTarget(contacts);
        if (!array) return;
        std::memcpy(static_cast<char*>(array) + 32, measurements.data(), sizeof(measurements));
        void* args[]{&id, array};
        StageLoader::Invoke(setter, nullptr, args);
        SetStageShaderFloat("_GakuExternalStageContactCount", static_cast<float>(count), countId);
        SetStageShaderFloat("_GakuExternalStageContactSize", GakuMotion::externalStageShadowSize.load(), sizeId);
        static int lastScene{};
        static size_t lastCount{};
        if (lastScene != mountedScene || lastCount != count)
        {
            GakuMotion::host::Info("[GakuMotion][ExternalStage] contact shadows scene=%d samples=%zu",
                mountedScene, count);
            lastScene = mountedScene;
            lastCount = count;
        }
    }
}
