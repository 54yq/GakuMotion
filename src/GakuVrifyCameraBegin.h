#pragma once
#include "GakuPlugin.h"
#define GAKU_VRIFY_CAMERA_BEGIN_SERVICE "gaku.vrify.camera-begin"
#define GAKU_VRIFY_CAMERA_BEGIN_VERSION 1u
// Resident, single Motion observer. Targets 0=RenderPipeline, 1=Manager.
// Callback runs after the original, on source cameras only, even without XR.
// No trampoline crosses this interface. Unsubscribe closes new admission;
// already captured callbacks may finish while both DLLs remain resident.
typedef void (*GakuVrifyAfterCameraBegin)(void* userData, void* camera);
typedef struct GakuVrifyCameraBeginService {
    uint32_t abiVersion, structSize;
    void* context;
    void* (*getTarget)(void*, uint32_t);
    GakuStatus (*subscribe)(void*, GakuVrifyAfterCameraBegin, void*);
    GakuStatus (*unsubscribe)(void*, GakuVrifyAfterCameraBegin, void*);
} GakuVrifyCameraBeginService;
