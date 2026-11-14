#pragma once
#include <cmath>
#include <cstdint>
#include <chrono>
namespace GakuMotion {
// A live cumulative counter, not a bounded trace buffer. Ages refer to observed
// increments at the poll resolution, not exact engine callback timestamps.
struct RealtimeCounter {
 bool known{},initialized{}; uint64_t total{},origin{},previous{},observed{};
 double rate{},window{},lastChange{-1};
 void Reset(){*this={};lastChange=-1;}
 void Sample(uint64_t count,bool available,double now){
  if(!available||!std::isfinite(now)){known=false;initialized=false;rate=0;return;}
  known=true;
  if(!initialized||count<observed||now<window){initialized=true;origin=previous=observed=count;window=now;total=0;rate=0;lastChange=-1;return;}
  total=count-origin;
  if(count>observed)lastChange=now;
  observed=count;
  const double elapsed=now-window;
  if(elapsed>=0.25){rate=double(count-previous)/elapsed;previous=count;window=now;}
 }
 double Age(double now)const{return known&&lastChange>=0&&now>=lastChange?now-lastChange:-1;}
};
}
