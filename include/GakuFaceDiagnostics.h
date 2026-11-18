#pragma once
#include <stdint.h>

// Optional observer ABI, shared verbatim with GakuRecorder. Unity main thread only.
#define GAKU_FACE_DIAGNOSTICS_VERSION 1u
#define GAKU_FACE_DIAGNOSTICS_CAPACITY 1024u
#define GAKU_FACE_NAME_BYTES 128u

enum GakuFaceDiagnosticStatus
{
	GAKU_FACE_PENDING = 0, GAKU_FACE_API_MISSING = 1,
	GAKU_FACE_MODEL_MISSING = 2, GAKU_FACE_COUNT_INVALID = 3,
	GAKU_FACE_BOUND = 4
};

using GakuFaceChannelDiagnostic = struct GakuFaceChannelDiagnostic
{
	char name[GAKU_FACE_NAME_BYTES];
	int32_t index; // -1 means no name match; readback values are unavailable.
	float target, before, after; // Actual setter target and raw readbacks, in native percentage units.
};
using GakuFaceDiagnosticSnapshot = struct GakuFaceDiagnosticSnapshot
{
	uint32_t version, size, status, channel_count;
	uint64_t sequence, actor, model, qpc_ticks;
	float clip_time;
	int32_t playback_state, unity_frame;
	GakuFaceChannelDiagnostic channels[GAKU_FACE_DIAGNOSTICS_CAPACITY];
};
// Calling renews a short observer lease; no game state is changed by this call.
using GakuMotionGetFaceDiagnosticsFn = int(*)(void* actor, GakuFaceDiagnosticSnapshot* out, uint32_t size);
