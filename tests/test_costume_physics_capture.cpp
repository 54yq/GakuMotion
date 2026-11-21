#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "GkCostumePhysicsCapture.hpp"
#include "GkNativeSwingRenderFilter.hpp"
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
    native_swing::CameraPair pair;
    int mainCamera{}, overlayCamera{};
    // 7435 is the unpaired camera-begin from the existing Live trace.
    assert(!pair.Begin(&mainCamera,7435));
    assert(!pair.End(&overlayCamera,7435));
    assert(!pair.End(&mainCamera,7436));
    assert(pair.pending == &mainCamera);
    pair = {};
    assert(!pair.Begin(&mainCamera,7436));
    assert(pair.End(&mainCamera,7436));
    assert(pair.Begin(&mainCamera,7437));
    assert(!pair.Begin(&mainCamera,7437));
    assert(!pair.End(&overlayCamera,7437));
    assert(pair.End(&mainCamera,7437));
    assert(!pair.End(&mainCamera,7437));
    assert(!pair.Begin(&overlayCamera,7438));
    assert(pair.End(&overlayCamera,7438));
    assert(!pair.Begin(&overlayCamera,7440)); // Missing prior frame
    assert(pair.End(&overlayCamera,7440));

    native_swing::RotationSmoother filter;
    const auto rotation = [](float degrees) {
        const float half = degrees * .00872664626f;
        return Quaternion(0,0,std::sin(half),std::cos(half));
    };
    Quaternion smoothed{};
    float center{};
    assert(!filter.Push(0.f,rotation(1.f),smoothed,center));
    for (unsigned i=1;i<80;++i) {
        const float time = float(i)/60.f;
        const float offset = i%2 ? -1.f : 1.f;
        assert(filter.Push(time,rotation(time*50.f + offset),smoothed,center));
        if (i>=6) {
            assert(std::fabs(center - (time-.05f)) < .00001f);
            assert(native_swing::Angle(rotation(center*50.f),smoothed) < .14f);
        }
    }
    filter.Reset();
    assert(!filter.Push(80.f,rotation(16.f),smoothed,center));
    assert(filter.Push(80.017f,rotation(16.f),smoothed,center));
    assert(native_swing::Angle(rotation(16.f),smoothed) < .1f);
    const auto hairB = rotation(16.f);
    const auto hair = rotation(15.5f);

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
	sampled.skirt.position = {};
	const auto beforeSample = sampled.skirt.rotation;
	capture.RecordActorPose("after_plugin_pose", sampled.binding, sampled.controller, 3, 103.126f, 2, 2, 2, .02f);
	assert(sampled.skirt.rotation.x == beforeSample.x && sampled.skirt.rotation.w == beforeSample.w);
    GbbSwingStateSample swing{};
    swing.source_frame = 2;
    swing.before_count = swing.after_count = 1;
    swing.delta_time = .02f;
    swing.local_rotation.w = 1.f;
    swing.before[0].hit_checks = 255;
    swing.before[0].child_speed = {1.f, 2.f, 3.f};
    swing.after[0] = swing.before[0];
    capture.RecordSwingState("LeftFrontHair1_S", sampled.binding, 3, 13.157f, swing);
    swing.before[0].hit_checks = INT32_MIN;
    swing.after[0].use_limit = INT32_MIN+1;
    capture.RecordSwingState("RigntBackCloth1_S", sampled.binding, 3, 13.157f, swing);
    capture.RecordSwingRender("LeftFrontHair1_S", sampled.binding.actor, 7893, 13.161f,
        13.144f, hairB, hair, hairB, "camera_end", true);
    capture.RecordSwingRenderSummary(sampled.binding.actor,7893,13.161f,15,15,15,"camera_end");
	assert(capture.Close("synthetic") && capture.Frames() == 3);
	{
		std::ifstream recorded(path);
		const std::string contents((std::istreambuf_iterator<char>(recorded)), {});
		assert(contents.find("\"event\":\"actor_pose\"") != std::string::npos);
		assert(contents.find("\"source_frame\":2,\"native_dt_s\":0.02") != std::string::npos);
		const auto actorPose = contents.substr(contents.find("\"event\":\"actor_pose\""));
		assert(actorPose.find("Skirt1_S") != std::string::npos && actorPose.find("Hair1_S") != std::string::npos);
        assert(contents.find("\"restore_phase\":\"camera_end\",\"restored\":true") != std::string::npos);
        const auto hair = contents.find("\"event\":\"swing_state\",\"bone\":\"LeftFrontHair1_S\"");
        const auto cloth = contents.find("\"event\":\"swing_state\",\"bone\":\"RigntBackCloth1_S\"");
        assert(hair < cloth && cloth != std::string::npos);
        assert(contents.substr(hair, cloth - hair).find("\"hit_checks\":255") != std::string::npos);
        const auto clothState = contents.substr(cloth);
        assert(clothState.find("\"hit_checks\":null") != std::string::npos);
        assert(clothState.find("\"child_speed\":[1,2,3]") != std::string::npos);
        assert(clothState.find("\"field_errors\":{\"hit_checks\":\"layout_unavailable\"}") != std::string::npos);
        assert(clothState.find("\"field_errors\":{\"use_limit\":\"read_fault\"}") != std::string::npos);
	}
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
