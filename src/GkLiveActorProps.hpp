#pragma once
// Only exact Root_X/Prop_X pairs directly under a confirmed actor are exposed.
// Never traverses or changes actor bones or generic stage nodes.
namespace LiveActorProps
{
    struct Saved { uint64_t token{}, epoch{}; uintptr_t owner{}; int scene{}, id{}; bool original{};
        std::string root, prop; StageLoader::Handle actor{}, object{}; bool geometry{}; };
    inline std::unordered_map<std::string,Saved> saved;
    using TargetFn=void*(*)(StageLoader::Handle);
    inline TargetFn Target() { return reinterpret_cast<TargetFn>(GetProcAddress(g_gameAssembly,"il2cpp_gchandle_get_target")); }
    inline void Release(Saved& s) { if (StageLoader::freeHandle) { if(s.actor)StageLoader::freeHandle(s.actor); if(s.object)StageLoader::freeHandle(s.object); } s.actor={}; s.object={}; }
    inline void AbandonActor(uint64_t token) {
        for (auto it = saved.begin(); it != saved.end();) {
            if (it->second.token != token) { ++it; continue; }
            Release(it->second);
            it = saved.erase(it);
        }
    }
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
    inline void* Resolve(void* actor, int scene, const std::string& root, const std::string& prop) {
        if(!actor || !g_getGameObjectNative || !StageLoader::goTransform || !CameraBridge::IsAlive(actor))return nullptr;
        void* actorGo=g_getGameObjectNative(actor,nullptr);
        if(!actorGo || StageLoader::SceneOfFreshGo(actorGo)!=scene)return nullptr;
        void* tr=StageLoader::Invoke(StageLoader::goTransform,actorGo);
        void* parent=UniqueChild(tr,root); void* node=UniqueChild(parent,prop);
        void* go=node?g_getGameObjectNative(node,nullptr):nullptr;
        return go && CameraBridge::IsAlive(go) && StageLoader::SceneOfFreshGo(go)==scene ? go : nullptr;
    }
    inline void* ResolveSuitcaseGeometry(void* actor, int scene, const std::string& root, const std::string& prop) {
        if (root != "Root_Hknesuitcase" || prop != "Prop_Hknesuitcase" || !Resolve(actor,scene,root,prop)) return nullptr;
        void* actorGo=g_getGameObjectNative(actor,nullptr);
        void* tr=actorGo?StageLoader::Invoke(StageLoader::goTransform,actorGo):nullptr;
        void* parent=UniqueChild(tr,root);
        void* node=UniqueChild(parent,"Geo_Hknesuitcase");
        void* go=node?g_getGameObjectNative(node,nullptr):nullptr;
        return go && CameraBridge::IsAlive(go) && StageLoader::SceneOfFreshGo(go)==scene ? go : nullptr;
    }
    inline bool RestoreOne(GakuMotion::Core& core, Saved& s) {
        auto target=Target(); if(!target || !s.actor || !s.object)return false;
        void* actor=target(s.actor); void* object=target(s.object);
        if(!actor || !object || !CameraBridge::IsAlive(actor) || !CameraBridge::IsAlive(object))return true;
        if(core.liveOwnerToken!=s.owner || core.liveOwnerScene!=s.scene ||
           core.sceneActorRegistry.Resolve(s.token,s.epoch)!=actor ||
           (s.geometry ? ResolveSuitcaseGeometry(actor,s.scene,s.root,s.prop) : Resolve(actor,s.scene,s.root,s.prop))!=object ||
           StageLoader::ReadInt(StageLoader::instanceId,object)!=s.id) return false;
        bool active{}; if(!ReadActive(object,active))return false;
        return active || !s.original || SetActive(object,true);
    }
    inline bool RestoreAll(GakuMotion::Core& core) {
        bool clean=true;
        for(auto it=saved.begin();it!=saved.end();) {
            if(RestoreOne(core,it->second)) {
                if(g_hostContext.logInfo)g_hostContext.logInfo("[GakuMotion][LiveProp] restored actor_token=%llu prop=%s",static_cast<unsigned long long>(it->second.token),it->second.prop.c_str());
                Release(it->second); it=saved.erase(it);
            }
            else { clean=false; ++it; }
        }
        return clean;
    }
    inline void Tick(GakuMotion::Core& core, void* actor, int scene) {
        if(!core.liveTakeoverActive || !core.liveViewingConfirmed || !core.liveOwnerToken ||
           scene!=core.liveOwnerScene || core.injectionPolicy.Read().captureDepth!=0 || !GakuMotion::capture::LiveAutoPhotoIdle() ||
           !StageLoader::newHandle || !StageLoader::freeHandle || !StageLoader::instanceId || !Target())return;
        const auto* entry=core.sceneActorRegistry.Find(actor); if(!entry)return;
        static std::unordered_map<uint64_t,std::chrono::steady_clock::time_point> next;
        const auto now=std::chrono::steady_clock::now(); if(now<next[entry->token])return;
        next[entry->token]=now+std::chrono::milliseconds(200);
        void* actorGo=g_getGameObjectNative?g_getGameObjectNative(actor,nullptr):nullptr;
        void* root=actorGo?StageLoader::Invoke(StageLoader::goTransform,actorGo):nullptr;
        if(!root || StageLoader::SceneOfFreshGo(actorGo)!=scene)return;
        auto& list=core.actorProps;
        list.erase(std::remove_if(list.begin(),list.end(),[&](const auto& p){return p.actorToken==entry->token;}),list.end());
        int n=HelperGetChildCount(root); if(n<0||n>512)return;
        for(int i=0;i<n;++i) {
            void* r=HelperGetChild(root,i); if(!r)continue;
            const std::string rn=HelperGetTransformName(r);
            if(rn.size()<=5 || rn.size()>120 || rn.compare(0,5,"Root_")!=0)continue;
            const std::string pn="Prop_"+rn.substr(5);
            if(!GakuMotion::live_actor_props::IsSupportedPair(rn,pn))continue;
            if(UniqueChild(root,rn)!=r)continue;
            void* p=UniqueChild(r,pn); if(!p)continue;
            void* go=g_getGameObjectNative(p,nullptr); if(!go || !CameraBridge::IsAlive(go) || StageLoader::SceneOfFreshGo(go)!=scene)continue;
            const int id=StageLoader::ReadInt(StageLoader::instanceId,go); if(!id)continue;
            const auto key=GakuMotion::ActorPropKey(entry->token,rn,pn);
            const auto wanted=core.actorPropHidden.find(key); const bool hide=wanted!=core.actorPropHidden.end()&&wanted->second;
            list.push_back({entry->token,rn,pn,GakuMotion::FormatIdolDisplayName(entry->character.c_str()),hide});
            // The suitcase mesh is a sibling of the prop object. Track its
            // original state and identity independently, as GakuActors does.
            if(rn=="Root_Hknesuitcase" && pn=="Prop_Hknesuitcase") {
                const auto geometryKey=key+"|Geo_Hknesuitcase";
                auto geometryIt=saved.find(geometryKey);
                void* geometry=ResolveSuitcaseGeometry(actor,scene,rn,pn);
                if(!geometry) {
                    if(g_hostContext.logWarn)g_hostContext.logWarn("[GakuMotion][LiveProp] suitcase geometry unavailable actor_token=%llu",static_cast<unsigned long long>(entry->token));
                    continue;
                }
                const int geometryId=StageLoader::ReadInt(StageLoader::instanceId,geometry);
                if(!geometryId)continue;
                if(geometryIt!=saved.end() && (geometryIt->second.id!=geometryId || geometryIt->second.epoch!=core.sceneActorRegistry.Epoch())) continue;
                if(!hide) {
                    if(geometryIt!=saved.end() && RestoreOne(core,geometryIt->second)) { Release(geometryIt->second); saved.erase(geometryIt); }
                } else {
                    bool geometryActive{}; if(!ReadActive(geometry,geometryActive))continue;
                    if(geometryIt==saved.end()) {
                        Saved s{entry->token,core.sceneActorRegistry.Epoch(),core.liveOwnerToken,scene,geometryId,geometryActive,rn,pn};
                        s.geometry=true; s.actor=StageLoader::newHandle(actor,false); s.object=StageLoader::newHandle(geometry,false);
                        if(!s.actor || !s.object) { Release(s); continue; }
                        geometryIt=saved.emplace(geometryKey,std::move(s)).first;
                    }
                    if(geometryActive && !SetActive(geometry,false)) {
                        if(g_hostContext.logWarn)g_hostContext.logWarn("[GakuMotion][LiveProp] suitcase geometry hide unconfirmed actor_token=%llu",static_cast<unsigned long long>(entry->token));
                        continue;
                    }
                }
            }
            auto it=saved.find(key);
            if(it!=saved.end() && (it->second.id!=id || it->second.epoch!=core.sceneActorRegistry.Epoch())) { Release(it->second); saved.erase(it); it=saved.end(); }
            if(!hide) {
                if(it!=saved.end() && RestoreOne(core,it->second)) {
                    if(g_hostContext.logInfo)g_hostContext.logInfo("[GakuMotion][LiveProp] restored actor_token=%llu prop=%s",static_cast<unsigned long long>(entry->token),pn.c_str());
                    Release(it->second); saved.erase(it);
                }
                continue;
            }
            bool active{}; if(!ReadActive(go,active))continue;
            if(it==saved.end()) {
                Saved s{entry->token,core.sceneActorRegistry.Epoch(),core.liveOwnerToken,scene,id,active,rn,pn};
                s.actor=StageLoader::newHandle(actor,false); s.object=StageLoader::newHandle(go,false);
                if(!s.actor || !s.object) { Release(s); continue; }
                it=saved.emplace(key,std::move(s)).first;
            }
            if(active) {
                if(SetActive(go,false)) { if(g_hostContext.logInfo)g_hostContext.logInfo("[GakuMotion][LiveProp] hidden actor_token=%llu prop=%s",static_cast<unsigned long long>(entry->token),pn.c_str()); }
                else if(g_hostContext.logWarn) g_hostContext.logWarn("[GakuMotion][LiveProp] hide unconfirmed for %s",pn.c_str());
            }
        }
    }
}
