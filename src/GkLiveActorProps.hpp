#pragma once
// Discover attached props within actors in the verified scene.
namespace LiveTimelineProps
{
    bool RestoreAll();
    void Abandon();
    bool HasSaved();
    void Scan(GakuMotion::Core& core, const std::vector<void*>& actorTransforms, bool canWrite);
    void RestoreUnselected(const GakuMotion::Core& core);
}
namespace LiveActorProps
{
    struct TargetState {
        StageLoader::Handle handle{};
        int instanceId{};
        bool original{};
    };
    struct Saved {
        GakuMotion::injection::OwnerSnapshot owner;
        uint64_t token{}, epoch{};
        std::string root, prop;
        StageLoader::Handle actor{}, attachment{};
        TargetState object, geometry;
    };
    inline std::unordered_map<std::string, Saved> saved;
    using TargetFn = void*(*)(StageLoader::Handle);
    inline TargetFn Target() { return reinterpret_cast<TargetFn>(GetProcAddress(g_gameAssembly,"il2cpp_gchandle_get_target")); }

    inline void Release(const Saved& state) {
        for (auto handle : {state.actor, state.attachment, state.object.handle, state.geometry.handle})
            if (handle) StageLoader::freeHandle(handle);
    }
    inline void AbandonActor(uint64_t token) {
        std::erase_if(saved, [&](auto& item) {
            if (item.second.token != token) return false;
            Release(item.second);
            return true;
        });
    }
    inline void Abandon() {
        for (auto& item : saved) Release(item.second);
        saved.clear();
        LiveTimelineProps::Abandon();
    }
    inline bool HasSaved() { return !saved.empty() || LiveTimelineProps::HasSaved(); }

    inline bool ReadActive(void* go, bool& active) {
        if(!go || !StageLoader::activeSelf || !g_faceRuntime.invoke || !g_safeBones.unbox) return false;
        void* ex=nullptr; void* boxed=g_faceRuntime.invoke(StageLoader::activeSelf,go,nullptr,&ex);
        void* raw=boxed&&!ex?g_safeBones.unbox(boxed):nullptr; if(!raw)return false; active=*static_cast<bool*>(raw);return true;
    }
    inline bool SetActive(void* go, bool active) {
        if(!go || !StageLoader::setActive || !g_faceRuntime.invoke)return false;
        void* args[]{&active}; void* ex=nullptr; g_faceRuntime.invoke(StageLoader::setActive,go,args,&ex);
        bool after{}; return !ex && ReadActive(go,after) && after==active;
    }
    inline void* UniqueChild(void* parent, const std::string& name) {
        if(!parent || !g_getChildCountNative || !g_getChildNative || !g_getNameNative)return nullptr;
        int count=HelperGetChildCount(parent); if(count<0||count>512)return nullptr;
        void* found=nullptr;
        for(int i=0;i<count;++i) { void* child=HelperGetChild(parent,i); if(child && HelperGetTransformName(child)==name) { if(found)return nullptr; found=child; } }
        return found;
    }

