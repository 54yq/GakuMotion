#pragma once

namespace ExternalStage
{
    using Handle = StageLoader::Handle;
    constexpr char scenePath[] = "Assets/MeniShukiRushStage.unity";
    constexpr char sceneName[] = "MeniShukiRushStage";
    enum class Pending { None, Bundle, Load, Unload };

    inline const void *openBundle{}, *getBundle{}, *unloadBundle{}, *isDone{};
    inline const void *loadScene{}, *unloadScene{}, *sceneCount{}, *sceneAt{}, *getEnabled{}, *setEnabled{};
    inline void* rendererClass{};
    inline const void *getLayer{}, *setLayer{}, *getCameraMask{}, *getSceneMask{}, *getOverrideMask{}, *setOverrideMask{};
    inline const void *getObjectSceneMask{}, *getBounds{}, *calculatePlanes{}, *testBounds{}, *getVisible{};
    inline Handle cameraLease{};
    inline uint64_t originalCameraMask{}, appliedCameraMask{};
    inline int visibilitySamples{};
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
        getEnabled = api.Find(rendererClass, "get_enabled", false, {0x02}, {});
        setEnabled = api.Find(rendererClass, "set_enabled", false, {0x01}, {{0x02}});
        void* camera = core("UnityEngine", "Camera");
        void* bounds = core("UnityEngine", "Bounds");
        void* utility = core("UnityEngine", "GeometryUtility");
        void* plane = core("UnityEngine", "Plane");
        auto arrayClass = reinterpret_cast<void* (*)(void*, uint32_t)>(
            GetProcAddress(g_gameAssembly, "il2cpp_array_class_get"));
        void* planes = plane && arrayClass ? arrayClass(plane, 1) : nullptr;
        getLayer = api.Find(StageLoader::goClass, "get_layer", false, {0x08}, {});
        setLayer = api.Find(StageLoader::goClass, "set_layer", false, {0x01}, {{0x08}});
        getCameraMask = api.Find(camera, "get_cullingMask", false, {0x08}, {});
        getSceneMask = api.Find(camera, "get_sceneCullingMask", false, {0x0b}, {});
        getOverrideMask = api.Find(camera, "get_overrideSceneCullingMask", false, {0x0b}, {});
        setOverrideMask = api.Find(camera, "set_overrideSceneCullingMask", false, {0x01}, {{0x0b}});
        getObjectSceneMask = api.Find(StageLoader::goClass, "get_sceneCullingMask", false, {0x0b}, {});
        getVisible = api.Find(rendererClass, "get_isVisible", false, {0x02}, {});
        if (camera && bounds && planes && StageLoader::valueSize(bounds, &align) == 6 * sizeof(float))
        {
            getBounds = api.Find(rendererClass, "get_bounds", false, {0x11, bounds}, {});
            calculatePlanes = api.Find(utility, "CalculateFrustumPlanes", true, {0x1d, planes}, {{0x12, camera}});
            testBounds = api.Find(utility, "TestPlanesAABB", true, {0x02}, {{0x1d, planes}, {0x11, bounds}});
        }
        const bool ready = openBundle && getBundle && unloadBundle && isDone && loadScene && unloadScene &&
            sceneCount && sceneAt && getEnabled && setEnabled && StageLoader::sceneName &&
            StageLoader::sceneLoaded && StageLoader::componentGo && StageLoader::goScene &&
            StageLoader::findAll && StageLoader::arrayLength && StageLoader::newHandle &&
            StageLoader::freeHandle && StageLoader::stringNew && SceneOwnerRuntime::handleTarget &&
            il2cpp_class_get_type && il2cpp_type_get_object;
        if (!ready)
            GakuMotion::host::Warn("[GakuMotion][ExternalStage] metadata unavailable: bundle=%d/%d/%d async=%d scene=%d/%d/%d/%d renderer_enabled=%d/%d",
                !!openBundle, !!getBundle, !!unloadBundle, !!isDone, !!loadScene, !!unloadScene,
                !!sceneCount, !!sceneAt, !!getEnabled, !!setEnabled);
        return ready;
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

    inline bool ReadMask(const void* method, void* object, uint64_t& mask)
    {
        void* boxed = StageLoader::Invoke(method, object);
        const auto* value = boxed && g_safeBones.unbox ? static_cast<uint64_t*>(g_safeBones.unbox(boxed)) : nullptr;
        if (!value) return false;
        mask = *value;
        return true;
    }

