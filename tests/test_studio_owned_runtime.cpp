// Exercise the production owned-Unity adapter against an asynchronous-destruction
// fake. No Unity/game process is loaded and no rendered-light claim is made.
#include "GkInjectionPolicy.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace GakuMotion
{
    struct Core
    {
        bool allowed{true};
        bool InjectionAllowed() const { return allowed; }
        static Core& Instance() { static Core value; return value; }
    };
}

namespace StudioOwnedHarness
{
    enum class Method : uintptr_t { Alive = 1, Destroy, Constructor, SetParent, InstanceId,
        GoTransform, ComponentGo, SetActive, ActiveSelf, GoScene, FindAll };
    const void* Token(Method method) { return reinterpret_cast<const void*>(static_cast<uintptr_t>(method)); }
    struct Counters
    {
        unsigned reflection{}, invocations{}, strings{}, allocations{}, constructors{}, searches{}, parents{};
        unsigned roots{}, releases{}, deactivations{}, destroys{};
    };
    inline Counters calls;
    inline int objectClassTag{}, gameObjectClassTag{}, componentClassTag{}, transformClassTag{}, photoClassTag{}, stringClassTag{};
    struct Object
    {
        void* klass{};
        bool nativeAlive{true}, active{true};
        int id{}, scene{};
        Object *go{}, *transform{}, *parent{};
    };
    struct Array
    {
        std::array<unsigned char, 32> header{};
        std::array<void*, 2> values{};
        uintptr_t count{1};
    };
    static_assert(offsetof(Array, values) == 32);
    inline Array photoArray;
    inline std::vector<std::unique_ptr<Object>> objects;
    inline std::unordered_map<void*, void*> handles;
    inline uintptr_t nextHandle{1};
    inline int nextId{700};
    inline bool unityThread{true}, ignoreDeactivate{}, throwDeactivate{}, throwDestroy{}, wrongParentScene{}, failRootTarget{}, failString{};
    inline Object *photoPresenter{}, *photoGo{}, *lastAllocated{};
    inline bool boolBox{};
    inline int intBox{};
    inline int exceptionTag{};
    inline std::atomic<bool> g_runtimeReady{true};
    inline bool g_activeSceneVerified{true};
    inline int g_verifiedActiveSceneHandle{422};
    inline std::string g_verifiedOwnerSceneName{"Photography"};
    inline GakuMotion::injection::OwnerSnapshot g_sceneOwner;
    inline void* g_gameAssembly = reinterpret_cast<void*>(1);
    bool ProtectionOnUnityThread() { return unityThread; }