    inline std::vector<void*> Children(void* transform) {
        const int count = HelperGetChildCount(transform);
        std::vector<void*> children;
        if (count < 0 || count > 512) return children;
        children.reserve(count);
        for (int i = 0; i < count; ++i)
            if (void* child = HelperGetChild(transform, i)) children.push_back(child);
        return children;
    }
    inline void* SceneObject(void* transform, int scene) {
        void* go = transform ? g_getGameObjectNative(transform, nullptr) : nullptr;
        return go && CameraBridge::IsAlive(go) && StageLoader::SceneOfFreshGo(go) == scene ? go : nullptr;
    }
    inline void* ActorTransform(void* actor, int scene) {
        if (!actor || !g_getComponentTransformNative) return nullptr;
        void* transform = g_getComponentTransformNative(actor);
        return SceneObject(transform, scene) ? transform : nullptr;
    }
    inline bool RestoreTarget(const TargetState& state, void* object, TargetFn target) {
        if (!state.handle) return true;
        void* previous = target(state.handle);
        if (!previous || !CameraBridge::IsAlive(previous)) return true;
        if (previous != object || StageLoader::ReadInt(StageLoader::instanceId, object) != state.instanceId)
            return false;
        bool active{};
        return ReadActive(object, active) && (active == state.original || SetActive(object, state.original));
    }
    inline bool RestoreOne(GakuMotion::Core& core, const Saved& state) {
        const auto target = Target();
        if (!target) return false;
        void* actor = target(state.actor);
        if (!actor || !CameraBridge::IsAlive(actor)) return true;
        if (!g_sceneOwner.SameOwner(state.owner) || core.sceneActorRegistry.Resolve(state.token, state.epoch) != actor)
            return false;
        const int scene = state.owner.sceneHandle;
        void* prop = target(state.object.handle);
        if (!prop || !CameraBridge::IsAlive(prop))
            return !state.geometry.handle || !CameraBridge::IsAlive(target(state.geometry.handle));
        if (!g_getParentNative) return false;
        void* propTransform = StageLoader::Invoke(StageLoader::goTransform, prop);
        void* root = propTransform ? g_getParentNative(propTransform) : nullptr;
        void* parent = state.root.empty() ? root : root ? g_getParentNative(root) : nullptr;
        void* actorTransform = ActorTransform(actor, scene);
        if (!actorTransform || !root || target(state.attachment) != root ||
            (!state.root.empty() && UniqueChild(parent, state.root) != root) ||
            UniqueChild(root, state.prop) != propTransform || StageLoader::SceneOfFreshGo(prop) != scene)
            return false;
        void* ancestor = parent;
        for (int depth = 0; ancestor && ancestor != actorTransform &&
            depth < GakuMotion::live_actor_props::MaxPropDepth; ++depth)
            ancestor = g_getParentNative(ancestor);
        if (ancestor != actorTransform) return false;
        const bool propRestored = RestoreTarget(state.object, prop, target);
        if (!state.geometry.handle) return propRestored;
        // The suitcase mesh is a sibling. Restore each node's own activeSelf.
        void* geometry = SceneObject(UniqueChild(root, "Geo_Hknesuitcase"), scene);
        const bool geometryRestored = RestoreTarget(state.geometry, geometry, target);
        return propRestored && geometryRestored;
    }
    inline bool RestoreAll(GakuMotion::Core& core) {
        std::erase_if(saved, [&](auto& item) {
            if (!RestoreOne(core, item.second)) return false;
            Release(item.second);
            return true;
        });
        const bool timelineRestored = LiveTimelineProps::RestoreAll();
        return saved.empty() && timelineRestored;
    }
    inline bool SaveTarget(void* object, TargetState& state) {
        state.instanceId = StageLoader::ReadInt(StageLoader::instanceId, object);
        if (!state.instanceId || !ReadActive(object, state.original)) return false;
        state.handle = StageLoader::newHandle(object, false);
        return state.handle != 0;
    }
    inline bool HideTarget(void* object, const TargetState& state, TargetFn target) {
        bool active{};
        return target(state.handle) == object &&
            StageLoader::ReadInt(StageLoader::instanceId, object) == state.instanceId &&
            ReadActive(object, active) && (!active || SetActive(object, false));
    }
    inline void Hide(GakuMotion::Core& core, void* actor, void* attachment, const GakuMotion::GuiActorProp& prop,
        void* object, void* geometry, const std::string& key, TargetFn target) {
        auto found = saved.find(key);
        if (found == saved.end()) {
            Saved state{g_sceneOwner, prop.actorToken, core.sceneActorRegistry.Epoch(), prop.rootName, prop.propName};
            state.actor = StageLoader::newHandle(actor, false);
            state.attachment = StageLoader::newHandle(attachment, false);
            if (!state.actor || !state.attachment || !SaveTarget(object, state.object) ||
                (geometry && !SaveTarget(geometry, state.geometry))) {
                Release(state);
                core.actorPropHidden.erase(key);
                return;
            }
            found = saved.emplace(key, std::move(state)).first;
        }
        const auto& state = found->second;
        if (target(state.actor) == actor && target(state.attachment) == attachment && state.owner.SameOwner(g_sceneOwner) &&
            state.epoch == core.sceneActorRegistry.Epoch() &&
            HideTarget(object, state.object, target) &&
            (!state.geometry.handle || (geometry && HideTarget(geometry, state.geometry, target)))) return;

        core.actorPropHidden.erase(key);
        if (g_hostContext.logWarn)
            g_hostContext.logWarn("[GakuMotion][LiveProp] hide unconfirmed for %s", prop.propName.c_str());
    }

