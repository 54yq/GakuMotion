#pragma once
#include "GkLiveObservation.hpp"
#include <algorithm>
#include <cmath>

namespace GakuMotion::live_audio {
using live_takeover::RuntimeAccess;
struct Api {
    void *threeD{}, *source{}, *clip{};
    const void *getClip{}, *getTime{}, *setTime{}, *getLength{};
    bool Ready() const { return threeD && source && clip && getClip && getTime && setTime && getLength; }
    static Api Resolve(void* (*find)(const char*,const char*,const char*), const ExactMethodMetadata& meta) {
        Api a; if(!find)return a;
        a.threeD=find("Assembly-CSharp.dll","Campus.Live","Live3DPresenter");
        a.source=find("UnityEngine.AudioModule.dll","UnityEngine","AudioSource");
        a.clip=find("UnityEngine.AudioModule.dll","UnityEngine","AudioClip");
        a.getClip=meta.Find(a.source,"get_clip",false,{0x12,a.clip},{});
        a.getTime=meta.Find(a.source,"get_time",false,{0x0c},{});
        a.setTime=meta.Find(a.source,"set_time",false,{0x01},{{0x0c}});
        a.getLength=meta.Find(a.clip,"get_length",false,{0x0c},{});
        return a;
    }
};
struct Observation { bool available{}; float time{},duration{}; unsigned sources{}; };

inline void* Field(const RuntimeAccess& r,void* obj,void* cls,const char* name,void* expected) {
    if(!obj||!cls||!expected||!r.objectClass||!r.findField||!r.fieldType||!r.fieldFlags||
       !r.typeCode||!r.classFromType||!r.readField||r.objectClass(obj)!=cls)return nullptr;
    void* f=r.findField(cls,name);void* t=f?r.fieldType(f):nullptr;
    if(!t||(r.fieldFlags(f)&0x10)||r.typeCode(t)!=0x12||r.classFromType(t)!=expected)return nullptr;
    void* value=nullptr;r.readField(obj,f,&value);
    return value&&r.objectClass(value)==expected?value:nullptr;
}
struct Source {void* object{};float time{},duration{};};
inline unsigned ReadSources(const Api& api,const live_takeover::Api& live,const RuntimeAccess& r,void* scene,Source (&out)[2]) {
    if(!api.Ready()||!live.Ready()||!r.invoke||!r.unbox)return 0;
    void* player=live_takeover::SceneField(live,r,scene,"_livePlayer",live.playerClass);
    void* threeD=Field(r,player,live.playerClass,"_3dLive",api.threeD);if(!threeD)return 0;
    unsigned count=0;
    for(const char* name:{"_bgmSource","_voiceSource"}) {
        void* source=Field(r,threeD,api.threeD,name,api.source);if(!source)continue;
        void* error=nullptr;void* clip=r.invoke(api.getClip,source,nullptr,&error);
        if(error||!clip||r.objectClass(clip)!=api.clip)continue;
        error=nullptr;void* t=r.invoke(api.getTime,source,nullptr,&error);if(error||!t)continue;
        auto* p=static_cast<float*>(r.unbox(t));if(!p)continue;float time=*p;
        error=nullptr;void* d=r.invoke(api.getLength,clip,nullptr,&error);if(error||!d)continue;
        p=static_cast<float*>(r.unbox(d));if(!p)continue;float duration=*p;
        if(!std::isfinite(time)||!std::isfinite(duration)||time<0||duration<=0||time>duration+1)continue;
        out[count++]={source,time,duration};
    }
    return count;
}
inline Observation Observe(const Api& api,const live_takeover::Api& live,const RuntimeAccess& r,void* scene) {
    Source src[2]{};unsigned n=ReadSources(api,live,r,scene,src);
    if(!n)return {};
    float duration=src[0].duration;for(unsigned i=1;i<n;++i)duration=(std::min)(duration,src[i].duration);
    return {true,src[0].time,duration,n};
}
inline bool Seek(const Api& api,const live_takeover::Api& live,const RuntimeAccess& r,void* scene,float seconds) {
    if(!std::isfinite(seconds)||seconds<0)return false;
    Source src[2]{};unsigned n=ReadSources(api,live,r,scene,src);if(!n)return false;
    for(unsigned i=0;i<n;++i)if(seconds>=src[i].duration)return false;
    for(unsigned i=0;i<n;++i) {
        void* arg[]{&seconds};void* ex=nullptr;r.invoke(api.setTime,src[i].object,arg,&ex);
        if(ex) {
            for(unsigned j=0;j<i;++j){void* rollback[]{&src[j].time};void* ignored=nullptr;r.invoke(api.setTime,src[j].object,rollback,&ignored);}
            return false;
        }
    }
    return true;
}
}