    Object* Make(void* klass, int scene = 0)
    {
        auto value = std::make_unique<Object>();
        value->klass = klass; value->id = ++nextId; value->scene = scene;
        auto* result = value.get(); objects.push_back(std::move(value)); return result;
    }
    Object* GameObject(int scene)
    {
        auto* go = Make(&gameObjectClassTag, scene);
        go->go = go;
        go->transform = Make(&transformClassTag, scene);
        go->transform->go = go;
        return go;
    }
    Object* Checked(void* value)
    {
        for (const auto& object : objects) if (object.get() == value) return object.get();
        throw std::runtime_error("production adapter dereferenced a non-object fake address");
    }
    void* ObjectClass(void* value) { return Checked(value)->klass; }
    bool Assignable(void* expected, void* actual)
    {
        return expected == actual || (expected == &componentClassTag &&
            (actual == &photoClassTag || actual == &transformClassTag || actual == &componentClassTag));
    }
    void* NewHandle(void* value, bool)
    {
        if (!value) return nullptr;
        auto handle = reinterpret_cast<void*>(nextHandle++);
        handles[handle] = failRootTarget ? nullptr : value;
        ++calls.roots;
        return handle;
    }
    void FreeHandle(void* handle)
    {
        if (handles.erase(handle) != 1) throw std::runtime_error("double/invalid owned GC root release");
        ++calls.releases;
    }
    void* HandleTarget(void* handle)
    {
        auto found = handles.find(handle);
        if (found == handles.end()) throw std::runtime_error("accessed a released fake GC root");
        return found->second;
    }
    void* NewString(const char*)
    { ++calls.strings; return failString ? nullptr : Make(&stringClassTag); }
    void* ObjectNew(void* klass)
    {
        ++calls.allocations;
        lastAllocated = Make(klass, g_verifiedActiveSceneHandle);
        lastAllocated->nativeAlive = false; lastAllocated->go = lastAllocated;
        lastAllocated->transform = Make(&transformClassTag, g_verifiedActiveSceneHandle);
        lastAllocated->transform->go = lastAllocated;
        return lastAllocated;
    }
    void* GetProcAddress(void*, const char*)
    { ++calls.reflection; return reinterpret_cast<void*>(&ObjectNew); }
    void* FindClass(const char*, const char*, const char* name)
    {
        ++calls.reflection;
        if (std::string(name) == "Object") return &objectClassTag;
        if (std::string(name) == "GameObject") return &gameObjectClassTag;
        if (std::string(name) == "Transform") return &transformClassTag;
        return nullptr;
    }
    void* Invoke(const void* method, void* target, void** args, void** exception)
    {
        ++calls.invocations;
        *exception = nullptr;
        const auto key = static_cast<Method>(reinterpret_cast<uintptr_t>(method));
        if (key == Method::Alive) { boolBox = Checked(args[0])->nativeAlive; return &boolBox; }
        if (key == Method::Destroy)
        {
            ++calls.destroys;
            Checked(args[0]);
            if (throwDestroy) *exception = &exceptionTag;
            // Deliberately leave nativeAlive=true until a later engine flush.
            return nullptr;
        }
        if (key == Method::FindAll) { ++calls.searches; return &photoArray; }
        auto* object = Checked(target);
        switch (key)
        {
        case Method::Constructor: ++calls.constructors; object->nativeAlive = true; object->active = true; return nullptr;
        case Method::InstanceId: intBox = object->id; return &intBox;
        case Method::GoTransform: return object->transform;
        case Method::ComponentGo: return object->go;
        case Method::GoScene: intBox = object->scene; return &intBox;
        case Method::ActiveSelf: boolBox = object->active; return &boolBox;
        case Method::SetActive:
            ++calls.deactivations;
            if (throwDeactivate) { *exception = &exceptionTag; return nullptr; }
            if (!ignoreDeactivate) object->active = *static_cast<bool*>(args[0]);
            return nullptr;
        case Method::SetParent:
            ++calls.parents;
            object->parent = Checked(args[0]);
            if (!wrongParentScene) object->go->scene = object->parent->go->scene;
            return nullptr;
        default: throw std::runtime_error("unimplemented fake Unity method");
        }
    }
    struct FaceAccess { decltype(&Invoke) invoke{Invoke}; decltype(&ObjectClass) objectClass{ObjectClass}; };
    inline FaceAccess g_faceRuntime;
    struct SafeBones { void* (*unbox)(void*){[](void* object) { return object; }}; };
    inline SafeBones g_safeBones;
    struct Metadata
    {
        struct Type { int code{}; void* klass{}; };
        const void* Find(void*, const char* name, bool, Type, std::initializer_list<Type>)
        {
            ++calls.reflection;
            const std::string key(name);
            if (key == "Destroy") return Token(Method::Destroy);
            if (key == "op_Implicit") return Token(Method::Alive);
            if (key == ".ctor") return Token(Method::Constructor);
            if (key == "SetParent") return Token(Method::SetParent);
            return nullptr;
        }
    };
    namespace StageLoader
    {
        using Handle = void*;
        inline Metadata metadata;
        inline auto newHandle = &NewHandle;
        inline auto freeHandle = &FreeHandle;
        inline auto stringNew = &NewString;
        inline auto assignable = &Assignable;
        inline void* componentClass = &componentClassTag;
        inline const void *instanceId = Token(Method::InstanceId), *goTransform = Token(Method::GoTransform),
            *componentGo = Token(Method::ComponentGo), *setActive = Token(Method::SetActive),
            *activeSelf = Token(Method::ActiveSelf), *goScene = Token(Method::GoScene), *findAll = Token(Method::FindAll);
        inline uintptr_t (*arrayLength)(void*){[](void* value) { return static_cast<Array*>(value)->count; }};
        void* Invoke(const void* method, void* target = nullptr, void** args = nullptr)
        {
            void* exception = nullptr;
            void* value = StudioOwnedHarness::Invoke(method, target, args, &exception);
            return exception ? nullptr : value;
        }
        int ReadInt(const void* method, void* target, int fallback = 0)
        { auto* value = static_cast<int*>(Invoke(method, target)); return value ? *value : fallback; }
        bool ReadBool(const void* method, void* target, bool fallback = false)
        { auto* value = static_cast<bool*>(Invoke(method, target)); return value ? *value : fallback; }
        int SceneOfFreshGo(void* go) { return ReadInt(goScene, go); }
    }
    void* ClassType(void* klass) { ++calls.reflection; return klass; }
    void* TypeObject(void*) { ++calls.reflection; return Make(&stringClassTag); }
    inline auto il2cpp_class_get_type = &ClassType;
    inline auto il2cpp_type_get_object = &TypeObject;
    namespace SceneOwnerRuntime
    {
        inline bool ready{true};
        inline void* photoClass = &photoClassTag;
        inline auto handleTarget = &HandleTarget;
        inline std::string cachedActiveScene{"env_3d_live_all001-00-noon"};
        struct Observation { GakuMotion::injection::OwnerSnapshot owner; std::string sceneName; };
        Observation ReadCandidate(void* value)
        {
            auto* candidate = Checked(value);
            auto owner = g_sceneOwner;
            owner.presenterIdentity = reinterpret_cast<uintptr_t>(candidate);
            owner.sceneHandle = candidate->go->scene;
            return {owner, "Photography"};
        }
    }

#include "../src/GkStudioRuntime.hpp"

