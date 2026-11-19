#pragma once

// The Live timeline can bind a model on a separate scene branch to a character.
// Read the game's slot bindings and mask only the model's mesh renderers.
namespace LiveTimelineProps
{
    struct Saved {
        StageLoader::Handle object{};
        int id{}, scene{};
        std::vector<ModelRendererMask::Entry> renderers;
    };
    inline std::unordered_map<std::string, Saved> saved;

    struct Root {
        StageLoader::Handle handle{};
        explicit Root(void* obj) : handle(obj && StageLoader::newHandle ? StageLoader::newHandle(obj, false) : nullptr) {}
        ~Root() { if (handle) StageLoader::freeHandle(handle); }
        Root(const Root&) = delete;
    };

    inline bool IsReference(int code) { return code == 0x12 || code == 0x15 || code == 0x1c || code == 0x1d; }

    inline const void* Method(void* klass, const char* name, bool takesIndex, int returns)
    {
        auto& md = StageLoader::metadata;
        if (!klass || !il2cpp_class_get_method_from_name || !md.flags || !md.result || !md.parameter ||
            !md.typeCode || !md.byRef) return nullptr;
        const auto* method = il2cpp_class_get_method_from_name(klass, name, takesIndex ? 1 : 0);
        uint32_t implementation{};
        void* result = method ? md.result(method) : nullptr;
        if (!result || md.byRef(result) || (md.flags(method, &implementation) & 0x10)) return nullptr;
        const int code = md.typeCode(result);
        if (returns ? code != returns : !IsReference(code)) return nullptr;
        if (takesIndex && !md.Matches(md.parameter(method, 0), {0x08})) return nullptr;
        return method;
    }

    inline int ArrayLength(void* collection)
    {
        if (!collection || !il2cpp_class_get_type || !StageLoader::arrayLength) return -1;
        void* type = il2cpp_class_get_type(g_faceRuntime.objectClass(collection));
        if (!type || StageLoader::metadata.typeCode(type) != 0x1d) return -1;
        const auto count = StageLoader::arrayLength(collection);
        return count <= 512 ? static_cast<int>(count) : -1;
    }

    inline int ListCount(void* collection)
    {
        const int length = ArrayLength(collection);
        if (length >= 0) return length;
        const auto count = collection ? Method(g_faceRuntime.objectClass(collection), "get_Count", false, 0x08) : nullptr;
        return StageLoader::ReadInt(count, collection, -1);
    }

    template<class Visit>
    void VisitList(void* list, Visit visit)
    {
        Root pinned(list);
        if (!pinned.handle) return;
        const int length = ArrayLength(list);
        if (length >= 0) {
            if (length == 0) return;
            using HeaderSize = size_t (*)();
            const auto header = reinterpret_cast<HeaderSize>(GetProcAddress(g_gameAssembly, "il2cpp_array_object_header_size"));
            if (!header || header() != 32) return;
            using ElementClass = void* (*)(void*);
            const auto elementClass = reinterpret_cast<ElementClass>(GetProcAddress(g_gameAssembly, "il2cpp_class_get_element_class"));
            void* element = elementClass ? elementClass(g_faceRuntime.objectClass(list)) : nullptr;
            void* itemType = element ? il2cpp_class_get_type(element) : nullptr;
            if (!itemType || !IsReference(StageLoader::metadata.typeCode(itemType))) return;
            auto items = reinterpret_cast<void**>(static_cast<char*>(list) + 32);
            for (int i = 0; i < length; ++i) if (items[i]) visit(items[i]);
            return;
        }
        void* klass = g_faceRuntime.objectClass(list);
        const auto* item = Method(klass, "get_Item", true, 0);
        const int count = ListCount(list);
        if (item && count >= 0 && count <= 512) {
            for (int i = 0; i < count; ++i) {
                void* args[]{&i};
                if (void* value = StageLoader::Invoke(item, list, args)) visit(value);
            }
            return;
        }
        const auto* enumerator = Method(klass, "System.Collections.IEnumerable.GetEnumerator", false, 0);
        if (!enumerator) enumerator = Method(klass, "GetEnumerator", false, 0);
        void* cursor = StageLoader::Invoke(enumerator, list);
        Root pinnedCursor(cursor);
        if (!pinnedCursor.handle) return;
        klass = g_faceRuntime.objectClass(cursor);
        const auto* next = Method(klass, "MoveNext", false, 0x02);
        const auto* current = Method(klass, "System.Collections.IEnumerator.get_Current", false, 0);
        if (!current) current = Method(klass, "get_Current", false, 0);
        if (!next || !current) return;
        for (int i = 0; i < 512 && StageLoader::ReadBool(next, cursor); ++i)
            if (void* value = StageLoader::Invoke(current, cursor)) visit(value);
    }

