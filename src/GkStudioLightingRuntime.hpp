#pragma once

// Private plugin adapter, included after StudioRuntime. Metadata resolution and
// creation are explicit Photography work. No native Live or Timeline is used.
namespace StudioLighting
{
    using Lighting = GakuMotion::studio::Lighting;
    using Type = GakuMotion::ExactMethodMetadata::Type;
    struct Color { float r{}, g{}, b{}, a{1}; };
    struct Vector2 { float x{}, y{}; };
    struct Vector3 { float x{}, y{}, z{}; };
    struct Quaternion { float x{}, y{}, z{}, w{1}; };
    inline StudioRuntime::Root lamps[3], actorGo, actorProfile, actorComponent;
    inline StudioRuntime::Context bound, observed;
    inline Lighting applied, requested;
    inline bool observedRequest{}, appliedValid{}, failed{}, pendingRevoke{};
    inline std::string failure, actorError, decalError;
    inline unsigned cleanupAttempts{};
    inline std::chrono::steady_clock::time_point nextCleanup{};
    inline void *lightClass{}, *behaviourClass{}, *typeClass{}, *singleClass{}, *colorClass{}, *vector2Class{}, *vector3Class{}, *quaternionClass{};
    inline void *lightTypeClass{}, *shadowClass{}, *volumeClass{}, *profileClass{}, *volumeComponentClass{}, *actorClass{}, *scriptableClass{};
    inline void* decalClass{};
    inline void* additionalLightClass{};
    inline const void *addComponent{}, *getComponent{}, *setEnabled{}, *setType{}, *setColor{}, *setIntensity{};
    inline const void *setRange{}, *setSpotAngle{}, *setInnerSpotAngle{}, *setMask{}, *setShadows{}, *setPosition{}, *setRotation{};
    inline const void* setAdditionalMask{};
    inline const void *createScriptable{}, *addVolumeComponent{}, *setProfile{}, *setGlobal{};
    inline const void *getDecalColor{}, *setDecalColor{}, *getDecalIntensity{}, *setDecalIntensity{};
    inline void (*staticGet)(void*, void*){};
    inline bool (*isValueType)(void*){};
    inline int spotType{}, noShadows{};
    inline int environmentLayer{-1}, actorVolumeLayer{-1};
    inline std::string actorLayerError;
    inline void *cameraClass{}, *meshRendererClass{}, *additionalCameraClass{}, *layerMaskClass{};
    inline const void *getLayer{}, *setLayer{}, *getCullingMask{}, *getVolumeMask{}, *getGlobal{}, *getBehaviourEnabled{};

    struct Decal
    {
        StudioRuntime::Root reference; // Borrowed component; never DestroyOwned.
        int scene{}, instance{};
        Color color;
        float intensity{};
    };
    inline std::vector<Decal> decals;