    inline bool SetCameraMask(void* camera, uint64_t mask)
    {
        if (!setOverrideMask) return false;
        void* args[]{&mask};
        void* exception = nullptr;
        g_faceRuntime.invoke(setOverrideMask, camera, args, &exception);
        uint64_t current = 0;
        return !exception && ReadMask(getOverrideMask, camera, current) && current == mask;
    }

    inline void RestoreCamera()
    {
        if (!cameraLease) return;
        void* camera = SceneOwnerRuntime::handleTarget(cameraLease);
        uint64_t current = 0;
        if (camera && CameraBridge::IsAlive(camera) &&
            ReadMask(getOverrideMask, camera, current) && current == appliedCameraMask)
            SetCameraMask(camera, originalCameraMask);
        StageLoader::freeHandle(cameraLease);
        cameraLease = {};
    }

    inline bool PrepareVisibility(void** renderers, uintptr_t count, int background)
    {
        void* camera = StageLoader::Invoke(StageLoader::cameraMain);
        if (!camera || !getLayer || !setLayer || !getCameraMask) return false;
        const uint32_t layers = static_cast<uint32_t>(StageLoader::ReadInt(getCameraMask, camera));
        std::array<unsigned, 32> nativeLayers{};
        uint64_t cameraScenes = 0, stageScenes = 0;
        const bool hasSceneMask = ReadMask(getSceneMask, camera, cameraScenes);
        unsigned stageCount = 0, visibleLayers = 0, sceneRejected = 0, frustumHits = 0;
        void* planeArgs[]{camera};
        void* planes = calculatePlanes ? StageLoader::Invoke(calculatePlanes, nullptr, planeArgs) : nullptr;
        Handle planeHandle = planes ? StageLoader::newHandle(planes, false) : nullptr;
        for (uintptr_t i = 0; i < count; ++i)
        {
            void* renderer = renderers[i];
            void* object = renderer ? StageLoader::Invoke(StageLoader::componentGo, renderer) : nullptr;
            if (!object) continue;
            const int scene = StageLoader::SceneOfFreshGo(object);
            if (scene != background && scene != mountedScene) continue;
            const int layer = StageLoader::ReadInt(getLayer, object, -1);
            const bool inMask = layer >= 0 && layer < 32 && (layers & (uint32_t{1} << layer));
            if (scene == background)
            {
                if (inMask && StageLoader::ReadBool(getEnabled, renderer, false)) ++nativeLayers[layer];
                continue;
            }
            if (!StageLoader::ReadBool(getEnabled, renderer, false)) continue;
            ++stageCount;
            if (inMask) ++visibleLayers;
            uint64_t objectScenes = 0;
            if (hasSceneMask && ReadMask(getObjectSceneMask, object, objectScenes) && objectScenes)
            {
                stageScenes |= objectScenes;
                if (!(objectScenes & cameraScenes)) ++sceneRejected;
            }
            if (planeHandle && testBounds && getBounds)
            {
                void* boxed = StageLoader::Invoke(getBounds, renderer);
                void* raw = boxed && g_safeBones.unbox ? g_safeBones.unbox(boxed) : nullptr;
                if (!raw) continue;
                std::array<float, 6> bounds{};
                std::memcpy(bounds.data(), raw, sizeof(bounds));
                void* args[]{SceneOwnerRuntime::handleTarget(planeHandle), bounds.data()};
                if (args[0] && StageLoader::ReadBool(testBounds, nullptr, false, args)) ++frustumHits;
            }
        }
        if (planeHandle) StageLoader::freeHandle(planeHandle);
        const auto preferred = std::max_element(nativeLayers.begin(), nativeLayers.end());
        int layer = *preferred ? static_cast<int>(preferred - nativeLayers.begin()) : -1;
        unsigned relayered = 0;
        if (visibleLayers < stageCount && layer >= 0)
        {
            for (uintptr_t i = 0; i < count; ++i)
            {
                void* renderer = renderers[i];
                void* object = renderer ? StageLoader::Invoke(StageLoader::componentGo, renderer) : nullptr;
                if (!object || StageLoader::SceneOfFreshGo(object) != mountedScene) continue;
                const int previous = StageLoader::ReadInt(getLayer, object, -1);
                if (previous >= 0 && previous < 32 && (layers & (uint32_t{1} << previous))) continue;
                void* args[]{&layer};
                StageLoader::Invoke(setLayer, object, args);
                if (StageLoader::ReadInt(getLayer, object, -1) == layer) ++relayered;
            }
        }
        bool sceneMaskReady = sceneRejected == 0;
        if (sceneRejected && stageScenes && ReadMask(getOverrideMask, camera, originalCameraMask))
        {
            cameraLease = StageLoader::newHandle(camera, false);
            appliedCameraMask = cameraScenes | stageScenes;
            if (cameraLease && SetCameraMask(camera, appliedCameraMask))
            {
                uint64_t after = 0;
                sceneMaskReady = ReadMask(getSceneMask, camera, after) && (after & stageScenes) == stageScenes;
            }
        }
        GakuMotion::host::Info("[GakuMotion][ExternalStage] visibility stage_renderers=%u camera_layers=%08x layers_before=%u relayered=%u native_layer=%d camera_scenes=%llx stage_scenes=%llx scene_rejected=%u scene_mask_ready=%d frustum=%d/%u",
            stageCount, layers, visibleLayers, relayered, layer, static_cast<unsigned long long>(cameraScenes),
            static_cast<unsigned long long>(stageScenes), sceneRejected, sceneMaskReady,
            planeHandle && testBounds && getBounds ? static_cast<int>(frustumHits) : -1, stageCount);
        return stageCount != 0 && visibleLayers + relayered != 0 && sceneMaskReady;
    }

