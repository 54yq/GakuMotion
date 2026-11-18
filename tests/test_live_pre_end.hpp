#include "GkLivePreEndLoop.hpp"
namespace LivePreEndHarness {
inline void Test(){
 using namespace GakuMotion::live_pre_end;using StageRuntimeHarness::Check;
 Loop loop;Sample s{1,2,true,true,false,false,148.9f,150,1,10};
 Check(loop.Step(s)==Action::None,"loop does not jump before threshold");
 s.time=149;Check(loop.Step(s)==Action::SeekStart,"loop dispatches at configured threshold");
 s.now=10.1;Check(loop.Step(s)==Action::None,"pending seek does not dispatch twice");
 s.time=0.02f;Check(loop.Step(s)==Action::Confirmed&&loop.Cycles()==1,"loop confirms observed start");
 s.now=11;Check(loop.Step(s)==Action::None,"loop stays at opening after confirmation");
 s.time=149;s.paused=true;Check(loop.Step(s)==Action::None,"pause prevents automatic jump");
 s.paused=false;s.blocked=true;Check(loop.Step(s)==Action::None,"capture takeover and manual operations block loop");
 s.blocked=false;Check(loop.Step(s)==Action::SeekStart,"loop resumes after block");
 s.now=14.1;Check(loop.Step(s)==Action::Failed,"unconfirmed seek times out");
 loop.Reset();s.now=20;s.lead=0.5f;s.time=149.49f;Check(loop.Step(s)==Action::None,"half-second setting preserves threshold");
 s.time=149.5f;Check(loop.Step(s)==Action::SeekStart,"half-second setting dispatches on threshold");
 s.owner=3;s.time=0.1f;Check(loop.Step(s)==Action::None&&loop.Cycles()==0,"scene owner change cannot confirm old seek");
 s.enabled=false;s.time=149.5f;Check(loop.Step(s)==Action::None,"disabled loop cannot seek");
 s.enabled=true;s.lead=0.1f;Check(loop.Step(s)==Action::Failed,"invalid lead fails closed");
 s.lead=1;s.duration=1;Check(loop.Step(s)==Action::Failed,"short invalid timeline cannot seek repeatedly");
}
}
