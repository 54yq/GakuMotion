// Exercise the actual studio-lighting adapter against checked fake IL2CPP and
// Unity APIs. No game process, renderer, or installed configuration is used.
#include "GkStageStudioSettings.hpp"
#include "GkExactMethod.hpp"
#include "GkFaceRuntime.hpp"
#include "GkInjectionPolicy.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <memory>
#include <unordered_map>

namespace LightingHarness
{
    struct Class;
    struct Field;
    struct Kind { int code{}; Class* klass{}; bool byRef{}; };
    struct Field { std::string name; Kind type; uint32_t flags{}; int literal{}; };
    struct Method
    {
        Class* klass{}; std::string name; Kind result; std::vector<Kind> args; uint32_t flags{};
    };
    struct Class
    {
        std::string image, ns, name; Class* parent{}; bool value{}; int size{};
        std::unordered_map<std::string, Field> fields;
        std::vector<std::unique_ptr<Method>> methods;
    };
    struct Object
    {
        std::array<unsigned char, 32> header{};
        std::array<void*, 2048> array{};
        Class* klass{}; Class* represented{}; int scene{}, id{};
        bool alive{true}, active{true}, owned{}, enabled{true}, global{true}; size_t count{};
        Object* go{}; Object* transform{};
        std::unordered_map<Class*, Object*> components;
        std::unordered_map<std::string, std::array<unsigned char, 32>> data;
        std::array<unsigned char, 64> raw{};
        std::array<float, 4> color{1, 1, 1, 1}, position{}, rotation{};
        float intensity{1}, range{}, outer{}, inner{}; int type{}, mask{}, shadows{}, layer{};
        uint32_t cullingMask{}, volumeMask{};
    };
    static_assert(offsetof(Object, array) == 32);
    inline std::vector<std::unique_ptr<Class>> classes;
    inline std::vector<std::unique_ptr<Object>> objects;
    inline std::unordered_map<void*, Object*> handles;
    inline uintptr_t nextHandle{1};
    inline int nextId{1}, invokes{}, reflections{}, writes{}, created{}, destroyed{}, fieldWrites{};
    inline bool mainThread{true}, actorMissing{}, failRestore{}, failDestroy{}, failAlive{}, ignoreColorSetter{}, ignoreLayerSetter{};
    inline Object* currentCamera{};
    inline int failCreateAt{}, createAttempts{};
    inline void* g_gameAssembly = reinterpret_cast<void*>(1);
    inline std::atomic<bool> g_runtimeReady{true};
    inline GakuMotion::FaceRuntimeAccess g_faceRuntime;
    struct SafeBones { void* (*unbox)(void*){}; };
    void* Unbox(void* object) { return object ? static_cast<Object*>(object)->raw.data() : nullptr; }
    inline SafeBones g_safeBones{Unbox};
    inline std::unordered_map<int, bool> loaded{{10, true}, {20, true}, {30, true}};
    bool ProtectionOnUnityThread() { return mainThread; }
    Class* K(const char* name)
    { for (const auto& klass : classes) if (klass->name == name) return klass.get(); throw std::runtime_error(name); }
    Class* AddClass(const char* image, const char* ns, const char* name, Class* parent = nullptr, int size = 0)
    {
        auto value = std::make_unique<Class>(); value->image = image; value->ns = ns; value->name = name;
        value->parent = parent; value->value = size > 0; value->size = size;
        auto* result = value.get(); classes.push_back(std::move(value)); return result;
    }
    Object* New(Class* klass, int scene = 0, bool owned = false)
    {
        auto value = std::make_unique<Object>(); value->klass = klass; value->scene = scene;
        value->owned = owned; value->id = nextId++;
        auto* result = value.get(); objects.push_back(std::move(value)); return result;
    }
    Object* Box(const void* raw, size_t size, Class* klass = nullptr)
    { auto* object = New(klass ? klass : K("Object")); std::memcpy(object->raw.data(), raw, size); return object; }
    Method* AddMethod(Class* owner, const char* name, Kind result, std::initializer_list<Kind> args = {}, bool stat = false)
    {
        auto method = std::make_unique<Method>(); method->klass = owner; method->name = name;
        method->result = result; method->args = args; method->flags = stat ? 0x10 : 0;
        auto* out = method.get(); owner->methods.push_back(std::move(method)); return out;
    }
    void FieldDef(Class* owner, const char* name, Kind type)
    { owner->fields[name] = {name, type}; }
    template<class T> void Data(Object* object, const char* name, const T& value)
    { std::memcpy(object->data[name].data(), &value, sizeof(value)); }
    template<class T> T Data(Object* object, const char* name)
    { T value{}; std::memcpy(&value, object->data.at(name).data(), sizeof(value)); return value; }
    void* FindClass(const char* image, const char* ns, const char* name)
    {
        ++reflections;
        if (actorMissing && std::strcmp(name, "VLActorParameter") == 0) return nullptr;
        for (const auto& klass : classes)
            if (klass->image == image && klass->ns == ns && klass->name == name) return klass.get();
        return nullptr;
    }
    const void* Methods(void* klass, void** iterator)
    {
        auto& methods = static_cast<Class*>(klass)->methods;
        const auto index = reinterpret_cast<uintptr_t>(*iterator);
        if (index >= methods.size()) return nullptr;
        *iterator = reinterpret_cast<void*>(index + 1); return methods[index].get();
    }
    const char* MethodName(const void* value) { return static_cast<const Method*>(value)->name.c_str(); }
    uint32_t Count(const void* value) { return static_cast<uint32_t>(static_cast<const Method*>(value)->args.size()); }
    uint32_t Flags(const void* value, uint32_t* implementation) { *implementation = 0; return static_cast<const Method*>(value)->flags; }
    bool Never(const void*) { return false; }
    void* Owner(const void* value) { return static_cast<const Method*>(value)->klass; }
    void* Argument(const void* value, uint32_t index) { return const_cast<Kind*>(&static_cast<const Method*>(value)->args.at(index)); }
    void* Result(const void* value) { return const_cast<Kind*>(&static_cast<const Method*>(value)->result); }
    int Code(void* value) { return static_cast<Kind*>(value)->code; }
    bool ByRef(void* value) { return static_cast<Kind*>(value)->byRef; }
    void* TypeClass(void* value) { return static_cast<Kind*>(value)->klass; }
    void* ObjectClass(void* value) { return value ? static_cast<Object*>(value)->klass : nullptr; }
    void* Parent(void* value) { return static_cast<Class*>(value)->parent; }
    void* FindField(void* klass, const char* name)
    {
        auto& fields = static_cast<Class*>(klass)->fields; auto it = fields.find(name);
        return it == fields.end() ? nullptr : &it->second;
    }
    void* FieldType(void* field) { return &static_cast<Field*>(field)->type; }
    uint32_t FieldFlags(void* field) { return static_cast<Field*>(field)->flags; }
    size_t FieldSize(Field* field)
    {
        if (field->type.code == 0x02) return 1;
        if (field->type.code == 0x12 || field->type.code == 0x15) return sizeof(void*);
        return field->type.klass ? static_cast<size_t>(field->type.klass->size) : sizeof(float);
    }
    void ReadField(void* object, void* field, void* output)
    { auto* item = static_cast<Field*>(field); std::memcpy(output, static_cast<Object*>(object)->data.at(item->name).data(), FieldSize(item)); }
    void WriteField(void* object, void* field, void* input)
    { ++fieldWrites; auto* item = static_cast<Field*>(field); std::memcpy(static_cast<Object*>(object)->data[item->name].data(), input, FieldSize(item)); }
    bool ValueType(void* klass) { return static_cast<Class*>(klass)->value; }
    int ValueSize(void* klass, uint32_t* alignment) { *alignment = 4; return static_cast<Class*>(klass)->size; }
    void StaticGet(void* field, void* output) { *static_cast<int*>(output) = static_cast<Field*>(field)->literal; }
    bool Assignable(void* expected, void* actual)
    { for (auto* klass = static_cast<Class*>(actual); klass; klass = klass->parent) if (klass == expected) return true; return false; }
    void* GetProcAddress(void*, const char* name)
    {
        ++reflections;
        if (!std::strcmp(name, "il2cpp_class_is_valuetype")) return reinterpret_cast<void*>(ValueType);
        if (!std::strcmp(name, "il2cpp_field_static_get_value")) return reinterpret_cast<void*>(StaticGet);
        return nullptr;
    }
    void* NewHandle(void* value, bool)
    { if (!value) return nullptr; void* handle = reinterpret_cast<void*>(nextHandle++); handles[handle] = static_cast<Object*>(value); return handle; }
    void FreeHandle(void* value) { if (handles.erase(value) != 1) throw std::runtime_error("double root free"); }
    void* HandleTarget(void* value) { return handles.at(value); }
    void* ClassType(void* value) { ++reflections; return value; }
    void* TypeObject(void* value) { ++reflections; auto* object = New(K("Type")); object->represented = static_cast<Class*>(value); return object; }
    inline void* (*il2cpp_class_get_type)(void*) = ClassType;
    inline void* (*il2cpp_type_get_object)(void*) = TypeObject;
    namespace SceneOwnerRuntime { inline void* (*handleTarget)(void*) = HandleTarget; }
    namespace StageLoader
    {
        using Handle = void*;
        inline Handle (*newHandle)(void*, bool) = NewHandle;
        inline void (*freeHandle)(Handle) = FreeHandle;
        inline bool (*assignable)(void*, void*) = Assignable;
        inline int (*valueSize)(void*, uint32_t*) = ValueSize;
        inline GakuMotion::ExactMethodMetadata metadata;
        inline void* componentClass{};
        inline const void *instanceId{}, *goTransform{}, *componentGo{}, *setActive{}, *sceneLoaded{}, *findAll{}, *cameraMain{};
        uintptr_t Length(void* value) { return static_cast<Object*>(value)->count; }
        inline uintptr_t (*arrayLength)(void*) = Length;
        void* Invoke(const void* method, void* object = nullptr, void** args = nullptr);
        int ReadInt(const void* method, void* object);
        int SceneOfFreshGo(void* go) { return static_cast<Object*>(go)->scene; }
    }
    namespace StudioRuntime
    {
        inline std::string error;
        struct Context
        {
            GakuMotion::injection::OwnerSnapshot owner; int activeScene{}; std::string activeName; bool allowed{};
            bool SameOwner(const Context& other) const { return owner.SameOwner(other.owner) && activeScene == other.activeScene; }
        };
        inline Context current{{true, 700, 10, true, GakuMotion::injection::OwnerRole::Photography, false}, 20, "Photography", true};
        inline int currentReads{}, revokeOnCurrentRead{};
        Context Current()
        { auto result = current; if (revokeOnCurrentRead && ++currentReads >= revokeOnCurrentRead) result.allowed = false; return result; }
        struct Root
        {
            void* handle{}; int instanceId{};
            Root() = default; Root(const Root&) = delete; Root& operator=(const Root&) = delete;
            Root(Root&& other) noexcept : handle(other.handle), instanceId(other.instanceId) { other.handle = nullptr; }
            Root& operator=(Root&& other) noexcept { if (this != &other) { Reset(); handle = other.handle; instanceId = other.instanceId; other.handle = nullptr; } return *this; }
            ~Root() { Reset(); }
            bool Has() const { return handle != nullptr; }
            void* Get() const { return handle ? HandleTarget(handle) : nullptr; }
            void* Keep(void* value) { if (!handle) handle = NewHandle(value, false); return Get(); }
            void Reset() { if (handle) FreeHandle(handle); handle = nullptr; instanceId = 0; }
        };
        struct ScopedRoots
        {
            std::vector<void*> roots;
            ~ScopedRoots() { for (auto* root : roots) FreeHandle(root); }
            void* Keep(void* value) { if (!value) return nullptr; auto* h = NewHandle(value, false); roots.push_back(h); return HandleTarget(h); }
        };
        inline void *gameObjectClass{}, *transformClass{};
        inline const void* aliveMethod{};
        bool Resolve() { ++reflections; return true; }
        bool Call(const void* metadata, void* instance, void** args = nullptr, void** result = nullptr)
        {
            ++invokes;
            if (!metadata || !mainThread) return false;
            const auto& method = *static_cast<const Method*>(metadata); auto* self = static_cast<Object*>(instance);
            const auto& name = method.name;
            void* value = nullptr;
            if (name == "Alive")
            { if (failAlive) return false; bool alive = args[0] && static_cast<Object*>(args[0])->alive; value = Box(&alive, 1); }
            else if (name == "SceneLoaded") { bool active = loaded[*static_cast<int*>(args[0])]; value = Box(&active, 1); }
            else if (name == "CameraMain") value = currentCamera;
            else if (name == "FindAll")
            {
                auto* wanted = static_cast<Object*>(args[0])->represented; auto* array = New(K("Object"));
                for (auto& item : objects)
                    if (item.get() != array && item->alive && item->active && item->klass == wanted)
                        array->array[array->count++] = item.get();
                value = array;
            }
            else if (name == "CreateInstance") value = New(static_cast<Object*>(args[0])->represented, 0, true);
            else if (name == "Add")
            {
                auto* actor = New(static_cast<Object*>(args[0])->represented, 0, true); uint8_t active = 1; Data(actor, "active", active);
                for (const auto& [fieldName, field] : actor->klass->fields)
                {
                    if (field.type.code != 0x12) continue;
                    auto* parameter = New(field.type.klass, 0, true); uint8_t overridden = 0; Data(parameter, "m_OverrideState", overridden);
                    Data(actor, fieldName.c_str(), parameter);
                }
                self->components[actor->klass] = actor; value = actor;
            }
            else if (name == "AddComponent" || name == "GetComponent")
            {
                auto* klass = static_cast<Object*>(args[0])->represented;
                auto it = self->components.find(klass);
                if (it != self->components.end()) value = it->second;
                else if (name == "AddComponent")
                { auto* component = New(klass, self->scene, true); component->go = self; self->components[klass] = component; value = component; }
            }
            else if (!self || !self->alive) return false;
            else if (name == "InstanceId") { value = Box(&self->id, 4); }
            else if (name == "GameObject") value = self->go;
            else if (name == "Transform") value = self->transform;
            else if (name == "SetActive") { ++writes; self->active = *static_cast<bool*>(args[0]); }
            else if (name == "get_layer") value = Box(&self->layer, 4);
            else if (name == "set_layer") { ++writes; if (!ignoreLayerSetter) self->layer = *static_cast<int*>(args[0]); }
            else if (name == "get_cullingMask") value = Box(&self->cullingMask, 4);
            else if (name == "get_volumeLayerMask") value = Box(&self->volumeMask, 4, K("LayerMask"));
            else if (name == "get_enabled") value = Box(&self->enabled, 1);
            else if (name == "get_isGlobal") value = Box(&self->global, 1);
            else if (name == "get_Color") value = Box(self->color.data(), 16, K("Color"));
            else if (name == "get_Intensity") value = Box(&self->intensity, 4, K("Single"));
            else if (name == "set_color" || name == "set_Color")
            { if (failRestore && self->klass == K("CampusDecalLight")) return false; ++writes; if (!ignoreColorSetter) std::memcpy(self->color.data(), args[0], 16); }
            else if (name == "set_intensity" || name == "set_Intensity") { ++writes; self->intensity = *static_cast<float*>(args[0]); }
            else if (name == "set_type") { ++writes; self->type = *static_cast<int*>(args[0]); }
            else if (name == "set_renderingLayerMask") { ++writes; std::memcpy(&self->mask, args[0], 4); }
            else if (name == "set_shadows") { ++writes; self->shadows = *static_cast<int*>(args[0]); }
            else if (name == "set_range") { ++writes; self->range = *static_cast<float*>(args[0]); }
            else if (name == "set_spotAngle") { ++writes; self->outer = *static_cast<float*>(args[0]); }
            else if (name == "set_innerSpotAngle") { ++writes; self->inner = *static_cast<float*>(args[0]); }
            else if (name == "set_position") { ++writes; std::memcpy(self->position.data(), args[0], 12); }
            else if (name == "set_rotation") { ++writes; std::memcpy(self->rotation.data(), args[0], 16); }
            else if (name == "set_profile") { ++writes; Data(self, "profile", static_cast<Object*>(args[0])); }
            else if (name == "set_isGlobal") { ++writes; self->global = *static_cast<bool*>(args[0]); }
            else if (name == "set_enabled") { ++writes; self->enabled = *static_cast<bool*>(args[0]); }
            else throw std::runtime_error("unexpected production API: " + name);
            if (result) *result = value; return true;
        }
        bool CreateOwnedRoot(const char*, const Context& context, Root& root)
        {
            ++createAttempts; if (failCreateAt && createAttempts == failCreateAt) return false;
            if (!context.allowed || !context.SameOwner(current)) return false;
            auto* go = New(K("GameObject"), context.owner.sceneHandle, true); go->active = false;
            go->transform = New(K("Transform"), go->scene, true); go->transform->go = go;
            ++created; root.Keep(go); root.instanceId = go->id; return true;
        }
        bool DestroyOwned(Root& root)
        {
            if (!root.Has()) return true;
            if (failDestroy) return false;
            auto* object = static_cast<Object*>(root.Get()); object->alive = false; object->active = false;
            for (auto& [klass, component] : object->components) component->alive = false;
            ++destroyed; root.Reset(); return true;
        }
    }
    void* StageLoader::Invoke(const void* method, void* object, void** args)
    { void* result = nullptr; StudioRuntime::Call(method, object, args, &result); return result; }
    int StageLoader::ReadInt(const void* method, void* object)
    { void* result = Invoke(method, object); int value = 0; if (result) std::memcpy(&value, Unbox(result), 4); return value; }

#include "GkStudioLightingRuntime.hpp"

