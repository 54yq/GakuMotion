// SPDX-License-Identifier: MIT
#pragma once

#include <stdint.h>
#include "GakuPlugin.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GAKU_SERVICE_MOTION_STAGE_STATE "gakumotion.stage-state/v1"
#define GAKU_MOTION_STAGE_STATE_VERSION 1u

typedef struct GakuMotionStageStateV1 {
    uint32_t abiVersion;
    uint32_t structSize;
    uint32_t sceneAllowed;
    uint32_t injectionActive;
    uint32_t greenScreenActive;
    uint32_t overrideCameraClear;
    uint32_t hideBackground;
    uint32_t reserved;
    float greenScreenColor[4];
} GakuMotionStageStateV1;

typedef struct GakuMotionStageStateServiceV1 {
    uint32_t abiVersion;
    uint32_t structSize;
    void* context;
    GakuStatus (*getState)(void* context, GakuMotionStageStateV1* output);
} GakuMotionStageStateServiceV1;

#ifdef __cplusplus
}
#endif
