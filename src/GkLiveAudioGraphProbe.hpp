#pragma once
// Included in the plugin namespace. All handles are local to one Unity-thread
// read; no graph state is changed or retained.
namespace LiveAudioGraphProbe {
    using Bytes=std::array<unsigned char,16>;
    inline bool Inspect(void* scene,const GakuMotion::live_takeover::RuntimeAccess& r) {
        auto fail=[](const char* reason){if(g_hostContext.logInfo)g_hostContext.logInfo("[GakuMotion][LiveAudioGraph] unavailable=%s",reason);return false;};
        if(!scene||!g_liveApi.Ready()||!r.invoke||!r.unbox||!r.objectClass||!il2cpp_class_get_parent)return fail("runtime");
        const auto meta=GroundShadowProbeBootstrap::Resolve(g_gameAssembly).method;
        auto find=[](const char* name){void* c=FindClass("UnityEngine.DirectorModule.dll","UnityEngine.Playables",name);return c?c:FindClass("UnityEngine.CoreModule.dll","UnityEngine.Playables",name);};
        void* td=FindClass("Assembly-CSharp.dll","Campus.Live","Live3DPresenter");
        void* directorClass=find("PlayableDirector"),*graphClass=find("PlayableGraph"),*outClass=find("PlayableOutputHandle"),*handleClass=find("PlayableHandle");
        void* player=GakuMotion::live_takeover::SceneField(g_liveApi,r,scene,"_livePlayer",g_liveApi.playerClass);
        void* threeD=GakuMotion::live_audio::Field(r,player,g_liveApi.playerClass,"_3dLive",td);if(!threeD)return fail("three_d");
        void* controller=LiveCodeCapture::ObjectField(threeD,td,"_controller");if(!controller)return fail("controller");
        void* base=r.objectClass(controller);for(unsigned i=0;base&&i<6&&LiveCodeCapture::ClassName(base)!="VL.Timeline.VLTimelineController";++i)base=il2cpp_class_get_parent(base);
        if(!base||!directorClass||!graphClass||!outClass||!handleClass)return fail("classes");
        void* field=r.findField(base,"_master");void* type=field?r.fieldType(field):nullptr;
        if(!type||(r.fieldFlags(field)&0x10)||r.typeCode(type)!=0x12||r.classFromType(type)!=directorClass)return fail("master_field");
        void* director=nullptr;r.readField(controller,field,&director);if(!director||r.objectClass(director)!=directorClass)return fail("master_value");
        using Size=int(*)(void*,uint32_t*);auto size=reinterpret_cast<Size>(GetProcAddress(g_gameAssembly,"il2cpp_class_value_size"));uint32_t alignment{};
        if(!size||size(graphClass,&alignment)!=16||size(outClass,&alignment)!=16||size(handleClass,&alignment)!=16)return fail("value_sizes");
        const void* getGraph=meta.Find(directorClass,"get_playableGraph",false,{0x11,graphClass},{});
        const void* graphValid=meta.Find(graphClass,"IsValid",false,{0x02},{});
        const void* getOutput=meta.Find(graphClass,"GetOutputInternal",false,{0x02},{{0x08},{0x11,outClass,true}});
        const void* outputValid=meta.Find(outClass,"IsValid",false,{0x02},{});
        const void* outputType=meta.Find(outClass,"GetPlayableOutputType",false,{0x12},{});
        const void* outputSource=meta.Find(outClass,"GetSourcePlayable",false,{0x11,handleClass},{});
        const void* nodeValid=meta.Find(handleClass,"IsValid",false,{0x02},{});
        const void* nodeType=meta.Find(handleClass,"GetPlayableType",false,{0x12},{});
        const void* nodeTime=meta.Find(handleClass,"GetTime",false,{0x0d},{});
        const void* inputCount=meta.Find(handleClass,"GetInputCount",false,{0x08},{});
        const void* input=meta.Find(handleClass,"GetInputHandle",false,{0x11,handleClass},{{0x08}});
        using FromSystem=void*(*)(void*);auto fromSystem=reinterpret_cast<FromSystem>(GetProcAddress(g_gameAssembly,"il2cpp_class_from_system_type"));
        if(!getGraph||!graphValid||!getOutput||!outputValid||!outputType||!outputSource||!nodeValid||!nodeType||!nodeTime||!inputCount||!input||!fromSystem){if(g_hostContext.logInfo)g_hostContext.logInfo("[GakuMotion][LiveAudioGraph] methods graph=%d valid=%d output=%d outvalid=%d outtype=%d source=%d nodevalid=%d nodetype=%d time=%d inputs=%d input=%d types=%d",int(!!getGraph),int(!!graphValid),int(!!getOutput),int(!!outputValid),int(!!outputType),int(!!outputSource),int(!!nodeValid),int(!!nodeType),int(!!nodeTime),int(!!inputCount),int(!!input),int(!!fromSystem));return false;}
        auto call=[&](const void* method,void* obj,void** args=nullptr)->void*{void* ex=nullptr;void* x=r.invoke(method,obj,args,&ex);return ex?nullptr:x;};
        auto raw=[&](const void* method,void* obj,void** args=nullptr)->void*{void* x=call(method,obj,args);return x?r.unbox(x):nullptr;};
        auto copy=[&](const void* method,void* obj,Bytes& to,void** args=nullptr)->bool{void* p=raw(method,obj,args);if(!p)return false;std::memcpy(to.data(),p,to.size());return true;};
        void* boxed=call(getGraph,director);void* graphRaw=boxed?r.unbox(boxed):nullptr;if(!graphRaw)return fail("graph_read");
        alignas(16) Bytes graph{};std::memcpy(graph.data(),graphRaw,16);auto* valid=static_cast<unsigned char*>(raw(graphValid,graph.data()));if(!valid||!*valid)return fail("graph_invalid");
        unsigned total=0,outputs=0;
        auto walk=[&](auto&& self,Bytes& node,int depth,int output)->void {
            if(depth>12||total>=128)return;auto* ok=static_cast<unsigned char*>(raw(nodeValid,node.data()));if(!ok||!*ok)return;
            void* typeObject=call(nodeType,node.data());void* klass=typeObject?fromSystem(typeObject):nullptr;
            auto* t=static_cast<double*>(raw(nodeTime,node.data()));const double time=t?*t:-1.0;
            auto* c=static_cast<int*>(raw(inputCount,node.data()));int n=c?*c:0;if(n<0||n>32)n=0;
            if(g_hostContext.logInfo)g_hostContext.logInfo("[GakuMotion][LiveAudioGraph] output=%d depth=%d type=%s time=%.3f inputs=%d",output,depth,LiveCodeCapture::ClassName(klass).c_str(),time,n);
            ++total;for(int i=0;i<n&&total<128;++i){alignas(16) Bytes child{};void* args[]{&i};if(copy(input,node.data(),child,args))self(self,child,depth+1,output);}
        };
        for(int i=0;i<32;++i){alignas(16) Bytes out{};void* args[]{&i,out.data()};auto* ok=static_cast<unsigned char*>(raw(getOutput,graph.data(),args));if(!ok||!*ok)break;
            ok=static_cast<unsigned char*>(raw(outputValid,out.data()));if(!ok||!*ok)continue;
            void* typeObject=call(outputType,out.data());void* klass=typeObject?fromSystem(typeObject):nullptr;
            if(g_hostContext.logInfo)g_hostContext.logInfo("[GakuMotion][LiveAudioGraph] output=%d output_type=%s",i,LiveCodeCapture::ClassName(klass).c_str());
            ++outputs;alignas(16) Bytes root{};if(copy(outputSource,out.data(),root))walk(walk,root,0,i);
        }
        if(g_hostContext.logInfo)g_hostContext.logInfo("[GakuMotion][LiveAudioGraph] read_only=1 outputs=%u nodes=%u bounded=1",outputs,total);
        return outputs!=0;
    }
}
