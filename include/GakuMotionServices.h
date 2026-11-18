#pragma once
#include <stdint.h>

// Provider-owned domain contract, queried through GML's service registry.
#define GAKUMOTION_SERVICE_PLAYBACK_STATE "gakumotion.playback-state"
#define GAKUMOTION_PLAYBACK_SERVICE_VERSION 1u
typedef struct GakuMotionPlaybackService {
    uint32_t abiVersion;
    uint32_t structSize;
    // Thread-safe read; 1 only while custom motion is actively playing.
    // Module/function remains resident; returns 0 after plugin shutdown.
    uint32_t (*isPlaying)(void);
} GakuMotionPlaybackService;