    inline int passed{}, failed{};
    void Check(bool condition, const char* message)
    {
        if (condition) { ++passed; std::printf("PASS: %s\n", message); }
        else { ++failed; std::printf("FAIL: %s\n", message); }
    }
    void Setup()
    {
        if (!handles.empty()) throw std::runtime_error("previous test leaked an owned GC handle");
        calls = {};
        unityThread = g_activeSceneVerified = true;
        ignoreDeactivate = throwDeactivate = throwDestroy = wrongParentScene = failRootTarget = failString = false;
        GakuMotion::Core::Instance().allowed = true;
        g_runtimeReady.store(true);
        g_verifiedOwnerSceneName = "Photography";
        g_verifiedActiveSceneHandle = 422;
        SceneOwnerRuntime::cachedActiveScene = "env_3d_live_all001-00-noon";
        SceneOwnerRuntime::ready = true;
        photoGo = GameObject(421);
        photoPresenter = Make(&photoClassTag, 421); photoPresenter->go = photoGo;
        photoArray.values[0] = photoPresenter; photoArray.count = 1;
        g_sceneOwner = {true, reinterpret_cast<uintptr_t>(photoPresenter), 421, true,
            GakuMotion::injection::OwnerRole::Photography, false};
        StudioRuntime::ready = false;
        StudioRuntime::error.clear();
    }
    bool NoEntryWork() { return calls.reflection == 0 && calls.invocations == 0 && calls.allocations == 0 && calls.strings == 0; }
    void ReadyForCleanup()
    {
        if (!StudioRuntime::Resolve()) throw std::runtime_error("fake metadata did not resolve");
        calls = {};
    }
    Object* Own(StudioRuntime::Root& root)
    {
        auto* value = GameObject(421);
        if (!root.Keep(value)) throw std::runtime_error("cannot root fake owned object");
        root.instanceId = value->id;
        return value;
    }
}

