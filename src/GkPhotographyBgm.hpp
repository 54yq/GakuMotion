#pragma once

namespace PhotographyBgm
{
    inline const void *getUnityVolume{}, *setUnityVolume{};
    inline void (*getCriVolume)(const char*, float*){};
    inline void (*setCriVolume)(const char*, float){};
    inline HMODULE criModule{};
    inline std::optional<float> unityOriginal, criOriginal;
    inline bool requested = false;

    inline float UnityVolume()
    {
        void* boxed = StageLoader::Invoke(getUnityVolume);
        const auto* value = boxed && g_safeBones.unbox ? static_cast<float*>(g_safeBones.unbox(boxed)) : nullptr;
        return value ? *value : -1.0f;
    }

    inline void SetUnityVolume(float volume)
    {
        void* args[]{&volume};
        StageLoader::Invoke(setUnityVolume, nullptr, args);
    }

    inline float CriVolume()
    {
        if (!getCriVolume || !criModule || GetModuleHandleW(L"cri_ware_unity.dll") != criModule) return -1.0f;
        float volume = -1.0f;
        getCriVolume("MasterOut", &volume);
        return volume;
    }

    inline void Release()
    {
        if (unityOriginal && UnityVolume() == 0.0f) SetUnityVolume(*unityOriginal);
        if (criOriginal && CriVolume() == 0.0f) setCriVolume("MasterOut", *criOriginal);
        if (requested)
            GakuMotion::host::Info("[GakuMotion][PhotographyAudio] released Unity=%g CRI=%g", UnityVolume(), CriVolume());
        unityOriginal.reset();
        criOriginal.reset();
        requested = false;
    }

    inline void Tick(bool enabled)
    {
        if (!enabled)
        {
            if (requested) Release();
            return;
        }

        if (!getUnityVolume || !setUnityVolume)
        {
            void* listener = FindClass("UnityEngine.AudioModule.dll", "UnityEngine", "AudioListener");
            auto& metadata = StageLoader::metadata;
            getUnityVolume = metadata.Find(listener, "get_volume", true, {0x0c}, {});
            setUnityVolume = metadata.Find(listener, "set_volume", true, {0x01}, {{0x0c}});
        }
        HMODULE module = GetModuleHandleW(L"cri_ware_unity.dll");
        if (module != criModule)
        {
            criOriginal.reset();
            criModule = module;
            getCriVolume = module ? reinterpret_cast<decltype(getCriVolume)>(
                GetProcAddress(module, "criAtomExAsr_GetBusVolumeByName")) : nullptr;
            setCriVolume = module ? reinterpret_cast<decltype(setCriVolume)>(
                GetProcAddress(module, "criAtomExAsr_SetBusVolumeByName")) : nullptr;
        }
        const float unity = UnityVolume();
        if (setUnityVolume && std::isfinite(unity) && unity > 0.0f)
        {
            unityOriginal = unity;
            SetUnityVolume(0.0f);
        }
        const float cri = CriVolume();
        if (setCriVolume && std::isfinite(cri) && cri > 0.0f)
        {
            criOriginal = cri;
            setCriVolume("MasterOut", 0.0f);
        }
        if (!requested)
            GakuMotion::host::Info("[GakuMotion][PhotographyAudio] Unity=%g->%g CRI MasterOut=%g->%g",
                unity, UnityVolume(), cri, CriVolume());
        requested = true;
    }
}