    inline bool Accessible()
    { return ProtectionOnUnityThread() && g_runtimeReady.load(std::memory_order_acquire); }
    inline bool SameToken(const StudioRuntime::Context& a, const StudioRuntime::Context& b)
    {
        return a.owner.presenterIdentity == b.owner.presenterIdentity && a.owner.sceneHandle == b.owner.sceneHandle &&
            a.owner.role == b.owner.role && a.owner.confirmed == b.owner.confirmed &&
            a.owner.sceneLoaded == b.owner.sceneLoaded && a.activeScene == b.activeScene &&
            a.activeName == b.activeName && a.allowed == b.allowed;
    }
    inline bool Eligible(const StudioRuntime::Context& context)
    {
        return context.allowed && context.owner.Valid() &&
            context.owner.role == GakuMotion::injection::OwnerRole::Photography && context.activeScene != 0 && Accessible();
    }
    inline bool HasObjects()
    { return lamps[0].Has() || lamps[1].Has() || lamps[2].Has() || actorGo.Has() || actorProfile.Has() || actorComponent.Has() || !decals.empty(); }
    inline const void* Method(void* klass, const char* name, bool stat, Type result, std::initializer_list<Type> args)
    {
        if (!klass || ((result.code == 0x12 || result.code == 0x11) && !result.klass)) return nullptr;
        for (const auto& type : args) if ((type.code == 0x12 || type.code == 0x11) && !type.klass) return nullptr;
        return StageLoader::metadata.Find(klass, name, stat, result, args);
    }
    inline bool Layout(void* klass, size_t size, uint32_t alignment)
    {
        uint32_t actual = 0;
        return klass && StageLoader::valueSize && isValueType && isValueType(klass) &&
            StageLoader::valueSize(klass, &actual) == static_cast<int>(size) && actual == alignment;
    }
    inline void* Field(void* object, const char* name, int code, void* klass = nullptr)
    {
        void* field = object ? g_faceRuntime.InstanceField(object, name, code) : nullptr;
        return field && StageLoader::metadata.Matches(g_faceRuntime.fieldType(field), {code, klass}) ? field : nullptr;
    }
    inline bool Put(void* object, const char* name, int code, const void* value, void* klass = nullptr)
    {
        void* field = Field(object, name, code, klass);
        if (!field || !g_faceRuntime.writeField) return false;
        g_faceRuntime.writeField(object, field, const_cast<void*>(value)); return true;
    }
    inline bool Enum(void* klass, const char* name, int& result)
    {
        if (!Layout(klass, sizeof(int), alignof(int)) || !staticGet) return false;
        void* field = g_faceRuntime.findField(klass, name);
        if (!field || (g_faceRuntime.fieldFlags(field) & 0x50) != 0x50 ||
            !StageLoader::metadata.Matches(g_faceRuntime.fieldType(field), {0x11, klass})) return false;
        staticGet(field, &result); return true;
    }
    inline bool ResolveLights()
    {
        if (setRotation) return true;
        if (!Accessible() || !StudioRuntime::Resolve() || !g_faceRuntime.readField || !g_faceRuntime.writeField ||
            !g_faceRuntime.fieldFlags || !g_faceRuntime.fieldType || !il2cpp_class_get_type || !il2cpp_type_get_object) return false;
        staticGet = reinterpret_cast<decltype(staticGet)>(GetProcAddress(g_gameAssembly, "il2cpp_field_static_get_value"));
        isValueType = reinterpret_cast<decltype(isValueType)>(GetProcAddress(g_gameAssembly, "il2cpp_class_is_valuetype"));
        const auto unity = [](const char* name) { return FindClass("UnityEngine.CoreModule.dll", "UnityEngine", name); };
        lightClass = unity("Light"); behaviourClass = unity("Behaviour"); colorClass = unity("Color");
        vector2Class = unity("Vector2"); vector3Class = unity("Vector3"); quaternionClass = unity("Quaternion");
        lightTypeClass = unity("LightType"); shadowClass = unity("LightShadows");
        typeClass = FindClass("mscorlib.dll", "System", "Type");
        singleClass = FindClass("mscorlib.dll", "System", "Single");
        if (!Layout(singleClass, sizeof(float), alignof(float)) || !Layout(colorClass, sizeof(Color), alignof(Color)) || !Layout(vector2Class, sizeof(Vector2), alignof(Vector2)) ||
            !Layout(vector3Class, sizeof(Vector3), alignof(Vector3)) || !Layout(quaternionClass, sizeof(Quaternion), alignof(Quaternion)) ||
            !Enum(lightTypeClass, "Spot", spotType) || !Enum(shadowClass, "None", noShadows)) return false;
        addComponent = Method(StudioRuntime::gameObjectClass, "AddComponent", false, {0x12, StageLoader::componentClass}, {{0x12, typeClass}});
        getComponent = Method(StudioRuntime::gameObjectClass, "GetComponent", false, {0x12, StageLoader::componentClass}, {{0x12, typeClass}});
        setEnabled = Method(behaviourClass, "set_enabled", false, {0x01}, {{0x02}});
        setType = Method(lightClass, "set_type", false, {0x01}, {{0x11, lightTypeClass}});
        setColor = Method(lightClass, "set_color", false, {0x01}, {{0x11, colorClass}});
        setIntensity = Method(lightClass, "set_intensity", false, {0x01}, {{0x0c}});
        setRange = Method(lightClass, "set_range", false, {0x01}, {{0x0c}});
        setSpotAngle = Method(lightClass, "set_spotAngle", false, {0x01}, {{0x0c}});
        setInnerSpotAngle = Method(lightClass, "set_innerSpotAngle", false, {0x01}, {{0x0c}});
        setMask = Method(lightClass, "set_renderingLayerMask", false, {0x01}, {{0x09}});
        if (!setMask) setMask = Method(lightClass, "set_renderingLayerMask", false, {0x01}, {{0x08}});
        setShadows = Method(lightClass, "set_shadows", false, {0x01}, {{0x11, shadowClass}});
        setPosition = Method(StudioRuntime::transformClass, "set_position", false, {0x01}, {{0x11, vector3Class}});
        const void* rotation = Method(StudioRuntime::transformClass, "set_rotation", false, {0x01}, {{0x11, quaternionClass}});
        if (!addComponent || !getComponent || !setEnabled || !setType || !setColor || !setIntensity ||
            !setRange || !setSpotAngle || !setInnerSpotAngle || !setMask || !setShadows || !setPosition || !rotation) return false;
        additionalLightClass = FindClass("Unity.RenderPipelines.Universal.Runtime.dll", "UnityEngine.Rendering.Universal", "UniversalAdditionalLightData");
        setAdditionalMask = Method(additionalLightClass, "set_lightLayerMask", false, {0x01}, {{0x09}});
        setRotation = rotation; return true;
    }
    inline void* Component(StudioRuntime::ScopedRoots& roots, void* go, void* klass, bool create)
    {
        if (!go || !klass) return nullptr;
        void* type = roots.Keep(il2cpp_type_get_object(il2cpp_class_get_type(klass)));
        void* args[]{type}; void* value = nullptr;
        if (!type || !StudioRuntime::Call(create ? addComponent : getComponent, go, args, &value)) return nullptr;
        value = roots.Keep(value);
        return value && StageLoader::assignable(klass, g_faceRuntime.objectClass(value)) ? value : nullptr;
    }
    inline bool Active(StudioRuntime::Root& root, bool active)
    {
        if (!root.Has()) return true;
        void* object = root.Get(); void* args[]{&active};
        return object && StudioRuntime::Call(StageLoader::setActive, object, args);
    }
    inline bool DestroyGo(StudioRuntime::Root& root)
    {
        if (!root.Has()) return true;
        // Deactivation is synchronous; Unity Destroy itself completes later.
        void* object = root.Get();
        if (object)
        {
            void* args[]{object}; void* boxed = nullptr;
            if (!StudioRuntime::Call(StudioRuntime::aliveMethod, nullptr, args, &boxed) || !boxed || !g_safeBones.unbox(boxed)) return false;
            if (*static_cast<bool*>(g_safeBones.unbox(boxed)) && !Active(root, false)) return false;
        }
        return StudioRuntime::DestroyOwned(root);
    }
    inline bool ActorOff()
    {
        bool ok = DestroyGo(actorGo);
        // These ScriptableObjects were created by us, never borrowed profiles.
        if (ok) ok = StudioRuntime::DestroyOwned(actorComponent) && StudioRuntime::DestroyOwned(actorProfile);
        return ok;
    }
    inline bool ReadScalar(const void* method, void* object, void* output, size_t size)
    {
        void* boxed = nullptr;
        if (!StudioRuntime::Call(method, object, nullptr, &boxed) || !boxed ||
            g_faceRuntime.objectClass(boxed) != (size == sizeof(Color) ? colorClass : singleClass)) return false;
        void* raw = g_safeBones.unbox(boxed);
        if (!raw) return false;
        std::memcpy(output, raw, size); return true;
    }
    inline bool ReadValue(const void* method, void* object, void* output, size_t size, void* expectedBox = nullptr)
    {
        void* boxed = nullptr;
        if (!method || !StudioRuntime::Call(method, object, nullptr, &boxed) || !boxed ||
            (expectedBox && g_faceRuntime.objectClass(boxed) != expectedBox)) return false;
        void* raw = g_safeBones.unbox(boxed);
        if (!raw) return false;
        std::memcpy(output, raw, size); return true;
    }
    inline bool AliveKnown(void* object, bool& alive)
    {
        void* args[]{object}; void* boxed = nullptr;
        if (!object || !StudioRuntime::Call(StudioRuntime::aliveMethod, nullptr, args, &boxed) || !boxed || !g_safeBones.unbox(boxed)) return false;
        alive = *static_cast<bool*>(g_safeBones.unbox(boxed)); return true;
    }
    inline bool ResolveLayers()
    {
        if (getLayer && setLayer && getCullingMask) return true;
        cameraClass = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Camera");
        meshRendererClass = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "MeshRenderer");
        additionalCameraClass = FindClass("Unity.RenderPipelines.Universal.Runtime.dll", "UnityEngine.Rendering.Universal", "UniversalAdditionalCameraData");
        layerMaskClass = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "LayerMask");
        volumeClass = FindClass("Unity.RenderPipelines.Core.Runtime.dll", "UnityEngine.Rendering", "Volume");
        getLayer = Method(StudioRuntime::gameObjectClass, "get_layer", false, {0x08}, {});
        setLayer = Method(StudioRuntime::gameObjectClass, "set_layer", false, {0x01}, {{0x08}});
        getCullingMask = Method(cameraClass, "get_cullingMask", false, {0x08}, {});
        getGlobal = Method(volumeClass, "get_isGlobal", false, {0x02}, {});
        getBehaviourEnabled = Method(behaviourClass, "get_enabled", false, {0x02}, {});
        if (Layout(layerMaskClass, sizeof(uint32_t), alignof(uint32_t)))
            getVolumeMask = Method(additionalCameraClass, "get_volumeLayerMask", false, {0x11, layerMaskClass}, {});
        return meshRendererClass && getLayer && setLayer && getCullingMask && StageLoader::cameraMain &&
            StageLoader::findAll && StageLoader::arrayLength;
    }
    inline bool SceneLayers(void* klass, int scene, uint32_t mask, bool volumes, uint32_t& layers)
    {
        layers = 0;
        if (!klass) return false;
        StudioRuntime::ScopedRoots roots;
        void* type = roots.Keep(il2cpp_type_get_object(il2cpp_class_get_type(klass)));
        bool inactive = false; void* args[]{type, &inactive}; void* array = nullptr;
        if (!type || !StudioRuntime::Call(StageLoader::findAll, nullptr, args, &array) || !(array = roots.Keep(array))) return false;
        const auto count = StageLoader::arrayLength(array);
        if (count > (volumes ? 512u : 4096u)) return false;
        void** values = reinterpret_cast<void**>(static_cast<char*>(array) + 32);
        for (uintptr_t i = 0; i < count; ++i)
        {
            void* object = values[i];
            if (!object || !StageLoader::assignable(klass, g_faceRuntime.objectClass(object))) continue;
            bool alive = false;
            if (!AliveKnown(object, alive)) return false;
            if (!alive) continue;
            void* go = roots.Keep(StageLoader::Invoke(StageLoader::componentGo, object));
            if (!go) return false;
            if (StageLoader::SceneOfFreshGo(go) != scene || (volumes && go == actorGo.Get())) continue;
            if (volumes)
            {
                uint8_t global = 0, enabled = 0;
                if (!ReadValue(getGlobal, object, &global, sizeof(global)) ||
                    !ReadValue(getBehaviourEnabled, object, &enabled, sizeof(enabled))) return false;
                if (!global || !enabled) continue;
            }
            int layer = -1;
            if (!ReadValue(getLayer, go, &layer, sizeof(layer)) || layer < 0 || layer >= 32) return false;
            const uint32_t bit = uint32_t{1} << layer;
            if ((mask & bit) != 0) layers |= bit;
        }
        return true;
    }
    inline int UniqueLayer(uint32_t mask)
    {
        if (!mask || (mask & (mask - 1)) != 0) return -1;
        for (int layer = 0; layer < 32; ++layer) if (mask == (uint32_t{1} << layer)) return layer;
        return -1;
    }
    inline bool FreshLayerContext(const StudioRuntime::Context& context)
    {
        const auto fresh = StudioRuntime::Current();
        if (fresh.allowed && Eligible(fresh) && context.SameOwner(fresh)) return true;
        failure = "摄影场景许可已变化，未应用自定义灯光。"; return false;
    }
    inline bool SelectLayers(const Lighting& settings, const StudioRuntime::Context& context)
    {
        environmentLayer = actorVolumeLayer = -1; actorLayerError.clear();
        if (!FreshLayerContext(context)) return false;
        if (!ResolveLayers()) { failure = "摄影相机与对象层接口不可用"; return false; }
        StudioRuntime::ScopedRoots roots;
        void* camera = roots.Keep(StageLoader::Invoke(StageLoader::cameraMain));
        bool alive = false; uint32_t culling = 0, layers = 0;
        if (!AliveKnown(camera, alive) || !alive || !ReadValue(getCullingMask, camera, &culling, sizeof(culling)) ||
            !SceneLayers(meshRendererClass, context.activeScene, culling, false, layers))
        { failure = "无法读取当前摄影背景的可见对象层"; return false; }
        environmentLayer = UniqueLayer(layers);
        if (environmentLayer < 0) { failure = "当前摄影背景没有唯一可见的原生对象层，未创建自有射灯"; return false; }
        if (!settings.actorStyle) return FreshLayerContext(context);
        void* go = roots.Keep(StageLoader::Invoke(StageLoader::componentGo, camera));
        void* additional = Component(roots, go, additionalCameraClass, false);
        uint32_t volumeMask = 0;
        if (!additional || !getVolumeMask || !getGlobal || !getBehaviourEnabled ||
            !ReadValue(getVolumeMask, additional, &volumeMask, sizeof(volumeMask), layerMaskClass) ||
            !SceneLayers(volumeClass, context.activeScene, volumeMask, true, layers))
            actorLayerError = "相机Volume层或原生Volume层不可读";
        else
        {
            if (!layers && context.owner.sceneHandle != context.activeScene &&
                !SceneLayers(volumeClass, context.owner.sceneHandle, volumeMask, true, layers))
                actorLayerError = "摄影所属场景Volume层不可读";
            actorVolumeLayer = UniqueLayer(layers);
            if (actorVolumeLayer < 0 && actorLayerError.empty()) actorLayerError = "未发现唯一且被相机接受的原生全局Volume层";
        }
        return FreshLayerContext(context);
    }
    inline bool SetOwnedLayer(StudioRuntime::Root& root, int layer)
    {
        if (!root.Has() || layer < 0 || layer >= 32) return false;
        void* go = root.Get(); void* args[]{&layer}; int after = -1;
        return go && StudioRuntime::Call(setLayer, go, args) && ReadValue(getLayer, go, &after, sizeof(after)) && after == layer;
    }
    inline bool Near(float a, float b)
    { return std::isfinite(a) && std::isfinite(b) && std::fabs(a - b) <= 1e-5f * std::max(1.0f, std::fabs(b)); }
    inline bool Near(const Color& a, const Color& b)
    { return Near(a.r, b.r) && Near(a.g, b.g) && Near(a.b, b.b) && Near(a.a, b.a); }
    inline bool RestoreDecals()
    {
        bool complete = true;
        for (auto& decal : decals)
        {
            if (!decal.reference.Has()) continue;
            int scene = decal.scene; void* args[]{&scene}; bool loaded = false;
            void* boxed = nullptr;
            if (!StudioRuntime::Call(StageLoader::sceneLoaded, nullptr, args, &boxed) || !boxed || !g_safeBones.unbox(boxed))
            { complete = false; continue; }
            loaded = *static_cast<bool*>(g_safeBones.unbox(boxed));
            if (!loaded) { decal.reference.Reset(); continue; }
            void* object = decal.reference.Get(); void* aliveArgs[]{object}; boxed = nullptr;
            if (!object || !StudioRuntime::Call(StudioRuntime::aliveMethod, nullptr, aliveArgs, &boxed) || !boxed || !g_safeBones.unbox(boxed))
            { complete = false; continue; }
            if (!*static_cast<bool*>(g_safeBones.unbox(boxed))) { decal.reference.Reset(); continue; }
            void* go = nullptr;
            if (!StudioRuntime::Call(StageLoader::componentGo, object, nullptr, &go) || !go)
            { complete = false; continue; }
            if (StageLoader::SceneOfFreshGo(go) != decal.scene || StageLoader::ReadInt(StageLoader::instanceId, object) != decal.instance)
            { complete = false; continue; }
            void* colorArgs[]{&decal.color}; void* intensityArgs[]{&decal.intensity};
            if (!StudioRuntime::Call(setDecalColor, object, colorArgs) || !StudioRuntime::Call(setDecalIntensity, object, intensityArgs))
            { complete = false; continue; }
            Color restoredColor; float restoredIntensity = 0;
            if (!ReadScalar(getDecalColor, object, &restoredColor, sizeof(Color)) ||
                !ReadScalar(getDecalIntensity, object, &restoredIntensity, sizeof(float)) ||
                !Near(restoredColor, decal.color) || !Near(restoredIntensity, decal.intensity))
            { complete = false; continue; }
            decal.reference.Reset();
        }
        if (complete) decals.clear();
        return complete;
    }
    inline bool Revoke(std::string& notice)
    {
        if (!HasObjects()) { appliedValid = false; pendingRevoke = false; return true; }
        pendingRevoke = true;
        // Host deactivation clears runtimeReady before revoking leases, while
        // the already-resolved Unity API is still alive. Only new work needs
        // runtimeReady; cleanup remains main-thread-only and checks every call.
        if (!ProtectionOnUnityThread()) { notice = "摄影灯光等待主线程恢复原值并清理自有对象。"; return false; }
        const auto now = std::chrono::steady_clock::now();
        if (cleanupAttempts >= 3 || now < nextCleanup)
        { notice = "摄影灯光清理尚未完成；恢复记录保留，修改配置或重新进入摄影后重试。"; return false; }
        ++cleanupAttempts; nextCleanup = now + std::chrono::seconds(1);
        bool ok = RestoreDecals();
        if (!ActorOff()) ok = false;
        for (auto& lamp : lamps) if (!DestroyGo(lamp)) ok = false;
        appliedValid = false;
        if (ok) { pendingRevoke = false; cleanupAttempts = 0; nextCleanup = {}; notice = "自有灯光已关闭，背景灯原值已恢复。"; }
        else notice = "摄影灯光清理失败；自有对象和背景原值记录保留，等待有限重试。";
        return ok;
    }
    inline bool Shutdown(std::string& notice) { return Revoke(notice); }

    inline bool Lamp(size_t index, const StudioRuntime::Context& context, const std::array<float, 3>& rgb,
        float intensity, float yaw, float pitch)
    {
        StudioRuntime::ScopedRoots roots;
        bool created = !lamps[index].Has();
        const char* names[]{"Lighting&PostEffects (GakuMotion Studio Key)", "Lighting&PostEffects (GakuMotion Studio Fill)", "Lighting&PostEffects (GakuMotion Studio Rim)"};
        if (created && !StudioRuntime::CreateOwnedRoot(names[index], context, lamps[index])) return false;
        void* go = lamps[index].Get();
        if (!SetOwnedLayer(lamps[index], environmentLayer)) return false;
        void* light = Component(roots, go, lightClass, created);
        void* transform = roots.Keep(StageLoader::Invoke(StageLoader::goTransform, go));
        if (!light || !transform) return false;
        if (additionalLightClass && setAdditionalMask)
        {
            void* additional = Component(roots, go, additionalLightClass, false);
            if (!additional) additional = Component(roots, go, additionalLightClass, true);
            uint32_t layers = UINT32_MAX; void* layerArgs[]{&layers};
            if (!additional || !StudioRuntime::Call(setAdditionalMask, additional, layerArgs)) return false;
        }
        constexpr float radians = 0.01745329251994329577f;
        const float y = yaw * radians, p = pitch * radians;
        const Vector3 forward{std::sin(y) * std::cos(p), -std::sin(p), std::cos(y) * std::cos(p)};
        const Vector3 position{-forward.x * 5, 1.0f - forward.y * 5, -forward.z * 5};
        const Quaternion rotation{std::sin(p * .5f) * std::cos(y * .5f), std::cos(p * .5f) * std::sin(y * .5f),
            -std::sin(p * .5f) * std::sin(y * .5f), std::cos(p * .5f) * std::cos(y * .5f)};
        Color color{rgb[0], rgb[1], rgb[2], 1};
        float range = 14, outer = 90, inner = 65; uint32_t allLayers = UINT32_MAX; bool enabled = true;
        void* typeArgs[]{&spotType}; void* colorArgs[]{&color}; void* intensityArgs[]{&intensity};
        void* rangeArgs[]{&range}; void* outerArgs[]{&outer}; void* innerArgs[]{&inner};
        void* maskArgs[]{&allLayers}; void* shadowArgs[]{&noShadows}; void* enabledArgs[]{&enabled};
        void* positionArgs[]{const_cast<Vector3*>(&position)}; void* rotationArgs[]{const_cast<Quaternion*>(&rotation)};
        return StudioRuntime::Call(setType, light, typeArgs) && StudioRuntime::Call(setColor, light, colorArgs) &&
            StudioRuntime::Call(setIntensity, light, intensityArgs) && StudioRuntime::Call(setRange, light, rangeArgs) &&
            StudioRuntime::Call(setSpotAngle, light, outerArgs) && StudioRuntime::Call(setInnerSpotAngle, light, innerArgs) &&
            StudioRuntime::Call(setMask, light, maskArgs) && StudioRuntime::Call(setShadows, light, shadowArgs) &&
            StudioRuntime::Call(setEnabled, light, enabledArgs) && StudioRuntime::Call(setPosition, transform, positionArgs) &&
            StudioRuntime::Call(setRotation, transform, rotationArgs) && Active(lamps[index], true);
    }
    inline bool Parameter(void* actor, const char* name, int code, void* valueClass, const void* value)
    {
        StudioRuntime::ScopedRoots roots;
        void* field = nullptr;
        for (int reference : {0x12, 0x15})
            if ((field = g_faceRuntime.InstanceField(actor, name, reference))) break;
        if (!field || StageLoader::metadata.byRef(g_faceRuntime.fieldType(field))) return false;
        void* declared = StageLoader::metadata.classFromType(g_faceRuntime.fieldType(field));
        if (!declared || isValueType(declared)) return false;
        void* parameter = nullptr; g_faceRuntime.readField(actor, field, &parameter);
        parameter = roots.Keep(parameter);
        if (!parameter || !StageLoader::assignable(declared, g_faceRuntime.objectClass(parameter))) return false;
        uint8_t overridden = 1;
        return Put(parameter, "m_Value", code, value, valueClass) && Put(parameter, "m_OverrideState", 0x02, &overridden);
    }
    inline bool Actor(const Lighting& settings, const StudioRuntime::Context& context)
    {
        if (!settings.actorStyle) return ActorOff();
        if (actorVolumeLayer < 0) return false;
        if (!actorClass)
        {
            volumeClass = FindClass("Unity.RenderPipelines.Core.Runtime.dll", "UnityEngine.Rendering", "Volume");
            profileClass = FindClass("Unity.RenderPipelines.Core.Runtime.dll", "UnityEngine.Rendering", "VolumeProfile");
            volumeComponentClass = FindClass("Unity.RenderPipelines.Core.Runtime.dll", "UnityEngine.Rendering", "VolumeComponent");
            scriptableClass = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "ScriptableObject");
            void* candidate = FindClass("Unity.RenderPipelines.Universal.Runtime.dll", "VL.Rendering", "VLActorParameter");
            createScriptable = Method(scriptableClass, "CreateInstance", true, {0x12, scriptableClass}, {{0x12, typeClass}});
            addVolumeComponent = Method(profileClass, "Add", false, {0x12, volumeComponentClass}, {{0x12, typeClass}, {0x02}});
            setProfile = Method(volumeClass, "set_profile", false, {0x01}, {{0x12, profileClass}});
            setGlobal = Method(volumeClass, "set_isGlobal", false, {0x01}, {{0x02}});
            if (!candidate || !createScriptable || !addVolumeComponent || !setProfile || !setGlobal) return false;
            actorClass = candidate;
        }
        StudioRuntime::ScopedRoots roots;
        if (!actorGo.Has())
        {
            if (!StudioRuntime::CreateOwnedRoot("Lighting&PostEffects (GakuMotion Studio Actor)", context, actorGo)) return false;
            if (!SetOwnedLayer(actorGo, actorVolumeLayer)) return false;
            void* type = roots.Keep(il2cpp_type_get_object(il2cpp_class_get_type(profileClass)));
            void* args[]{type}; void* profile = nullptr;
            if (!type || !StudioRuntime::Call(createScriptable, nullptr, args, &profile) ||
                !profile || g_faceRuntime.objectClass(profile) != profileClass || !actorProfile.Keep(profile)) return false;
            type = roots.Keep(il2cpp_type_get_object(il2cpp_class_get_type(actorClass)));
            bool overrides = false; void* addArgs[]{type, &overrides}; void* component = nullptr;
            if (!type || !StudioRuntime::Call(addVolumeComponent, profile, addArgs, &component) ||
                !component || g_faceRuntime.objectClass(component) != actorClass || !actorComponent.Keep(component)) return false;
        }
        void* volume = Component(roots, actorGo.Get(), volumeClass, !actorProfile.Has() ? true : false);
        // Add only when the freshly owned GO has no Volume; no borrowed profile is edited.
        if (!volume) volume = Component(roots, actorGo.Get(), volumeClass, true);
        if (!volume) return false;
        if (!SetOwnedLayer(actorGo, actorVolumeLayer)) return false;
        bool global = true; float weight = 1, priority = 10000; uint8_t active = 1;
        void* globalArgs[]{&global}; void* profileArgs[]{actorProfile.Get()};
        if (!StudioRuntime::Call(setGlobal, volume, globalArgs) || !Put(volume, "weight", 0x0c, &weight) ||
            !Put(volume, "priority", 0x0c, &priority) || !StudioRuntime::Call(setProfile, volume, profileArgs) ||
            !Put(actorComponent.Get(), "active", 0x02, &active)) return false;
        const auto scaled = [&](const auto& rgb, float intensity) {
            return Color{rgb[0] * intensity * settings.actorIntensity, rgb[1] * intensity * settings.actorIntensity,
                rgb[2] * intensity * settings.actorIntensity, 1};
        };
        const Color key = scaled(settings.keyColor, settings.keyIntensity);
        const Color fill = scaled(settings.fillColor, settings.fillIntensity);
        const Color rim = scaled(settings.rimColor, settings.rimIntensity);
        const Vector2 keyAngle{settings.keyYaw, settings.keyPitch}, rimAngle{settings.rimYaw, settings.rimPitch};
        return Parameter(actorComponent.Get(), "lightColor", 0x11, colorClass, &key) &&
            Parameter(actorComponent.Get(), "shadeColorAdditive", 0x11, colorClass, &fill) &&
            Parameter(actorComponent.Get(), "rimColor", 0x11, colorClass, &rim) &&
            Parameter(actorComponent.Get(), "mainLightAngle", 0x11, vector2Class, &keyAngle) &&
            Parameter(actorComponent.Get(), "rimAngle", 0x11, vector2Class, &rimAngle) &&
            Parameter(actorComponent.Get(), "rimPower", 0x0c, nullptr, &settings.rimPower) && Active(actorGo, true);
    }
    inline bool Decals(const Lighting& settings, const StudioRuntime::Context& context)
    {
        if (!settings.decalEnabled) return RestoreDecals();
        if (!decalClass)
        {
            void* candidate = FindClass("campus-submodule.Runtime.dll", "Campus.DecalLight", "CampusDecalLight");
            getDecalColor = Method(candidate, "get_Color", false, {0x11, colorClass}, {});
            setDecalColor = Method(candidate, "set_Color", false, {0x01}, {{0x11, colorClass}});
            getDecalIntensity = Method(candidate, "get_Intensity", false, {0x0c}, {});
            setDecalIntensity = Method(candidate, "set_Intensity", false, {0x01}, {{0x0c}});
            if (!candidate || !getDecalColor || !setDecalColor || !getDecalIntensity || !setDecalIntensity) return false;
            decalClass = candidate;
        }
        if (decals.empty())
        {
            StudioRuntime::ScopedRoots roots;
            void* type = roots.Keep(il2cpp_type_get_object(il2cpp_class_get_type(decalClass)));
            bool inactive = false; void* args[]{type, &inactive}; void* array = nullptr;
            if (!type || !StudioRuntime::Call(StageLoader::findAll, nullptr, args, &array) || !(array = roots.Keep(array))) return false;
            const auto count = StageLoader::arrayLength(array);
            if (count > 2048) return false;
            void** values = reinterpret_cast<void**>(static_cast<char*>(array) + 32);
            for (uintptr_t i = 0; i < count; ++i)
            {
                void* object = values[i];
                if (!object || g_faceRuntime.objectClass(object) != decalClass) continue;
                void* go = roots.Keep(StageLoader::Invoke(StageLoader::componentGo, object));
                if (!go || StageLoader::SceneOfFreshGo(go) != context.activeScene) continue;
                Decal saved; saved.scene = context.activeScene; saved.instance = StageLoader::ReadInt(StageLoader::instanceId, object);
                if (!saved.instance || !ReadScalar(getDecalColor, object, &saved.color, sizeof(Color)) ||
                    !ReadScalar(getDecalIntensity, object, &saved.intensity, sizeof(float)) || !saved.reference.Keep(object)) return false;
                decals.push_back(std::move(saved));
            }
        }
        for (auto& decal : decals)
        {
            if (!decal.reference.Has()) continue;
            Color color{decal.color.r * settings.decalTint[0], decal.color.g * settings.decalTint[1],
                decal.color.b * settings.decalTint[2], decal.color.a};
            float intensity = decal.intensity * settings.decalIntensityScale;
            void* colorArgs[]{&color}; void* intensityArgs[]{&intensity};
            if (!StudioRuntime::Call(setDecalColor, decal.reference.Get(), colorArgs) ||
                !StudioRuntime::Call(setDecalIntensity, decal.reference.Get(), intensityArgs)) return false;
            Color actualColor; float actualIntensity = 0;
            if (!ReadScalar(getDecalColor, decal.reference.Get(), &actualColor, sizeof(Color)) ||
                !ReadScalar(getDecalIntensity, decal.reference.Get(), &actualIntensity, sizeof(float)) ||
                !Near(actualColor, color) || !Near(actualIntensity, intensity)) return false;
        }
        return true;
    }
    inline void Tick(const Lighting& settings, const StudioRuntime::Context& context, std::string& notice)
    {
        const bool changed = !observedRequest || settings != requested || !SameToken(context, observed);
        if (changed)
        { requested = settings; observed = context; observedRequest = true; failed = false; cleanupAttempts = 0; nextCleanup = {}; }
        if (!settings.enabled || !Eligible(context))
        { if (HasObjects()) Revoke(notice); else notice = settings.enabled ? "等待进入已验证的偶像摄影场景。" : "自定义摄影灯光未启用。"; return; }
        if (HasObjects() && !bound.SameOwner(context))
            if (!Revoke(notice)) return;
        if (pendingRevoke && !Revoke(notice)) return;
        if (failed) { notice = failure; return; }
        if (appliedValid && !changed) return;
        GakuMotion::studio::Settings checked; checked.lighting = settings;
        std::string validationError;
        if (!checked.Validate(validationError))
        { Revoke(notice); failed = true; failure = notice = "摄影灯光参数不合法：" + validationError; return; }
        if (!ResolveLights())
        { failed = true; failure = notice = "摄影灯光接口不匹配；未创建灯光，修改配置或重新进入摄影后可重试。"; return; }
        if (!SelectLayers(settings, context))
        { std::string reason = failure; Revoke(notice); failed = true; failure = notice = reason; return; }
        bound = context;
        if (!Lamp(0, context, settings.keyColor, settings.keyIntensity, settings.keyYaw, settings.keyPitch) ||
            !Lamp(1, context, settings.fillColor, settings.fillIntensity, settings.fillYaw, settings.fillPitch) ||
            !Lamp(2, context, settings.rimColor, settings.rimIntensity, settings.rimYaw, settings.rimPitch))
        {
            Revoke(notice); failed = true; failure = notice = "自有射灯创建或设置失败；恢复记录保留，修改配置或重新进入摄影后可重试。"; return;
        }
        actorError.clear(); decalError.clear();
        if (!Actor(settings, context))
        {
            actorError = actorLayerError.empty() ? "人物风格光接口不可用" : "人物风格光未启用：" + actorLayerError;
            if (!ActorOff())
            { Revoke(notice); failed = true; failure = notice = "人物风格光配置失败，清理和恢复记录保留，等待有限重试。"; return; }
        }
        if (!Decals(settings, context))
        {
            decalError = "背景灯强调未完成";
            if (!RestoreDecals())
            { Revoke(notice); failed = true; failure = notice = "背景灯原值恢复失败，快照保留，等待有限重试。"; return; }
        }
        applied = settings; appliedValid = true;
        notice = "三盏自有射灯已配置；";
        notice += settings.actorStyle ? (actorError.empty() ? "人物风格光已配置" : actorError) : "人物风格光未启用";
        if (!decalError.empty()) notice += "；" + decalError;
        else if (settings.decalEnabled) notice += "；背景投射灯已配置 " + std::to_string(decals.size()) + " 项";
        notice += "。画面效果待实机确认。";
    }
}
