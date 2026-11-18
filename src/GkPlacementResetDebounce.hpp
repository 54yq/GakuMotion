#pragma once
#include <unordered_map>
#include <cstdint>
namespace GakuMotion {
// Actor identity and timestamp are supplied by the Unity main thread.
struct PlacementResetDebounce {
 struct Entry { const void* actor{}; uint64_t epoch{}, generation{}; float x{},y{},z{}; double changed{}; bool initialized{}, pending{}; };
 std::unordered_map<unsigned,Entry> entries{};
 void Clear() { entries.clear(); }
 bool Observe(unsigned slot,const void* actor,uint64_t epoch,uint64_t generation,float x,float y,float z,double now) {
  if(slot>=64 || !actor) return false;
  auto& e=entries[slot];
  if(!e.initialized || e.actor!=actor || e.epoch!=epoch || e.generation!=generation) {e={actor,epoch,generation,x,y,z,now,true,false}; return false;}
  if(e.x!=x || e.y!=y || e.z!=z) {e.x=x;e.y=y;e.z=z;e.changed=now;e.pending=true;return false;}
  if(e.pending && now>=e.changed && now-e.changed>=0.5) {e.pending=false;return true;}
  return false;
 }
};
}
