#pragma once

#include <stdint.h>
#include "GakuPlugin.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GAKU_WEB_PANEL_VERSION 1u
#define GAKU_WEB_PANEL_SERVICE_SUFFIX ".web-panel/v1"

typedef struct GakuWebPanelAssetsV1 {
    uint32_t abiVersion;
    uint32_t structSize;
    const wchar_t* assetsDir;
    const wchar_t* entryPath;
    const wchar_t* virtualHost;
} GakuWebPanelAssetsV1;

typedef void (*GakuWebPanelEventSink)(void* userData, const char* json);

typedef struct GakuWebPanelServiceV1 {
    uint32_t abiVersion;
    uint32_t structSize;
    void* context;
    GakuStatus (*getAssets)(void* context, GakuWebPanelAssetsV1* output);
    void (*onShow)(void* context);
    void (*onHide)(void* context);
    void (*onUiTick)(void* context);
    void (*setEventSink)(void* context, GakuWebPanelEventSink sink, void* userData);
    GakuStatus (*handleMessage)(void* context, const char* requestJson,
        uint32_t responseCapacity, char* responseJson, uint32_t* requiredBytes);
    void (*onShutdown)(void* context);
} GakuWebPanelServiceV1;

#ifdef __cplusplus
}
#endif