int main()
{
    using namespace StudioOwnedHarness;
    namespace runtime = StudioOwnedHarness::StudioRuntime;
    try
    {
        Setup();
        {
            runtime::Root root;
            auto disabled = runtime::Current(); disabled.allowed = false;
            Check(!runtime::CreateOwnedRoot("disabled", disabled, root) && NoEntryWork() && !root.Has(),
                "disabled request performs zero reflection, managed calls, strings or object creation");
            g_verifiedOwnerSceneName = "Title";
            g_sceneOwner.role = GakuMotion::injection::OwnerRole::Other;
            Check(!runtime::CreateOwnedRoot("Title", runtime::Current(), root) && NoEntryWork(),
                "Title scene performs zero new managed or reflection work");
        }
        Setup();
        {
            runtime::Root root;
            const auto permitted = runtime::Current();
            unityThread = false;
            Check(!runtime::CreateOwnedRoot("worker", permitted, root) && NoEntryWork(),
                "worker call cannot reflect, allocate or invoke Unity even with an earlier permitted context");
            unityThread = true;
            GakuMotion::Core::Instance().allowed = false;
            Check(permitted.SameOwner(runtime::Current()) && !runtime::CreateOwnedRoot("revoked", permitted, root) && NoEntryWork(),
                "same-owner context cannot create after Current.allowed has been revoked");
            GakuMotion::Core::Instance().allowed = true;
            g_runtimeReady.store(false);
            Check(!runtime::CreateOwnedRoot("not ready", permitted, root) && NoEntryWork(),
                "runtime-not-ready state rejects a stale permitted context before reflection or allocation");
            g_runtimeReady.store(true); g_activeSceneVerified = false;
            Check(!runtime::CreateOwnedRoot("unverified scene", permitted, root) && NoEntryWork(),
                "unverified active scene rejects creation before any new Unity work");
        }
        Setup();
        {
            runtime::Root root;
            const auto context = runtime::Current();
            Check(runtime::CreateOwnedRoot("Photo rig", context, root) && root.Has(),
                "allowed Photography request creates through the production root adapter");
            auto* go = Checked(root.Get());
            Check(go->scene == context.owner.sceneHandle && go->scene != context.activeScene && !go->active &&
                go->transform->parent == photoGo->transform && calls.searches == 1 && calls.parents == 1 && root.instanceId == go->id,
                "created inactive rig is parented to the fresh Photo owner and moved out of the additive active scene");
            Check(runtime::DestroyOwned(root) && !root.Has() && !go->active && go->nativeAlive,
                "successful cleanup confirms inactive before scheduling Destroy without claiming native destruction completed");
            Check(handles.empty() && calls.roots == calls.releases, "successful creation and cleanup release every temporary and retained root once");
        }
        Setup();
        {
            runtime::Root root;
            failString = true;
            Check(!runtime::CreateOwnedRoot("failed string", runtime::Current(), root) && !root.Has() &&
                calls.allocations == 0 && handles.empty(), "failed name allocation creates no Unity object or retained root");
            failString = false; failRootTarget = true;
            Check(!root.Keep(photoGo) && !root.Has() && handles.empty() && calls.roots == calls.releases,
                "a GC root with no target is released immediately instead of leaving an uncleareable handle");
        }
        Setup(); ReadyForCleanup();
        {
            runtime::Root root; auto* go = Own(root); ignoreDeactivate = true;
            Check(!runtime::DestroyOwned(root) && root.Has() && go->active && calls.destroys == 0,
                "ignored SetActive cannot report cleanup success or discard its retained root");
            ignoreDeactivate = false;
            Check(runtime::DestroyOwned(root) && !go->active && !root.Has(), "ignored deactivation remains retryable");
        }
        Setup(); ReadyForCleanup();
        {
            runtime::Root root; ignoreDeactivate = true;
            Check(!runtime::CreateOwnedRoot("ignored create deactivation", runtime::Current(), root) && root.Has() &&
                lastAllocated && lastAllocated->active && calls.parents == 0 && calls.destroys == 0,
                "creation refuses an unconfirmed inactive root and retains it when rollback deactivation is also ignored");
            ignoreDeactivate = false;
            Check(runtime::DestroyOwned(root) && !root.Has(), "failed creation remains owned and can be cleaned on a later main-thread retry");
        }
        Setup(); ReadyForCleanup();
        {
            runtime::Root root; auto* go = Own(root); throwDeactivate = true;
            Check(!runtime::DestroyOwned(root) && root.Has() && go->active && calls.destroys == 0,
                "managed deactivation exception retains ownership and does not issue Destroy");
            throwDeactivate = false; runtime::DestroyOwned(root);
        }
        Setup(); ReadyForCleanup();
        {
            runtime::Root root; auto* go = Own(root); throwDestroy = true;
            Check(!runtime::DestroyOwned(root) && root.Has() && !go->active && go->nativeAlive && calls.destroys == 1,
                "Destroy exception retains the synchronously inactive object's GC root");
            throwDestroy = false;
            Check(runtime::DestroyOwned(root) && !root.Has() && go->nativeAlive, "failed Destroy can be retried without pretending the fake native object vanished");
        }
        Setup(); ReadyForCleanup();
        {
            runtime::Root root; auto* go = Own(root); ++root.instanceId;
            Check(!runtime::DestroyOwned(root) && root.Has() && go->active && calls.deactivations == 0 && calls.destroys == 0,
                "mismatched instance identity prevents mutation and preserves the owned root");
            root.instanceId = go->id; runtime::DestroyOwned(root);
        }
        Setup(); ReadyForCleanup();
        {
            runtime::Root root; auto* go = Own(root); go->klass = nullptr;
            Check(!runtime::DestroyOwned(root) && root.Has() && go->active && calls.deactivations == 0 && calls.destroys == 0,
                "unavailable runtime class cannot silently fall through to destroying a still-active owned object");
            go->klass = &gameObjectClassTag; runtime::DestroyOwned(root);
        }
        Setup(); ReadyForCleanup();
        {
            runtime::Root root; auto* go = Own(root); go->nativeAlive = false;
            Check(runtime::DestroyOwned(root) && !root.Has() && calls.deactivations == 0 && calls.destroys == 0,
                "already destroyed native object releases only its managed shell root");
        }
        Setup(); ReadyForCleanup();
        {
            runtime::Root root;
            auto* go = GameObject(421);
            auto* component = Make(&componentClassTag, 421); component->go = go;
            root.Keep(component); root.instanceId = component->id;
            Check(runtime::DestroyOwned(root) && !root.Has() && !go->active && component->nativeAlive &&
                calls.deactivations == 1 && calls.destroys == 1 && handles.empty(),
                "owned Component cleanup confirms its own GameObject inactive before requesting Component destruction");
        }
        Setup(); ReadyForCleanup();
        {
            runtime::Root root; auto* go = Own(root); unityThread = false;
            Check(!runtime::DestroyOwned(root) && root.Has() && calls.invocations == 0 && go->active,
                "worker cleanup preserves the root and performs zero Unity calls");
            unityThread = true; runtime::DestroyOwned(root);
        }
        Setup();
        {
            runtime::Root root; wrongParentScene = true;
            Check(!runtime::CreateOwnedRoot("wrong scene", runtime::Current(), root) && !root.Has() &&
                lastAllocated && !lastAllocated->active && lastAllocated->nativeAlive && calls.destroys == 1,
                "failed Photo scene attachment rolls back its own inactive object without accepting the wrong scene");
        }
        Check(handles.empty(), "all owned-adapter cases finish without GC handle leaks");
    }
    catch (const std::exception& error)
    {
        ++failed;
        std::printf("FAIL: owned adapter harness exception: %s\n", error.what());
    }
    std::printf("Studio owned runtime checks: %d passed, %d failed\n", passed, failed);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
