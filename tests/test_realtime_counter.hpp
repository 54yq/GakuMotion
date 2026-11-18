#include "GkRealtimeCounter.hpp"
#include <limits>
namespace RealtimeCounterHarness {inline void Test(){
 using StageRuntimeHarness::Check;GakuMotion::RealtimeCounter m;
 m.Sample(100,true,0);Check(m.known&&m.total==0&&m.Age(0)<0,"live meter starts with no fabricated callback");
 m.Sample(110,true,1);Check(m.total==10&&m.rate==10&&m.Age(1)==0,"live counter continues beyond snapshot window");
 m.Sample(110,true,2);Check(m.rate==0&&m.Age(2)==1,"paused counter rate falls to zero and age advances");
 m.Sample(111,true,2.1);m.Sample(111,true,2.2);Check(m.Age(2.2)>0.09,"unchanged polls do not refresh last increment age");
 m.Sample(200,true,3);Check(m.total==100,"live total is not capped at 64 samples");
 m.Sample(3,true,4);Check(m.total==0&&m.rate==0,"counter reset cannot underflow delta");
 m.Sample(5,false,5);Check(!m.known&&m.Age(5)<0,"missing counter is unknown rather than zero");
 m.Sample(50,true,6);Check(m.total==0&&m.rate==0,"reconnected counter rebases without a false spike");
 m.Sample(51,true,std::numeric_limits<double>::quiet_NaN());Check(!m.known,"invalid clock cannot appear fresh");
 m.Reset();Check(!m.known&&!m.initialized,"scene identity reset clears telemetry");
}}
