#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <vector>
struct FakeRenderer { int id; bool forced{}, enabled{true}, alive{true}; };
struct FakeActor { std::vector<FakeRenderer*> renderers; };
bool missingForce=false, missingElement=false;
struct FakeArray { char header[24]{}; uintptr_t length{}; FakeRenderer* elements[8]{}; };
FakeArray fakeArray;
size_t FakeHeaderSize(){return 32;}
void* g_gameAssembly=reinterpret_cast<void*>(1);
void* ClassType(void* p){return p;} void* TypeObject(void* p){return p;}
void* FakeProc(void*,const char* name){return !std::strcmp(name,"il2cpp_class_get_type")?reinterpret_cast<void*>(&ClassType):!std::strcmp(name,"il2cpp_type_get_object")?reinterpret_cast<void*>(&TypeObject):!std::strcmp(name,"il2cpp_array_object_header_size")?reinterpret_cast<void*>(&FakeHeaderSize):nullptr;}
#define GetProcAddress FakeProc
void* FindClass(const char*,const char*,const char* name){return reinterpret_cast<void*>(!std::strcmp(name,"Component")?1:!std::strcmp(name,"Renderer")?2:!std::strcmp(name,"Array")?3:0);}
namespace StageLoader {
 using Handle=void*; struct Type {int code;};
 struct Metadata {const void* Find(void*,const char* name,bool,Type,std::initializer_list<Type>)const {
 if(missingElement && !std::strcmp(name,"GetValue"))return nullptr;
 if(missingForce && (!std::strcmp(name,"get_forceRenderingOff") || !std::strcmp(name,"set_forceRenderingOff")))return nullptr;
 const char* names[]={"GetComponentsInChildren","GetValue","get_forceRenderingOff","set_forceRenderingOff","get_enabled","set_enabled"};
 for(uintptr_t i=0;i<6;++i)if(!std::strcmp(name,names[i]))return reinterpret_cast<void*>(i+1);return nullptr;
 }} metadata;
 void* Pin(void* p,bool){return p;}void Free(void*){};void*(*newHandle)(void*,bool)=Pin;void(*freeHandle)(void*)=Free;
 uintptr_t Len(void* p){return static_cast<FakeArray*>(p)->length;}uintptr_t(*arrayLength)(void*)=Len;
 const void* instanceId=reinterpret_cast<void*>(1);int ReadInt(const void*,void* p){return static_cast<FakeRenderer*>(p)->id;}
}
namespace LiveActorProps { using TargetFn=void*(*)(void*);void* Unpin(void* p){return p;}TargetFn Target(){return Unpin;} }
namespace CameraBridge {bool IsAlive(void* p){return static_cast<FakeRenderer*>(p)->alive;} }
struct Runtime {void* (*invoke)(const void*,void*,void**,void**)=+[](const void* method,void* object,void** args,void** ex)->void* {
 *ex=nullptr;const auto id=reinterpret_cast<uintptr_t>(method);if(id==1){
  const auto& list=static_cast<FakeActor*>(object)->renderers;fakeArray.length=list.size();
  for(size_t i=0;i<list.size();++i)fakeArray.elements[i]=list[i];return &fakeArray;
 }
 if(id==2)return static_cast<FakeArray*>(object)->elements[*static_cast<int*>(args[0])];
 auto* r=static_cast<FakeRenderer*>(object);if(id==3)return &r->forced;if(id==4){r->forced=*static_cast<bool*>(args[0]);return nullptr;}
 if(id==5)return &r->enabled;if(id==6){r->enabled=*static_cast<bool*>(args[0]);return nullptr;}return nullptr;
};} g_faceRuntime;
struct Bones {void*(*unbox)(void*)=+[](void* p)->void*{return p;};} g_safeBones;
#include "GkModelRendererMask.hpp"
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(int argc,char** argv){
 missingForce=argc>1 && std::strstr(argv[1],"force");
 missingElement=argc>1 && std::strstr(argv[1],"array");
 FakeRenderer a{1,false,true},b{2,true,false};FakeActor actor{{&a,&b}};std::vector<ModelRendererMask::Entry> entries;
 CHECK(ModelRendererMask::Capture(&actor,entries)&&entries.size()==2);
 if(missingForce) {
  CHECK(ModelRendererMask::Apply(entries,true)&&!a.enabled&&!b.enabled);
  a.enabled=true;CHECK(ModelRendererMask::Apply(entries,true)&&!a.enabled);
  b.enabled=true;CHECK(ModelRendererMask::Apply(entries,false)&&a.enabled&&!b.enabled);
 } else {
  CHECK(ModelRendererMask::Apply(entries,true)&&a.forced&&b.forced&&a.enabled&&!b.enabled);
  a.forced=false;CHECK(ModelRendererMask::Apply(entries,true)&&a.forced);
  a.enabled=false;b.enabled=true;
  CHECK(ModelRendererMask::Apply(entries,false)&&!a.forced&&b.forced&&a.enabled&&!b.enabled);
 }
 a.alive=false;b.alive=false;CHECK(!ModelRendererMask::Apply(entries,true));
 ModelRendererMask::Release(entries);CHECK(entries.empty());
 FakeActor empty{};CHECK(!ModelRendererMask::Capture(&empty,entries));
 std::puts("Renderer mask: independent renderer state, reapplication, restoration and missing renderers passed.");
}
