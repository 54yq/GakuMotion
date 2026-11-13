#pragma once
/* rinha.facial-fit v1 — the process-wide facial fit table published by
   RinhaReplacement and consumed by GakuMotion.

   Contract rules, identical to every other GakuLoader service table:
     - the table starts with GakuServiceHeader and is copied by value;
     - every pointer below stays valid until process exit;
     - evaluation is a pure function of the supplied inputs, takes no locks,
       allocates nothing and is safe on the Unity main thread;
     - a face pointer returned by findFace/faceAt is immutable and may be
       cached by the consumer for the lifetime of the process.

   This header is duplicated byte-for-byte in both repositories; the
   check_contract_parity tool refuses a release where they diverge. */
#include "GakuPlugin.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RINHA_FACIAL_FIT_SERVICE "rinha.facial-fit"
#define RINHA_FACIAL_FIT_VERSION 1u

#define RINHA_FACIAL_FIT_MOUTH_SHAPES 9
#define RINHA_FACIAL_FIT_EYE_AMPLITUDES 5
#define RINHA_FACIAL_FIT_MAX_KEYS 64
#define RINHA_FACIAL_FIT_MAX_CHANNELS 64
#define RINHA_FACIAL_FIT_NAME_BYTES 64

/* Grade 0 means "fitted but not accepted by the bundle's own measurements".
   A consumer must keep such a component opt-in and say so in its UI. */
typedef struct RinhaFacialFitFace {
    const char* faceName;   /* exact face prefab identity, e.g. mdl_chr_krnh-base-0000_face */
    const char* character;  /* four character code, e.g. krnh */
    uint32_t mouthPresent;
    uint32_t eyesPresent;
    uint32_t mouthGrade;
    uint32_t eyesGrade;
    uint32_t mouthKnots;
    uint32_t eyeKeyCount;      /* <= RINHA_FACIAL_FIT_MAX_KEYS */
    uint32_t eyeChannelCount;  /* <= RINHA_FACIAL_FIT_MAX_CHANNELS */
    const char* const* mouthShapeNames;  /* RINHA_FACIAL_FIT_MOUTH_SHAPES native channels */
    const char* const* eyeKeyNames;      /* eyeKeyCount NFKC source morph names */
    const char* const* eyeChannelNames;  /* eyeChannelCount native channels */
    const uint8_t* eyeKeyGrades;         /* eyeKeyCount, 1 = measured single-morph support */
    const float* eyeKeyAmplitudes;       /* RINHA_FACIAL_FIT_EYE_AMPLITUDES training amplitudes */
} RinhaFacialFitFace;

typedef struct RinhaFacialFitService {
    uint32_t abiVersion;
    uint32_t structSize;
    void* context;
    /* Number of fitted faces; 0 means the provider loaded no table. */
    uint32_t (*faceCount)(void* context);
    /* Lifetime-stable face record, or NULL for an out-of-range index. */
    const RinhaFacialFitFace* (*faceAt)(void* context, uint32_t index);
    /* Exact identity match, or NULL when this face has no fitted table. */
    const RinhaFacialFitFace* (*findFace)(void* context, const char* faceName);
    /* Five source vowel weights -> RINHA_FACIAL_FIT_MOUTH_SHAPES native weights.
       Refuses (GAKU_E_INVALID_ARGUMENT) a non-finite or out-of-range input,
       (GAKU_E_LIMIT) an input outside the fitted convex hull and
       (GAKU_E_UNAVAILABLE) a face without an inverse-distance mouth table. On
       refusal the output buffer is untouched. */
    GakuStatus (*evaluateMouth)(void* context, const RinhaFacialFitFace* face,
        const float* vowels, float* outWeights);
    /* eyeKeyCount source amplitudes (profile order) -> eyeChannelCount native
       weights. Refuses (GAKU_E_INVALID_ARGUMENT) a non-finite or out-of-range
       amplitude. On refusal the output buffer is untouched. */
    GakuStatus (*evaluateEyes)(void* context, const RinhaFacialFitFace* face,
        const float* keyAmplitudes, float* outWeights);
} RinhaFacialFitService;

#ifdef __cplusplus
}
#endif
