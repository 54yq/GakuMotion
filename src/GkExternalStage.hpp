#pragma once

namespace ExternalStage
{
    using Handle = StageLoader::Handle;
    constexpr char scenePath[] = "Assets/MeniShukiRushStage.unity";
    constexpr char sceneName[] = "MeniShukiRushStage";
    enum class Pending { None, Bundle, Load, Unload };

    inline const void *openBundle{}, *getBundle{}, *unloadBundle{}, *isDone{};
    inline const void *loadScene{}, *unloadScene{}, *sceneCount{}, *sceneAt{}, *getHidden{}, *setHidden{};
    inline void* rendererClass{};
    inline Handle operation{}, bundle{};
    inline Pending pending{};
    inline int mountedScene{}, hiddenScene{};
    inline std::vector<Handle> hiddenRenderers;
    inline bool attempted{};
    inline GakuMotion::injection::OwnerSnapshot owner;
    inline std::atomic<int> status{0};

    inline bool Resolve()
    {
        auto& api = StageLoader::metadata;
        const auto core = [](const char* ns, const char* name) {
            return FindClass("UnityEngine.CoreModule.dll", ns, name);
        };
        const auto ab = [](const char* name) {
            return FindClass("UnityEngine.AssetBundleModule.dll", "UnityEngine", name);
        };
        void* scene = core("UnityEngine.SceneManagement", "Scene");
        void* manager = core("UnityEngine.SceneManagement", "SceneManager");
        void* mode = core("UnityEngine.SceneManagement", "LoadSceneMode");
        void* async = core("UnityEngine", "AsyncOperation");
        void* bundleClass = ab("AssetBundle");
        void* request = ab("AssetBundleCreateRequest");
        rendererClass = core("UnityEngine", "Renderer");
        uint32_t align = 0;
        if (!scene || !mode || !async || !bundleClass || !request || !rendererClass ||
            !StageLoader::valueSize || StageLoader::valueSize(scene, &align) != sizeof(int) ||
            StageLoader::valueSize(mode, &align) != sizeof(int)) return false;

        openBundle = api.Find(bundleClass, "LoadFromFileAsync", true, {0x12, request}, {{0x0e}});
        getBundle = api.Find(request, "get_assetBundle", false, {0x12, bundleClass}, {});
        unloadBundle = api.Find(bundleClass, "Unload", false, {0x01}, {{0x02}});
        isDone = api.Find(async, "get_isDone", false, {0x02}, {});
        loadScene = api.Find(manager, "LoadSceneAsync", true, {0x12, async}, {{0x0e}, {0x11, mode}});
        unloadScene = api.Find(manager, "UnloadSceneAsync", true, {0x12, async}, {{0x11, scene}});
        sceneCount = api.Find(manager, "get_sceneCount", true, {0x08}, {});
        sceneAt = api.Find(manager, "GetSceneAt", true, {0x11, scene}, {{0x08}});
        getHidden = api.Find(rendererClass, "get_forceRenderingOff", false, {0x02}, {});
        setHidden = api.Find(rendererClass, "set_forceRenderingOff", false, {0x01}, {{0x02}});
        return openBundle && getBundle && unloadBundle && isDone && loadScene && unloadScene &&
            sceneCount && sceneAt && getHidden && setHidden && StageLoader::sceneName &&
            StageLoader::sceneLoaded && StageLoader::componentGo && StageLoader::goScene &&
            StageLoader::findAll && StageLoader::arrayLength && StageLoader::newHandle &&
            StageLoader::freeHandle && StageLoader::stringNew && SceneOwnerRuntime::handleTarget &&
            il2cpp_class_get_type && il2cpp_type_get_object;
    }

    inline int FindStageScene()
    {
        const int count = StageLoader::ReadInt(sceneCount, nullptr);
        for (int i = 0; i < count; ++i)
        {
            void* args[]{&i};
            int handle = StageLoader::ReadInt(sceneAt, nullptr, 0, args);
            void* sceneArgs[]{&handle};
            if (handle && Il2CppStringToUtf8(StageLoader::Invoke(StageLoader::sceneName, nullptr, sceneArgs)) == sceneName)
                return handle;
        }
        return 0;
    }

    inline bool SetHidden(void* renderer, bool hidden)
    {
        void* args[]{&hidden};
        void* exception = nullptr;
        g_faceRuntime.invoke(setHidden, renderer, args, &exception);
        return !exception;
    }

    inline void RestoreBackground()
    {
        const int restoredScene = hiddenScene;
        const size_t restoredCount = hiddenRenderers.size();
        for (Handle handle : hiddenRenderers)
        {
            if (void* renderer = SceneOwnerRuntime::handleTarget(handle))
                if (StageLoader::ReadBool(getHidden, renderer, false)) SetHidden(renderer, false);
            StageLoader::freeHandle(handle);
        }
        hiddenRenderers.clear();
        hiddenScene = 0;
        if (restoredScene)
            GakuMotion::host::Info("[GakuMotion][ExternalStage] restored background=%d renderers=%zu",
                restoredScene, restoredCount);
    }

