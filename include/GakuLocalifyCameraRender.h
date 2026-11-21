#pragma once
#include "GakuPlugin.h"

#define GAKULOCALIFY_SERVICE_CAMERA_RENDER "gakulocalify.camera-render"
#define GAKULOCALIFY_CAMERA_RENDER_VERSION 1u
enum GakuLocalifyCameraRenderKind {
    GAKULOCALIFY_CAMERA_RENDER_MAIN = 0,
    GAKULOCALIFY_CAMERA_RENDER_PUSH = 1,
    GAKULOCALIFY_CAMERA_RENDER_END = 2,
    GAKULOCALIFY_CAMERA_RENDER_COUNT = 3
};

#ifdef __cplusplus
extern "C" {
#endif
/* Finite bridge for three concrete Localify-owned camera/render entries.
   Functions and userData must remain resident until process exit. Callbacks
   run without the provider lock. Unsubscribe/close prevent new snapshots, not
   calls using an already captured snapshot. No hot unloading or drain. */
typedef struct GakuLocalifyCameraRenderCallbacks {
    uint32_t abiVersion;
    uint32_t structSize;
    void* userData;
    uint32_t (*isActive)(void* userData);
    void (*onMainCamera)(void* userData, void** camera);
    void (*beforePush)(void* userData, void* self, void* state);
    void (*afterPush)(void* userData, void* self);
    void (*afterEndCamera)(void* userData, void* camera);
} GakuLocalifyCameraRenderCallbacks;

typedef struct GakuLocalifyCameraRenderService {
    uint32_t abiVersion;
    uint32_t structSize;
    void* context;
    /* NULL until the matching concrete detour has installed successfully. */
    void* (*getTarget)(void* context, uint32_t kind);
    /* One observer. All callbacks/userData are required. An identical repeat
       is idempotent; any different binding conflicts until unsubscribed. */
    GakuStatus (*subscribe)(void* context, const GakuLocalifyCameraRenderCallbacks* callbacks);
    GakuStatus (*unsubscribe)(void* context, void* userData);
} GakuLocalifyCameraRenderService;
#ifdef __cplusplus
}
#endif
