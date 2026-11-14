#pragma once
// Unity-thread only. This changes ActorAnimation's shared wind inputs, never
// the stage wind volume, the global force, or any native solver buffers.
namespace LiveWindOverride
{
    inline GakuMotion::live_wind::Lease lease;
    inline StageLoader::Handle handle{};
    inline void* property{};
    inline int bytes{}, powerOffset{-1}, fluctuationOffset{-1};
    using TargetFn = void* (*)(StageLoader::Handle);
    inline TargetFn targetFn{};
    inline auto nextFind = std::chrono::steady_clock::time_point{};
    inline void Release() { if (handle && StageLoader::freeHandle) StageLoader::freeHandle(handle); handle={}; lease.Clear(); }
    inline bool Read(void* object, std::array<unsigned char,1024>& data, GakuMotion::live_wind::Values& values) {
        if (!object || !property || !g_faceRuntime.readField || bytes <= 0 || bytes > 1024 ||
            powerOffset < 0 || fluctuationOffset < 0) return false;
        g_faceRuntime.readField(object, property, data.data());
        std::memcpy(&values.power, data.data()+powerOffset, sizeof(float));
        std::memcpy(&values.fluctuation, data.data()+fluctuationOffset, sizeof(float));
        return GakuMotion::live_wind::Finite(values);
    }
    inline bool Write(void* object, std::array<unsigned char,1024>& data, GakuMotion::live_wind::Values values) {
        if (!g_faceRuntime.writeField || !GakuMotion::live_wind::Finite(values)) return false;
        std::memcpy(data.data()+powerOffset, &values.power, sizeof(float));
        std::memcpy(data.data()+fluctuationOffset, &values.fluctuation, sizeof(float));
        g_faceRuntime.writeField(object, property, data.data());
        std::array<unsigned char,1024> verify{}; GakuMotion::live_wind::Values actual{};
        return Read(object, verify, actual) && actual.power == values.power && actual.fluctuation == values.fluctuation;
    }
    inline bool Restore(uintptr_t owner, int scene) {
        if (!lease.active) { Release(); return true; }
        const auto& core=GakuMotion::Core::Instance();
        if (core.liveOwnerToken != lease.owner || core.liveOwnerScene != lease.scene) return false;
        void* object = handle && targetFn ? targetFn(handle) : nullptr;
        if (!object || !CameraBridge::IsAlive(object)) { Release(); return true; }
        const int id = StageLoader::ReadInt(StageLoader::instanceId, object);
        if (!lease.Matches(owner,scene,id)) {
            if (g_hostContext.logWarn) g_hostContext.logWarn("[GakuMotion][LiveWind] restore refused: owner or manager changed");
            return false;
        }
        std::array<unsigned char,1024> data{}; GakuMotion::live_wind::Values current{};
        if (!Read(object,data,current) || !Write(object,data,lease.RestoreValues(current))) return false;
        if (g_hostContext.logInfo) g_hostContext.logInfo("[GakuMotion][LiveWind] restored=1 power=%.4f fluctuation=%.4f",lease.saved.power,lease.saved.fluctuation);
        Release(); return true;
    }
    inline bool Initialize(uintptr_t owner, int scene) {
        if (!g_gameAssembly || !owner || !scene || !g_faceRuntime.findField || !g_faceRuntime.fieldType ||
            !g_faceRuntime.fieldFlags || !g_faceRuntime.typeCode || !g_faceRuntime.readField || !g_faceRuntime.writeField ||
            !g_faceRuntime.parentClass || !g_componentKlass ||
            !StageLoader::instanceId || !StageLoader::findAll || !StageLoader::arrayLength || !StageLoader::newHandle ||
            !StageLoader::freeHandle || !il2cpp_class_get_type || !il2cpp_type_get_object) return false;
        auto api=GroundShadowProbeBootstrap::Resolve(g_gameAssembly);
        auto size=api.valueSize;
        void* cls=FindClass("ActorAnimation.Runtime.dll","ActorAnimation","ActorAnimationManageData");
        void* dataClass=FindClass("ActorAnimation.Runtime.dll","ActorAnimation","ActorAnimationManagePropertyData");
        if (!cls || !dataClass || !size || !api.fieldOffset || !api.method.classFromType) return false;
        bool component=false;
        for (void* current=cls; current; current=g_faceRuntime.parentClass(current))
            if (current==g_componentKlass) { component=true; break; }
        if (!component) return false;
        property=g_faceRuntime.findField(cls,"propertyData");
        void* type=property ? g_faceRuntime.fieldType(property) : nullptr;
        uint32_t align{}; bytes=size(dataClass,&align);
        if (!type || g_faceRuntime.typeCode(type)!=0x11 || (g_faceRuntime.fieldFlags(property)&0x30) ||
            api.method.classFromType(type)!=dataClass || bytes<=0 || bytes>1024) return false;
        auto offset=[&](const char* name) {
            void* field=g_faceRuntime.findField(dataClass,name); void* ft=field?g_faceRuntime.fieldType(field):nullptr;
            if (!ft || g_faceRuntime.typeCode(ft)!=0x0c || (g_faceRuntime.fieldFlags(field)&0x10)) return -1;
            int n=api.fieldOffset(field)-16; return n>=0 && n+4<=bytes ? n : -1;
        };
        powerOffset=offset("windPower"); fluctuationOffset=offset("windFluctuation");
        if (powerOffset<0 || fluctuationOffset<0 || powerOffset==fluctuationOffset) return false;
        targetFn=reinterpret_cast<TargetFn>(GetProcAddress(g_gameAssembly,"il2cpp_gchandle_get_target"));
        void* typeObject=il2cpp_type_get_object(il2cpp_class_get_type(cls));
        if (!targetFn || !typeObject) return false;
        bool inactive=false; void* args[]{typeObject,&inactive};
        void* array=StageLoader::Invoke(StageLoader::findAll,nullptr,args);
        auto pin=array?StageLoader::newHandle(array,false):nullptr;
        if (!pin) return false;
        struct ReleasePin { StageLoader::Handle pin; ~ReleasePin(){StageLoader::freeHandle(pin);} } release{pin};
        if (StageLoader::arrayLength(array)!=1) return false;
        void* object=*reinterpret_cast<void**>(static_cast<char*>(array)+32);
        if (!object || !CameraBridge::IsAlive(object)) return false;
        const int id=StageLoader::ReadInt(StageLoader::instanceId,object);
        std::array<unsigned char,1024> data{}; GakuMotion::live_wind::Values current{};
        if (!id || !Read(object,data,current)) return false;
        handle=StageLoader::newHandle(object,false);
        if (!handle || !lease.Begin(owner,scene,id,current)) { Release(); return false; }
        if (g_hostContext.logInfo) g_hostContext.logInfo("[GakuMotion][LiveWind] acquired=1 original_power=%.4f original_fluctuation=%.4f",current.power,current.fluctuation);
        return true;
    }
    inline void Tick(GakuMotion::Core& core) {
        const bool wanted=core.liveTakeoverActive && !core.liveTakeoverFault && core.liveViewingConfirmed &&
            core.liveObservation.confirmed && core.liveObservation.paused && core.liveOwnerToken && core.liveOwnerScene &&
            core.injectionPolicy.Read().captureDepth==0 && GakuMotion::capture::LiveAutoPhotoIdle();
        if (!wanted) { if (lease.active) Restore(core.liveOwnerToken,core.liveOwnerScene); return; }
        if (lease.active && !lease.Matches(core.liveOwnerToken,core.liveOwnerScene,lease.instance)) {
            if (!Restore(core.liveOwnerToken,core.liveOwnerScene)) return;
        }
        if (!lease.active) {
            const auto now=std::chrono::steady_clock::now(); if (now<nextFind) return;
            nextFind=now+std::chrono::seconds(2);
            if (!Initialize(core.liveOwnerToken,core.liveOwnerScene)) return;
        }
        void* object=targetFn(handle);
        if (!object || !CameraBridge::IsAlive(object) || StageLoader::ReadInt(StageLoader::instanceId,object)!=lease.instance) { Release(); return; }
        std::array<unsigned char,1024> data{}; GakuMotion::live_wind::Values current{};
        if (!Read(object,data,current)) return;
        lease.Observe(current);
        if ((current.power!=0 || current.fluctuation!=0) && !Write(object,data,{})) {
            if (g_hostContext.logWarn) g_hostContext.logWarn("[GakuMotion][LiveWind] write unconfirmed; restoring original values");
            Restore(lease.owner,lease.scene);
        }
    }
}
