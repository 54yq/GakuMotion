#pragma once
#include "GakuPlugin.h"

/* Finite Localify camera coexistence service. Localify owns these four
   Transform detours; a subscriber observes writes AFTER the original call.
   Callbacks and targets remain resident until process exit (no hot unload).
   The raw self ABI is established by matching the exact installed icall target,
   not by assuming a managed object or exposing a generic hook chain. */
#define GAKULOCALIFY_SERVICE_CAMERA_WRITE "gakulocalify.camera-write"
#define GAKULOCALIFY_CAMERA_WRITE_VERSION 1u

enum GakuLocalifyCameraWriteKind {
    GAKULOCALIFY_CAMERA_WORLD_POSITION = 0,
    GAKULOCALIFY_CAMERA_WORLD_ROTATION = 1,
    GAKULOCALIFY_CAMERA_WORLD_POSE = 2,
    GAKULOCALIFY_CAMERA_LOCAL_POSITION = 3,
    GAKULOCALIFY_CAMERA_WRITE_COUNT = 4
};

#ifdef __cplusplus
extern "C" {
#endif
typedef void (*GakuLocalifyCameraWriteFn)(void* userData, uint32_t kind, void* transformSelf);
typedef struct GakuLocalifyCameraWriteService {
    uint32_t abiVersion;
    uint32_t structSize;
    void* context;
    /* NULL until this specific detour is successfully installed. */
    void* (*getTarget)(void* context, uint32_t kind);
    /* One observer. Repeating the same binding succeeds; others conflict.
       A notification snapshots the binding, then calls it without a lock.
       Unsubscribe prevents new snapshots, not an already in-flight callback. */
    GakuStatus (*subscribe)(void* context, GakuLocalifyCameraWriteFn callback, void* userData);
    GakuStatus (*unsubscribe)(void* context, GakuLocalifyCameraWriteFn callback, void* userData);
} GakuLocalifyCameraWriteService;
#ifdef __cplusplus
}
#endif
