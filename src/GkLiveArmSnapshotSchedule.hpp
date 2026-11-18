#pragma once
#include <cstdint>
#include <unordered_map>
namespace GakuMotion::live_arm_snapshot
{
    struct Schedule {
        uintptr_t owner{}; int scene{}; std::unordered_map<void*,double> next;
        bool Due(uintptr_t currentOwner, int currentScene, void* actor, double now) {
            if(!currentOwner || !currentScene || !actor)return false;
            if(owner!=currentOwner || scene!=currentScene){owner=currentOwner;scene=currentScene;next.clear();}
            const auto it=next.find(actor);
            if(it==next.end() && next.size()>=16)return false;
            if(it!=next.end() && now<it->second)return false;
            next[actor]=now+1.0;return true;
        }
    };
}
