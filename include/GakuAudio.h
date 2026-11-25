// SPDX-License-Identifier: MIT
#pragma once

#include <stdint.h>
#include "GakuPlugin.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GAKU_SERVICE_AUDIO_CORE "gakuaudio.core/v1"
#define GAKU_AUDIO_VERSION 1u

typedef enum GakuAudioPlaybackState {
    GAKU_AUDIO_STOPPED = 0,
    GAKU_AUDIO_STARTING = 1,
    GAKU_AUDIO_PLAYING = 2,
    GAKU_AUDIO_PAUSED = 3,
    GAKU_AUDIO_FAILED = 4
} GakuAudioPlaybackState;

typedef struct GakuAudioObservationV1 {
    uint32_t abiVersion;
    uint32_t structSize;
    uint32_t state;
    uint32_t sourceOpen;
    uint32_t confirmed;
    int32_t errorCode;
    uint64_t sourceEpoch;
    uint32_t sampleRate;
    uint32_t channels;
    double positionSeconds;
    double durationSeconds;
} GakuAudioObservationV1;

typedef struct GakuAudioMetadataV1 {
    uint32_t abiVersion;
    uint32_t structSize;
    uint32_t valid;
    uint32_t sampleRate;
    uint32_t channels;
    uint32_t hasCover;
    uint32_t coverByteSize;
    uint32_t reserved;
    double durationSeconds;
    char codec[32];
    char title[256];
    char artist[256];
    char album[256];
    char coverMime[32];
} GakuAudioMetadataV1;

typedef struct GakuAudioEffectPresetV1 {
    uint32_t abiVersion;
    uint32_t structSize;
    char id[64];
    char name[64];
    char category[32];
    char description[192];
} GakuAudioEffectPresetV1;

typedef struct GakuAudioOfflineRenderRequestV1 {
    uint32_t abiVersion;
    uint32_t structSize;
    const char* inputPathUtf8;
    const char* outputWavPathUtf8;
    const char* effectPresetId;
    double trimOffsetSeconds;
    double timelineDurationSeconds;
    float audioSpeed;
    float playbackSpeed;
    float volume;
    uint32_t targetSampleRate;
    uint32_t reserved;
} GakuAudioOfflineRenderRequestV1;

typedef struct GakuAudioServiceV1 {
    uint32_t abiVersion;
    uint32_t structSize;
    void* context;
    GakuStatus (*openAndPlay)(void* context, const char* pathUtf8, double startSeconds);
    GakuStatus (*pause)(void* context);
    GakuStatus (*resumeAt)(void* context, double seconds);
    GakuStatus (*seek)(void* context, double seconds, uint32_t whilePlaying);
    GakuStatus (*stop)(void* context);
    GakuStatus (*observe)(void* context, GakuAudioObservationV1* output);
    GakuStatus (*setVolume)(void* context, float volume);
    GakuStatus (*setPlaybackRate)(void* context, float rate);
    GakuStatus (*probeFile)(void* context, const char* pathUtf8, GakuAudioMetadataV1* output);
    GakuStatus (*extractCover)(void* context, const char* pathUtf8, void* outBuffer,
        uint32_t bufferCapacity, uint32_t* outBytes, char* outMime32);
    uint32_t (*getPresetCount)(void* context);
    GakuStatus (*getPreset)(void* context, uint32_t index, GakuAudioEffectPresetV1* output);
    GakuStatus (*getActivePreset)(void* context, char* outId, uint32_t capacity);
    GakuStatus (*setActivePreset)(void* context, const char* presetId);
    GakuStatus (*renderOfflineWav)(void* context, const GakuAudioOfflineRenderRequestV1* request,
        char* errorBuffer, uint32_t errorCapacity);
} GakuAudioServiceV1;

#ifdef __cplusplus
}
#endif
