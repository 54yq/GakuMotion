// SPDX-License-Identifier: MIT
#pragma once

#include <stdint.h>
#include "GakuPlugin.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GAKU_SERVICE_MOTION_CLOCK "gakumotion.clock/v1"
#define GAKU_MOTION_CLOCK_VERSION 1u

typedef enum GakuMotionRunMode {
    GAKUMOTION_RUN_REALTIME = 0,
    GAKUMOTION_RUN_RECORDING = 1
} GakuMotionRunMode;

typedef enum GakuMotionClockSource {
    GAKUMOTION_CLOCK_AUDIO = 0,
    GAKUMOTION_CLOCK_ENGINE_DELTA = 1,
    GAKUMOTION_CLOCK_FIXED_STEP = 2,
    GAKUMOTION_CLOCK_EXTERNAL = 3
} GakuMotionClockSource;

typedef struct GakuMotionClockSnapshotV1 {
    uint32_t abiVersion;
    uint32_t structSize;
    uint32_t runMode;
    uint32_t configuredSource;
    uint32_t effectiveSource;
    uint32_t playbackState;
    uint32_t fpsNumerator;
    uint32_t fpsDenominator;
    uint32_t waitingForRenderComplete;
    uint32_t hasAudio;
    double currentTimeSeconds;
    double timelineDurationSeconds;
    double lastStepDeltaSeconds;
    int64_t fixedFrameIndex;
    uint64_t leaseId;
    uint64_t evaluatedFrameSerial;
    float audioOffsetSeconds;
    float audioSpeed;
    float playbackSpeed;
    float musicVolume;
    char leaseOwner[64];
    char trackTitle[256];
    char audioFilePath[1024];
    char activeAudioEffectId[64];
} GakuMotionClockSnapshotV1;

typedef struct GakuMotionClockServiceV1 {
    uint32_t abiVersion;
    uint32_t structSize;
    void* context;
    GakuStatus (*getSnapshot)(void* context, GakuMotionClockSnapshotV1* output);
    GakuStatus (*setRunMode)(void* context, uint32_t runMode);
    GakuStatus (*configureClock)(void* context, uint32_t clockSource, uint32_t fpsNumerator, uint32_t fpsDenominator);
    GakuStatus (*acquireRecordingLease)(void* context, const char* ownerName, uint32_t fpsNumerator,
        uint32_t fpsDenominator, uint64_t* outLeaseId);
    GakuStatus (*releaseRecordingLease)(void* context, uint64_t leaseId);
    GakuStatus (*recordingBegin)(void* context, uint64_t leaseId, double startSeconds);
    GakuStatus (*recordingStop)(void* context, uint64_t leaseId);
    GakuStatus (*notifyFrameRendered)(void* context, uint64_t leaseId, uint64_t evaluatedFrameSerial,
        uint32_t advanceFrames);
    GakuStatus (*beginOffscreenCapture)(void* context);
    GakuStatus (*endOffscreenCapture)(void* context);
} GakuMotionClockServiceV1;

#ifdef __cplusplus
}
#endif
