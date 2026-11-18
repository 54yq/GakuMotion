#pragma once
#include <stdint.h>
// Read-only observer ABI, shared verbatim with Recorder. Unity main thread only.
#define GAKU_PHYSICS_DIAGNOSTICS_VERSION 1u
using GakuPhysicsDiagnosticSnapshot = struct GakuPhysicsDiagnosticSnapshot
{
	uint32_t version, size, requested_backend, actual_backend;
	uint32_t bridge_state, solved_bones, bound_bones, bridge_fault;
	uint64_t actor, sequence;
	int32_t unity_frame, playback_state;
	float clip_time;
	uint32_t reserved;
	uint64_t body_calls, body_total_ns;
	uint64_t dispatch_calls, dispatch_total_ns, dispatch_last_ns, dispatch_max_ns;
	uint64_t fallback_calls, fallback_total_ns, fallback_clamps, fallback_resets;
	uint64_t bridge_publish_calls, bridge_publish_total_ns;
	uint64_t bridge_native_calls, bridge_native_total_ns;
	uint64_t native_solver_calls, native_solver_total_ns;
	uint64_t bridge_failures, bridge_expired_skips, bridge_identity_skips;
};
using GakuMotionGetPhysicsDiagnosticsFn = int(*)(void* actor, GakuPhysicsDiagnosticSnapshot* out, uint32_t size);
