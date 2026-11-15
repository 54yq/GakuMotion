#pragma once
#include <atomic>
#include <cstdint>
namespace GakuMotion::visual_handoff {
inline std::atomic_bool requested{false};
inline std::atomic_bool released{false};
inline uint32_t Request(uint32_t value) {
    const bool next=value!=0;
    const bool previous=requested.exchange(next,std::memory_order_acq_rel);
    if(previous!=next)released.store(false,std::memory_order_release);
    return 1;
}
inline uint32_t IsReleased() {return requested.load(std::memory_order_acquire)&&released.load(std::memory_order_acquire)?1u:0u;}
inline bool Requested(){return requested.load(std::memory_order_acquire);}
}
