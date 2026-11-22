#pragma once

namespace ExternalStage
{
    using Handle = StageLoader::Handle;
    constexpr auto& profile = ExternalStageProfiles::meniShukiRush;
    enum class Pending { None, Bundle, Load, Unload };

    inline const void *openBundle{}, *getBundle{}, *unloadBundle{}, *isDone{};
    inline const void *loadScene{}, *unloadScene{}, *sceneCount{}, *sceneAt{}, *getEnabled{}, *setEnabled{};
    inline void* rendererClass{};
    inline const void *getLayer{}, *setLayer{}, *getCameraMask{}, *getSceneMask{}, *getOverrideMask{}, *setOverrideMask{};
    inline const void *getObjectSceneMask{}, *getBounds{}, *calculatePlanes{}, *testBounds{}, *getVisible{};
    inline Handle cameraLease{};
    inline uint64_t originalCameraMask{}, appliedCameraMask{};
    inline int visibilitySamples{};
    inline int alignedBackground{};
    inline float alignedFloor{};
    inline std::chrono::steady_clock::time_point nextAlignment{};
    inline Handle operation{}, bundle{};
    inline Pending pending{};
    inline int mountedScene{}, hiddenScene{};
    inline std::vector<Handle> hiddenRenderers;
    inline bool attempted{};
    inline GakuMotion::injection::OwnerSnapshot owner;
    inline std::atomic<int> status{0};
    inline std::array<Handle, 2> doorTransforms{};
    inline const void* setDoorRotation{};
    inline int doorScene{};
    inline float lastDoorTime = -1;
    inline std::chrono::steady_clock::time_point lastTimelineTick{};

    inline void SetStageShaderFloat(const char* property, float frame, int& id)
    {
        static const void* setter = StageLoader::metadata.Find(
            FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Shader"), "SetGlobalFloat",
            true, {0x01}, {{0x0e}, {0x0c}});
        if (!setter) return;
        // Reuse the property ID to avoid an allocation every stage frame.
        static const void* nameToId = StageLoader::metadata.Find(
            FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Shader"), "PropertyToID",
            true, {0x08}, {{0x0e}});
        static const void* setterById = StageLoader::metadata.Find(
            FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Shader"), "SetGlobalFloat",
            true, {0x01}, {{0x08}, {0x0c}});
        if (!id) {
            void* name = StageLoader::stringNew(property);
            void* args[]{name};
            id = name && nameToId ? StageLoader::ReadInt(nameToId, nullptr, 0, args) : 0;
        }
        if (setterById && id)
        {
            void* args[]{&id, &frame};
            StageLoader::Invoke(setterById, nullptr, args);
        }
        else if (void* name = StageLoader::stringNew(property))
        {
            void* args[]{name, &frame};
            StageLoader::Invoke(setter, nullptr, args);
        }
    }

    inline int lightFrameId{}, gradeId{};
    inline Handle stageRoot{};
    inline UnityVector3 stageOrigin{};
    inline UnityVector3 stageAnchor{}, stageTravel{};
    inline float stageYaw{};

    inline UnityVector3 RotateStageOffset(UnityVector3 value)
    {
        const float c = std::cos(stageYaw), s = std::sin(stageYaw);
        return {c * value.x + s * value.z, value.y, -s * value.x + c * value.z};
    }

    inline void SetStagePlacement(void* transform, float yaw)
    {
        stageYaw = yaw;
        const auto offset = RotateStageOffset(profile.rootOffset);
        stageOrigin = {stageAnchor.x + offset.x, stageAnchor.y + offset.y, stageAnchor.z + offset.z};
        g_setWorldRotNative(transform, {0, std::sin(yaw * 0.5f), 0, std::cos(yaw * 0.5f)});
        g_setWorldPosNative(transform, stageOrigin);
    }

    inline void ApplyStageTravel(float frame)
    {
        if (!stageRoot || !g_setWorldPosNative) return;
        void* transform = SceneOwnerRuntime::handleTarget(stageRoot);
        if (!transform) return;
        const float fraction = std::clamp((frame - profile.travelStart) / (profile.travelEnd - profile.travelStart), 0.0f, 1.0f);
        // Stage VMD root Z uses the destination key’s Bezier (0, 0, 0, 0).
        stageTravel = RotateStageOffset({0, 0, profile.travelZ * fraction});
        auto position = stageOrigin;
        position.x += stageTravel.x;
        position.z += stageTravel.z;
        g_setWorldPosNative(transform, position);
    }
    inline void SetStageLightFrame(float frame) { SetStageShaderFloat(profile.lightFrameProperty, frame, lightFrameId); }