    void Setup()
    {
        auto* object = AddClass("UnityEngine.CoreModule.dll", "UnityEngine", "Object");
        auto* component = AddClass("UnityEngine.CoreModule.dll", "UnityEngine", "Component", object);
        auto* behaviour = AddClass("UnityEngine.CoreModule.dll", "UnityEngine", "Behaviour", component);
        auto* go = AddClass("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject", object);
        auto* transform = AddClass("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", component);
        auto* light = AddClass("UnityEngine.CoreModule.dll", "UnityEngine", "Light", behaviour);
        auto* camera = AddClass("UnityEngine.CoreModule.dll", "UnityEngine", "Camera", behaviour);
        AddClass("UnityEngine.CoreModule.dll", "UnityEngine", "MeshRenderer", component);
        auto* additionalCamera = AddClass("Unity.RenderPipelines.Universal.Runtime.dll", "UnityEngine.Rendering.Universal", "UniversalAdditionalCameraData", behaviour);
        auto* layerMask = AddClass("UnityEngine.CoreModule.dll", "UnityEngine", "LayerMask", nullptr, 4);
        auto* type = AddClass("mscorlib.dll", "System", "Type");
        AddClass("mscorlib.dll", "System", "Single", nullptr, 4);
        auto* color = AddClass("UnityEngine.CoreModule.dll", "UnityEngine", "Color", nullptr, 16);
        AddClass("UnityEngine.CoreModule.dll", "UnityEngine", "Vector2", nullptr, 8);
        auto* vector3 = AddClass("UnityEngine.CoreModule.dll", "UnityEngine", "Vector3", nullptr, 12);
        auto* quaternion = AddClass("UnityEngine.CoreModule.dll", "UnityEngine", "Quaternion", nullptr, 16);
        auto* lightType = AddClass("UnityEngine.CoreModule.dll", "UnityEngine", "LightType", nullptr, 4);
        auto* shadows = AddClass("UnityEngine.CoreModule.dll", "UnityEngine", "LightShadows", nullptr, 4);
        lightType->fields["Spot"] = {"Spot", {0x11, lightType}, 0x50, 23}; // Not hardcoded Unity constants.
        shadows->fields["None"] = {"None", {0x11, shadows}, 0x50, 17};
        AddMethod(go, "AddComponent", {0x12, component}, {{0x12, type}});
        AddMethod(go, "GetComponent", {0x12, component}, {{0x12, type}});
        AddMethod(go, "get_layer", {0x08}); AddMethod(go, "set_layer", {0x01}, {{0x08}});
        AddMethod(behaviour, "set_enabled", {0x01}, {{0x02}});
        AddMethod(behaviour, "get_enabled", {0x02});
        AddMethod(camera, "get_cullingMask", {0x08});
        AddMethod(additionalCamera, "get_volumeLayerMask", {0x11, layerMask});
        AddMethod(light, "set_type", {0x01}, {{0x11, lightType}});
        AddMethod(light, "set_color", {0x01}, {{0x11, color}});
        for (const char* name : {"set_intensity", "set_range", "set_spotAngle", "set_innerSpotAngle"}) AddMethod(light, name, {0x01}, {{0x0c}});
        AddMethod(light, "set_renderingLayerMask", {0x01}, {{0x09}});
        AddMethod(light, "set_shadows", {0x01}, {{0x11, shadows}});
        AddMethod(transform, "set_position", {0x01}, {{0x11, vector3}});
        AddMethod(transform, "set_rotation", {0x01}, {{0x11, quaternion}});
        StageLoader::componentClass = component; StudioRuntime::gameObjectClass = go; StudioRuntime::transformClass = transform;
        StageLoader::instanceId = AddMethod(object, "InstanceId", {0x08});
        StageLoader::goTransform = AddMethod(go, "Transform", {0x12, transform});
        StageLoader::componentGo = AddMethod(component, "GameObject", {0x12, go});
        StageLoader::setActive = AddMethod(go, "SetActive", {0x01}, {{0x02}});
        StageLoader::sceneLoaded = AddMethod(object, "SceneLoaded", {0x02}, {{0x08}}, true);
        StageLoader::findAll = AddMethod(object, "FindAll", {0x1d}, {{0x12, type}, {0x02}}, true);
        StageLoader::cameraMain = AddMethod(camera, "CameraMain", {0x12, camera}, {}, true);
        StudioRuntime::aliveMethod = AddMethod(object, "Alive", {0x02}, {{0x12, object}}, true);
        auto* decal = AddClass("campus-submodule.Runtime.dll", "Campus.DecalLight", "CampusDecalLight", behaviour);
        AddMethod(decal, "get_Color", {0x11, color}); AddMethod(decal, "get_Intensity", {0x0c});
        AddMethod(decal, "set_Color", {0x01}, {{0x11, color}}); AddMethod(decal, "set_Intensity", {0x01}, {{0x0c}});
        auto* scriptable = AddClass("UnityEngine.CoreModule.dll", "UnityEngine", "ScriptableObject", object);
        auto* volume = AddClass("Unity.RenderPipelines.Core.Runtime.dll", "UnityEngine.Rendering", "Volume", behaviour);
        auto* profile = AddClass("Unity.RenderPipelines.Core.Runtime.dll", "UnityEngine.Rendering", "VolumeProfile", scriptable);
        auto* volumeComponent = AddClass("Unity.RenderPipelines.Core.Runtime.dll", "UnityEngine.Rendering", "VolumeComponent", scriptable);
        auto* actor = AddClass("Unity.RenderPipelines.Universal.Runtime.dll", "VL.Rendering", "VLActorParameter", volumeComponent);
        FieldDef(volume, "weight", {0x0c}); FieldDef(volume, "priority", {0x0c}); FieldDef(volumeComponent, "active", {0x02});
        auto* colorParameter = AddClass("fake", "", "ColorParameter");
        auto* angleParameter = AddClass("fake", "", "AngleParameter");
        auto* floatParameter = AddClass("fake", "", "FloatParameter");
        FieldDef(colorParameter, "m_Value", {0x11, color}); FieldDef(angleParameter, "m_Value", {0x11, K("Vector2")});
        FieldDef(floatParameter, "m_Value", {0x0c});
        for (auto* p : {colorParameter, angleParameter, floatParameter}) FieldDef(p, "m_OverrideState", {0x02});
        for (const char* name : {"lightColor", "shadeColorAdditive", "rimColor"}) FieldDef(actor, name, {0x12, colorParameter});
        for (const char* name : {"mainLightAngle", "rimAngle"}) FieldDef(actor, name, {0x12, angleParameter});
        FieldDef(actor, "rimPower", {0x12, floatParameter});
        AddMethod(scriptable, "CreateInstance", {0x12, scriptable}, {{0x12, type}}, true);
        AddMethod(profile, "Add", {0x12, volumeComponent}, {{0x12, type}, {0x02}});
        AddMethod(volume, "set_profile", {0x01}, {{0x12, profile}}); AddMethod(volume, "set_isGlobal", {0x01}, {{0x02}});
        AddMethod(volume, "get_isGlobal", {0x02});
        StageLoader::metadata = {Methods, MethodName, Count, Flags, Never, Never, Owner, Argument, Result, Code, ByRef, TypeClass};
        g_faceRuntime.objectClass = ObjectClass; g_faceRuntime.parentClass = Parent; g_faceRuntime.findField = FindField;
        g_faceRuntime.fieldType = FieldType; g_faceRuntime.fieldFlags = FieldFlags; g_faceRuntime.typeCode = Code;
        g_faceRuntime.readField = ReadField; g_faceRuntime.writeField = WriteField;
        currentCamera = New(camera, 10); currentCamera->go = New(go, 10);
        currentCamera->cullingMask = uint32_t{1} << 7; // Deliberately not the observed game's layer12.
        auto* cameraData = New(additionalCamera, 10); cameraData->go = currentCamera->go;
        cameraData->volumeMask = (uint32_t{1} << 9) | (uint32_t{1} << 17);
        currentCamera->go->components[additionalCamera] = cameraData;
    }
    Object* NativeDecal(int scene, float intensity)
    { auto* object = New(K("CampusDecalLight"), scene); object->go = New(K("GameObject"), scene); object->intensity = intensity; return object; }
    Object* NativeComponent(const char* name, int scene, int layer)
    { auto* object = New(K(name), scene); object->go = New(K("GameObject"), scene); object->go->layer = layer; return object; }
    void ClearCounts() { invokes = reflections = writes = created = destroyed = fieldWrites = createAttempts = 0; }
}

