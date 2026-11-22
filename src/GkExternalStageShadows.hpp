#pragma once

namespace ExternalStage
{
    // Stage materials consume the two scene-local projected foot contacts.
    inline void UpdateContactShadows(void* actor, GakuMotion::IdolController& controller)
    {
        static bool applied = false;
        const bool eligible = actor && status.load() == 1 && alignedBackground && controller.fnGetBoneTransform &&
            g_getWorldPosNative;
        if (!eligible && !applied) return;
        auto& api = StageLoader::metadata;
        static void* shader = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Shader");
        static void* vec = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Vector4");
        static const void* setter = api.Find(shader, "SetGlobalVector", true, {0x01}, {{0x08}, {0x11, vec}});
        static const void* propertyId = api.Find(shader, "PropertyToID", true, {0x08}, {{0x0e}});
        uint32_t alignment = 0;
        if (!setter || !propertyId || !vec || !StageLoader::valueSize ||
            StageLoader::valueSize(vec, &alignment) != 4 * sizeof(float)) return;
        static auto ids = [&]() {
            std::array<int, 2> result{};
            const char* names[]{"_GakuExternalStageFootL", "_GakuExternalStageFootR"};
            for (size_t i = 0; i < result.size(); ++i)
            {
                void* name = StageLoader::stringNew(names[i]);
                void* args[]{name};
                if (name) result[i] = StageLoader::ReadInt(propertyId, nullptr, 0, args);
            }
            return result;
        }();
        void* object = eligible ? StageLoader::Invoke(StageLoader::componentGo, actor) : nullptr;
        const bool owned = object && StageLoader::SceneOfFreshGo(object) == owner.sceneHandle;
        for (size_t i = 0; i < ids.size(); ++i)
        {
            std::array<float, 4> shadow{0, alignedFloor, 0, 0};
            void* foot = owned ? controller.fnGetBoneTransform(actor, i == 0 ? 5 : 6) : nullptr;
            if (foot)
            {
                auto p = g_getWorldPosNative(foot);
                shadow = {p.x, alignedFloor, p.z, std::clamp(1.0f - (p.y - alignedFloor) / 0.55f, 0.0f, 1.0f)};
            }
            if (ids[i])
            {
                void* args[]{&ids[i], shadow.data()};
                StageLoader::Invoke(setter, nullptr, args);
            }
        }
        applied = owned;
    }
}