    inline void ResetTimeline()
    {
        if (!doorScene) return;
        for (auto& handle : doorTransforms)
        {
            if (handle) StageLoader::freeHandle(handle);
            handle = {};
        }
        doorScene = 0;
        SetStageLightFrame(0);
        lastDoorTime = -1;
        lastTimelineTick = {};
        GakuMotion::externalStagePlaying.store(false);
        GakuMotion::externalStageSeek.store(-1);
        GakuMotion::externalStageTime.store(0);
    }

    inline void BindDoors()
    {
        doorScene = mountedScene;
        auto& api = StageLoader::metadata;
        void* quat = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Quaternion");
        uint32_t alignment = 0;
        if (!quat || !StageLoader::valueSize ||
            StageLoader::valueSize(quat, &alignment) != sizeof(UnityQuaternion)) return;
        setDoorRotation = api.Find(StageLoader::transformClass, "set_localRotation", false, {0x01}, {{0x11, quat}});
        if (!setDoorRotation || !StageLoader::goType || !StageLoader::goTransform || !StageLoader::objectName) return;
        bool inactive = true;
        void* args[]{StageLoader::goType, &inactive};
        void* array = StageLoader::Invoke(StageLoader::findAll, nullptr, args);
        Handle pin = array ? StageLoader::newHandle(array, false) : nullptr;
        if (!pin) return;
        const auto& names = profile.doorNames;
        const auto count = StageLoader::arrayLength(array);
        auto** objects = reinterpret_cast<void**>(static_cast<char*>(array) + 32);
        for (uintptr_t i = 0; i < count; ++i)
        {
            void* object = objects[i];
            if (!object || StageLoader::SceneOfFreshGo(object) != mountedScene) continue;
            const auto name = Il2CppStringToUtf8(StageLoader::Invoke(StageLoader::objectName, object));
            for (size_t j = 0; j < names.size(); ++j)
            {
                if (doorTransforms[j] || name != names[j]) continue;
                if (void* transform = StageLoader::Invoke(StageLoader::goTransform, object))
                    doorTransforms[j] = StageLoader::newHandle(transform, false);
            }
        }
        StageLoader::freeHandle(pin);
        GakuMotion::host::Info("[GakuMotion][ExternalStage] timeline doors=%d/%d scene=%d",
            !!doorTransforms[0], !!doorTransforms[1], mountedScene);
    }

