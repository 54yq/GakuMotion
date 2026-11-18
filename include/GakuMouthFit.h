#pragma once
#include "GakuPlugin.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GAKU_MOUTH_FIT_SERVICE "chara-adapter.mouth-fit"
#define GAKU_MOUTH_FIT_VERSION 1u
#define GAKU_MOUTH_FIT_MAX_INPUTS 16u
#define GAKU_MOUTH_FIT_MAX_OUTPUTS 32u
#define GAKU_MOUTH_FIT_MAX_KNOTS 512u

typedef struct GakuMouthFitFace {
    const char* faceName;
    const char* character;
    uint32_t inputCount;
    uint32_t outputCount;
    uint32_t knotCount;
    uint32_t qualityGrade;
    const char* const* inputNames;
    const char* const* outputNames;
    const char* tableSha256;
} GakuMouthFitFace;

typedef struct GakuMouthFitService {
    uint32_t abiVersion;
    uint32_t structSize;
    void* context;
    const GakuMouthFitFace* (*findFace)(void* context, const char* faceName);
    GakuStatus (*evaluate)(void* context, const GakuMouthFitFace* face,
        const float* input, uint32_t inputCount, float* output, uint32_t outputCount);
} GakuMouthFitService;

#ifdef __cplusplus
}
#endif
