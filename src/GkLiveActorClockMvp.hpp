#pragma once
#include <algorithm>
#include <vector>
#include <type_traits>
#include "GkLiveActorClipEnd.hpp"
// Engine-owned graph clock trial. Never copies job payloads, calls Evaluate,
// modifies streams, or claims to provide a Swing checkpoint.
template <int Slot> struct LiveActorClockState {
    alignas(16) static inline std::array<unsigned char,16> graph{};
    static inline uintptr_t owner{}, actor{}, player{}; static inline int scene{};
    static inline bool offered{},active{}; static inline int originalMode{},originalPlaying{};
    static inline double started{};
    static inline double rootStart{},rootDelta{};static inline bool rootSampled{},rootAdvancedKnown{};
    static inline const void *valid{},*playing{},*mode{},*setMode{},*play{},*stop{};
    static inline void* graphClass{};
    static inline unsigned verifiedOutputs{};
    static inline bool mmdSession{}, borrowedRunningGraph{};
    static inline LiveActorClipEnd clipEnd;
    struct Node { alignas(16) std::array<unsigned char,16> handle{}; double time{}; };
    static inline std::vector<Node> nodes;
    static inline const void *nodeValid{},*nodeTime{},*nodeSetTime{},*nodeSetDone{},*nodeInputs{},*nodeInput{},*rootCount{},*rootGet{};
    static inline double loopStarted{},loopWindow{3.0}; static inline uint64_t loops{};
    static inline const char* loopError{};
    static inline void* NodeCall(const void* method,void* self,void** args,bool& ok) {
        void* ex=nullptr;void* boxed=g_faceRuntime.invoke(method,self,args,&ex);ok=!ex;return boxed;
    }
    static inline bool CaptureLoop(double now,double remaining) {
        loopError="resolve_handle_class";
        nodes.clear();loops=0;loopStarted=now;loopWindow=std::isfinite(remaining)?(std::max)(0.1,(std::min)(3.0,remaining-0.15)):3.0;
        const auto md=GroundShadowProbeBootstrap::Resolve(g_gameAssembly).method;
        void* h=FindClass("UnityEngine.DirectorModule.dll","UnityEngine.Playables","PlayableHandle");
        if(!h)h=FindClass("UnityEngine.CoreModule.dll","UnityEngine.Playables","PlayableHandle");
        if(!h)return false;
        using ValueSize=int(*)(void*,uint32_t*);
        const auto size=reinterpret_cast<ValueSize>(GetProcAddress(g_gameAssembly,"il2cpp_class_value_size"));
        uint32_t alignment{};if(!size||size(h,&alignment)!=16)return false;
        nodeValid=md.Find(h,"IsValid",false,{0x02},{});nodeTime=md.Find(h,"GetTime",false,{0x0d},{});
        nodeSetTime=md.Find(h,"SetTime",false,{0x01},{{0x0d}});nodeSetDone=md.Find(h,"SetDone",false,{0x01},{{0x02}});
        nodeInputs=md.Find(h,"GetInputCount",false,{0x08},{});nodeInput=md.Find(h,"GetInputHandle",false,{0x11,h},{{0x08}});
        rootCount=md.Find(graphClass,"GetRootPlayableCount",false,{0x08},{});
        rootGet=md.Find(graphClass,"GetRootPlayableInternal",false,{0x11,h},{{0x08}});
        loopError="resolve_loop_methods";
        if(!nodeValid||!nodeTime||!nodeSetTime||!nodeSetDone||!nodeInputs||!nodeInput||!rootCount||!rootGet)return false;
        auto copyHandle=[&](const void* method,void* self,void** args,Node& n){bool ok{};void* b=NodeCall(method,self,args,ok);void* raw=ok&&b?g_safeBones.unbox(b):nullptr;if(!raw)return false;std::memcpy(n.handle.data(),raw,16);return true;};
        loopError="enumerate_graph_roots";
        bool ok{};void* b=NodeCall(rootCount,graph.data(),nullptr,ok);void* raw=ok&&b?g_safeBones.unbox(b):nullptr;
        if(!raw)return false;const int roots=*static_cast<int*>(raw);if(roots<1||roots>32)return false;
        std::vector<Node> pending;
        for(int i=0;i<roots;++i){Node n;void* args[]{&i};if(!copyHandle(rootGet,graph.data(),args,n))return false;pending.push_back(n);}
        for(size_t i=0;i<pending.size();++i){
            loopError="validate_node";
            Node n=pending[i];bool seen=false;for(const auto& old:nodes)if(old.handle==n.handle){seen=true;break;}if(seen)continue;
            b=NodeCall(nodeValid,n.handle.data(),nullptr,ok);raw=ok&&b?g_safeBones.unbox(b):nullptr;if(!raw)return false;
            if(!*static_cast<unsigned char*>(raw)){if(i<static_cast<size_t>(roots)||n.handle!=std::array<unsigned char,16>{})return false;continue;}
            if(nodes.size()>=256)return false;
            loopError="read_node_time";
            b=NodeCall(nodeTime,n.handle.data(),nullptr,ok);raw=ok&&b?g_safeBones.unbox(b):nullptr;if(!raw)return false;n.time=*static_cast<double*>(raw);if(!std::isfinite(n.time))return false;
            nodes.push_back(n);
            loopError="enumerate_node_inputs";
            b=NodeCall(nodeInputs,n.handle.data(),nullptr,ok);raw=ok&&b?g_safeBones.unbox(b):nullptr;if(!raw)return false;const int count=*static_cast<int*>(raw);if(count<0||count>256||pending.size()+count>4096)return false;
            for(int j=0;j<count;++j){Node child;void* args[]{&j};if(!copyHandle(nodeInput,n.handle.data(),args,child))return false;pending.push_back(child);}
        }
        if(nodes.empty())return false;loopError=nullptr;return true;
    }
    static inline bool TickLoop(double now) {
        if(!active||!SameOwner()||Read(valid,true)!=1)return false;
        if (mmdSession) {
            if (Read(mode) != 1 || Read(playing, true) != 1) return false;
            if (!clipEnd.ClipCount() || clipEnd.Check(graph.data(), now)) return true;
            loopError = "clip_end_lost_native_clock_continues";
            return clipEnd.Restore();
        }
        if(now-loopStarted<loopWindow)return true;
        // Reset only this actor's graph. Never seek the paused stage Director.
        for(auto& n:nodes){bool ok{};void* b=NodeCall(nodeValid,n.handle.data(),nullptr,ok);void* raw=ok&&b?g_safeBones.unbox(b):nullptr;if(!raw||!*static_cast<unsigned char*>(raw))return false;}
        for(auto& n:nodes){bool ok{};void* t[]{&n.time};NodeCall(nodeSetTime,n.handle.data(),t,ok);if(!ok)return false;bool done=false;void* d[]{&done};NodeCall(nodeSetDone,n.handle.data(),d,ok);if(!ok)return false;}
        loopStarted=now;++loops;return true;
    }
    static inline void* Call(const void* method,void** args=nullptr) {
        if(!method||!g_faceRuntime.invoke||!g_safeBones.unbox)return nullptr;
        void* ex=nullptr;void* result=g_faceRuntime.invoke(method,graph.data(),args,&ex);
        return ex?nullptr:result;
    }
    static inline int Read(const void* method,bool boolean=false) {
        void* result=Call(method);void* raw=result?g_safeBones.unbox(result):nullptr;
        return !raw?-1:boolean?int(*static_cast<unsigned char*>(raw)):*static_cast<int*>(raw);
    }
    static inline bool InvokeVoid(const void* method,void** args=nullptr) {
        if(!method||!g_faceRuntime.invoke)return false;
        void* ex=nullptr;g_faceRuntime.invoke(method,graph.data(),args,&ex);return !ex;
    }
    static inline void Reject(const std::string& detail) {
        auto& c=GakuMotion::Core::Instance();offered=false;
        const std::string message="角色图实验不可用："+detail;
        if constexpr(Slot==0) {
            c.liveActorClockAvailable=false;
            if(c.liveActorClockNotice!=message&&g_hostContext.logInfo)g_hostContext.logInfo("[GakuMotion][ActorClockGate] %s",message.c_str());
            c.liveActorClockNotice=message;
        }
    }
    static inline bool RootTime(double& time) {
        if(!graphClass)return false;
        const auto metadata=GroundShadowProbeBootstrap::Resolve(g_gameAssembly).method;
        void* handle=FindClass("UnityEngine.DirectorModule.dll","UnityEngine.Playables","PlayableHandle");
        if(!handle)handle=FindClass("UnityEngine.CoreModule.dll","UnityEngine.Playables","PlayableHandle");
        using ValueSize=int(*)(void*,uint32_t*);
        const auto size=reinterpret_cast<ValueSize>(GetProcAddress(g_gameAssembly,"il2cpp_class_value_size"));
        uint32_t align{};if(!handle||!size||size(handle,&align)!=16)return false;
        const void* root=metadata.Find(graphClass,"GetRootPlayableInternal",false,{0x11,handle},{{0x08}});
        const void* getTime=metadata.Find(handle,"GetTime",false,{0x0d},{});
        if(!root||!getTime)return false;
        int index=0;void* args[]{&index};void* boxed=Call(root,args);
        void* raw=boxed?g_safeBones.unbox(boxed):nullptr;if(!raw)return false;
        alignas(16) std::array<unsigned char,16> copy{};std::memcpy(copy.data(),raw,copy.size());
        void* ex=nullptr;boxed=g_faceRuntime.invoke(getTime,copy.data(),nullptr,&ex);
        raw=boxed&&!ex?g_safeBones.unbox(boxed):nullptr;if(!raw)return false;
        time=*static_cast<double*>(raw);return std::isfinite(time);
    }
    static inline void Offer(void* value,void* klass,void* actorValue,void* playerValue,bool targetMatches) {
        auto& c=GakuMotion::Core::Instance();
        if(active)return;
        if(c.liveActorClockFault){Reject("上次恢复未确认，请重新进入 Live");return;}
        if(!value||!klass||!actorValue||!playerValue||!c.liveViewingConfirmed||!c.liveOwnerToken){Reject("场景或角色图未确认");return;}
        if(!targetMatches){Reject("首个输出尚未匹配当前角色");return;}
        using ValueSize=int(*)(void*,uint32_t*);
        const auto size=reinterpret_cast<ValueSize>(GetProcAddress(g_gameAssembly,"il2cpp_class_value_size"));
        uint32_t align{};if(!size){Reject("图值类型大小查询接口缺失");return;}
        const int graphSize=size(klass,&align);if(graphSize!=16){Reject("图句柄大小不符："+std::to_string(graphSize));return;}
        const auto metadata=GroundShadowProbeBootstrap::Resolve(g_gameAssembly).method;
        void* modeClass=FindClass("UnityEngine.DirectorModule.dll","UnityEngine.Playables","DirectorUpdateMode");
        if(!modeClass)modeClass=FindClass("UnityEngine.CoreModule.dll","UnityEngine.Playables","DirectorUpdateMode");
        void* outputHandle=FindClass("UnityEngine.DirectorModule.dll","UnityEngine.Playables","PlayableOutputHandle");
        if(!outputHandle)outputHandle=FindClass("UnityEngine.CoreModule.dll","UnityEngine.Playables","PlayableOutputHandle");
        void* outputClass=FindClass("UnityEngine.DirectorModule.dll","UnityEngine.Playables","PlayableOutput");
        if(!outputClass)outputClass=FindClass("UnityEngine.CoreModule.dll","UnityEngine.Playables","PlayableOutput");
        void* animationClass=FindClass("UnityEngine.AnimationModule.dll","UnityEngine.Animations","AnimationPlayableOutput");
        const void* publicGet=outputClass?metadata.Find(klass,"GetOutput",false,{0x11,outputClass},{{0x08}}):nullptr;
        const void* internalGet=outputHandle?metadata.Find(klass,"GetOutputInternal",false,{0x02},{{0x08},{0x11,outputHandle,true}}):nullptr;
        const void* getHandle=outputClass&&outputHandle?metadata.Find(outputClass,"GetHandle",false,{0x11,outputHandle},{}):nullptr;
        const void* isNull=outputHandle?metadata.Find(outputHandle,"IsNull",false,{0x02},{}):nullptr;
        const void* outputValid=outputHandle?metadata.Find(outputHandle,"IsValid",false,{0x02},{}):nullptr;
        const void* cast=outputClass&&animationClass?metadata.Find(animationClass,"op_Explicit",true,{0x11,animationClass},{{0x11,outputClass}}):nullptr;
        const void* target=animationClass&&g_animatorKlass?metadata.Find(animationClass,"GetTarget",false,{0x12,g_animatorKlass},{}):nullptr;
        if(!publicGet||!getHandle||!isNull||!outputValid||!cast||!target){Reject("输出类型或目标枚举接口未解析");return;}
        uint32_t a{};
        if(size(outputClass,&a)!=16||size(outputHandle,&a)!=16||size(animationClass,&a)!=16){Reject("输出值类型大小不符");return;}
        void* expected=HelperGetActorAnimator(actorValue);
        if(!expected){Reject("当前角色 Animator 不可用");return;}
        unsigned checked=0;bool ended=false;
        // Multiple animation outputs are allowed only when EVERY output is
        // valid and targets this actor. Count alone is not a sharing test.
        for(int index=0;index<32;++index){
            void* args[]{&index};void* ex=nullptr;
            void* boxed=g_faceRuntime.invoke(publicGet,value,args,&ex);void* raw=boxed&&!ex?g_safeBones.unbox(boxed):nullptr;
            if(!raw){
                // Some Unity builds throw at the first out-of-range index.
                // Only a successful internal false result establishes the end.
                if(internalGet){
                    alignas(16) std::array<unsigned char,16> unused{};void* fallbackArgs[]{&index,unused.data()};ex=nullptr;
                    boxed=g_faceRuntime.invoke(internalGet,value,fallbackArgs,&ex);raw=boxed&&!ex?g_safeBones.unbox(boxed):nullptr;
                    if(raw&&*static_cast<unsigned char*>(raw)==0){ended=true;break;}
                }
                Reject("输出 "+std::to_string(index)+" 查询失败，无法确认完整输出列表");return;
            }
            alignas(16) std::array<unsigned char,16> output{};std::memcpy(output.data(),raw,16);
            boxed=g_faceRuntime.invoke(getHandle,output.data(),nullptr,&ex);raw=boxed&&!ex?g_safeBones.unbox(boxed):nullptr;
            if(!raw){Reject("输出句柄读取失败");return;}
            alignas(16) std::array<unsigned char,16> handle{};std::memcpy(handle.data(),raw,16);
            boxed=g_faceRuntime.invoke(isNull,handle.data(),nullptr,&ex);raw=boxed&&!ex?g_safeBones.unbox(boxed):nullptr;
            if(!raw){Reject("无法确认输出是否为空");return;}
            if(*static_cast<unsigned char*>(raw)!=0){ended=true;break;}
            boxed=g_faceRuntime.invoke(outputValid,handle.data(),nullptr,&ex);raw=boxed&&!ex?g_safeBones.unbox(boxed):nullptr;
            if(!raw||*static_cast<unsigned char*>(raw)==0){Reject("输出 "+std::to_string(index)+" 句柄无效");return;}
            void* castArgs[]{output.data()};boxed=g_faceRuntime.invoke(cast,nullptr,castArgs,&ex);raw=boxed&&!ex?g_safeBones.unbox(boxed):nullptr;
            if(!raw){Reject("输出 "+std::to_string(index)+" 不是可确认的动画输出");return;}
            alignas(16) std::array<unsigned char,16> animation{};std::memcpy(animation.data(),raw,16);
            void* animator=g_faceRuntime.invoke(target,animation.data(),nullptr,&ex);
            if(ex||animator!=expected){Reject("输出 "+std::to_string(index)+" 指向其他对象或目标未知");return;}
            ++checked;
        }
        if(!ended||!checked){Reject("输出为空或超过 32 项检查上限，未确认角色专用图");return;}
        verifiedOutputs=checked;
        valid=metadata.Find(klass,"IsValid",false,{0x02},{});
        playing=metadata.Find(klass,"IsPlaying",false,{0x02},{});
        mode=modeClass?metadata.Find(klass,"GetTimeUpdateMode",false,{0x11,modeClass},{}):nullptr;
        setMode=modeClass?metadata.Find(klass,"SetTimeUpdateMode",false,{0x01},{{0x11,modeClass}}):nullptr;
        play=metadata.Find(klass,"Play",false,{0x01},{});stop=metadata.Find(klass,"Stop",false,{0x01},{});
        if(!valid||!playing||!mode||!setMode||!play||!stop){Reject("图播放或恢复接口未全部解析");return;}
        std::memcpy(graph.data(),value,graph.size());graphClass=klass;
        owner=c.liveOwnerToken;scene=c.liveOwnerScene;actor=reinterpret_cast<uintptr_t>(actorValue);player=reinterpret_cast<uintptr_t>(playerValue);
        const int v=Read(valid,true),m=Read(mode),p=Read(playing,true);
        offered=v==1 && ((m==3 && p==0) || (Slot==0 && c.liveTakeoverActive && m==1 && p==1));
        if(!offered){Reject("图状态不符：valid="+std::to_string(v)+"，mode="+std::to_string(m)+"，playing="+std::to_string(p));return;}
        if constexpr(Slot==0) {
            c.liveActorClockAvailable=offered;
            if(c.liveActorClockNotice.empty()||c.liveActorClockNotice.rfind("角色图实验不可用：",0)==0)c.liveActorClockNotice="已核对 "+std::to_string(verifiedOutputs)+" 个动画输出，均属于当前角色；暂停后可运行实验。";
        }
    }
    static inline bool SameOwner() {
        const auto& c=GakuMotion::Core::Instance();return owner&&owner==c.liveOwnerToken&&scene==c.liveOwnerScene&&c.liveViewingConfirmed;
    }
    static inline bool Start(double now,bool forMmd=false) {
        auto& c=GakuMotion::Core::Instance();
        if(active||c.liveActorClockFault||!offered||!SameOwner()||!c.liveObservation.confirmed||!c.liveObservation.paused||c.liveTakeoverActive!=forMmd||c.liveTakeoverFault||
            Read(valid,true)!=1)return false;
        const int currentMode=Read(mode), currentPlaying=Read(playing,true);
        const bool alreadyRunning = Slot==0 && forMmd && currentMode==1 && currentPlaying==1;
        if(!alreadyRunning && (currentMode!=3 || currentPlaying!=0))return false;
        if(forMmd){loops=0;nodes.clear();loopError=nullptr;}
        if(forMmd && !clipEnd.Prepare(graphClass, graph.data(), verifiedOutputs)) {
            clipEnd = {};
            loopError = "native_clock_without_clip_end";
        }
        originalMode=currentMode;originalPlaying=currentPlaying;
        borrowedRunningGraph=alreadyRunning;
        rootSampled=RootTime(rootStart);rootAdvancedKnown=false;rootDelta=0;
        // Mark before writes so every partial start takes the restoration path.
        active=true;mmdSession=forMmd;started=now;
        if(forMmd && clipEnd.ClipCount() && !clipEnd.Park(now)) {
            loopError = "original_animation_clip_end_failed";
            if (!clipEnd.Restore()) return false;
            loopError = "native_clock_without_clip_end";
        }
        if(alreadyRunning)return true; // Only the animation clip clocks are leased.
        int gameTime=1;void* args[]{&gameTime};
        return InvokeVoid(setMode,args)&&InvokeVoid(play)&&Read(mode)==1&&Read(playing,true)==1;
    }
    static inline bool RestoreGraph() {
        if(!active)return true;
        if(!SameOwner()||Read(valid,true)!=1){clipEnd={};active=false;offered=false;return false;}
        double rootEnd{};rootAdvancedKnown=rootSampled&&RootTime(rootEnd);rootDelta=rootAdvancedKnown?rootEnd-rootStart:0;
        bool ok=borrowedRunningGraph || InvokeVoid(stop);
        ok=clipEnd.Restore() && ok;
        if(!borrowedRunningGraph) {
            void* args[]{&originalMode};ok=InvokeVoid(setMode,args)&&ok;
            if(originalPlaying)ok=InvokeVoid(play)&&ok;
        }
        ok=Read(mode)==originalMode&&Read(playing,true)==originalPlaying&&ok;
        active=false;offered=false;borrowedRunningGraph=false;return ok;
    }
};
struct LiveDynamicFollowerClock {
    alignas(16) std::array<unsigned char,16> graph{};
    uintptr_t owner{}, actor{}, player{}; int scene{};
    bool offered{},active{}; int originalMode{},originalPlaying{};
    double started{};
    double rootStart{},rootDelta{};bool rootSampled{},rootAdvancedKnown{};
    const void *valid{},*playing{},*mode{},*setMode{},*play{},*stop{};
    void* graphClass{};
    unsigned verifiedOutputs{};
    bool mmdSession{};
    LiveActorClipEnd clipEnd;
    struct Node { alignas(16) std::array<unsigned char,16> handle{}; double time{}; };
    std::vector<Node> nodes;
    const void *nodeValid{},*nodeTime{},*nodeSetTime{},*nodeSetDone{},*nodeInputs{},*nodeInput{},*rootCount{},*rootGet{};
    double loopStarted{},loopWindow{3.0}; uint64_t loops{};
    const char* loopError{};
    void* NodeCall(const void* method,void* self,void** args,bool& ok) {
        void* ex=nullptr;void* boxed=g_faceRuntime.invoke(method,self,args,&ex);ok=!ex;return boxed;
    }
    bool CaptureLoop(double now,double remaining) {
        loopError="resolve_handle_class";
        nodes.clear();loops=0;loopStarted=now;loopWindow=std::isfinite(remaining)?(std::max)(0.1,(std::min)(3.0,remaining-0.15)):3.0;
        const auto md=GroundShadowProbeBootstrap::Resolve(g_gameAssembly).method;
        void* h=FindClass("UnityEngine.DirectorModule.dll","UnityEngine.Playables","PlayableHandle");
        if(!h)h=FindClass("UnityEngine.CoreModule.dll","UnityEngine.Playables","PlayableHandle");
        if(!h)return false;
        using ValueSize=int(*)(void*,uint32_t*);
        const auto size=reinterpret_cast<ValueSize>(GetProcAddress(g_gameAssembly,"il2cpp_class_value_size"));
        uint32_t alignment{};if(!size||size(h,&alignment)!=16)return false;
        nodeValid=md.Find(h,"IsValid",false,{0x02},{});nodeTime=md.Find(h,"GetTime",false,{0x0d},{});
        nodeSetTime=md.Find(h,"SetTime",false,{0x01},{{0x0d}});nodeSetDone=md.Find(h,"SetDone",false,{0x01},{{0x02}});
        nodeInputs=md.Find(h,"GetInputCount",false,{0x08},{});nodeInput=md.Find(h,"GetInputHandle",false,{0x11,h},{{0x08}});
        rootCount=md.Find(graphClass,"GetRootPlayableCount",false,{0x08},{});
        rootGet=md.Find(graphClass,"GetRootPlayableInternal",false,{0x11,h},{{0x08}});
        loopError="resolve_loop_methods";
        if(!nodeValid||!nodeTime||!nodeSetTime||!nodeSetDone||!nodeInputs||!nodeInput||!rootCount||!rootGet)return false;
        auto copyHandle=[&](const void* method,void* self,void** args,Node& n){bool ok{};void* b=NodeCall(method,self,args,ok);void* raw=ok&&b?g_safeBones.unbox(b):nullptr;if(!raw)return false;std::memcpy(n.handle.data(),raw,16);return true;};
        loopError="enumerate_graph_roots";
        bool ok{};void* b=NodeCall(rootCount,graph.data(),nullptr,ok);void* raw=ok&&b?g_safeBones.unbox(b):nullptr;
        if(!raw)return false;const int roots=*static_cast<int*>(raw);if(roots<1||roots>32)return false;
        std::vector<Node> pending;
        for(int i=0;i<roots;++i){Node n;void* args[]{&i};if(!copyHandle(rootGet,graph.data(),args,n))return false;pending.push_back(n);}
        for(size_t i=0;i<pending.size();++i){
            loopError="validate_node";
            Node n=pending[i];bool seen=false;for(const auto& old:nodes)if(old.handle==n.handle){seen=true;break;}if(seen)continue;
            b=NodeCall(nodeValid,n.handle.data(),nullptr,ok);raw=ok&&b?g_safeBones.unbox(b):nullptr;if(!raw)return false;
            if(!*static_cast<unsigned char*>(raw)){if(i<static_cast<size_t>(roots)||n.handle!=std::array<unsigned char,16>{})return false;continue;}
            if(nodes.size()>=256)return false;
            loopError="read_node_time";
            b=NodeCall(nodeTime,n.handle.data(),nullptr,ok);raw=ok&&b?g_safeBones.unbox(b):nullptr;if(!raw)return false;n.time=*static_cast<double*>(raw);if(!std::isfinite(n.time))return false;
            nodes.push_back(n);
            loopError="enumerate_node_inputs";
            b=NodeCall(nodeInputs,n.handle.data(),nullptr,ok);raw=ok&&b?g_safeBones.unbox(b):nullptr;if(!raw)return false;const int count=*static_cast<int*>(raw);if(count<0||count>256||pending.size()+count>4096)return false;
            for(int j=0;j<count;++j){Node child;void* args[]{&j};if(!copyHandle(nodeInput,n.handle.data(),args,child))return false;pending.push_back(child);}
        }
        if(nodes.empty())return false;loopError=nullptr;return true;
    }
    bool TickLoop(double now) {
        if(!active||!SameOwner()||Read(valid,true)!=1)return false;
        if (mmdSession) {
            if (Read(mode) != 1 || Read(playing, true) != 1) return false;
            if (!clipEnd.ClipCount() || clipEnd.Check(graph.data(), now)) return true;
            loopError = "clip_end_lost_native_clock_continues";
            return clipEnd.Restore();
        }
        if(now-loopStarted<loopWindow)return true;
        // Reset only this actor's graph. Never seek the paused stage Director.
        for(auto& n:nodes){bool ok{};void* b=NodeCall(nodeValid,n.handle.data(),nullptr,ok);void* raw=ok&&b?g_safeBones.unbox(b):nullptr;if(!raw||!*static_cast<unsigned char*>(raw))return false;}
        for(auto& n:nodes){bool ok{};void* t[]{&n.time};NodeCall(nodeSetTime,n.handle.data(),t,ok);if(!ok)return false;bool done=false;void* d[]{&done};NodeCall(nodeSetDone,n.handle.data(),d,ok);if(!ok)return false;}
        loopStarted=now;++loops;return true;
    }
    void* Call(const void* method,void** args=nullptr) {
        if(!method||!g_faceRuntime.invoke||!g_safeBones.unbox)return nullptr;
        void* ex=nullptr;void* result=g_faceRuntime.invoke(method,graph.data(),args,&ex);
        return ex?nullptr:result;
    }
    int Read(const void* method,bool boolean=false) {
        void* result=Call(method);void* raw=result?g_safeBones.unbox(result):nullptr;
        return !raw?-1:boolean?int(*static_cast<unsigned char*>(raw)):*static_cast<int*>(raw);
    }
    bool InvokeVoid(const void* method,void** args=nullptr) {
        if(!method||!g_faceRuntime.invoke)return false;
        void* ex=nullptr;g_faceRuntime.invoke(method,graph.data(),args,&ex);return !ex;
    }
    void Reject(const std::string& detail) {
        auto& c=GakuMotion::Core::Instance();offered=false;
        const std::string message="角色图实验不可用："+detail;
        if constexpr(false) {
            c.liveActorClockAvailable=false;
            if(c.liveActorClockNotice!=message&&g_hostContext.logInfo)g_hostContext.logInfo("[GakuMotion][ActorClockGate] %s",message.c_str());
            c.liveActorClockNotice=message;
        }
    }
    bool RootTime(double& time) {
        if(!graphClass)return false;
        const auto metadata=GroundShadowProbeBootstrap::Resolve(g_gameAssembly).method;
        void* handle=FindClass("UnityEngine.DirectorModule.dll","UnityEngine.Playables","PlayableHandle");
        if(!handle)handle=FindClass("UnityEngine.CoreModule.dll","UnityEngine.Playables","PlayableHandle");
        using ValueSize=int(*)(void*,uint32_t*);
        const auto size=reinterpret_cast<ValueSize>(GetProcAddress(g_gameAssembly,"il2cpp_class_value_size"));
        uint32_t align{};if(!handle||!size||size(handle,&align)!=16)return false;
        const void* root=metadata.Find(graphClass,"GetRootPlayableInternal",false,{0x11,handle},{{0x08}});
        const void* getTime=metadata.Find(handle,"GetTime",false,{0x0d},{});
        if(!root||!getTime)return false;
        int index=0;void* args[]{&index};void* boxed=Call(root,args);
        void* raw=boxed?g_safeBones.unbox(boxed):nullptr;if(!raw)return false;
        alignas(16) std::array<unsigned char,16> copy{};std::memcpy(copy.data(),raw,copy.size());
        void* ex=nullptr;boxed=g_faceRuntime.invoke(getTime,copy.data(),nullptr,&ex);
        raw=boxed&&!ex?g_safeBones.unbox(boxed):nullptr;if(!raw)return false;
        time=*static_cast<double*>(raw);return std::isfinite(time);
    }
    void Offer(void* value,void* klass,void* actorValue,void* playerValue,bool targetMatches) {
        auto& c=GakuMotion::Core::Instance();
        if(active)return;
        if(c.liveActorClockFault){Reject("上次恢复未确认，请重新进入 Live");return;}
        if(!value||!klass||!actorValue||!playerValue||!c.liveViewingConfirmed||!c.liveOwnerToken){Reject("场景或角色图未确认");return;}
        if(!targetMatches){Reject("首个输出尚未匹配当前角色");return;}
        using ValueSize=int(*)(void*,uint32_t*);
        const auto size=reinterpret_cast<ValueSize>(GetProcAddress(g_gameAssembly,"il2cpp_class_value_size"));
        uint32_t align{};if(!size){Reject("图值类型大小查询接口缺失");return;}
        const int graphSize=size(klass,&align);if(graphSize!=16){Reject("图句柄大小不符："+std::to_string(graphSize));return;}
        const auto metadata=GroundShadowProbeBootstrap::Resolve(g_gameAssembly).method;
        void* modeClass=FindClass("UnityEngine.DirectorModule.dll","UnityEngine.Playables","DirectorUpdateMode");
        if(!modeClass)modeClass=FindClass("UnityEngine.CoreModule.dll","UnityEngine.Playables","DirectorUpdateMode");
        void* outputHandle=FindClass("UnityEngine.DirectorModule.dll","UnityEngine.Playables","PlayableOutputHandle");
        if(!outputHandle)outputHandle=FindClass("UnityEngine.CoreModule.dll","UnityEngine.Playables","PlayableOutputHandle");
        void* outputClass=FindClass("UnityEngine.DirectorModule.dll","UnityEngine.Playables","PlayableOutput");
        if(!outputClass)outputClass=FindClass("UnityEngine.CoreModule.dll","UnityEngine.Playables","PlayableOutput");
        void* animationClass=FindClass("UnityEngine.AnimationModule.dll","UnityEngine.Animations","AnimationPlayableOutput");
        const void* publicGet=outputClass?metadata.Find(klass,"GetOutput",false,{0x11,outputClass},{{0x08}}):nullptr;
        const void* internalGet=outputHandle?metadata.Find(klass,"GetOutputInternal",false,{0x02},{{0x08},{0x11,outputHandle,true}}):nullptr;
        const void* getHandle=outputClass&&outputHandle?metadata.Find(outputClass,"GetHandle",false,{0x11,outputHandle},{}):nullptr;
        const void* isNull=outputHandle?metadata.Find(outputHandle,"IsNull",false,{0x02},{}):nullptr;
        const void* outputValid=outputHandle?metadata.Find(outputHandle,"IsValid",false,{0x02},{}):nullptr;
        const void* cast=outputClass&&animationClass?metadata.Find(animationClass,"op_Explicit",true,{0x11,animationClass},{{0x11,outputClass}}):nullptr;
        const void* target=animationClass&&g_animatorKlass?metadata.Find(animationClass,"GetTarget",false,{0x12,g_animatorKlass},{}):nullptr;
        if(!publicGet||!getHandle||!isNull||!outputValid||!cast||!target){Reject("输出类型或目标枚举接口未解析");return;}
        uint32_t a{};
        if(size(outputClass,&a)!=16||size(outputHandle,&a)!=16||size(animationClass,&a)!=16){Reject("输出值类型大小不符");return;}
        void* expected=HelperGetActorAnimator(actorValue);
        if(!expected){Reject("当前角色 Animator 不可用");return;}
        unsigned checked=0;bool ended=false;
        // Multiple animation outputs are allowed only when EVERY output is
        // valid and targets this actor. Count alone is not a sharing test.
        for(int index=0;index<32;++index){
            void* args[]{&index};void* ex=nullptr;
            void* boxed=g_faceRuntime.invoke(publicGet,value,args,&ex);void* raw=boxed&&!ex?g_safeBones.unbox(boxed):nullptr;
            if(!raw){
                // Some Unity builds throw at the first out-of-range index.
                // Only a successful internal false result establishes the end.
                if(internalGet){
                    alignas(16) std::array<unsigned char,16> unused{};void* fallbackArgs[]{&index,unused.data()};ex=nullptr;
                    boxed=g_faceRuntime.invoke(internalGet,value,fallbackArgs,&ex);raw=boxed&&!ex?g_safeBones.unbox(boxed):nullptr;
                    if(raw&&*static_cast<unsigned char*>(raw)==0){ended=true;break;}
                }
                Reject("输出 "+std::to_string(index)+" 查询失败，无法确认完整输出列表");return;
            }
            alignas(16) std::array<unsigned char,16> output{};std::memcpy(output.data(),raw,16);
            boxed=g_faceRuntime.invoke(getHandle,output.data(),nullptr,&ex);raw=boxed&&!ex?g_safeBones.unbox(boxed):nullptr;
            if(!raw){Reject("输出句柄读取失败");return;}
            alignas(16) std::array<unsigned char,16> handle{};std::memcpy(handle.data(),raw,16);
            boxed=g_faceRuntime.invoke(isNull,handle.data(),nullptr,&ex);raw=boxed&&!ex?g_safeBones.unbox(boxed):nullptr;
            if(!raw){Reject("无法确认输出是否为空");return;}
            if(*static_cast<unsigned char*>(raw)!=0){ended=true;break;}
            boxed=g_faceRuntime.invoke(outputValid,handle.data(),nullptr,&ex);raw=boxed&&!ex?g_safeBones.unbox(boxed):nullptr;
            if(!raw||*static_cast<unsigned char*>(raw)==0){Reject("输出 "+std::to_string(index)+" 句柄无效");return;}
            void* castArgs[]{output.data()};boxed=g_faceRuntime.invoke(cast,nullptr,castArgs,&ex);raw=boxed&&!ex?g_safeBones.unbox(boxed):nullptr;
            if(!raw){Reject("输出 "+std::to_string(index)+" 不是可确认的动画输出");return;}
            alignas(16) std::array<unsigned char,16> animation{};std::memcpy(animation.data(),raw,16);
            void* animator=g_faceRuntime.invoke(target,animation.data(),nullptr,&ex);
            if(ex||animator!=expected){Reject("输出 "+std::to_string(index)+" 指向其他对象或目标未知");return;}
            ++checked;
        }
        if(!ended||!checked){Reject("输出为空或超过 32 项检查上限，未确认角色专用图");return;}
        verifiedOutputs=checked;
        valid=metadata.Find(klass,"IsValid",false,{0x02},{});
        playing=metadata.Find(klass,"IsPlaying",false,{0x02},{});
        mode=modeClass?metadata.Find(klass,"GetTimeUpdateMode",false,{0x11,modeClass},{}):nullptr;
        setMode=modeClass?metadata.Find(klass,"SetTimeUpdateMode",false,{0x01},{{0x11,modeClass}}):nullptr;
        play=metadata.Find(klass,"Play",false,{0x01},{});stop=metadata.Find(klass,"Stop",false,{0x01},{});
        if(!valid||!playing||!mode||!setMode||!play||!stop){Reject("图播放或恢复接口未全部解析");return;}
        std::memcpy(graph.data(),value,graph.size());graphClass=klass;
        owner=c.liveOwnerToken;scene=c.liveOwnerScene;actor=reinterpret_cast<uintptr_t>(actorValue);player=reinterpret_cast<uintptr_t>(playerValue);
        const int v=Read(valid,true),m=Read(mode),p=Read(playing,true);
        offered=v==1 && m==3 && p==0;
        if(!offered){Reject("图状态不符：valid="+std::to_string(v)+"，mode="+std::to_string(m)+"，playing="+std::to_string(p));return;}
        if constexpr(false) {
            c.liveActorClockAvailable=offered;
            if(c.liveActorClockNotice.empty()||c.liveActorClockNotice.rfind("角色图实验不可用：",0)==0)c.liveActorClockNotice="已核对 "+std::to_string(verifiedOutputs)+" 个动画输出，均属于当前角色；暂停后可运行实验。";
        }
    }
    bool SameOwner() {
        const auto& c=GakuMotion::Core::Instance();return owner&&owner==c.liveOwnerToken&&scene==c.liveOwnerScene&&c.liveViewingConfirmed;
    }
    bool Start(double now,bool forMmd=false) {
        auto& c=GakuMotion::Core::Instance();
        if(active||c.liveActorClockFault||!offered||!SameOwner()||!c.liveObservation.confirmed||!c.liveObservation.paused||c.liveTakeoverActive!=forMmd||c.liveTakeoverFault||
            Read(valid,true)!=1||Read(mode)!=3||Read(playing,true)!=0)return false;
        if(forMmd){loops=0;nodes.clear();loopError=nullptr;}
        if(forMmd && !clipEnd.Prepare(graphClass, graph.data(), verifiedOutputs)) {
            clipEnd = {};
            loopError = "native_clock_without_clip_end";
        }
        originalMode=3;originalPlaying=0;
        rootSampled=RootTime(rootStart);rootAdvancedKnown=false;rootDelta=0;
        // Mark before writes so every partial start takes the restoration path.
        active=true;mmdSession=forMmd;started=now;int gameTime=1;void* args[]{&gameTime};
        if(forMmd && clipEnd.ClipCount() && !clipEnd.Park(now)) {
            loopError = "original_animation_clip_end_failed";
            if (!clipEnd.Restore()) return false;
            loopError = "native_clock_without_clip_end";
        }
        return InvokeVoid(setMode,args)&&InvokeVoid(play)&&Read(mode)==1&&Read(playing,true)==1;
    }
    bool RestoreGraph() {
        if(!active)return true;
        if(!SameOwner()||Read(valid,true)!=1){clipEnd={};active=false;offered=false;return false;}
        double rootEnd{};rootAdvancedKnown=rootSampled&&RootTime(rootEnd);rootDelta=rootAdvancedKnown?rootEnd-rootStart:0;
        bool ok=InvokeVoid(stop);
        ok=clipEnd.Restore() && ok;
        void* args[]{&originalMode};ok=InvokeVoid(setMode,args)&&ok;
        if(originalPlaying)ok=InvokeVoid(play)&&ok;
        ok=Read(mode)==originalMode&&Read(playing,true)==originalPlaying&&ok;
        active=false;offered=false;return ok;
    }
};

using LiveActorClockMvp = LiveActorClockState<0>;
using LiveFollowerClock1 = LiveActorClockState<1>;
using LiveFollowerClock2 = LiveActorClockState<2>;
