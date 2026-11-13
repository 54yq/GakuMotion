#pragma once

// Private plugin adapter. Resolve/create only after the explicit Photography
// gate. Cleanup touches only strongly rooted objects created by this adapter.
namespace StudioRuntime
{
    inline std::string error;
    struct Context
    {
        GakuMotion::injection::OwnerSnapshot owner;
        int activeScene{};
        std::string activeName;
        bool allowed{};
        bool SameOwner(const Context& other) const
        { return owner.SameOwner(other.owner) && activeScene == other.activeScene; }
    };
    inline Context Current()
    {
        const auto& core = GakuMotion::Core::Instance();
        const bool photo = g_sceneOwner.Valid() &&
            g_sceneOwner.role == GakuMotion::injection::OwnerRole::Photography &&
            g_verifiedOwnerSceneName == "Photography" && g_activeSceneVerified;
        return {g_sceneOwner, g_verifiedActiveSceneHandle, SceneOwnerRuntime::cachedActiveScene,
            photo && core.InjectionAllowed() && ProtectionOnUnityThread() && g_runtimeReady.load(std::memory_order_acquire)};
    }
    struct Root
    {
        StageLoader::Handle handle{};
        int instanceId{};
        Root() = default;
        Root(const Root&) = delete;
        Root& operator=(const Root&) = delete;
        Root(Root&& other) noexcept : handle(other.handle), instanceId(other.instanceId)
        { other.handle = nullptr; other.instanceId = 0; }
        Root& operator=(Root&& other) noexcept
        {
            if (this != &other) { Reset(); handle = other.handle; instanceId = other.instanceId; other.handle = nullptr; other.instanceId = 0; }
            return *this;
        }
        ~Root() { Reset(); }
        void* Get() const
        { return handle && SceneOwnerRuntime::handleTarget ? SceneOwnerRuntime::handleTarget(handle) : nullptr; }
        void* Keep(void* value)
        {
            if (handle) return Get() == value ? value : nullptr;
            if (!value || !StageLoader::newHandle || !StageLoader::freeHandle) return nullptr;
            handle = StageLoader::newHandle(value, false);
            void* target = Get();
            if (!target) Reset();
            return target;
        }
        bool Has() const { return handle != nullptr; }
        explicit operator bool() const { return Has(); }
        void Reset()
        {
            if (handle && StageLoader::freeHandle) StageLoader::freeHandle(handle);
            handle = nullptr; instanceId = 0;
        }
    };
    struct ScopedRoots
    {
        std::vector<StageLoader::Handle> handles;
        ~ScopedRoots() { for (auto handle : handles) StageLoader::freeHandle(handle); }
        void* Keep(void* value)
        {
            if (!value || !StageLoader::newHandle || !StageLoader::freeHandle || !SceneOwnerRuntime::handleTarget) return nullptr;
            auto handle = StageLoader::newHandle(value, false);
            if (!handle) return nullptr;
            handles.push_back(handle);
            return SceneOwnerRuntime::handleTarget(handle);
        }
    };
    inline void *objectClass{}, *gameObjectClass{}, *transformClass{};
    inline const void *destroyMethod{}, *aliveMethod{}, *goConstructor{}, *setParent{};
    inline void* (*objectNew)(void*){};
    inline bool ready{};
    inline bool Call(const void* method, void* target, void** args = nullptr, void** result = nullptr)
    {
        if (!method || !g_faceRuntime.invoke || !ProtectionOnUnityThread())
        { error = "Unity method or main thread unavailable"; return false; }
        void* exception = nullptr;
        void* value = g_faceRuntime.invoke(method, target, args, &exception);
        if (exception) { error = "Unity method raised an exception"; return false; }
        if (result) *result = value;
        return true;
    }
    inline bool Resolve()
    {
        if (ready) return true;
        if (!ProtectionOnUnityThread()) return false;
        objectClass = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Object");
        gameObjectClass = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject");
        transformClass = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Transform");
        objectNew = reinterpret_cast<decltype(objectNew)>(GetProcAddress(g_gameAssembly, "il2cpp_object_new"));
        auto& m = StageLoader::metadata;
        destroyMethod = m.Find(objectClass, "Destroy", true, {0x01}, {{0x12, objectClass}});
        aliveMethod = m.Find(objectClass, "op_Implicit", true, {0x02}, {{0x12, objectClass}});
        goConstructor = m.Find(gameObjectClass, ".ctor", false, {0x01}, {{0x0e}});
        setParent = m.Find(transformClass, "SetParent", false, {0x01}, {{0x12, transformClass}, {0x02}});
        ready = destroyMethod && aliveMethod && goConstructor && setParent && objectNew &&
            StageLoader::newHandle && StageLoader::freeHandle && SceneOwnerRuntime::handleTarget &&
            StageLoader::instanceId && StageLoader::goTransform && StageLoader::componentGo &&
            StageLoader::stringNew && StageLoader::setActive && StageLoader::activeSelf && StageLoader::goScene &&
            StageLoader::componentClass && StageLoader::assignable && g_faceRuntime.objectClass && g_safeBones.unbox;
        if (!ready) error = "Owned Unity object metadata contract unavailable";
        return ready;
    }
    inline bool Alive(void* object)
    {
        if (!object || !ready) return false;
        void* args[]{object}; void* boxed = nullptr;
        if (!Call(aliveMethod, nullptr, args, &boxed) || !boxed) return false;
        void* value = g_safeBones.unbox(boxed);
        return value && *static_cast<bool*>(value);
    }
    inline bool DestroyOwned(Root& object)
    {
        if (!object.Has()) return true;
        if (!ProtectionOnUnityThread() || !ready) return false;
        void* target = object.Get();
        if (!target) { error = "Owned GC root target unavailable"; return false; }
        // A destroyed Unity native object still has a rooted managed shell.
        void* aliveArgs[]{target}; void* boxed = nullptr;
        if (!Call(aliveMethod, nullptr, aliveArgs, &boxed) || !boxed || !g_safeBones.unbox(boxed)) return false;
        if (!*static_cast<bool*>(g_safeBones.unbox(boxed))) { object.Reset(); return true; }
        if (object.instanceId && StageLoader::ReadInt(StageLoader::instanceId, target) != object.instanceId)
        { error = "Owned Unity instance identity changed"; return false; }
        ScopedRoots roots;
        void* klass = g_faceRuntime.objectClass(target);
        if (!klass) { error = "Owned Unity object class unavailable"; return false; }
        void* go = StageLoader::assignable(gameObjectClass, klass) ? target : nullptr;
        if (!go && StageLoader::assignable(StageLoader::componentClass, klass))
        {
            if (!Call(StageLoader::componentGo, target, nullptr, &go) || !(go = roots.Keep(go))) return false;
        }
        if (go)
        {
            // Destroy is deferred. Synchronous deactivation and readback are
            // the native-capture restoration gate; deletion follows normally.
            bool active = false; void* inactiveArgs[]{&active};
            if (!Call(StageLoader::setActive, go, inactiveArgs) ||
                !Call(StageLoader::activeSelf, go, nullptr, &boxed) || !boxed || !g_safeBones.unbox(boxed) ||
                *static_cast<bool*>(g_safeBones.unbox(boxed)))
            { error = "Owned Unity object did not confirm inactive"; return false; }
        }
        void* args[]{target};
        if (!Call(destroyMethod, nullptr, args)) return false;
        object.Reset();
        return true;
    }
    inline void* FreshPhotoRoot(ScopedRoots& roots, const Context& context)
    {
        const auto current = Current();
        if (!context.allowed || !current.allowed || !context.SameOwner(current) || !SceneOwnerRuntime::ready ||
            !StageLoader::findAll || !StageLoader::arrayLength || !il2cpp_class_get_type || !il2cpp_type_get_object) return nullptr;
        void* type = roots.Keep(il2cpp_type_get_object(il2cpp_class_get_type(SceneOwnerRuntime::photoClass)));
        bool inactive = false;
        void* args[]{type, &inactive}; void* array = nullptr;
        if (!type || !Call(StageLoader::findAll, nullptr, args, &array) || !(array = roots.Keep(array))) return nullptr;
        if (StageLoader::arrayLength(array) != 1) return nullptr;
        void* root = reinterpret_cast<void**>(static_cast<char*>(array) + 32)[0];
        if (!root || g_faceRuntime.objectClass(root) != SceneOwnerRuntime::photoClass) return nullptr;
        root = roots.Keep(root);
        const auto observed = SceneOwnerRuntime::ReadCandidate(root);
        return observed.owner.SameOwner(context.owner) && observed.sceneName == "Photography" ? root : nullptr;
    }
    inline bool AttachOwnedRoot(void* go, const Context& context)
    {
        ScopedRoots roots;
        void* presenter = FreshPhotoRoot(roots, context);
        if (!presenter) { error = "Fresh Photography parent unavailable"; return false; }
        void* parentGo = roots.Keep(StageLoader::Invoke(StageLoader::componentGo, presenter));
        void* parentTransform = roots.Keep(StageLoader::Invoke(StageLoader::goTransform, parentGo));
        void* childTransform = roots.Keep(StageLoader::Invoke(StageLoader::goTransform, go));
        bool worldPositionStays = false;
        void* args[]{parentTransform, &worldPositionStays};
        if (!parentTransform || !childTransform || !Call(setParent, childTransform, args)) return false;
        return StageLoader::SceneOfFreshGo(go) == context.owner.sceneHandle;
    }
    inline bool CreateOwnedRoot(const char* name, const Context& context, Root& output)
    {
        const auto current = Current();
        if (!context.allowed || !current.allowed || !context.SameOwner(current) || output.Has() || !Resolve()) return false;
        error.clear();
        ScopedRoots roots;
        void* text = roots.Keep(StageLoader::stringNew(name));
        if (!text) return false;
        void* go = output.Keep(objectNew(gameObjectClass));
        if (!go) return false;
        void* args[]{text};
        if (!Call(goConstructor, go, args)) { DestroyOwned(output); return false; }
        output.instanceId = StageLoader::ReadInt(StageLoader::instanceId, go);
        if (!output.instanceId || !StageLoader::setActive || !StageLoader::ReadBool(StageLoader::activeSelf, go, false))
        { DestroyOwned(output); return false; }
        bool inactive = false; void* activeArgs[]{&inactive};
        void* boxed = nullptr;
        if (!Call(StageLoader::setActive, go, activeArgs) ||
            !Call(StageLoader::activeSelf, go, nullptr, &boxed) || !boxed || !g_safeBones.unbox(boxed) ||
            *static_cast<bool*>(g_safeBones.unbox(boxed)) || !AttachOwnedRoot(go, context))
        { DestroyOwned(output); return false; }
        return true;
    }
}