int main()
{
    using namespace LightingHarness;
    unsigned checks = 0, failures = 0;
    const auto check = [&](bool ok, const char* label) { ++checks; if (!ok) { ++failures; std::cerr << "FAIL: " << label << '\n'; } };
    try
    {
        Setup(); std::string notice; GakuMotion::studio::Lighting settings;
        NativeComponent("MeshRenderer", 20, 7);
        NativeComponent("MeshRenderer", 30, 7);
        auto* nativeVolume = NativeComponent("Volume", 20, 9);
        NativeComponent("Volume", 30, 9);
        NativeComponent("Volume", 10, 17);
        auto context = StudioRuntime::current;
        auto title = context; title.allowed = false; title.activeName = "Title";
        title.owner = {true, 900, 30, true, GakuMotion::injection::OwnerRole::Other, false};
        ClearCounts(); settings.enabled = true;
        for (int i = 0; i < 100; ++i) StudioLighting::Tick(settings, title, notice);
        check(invokes == 0 && reflections == 0 && created == 0, "Title has zero new reflection, query, creation or field writes");
        settings.enabled = false; StudioLighting::Tick(settings, context, notice);
        check(invokes == 0 && reflections == 0, "disabled Photography setting does not resolve or invoke Unity");
        settings.enabled = true; mainThread = false; StudioLighting::Tick(settings, context, notice);
        check(invokes == 0 && reflections == 0, "worker-thread context cannot resolve, create or invoke lighting APIs");
        mainThread = true; settings.keyIntensity = std::numeric_limits<float>::quiet_NaN();
        StudioLighting::Tick(settings, context, notice);
        check(invokes == 0 && reflections == 0 && created == 0,
            "invalid numeric settings are rejected before reflection or native object creation");
        settings.keyIntensity = 1.4f;

        auto* selected = NativeDecal(20, .005f); auto* otherScene = NativeDecal(30, .02f);
        const auto originalColor = selected->color;
        settings.enabled = true; settings.decalEnabled = true; settings.decalTint = {.5f, .75f, 1}; settings.decalIntensityScale = 2;
        StudioLighting::Tick(settings, context, notice);
        check(created == 4 && StudioLighting::HasObjects(), "three owned Spot GOs and one actor-volume GO are created");
        check(static_cast<const Method*>(StudioLighting::setMask)->args[0].code == 0x09,
            "UInt32 rendering-layer setter is preferred when present");
        for (auto& root : StudioLighting::lamps)
        {
            auto* go = static_cast<Object*>(root.Get()); auto* light = go->components.at(K("Light"));
            check(go->scene == context.owner.sceneHandle && go->active && light->type == 23 && light->shadows == 17 && light->mask == -1,
                "each owned lamp uses runtime enum values, all rendering layers and no shadow");
            check(go->layer == 7 && (currentCamera->cullingMask & (uint32_t{1} << go->layer)),
                "each Spot adopts the actual camera-visible environment layer rather than layer0 or hardcoded12");
        }
        check(static_cast<Object*>(StudioLighting::actorGo.Get())->layer == 9,
            "actor Volume adopts the actual global native Volume layer accepted by camera volume mask");
        check(selected->intensity == .01f && selected->color[0] == .5f && otherScene->intensity == .02f,
            "only exact active-scene DecalLight values are changed");
        auto* actor = static_cast<Object*>(StudioLighting::actorComponent.Get());
        auto* lightColor = Data<Object*>(actor, "lightColor");
        check(Data<uint8_t>(lightColor, "m_OverrideState") == 1 && Data<std::array<float, 4>>(lightColor, "m_Value")[0] == settings.keyColor[0] * settings.keyIntensity,
            "actor style uses only its owned parameter overrides");
        const auto liveCalls = invokes, liveWrites = writes, liveFieldWrites = fieldWrites;
        StudioLighting::Tick(settings, context, notice);
        check(invokes == liveCalls && writes == liveWrites && fieldWrites == liveFieldWrites,
            "unchanged settings perform no per-frame writes or object creation");
        settings.keyIntensity = 2; StudioLighting::Tick(settings, context, notice);
        check(created == 4 && static_cast<Object*>(StudioLighting::lamps[0].Get())->components.at(K("Light"))->intensity == 2,
            "configuration updates reuse owned lights rather than recreate GOs");
        settings.decalEnabled = false; StudioLighting::Tick(settings, context, notice);
        check(selected->intensity == .005f && selected->color == originalColor && StudioLighting::decals.empty(),
            "disabling the optional Decal control restores its precise original values");
        settings.decalEnabled = true; StudioLighting::Tick(settings, context, notice);
        ignoreColorSetter = true; settings.decalEnabled = false; StudioLighting::Tick(settings, context, notice);
        check(!StudioLighting::decals.empty(), "successful no-op setter does not discard a Decal snapshot before restoration readback matches");
        ignoreColorSetter = false; StudioLighting::nextCleanup = {}; StudioLighting::Tick(settings, context, notice);
        check(StudioLighting::decals.empty() && selected->color == originalColor,
            "restore retry verifies both original RGB and intensity before releasing the snapshot");
        settings.enabled = false; StudioLighting::Tick(settings, context, notice);
        check(!StudioLighting::HasObjects() && handles.empty(), "disable destroys own native objects and releases all GC roots");

        nativeVolume->enabled = false; settings.enabled = true; StudioLighting::Tick(settings, context, notice);
        check(StudioLighting::actorGo.Has() && static_cast<Object*>(StudioLighting::actorGo.Get())->layer == 17,
            "missing enabled environment Volume falls back only to observed native Volume in current Photography owner scene");
        settings.enabled = false; StudioLighting::Tick(settings, context, notice); nativeVolume->enabled = true;

        auto* ambiguousVolume = NativeComponent("Volume", 20, 17);
        settings.enabled = true; StudioLighting::Tick(settings, context, notice);
        check(StudioLighting::lamps[0].Has() && !StudioLighting::actorGo.Has() && notice.find("唯一") != std::string::npos,
            "ambiguous native Volume layers downgrade actor style instead of guessing a layer");
        settings.enabled = false; StudioLighting::Tick(settings, context, notice); ambiguousVolume->alive = false;

        auto* otherLayer = NativeComponent("MeshRenderer", 20, 8);
        currentCamera->cullingMask |= uint32_t{1} << 8; settings.enabled = true;
        const auto attemptsBeforeLayers = createAttempts; StudioLighting::Tick(settings, context, notice);
        check(!StudioLighting::HasObjects() && createAttempts == attemptsBeforeLayers,
            "multiple camera-visible environment layers reject creation rather than choose an arbitrary bit");
        const auto failedLayerInvokes = invokes, failedLayerReflections = reflections;
        for (int i = 0; i < 100; ++i) StudioLighting::Tick(settings, context, notice);
        check(createAttempts == attemptsBeforeLayers && invokes == failedLayerInvokes && reflections == failedLayerReflections,
            "failed layer selection does not create or scan again every frame");
        settings.enabled = false; StudioLighting::Tick(settings, context, notice);
        otherLayer->alive = false; currentCamera->cullingMask = uint32_t{1} << 7;
        ignoreLayerSetter = true; settings.enabled = true; StudioLighting::Tick(settings, context, notice);
        check(!StudioLighting::HasObjects() && handles.empty(), "successful no-op layer setter fails readback and rolls back owned lamp");
        ignoreLayerSetter = false; settings.enabled = false; StudioLighting::Tick(settings, context, notice);

        settings.enabled = true; StudioRuntime::current.allowed = false;
        const auto beforeStaleInvokes = invokes, beforeStaleReflections = reflections;
        StudioLighting::Tick(settings, context, notice);
        check(!StudioLighting::HasObjects() && invokes == beforeStaleInvokes && reflections == beforeStaleReflections,
            "stale allowed snapshot with current permission revoked cannot query layers or create lamps");
        StudioRuntime::current.allowed = true; settings.enabled = false; StudioLighting::Tick(settings, context, notice);
        StudioRuntime::currentReads = 0; StudioRuntime::revokeOnCurrentRead = 2;
        settings.enabled = true; const auto beforeLostPermission = createAttempts;
        StudioLighting::Tick(settings, context, notice);
        check(!StudioLighting::HasObjects() && createAttempts == beforeLostPermission && handles.empty(),
            "permission revoked after camera-layer sampling prevents all creation and releases temporary roots");
        StudioRuntime::revokeOnCurrentRead = 0; settings.enabled = false; StudioLighting::Tick(settings, context, notice);

        actorMissing = true; StudioLighting::actorClass = nullptr; settings.enabled = true;
        StudioLighting::Tick(settings, context, notice);
        check(StudioLighting::lamps[0].Has() && !StudioLighting::actorGo.Has() && notice.find("接口不可用") != std::string::npos,
            "optional actor API absence is reported without claiming actor-style success");
        settings.enabled = false; StudioLighting::Tick(settings, context, notice); actorMissing = false;

        settings.enabled = true; settings.actorStyle = false; settings.decalEnabled = true;
        StudioLighting::Tick(settings, context, notice); failAlive = true; settings.enabled = false;
        StudioLighting::Tick(settings, context, notice);
        check(!StudioLighting::decals.empty() && StudioLighting::HasObjects(),
            "native-alive query exception retains original Decal snapshot and owned cleanup roots");
        failAlive = false; StudioLighting::nextCleanup = {}; StudioLighting::Tick(settings, context, notice);
        check(!StudioLighting::HasObjects() && selected->intensity == .005f, "a later bounded cleanup retry restores borrowed values and frees own roots");

        settings.enabled = true; settings.decalEnabled = false; failCreateAt = createAttempts + 2;
        StudioLighting::Tick(settings, context, notice);
        check(!StudioLighting::HasObjects(), "partial lamp creation failure rolls back prior own lamp");
        const int attempted = createAttempts; for (int i = 0; i < 100; ++i) StudioLighting::Tick(settings, context, notice);
        check(createAttempts == attempted, "unchanged failed configuration does not create again every frame");
        failCreateAt = 0; settings.keyYaw += 1; StudioLighting::Tick(settings, context, notice);
        check(StudioLighting::lamps[2].Has(), "explicit configuration change permits a new creation attempt");

        const auto oldOwner = context.owner;
        settings.decalEnabled = true; StudioLighting::Tick(settings, context, notice);
        loaded[20] = false; selected->alive = false;
        context.owner.presenterIdentity = 701; context.owner.sceneHandle = 30; context.activeScene = 30;
        StudioRuntime::current = context; StudioLighting::Tick(settings, context, notice);
        check(StudioLighting::bound.owner.presenterIdentity == 701 && !oldOwner.SameOwner(context.owner),
            "new Photography owner receives new lamps after previous lease cleanup");
        check(!selected->alive && selected->intensity == .01f,
            "already unloaded old Decal scene is retired without touching its destroyed component");
        settings.enabled = false; StudioLighting::Tick(settings, context, notice);
        check(!StudioLighting::HasObjects() && handles.empty() && otherScene->intensity == .02f,
            "final shutdown restores the new owner's background and leaves no roots");
        for (const auto& method : K("Light")->methods)
            if (method->name == "set_renderingLayerMask") method->args[0].code = 0x08;
        StudioLighting::setRotation = nullptr; settings.enabled = true; settings.decalEnabled = false;
        StudioLighting::Tick(settings, context, notice);
        check(StudioLighting::lamps[0].Has() && static_cast<const Method*>(StudioLighting::setMask)->args[0].code == 0x08,
            "current-client Int32 rendering-layer overload remains an exact checked fallback");
        settings.enabled = false; StudioLighting::Tick(settings, context, notice);
        check(!StudioLighting::HasObjects() && handles.empty(), "fallback overload also leaves no owned objects or roots after disable");
        settings.enabled = true; StudioLighting::Tick(settings, context, notice);
        g_runtimeReady.store(false);
        check(StudioLighting::Shutdown(notice) && !StudioLighting::HasObjects() && handles.empty(),
            "host deactivation can revoke existing lighting after runtimeReady is cleared");
        g_runtimeReady.store(true);
        std::cout << "Studio lighting production runtime: " << checks << " checks, " << failures << " failures\n";
    }
    catch (const std::exception& error) { ++failures; std::cerr << "Harness exception: " << error.what() << '\n'; }
    return failures ? 1 : 0;
}