    inline bool Restore(Saved& item)
    {
        const auto target = LiveActorProps::Target();
        if (!target || !CameraBridge::OnUnityThread()) return false;
        void* object = target(item.object);
        if (CameraBridge::IsAlive(object) &&
            (StageLoader::ReadInt(StageLoader::instanceId, object) != item.id ||
             StageLoader::SceneOfFreshGo(object) != item.scene ||
             !ModelRendererMask::ApplyMeshMask(item.renderers, false))) return false;
        ModelRendererMask::Release(item.renderers);
        StageLoader::freeHandle(item.object);
        return true;
    }

    inline bool RestoreAll()
    {
        std::erase_if(saved, [](auto& item) { return Restore(item.second); });
        return saved.empty();
    }

    inline void Abandon() { RestoreAll(); }
    inline bool HasSaved() { return !saved.empty(); }

    inline void RestoreUnselected(const GakuMotion::Core& core)
    {
        std::erase_if(saved, [&](auto& item) {
            return !core.actorPropHidden.contains(item.first) && Restore(item.second);
        });
    }

    inline bool IsBelow(void* node, void* ancestor)
    {
        for (int depth = 0; node && depth < 64; ++depth, node = g_getParentNative(node))
            if (node == ancestor) return true;
        return false;
    }

    inline const std::array<void*, 2>& MeshClasses()
    {
        static const std::array<void*, 2> classes{
            FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "MeshRenderer"),
            FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "SkinnedMeshRenderer")};
        return classes;
    }

    inline bool IsMeshRenderer(void* renderer)
    {
        const auto klass = g_faceRuntime.objectClass(renderer);
        const auto& classes = MeshClasses();
        return klass && std::find(classes.begin(), classes.end(), klass) != classes.end();
    }

    inline bool HasMesh(void* transform)
    {
        if (!il2cpp_class_get_type || !il2cpp_type_get_object) return false;
        const auto& md = StageLoader::metadata;
        void* component = StageLoader::componentClass;
        static const auto* get = md.Find(component, "GetComponentInChildren", false, {0x12, component}, {{0x12}, {0x02}});
        if (!get) return false;
        for (void* klass : MeshClasses()) {
            if (!klass) continue;
            void* type = il2cpp_class_get_type(klass);
            if (!type) continue;
            void* managedType = il2cpp_type_get_object(type);
            bool includeInactive = true;
            void* args[]{managedType, &includeInactive};
            if (managedType && StageLoader::Invoke(get, transform, args)) return true;
        }
        return false;
    }

    inline void Hide(GakuMotion::Core& core, const GakuMotion::GuiActorProp& prop, void* object, void* transform, int scene)
    {
        const auto key = GakuMotion::ActorPropKey(prop);
        if (!core.actorPropHidden.contains(key)) return;
        auto found = saved.find(key);
        if (found == saved.end()) {
            Saved state{StageLoader::newHandle(object, false), prop.instanceId, scene};
            if (state.object && ModelRendererMask::Capture(transform, state.renderers, IsMeshRenderer))
                found = saved.emplace(key, std::move(state)).first;
            else if (state.object) StageLoader::freeHandle(state.object);
        }
        if (found != saved.end() && LiveActorProps::Target()(found->second.object) == object &&
            found->second.scene == scene && ModelRendererMask::ApplyMeshMask(found->second.renderers, true)) return;
        core.actorPropHidden.erase(key);
        core.ensembleNotice = "该时间轴道具的网格显隐未确认。";
        if (g_hostContext.logWarn) g_hostContext.logWarn("[GakuMotion][LiveTimelineProp] hide unconfirmed: %s", prop.propName.c_str());
    }

    inline void Scan(GakuMotion::Core& core, const std::vector<void*>& actors, bool canWrite)
    {
        if (!core.liveViewingConfirmed || !g_sceneOwner.Valid() ||
            g_sceneOwner.role != GakuMotion::injection::OwnerRole::Live || !g_getParentNative ||
            !g_faceRuntime.objectClass || !StageLoader::metadata.typeCode || !StageLoader::assignable ||
            !StageLoader::goClass || !StageLoader::transformClass || !StageLoader::sceneLoaded) return;
        static void* presenterClass = FindClass("Assembly-CSharp.dll", "Campus.Live", "Live3DPresenter");
        static void* controllerClass = FindClass("campus-submodule.Runtime.dll", "Campus.Common.Live", "CampusLiveTimelineController");
        if (!controllerClass) controllerClass = FindClass("Assembly-CSharp.dll", "Campus.Common.Live", "CampusLiveTimelineController");
        const GakuMotion::live_takeover::RuntimeAccess access{
            g_faceRuntime.objectClass, g_faceRuntime.findField, g_faceRuntime.fieldType, g_faceRuntime.fieldFlags,
            g_faceRuntime.typeCode, StageLoader::metadata.classFromType, g_faceRuntime.readField,
            g_faceRuntime.invoke, g_safeBones.unbox};
        auto* player = GakuMotion::live_takeover::SceneField(g_liveApi, access,
            reinterpret_cast<void*>(g_sceneOwner.presenterIdentity), "_livePlayer", g_liveApi.playerClass);
        auto* live = GakuMotion::live_audio::Field(access, player, g_liveApi.playerClass, "_3dLive", presenterClass);
        if (!live) return;
        auto* controller = GakuMotion::live_audio::Field(access, live, presenterClass, "_controller", controllerClass);
        const auto* props = Method(presenterClass, "GetTimelineProps", true, 0);
        const auto* constraints = Method(controllerClass, "GetFieldConstraintTargets", true, 0);
        auto* virtualActors = StageLoader::Invoke(Method(presenterClass, "get_VirtualActors", false, 0), live);
        const int slots = ListCount(virtualActors);
        const int count = std::clamp(slots >= 0 ? slots : static_cast<int>(actors.size()), 0, 12);

        std::vector<void*> modelRoots;
        auto add = [&](void* item, int slot) {
            void* klass = g_faceRuntime.objectClass(item);
            if (!klass || (klass != StageLoader::goClass &&
                !StageLoader::assignable(StageLoader::componentClass, klass))) return;
            if (!CameraBridge::IsAlive(item)) return;
            void* object = klass == StageLoader::goClass ? item :
                StageLoader::Invoke(StageLoader::componentGo, item);
            void* transform = object ? StageLoader::Invoke(StageLoader::goTransform, object) : nullptr;
            if (!transform || std::any_of(actors.begin(), actors.end(), [&](void* actor) {
                return actor && (IsBelow(transform, actor) || IsBelow(actor, transform));
            }) || std::any_of(modelRoots.begin(), modelRoots.end(), [&](void* root) {
                return IsBelow(transform, root) || IsBelow(root, transform);
            })) return;
            int scene = StageLoader::SceneOfFreshGo(object);
            void* sceneArgs[]{&scene};
            if (!scene || !StageLoader::ReadBool(StageLoader::sceneLoaded, nullptr, false, sceneArgs)) return;
            const int id = StageLoader::ReadInt(StageLoader::instanceId, object);
            if (!id || std::any_of(core.actorProps.begin(), core.actorProps.end(),
                [&](const auto& prop) { return prop.instanceId == id; }) || !HasMesh(transform)) return;
            const auto name = HelperGetTransformName(transform);
            bool active{};
            if (name.empty() || !LiveActorProps::ReadActive(object, active)) return;
            GakuMotion::GuiActorProp prop{0, "Timeline", name, "时间轴道具 · 角色位 " + std::to_string(slot + 1),
                id, active, canWrite, false, true};
            if (canWrite) Hide(core, prop, object, transform, scene);
            modelRoots.push_back(transform);
            core.actorProps.push_back(std::move(prop));
        };
        for (int slot = 0; slot < count; ++slot) {
            void* args[]{&slot};
            VisitList(StageLoader::Invoke(props, live, args), [&](void* item) { add(item, slot); });
        }
        for (int slot = 0; controller && slot < count; ++slot) {
            void* args[]{&slot};
            VisitList(StageLoader::Invoke(constraints, controller, args), [&](void* item) { add(item, slot); });
        }
        if (core.actorPropScanRequested && g_hostContext.logInfo) {
            const auto found = std::count_if(core.actorProps.begin(), core.actorProps.end(), [](const auto& prop) { return prop.timeline; });
            g_hostContext.logInfo("[GakuMotion][LiveTimelineProp] slots=%d timeline_props=%d constraint_targets=%d found=%zu",
                count, int(props != nullptr), int(controller && constraints), static_cast<size_t>(found));
        }
    }
}