    inline void TickTimeline(float motionTime, bool hasMotion)
    {
        if (status.load() != 1 || !mountedScene)
        {
            ResetTimeline();
            return;
        }
        if (doorScene != mountedScene)
        {
            ResetTimeline();
            BindDoors();
        }
        using namespace GakuMotion;
        const auto now = std::chrono::steady_clock::now();
        const float dt = lastTimelineTick.time_since_epoch().count() ?
            std::chrono::duration<float>(now - lastTimelineTick).count() : 0;
        lastTimelineTick = now;
        const float seek = externalStageSeek.exchange(-1);
        float time = externalStageTime.load();
        if (externalStageFollow.load())
        {
            externalStagePlaying.store(false);
            time = hasMotion ? motionTime : 0.0f;
        }
        else if (seek >= 0.0f)
            time = seek;
        else if (externalStagePlaying.load())
            time = time >= externalStageDuration ? 0.0f : time + dt;
        time = std::clamp(time, 0.0f, externalStageDuration);
        externalStageTime.store(time);
        if (time >= externalStageDuration) externalStagePlaying.store(false);
        SetStageShaderFloat("_GakuExternalStageGradeEnabled", externalStageGrade.load() ? 1.0f : 0.0f, gradeId);
        if (time == lastDoorTime) return;
        lastDoorTime = time;
        SetStageLightFrame(time * 30.0f);
        ApplyStageTravel(time * 30.0f);
        if (!setDoorRotation) return;

        const float fraction = std::clamp((time * 30.0f - 2933.0f) / 61.0f, 0.0f, 1.0f);
        // VMD rotation interpolation uses the ending key's control points.
        float lower = 0.0f, upper = 1.0f;
        for (int step = 0; step < 16; ++step)
        {
            const float t = (lower + upper) * 0.5f;
            const float u = 1.0f - t;
            const float x = (3.0f * u * u * t * 42.0f + 3.0f * u * t * t * 85.0f) / 127.0f + t * t * t;
            if (x < fraction) lower = t; else upper = t;
        }
        const float t = fraction == 0.0f || fraction == 1.0f ? fraction : (lower + upper) * 0.5f;
        const float angle = std::atan2(0.537299633f, 0.843391418f) * t * t * (3.0f - 2.0f * t);
        for (size_t i = 0; i < doorTransforms.size(); ++i)
        {
            void* door = doorTransforms[i] ? SceneOwnerRuntime::handleTarget(doorTransforms[i]) : nullptr;
            if (!door) continue;
            // OBJ import mirrors X, reversing the source PMX/VMD Y rotations.
            UnityQuaternion rotation{0, std::sin(i == 0 ? angle : -angle), 0, std::cos(angle)};
            void* args[]{&rotation};
            StageLoader::Invoke(setDoorRotation, door, args);
        }
    }

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
            if (handle && Il2CppStringToUtf8(StageLoader::Invoke(StageLoader::sceneName, nullptr, sceneArgs)) == profile.sceneName)
                return handle;
        }
        return 0;
    }

    inline float MeasureActorHeight(void* actor)
    {
        if (!actor || !getBounds || !g_getParentNative || !g_safeBones.unbox) return -1;
        void* object = StageLoader::Invoke(StageLoader::componentGo, actor);
        if (!object || StageLoader::SceneOfFreshGo(object) != owner.sceneHandle) return -1;
        void* root = StageLoader::Invoke(StageLoader::goTransform, object);
        if (!root) return -1;
        void* klass = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "SkinnedMeshRenderer");
        void* type = klass ? il2cpp_type_get_object(il2cpp_class_get_type(klass)) : nullptr;
        if (!type) return -1;
        bool inactive = false;
        void* args[]{type, &inactive};
        void* list = StageLoader::Invoke(StageLoader::findAll, nullptr, args);
        Handle pin = list ? StageLoader::newHandle(list, false) : nullptr;
        if (!pin) return -1;
        float bottom = INFINITY, top = -INFINITY;
        const auto count = StageLoader::arrayLength(list);
        for (uintptr_t i = 0; i < count; ++i)
        {
            auto** items = reinterpret_cast<void**>(static_cast<char*>(SceneOwnerRuntime::handleTarget(pin)) + 32);
            void* renderer = items[i];
            if (!renderer || !StageLoader::ReadBool(getEnabled, renderer, false)) continue;
            void* go = StageLoader::Invoke(StageLoader::componentGo, renderer);
            void* transform = go ? StageLoader::Invoke(StageLoader::goTransform, go) : nullptr;
            for (unsigned depth = 0; transform && transform != root && depth < 128; ++depth)
                transform = g_getParentNative(transform);
            if (transform != root) continue;
            void* boxed = StageLoader::Invoke(getBounds, renderer);
            const auto* bounds = boxed ? static_cast<const float*>(g_safeBones.unbox(boxed)) : nullptr;
            if (!bounds || !std::isfinite(bounds[1]) || !(bounds[4] > 0 && bounds[4] < 3)) continue;
            bottom = std::min(bottom, bounds[1] - bounds[4]);
            top = std::max(top, bounds[1] + bounds[4]);
        }
        StageLoader::freeHandle(pin);
        return top > bottom ? top - bottom : -1;
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
        // The game renders the native background even when this getter reports zero.
        // Only a nonzero effective mask can establish a scene-bit exclusion.
        const bool hasSceneMask = ReadMask(getSceneMask, camera, cameraScenes) && cameraScenes != 0;
        unsigned stageCount = 0, visibleLayers = 0, sceneRejected = 0, frustumHits = 0;
        void* planeArgs[]{camera};
        void* planes = calculatePlanes ? StageLoader::Invoke(calculatePlanes, nullptr, planeArgs) : nullptr;
        Handle planeHandle = planes ? StageLoader::newHandle(planes, false) : nullptr;
        std::vector<uintptr_t> stageRenderers;
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
            stageRenderers.push_back(i);
            if (inMask) ++visibleLayers;
            uint64_t objectScenes = 0;
            if (hasSceneMask && ReadMask(getObjectSceneMask, object, objectScenes) && objectScenes)
            {
                stageScenes |= objectScenes;
                if (!(objectScenes & cameraScenes)) ++sceneRejected;
            }
        }
        const auto countFrustumHits = [&]() {
            unsigned hits = 0;
            for (const auto index : stageRenderers)
            {
                void* boxed = StageLoader::Invoke(getBounds, renderers[index]);
                void* raw = boxed && g_safeBones.unbox ? g_safeBones.unbox(boxed) : nullptr;
                if (!raw) continue;
                std::array<float, 6> bounds{};
                std::memcpy(bounds.data(), raw, sizeof(bounds));
                void* args[]{SceneOwnerRuntime::handleTarget(planeHandle), bounds.data()};
                if (args[0] && StageLoader::ReadBool(testBounds, nullptr, false, args)) ++hits;
            }
            return hits;
        };
        bool framingReady = true;
        if (planeHandle && testBounds && getBounds)
        {
            frustumHits = countFrustumHits();
            void* transform = stageRoot ? SceneOwnerRuntime::handleTarget(stageRoot) : nullptr;
            if (!frustumHits && transform && g_setWorldRotNative && g_setWorldPosNative)
            {
                constexpr float halfPi = 1.570796327f;
                float bestYaw = 0;
                for (const float yaw : {halfPi, -halfPi, 2 * halfPi})
                {
                    SetStagePlacement(transform, yaw);
                    const auto hits = countFrustumHits();
                    if (hits > frustumHits) { frustumHits = hits; bestYaw = yaw; }
                }
                SetStagePlacement(transform, bestYaw);
                GakuMotion::host::Info("[GakuMotion][ExternalStage] camera framing yaw=%g frustum=%u/%u",
                    stageYaw, frustumHits, stageCount);
            }
            framingReady = frustumHits != 0;
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
        GakuMotion::host::Info("[GakuMotion][ExternalStage] visibility stage_renderers=%u camera_layers=%08x layers_before=%u relayered=%u native_layer=%d camera_scenes=%llx stage_scenes=%llx scene_mask_known=%d scene_rejected=%u scene_mask_ready=%d frustum=%d/%u",
            stageCount, layers, visibleLayers, relayered, layer, static_cast<unsigned long long>(cameraScenes),
            static_cast<unsigned long long>(stageScenes), hasSceneMask, sceneRejected, sceneMaskReady,
            planeHandle && testBounds && getBounds ? static_cast<int>(frustumHits) : -1, stageCount);
        return stageCount != 0 && visibleLayers + relayered != 0 && sceneMaskReady && framingReady;
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

    inline void ReleaseHeldProp();

    inline bool AlignToActor(int background, void* actor, GakuMotion::IdolController* controller)
    {
        if (alignedBackground == background) return true;
        if (!g_getWorldPosNative || !StageLoader::setPosition || !StageLoader::goType) return false;

        // Only use a live actor owned by the current photography presenter.
        std::optional<UnityVector3> anchor;
        void* actorObject = actor ? StageLoader::Invoke(StageLoader::componentGo, actor) : nullptr;
        if (actorObject && StageLoader::SceneOfFreshGo(actorObject) == owner.sceneHandle)
            if (void* transform = StageLoader::Invoke(StageLoader::goTransform, actorObject))
                anchor = g_getWorldPosNative(transform);
        for (void* type : StageLoader::actorTypes)
        {
            if (anchor) break;
            if (!type) continue;
            bool inactive = false;
            void* args[]{type, &inactive};
            void* list = StageLoader::Invoke(StageLoader::findAll, nullptr, args);
            Handle pin = list ? StageLoader::newHandle(list, false) : nullptr;
            if (!pin) continue;
            auto** actors = reinterpret_cast<void**>(static_cast<char*>(list) + 32);
            const auto count = StageLoader::arrayLength(list);
            for (uintptr_t i = 0; i < count; ++i)
            {
                void* object = actors[i] ? StageLoader::Invoke(StageLoader::componentGo, actors[i]) : nullptr;
                if (!object || StageLoader::SceneOfFreshGo(object) != owner.sceneHandle) continue;
                void* transform = StageLoader::Invoke(StageLoader::goTransform, object);
                if (!transform) continue;
                anchor = g_getWorldPosNative(transform);
                break;
            }
            StageLoader::freeHandle(pin);
        }
        if (anchor && actorObject && controller && controller->fnGetBoneTransform &&
            StageLoader::SceneOfFreshGo(actorObject) == owner.sceneHandle)
        {
            void* left = controller->fnGetBoneTransform(actor, 5);
            void* right = controller->fnGetBoneTransform(actor, 6);
            if (left && right)
            {
                const auto l = g_getWorldPosNative(left), r = g_getWorldPosNative(right);
                if (std::isfinite(l.x + r.x) && std::isfinite(l.z + r.z))
                {
                    GakuMotion::host::Info("[GakuMotion][ExternalStage] actor pivot=(%g,%g,%g) feet=(%g,%g,%g)/(%g,%g,%g)",
                        anchor->x, anchor->y, anchor->z, l.x, l.y, l.z, r.x, r.y, r.z);
                    anchor->x = (l.x + r.x) * 0.5f;
                    anchor->y = std::min(l.y, r.y);
                    anchor->z = (l.z + r.z) * 0.5f;
                }
            }
        }
        if (!anchor || !std::isfinite(anchor->x) || !std::isfinite(anchor->y) || !std::isfinite(anchor->z)) return false;

        bool inactive = false;
        void* args[]{StageLoader::goType, &inactive};
        void* list = StageLoader::Invoke(StageLoader::findAll, nullptr, args);
        Handle pin = list ? StageLoader::newHandle(list, false) : nullptr;
        if (!pin) return false;
        auto** objects = reinterpret_cast<void**>(static_cast<char*>(list) + 32);
        const auto count = StageLoader::arrayLength(list);
        bool aligned = false;
        for (uintptr_t i = 0; i < count; ++i)
        {
            void* object = objects[i];
            if (!object || StageLoader::SceneOfFreshGo(object) != mountedScene ||
                Il2CppStringToUtf8(StageLoader::Invoke(StageLoader::objectName, object)) != profile.rootName)
                continue;
            void* transform = StageLoader::Invoke(StageLoader::goTransform, object);
            if (!transform) break;
            ReleaseHeldProp();
            stageAnchor = *anchor;
            stageYaw = 0;
            if (g_setWorldRotNative) g_setWorldRotNative(transform, {0, 0, 0, 1});
            auto position = profile.rootOffset;
            position.x += anchor->x;
            position.y += anchor->y;
            position.z += anchor->z;
            void* setArgs[]{&position};
            StageLoader::Invoke(StageLoader::setPosition, transform, setArgs);
            const auto after = g_getWorldPosNative(transform);
            if (stageRoot) StageLoader::freeHandle(stageRoot);
            stageRoot = StageLoader::newHandle(transform, false);
            stageOrigin = position;
            stageTravel = {};
            lastDoorTime = -1;
            aligned = stageRoot && std::abs(after.x - position.x) < 0.002f && std::abs(after.y - position.y) < 0.002f &&
                std::abs(after.z - position.z) < 0.002f;
            GakuMotion::host::Info("[GakuMotion][ExternalStage] anchor background=%d actor=(%g,%g,%g) stage=(%g,%g,%g) aligned=%d",
                background, anchor->x, anchor->y, anchor->z, after.x, after.y, after.z, aligned);
            break;
        }
        StageLoader::freeHandle(pin);
        if (aligned) { alignedBackground = background; alignedFloor = anchor->y; }
        return aligned;
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
            "GakuLoader/plugins/GakuMotion.ExternalStage" / profile.bundleFile).u8string();
        void* path = StageLoader::stringNew(reinterpret_cast<const char*>(assetPath.c_str()));
        void* args[]{path};
        return path && Start(Pending::Bundle, StageLoader::Invoke(openBundle, nullptr, args));
    }

    inline void Tick(bool admitted, int background, const GakuMotion::injection::OwnerSnapshot& current, void* actor, GakuMotion::IdolController* controller = nullptr)
    {
        const bool wanted = admitted && (!owner.Valid() || owner.SameOwner(current));
        if (!wanted)
        {
            ReleaseHeldProp();
            if (stageRoot) StageLoader::freeHandle(stageRoot);
            stageRoot = {};
        }
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
                void* path = StageLoader::stringNew(profile.scenePath);
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
            alignedBackground = 0;
            nextAlignment = {};
            attempted = false;
            status.store(0);
            return;
        }

        if (mountedScene)
        {
            if (!background) { status.store(0); return; }
            if (alignedBackground != background)
            {
                status.store(2);
                const auto now = std::chrono::steady_clock::now();
                if (now < nextAlignment) return;
                nextAlignment = now + std::chrono::milliseconds(150);
                if (!AlignToActor(background, actor, controller)) return;
            }
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
