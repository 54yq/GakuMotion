#pragma once

// Read-only, bounded Photography diagnostics. Called by the existing status
// sampler; it installs no hooks and retains no managed object between calls.
namespace StudioDiagnostics
{
    using Type = GakuMotion::ExactMethodMetadata::Type;
    inline bool attempted{};
    inline void *cameraClass{}, *typeClass{}, *additionalClass{}, *layerMaskClass{}, *volumeClass{};
    inline const void *getLayer{}, *getActive{}, *getCullingMask{}, *getNear{}, *getFar{}, *getComponent{};
    inline const void *getVolumeMask{}, *getPostProcessing{}, *getEnabled{};

    inline std::string Quote(const std::string& value)
    { return "\"" + GakuMotion::jsonlite::EscapeString(value) + "\""; }
    inline std::string Identity(void* value)
    { return Quote(std::to_string(reinterpret_cast<uintptr_t>(value))); }
    inline std::string Number(float value)
    { return std::isfinite(value) ? std::to_string(value) : "null"; }
    inline std::string Vector(const GakuMotion::Vector3& value)
    { return "[" + Number(value.x) + "," + Number(value.y) + "," + Number(value.z) + "]"; }
    inline std::string Rotation(const GakuMotion::Quaternion& value)
    { return "[" + Number(value.x) + "," + Number(value.y) + "," + Number(value.z) + "," + Number(value.w) + "]"; }
    inline bool Pose(void* transform, GakuMotion::Vector3& position, GakuMotion::Quaternion& rotation)
    {
        if (!transform || !g_getWorldPosNative || !g_getWorldRotNative || !CameraBridge::IsAlive(transform)) return false;
        float p[3]{}, q[4]{};
        HelperGetWorldPos(transform, p); HelperGetWorldRot(transform, q);
        position = {p[0], p[1], p[2]}; rotation = {q[0], q[1], q[2], q[3]};
        return GakuMotion::CameraFinite(position) && std::isfinite(q[0]) && std::isfinite(q[1]) &&
            std::isfinite(q[2]) && std::isfinite(q[3]);
    }
    inline bool Read(const void* method, void* self, void* output, size_t size, void* expectedBox = nullptr)
    {
        void* boxed = nullptr;
        if (!method || !StudioRuntime::Call(method, self, nullptr, &boxed) || !boxed ||
            (expectedBox && g_faceRuntime.objectClass(boxed) != expectedBox)) return false;
        void* raw = g_safeBones.unbox(boxed);
        if (!raw) return false;
        std::memcpy(output, raw, size); return true;
    }
    inline void Resolve()
    {
        if (attempted) return;
        attempted = true;
        auto& meta = StageLoader::metadata;
        void* go = StudioRuntime::gameObjectClass;
        cameraClass = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Camera");
        void* behaviour = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Behaviour");
        typeClass = FindClass("mscorlib.dll", "System", "Type");
        additionalClass = FindClass("Unity.RenderPipelines.Universal.Runtime.dll", "UnityEngine.Rendering.Universal", "UniversalAdditionalCameraData");
        layerMaskClass = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "LayerMask");
        volumeClass = FindClass("Unity.RenderPipelines.Core.Runtime.dll", "UnityEngine.Rendering", "Volume");
        getLayer = go ? meta.Find(go, "get_layer", false, {0x08}, {}) : nullptr;
        getActive = go ? meta.Find(go, "get_activeInHierarchy", false, {0x02}, {}) : nullptr;
        getCullingMask = cameraClass ? meta.Find(cameraClass, "get_cullingMask", false, {0x08}, {}) : nullptr;
        getNear = cameraClass ? meta.Find(cameraClass, "get_nearClipPlane", false, {0x0c}, {}) : nullptr;
        getFar = cameraClass ? meta.Find(cameraClass, "get_farClipPlane", false, {0x0c}, {}) : nullptr;
        getEnabled = behaviour ? meta.Find(behaviour, "get_enabled", false, {0x02}, {}) : nullptr;
        if (go && typeClass && StageLoader::componentClass)
            getComponent = meta.Find(go, "GetComponent", false, {0x12, StageLoader::componentClass}, {{0x12, typeClass}});
        uint32_t alignment = 0;
        if (additionalClass && layerMaskClass && StageLoader::valueSize &&
            StageLoader::valueSize(layerMaskClass, &alignment) == sizeof(uint32_t) && alignment == alignof(uint32_t))
            getVolumeMask = meta.Find(additionalClass, "get_volumeLayerMask", false, {0x11, layerMaskClass}, {});
        if (additionalClass) getPostProcessing = meta.Find(additionalClass, "get_renderPostProcessing", false, {0x02}, {});
    }
    inline void* Component(StudioRuntime::ScopedRoots& roots, void* go, void* klass)
    {
        if (!getComponent || !go || !klass || !il2cpp_class_get_type || !il2cpp_type_get_object) return nullptr;
        void* type = roots.Keep(il2cpp_type_get_object(il2cpp_class_get_type(klass)));
        void* args[]{type}; void* value = nullptr;
        if (!type || !StudioRuntime::Call(getComponent, go, args, &value)) return nullptr;
        value = roots.Keep(value);
        return value && StageLoader::assignable(klass, g_faceRuntime.objectClass(value)) ? value : nullptr;
    }
    inline std::string SceneObject(void* object, void* go)
    {
        const int id = object ? StageLoader::ReadInt(StageLoader::instanceId, object) : 0;
        return "\"identity\":" + Identity(object) + ",\"instance_id\":" + std::to_string(id) +
            ",\"scene_handle\":" + std::to_string(go ? StageLoader::SceneOfFreshGo(go) : 0);
    }
    inline std::string Snapshot(const StudioRuntime::Context& context)
    {
        const auto fresh = StudioRuntime::Current();
        if (!context.allowed || !context.owner.Valid() ||
            context.owner.role != GakuMotion::injection::OwnerRole::Photography || !ProtectionOnUnityThread() ||
            !g_runtimeReady.load(std::memory_order_acquire) || !fresh.allowed || !context.SameOwner(fresh))
            return "{\"available\":false,\"reason\":\"photography-gate-denied\"}";
        if (!StageLoader::instanceId || !StageLoader::componentGo || !StageLoader::cameraMain ||
            !g_safeBones.unbox || !g_faceRuntime.objectClass || !StageLoader::assignable ||
            !StudioRuntime::gameObjectClass || !StudioRuntime::transformClass)
            return "{\"available\":false,\"reason\":\"existing-runtime-api-unavailable\"}";
        Resolve();
        StudioRuntime::ScopedRoots roots;
        auto& core = GakuMotion::Core::Instance();
        const auto* entry = core.sceneActorRegistry.Find(core.player.controller.targetActor);
        bool selected = entry != nullptr;
        if (!entry && !core.sceneActorRegistry.Entries().empty()) entry = &core.sceneActorRegistry.Entries().front();
        std::string actorJson = "{\"available\":false}", cameraJson = "{\"available\":false}", volumeJson = "{\"available\":false}";
        if (entry && entry->actor && CameraBridge::IsAlive(entry->actor))
        {
            void* actor = roots.Keep(entry->actor);
            void* go = roots.Keep(StageLoader::Invoke(StageLoader::componentGo, actor));
            const int id = StageLoader::ReadInt(StageLoader::instanceId, actor);
            if (go && id == entry->instanceId && StageLoader::SceneOfFreshGo(go) == context.owner.sceneHandle)
            {
                void* transform = roots.Keep(HelperGetComponentTransform(actor));
                void* hips = roots.Keep(HelperGetBoneTransform(actor, 0));
                void* head = roots.Keep(HelperGetBoneTransform(actor, 10));
                GakuMotion::Vector3 position, hipsPosition, headPosition;
                GakuMotion::Quaternion rotation, hipsRotation, headRotation;
                const bool rootPose = Pose(transform, position, rotation), hipsPose = Pose(hips, hipsPosition, hipsRotation), headPose = Pose(head, headPosition, headRotation);
                int layer = -1; uint8_t active = 0;
                const bool layerRead = Read(getLayer, go, &layer, sizeof(layer));
                const bool activeRead = Read(getActive, go, &active, sizeof(active));
                actorJson = "{\"available\":true," + SceneObject(actor, go) +
                    ",\"selected_target\":" + (selected ? "true" : "false") + ",\"registry_token\":" + std::to_string(entry->token) +
                    ",\"layer\":" + (layerRead ? std::to_string(layer) : "null") +
                    ",\"active_in_hierarchy\":" + (activeRead ? (active ? "true" : "false") : "null") +
                    ",\"raw_root_position\":" + (rootPose ? Vector(position) : "null") +
                    ",\"raw_root_rotation\":" + (rootPose ? Rotation(rotation) : "null") +
                    ",\"hips_position\":" + (hipsPose ? Vector(hipsPosition) : "null") +
                    ",\"head_position\":" + (headPose ? Vector(headPosition) : "null") + "}";
            }
        }
        uint32_t volumeMask = 0; bool volumeMaskRead = false;
        void* camera = roots.Keep(StageLoader::Invoke(StageLoader::cameraMain));
        if (camera && CameraBridge::IsAlive(camera))
        {
            void* transform = roots.Keep(HelperGetComponentTransform(camera));
            void* go = roots.Keep(StageLoader::Invoke(StageLoader::componentGo, camera));
            GakuMotion::CameraFrame frame;
            const bool pose = transform && CameraBridge::Read(camera, transform, frame);
            int culling = 0; float nearClip = 0, farClip = 0;
            const bool cullingRead = Read(getCullingMask, camera, &culling, sizeof(culling));
            const bool nearRead = Read(getNear, camera, &nearClip, sizeof(nearClip));
            const bool farRead = Read(getFar, camera, &farClip, sizeof(farClip));
            void* additional = Component(roots, go, additionalClass);
            uint8_t post = 0;
            volumeMaskRead = additional && Read(getVolumeMask, additional, &volumeMask, sizeof(volumeMask), layerMaskClass);
            const bool postRead = additional && Read(getPostProcessing, additional, &post, sizeof(post));
            cameraJson = "{\"available\":true," + SceneObject(camera, go) +
                ",\"position\":" + (pose ? Vector(frame.position) : "null") + ",\"rotation\":" + (pose ? Rotation(frame.rotation) : "null") +
                ",\"fov\":" + (pose ? Number(frame.fov) : "null") +
                ",\"culling_mask\":" + (cullingRead ? std::to_string(static_cast<uint32_t>(culling)) : "null") +
                ",\"near_clip\":" + (nearRead ? Number(nearClip) : "null") + ",\"far_clip\":" + (farRead ? Number(farClip) : "null") +
                ",\"volume_layer_mask\":" + (volumeMaskRead ? std::to_string(volumeMask) : "null") +
                ",\"post_processing\":" + (postRead ? (post ? "true" : "false") : "null") + "}";
        }
        if (StudioLighting::actorGo.Has())
        {
            void* go = roots.Keep(StudioLighting::actorGo.Get());
            if (go && StudioRuntime::Alive(go))
            {
                int layer = -1; uint8_t active = 0, enabled = 0;
                const bool layerRead = Read(getLayer, go, &layer, sizeof(layer));
                const bool activeRead = Read(getActive, go, &active, sizeof(active));
                void* volume = Component(roots, go, volumeClass);
                const bool enabledRead = volume && Read(getEnabled, volume, &enabled, sizeof(enabled));
                const bool matches = layerRead && volumeMaskRead && layer >= 0 && layer < 32 && (volumeMask & (uint32_t{1} << layer)) != 0;
                volumeJson = "{\"available\":true," + SceneObject(go, go) +
                    ",\"layer\":" + (layerRead ? std::to_string(layer) : "null") +
                    ",\"active_in_hierarchy\":" + (activeRead ? (active ? "true" : "false") : "null") +
                    ",\"component_enabled\":" + (enabledRead ? (enabled ? "true" : "false") : "null") +
                    ",\"included_in_camera_volume_mask\":" + (layerRead && volumeMaskRead ? (matches ? "true" : "false") : "null") + "}";
            }
        }
        const auto after = StudioRuntime::Current();
        if (!after.allowed || !context.SameOwner(after))
            return "{\"available\":false,\"reason\":\"owner-changed-during-sample\"}";
        return "{\"available\":true,\"actor_registry_count\":" + std::to_string(core.sceneActorRegistry.Entries().size()) +
            ",\"owner_scene\":" + std::to_string(context.owner.sceneHandle) + ",\"active_scene\":" + std::to_string(context.activeScene) +
            ",\"actor\":" + actorJson + ",\"camera_main\":" + cameraJson + ",\"owned_actor_volume\":" + volumeJson + "}";
    }
}