    inline bool HideBackground(int background)
    {
        void* type = il2cpp_type_get_object(il2cpp_class_get_type(rendererClass));
        bool includeInactive = true;
        void* args[]{type, &includeInactive};
        void* list = type ? StageLoader::Invoke(StageLoader::findAll, nullptr, args) : nullptr;
        Handle pin = list ? StageLoader::newHandle(list, false) : nullptr;
        if (!pin) return false;
        const uintptr_t count = StageLoader::arrayLength(list);
        auto** renderers = reinterpret_cast<void**>(static_cast<char*>(list) + 32);
        size_t found = 0;
        bool complete = true;
        for (uintptr_t i = 0; i < count; ++i)
        {
            void* renderer = renderers[i];
            void* object = renderer ? StageLoader::Invoke(StageLoader::componentGo, renderer) : nullptr;
            if (!object || StageLoader::SceneOfFreshGo(object) != background) continue;
            ++found;
            if (StageLoader::ReadBool(getHidden, renderer, true)) continue;
            Handle saved = StageLoader::newHandle(renderer, false);
            if (!saved || !SetHidden(renderer, true))
            {
                if (saved) StageLoader::freeHandle(saved);
                complete = false;
                break;
            }
            hiddenRenderers.push_back(saved);
        }
        StageLoader::freeHandle(pin);
        if (!complete || !found)
        {
            RestoreBackground();
            return false;
        }
        hiddenScene = background;
        GakuMotion::host::Info("[GakuMotion][ExternalStage] additive_scene=%d background=%d renderers_hidden=%zu",
            mountedScene, background, hiddenRenderers.size());
        return true;
    }

    inline bool Start(Pending kind, void* request)
    {
        if (!request) return false;
        operation = StageLoader::newHandle(request, false);
        if (!operation) return false;
        pending = kind;
        return true;
    }

    inline void ReleaseBundle()
    {
        if (!bundle) return;
        if (void* object = SceneOwnerRuntime::handleTarget(bundle))
        {
            bool all = true;
            void* args[]{&all};
            StageLoader::Invoke(unloadBundle, object, args);
        }
        StageLoader::freeHandle(bundle);
        bundle = {};
    }

    inline bool StartBundle()
    {
        wchar_t executable[32768];
        const DWORD length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
        if (!length || length >= std::size(executable)) return false;
        const auto assetPath = (std::filesystem::path(executable).parent_path() /
            "GakuLoader/plugins/GakuMotion.ExternalStage/meni-shuki-rush-scene.bundle").u8string();
        void* path = StageLoader::stringNew(reinterpret_cast<const char*>(assetPath.c_str()));
        void* args[]{path};
        return path && Start(Pending::Bundle, StageLoader::Invoke(openBundle, nullptr, args));
    }

    inline void Tick(bool admitted, int background, const GakuMotion::injection::OwnerSnapshot& current)
    {
        const bool wanted = admitted && (!owner.Valid() || owner.SameOwner(current));
        if (!wanted || hiddenScene != background)
            RestoreBackground();
        if (operation)
        {
            void* request = SceneOwnerRuntime::handleTarget(operation);
            if (request && !StageLoader::ReadBool(isDone, request, false)) return;
            const auto finished = pending;
            if (finished == Pending::Bundle && request)
            {
                void* loaded = StageLoader::Invoke(getBundle, request);
                bundle = loaded ? StageLoader::newHandle(loaded, false) : nullptr;
            }
            StageLoader::freeHandle(operation);
            operation = {};
            pending = Pending::None;
            if (finished == Pending::Load) mountedScene = FindStageScene();
            if (finished == Pending::Unload)
            {
                mountedScene = FindStageScene();
                GakuMotion::host::Info("[GakuMotion][ExternalStage] unload complete remaining_scene=%d", mountedScene);
                if (!mountedScene) ReleaseBundle();
            }

            if (finished == Pending::Bundle && wanted && bundle)
            {
                void* path = StageLoader::stringNew(scenePath);
                int additive = 1;
                void* args[]{path, &additive};
                if (path && Start(Pending::Load, StageLoader::Invoke(loadScene, nullptr, args)))
                    return;
                GakuMotion::host::Warn("[GakuMotion][ExternalStage] LoadSceneAsync(Additive) could not start");
            }
            if (finished == Pending::Bundle || (finished == Pending::Load && !mountedScene))
            {
                ReleaseBundle();
                status.store(wanted ? -1 : 0);
                if (wanted) GakuMotion::host::Warn("[GakuMotion][ExternalStage] streamed scene did not load");
            }
        }

        if (!wanted)
        {
            if (mountedScene)
            {
                void* args[]{&mountedScene};
                if (Start(Pending::Unload, StageLoader::Invoke(unloadScene, nullptr, args)))
                {
                    status.store(0);
                    return;
                }
                if (FindStageScene()) return;
                mountedScene = 0;
            }
            ReleaseBundle();
            owner = {};
            attempted = false;
            status.store(0);
            return;
        }

        if (mountedScene)
        {
            if (!background) { status.store(0); return; }
            if (!hiddenScene && !HideBackground(background))
            {
                status.store(-1);
                GakuMotion::host::Warn("[GakuMotion][ExternalStage] hiding native background failed; retiring additive scene");
                void* args[]{&mountedScene};
                Start(Pending::Unload, StageLoader::Invoke(unloadScene, nullptr, args));
                return;
            }
            status.store(1);
            return;
        }
        if (!background || attempted) return;
        attempted = true;
        owner = current;
        if (!Resolve() || FindStageScene() || !StartBundle())
        {
            status.store(-1);
            GakuMotion::host::Warn("[GakuMotion][ExternalStage] cannot start independent stage bundle");
            return;
        }
        status.store(2);
        GakuMotion::host::Info("[GakuMotion][ExternalStage] opening independent stage bundle; photography=%d background=%d",
            current.sceneHandle, background);
    }
}