    inline void Scan(GakuMotion::Core& core, const std::vector<GakuMotion::SceneActorObservation>& actors, bool canWrite) {
        core.actorProps.clear();
        if (!g_getGameObjectNative || !g_getComponentTransformNative || !StageLoader::goTransform || !StageLoader::instanceId ||
            !g_getChildCountNative || !g_getChildNative || !g_getNameNative) return;
        const auto target = Target();
        canWrite = canWrite && target && StageLoader::newHandle && StageLoader::freeHandle &&
            StageLoader::setActive && StageLoader::activeSelf && g_getParentNative;
        const int scene = g_sceneOwner.sceneHandle;
        std::vector<void*> actorTransforms;
        actorTransforms.reserve(actors.size());
        for (const auto& actor : actors)
            actorTransforms.push_back(CameraBridge::IsAlive(actor.actor) ? ActorTransform(actor.actor, scene) : nullptr);
        const auto isOtherActor = [&](void* node) {
            return std::find(actorTransforms.begin(), actorTransforms.end(), node) != actorTransforms.end();
        };
        for (size_t index = 0; index < actors.size(); ++index) {
            const auto& actor = actors[index];
            void* transform = actorTransforms[index];
            if (!transform) continue;
            const auto* entry = core.sceneActorRegistry.Find(actor.actor);
            const auto token = entry && entry->instanceId == actor.instanceId ? entry->token : 0;
            if (std::find(actorTransforms.begin(), actorTransforms.begin() + index, transform) !=
                actorTransforms.begin() + index) continue;
            const auto name = !actor.displayName.empty() ? actor.displayName : actor.character.empty() ?
                "未识别角色" : GakuMotion::FormatIdolDisplayName(actor.character.c_str());
            const auto label = name + " · " + std::to_string(index + 1);
            for (const auto& match : GakuMotion::live_actor_props::ScanActorProps(
                transform, Children, HelperGetTransformName, isOtherActor)) {
                const auto& [root, propTransform, rootName, propName] = match;
                void* object = SceneObject(propTransform, scene);
                if (!object) continue;
                const int id = StageLoader::ReadInt(StageLoader::instanceId, object);
                bool active{};
                if (!id || !ReadActive(object, active)) continue;

                const bool isSuitcase = GakuMotion::live_actor_props::PropSuffix(propName) == "Hknesuitcase";
                void* geometry = isSuitcase ? SceneObject(UniqueChild(root, "Geo_Hknesuitcase"), scene) : nullptr;
                GakuMotion::GuiActorProp prop{token, rootName, propName, label, id, active,
                    token && canWrite && (!isSuitcase || geometry)};
                if (prop.controllable) {
                    const auto key = GakuMotion::ActorPropKey(prop);
                    if (core.actorPropHidden.contains(key))
                        Hide(core, actor.actor, root, prop, object, geometry, key, target);
                }
                core.actorProps.push_back(std::move(prop));
            }
        }
        LiveTimelineProps::Scan(core, actorTransforms, canWrite);
        if (!canWrite) return;
        std::erase_if(core.actorPropHidden, [&](const auto& key) {
            return std::none_of(core.actorProps.begin(), core.actorProps.end(), [&](const auto& prop) {
                return prop.controllable && GakuMotion::ActorPropKey(prop) == key;
            });
        });
        std::erase_if(saved, [&](auto& item) {
            if (core.actorPropHidden.contains(item.first) || !RestoreOne(core, item.second)) return false;
            Release(item.second);
            return true;
        });
        LiveTimelineProps::RestoreUnselected(core);
    }
}
