// SPDX-License-Identifier: MIT
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "GakuPlugin.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GAKU_SERVICE_SCREENSHOT_CAPTURE "screenshotmanager.capture/v1"
#define GAKU_SCREENSHOT_VERSION 1u

typedef enum GakuScreenshotAlphaMode {
    GAKU_SCREENSHOT_ALPHA_AUTO_GREENSCREEN = 0,
    GAKU_SCREENSHOT_ALPHA_FORCE_TRANSPARENT = 1,
    GAKU_SCREENSHOT_ALPHA_OPAQUE = 2
} GakuScreenshotAlphaMode;

typedef enum GakuScreenshotImageFormat {
    GAKU_IMAGE_PNG = 0,
    GAKU_IMAGE_JPEG = 1
} GakuScreenshotImageFormat;

typedef enum GakuScreenshotPixelFormat {
    GAKU_PIXEL_RGBA8_SRGB_TOPDOWN = 0,
    GAKU_PIXEL_BGRA8_SRGB_TOPDOWN = 1
} GakuScreenshotPixelFormat;

typedef enum GakuScreenshotCaptureState {
    GAKU_CAPTURE_PENDING = 0,
    GAKU_CAPTURE_SUCCEEDED = 1,
    GAKU_CAPTURE_FAILED = 2
} GakuScreenshotCaptureState;

typedef struct GakuScreenshotCapabilitiesV1 {
    uint32_t abiVersion;
    uint32_t structSize;
    uint32_t runtimeReady;
    uint32_t cameraAvailable;
    uint32_t greenScreenDetected;
    uint32_t maxDimension;
    uint32_t windowWidth;
    uint32_t windowHeight;
} GakuScreenshotCapabilitiesV1;

typedef struct GakuScreenshotFileRequestV1 {
    uint32_t abiVersion;
    uint32_t structSize;
    uint32_t width;
    uint32_t height;
    uint32_t imageFormat;
    uint32_t jpegQuality;
    uint32_t alphaMode;
    uint32_t reserved;
    char outputPathUtf8[1024];
} GakuScreenshotFileRequestV1;

typedef struct GakuScreenshotResultV1 {
    uint32_t abiVersion;
    uint32_t structSize;
    uint32_t state;
    uint32_t width;
    uint32_t height;
    uint32_t hasAlpha;
    int32_t errorCode;
    char savedPathUtf8[1024];
    char errorMessage[256];
} GakuScreenshotResultV1;

typedef struct GakuScreenshotFrameRequestV1 {
    uint32_t abiVersion;
    uint32_t structSize;
    uint32_t width;
    uint32_t height;
    uint32_t pixelFormat;
    uint32_t alphaMode;
} GakuScreenshotFrameRequestV1;

typedef struct GakuScreenshotFrameInfoV1 {
    uint32_t abiVersion;
    uint32_t structSize;
    uint32_t width;
    uint32_t height;
    uint32_t rowPitch;
    uint32_t hasAlpha;
    uint64_t unityFrame;
} GakuScreenshotFrameInfoV1;

typedef struct GakuScreenshotServiceV1 {
    uint32_t abiVersion;
    uint32_t structSize;
    void* context;
    GakuStatus (*getCapabilities)(void* context, GakuScreenshotCapabilitiesV1* output);
    GakuStatus (*requestFileCapture)(void* context, const GakuScreenshotFileRequestV1* request, uint64_t* outTicket);
    GakuStatus (*pollCapture)(void* context, uint64_t ticket, GakuScreenshotResultV1* output);
    GakuStatus (*releaseTicket)(void* context, uint64_t ticket);
    GakuStatus (*captureFrameImmediate)(void* context, const GakuScreenshotFrameRequestV1* request,
        void* outPixels, size_t capacity, GakuScreenshotFrameInfoV1* outInfo,
        char* errorBuffer, uint32_t errorCapacity);
} GakuScreenshotServiceV1;

#ifdef __cplusplus
}
#endif
