#pragma once
#include "GakuPlugin.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GAKU_SERVICE_FILE_DROP "gakuloader.file-drop"
#define GAKU_FILE_DROP_VERSION 1u
#define GAKU_FILE_DROP_MAX_FILES 256u
#define GAKU_FILE_DROP_MAX_BYTES (1024u * 1024u)

typedef struct GakuFileDropTarget {
    uint32_t abiVersion; uint32_t structSize;
    float left, top, right, bottom; /* ImGui viewport coordinates: host client pixels (single viewport). */
} GakuFileDropTarget;

typedef struct GakuFileDropResult {
    uint32_t abiVersion; uint32_t structSize;
    uint32_t fileCount; uint32_t requiredBytes;
} GakuFileDropResult;

/* Optional extension; frozen GakuPlugin.h service layouts stay unchanged.
   Both calls are valid only inside this owner's onDesktopUi callback.
   Targets expire each completed UI frame; NULL disables the target and clears
   its pending batch. The host routes drops only to the selected panel's target.
   poll(0,NULL) peeks metadata. A sufficient buffer copies fileCount consecutive
   NUL-terminated UTF-8 paths and consumes the batch exactly once. Empty returns
   GAKU_OK with zero counts. Short buffers return GAKU_E_SIZE without consuming.
   One batch, at most MAX_FILES / MAX_BYTES, is retained; overflow is rejected. */
typedef struct GakuFileDropService {
    uint32_t abiVersion; uint32_t structSize;
    void* context;
    GakuStatus (*setTarget)(void* context, const GakuFileDropTarget* target);
    GakuStatus (*poll)(void* context, uint32_t resultSize, GakuFileDropResult* result,
        uint32_t pathCapacity, char* utf8Paths);
} GakuFileDropService;

#ifdef __cplusplus
}
#endif
