#pragma once
#include <cstdint>
struct GakuFrameContextV1 {
    uint32_t version, size, liveConfirmed, livePaused, takeover, boundFollowers;
    float liveTime, motionTime;
    uint32_t physicsActual, physicsRequested;
    uint64_t physicsDispatchNs, physicsNativeNs, physicsSolverNs, physicsFailures, physicsExpired, physicsIdentitySkips;
};
using GakuMotionGetFrameContextFn = int (*)(GakuFrameContextV1*, uint32_t);
