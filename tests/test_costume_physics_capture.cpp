#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "GkCostumePhysicsCapture.hpp"
#include "GkPhysicsMonitor.hpp"
#include <cassert>
#include <iostream>

using namespace GakuMotion;

struct Joint
{
	const char* name;
	Vector3 position{};
	Quaternion rotation{Quaternion::Identity()};
	std::vector<Joint*> children{};
};

struct Rig
{
	Joint tip{"Skirt1_S_End", {0, .10f, 0}}, skirt{"Skirt1_S", {}, Quaternion::Identity(), {&tip}};
	Joint hairTip{"Hair1_S_End", {0, .15f, 0}}, hair{"Hair1_S", {}, Quaternion::Identity(), {&hairTip}};
	Joint root{"Root", {}, Quaternion::Identity(), {&skirt, &hair}};
	IdolBoneBinding binding;
	IdolController controller;
	FallbackPhysics solver;

	Rig()
	{
		binding.actor = binding.actorTransform = &root;
		binding.bindPoseCaptured = true;
		controller.fnGetWorldPosition = [](void* node, float* out) {
			const auto p = static_cast<Joint*>(node)->position;
			out[0] = p.x; out[1] = p.y; out[2] = p.z;
		};
		controller.fnGetWorldRotation = controller.fnGetLocalRotation = [](void* node, float* out) {
			const auto q = static_cast<Joint*>(node)->rotation;
			out[0] = q.x; out[1] = q.y; out[2] = q.z; out[3] = q.w;
		};
		controller.fnSetLocalRotation = [](void* node, float x, float y, float z, float w) {
			static_cast<Joint*>(node)->rotation = Quaternion(x, y, z, w);
		};
		controller.fnGetChildCount = [](void* node) { return static_cast<int>(static_cast<Joint*>(node)->children.size()); };
		controller.fnGetChild = [](void* node, int i) -> void* { return static_cast<Joint*>(node)->children[i]; };
		controller.fnGetTransformName = [](void* node) { return std::string(static_cast<Joint*>(node)->name); };
	}
};

