#pragma once
// Uses the actor's own renderer switch; never disables the actor GameObject,
// Animator, transforms or lights. Unity objects remain main-thread-only.
namespace ActorModelVisibility
{
    struct Saved { uint64_t token{}, epoch{}; int scene{}, id{}; bool changed{}; StageLoader::Handle actor{}; std::vector<ModelRendererMask::Entry> renderers; bool failureReported{}; };
    inline std::unordered_map<uint64_t, Saved> saved;
    inline const void* Method(void* actor, const char* name, bool setter) {
        if (!actor || !g_faceRuntime.objectClass || !g_faceRuntime.parentClass) return nullptr;
        void* klass=g_faceRuntime.objectClass(actor);
        if(!klass)return nullptr;
        static std::unordered_map<void*,std::array<const void*,2>> cache;
        auto& methods=cache[klass]; const size_t index=setter?1:0;
        if(methods[index])return methods[index];
        for (int depth=0; klass && depth<32; ++depth, klass=g_faceRuntime.parentClass(klass)) {
            const void* m=setter ? StageLoader::metadata.Find(klass,name,false,{0x01},{{0x02}})
                                 : StageLoader::metadata.Find(klass,name,false,{0x02},{});
            if (m) { methods[index]=m; return m; }
        }
        return nullptr;
    }
    inline bool Read(void* actor, bool& visible) {
        const auto* method=Method(actor,"IsRendererActive",false);
        if (!method || !g_faceRuntime.invoke || !g_safeBones.unbox) return false;
        void* ex=nullptr; void* boxed=g_faceRuntime.invoke(method,actor,nullptr,&ex);
        void* raw=boxed && !ex ? g_safeBones.unbox(boxed) : nullptr;
        if (!raw) return false; visible=*static_cast<unsigned char*>(raw)!=0; return true;
    }
    inline bool Write(void* actor, bool visible) {
        const auto* method=Method(actor,"SetRendererActive",true);
        if (!method || !g_faceRuntime.invoke) return false;
        void* args[]{&visible}; void* ex=nullptr; g_faceRuntime.invoke(method,actor,args,&ex);
        bool actual{}; return !ex && Read(actor,actual) && actual==visible;
    }
    inline void Release(Saved& s) { ModelRendererMask::Release(s.renderers); if(s.actor && StageLoader::freeHandle) StageLoader::freeHandle(s.actor); s.actor={}; }
    inline bool Restore(GakuMotion::Core& core, Saved& s) {
        const auto target=LiveActorProps::Target();
        if (!target || !s.actor) return false;
        void* actor=target(s.actor);
        if (!actor || !CameraBridge::IsAlive(actor)) return true;
        if (g_currentSceneHandle!=s.scene || !g_sceneOwner.Valid() || g_sceneOwner.sceneHandle!=s.scene ||
            core.sceneActorRegistry.Resolve(s.token,s.epoch)!=actor || !StageLoader::instanceId ||
            StageLoader::ReadInt(StageLoader::instanceId,actor)!=s.id) return false;
        bool visible{}; if (!Read(actor,visible)) return false;
        const bool actorOk = !s.changed || visible || Write(actor,true);
        return ModelRendererMask::Apply(s.renderers,false) && actorOk;
    }
    inline bool RestoreAll(GakuMotion::Core& core) {
        bool ok=true;
        for (auto it=saved.begin(); it!=saved.end();) {
            if (Restore(core,it->second)) { Release(it->second); it=saved.erase(it); }
            else {ok=false; ++it;}
        }
        if (ok) core.actorModelHidden.clear();
        return ok;
    }
    inline void Abandon() { for (auto& item:saved) Release(item.second); saved.clear(); }
    inline void AbandonActor(uint64_t token) {
        const auto it = saved.find(token);
        if (it == saved.end()) return;
        Release(it->second);
        saved.erase(it);
    }
    inline void Tick(GakuMotion::Core& core,void* actor) {
        const auto* entry=core.sceneActorRegistry.Find(actor); if(!entry)return;
        auto found=saved.find(entry->token); if(found==saved.end())return;
        if(core.sceneActorRegistry.Resolve(found->second.token,found->second.epoch)!=actor || !CameraBridge::IsAlive(actor))return;
        bool visible{};
        const bool ok=Read(actor,visible) && (!visible || Write(actor,false)) && ModelRendererMask::Apply(found->second.renderers,true);
        if (!ok && !found->second.failureReported) {
            found->second.failureReported=true;
            core.ensembleNotice="模型显隐未完成：渲染状态变化，请重新勾选或检查日志";
            if(g_hostContext.logWarn)g_hostContext.logWarn("[GakuMotion][ActorModelVisibility] could not maintain hidden renderer state");
        }
        else if(ok) found->second.failureReported=false;
    }
    inline bool Apply(uint64_t token,uint64_t epoch,bool hidden) {
        auto& core=GakuMotion::Core::Instance();
        void* actor=core.sceneActorRegistry.Resolve(token,epoch);
        if (!actor || !CameraBridge::IsAlive(actor) || !g_sceneOwner.Valid() ||
            g_currentSceneHandle!=g_sceneOwner.sceneHandle || !StageLoader::instanceId ||
            !StageLoader::newHandle || !StageLoader::freeHandle || !LiveActorProps::Target()) return false;
        const auto* entry=core.sceneActorRegistry.Find(actor);
        if (!entry || StageLoader::ReadInt(StageLoader::instanceId,actor)!=entry->instanceId) return false;
        auto found=saved.find(token);
        if (!hidden) {
            if(found==saved.end()) return true;
            if(!Restore(core,found->second)) return false;
            Release(found->second); saved.erase(found); return true;
        }
        if(found!=saved.end()) { bool visible{}; return Read(actor,visible) && (!visible || Write(actor,false)) && ModelRendererMask::Apply(found->second.renderers,true); }
        bool visible{}; if(!Read(actor,visible)) return false;
        Saved record{token,epoch,g_currentSceneHandle,entry->instanceId,visible,StageLoader::newHandle(actor,false)};
        if(!record.actor) return false;
        if(!ModelRendererMask::Capture(actor,record.renderers)) {
            if(g_hostContext.logWarn)g_hostContext.logWarn("[GakuMotion][ActorModelVisibility] renderer capture failed: %s",ModelRendererMask::lastFailure);
            Release(record); return false;
        }
        // Retain the original state even if the setter fails after a partial write.
        auto inserted=saved.emplace(token,record).first;
        if ((visible && !Write(actor,false)) || !ModelRendererMask::Apply(inserted->second.renderers,true)) {
            if (Restore(core,inserted->second)) { Release(inserted->second); saved.erase(inserted); }
            return false;
        }
        if(g_hostContext.logInfo)g_hostContext.logInfo("[GakuMotion][ActorModelVisibility] hidden actor_token=%llu renderer_count=%zu",static_cast<unsigned long long>(token),inserted->second.renderers.size());
        return true;
    }
}
