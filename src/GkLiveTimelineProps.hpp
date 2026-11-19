#pragma once

// Discover models driven by Live slots or Timeline tracks and mask their renderers.
namespace LiveTimelineProps
{
    struct Saved {
        StageLoader::Handle object{};
        int id{}, scene{};
        std::vector<ModelRendererMask::Entry> renderers;
    };
    inline std::unordered_map<std::string, Saved> saved;
    inline constexpr const char* penlightPrefab = "mdl_prp_penlightset-env-00_penlightset";
    inline constexpr const char* penlightPrefabClone = "mdl_prp_penlightset-env-00_penlightset(Clone)";
    inline constexpr const char* penlightMeshes[]{"Geo_Penlightleft", "Geo_Penlightright"};
    struct TrackModel {
        StageLoader::Handle object{};
        std::string director;
    };
    inline GakuMotion::injection::OwnerSnapshot knownOwner;
    inline int knownActiveScene{};
    inline std::vector<StageLoader::Handle> knownObjects;
    inline std::vector<TrackModel> trackModels;

    inline void ClearKnownObjects()
    {
        for (auto handle : knownObjects) StageLoader::freeHandle(handle);
        for (const auto& model : trackModels) StageLoader::freeHandle(model.object);
        knownObjects.clear();
        trackModels.clear();
    }

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
            static const auto* get = [] {
                void* array = FindClass("mscorlib.dll", "System", "Array");
                if (!array) array = FindClass("System.Private.CoreLib.dll", "System", "Array");
                return StageLoader::metadata.Find(array, "GetValue", false, {0x1c}, {{0x08}});
            }();
            if (get) {
                for (int i = 0; i < length; ++i) {
                    void* args[]{&i};
                    if (void* value = StageLoader::Invoke(get, list, args)) visit(value);
                }
                return;
            }
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
             StageLoader::SceneOfFreshGo(object) != item.scene)) return false;
        if (!ModelRendererMask::ApplyMeshMask(item.renderers, false)) return false;
        if (g_hostContext.logInfo)
            g_hostContext.logInfo("[GakuMotion][LiveTimelineProp] restored id=%d meshes=%zu", item.id, item.renderers.size());
        ModelRendererMask::Release(item.renderers);
        StageLoader::freeHandle(item.object);
        return true;
    }

    inline bool RestoreAll()
    {
        std::erase_if(saved, [](auto& item) { return Restore(item.second); });
        return saved.empty();
    }

    inline void Abandon() {
        RestoreAll();
        ClearKnownObjects();
        knownOwner = {};
        knownActiveScene = 0;
    }
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

    inline void* GameObjectOf(void* value)
    {
        if (!value) return nullptr;
        auto* klass = g_faceRuntime.objectClass(value);
        if (!klass || (klass != StageLoader::goClass && !StageLoader::assignable(StageLoader::componentClass, klass)) ||
            !CameraBridge::IsAlive(value)) return nullptr;
        return klass == StageLoader::goClass ? value : StageLoader::Invoke(StageLoader::componentGo, value);
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

    inline bool IsPenlightMesh(const std::string& name)
    {
        return std::any_of(std::begin(penlightMeshes), std::end(penlightMeshes),
            [&](const char* known) { return name == known; });
    }

    // Keep the verified fallback for the handheld prefab when a slot omits it.
    inline void FindHandheldModels()
    {
        const auto& md = StageLoader::metadata;
        const auto remember = [](void* go, auto& list) {
            if (CameraBridge::IsAlive(go))
                if (auto handle = StageLoader::newHandle(go, false)) list.push_back(handle);
        };
        static const auto* find = md.Find(StageLoader::goClass, "Find", true, {0x12, StageLoader::goClass}, {{0x0e}});
        const auto findObject = [](const char* name) -> void* {
            if (!find || !StageLoader::stringNew) return nullptr;
            void* text = StageLoader::stringNew(name);
            void* args[]{text};
            return text ? StageLoader::Invoke(find, nullptr, args) : nullptr;
        };
        for (const char* name : {penlightPrefabClone, penlightPrefab}) {
            void* go = findObject(name);
            void* transform = CameraBridge::IsAlive(go) ? StageLoader::Invoke(StageLoader::goTransform, go) : nullptr;
            std::vector<ModelRendererMask::Entry> renderers;
            if (transform && ModelRendererMask::Capture(transform, renderers, IsMeshRenderer) && renderers.size() >= 2)
                remember(go, knownObjects);
            ModelRendererMask::Release(renderers);
        }
        if (!knownObjects.empty()) return;
        if (!il2cpp_class_get_type || !il2cpp_type_get_object || !StageLoader::findAll ||
            !StageLoader::arrayLength || !g_getNameNative) return;
        static void* filter = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "MeshFilter");
        static void* mesh = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Mesh");
        static const auto* sharedMesh = md.Find(filter, "get_sharedMesh", false, {0x12, mesh}, {});
        if (!sharedMesh) return;
        void* type = il2cpp_class_get_type(filter);
        void* managedType = type ? il2cpp_type_get_object(type) : nullptr;
        bool includeInactive = true;
        void* args[]{managedType, &includeInactive};
        void* filters = managedType ? StageLoader::Invoke(StageLoader::findAll, nullptr, args) : nullptr;
        Root pinned(filters);
        if (!pinned.handle) return;
        const auto count = StageLoader::arrayLength(filters);
        if (count > 12000) return;
        // StageLoader::findAll is the same typed Unity finder used for scene objects.
        auto items = reinterpret_cast<void**>(static_cast<char*>(filters) + 32);
        for (uintptr_t i = 0; i < count && knownObjects.size() < 8; ++i) {
            void* component = items[i];
            if (!component) continue;
            const auto name = Il2CppStringToUtf8(g_getNameNative(component));
            if (!IsPenlightMesh(name)) continue;
            void* asset = StageLoader::Invoke(sharedMesh, component);
            if (!asset || Il2CppStringToUtf8(g_getNameNative(asset)) != name) continue;
            remember(StageLoader::Invoke(StageLoader::componentGo, component), knownObjects);
        }
    }

    // Control clips store an ExposedReference<GameObject> resolved by their own director.
    inline void VisitControlTargets(void* director, void* track, const auto& visit)
    {
        static void* trackClass = FindClass("Unity.Timeline.dll", "UnityEngine.Timeline", "TrackAsset");
        static void* clipClass = FindClass("Unity.Timeline.dll", "UnityEngine.Timeline", "TimelineClip");
        static void* assetClass = FindClass("Unity.Timeline.dll", "UnityEngine.Timeline", "ControlPlayableAsset");
        static void* tableClass = [] {
            void* type = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "IExposedPropertyTable");
            return type ? type : FindClass("UnityEngine.DirectorModule.dll", "UnityEngine", "IExposedPropertyTable");
        }();
        static const auto* getClips = Method(trackClass, "GetClips", false, 0);
        static const auto* getAsset = Method(clipClass, "get_asset", false, 0);
        static const auto boxField = reinterpret_cast<void*(*)(void*, void*)>(
            GetProcAddress(g_gameAssembly, "il2cpp_field_get_value_object"));
        static void* source = assetClass && g_faceRuntime.findField ? g_faceRuntime.findField(assetClass, "sourceGameObject") : nullptr;
        const auto& md = StageLoader::metadata;
        if (!source || !boxField || !g_safeBones.unbox || !getAsset || !tableClass || !md.classFromType ||
            !g_faceRuntime.fieldType) return;
        void* sourceType = g_faceRuntime.fieldType(source);
        if (!sourceType || md.typeCode(sourceType) != 0x15) return;
        void* sourceClass = md.classFromType(sourceType);
        if (!sourceClass) return;
        const auto* resolve = il2cpp_class_get_method_from_name(sourceClass, "Resolve", 1);
        uint32_t implementation{};
        if (!resolve || (md.flags(resolve, &implementation) & 0x10) ||
            !md.Matches(md.parameter(resolve, 0), {0x12, tableClass}) ||
            !md.Matches(md.result(resolve), {0x12, StageLoader::goClass})) return;
        VisitList(StageLoader::Invoke(getClips, track), [&](void* clip) {
            if (g_faceRuntime.objectClass(clip) != clipClass) return;
            void* asset = StageLoader::Invoke(getAsset, clip);
            if (!asset || g_faceRuntime.objectClass(asset) != assetClass) return;
            void* boxed = boxField(source, asset);
            Root rooted(boxed);
            if (!rooted.handle || g_faceRuntime.objectClass(boxed) != sourceClass) return;
            void* data = g_safeBones.unbox(boxed);
            void* args[]{director};
            if (data) visit(StageLoader::Invoke(resolve, data, args));
        });
    }

    inline void FindTrackModels(void* controller, const GakuMotion::live_takeover::RuntimeAccess& access,
        const std::vector<void*>& actors)
    {
        const auto start = std::chrono::steady_clock::now();
        static void* timeline = FindClass("Unity.Timeline.dll", "UnityEngine.Timeline", "TimelineAsset");
        static void* animation = FindClass("Unity.Timeline.dll", "UnityEngine.Timeline", "AnimationTrack");
        static void* activation = FindClass("Unity.Timeline.dll", "UnityEngine.Timeline", "ActivationTrack");
        static void* control = FindClass("Unity.Timeline.dll", "UnityEngine.Timeline", "ControlTrack");
        static void* unityObject = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Object");
        static const auto* tracks = Method(timeline, "GetOutputTracks", false, 0);
        static const auto* asset = Method(g_playableDirectorKlass, "get_playableAsset", false, 0);
        static const auto* binding = unityObject ? StageLoader::metadata.Find(g_playableDirectorKlass, "GetGenericBinding", false,
            {0x12, unityObject}, {{0x12, unityObject}}) : nullptr;
        if (!tracks || !asset || !binding || !controller || !g_getNameNative ||
            !StageLoader::newHandle || !StageLoader::freeHandle || !il2cpp_class_get_parent) {
            if (g_hostContext.logWarn)
                g_hostContext.logWarn("[GakuMotion][LiveTimelineProp] director metadata asset=%d tracks=%d binding=%d",
                    asset != nullptr, tracks != nullptr, binding != nullptr);
            return;
        }

        // The controller owns the additive Timeline scene as well as the master director.
        const auto field = [&](const char* name, int code) -> void* {
            for (void* klass = access.objectClass(controller); klass; klass = il2cpp_class_get_parent(klass)) {
                void* info = access.findField(klass, name);
                if (!info) continue;
                void* type = access.fieldType(info);
                if (!type || (access.fieldFlags(info) & 0x10) || access.typeCode(type) != code ||
                    (code == 0x12 && access.classFromType(type) != g_playableDirectorKlass)) return nullptr;
                void* value = nullptr;
                access.readField(controller, info, &value);
                return value;
            }
            return nullptr;
        };
        const auto loaded = [](void* go) {
            int scene = StageLoader::SceneOfFreshGo(go);
            void* sceneArgs[]{&scene};
            return scene && StageLoader::ReadBool(StageLoader::sceneLoaded, nullptr, false, sceneArgs);
        };
        const auto protectedBranch = [](const std::string& name) {
            return name == "Mob" || name == "Crowd" || name == "AudiencePenlight" ||
                GakuMotion::StageManager::IsActorOrProtectedNode(name);
        };
        std::unordered_set<int> seen, seenDirectors;
        unsigned directorCount = 0, trackCount = 0;
        const auto visitDirector = [&](void* director) {
            if (!director || g_faceRuntime.objectClass(director) != g_playableDirectorKlass) return;
            void* go = GameObjectOf(director);
            if (!go || !loaded(go)) return;
            const int id = StageLoader::ReadInt(StageLoader::instanceId, director);
            if (!id || !seenDirectors.insert(id).second) return;
            void* playable = StageLoader::Invoke(asset, director);
            Root rooted(playable);
            if (!rooted.handle || g_faceRuntime.objectClass(playable) != timeline) return;
            const auto label = Il2CppStringToUtf8(g_getNameNative(go));
            ++directorCount;
            if (protectedBranch(label)) return;
            const auto remember = [&](void* value) {
                void* model = GameObjectOf(value);
                if (!model || !loaded(model)) return;
                const int modelId = StageLoader::ReadInt(StageLoader::instanceId, model);
                if (!modelId || seen.contains(modelId)) return;
                void* transform = StageLoader::Invoke(StageLoader::goTransform, model);
                if (!transform || std::any_of(actors.begin(), actors.end(), [&](void* actor) {
                    return actor && (IsBelow(actor, transform) || IsBelow(transform, actor));
                })) return;
                const auto name = HelperGetTransformName(transform);
                void* node = transform;
                for (int depth = 0; node && depth < 64; ++depth, node = g_getParentNative(node))
                    if (protectedBranch(node == transform ? name : HelperGetTransformName(node))) return;
                if (node || !HasMesh(transform)) return;
                if (auto handle = StageLoader::newHandle(model, false)) {
                    seen.insert(modelId);
                    trackModels.push_back({handle, label});
                    if (g_hostContext.logInfo)
                        g_hostContext.logInfo("[GakuMotion][LiveTimelineProp] track director=%s model=%s id=%d",
                            label.c_str(), name.c_str(), modelId);
                }
            };
            VisitList(StageLoader::Invoke(tracks, playable), [&](void* track) {
                ++trackCount;
                void* klass = g_faceRuntime.objectClass(track);
                if (klass == animation || klass == activation) {
                    void* bindingArgs[]{track};
                    remember(StageLoader::Invoke(binding, director, bindingArgs));
                }
                if (klass == control) VisitControlTargets(director, track, remember);
            });
        };
        visitDirector(field("_master", 0x12));
        VisitList(field("_directors", 0x15), visitDirector);
        VisitList(field("_controlDirectorsCache", 0x1d), visitDirector);
        if (g_hostContext.logInfo)
            g_hostContext.logInfo("[GakuMotion][LiveTimelineProp] directors=%u tracks=%u track_models=%zu discover_ms=%.2f",
                directorCount, trackCount, trackModels.size(),
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }

    inline void Hide(GakuMotion::Core& core, const GakuMotion::GuiActorProp& prop, void* object, void* transform, int scene)
    {
        const auto key = GakuMotion::ActorPropKey(prop);
        if (!core.actorPropHidden.contains(key)) return;
        auto found = saved.find(key);
        const bool newMask = found == saved.end();
        if (newMask) {
            Saved state{StageLoader::newHandle(object, false), prop.instanceId, scene};
            if (state.object && ModelRendererMask::Capture(transform, state.renderers, IsMeshRenderer))
                found = saved.emplace(key, std::move(state)).first;
            else {
                ModelRendererMask::Release(state.renderers);
                if (state.object) StageLoader::freeHandle(state.object);
            }
        }
        if (found != saved.end() && LiveActorProps::Target()(found->second.object) == object &&
            found->second.scene == scene && ModelRendererMask::ApplyMeshMask(found->second.renderers, true)) {
            if (newMask && g_hostContext.logInfo)
                g_hostContext.logInfo("[GakuMotion][LiveTimelineProp] hidden model=%s id=%d meshes=%zu",
                    prop.propName.c_str(), prop.instanceId, found->second.renderers.size());
            return;
        }
        core.actorPropHidden.erase(key);
        core.ensembleNotice = "该时间轴道具的网格显隐未确认。";
        if (g_hostContext.logWarn) g_hostContext.logWarn("[GakuMotion][LiveTimelineProp] hide unconfirmed: %s (%s)",
            prop.propName.c_str(), ModelRendererMask::lastFailure);
    }

    inline void Scan(GakuMotion::Core& core, const std::vector<void*>& actors, bool canWrite)
    {
        if (!core.liveViewingConfirmed || !g_sceneOwner.Valid() ||
            g_sceneOwner.role != GakuMotion::injection::OwnerRole::Live || !g_getParentNative ||
            !g_faceRuntime.objectClass || !StageLoader::metadata.typeCode || !StageLoader::assignable || !StageLoader::componentClass ||
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

        const auto target = LiveActorProps::Target();
        const int activeScene = g_verifiedActiveSceneHandle;
        const bool replacedModel = target && std::any_of(trackModels.begin(), trackModels.end(),
            [&](const auto& model) { return !CameraBridge::IsAlive(target(model.object)); });
        if (target && (!knownOwner.SameOwner(g_sceneOwner) || knownActiveScene != activeScene ||
            core.timelinePropDiscoveryRequested || replacedModel)) {
            const bool requested = core.timelinePropDiscoveryRequested;
            ClearKnownObjects();
            knownOwner = g_sceneOwner;
            knownActiveScene = activeScene;
            core.timelinePropDiscoveryRequested = false;
            int scene = activeScene;
            void* args[]{&scene};
            const auto sceneName = scene
                ? Il2CppStringToUtf8(StageLoader::Invoke(StageLoader::sceneName, nullptr, args)) : "";
            if (requested || sceneName.starts_with("env_3d_live_fktn004-")) FindHandheldModels();
            FindTrackModels(controller, access, actors);
        }

        std::vector<void*> modelRoots;
        unsigned readProps{}, readConstraints{};
        auto add = [&](void* item, const std::string& label, bool meshVerified = false, bool handheld = false) {
            void* object = GameObjectOf(item);
            void* transform = object ? StageLoader::Invoke(StageLoader::goTransform, object) : nullptr;
            if (!transform || std::any_of(actors.begin(), actors.end(), [&](void* actor) {
                return actor && (IsBelow(actor, transform) || (!handheld && IsBelow(transform, actor)));
            }) || std::any_of(modelRoots.begin(), modelRoots.end(), [&](void* root) {
                return IsBelow(transform, root) || IsBelow(root, transform);
            })) return;
            int scene = StageLoader::SceneOfFreshGo(object);
            void* sceneArgs[]{&scene};
            if (!scene || !StageLoader::ReadBool(StageLoader::sceneLoaded, nullptr, false, sceneArgs)) return;
            const int id = StageLoader::ReadInt(StageLoader::instanceId, object);
            if (!id || std::any_of(core.actorProps.begin(), core.actorProps.end(),
                [&](const auto& prop) { return prop.instanceId == id; }) || (!meshVerified && !HasMesh(transform))) return;
            const auto name = HelperGetTransformName(transform);
            bool active{};
            if (name.empty() || !LiveActorProps::ReadActive(object, active)) return;
            GakuMotion::GuiActorProp prop{0, "Timeline", name, label, id, active, canWrite, false, true};
            if (canWrite) Hide(core, prop, object, transform, scene);
            modelRoots.push_back(transform);
            core.actorProps.push_back(std::move(prop));
        };
        if (target) for (auto handle : knownObjects)
            add(target(handle), "手持荧光棒", true, true);
        for (int slot = 0; slot < count; ++slot) {
            const auto label = "角色位 " + std::to_string(slot + 1);
            void* args[]{&slot};
            VisitList(StageLoader::Invoke(props, live, args), [&](void* item) { ++readProps; add(item, label); });
        }
        for (int slot = 0; controller && slot < count; ++slot) {
            const auto label = "角色位 " + std::to_string(slot + 1);
            void* args[]{&slot};
            VisitList(StageLoader::Invoke(constraints, controller, args), [&](void* item) { ++readConstraints; add(item, label); });
        }
        if (target) for (const auto& model : trackModels)
            add(target(model.object), "轨道 · " + model.director, true);
        if (core.actorPropScanRequested && g_hostContext.logInfo) {
            g_hostContext.logInfo("[GakuMotion][LiveTimelineProp] slots=%d props=%u constraints=%u handheld=%zu tracks=%zu found=%zu",
                count, readProps, readConstraints, knownObjects.size(), trackModels.size(), core.actorProps.size());
        }
    }
}