int main()
{
	using Frame = FallbackPhysics::CostumeFrame;
	Rig sampled, control;
	Frame first;
	assert(sampled.solver.Publish(sampled.binding, sampled.controller, 0.f, 1, &first));
	assert(control.solver.Publish(control.binding, control.controller, 0.f, 1));
	assert(first.garments == 1 && first.solved == 1 && first.initialResets == 1 && first.timeReset && first.rebound);
	assert(first.maxima[Frame::Acceleration] < 0.f);

	const Quaternion external = Quaternion(.15f, 0.f, 0.f, .988686f).Normalized();
	sampled.skirt.rotation = control.skirt.rotation = external;
	sampled.skirt.position = control.skirt.position = {.03f, 0.f, 0.f};
	Frame second;
	assert(sampled.solver.Publish(sampled.binding, sampled.controller, .02f, 2, &second));
	assert(control.solver.Publish(control.binding, control.controller, .02f, 2));
	assert(second.garments == 1 && !second.rebound && !second.timeReset && second.substeps == 3);
	assert(std::fabs(second.maxima[Frame::Anchor] - .03f) < .0001f);
	assert(second.maxima[Frame::External] > 15.f && second.maxima[Frame::Acceleration] > 3.f);
	assert(second.peaks[Frame::Acceleration].bone == "Skirt1_S");
	const auto a = sampled.skirt.rotation;
	const auto b = control.skirt.rotation;
	assert(a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w);
	assert(sampled.solver.GetStats().clamps == control.solver.GetStats().clamps);

	CostumePhysicsCapture capture;
	const auto tick = CostumePhysicsCapture::Clock::now().time_since_epoch().count();
	const auto path = std::filesystem::current_path() / ("synthetic_costume_" + std::to_string(tick) + ".jsonl");
	assert(capture.Open(path, "synthetic \"costume\"", 42));
	capture.Record(2, sampled.binding.actor, 2, 103.126f, .02f, 1, 0, false, sampled.solver, second);

	Frame jumped;
	sampled.skirt.position = {1.f, 0.f, 0.f};
	assert(sampled.solver.Publish(sampled.binding, sampled.controller, .02f, 3, &jumped));
	assert(jumped.poseResets == 1 && !jumped.timeReset && jumped.maxima[Frame::Acceleration] < 0.f);
	capture.Record(2, sampled.binding.actor, 3, 103.126f, .02f, 2, 0, false, sampled.solver, jumped);
	assert(capture.Close("synthetic") && capture.Frames() == 2);
	assert(!capture.Open(path, "must not overwrite", 43));

	Rig handoff;
	assert(handoff.solver.Publish(handoff.binding, handoff.controller, 0.f, 1));
	handoff.skirt.position = handoff.hair.position = {.03f, 0.f, 0.f};
	assert(handoff.solver.Publish(handoff.binding, handoff.controller, .02f, 2));
	handoff.skirt.rotation = external;
	const auto released = handoff.solver.ReleaseToNative(handoff.binding, handoff.controller);
	assert(released.restored == 1 && released.preserved == 1 && released.unreadable == 0);
	assert(handoff.skirt.rotation.x == external.x && handoff.skirt.rotation.w == external.w);
	assert(handoff.hair.rotation.x == 0 && handoff.hair.rotation.z == 0 && handoff.hair.rotation.w == 1);
	assert(!handoff.solver.HasCaptureWrites());

	Rig stale;
	assert(stale.solver.Publish(stale.binding, stale.controller, 0.f, 1));
	stale.skirt.rotation = external;
	auto otherBinding = stale.binding;
	otherBinding.actor = &handoff.root;
	const auto discarded = stale.solver.ReleaseToNative(otherBinding, stale.controller);
	assert(!discarded.restored && !discarded.preserved && !discarded.unreadable);
	assert(stale.skirt.rotation.x == external.x && stale.skirt.rotation.w == external.w);

	// A scrub-sized anchor jump can drive the old pending fallback straight to
	// the cloth swing limit on an otherwise valid forearm sleeve chain.
	Rig oldPending;
	oldPending.skirt.name = "RightForeArmSleeve1_S";
	oldPending.tip.name = "RightForeArmSleeve2_S";
	assert(oldPending.solver.Publish(oldPending.binding, oldPending.controller, 0.f, 1));
	oldPending.skirt.position.x = .12f;
	assert(oldPending.solver.Publish(oldPending.binding, oldPending.controller, 1.f / 240.f, 2));
	const float degrees = 2.f * std::acos(std::fabs(oldPending.skirt.rotation.w)) * 180.f / 3.14159265f;
	assert(std::fabs(degrees - 25.f) < .01f && oldPending.solver.GetStats().clamps != 0);

	Rig livePending;
	livePending.skirt.name = oldPending.skirt.name;
	livePending.tip.name = oldPending.tip.name;
	livePending.skirt.rotation = external;
	for (const char* reason : {"native_waiting_for_playback", "native_pending", "native_pending_stale",
	                          "body_binding_pending", "discovering"})
	{
		const int actual = SelectSecondaryPhysicsBackend(2, false, true, reason);
		assert(actual == 0);
		if (actual == 1) livePending.solver.Publish(livePending.binding, livePending.controller, .02f, 2);
	}
	assert(!livePending.solver.HasCaptureWrites());
	assert(livePending.skirt.rotation.x == external.x && livePending.skirt.rotation.w == external.w);
	assert(SelectSecondaryPhysicsBackend(2, true, true, "native") == 2);
	assert(SelectSecondaryPhysicsBackend(2, false, true, "companion_faulted") == 1);
	assert(SelectSecondaryPhysicsBackend(2, false, true, "companion_missing") == 1);
	assert(SelectSecondaryPhysicsBackend(2, false, false, "native_pending") == 1);
	assert(SelectSecondaryPhysicsBackend(1, false, true, "fallback") == 1);
	assert(SelectSecondaryPhysicsBackend(0, false, true, "off") == 0);
	std::cout << "Costume capture contracts passed. Trace: " << path.filename().string() << '\n';
}
