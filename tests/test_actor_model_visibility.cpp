#include "GkSceneActors.hpp"
#include <unordered_map>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <cstring>
#include <initializer_list>
struct FakeActor { int id; bool visible; bool alive=true; int writes=0; bool failWrite=false; };
namespace GakuMotion { struct Core {
 SceneActorRegistry sceneActorRegistry; std::unordered_map<uint64_t,bool> actorModelHidden; std::string ensembleNotice;
 static Core& Instance(){static Core core;return core;}
}; }
struct FakeOwner { int sceneHandle{7}; bool Valid() const{return true;} } g_sceneOwner;
int g_currentSceneHandle=7;
namespace CameraBridge { bool IsAlive(void* p){return static_cast<FakeActor*>(p)->alive;} }
namespace StageLoader {
 using Handle=uintptr_t;
 struct Type {int code;};
 struct Metadata { const void* Find(void*,const char* name,bool,Type,std::initializer_list<Type>) const {
  return !std::strcmp(name,"IsRendererActive")?reinterpret_cast<void*>(1):!std::strcmp(name,"SetRendererActive")?reinterpret_cast<void*>(2):nullptr;
 }} metadata;
 const void* instanceId=reinterpret_cast<void*>(1);
 int ReadInt(const void*,void* actor){return static_cast<FakeActor*>(actor)->id;}
 Handle Pin(void* actor,bool){return reinterpret_cast<Handle>(actor);} void Free(Handle){}
 Handle (*newHandle)(void*,bool)=Pin;void (*freeHandle)(Handle)=Free;
}
namespace LiveActorProps { using Fn=void*(*)(StageLoader::Handle); void* Unpin(StageLoader::Handle h){return reinterpret_cast<void*>(h);} Fn Target(){return Unpin;} }
struct Runtime {
 void* (*objectClass)(void*)=+[](void*)->void*{return reinterpret_cast<void*>(1);};
 void* (*parentClass)(void*)=+[](void*)->void*{return nullptr;};
 void* (*invoke)(const void*,void*,void**,void**)=+[](const void* method,void* self,void** args,void** ex)->void*{
  auto* actor=static_cast<FakeActor*>(self);*ex=nullptr;
  if(method==reinterpret_cast<void*>(1))return &actor->visible;
  ++actor->writes;if(actor->failWrite){*ex=reinterpret_cast<void*>(1);return nullptr;}
  actor->visible=*static_cast<bool*>(args[0]);return nullptr;
 };
} g_faceRuntime;
struct Bones { void* (*unbox)(void*)=+[](void* p)->void*{return p;}; } g_safeBones;
struct FakeLog { void (*logInfo)(const char*,...)=nullptr; void (*logWarn)(const char*,...)=nullptr; } g_hostContext;
namespace ModelRendererMask {
 struct Entry {};
 bool captureOk=true, applyOk=true; const char* lastFailure="test";
 bool Capture(void*,std::vector<Entry>& entries){ if(captureOk)entries.push_back({});return captureOk; }
 bool Apply(std::vector<Entry>&,bool){return applyOk;}
 void Release(std::vector<Entry>& entries){entries.clear();}
}
#include "GkActorModelVisibility.hpp"
#define CHECK(x) do {if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(){
 auto& core=GakuMotion::Core::Instance();FakeActor a{1,true}, b{2,true}, initiallyHidden{3,false};
 core.sceneActorRegistry.Publish({{&a,1,""},{&b,2,""},{&initiallyHidden,3,""}});
 const auto epoch=core.sceneActorRegistry.Epoch(); const auto ta=core.sceneActorRegistry.Find(&a)->token;
 const auto tb=core.sceneActorRegistry.Find(&b)->token; const auto tc=core.sceneActorRegistry.Find(&initiallyHidden)->token;
 CHECK(ActorModelVisibility::Apply(ta,epoch,true)&&!a.visible&&b.visible);
 a.visible=true;ActorModelVisibility::Tick(core,&a);CHECK(!a.visible);
 CHECK(ActorModelVisibility::Apply(tb,epoch,true)&&!b.visible);
 CHECK(ActorModelVisibility::Apply(ta,epoch,false)&&a.visible&&!b.visible);
 CHECK(ActorModelVisibility::Apply(tc,epoch,true)&&!initiallyHidden.visible&&initiallyHidden.writes==0);
 CHECK(ActorModelVisibility::Apply(tc,epoch,false)&&!initiallyHidden.visible&&initiallyHidden.writes==0);
 CHECK(!ActorModelVisibility::Apply(tb,epoch+1,false)&&!b.visible);
 CHECK(ActorModelVisibility::RestoreAll(core)&&b.visible);
 ModelRendererMask::captureOk=false;CHECK(!ActorModelVisibility::Apply(ta,epoch,true)&&a.visible);
 ModelRendererMask::captureOk=true;
 a.failWrite=true;CHECK(!ActorModelVisibility::Apply(ta,epoch,true)&&a.visible);
 ActorModelVisibility::Abandon();
 std::puts("Actor model visibility: identity, independent actors, original visibility and restoration passed.");
}
