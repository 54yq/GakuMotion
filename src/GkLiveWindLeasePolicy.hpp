#pragma once
#include <cmath>
#include <cstdint>
namespace GakuMotion::live_wind
{
    struct Values { float power{}, fluctuation{}; };
    inline bool Finite(Values v) { return std::isfinite(v.power) && std::isfinite(v.fluctuation); }
    struct Lease {
        uintptr_t owner{}; int scene{}, instance{}; Values saved{}; bool active{};
        bool Begin(uintptr_t o, int s, int id, Values v) {
            if (active || !o || !s || !id || !Finite(v)) return false;
            owner=o; scene=s; instance=id; saved=v; active=true; return true;
        }
        bool Matches(uintptr_t o, int s, int id) const { return active && owner==o && scene==s && instance==id; }
        bool OwnerLost(uintptr_t confirmedOwner, int confirmedScene, bool sceneUnloaded) const {
            return active && (sceneUnloaded ||
                (confirmedOwner && confirmedScene && (owner != confirmedOwner || scene != confirmedScene)));
        }
        void Observe(Values v) {
            // A nonzero value is a later write by the game. Preserve it for restoration.
            if (Finite(v)) { if (v.power != 0) saved.power=v.power; if (v.fluctuation != 0) saved.fluctuation=v.fluctuation; }
        }
        Values RestoreValues(Values current) const {
            return {current.power==0 ? saved.power : current.power,
                    current.fluctuation==0 ? saved.fluctuation : current.fluctuation};
        }
        void Clear() { *this={}; }
    };
}
