#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include "GakuPlugin.h"

// Separate from resource handoff so either feature can be switched off alone.
namespace GakuMotion::camera_handoff {
inline std::atomic_bool requested{false},released{false};
inline std::atomic_bool nativeAbi{false};
using WriteCallback=void(*)(void*,uint32_t,void*);
inline std::mutex writeMutex;
inline WriteCallback writeCallback{};
inline void* writeUser{};
inline std::atomic<void*> transformTarget{nullptr},cameraTarget{nullptr};
inline uint32_t Request(uint32_t value){const bool next=value!=0;const bool prev=requested.exchange(next,std::memory_order_acq_rel);if(prev!=next)released.store(false,std::memory_order_release);return 1;}
inline uint32_t IsReleased(){return requested.load(std::memory_order_acquire)&&released.load(std::memory_order_acquire)?1u:0u;}
inline bool Requested(){return requested.load(std::memory_order_acquire);}
inline uint32_t NativeAbi(){return nativeAbi.load(std::memory_order_acquire)?1u:0u;}
inline GakuStatus Subscribe(WriteCallback callback,void* user){
    if(!callback||!user)return GAKU_E_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(writeMutex);
    if(!IsReleased()||!NativeAbi())return GAKU_E_UNAVAILABLE;
    if(writeCallback&&(writeCallback!=callback||writeUser!=user))return GAKU_E_CONFLICT;
    writeCallback=callback;writeUser=user;return GAKU_OK;
}
inline GakuStatus Unsubscribe(WriteCallback callback,void* user){
    std::lock_guard<std::mutex> lock(writeMutex);
    if(!writeCallback)return GAKU_OK;
    if(writeCallback!=callback||writeUser!=user)return GAKU_E_CONFLICT;
    transformTarget.store(nullptr,std::memory_order_release);cameraTarget.store(nullptr,std::memory_order_release);
    writeCallback=nullptr;writeUser=nullptr;return GAKU_OK;
}
inline GakuStatus SetTargets(void* user,void* transform,void* camera){
    std::lock_guard<std::mutex> lock(writeMutex);
    if(!writeCallback||writeUser!=user)return GAKU_E_CONFLICT;
    transformTarget.store(transform,std::memory_order_release);cameraTarget.store(camera,std::memory_order_release);return GAKU_OK;
}
inline void Notify(uint32_t kind,void* target){
    if(!target||!IsReleased())return;
    if(kind>1||target!=(kind==0?transformTarget.load(std::memory_order_acquire):cameraTarget.load(std::memory_order_acquire)))return;
    WriteCallback callback{};void* user{};
    {std::lock_guard<std::mutex> lock(writeMutex);callback=writeCallback;user=writeUser;}
    if(callback)callback(user,kind,target);
}
}
