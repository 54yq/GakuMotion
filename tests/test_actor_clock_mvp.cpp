#include <windows.h>
#include "GkLivePhysicsClockPolicy.hpp"
#include "GkLiveWindLeasePolicy.hpp"
#include "GkLiveArmSnapshotSchedule.hpp"
#include <array>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include "GkExactMethod.hpp"
namespace GakuMotion {
struct Core {
 uintptr_t liveOwnerToken{42};int liveOwnerScene{7};bool liveViewingConfirmed{true},liveTakeoverActive{},liveTakeoverFault{},liveActorClockFault{},liveActorClockAvailable{};
 struct {bool confirmed{true},paused{true};}liveObservation;
 std::string liveActorClockNotice;
 static Core& Instance(){static Core c;return c;}
};
}
HMODULE g_gameAssembly{};
bool internalMissing{},internalThrows{},publicThrows{},throwPastEnd{},invalidOutput{};int publicReads{},outputCount=1,foreignIndex=-1,nonAnimationIndex=-1;
void* g_animatorKlass=reinterpret_cast<void*>(99);
void* HelperGetActorAnimator(void*){return reinterpret_cast<void*>(2);}
void* FindClass(const char*,const char*,const char* name){
 if(!std::strcmp(name,"PlayableOutputHandle"))return reinterpret_cast<void*>(33);
 if(!std::strcmp(name,"PlayableHandle"))return reinterpret_cast<void*>(31);
 if(!std::strcmp(name,"AnimationScriptPlayable"))return reinterpret_cast<void*>(34);
 if(!std::strcmp(name,"CampusActorAnimationJob"))return reinterpret_cast<void*>(35);
 if(!std::strcmp(name,"AnimationMixerPlayable"))return reinterpret_cast<void*>(36);
 if(!std::strcmp(name,"AnimationLayerMixerPlayable"))return reinterpret_cast<void*>(37);
 return reinterpret_cast<void*>(1);
}
namespace GroundShadowProbeBootstrap {
struct Metadata {
 const void* Find(void* owner,const char* name,bool,GakuMotion::ExactMethodMetadata::Type,std::initializer_list<GakuMotion::ExactMethodMetadata::Type>) const {
  if(owner==reinterpret_cast<void*>(33)&&!std::strcmp(name,"IsValid"))return reinterpret_cast<void*>(11);
  if(owner==reinterpret_cast<void*>(31)&&!std::strcmp(name,"IsNull"))return reinterpret_cast<void*>(30);
  if(!std::strcmp(name,"op_Explicit"))return reinterpret_cast<void*>(12);
  if(!std::strcmp(name,"GetTarget"))return reinterpret_cast<void*>(13);
  const char* loopNames[]{"GetRootPlayableCount","GetTime","GetRootPlayableInternal","GetInputCount","GetInputHandle","SetTime","SetDone"};
  for(uintptr_t i=0;i<7;++i)if(!std::strcmp(name,loopNames[i]))return reinterpret_cast<void*>(i+14);
  const char* isolationNames[]{"GetSourcePlayable","GetPlayableType","GetJobType","GetInputWeightFromIndex","SetInputWeightFromIndex",
   "GetOutputCount","GetOutputHandle","ConnectInternal","DisconnectInternal"};
  for(uintptr_t i=0;i<9;++i)if(!std::strcmp(name,isolationNames[i]))return reinterpret_cast<void*>(i+21);
  const char* names[]{"IsValid","IsPlaying","GetTimeUpdateMode","SetTimeUpdateMode","Play","Stop","GetOutputInternal","GetOutput","GetHandle","IsNull"};
  for(uintptr_t i=0;i<10;++i)if(!std::strcmp(name,names[i]))return i==6&&internalMissing?nullptr:reinterpret_cast<void*>(i+1);
  return nullptr;
 }
};struct Api {Metadata method;};inline Api Resolve(HMODULE){return {};}}
int FakeValueSize(void*,uint32_t* align){*align=8;return 16;}
void* FakeClassFromType(void* type){return type;}
FARPROC FakeGetProcAddress(HMODULE,const char* name){
 if(!std::strcmp(name,"il2cpp_class_value_size"))return reinterpret_cast<FARPROC>(&FakeValueSize);
 if(!std::strcmp(name,"il2cpp_class_from_system_type"))return reinterpret_cast<FARPROC>(&FakeClassFromType);
 return nullptr;
}
int modeValue=3,playingValue=0,validValue=1,reply{};bool failPlay{},failStop{};int writes{};
double times[]{145.0,145.0}; bool failNodeRead{},failNodeWrite{};int nodeWrites{};
float inputWeight=0.75f;
int isolationWrites{};
bool wrongJobType{}, failDisconnectReadback{}, inputConnected=true;
void* Invoke(const void* m,void* self,void** args,void** ex){
 *ex=nullptr;
 switch(reinterpret_cast<uintptr_t>(m)){
 case 1:reply=validValue;return &reply;
 case 2:reply=playingValue;return &reply;
 case 3:reply=modeValue;return &reply;
 case 4:++writes;modeValue=*static_cast<int*>(args[0]);break;
 case 5:++writes;if(failPlay){*ex=reinterpret_cast<void*>(1);break;}playingValue=1;break;
 case 6:++writes;if(failStop){*ex=reinterpret_cast<void*>(1);break;}playingValue=0;break;
 case 7:if(internalThrows){*ex=reinterpret_cast<void*>(1);break;}reply=*static_cast<int*>(args[0])<outputCount;if(reply)std::memcpy(args[1],args[0],sizeof(int));return &reply;
 case 8:{++publicReads;const int i=*static_cast<int*>(args[0]);if(publicThrows||(throwPastEnd&&i>=outputCount)){*ex=reinterpret_cast<void*>(1);break;}static std::array<unsigned char,16> output{};std::memcpy(output.data(),&i,sizeof(i));return output.data();}
 case 9:return self;
 case 10:{int i{};std::memcpy(&i,self,sizeof(i));reply=i>=outputCount;return &reply;}
 case 11:reply=!invalidOutput;return &reply;
 case 12:{int i{};std::memcpy(&i,args[0],sizeof(i));if(i==nonAnimationIndex){*ex=reinterpret_cast<void*>(1);break;}return args[0];}
 case 13:{int i{};std::memcpy(&i,self,sizeof(i));return reinterpret_cast<void*>(uintptr_t(i==foreignIndex?3:2));}
 case 14:reply=1;return &reply;
 case 15:{if(failNodeRead){*ex=reinterpret_cast<void*>(1);break;}int i{};std::memcpy(&i,self,4);return &times[i];}
 case 16:{static std::array<unsigned char,16> h{};return h.data();}
 case 17:{int i{};std::memcpy(&i,self,4);reply=i==0?1:0;return &reply;}
 case 18:{if(!inputConnected&&failDisconnectReadback){failDisconnectReadback=false;return nullptr;}
          static std::array<unsigned char,16> h{};h[0]=inputConnected?1:255;return h.data();}
 case 19:{++nodeWrites;if(failNodeWrite){*ex=reinterpret_cast<void*>(1);break;}int i{};std::memcpy(&i,self,4);times[i]=*static_cast<double*>(args[0]);break;}
 case 20:++nodeWrites;break;
 case 21:{static std::array<unsigned char,16> job{};return job.data();}
 case 22:return reinterpret_cast<void*>(34);
 case 23:return reinterpret_cast<void*>(uintptr_t(wrongJobType?36:35));
 case 24:return &inputWeight;
 case 25:++isolationWrites;inputWeight=*static_cast<float*>(args[1]);break;
 case 26:reply=1;return &reply;
 case 27:{static std::array<unsigned char,16> job{};return job.data();}
 case 28:++isolationWrites;inputConnected=true;inputWeight=0.0f;reply=1;return &reply;
 case 29:++isolationWrites;inputConnected=false;break;
 case 30:reply=*static_cast<unsigned char*>(self)==255;return &reply;
 }return nullptr;
}
struct {void*(*invoke)(const void*,void*,void**,void**)=Invoke;} g_faceRuntime;
struct {void*(*unbox)(void*)=+[](void* p){return p;};}g_safeBones;
struct {void(*logInfo)(const char*,...){};}g_hostContext;
#define GetProcAddress FakeGetProcAddress
#include "GkLiveActorClockMvp.hpp"
#undef GetProcAddress
#define CHECK(x) do{if(!(x)){std::printf("FAIL line %d\n",__LINE__);return 1;}}while(0)
struct ClockTest : LiveActorClockMvp { static int Run(){
 using GakuMotion::live_physics_clock::ShouldRun;
 CHECK(ShouldRun(true,false,2,true,false,true));
 CHECK(ShouldRun(true,false,2,false,true,true));
 CHECK(!ShouldRun(true,false,2,false,true,false)); // paused preview cannot start a new lease
 CHECK(!ShouldRun(true,false,2,false,false,true));
 CHECK(!ShouldRun(false,false,2,false,true,true));
 CHECK(!ShouldRun(true,true,2,false,true,true));
 CHECK(!ShouldRun(true,false,1,false,true,true));
 GakuMotion::live_arm_snapshot::Schedule samples;
 void* sampleA=reinterpret_cast<void*>(1); void* sampleB=reinterpret_cast<void*>(2);
 CHECK(samples.Due(42,7,sampleA,10.0) && !samples.Due(42,7,sampleA,10.9));
 CHECK(samples.Due(42,7,sampleB,10.2) && samples.Due(42,7,sampleA,11.0));
 CHECK(samples.Due(43,7,sampleA,11.1) && !samples.Due(0,7,sampleA,12));
 GakuMotion::live_wind::Lease wind;
 CHECK(!wind.Begin(0,7,9,{4.f,3.f}));
 CHECK(wind.Begin(42,7,9,{4.f,3.f}) && wind.Matches(42,7,9));
 CHECK(!wind.Begin(42,7,9,{9.f,9.f}));
 CHECK(!wind.Matches(43,7,9) && !wind.Matches(42,8,9) && !wind.Matches(42,7,10));
 wind.Observe({0.f,0.f}); auto restored=wind.RestoreValues({0.f,0.f});
 CHECK(restored.power==4.f && restored.fluctuation==3.f);
 wind.Observe({5.f,2.f}); restored=wind.RestoreValues({0.f,0.f});
 CHECK(restored.power==5.f && restored.fluctuation==2.f);
 restored=wind.RestoreValues({6.f,1.f}); CHECK(restored.power==6.f && restored.fluctuation==1.f);
 wind.Clear(); CHECK(!wind.active && !wind.Matches(42,7,9));
 CHECK(!wind.Begin(42,7,9,{NAN,3.f}));
 CHECK(wind.Begin(42,7,9,{4.f,3.f}));
 CHECK(!wind.OwnerLost(42,7,false)); // Ordinary exit still restores the same owner's wind.
 CHECK(!wind.OwnerLost(0,0,false));  // A transient gap is not a scene change.
 CHECK(wind.OwnerLost(0,0,true));
 CHECK(wind.OwnerLost(43,7,false));
 CHECK(wind.OwnerLost(42,8,false));
 wind.Clear();
 CHECK(wind.Begin(43,8,9,{2.f,1.f})); // The manager can be the same in the next Live.
 restored=wind.RestoreValues({0.f,0.f});
 CHECK(restored.power==2.f && restored.fluctuation==1.f);
 CHECK(!wind.OwnerLost(43,8,false));
 auto& c=GakuMotion::Core::Instance();
 valid=reinterpret_cast<void*>(1);playing=reinterpret_cast<void*>(2);mode=reinterpret_cast<void*>(3);
 setMode=reinterpret_cast<void*>(4);play=reinterpret_cast<void*>(5);stop=reinterpret_cast<void*>(6);
 owner=42;scene=7;offered=true;
 c.liveObservation.paused=false;CHECK(!Start(1)&&writes==0);
 c.liveObservation.paused=true;c.liveTakeoverActive=true;CHECK(!Start(1)&&writes==0);c.liveTakeoverActive=false;
 CHECK(Start(1)&&active&&modeValue==1&&playingValue==1);
 CHECK(!Start(2));CHECK(RestoreGraph()&&!active&&modeValue==3&&playingValue==0);
 offered=true;failPlay=true;CHECK(!Start(3)&&active&&modeValue==1);CHECK(RestoreGraph()&&modeValue==3&&playingValue==0);failPlay=false;
 offered=true;CHECK(Start(3.5));failStop=true;CHECK(!RestoreGraph()&&playingValue==1);failStop=false;playingValue=0;modeValue=3;
 offered=true;CHECK(Start(4));c.liveOwnerToken=99;const int before=writes;
 CHECK(!RestoreGraph()&&writes==before&&!active); // no writes to a previous scene graph
 c.liveOwnerToken=42;modeValue=3;playingValue=0;offered=true;c.liveActorClockFault=true;
 CHECK(!Start(5));c.liveActorClockFault=false;validValue=0;CHECK(!Start(5));
 validValue=1;offered=false;active=false;modeValue=3;playingValue=0;
 auto offer=[&](){Offer(graph.data(),reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),reinterpret_cast<void*>(3),true);};
 offer();CHECK(offered&&c.liveActorClockAvailable&&verifiedOutputs==1);
 outputCount=2;offer();CHECK(offered&&verifiedOutputs==2); // two outputs can belong to one actor
 outputCount=4;offer();CHECK(offered&&verifiedOutputs==4);
 foreignIndex=1;offer();CHECK(!offered&&!c.liveActorClockAvailable);foreignIndex=-1;
 nonAnimationIndex=2;offer();CHECK(!offered);nonAnimationIndex=-1;
 invalidOutput=true;offer();CHECK(!offered);invalidOutput=false;
 publicThrows=true;offer();CHECK(!offered&&!c.liveActorClockNotice.empty());publicThrows=false;
 throwPastEnd=true;offer();CHECK(offered&&verifiedOutputs==4); // explicit internal end confirms bounds
 internalMissing=true;offer();CHECK(!offered);internalMissing=false;
 internalThrows=true;offer();CHECK(!offered);internalThrows=false;throwPastEnd=false;
 outputCount=33;offer();CHECK(!offered); // reject truncated enumeration
 outputCount=0;offer();CHECK(!offered);
 outputCount=2;offer();CHECK(offered&&c.liveActorClockNotice.find("不可用")==std::string::npos);
 CHECK(CaptureLoop(10,2.0)&&nodes.size()==2&&std::abs(loopWindow-1.85)<0.001);
 c.liveTakeoverActive=true;wrongJobType=true;
 CHECK(!Start(10,true) && !active && inputWeight==0.75f && isolationWrites==0);
 wrongJobType=false;failDisconnectReadback=true;
 CHECK(!Start(10,true) && active && playingValue==0 && !inputConnected);
 CHECK(RestoreGraph() && inputConnected && inputWeight==0.75f);
 offer();
 CHECK(Start(10,true)&&mmdSession && !inputConnected && inputIsolation.JobCount()==1);
 times[0]+=1.9;times[1]+=1.9;CHECK(TickLoop(11.9)&&loops==0&&nodeWrites==0);
 CHECK(TickLoop(200)&&loops==0&&nodeWrites==0); // no graph rewind even far beyond Live duration
 CHECK(RestoreGraph() && inputConnected && inputWeight==0.75f);
 offer();CHECK(Start(18,true) && !inputConnected);
 inputConnected=true;inputWeight=0.25f;CHECK(!TickLoop(18.5) && RestoreGraph() && inputWeight==0.75f);
 modeValue=1; playingValue=1; c.liveTakeoverActive=true; offer();
 const int borrowedWrites=writes;
 CHECK(offered && Start(19,true) && active && borrowedRunningGraph && writes==borrowedWrites && !inputConnected);
 CHECK(TickLoop(19.1) && RestoreGraph() && modeValue==1 && playingValue==1 && writes==borrowedWrites && inputConnected && inputWeight==0.75f);
 offer(); CHECK(Start(19.2,true) && borrowedRunningGraph);
 const int beforeIsolationLoss=isolationWrites;
 c.liveOwnerToken=99; CHECK(!RestoreGraph() && writes==borrowedWrites && isolationWrites==beforeIsolationLoss); c.liveOwnerToken=42;
 inputConnected=true;inputWeight=0.75f;
 c.liveTakeoverActive=false; offer(); CHECK(!offered); // Never borrow for the manual experiment.
 modeValue=3; playingValue=0;
 c.liveTakeoverActive=false;offer();failNodeRead=true;const int oldWrites=nodeWrites;
 CHECK(!CaptureLoop(20,1)&&nodeWrites==oldWrites);failNodeRead=false;
 CHECK(CaptureLoop(20,0.05)&&loopWindow==0.1);CHECK(Start(20));
 failNodeWrite=true;CHECK(!TickLoop(20.2));failNodeWrite=false;
 const int beforeLoss=nodeWrites;
 c.liveOwnerToken=99;CHECK(!TickLoop(21)&&nodeWrites==beforeLoss);CHECK(!RestoreGraph());c.liveOwnerToken=42;
 modeValue=3;playingValue=0;c.liveTakeoverActive=true;c.liveActorClockFault=false;
 auto originalActor=actor;
 LiveFollowerClock1::Offer(LiveFollowerClock1::graph.data(),reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),reinterpret_cast<void*>(3),true);
 CHECK(LiveFollowerClock1::offered && LiveFollowerClock1::actor==2 && actor==originalActor);
 CHECK(LiveFollowerClock1::Start(25,true) && LiveFollowerClock1::active && !LiveFollowerClock2::active && !active && !inputConnected);
 CHECK(LiveFollowerClock1::RestoreGraph() && modeValue==3 && playingValue==0 && inputConnected && inputWeight==0.75f);
 modeValue=3;playingValue=0;
 LiveFollowerClock2::Offer(LiveFollowerClock2::graph.data(),reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),reinterpret_cast<void*>(3),true);
 CHECK(LiveFollowerClock2::offered && LiveFollowerClock2::Start(26,true));
 c.liveOwnerToken=99;const int followerWrites=writes;
 CHECK(!LiveFollowerClock2::RestoreGraph() && writes==followerWrites && !LiveFollowerClock2::active);
 c.liveOwnerToken=42;
 modeValue=3; playingValue=0;
 LiveDynamicFollowerClock later;
 later.Offer(later.graph.data(),reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),reinterpret_cast<void*>(3),true);
 CHECK(later.offered && later.Start(27,true) && later.active && !LiveFollowerClock1::active);
 CHECK(later.RestoreGraph() && !later.active && modeValue==3 && playingValue==0);
 std::puts("actor clock MVP: gating, per-actor isolation, restoration and owner loss passed");return 0;
}};
int main(){ return ClockTest::Run(); }
