#pragma once

namespace PhotographyBgm
{
    inline StageLoader::Handle mutedSource{};
    inline const void* getVolume{};
    inline const void* setVolume{};
    inline float originalVolume{};
    inline bool reportedUnavailable{};

    inline float Volume(void* source)
    {
        void* boxed = StageLoader::Invoke(getVolume, source);
        const auto* value = boxed && g_safeBones.unbox ? static_cast<float*>(g_safeBones.unbox(boxed)) : nullptr;
        return value && std::isfinite(*value) ? *value : -1.0f;
    }

    inline void SetVolume(void* source, float value)
    {
        void* args[]{&value};
        StageLoader::Invoke(setVolume, source, args);
    }

    inline void Release()
    {
        if (!mutedSource) return;
        void* source = SceneOwnerRuntime::handleTarget(mutedSource);
        if (source && Volume(source) == 0.0f) SetVolume(source, originalVolume);
        StageLoader::freeHandle(mutedSource);
        mutedSource = {};
        GakuMotion::host::Info("[GakuMotion][PhotographyBgm] released source volume");
    }

    inline void Tick(bool enabled)
    {
        if (!enabled)
        {
            Release();
            reportedUnavailable = false;
            return;
        }
        if (!StageLoader::newHandle || !StageLoader::freeHandle || !SceneOwnerRuntime::handleTarget) return;

        auto& metadata = StageLoader::metadata;
        const auto objectClass = reinterpret_cast<void* (*)(void*)>(
            GetProcAddress(g_gameAssembly, "il2cpp_object_get_class"));
        const auto parent = reinterpret_cast<void* (*)(void*)>(
            GetProcAddress(g_gameAssembly, "il2cpp_class_get_parent"));
        if (!objectClass || !parent) return;
        static const void* currentBgm = [&]() -> const void* {
            for (const char* image : {"Assembly-CSharp.dll", "campus-submodule.Runtime.dll"})
            {
                void* manager = FindClass(image, "Campus.Common", "CampusSoundManager");
                const void* method = metadata.Find(manager, "GetCurrentBgmPlayer", true, {0x12, nullptr, false}, {});
                if (method)
                    return method;
            }
            return nullptr;
        }();
        void* player = StageLoader::Invoke(currentBgm);
        void* source = nullptr;
        if (player)
        {
            auto method = metadata.Find(objectClass(player), "get_Source", false, {0x12, nullptr, false}, {});
            source = StageLoader::Invoke(method, player);
        }
        if (mutedSource && SceneOwnerRuntime::handleTarget(mutedSource) != source) Release();
        if (!source)
        {
            if (!reportedUnavailable)
                GakuMotion::host::Warn("[GakuMotion][PhotographyBgm] CampusSoundManager current player/source unavailable getter=%d player=%d",
                    !!currentBgm, !!player);
            reportedUnavailable = true;
            return;
        }
        if (!mutedSource)
        {
            const void* cueMethod = nullptr;
            getVolume = setVolume = nullptr;
            for (void* klass = objectClass(source); klass; klass = parent(klass))
            {
                if (!cueMethod) cueMethod = metadata.Find(klass, "get_cueName", false, {0x0e}, {});
                if (!getVolume) getVolume = metadata.Find(klass, "get_volume", false, {0x0c}, {});
                if (!setVolume) setVolume = metadata.Find(klass, "set_volume", false, {0x01}, {{0x0c}});
            }
            float volume = Volume(source);
            if (!setVolume || volume < 0.0f)
            {
                if (!reportedUnavailable)
                    GakuMotion::host::Warn("[GakuMotion][PhotographyBgm] current source volume unavailable get=%d set=%d",
                        !!getVolume, !!setVolume);
                reportedUnavailable = true;
                return;
            }
            mutedSource = StageLoader::newHandle(source, false);
            if (!mutedSource) return;
            originalVolume = volume;
            SetVolume(source, 0.0f);
            const auto cue = Il2CppStringToUtf8(StageLoader::Invoke(cueMethod, source));
            GakuMotion::host::Info("[GakuMotion][PhotographyBgm] CampusSoundManager cue=%s before=%.3f after=%.3f",
                cue.empty() ? "-" : cue.c_str(), volume, Volume(source));
            reportedUnavailable = false;
            return;
        }
        const float volume = Volume(source);
        if (volume > 0.0f)
        {
            originalVolume = volume;
            SetVolume(source, 0.0f);
        }
    }
}