    inline void RestoreBackground()
    {
        RestoreCamera();
        const int restoredScene = hiddenScene;
        size_t restoredCount = 0;
        size_t failed = 0;
        for (Handle handle : hiddenRenderers)
        {
            void* renderer = SceneOwnerRuntime::handleTarget(handle);
            if (renderer && CameraBridge::IsAlive(renderer))
            {
                bool enabled = false;
                if (ModelRendererMask::ReadBool(getEnabled, renderer, enabled) &&
                    (enabled || ModelRendererMask::WriteBool(setEnabled, getEnabled, renderer, true)))
                    ++restoredCount;
                else
                    ++failed;
            }
            StageLoader::freeHandle(handle);
        }
        hiddenRenderers.clear();
        hiddenScene = 0;
        if (restoredScene)
            GakuMotion::host::Info("[GakuMotion][ExternalStage] restored background=%d renderers=%zu failed=%zu",
                restoredScene, restoredCount, failed);
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
        if (!PrepareVisibility(renderers, count, background))
        {
            StageLoader::freeHandle(pin);
            RestoreBackground();
            return false;
        }
        size_t found = 0;
        bool complete = true;
        for (uintptr_t i = 0; i < count; ++i)
        {
            void* renderer = renderers[i];
            void* object = renderer ? StageLoader::Invoke(StageLoader::componentGo, renderer) : nullptr;
            if (!object || StageLoader::SceneOfFreshGo(object) != background) continue;
            ++found;
            bool enabled = false;
            if (!ModelRendererMask::ReadBool(getEnabled, renderer, enabled))
            {
                complete = false;
                break;
            }
            if (!enabled) continue;
            Handle saved = StageLoader::newHandle(renderer, false);
            if (!saved)
            {
                complete = false;
                break;
            }
            // Only originally enabled renderers enter the restore list.
            hiddenRenderers.push_back(saved);
            if (!ModelRendererMask::WriteBool(setEnabled, getEnabled, renderer, false))
            {
                complete = false;
                break;
            }
        }
        StageLoader::freeHandle(pin);
        if (!complete || !found)
        {
            RestoreBackground();
            return false;
        }
        hiddenScene = background;
        visibilitySamples = 30;
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
            if (visibilitySamples > 0 && --visibilitySamples == 0 && getVisible)
            {
                void* type = il2cpp_type_get_object(il2cpp_class_get_type(rendererClass));
                bool inactive = false;
                void* args[]{type, &inactive};
                void* list = type ? StageLoader::Invoke(StageLoader::findAll, nullptr, args) : nullptr;
                Handle pin = list ? StageLoader::newHandle(list, false) : nullptr;
                if (pin)
                {
                    unsigned visible = 0, total = 0;
                    const auto count = StageLoader::arrayLength(list);
                    auto** renderers = reinterpret_cast<void**>(static_cast<char*>(list) + 32);
                    for (uintptr_t i = 0; i < count; ++i)
                    {
                        void* renderer = renderers[i];
                        void* object = renderer ? StageLoader::Invoke(StageLoader::componentGo, renderer) : nullptr;
                        if (object && StageLoader::SceneOfFreshGo(object) == mountedScene)
                        {
                            ++total;
                            if (StageLoader::ReadBool(getVisible, renderer, false)) ++visible;
                        }
                    }
                    StageLoader::freeHandle(pin);
                    GakuMotion::host::Info("[GakuMotion][ExternalStage] post-render visible=%u/%u scene=%d", visible, total, mountedScene);
                }
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
