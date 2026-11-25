#pragma once
#include "GkMeniStageSource.hpp"

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
    inline Handle cameraLease{}, rangeCameraLease{};
    inline const void *getFarClip{}, *setFarClip{}, *getOcclusion{}, *setOcclusion{};
    inline float originalFarClip{}, appliedFarClip{};
    inline bool originalOcclusion{}, changedOcclusion{};
    inline int originalClearFlags{};
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
    constexpr std::array<std::string_view, 3> animatedMeshNames{
        "StageAnimated_st_light_l_001", "StageAnimated_moon", "StageAnimated_star"};
    inline std::array<Handle, animatedMeshNames.size()> animatedMeshTransforms{};
    inline const void* setDoorRotation{};
    inline int doorScene{};
    inline float lastDoorTime = -1;
    inline std::chrono::steady_clock::time_point lastTimelineTick{};

    inline bool SetStageShaderFloat(const char* property, float frame, int& id)
    {
        static const void* setter = StageLoader::metadata.Find(
            FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Shader"), "SetGlobalFloat",
            true, {0x01}, {{0x0e}, {0x0c}});
        if (!setter || !StageLoader::stringNew) return false;
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
            return StageLoader::TryInvokeStaticVoid(setterById, args);
        }
        else if (void* name = StageLoader::stringNew(property))
        {
            void* args[]{name, &frame};
            return StageLoader::TryInvokeStaticVoid(setter, args);
        }
        return false;
    }

    inline int lightFrameId{}, gradeId{}, foregroundMaskId{};
    inline bool foregroundMaskActive{};
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

    inline UnityVector3 StageTravelAtFrame(float frame)
    {
        const auto key = GakuMotion::source_vmd::Sample(ExternalStageProfiles::meniRoot, frame);
        return RotateStageOffset({-key.position.x * profile.sourceScale,
            key.position.y * profile.sourceScale, key.position.z * profile.sourceScale});
    }

    inline void ApplyStageTravel(float frame)
    {
        if (!stageRoot || !g_setWorldPosNative) return;
        void* transform = SceneOwnerRuntime::handleTarget(stageRoot);
        if (!transform) return;
        stageTravel = StageTravelAtFrame(frame);
        auto position = stageOrigin;
        position.x += stageTravel.x;
        position.y += stageTravel.y;
        position.z += stageTravel.z;
        g_setWorldPosNative(transform, position);
    }
    inline void SetStageLightFrame(float frame) { SetStageShaderFloat(profile.lightFrameProperty, frame, lightFrameId); }

    inline void UpdateForegroundMask(const GakuMotion::CameraController& camera)
    {
        const float f = GakuMotion::externalStageTime.load() * 30.f;
        const bool crossing = (f >= 1775.f && f < 1830.f) || (f >= 1960.f && f < 2055.f) ||
            (f >= 2330.f && f < 2445.f);
        const bool active = status.load() == 1 && mountedScene && GakuMotion::externalStageFollow.load() &&
            camera.owned && camera.settings.mode == GakuMotion::CameraMode::Track && crossing;
        if (active == foregroundMaskActive) return;
        foregroundMaskActive = active;
        SetStageShaderFloat("_GakuExternalStageForegroundMask", active ? 1.f : 0.f, foregroundMaskId);
    }

    inline void ResetTimeline()
    {
        if (foregroundMaskActive)
        {
            SetStageShaderFloat("_GakuExternalStageForegroundMask", 0.f, foregroundMaskId);
            foregroundMaskActive = false;
        }
        if (!doorScene) return;
        const auto clear = [](auto& handles) {
            for (auto& handle : handles)
            {
                if (handle) StageLoader::freeHandle(handle);
                handle = {};
            }
        };
        clear(doorTransforms);
        clear(animatedMeshTransforms);
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
            const auto bind = [&](const auto& names, auto& handles) {
                for (size_t j = 0; j < names.size(); ++j)
                {
                    if (handles[j] || name != names[j]) continue;
                    if (void* transform = StageLoader::Invoke(StageLoader::goTransform, object))
                        handles[j] = StageLoader::newHandle(transform, false);
                }
            };
            bind(names, doorTransforms);
            bind(animatedMeshNames, animatedMeshTransforms);
        }
        StageLoader::freeHandle(pin);
        GakuMotion::host::Info("[GakuMotion][ExternalStage] source timeline doors=%d/%d movingMesh=%d/%d/%d scene=%d",
            !!doorTransforms[0], !!doorTransforms[1], !!animatedMeshTransforms[0],
            !!animatedMeshTransforms[1], !!animatedMeshTransforms[2], mountedScene);
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
        const float frame = time * 30.0f;
        SetStageLightFrame(frame);
        ApplyStageTravel(frame);
        if (g_setPosNative)
        {
            const std::array<std::span<const GakuMotion::source_vmd::BoneKey>, animatedMeshNames.size()> tracks{
                ExternalStageProfiles::st_light_l_001, ExternalStageProfiles::moon, ExternalStageProfiles::star};
            for (size_t i = 0; i < tracks.size(); ++i)
            {
                void* object = animatedMeshTransforms[i] ? SceneOwnerRuntime::handleTarget(animatedMeshTransforms[i]) : nullptr;
                if (!object) continue;
                const auto p = GakuMotion::source_vmd::Sample(tracks[i], frame).position;
                // The geometry parent already supplies the MMD-to-Unity scale.
                g_setPosNative(object, {-p.x, p.y, p.z});
            }
        }
        if (!setDoorRotation) return;

        for (size_t i = 0; i < doorTransforms.size(); ++i)
        {
            void* door = doorTransforms[i] ? SceneOwnerRuntime::handleTarget(doorTransforms[i]) : nullptr;
            if (!door) continue;
            const auto keys = i == 0 ? std::span{ExternalStageProfiles::meniLeftDoor}
                                     : std::span{ExternalStageProfiles::meniRightDoor};
            const auto q = GakuMotion::source_vmd::Sample(keys, frame).rotation;
            // Mirror the original X axis to match the imported stage mesh.
            UnityQuaternion rotation{q.x, -q.y, -q.z, q.w};
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
        getFarClip = api.Find(camera, "get_farClipPlane", false, {0x0c}, {});
        setFarClip = api.Find(camera, "set_farClipPlane", false, {0x01}, {{0x0c}});
        getOcclusion = api.Find(camera, "get_useOcclusionCulling", false, {0x02}, {});
        setOcclusion = api.Find(camera, "set_useOcclusionCulling", false, {0x01}, {{0x02}});
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

    inline float ReadFarClip(void* camera)
    {
        void* boxed = StageLoader::Invoke(getFarClip, camera);
        const auto* value = boxed && g_safeBones.unbox ? static_cast<float*>(g_safeBones.unbox(boxed)) : nullptr;
        return value && std::isfinite(*value) ? *value : 0;
    }

    inline void RestoreCameraRange()
    {
        if (!rangeCameraLease) return;
        void* camera = SceneOwnerRuntime::handleTarget(rangeCameraLease);
        if (camera && CameraBridge::IsAlive(camera))
        {
            if (appliedFarClip > 0 && std::abs(ReadFarClip(camera) - appliedFarClip) < 0.01f)
            {
                void* args[]{&originalFarClip};
                StageLoader::Invoke(setFarClip, camera, args);
            }
            if (originalClearFlags && HelperCameraGetClearFlags(camera) == 2)
                HelperCameraSetClearFlags(camera, originalClearFlags);
            bool current = true;
            if (changedOcclusion && ModelRendererMask::ReadBool(getOcclusion, camera, current) && !current)
                ModelRendererMask::WriteBool(setOcclusion, getOcclusion, camera, originalOcclusion);
        }
        StageLoader::freeHandle(rangeCameraLease);
        rangeCameraLease = {};
        appliedFarClip = 0;
        changedOcclusion = false;
        originalClearFlags = 0;
    }

    inline void UpdateCameraRange(void* camera)
    {
        if (!camera || !CameraBridge::IsAlive(camera)) return;
        const bool newCamera = !rangeCameraLease || SceneOwnerRuntime::handleTarget(rangeCameraLease) != camera;
        if (newCamera)
        {
            RestoreCameraRange();
            rangeCameraLease = StageLoader::newHandle(camera, false);
            if (!rangeCameraLease) return;
            originalFarClip = getFarClip && setFarClip ? ReadFarClip(camera) : 0;
            originalOcclusion = false;
            if (getOcclusion && setOcclusion)
                ModelRendererMask::ReadBool(getOcclusion, camera, originalOcclusion);
        }
        // Replace only the host skybox while this external stage owns photography.
        const int flags = status.load() == 1 ? HelperCameraGetClearFlags(camera) : 0;
        if (flags == 1)
        {
            HelperCameraSetClearFlags(camera, 2);
            if (HelperCameraGetClearFlags(camera) == 2 && !originalClearFlags)
            {
                originalClearFlags = flags;
                GakuMotion::host::Info("[GakuMotion][ExternalStage] native skybox suppressed");
            }
        }
        float cameraDistance = 0;
        void* go = StageLoader::Invoke(StageLoader::componentGo, camera);
        if (void* transform = go ? StageLoader::Invoke(StageLoader::goTransform, go) : nullptr)
        {
            const auto p = g_getWorldPosNative(transform);
            cameraDistance = std::hypot(p.x - stageAnchor.x, p.y - stageAnchor.y, p.z - stageAnchor.z);
        }
        const float target = profile.visibilityRadius + (std::isfinite(cameraDistance) ? cameraDistance : 0) + 10.0f;
        const float currentFar = originalFarClip > 0 ? ReadFarClip(camera) : 0;
        if (currentFar > 0 && currentFar < target)
        {
            if (!appliedFarClip || std::abs(currentFar - appliedFarClip) > 0.01f) originalFarClip = currentFar;
            float expanded = std::ceil((target + 10) / 10) * 10;
            void* args[]{&expanded};
            StageLoader::Invoke(setFarClip, camera, args);
            if (std::abs(ReadFarClip(camera) - expanded) < 0.01f) appliedFarClip = expanded;
        }
        bool occlusion = false;
        if (getOcclusion && setOcclusion && ModelRendererMask::ReadBool(getOcclusion, camera, occlusion) && occlusion)
        {
            if (ModelRendererMask::WriteBool(setOcclusion, getOcclusion, camera, false))
            {
                originalOcclusion = true;
                changedOcclusion = true;
            }
        }
        if (newCamera)
            GakuMotion::host::Info("[GakuMotion][ExternalStage] camera range original=%g actual=%g occlusion_override=%d",
                originalFarClip, ReadFarClip(camera), changedOcclusion);
    }

    inline void RestoreCamera()
    {
        RestoreCameraRange();
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
        UpdateCameraRange(camera);
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

    struct GroundOverlayMaterial { Handle material; float originalCutoff; };
    inline std::vector<GroundOverlayMaterial> groundOverlays;
    inline const void* getOverlayCutoff{};
    inline const void* setOverlayCutoff{};
    inline bool groundOverlayVisible = true;

    inline float ReadOverlayCutoff(void* material, void* name)
    {
        void* args[]{name};
        void* boxed = StageLoader::Invoke(getOverlayCutoff, material, args);
        const void* data = boxed && g_safeBones.unbox ? g_safeBones.unbox(boxed) : nullptr;
        return data ? *static_cast<const float*>(data) : std::numeric_limits<float>::quiet_NaN();
    }

    inline void BindGroundOverlays(void** renderers, uintptr_t count)
    {
        const auto unityClass = [](const char* name) {
            return FindClass("UnityEngine.CoreModule.dll", "UnityEngine", name);
        };
        auto& api = StageLoader::metadata;
        void* material = unityClass("Material");
        void* shader = unityClass("Shader");
        const auto arrayClass = reinterpret_cast<void* (*)(void*, uint32_t)>(
            GetProcAddress(g_gameAssembly, "il2cpp_array_class_get"));
        void* materials = arrayClass && material ? arrayClass(material, 1) : nullptr;
        if (!materials || !shader) return;
        const void* getMaterials = api.Find(rendererClass, "get_sharedMaterials", false, {0x1d, materials}, {});
        const void* getShader = api.Find(material, "get_shader", false, {0x12, shader}, {});
        getOverlayCutoff = api.Find(material, "GetFloat", false, {0x0c}, {{0x0e}});
        setOverlayCutoff = api.Find(material, "SetFloat", false, {0x01}, {{0x0e}, {0x0c}});
        if (!getMaterials || !getShader || !getOverlayCutoff || !setOverlayCutoff || !StageLoader::stringNew)
        {
            GakuMotion::host::Warn("[GakuMotion][GroundOverlay] material API unavailable");
            return;
        }
        void* name = StageLoader::stringNew("_Cutoff");
        if (!name) return;
        for (uintptr_t i = 0; i < count; ++i)
        {
            void* renderer = renderers[i];
            void* go = StageLoader::Invoke(StageLoader::componentGo, renderer);
            if (!go || StageLoader::SceneOfFreshGo(go) != mountedScene) continue;
            void* array = StageLoader::Invoke(getMaterials, renderer);
            const Handle pin = array ? StageLoader::newHandle(array, false) : nullptr;
            if (!pin) continue;
            const auto length = StageLoader::arrayLength(array);
            for (uintptr_t slot = 0; slot < length; ++slot)
            {
                void* pinnedArray = SceneOwnerRuntime::handleTarget(pin);
                if (!pinnedArray) break;
                void* mat = reinterpret_cast<void**>(static_cast<char*>(pinnedArray) + 32)[slot];
                if (!mat || std::any_of(groundOverlays.begin(), groundOverlays.end(),
                    [mat](const auto& found) { return SceneOwnerRuntime::handleTarget(found.material) == mat; }))
                    continue;
                if (Il2CppStringToUtf8(StageLoader::Invoke(StageLoader::objectName, mat)) != "material_08") continue;
                void* assigned = StageLoader::Invoke(getShader, mat);
                if (!assigned || Il2CppStringToUtf8(StageLoader::Invoke(StageLoader::objectName, assigned)) !=
                    "GakuMotion/ExternalStageGroundOverlay") continue;
                const float cutoff = ReadOverlayCutoff(mat, name);
                if (std::isfinite(cutoff))
                    if (Handle handle = StageLoader::newHandle(mat, false)) groundOverlays.push_back({handle, cutoff});
            }
            StageLoader::freeHandle(pin);
        }
        GakuMotion::host::Info("[GakuMotion][GroundOverlay] stage=%d material_08 bound=%zu",
            mountedScene, groundOverlays.size());
    }

    inline void ApplyGroundOverlay(bool visible)
    {
        if (groundOverlayVisible == visible || !StageLoader::stringNew) return;
        void* name = StageLoader::stringNew("_Cutoff");
        if (!name) return;
        size_t verified = 0;
        for (const auto& overlay : groundOverlays)
        {
            void* mat = SceneOwnerRuntime::handleTarget(overlay.material);
            if (!mat) continue;
            // Texture alpha is in [0, 1]. A cutoff of 2 discards the entire pass.
            float cutoff = visible ? overlay.originalCutoff : 2.f;
            void* args[]{name, &cutoff};
            StageLoader::Invoke(setOverlayCutoff, mat, args);
            if (ReadOverlayCutoff(mat, name) == cutoff) ++verified;
        }
        groundOverlayVisible = visible;
        GakuMotion::host::Info("[GakuMotion][GroundOverlay] stage=%d material_08 visible=%d verified=%zu/%zu",
            mountedScene, visible, verified, groundOverlays.size());
    }

    inline void ReleaseGroundOverlays()
    {
        ApplyGroundOverlay(true);
        for (const auto& overlay : groundOverlays) StageLoader::freeHandle(overlay.material);
        groundOverlays.clear();
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
        if (complete && found) BindGroundOverlays(renderers, count);
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
        ReleaseGroundOverlays();
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
                ReleaseGroundOverlays();
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
            if (hiddenScene) UpdateCameraRange(StageLoader::Invoke(StageLoader::cameraMain));
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
            ApplyGroundOverlay(GakuMotion::externalStageGroundOverlay.load());
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
