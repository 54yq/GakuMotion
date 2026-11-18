#pragma once

#include "GkMotionPlayer.hpp"
#include "test_waist_support.hpp"

namespace waistSupportIntegration
{
	using namespace GakuMotion;

	struct Node
	{
		std::string name;
		Node* parent{nullptr};
		std::vector<Node*> children;
		Vector3 position;
		Quaternion rotation;
		Vector3 scale{1, 1, 1};
		bool live{true};
	};

	struct Write
	{
		Node* node;
		char kind;
	};

	static std::array<Node, 16> nodes;
	static std::vector<Write> writes;
	static Node* liveHips = nullptr;
	static Node* liveLeftUpper = nullptr;
	static int staleCalls = 0;

	static Node* Read(void* pointer)
	{
		auto* node = static_cast<Node*>(pointer);
		if (!node->live) ++staleCalls;
		return node;
	}

	static Vector3 WorldPosition(Node* node);

	static Quaternion WorldRotation(Node* node)
	{
		Read(node);
		return node->parent ? (WorldRotation(node->parent) * node->rotation).Normalized() : node->rotation;
	}

	static Vector3 WorldPosition(Node* node)
	{
		Read(node);
		return node->parent
			       ? WorldPosition(node->parent) + WorldRotation(node->parent).Rotate(node->position)
			       : node->position;
	}

	static void GetWorldPosition(void* pointer, float* out)
	{
		const auto p = WorldPosition(Read(pointer));
		out[0] = p.x;
		out[1] = p.y;
		out[2] = p.z;
	}

	static void GetWorldRotation(void* pointer, float* out)
	{
		const auto q = WorldRotation(Read(pointer));
		out[0] = q.x;
		out[1] = q.y;
		out[2] = q.z;
		out[3] = q.w;
	}

	static void GetLocalPosition(void* pointer, float* out)
	{
		const auto p = Read(pointer)->position;
		out[0] = p.x;
		out[1] = p.y;
		out[2] = p.z;
	}

	static void GetLocalRotation(void* pointer, float* out)
	{
		const auto q = Read(pointer)->rotation;
		out[0] = q.x;
		out[1] = q.y;
		out[2] = q.z;
		out[3] = q.w;
	}

	static void GetScale(void* pointer, float* out)
	{
		const auto s = Read(pointer)->scale;
		out[0] = s.x;
		out[1] = s.y;
		out[2] = s.z;
	}

	static void SetLocalPosition(void* pointer, float x, float y, float z)
	{
		auto* node = Read(pointer);
		node->position = {x, y, z};
		writes.push_back({node, 'P'});
	}

	static void SetLocalRotation(void* pointer, float x, float y, float z, float w)
	{
		auto* node = Read(pointer);
		node->rotation = {x, y, z, w};
		writes.push_back({node, 'R'});
	}

	static void SetWorldPosition(void* pointer, float x, float y, float z)
	{
		auto* node = Read(pointer);
		const Vector3 p{x, y, z};
		node->position = node->parent
			                 ? WorldRotation(node->parent).Inverse().Rotate(p - WorldPosition(node->parent))
			                 : p;
		writes.push_back({node, 'W'});
	}

	static void* GetBone(void*, int hid)
	{
		return hid == 0 ? liveHips : hid == 1 ? liveLeftUpper : hid == 2 ? &nodes[7] : nullptr;
	}

	static int ChildCount(void* pointer) { return int(Read(pointer)->children.size()); }
	static void* Child(void* pointer, int index) { return Read(pointer)->children[size_t(index)]; }
	static std::string Name(void* pointer) { return Read(pointer)->name; }

	static size_t WritesTo(Node* node)
	{
		return size_t(std::count_if(writes.begin(), writes.end(), [node](const Write& w) { return w.node == node; }));
	}

	static void Parent(int child, int parent, const char* name, Vector3 position = {})
	{
		nodes[child].name = name;
		nodes[child].position = position;
		nodes[child].parent = &nodes[parent];
		nodes[parent].children.push_back(&nodes[child]);
	}

	static MotionClip Clip()
	{
		MotionClip clip;
		clip.format = MotionClip::Format::kGkClip;
		clip.version = 4;
		clip.total_frames = 2;
		clip.fps = 30;
		clip.duration_seconds = 1.f / 30;
		clip.bone_count = 3;
		// Deliberately use a non-HID order to exercise normal clip indexing.
		clip.bones = {{0, "Hips", false}, {2, "RightUpperLeg", false}, {1, "LeftUpperLeg", false}};
		clip.bone_parents = BuildBoneParentIndex(clip.bones);
		clip.bone_parent_hids = BuildBoneParentHids(clip.bones);
		clip.root_positions = {{0, 0, 0}, {0, 0, 0}};
		for (int frame = 0; frame < 2; ++frame)
		{
			clip.bone_rotations.push_back(Quaternion::Identity());
			clip.bone_rotations.push_back(Quaternion::FromEulerDegreesXYZ(35, -10, 15));
			clip.bone_rotations.push_back(Quaternion::FromEulerDegreesXYZ(35, 10, -15));
		}
		return clip;
	}

	static void Wire(IdolController& controller, const MotionClip& clip)
	{
		nodes = {};
		writes.clear();
		staleCalls = 0;
		nodes[0].name = "Actor";
		nodes[0].rotation = Quaternion::FromEulerDegreesXYZ(0, 70, 0);
		Parent(1, 0, "Hips", {0, .95f, 0});
		Parent(2, 1, "Pelvis");
		Parent(3, 2, "LeftUpLeg", {-.08f, -.05f, 0});
		Parent(4, 3, "LeftThigh_O", {0, -.25f, 0});
		Parent(5, 2, "LeftWaist_O", {-.1f, .08f, 0});
		Parent(6, 5, "LeftWaist_H", {0, -.14f, 0});
		Parent(7, 2, "RightUpLeg", {.08f, -.05f, 0});
		Parent(8, 7, "RightThigh_O", {0, -.25f, 0});
		Parent(9, 2, "RightWaist_O", {.1f, .08f, 0});
		Parent(10, 9, "RightWaist_H", {0, -.14f, 0});
		nodes[6].rotation = Quaternion::FromEulerDegreesXYZ(4, 0, 2);
		nodes[10].rotation = Quaternion::FromEulerDegreesXYZ(-3, 0, -2);
		Parent(14, 2, "LeftArm", {-.2f, .2f, 0});
		Parent(15, 14, "LeftArm_H");
		liveHips = &nodes[1];
		liveLeftUpper = &nodes[3];
		controller.fnGetBoneTransform = GetBone;
		controller.fnGetWorldPosition = GetWorldPosition;
		controller.fnGetWorldRotation = GetWorldRotation;
		controller.fnGetLocalPosition = GetLocalPosition;
		controller.fnGetLocalRotation = GetLocalRotation;
		controller.fnSetLocalPosition = SetLocalPosition;
		controller.fnSetLocalRotation = SetLocalRotation;
		controller.fnSetWorldPosition = SetWorldPosition;
		controller.fnGetChildCount = ChildCount;
		controller.fnGetChild = Child;
		controller.fnGetTransformName = Name;
		controller.fnGetLossyScale = GetScale;
		controller.probeFirstFrameDone = true;
		IdolBoneBinding binding;
		binding.actor = &nodes[0];
		binding.actorTransform = &nodes[0];
		binding.rootTransform = liveHips;
		binding.bindPoseCaptured = true;
		binding.boundBoneCount = 3;
		binding.restRootPos = {0, .95f, 0};
		binding.restActorRot = nodes[0].rotation;
		for (const auto& bone : clip.bones)
		{
			binding.boneTransforms.push_back(GetBone(binding.actor, bone.humanoid_id));
			binding.boneHids.push_back(bone.humanoid_id);
			binding.restLocals.push_back(Quaternion::Identity());
			binding.restRotations.push_back(Quaternion::Identity());
		}
		binding.leftArmAux.upperArm = &nodes[14];
		binding.leftArmAux.upperArmH = &nodes[15];
		controller.bindings[binding.actor] = binding;
		controller.currentBinding = binding;
		controller.SetTargetActor(binding.actor);
	}

	static void Apply(IdolController& controller, const MotionClip& clip)
	{
		controller.ApplyMotion(clip, 0, &nodes[0]);
	}
}

static void TestWaistSupportIntegration()
{
	using namespace GakuMotion;
	using namespace waistSupportTest;
	using namespace waistSupportIntegration;
	BeginTest("waist support default-on: completed pose, restoration, exact live chain, and scene discard");
	const auto clip = Clip();
	IdolController controller;
	Wire(controller, clip);
	const auto originalPosition = nodes[6].position;
	const auto originalRotation = nodes[6].rotation;
	controller.SetExperimentalWaistSupportEnabled(false);
	Apply(controller, clip);
	Check(!controller.IsExperimentalWaistSupportEnabled() && WritesTo(&nodes[6]) == 0 && WritesTo(&nodes[10]) == 0,
	      "explicitly disabled playback performs zero Waist_H reads or writes through the candidate");
	controller.SetExperimentalWaistSupportEnabled(true);
	writes.clear();
	Apply(controller, clip);
	waist_support::Result expected;
	const bool expectedValid = waist_support::Solve(WorldPosition(&nodes[5]), WorldRotation(&nodes[5]),
	                                                WorldPosition(&nodes[4]), expected);
	Check(expectedValid && Near(nodes[6].position, expected.localPosition) &&
	      SameRotation(nodes[6].rotation, expected.localRotation) && WritesTo(&nodes[6]) == 2 && WritesTo(&nodes[10]) ==
	      2,
	      "opt-in runtime writes both absolute helper components from the actual completed main pose");
	size_t finalMain = 0, armAux = 0, firstWaist = writes.size();
	for (size_t index = 0; index < writes.size(); ++index)
	{
		if (writes[index].node == &nodes[1] || writes[index].node == &nodes[3] || writes[index].node == &nodes[7])
			finalMain = index;
		if (writes[index].node == &nodes[15]) armAux = index;
		if ((writes[index].node == &nodes[6] || writes[index].node == &nodes[10]) && firstWaist == writes.size())
			firstWaist = index;
	}
	Check(firstWaist > finalMain && firstWaist > armAux,
	      "Waist_H applies after all principal bones and the arm auxiliary postpass");
	const auto firstPosition = nodes[6].position;
	const auto firstRotation = nodes[6].rotation;
	for (int frame = 0; frame < 20; ++frame) Apply(controller, clip);
	Check(Near(nodes[6].position, firstPosition) && SameRotation(nodes[6].rotation, firstRotation),
	      "repeated paused application recomputes an absolute pose without accumulation");
	controller.SetExperimentalWaistSupportEnabled(false);
	Check(Near(nodes[6].position, originalPosition) && SameRotation(nodes[6].rotation, originalRotation),
	      "disable restores the local position and rotation from before the first experimental write");
	writes.clear();
	Apply(controller, clip);
	Check(WritesTo(&nodes[6]) == 0 && WritesTo(&nodes[10]) == 0,
	      "disabled playback leaves restored helpers alone");

	controller.SetExperimentalWaistSupportEnabled(true);
	Apply(controller, clip);
	controller.InvalidateClipBindings();
	writes.clear();
	controller.BindActor(&nodes[0], clip);
	Check(Near(nodes[6].position, originalPosition) && SameRotation(nodes[6].rotation, originalRotation),
	      "explicit clip rebind restores the owned helper snapshot before rebuilding the binding");
	controller.probeFirstFrameDone = true; // This fake rig does not implement diagnostic limb geometry.
	Apply(controller, clip);
	controller.RestoreMute();
	Check(Near(nodes[6].position, originalPosition) && SameRotation(nodes[6].rotation, originalRotation),
	      "Stop restore path restores Waist_H even when no legacy animator mutation was required");

	MotionPlayer player;
	Wire(player.controller, clip);
	player.clip = clip;
	player.controller.SetExperimentalWaistSupportEnabled(true);
	Apply(player.controller, clip);
	player.Stop();
	Check(Near(nodes[6].position, originalPosition) && SameRotation(nodes[6].rotation, originalRotation),
	      "actual MotionPlayer::Stop reaches helper restoration without any scene dereference shortcut");

	IdolController invalidScale;
	Wire(invalidScale, clip);
	nodes[5].scale = {1.1f, 1, 1};
	invalidScale.SetExperimentalWaistSupportEnabled(true);
	Apply(invalidScale, clip);
	Check(WritesTo(&nodes[6]) == 0 && WritesTo(&nodes[10]) == 2,
	      "non-unit effective scale declines only the affected side");
	invalidScale.SetExperimentalWaistSupportEnabled(false);
	IdolController changedScale;
	Wire(changedScale, clip);
	changedScale.SetExperimentalWaistSupportEnabled(true);
	Apply(changedScale, clip);
	nodes[5].scale = {1.2f, 1, 1};
	writes.clear();
	Apply(changedScale, clip);
	Check(Near(nodes[6].position, originalPosition) && SameRotation(nodes[6].rotation, originalRotation) &&
	      WritesTo(&nodes[6]) == 2,
	      "scale becoming invalid restores the original local pose before declining the affected side");
	changedScale.SetExperimentalWaistSupportEnabled(false);
	IdolController disableScaled;
	Wire(disableScaled, clip);
	disableScaled.SetExperimentalWaistSupportEnabled(true);
	Apply(disableScaled, clip);
	nodes[5].scale = {1.2f, 1, 1};
	disableScaled.SetExperimentalWaistSupportEnabled(false);
	Check(Near(nodes[6].position, originalPosition) && SameRotation(nodes[6].rotation, originalRotation),
	      "disable restores captured LOCAL values even when current scale is outside solver scope");
	IdolController removedScaleApi;
	Wire(removedScaleApi, clip);
	removedScaleApi.SetExperimentalWaistSupportEnabled(true);
	Apply(removedScaleApi, clip);
	removedScaleApi.fnGetLossyScale = {};
	writes.clear();
	Apply(removedScaleApi, clip);
	Check(Near(nodes[6].position, originalPosition) && SameRotation(nodes[6].rotation, originalRotation) &&
	      WritesTo(&nodes[6]) == 2 && WritesTo(&nodes[10]) == 2,
	      "loss of a solver-only API restores both live snapshots without calling the missing API");
	removedScaleApi.SetExperimentalWaistSupportEnabled(false);
	IdolController stopWithoutWorldGetter;
	Wire(stopWithoutWorldGetter, clip);
	stopWithoutWorldGetter.SetExperimentalWaistSupportEnabled(true);
	Apply(stopWithoutWorldGetter, clip);
	stopWithoutWorldGetter.fnGetWorldRotation = nullptr;
	stopWithoutWorldGetter.RestoreMute();
	Check(Near(nodes[6].position, originalPosition) && SameRotation(nodes[6].rotation, originalRotation),
	      "Stop restoration needs no world-pose getter after current chain identity is validated");
	IdolController missingApi;
	Wire(missingApi, clip);
	missingApi.fnGetLossyScale = {};
	missingApi.SetExperimentalWaistSupportEnabled(true);
	Apply(missingApi, clip);
	Check(WritesTo(&nodes[6]) == 0 && WritesTo(&nodes[10]) == 0,
	      "unavailable required scale API disables the entire candidate");

	IdolController exactNames;
	Wire(exactNames, clip);
	nodes[5].name = "LeftWaist_O_extra";
	exactNames.SetExperimentalWaistSupportEnabled(true);
	Apply(exactNames, clip);
	Check(WritesTo(&nodes[6]) == 0 && WritesTo(&nodes[10]) == 2,
	      "substring-lookalike names cannot acquire a helper");
	exactNames.SetExperimentalWaistSupportEnabled(false);
	IdolController wrongBone;
	Wire(wrongBone, clip);
	liveLeftUpper = &nodes[12];
	wrongBone.SetExperimentalWaistSupportEnabled(true);
	Apply(wrongBone, clip);
	Check(WritesTo(&nodes[6]) == 0 && WritesTo(&nodes[10]) == 2,
	      "named Pelvis UpLeg must equal the current authoritative humanoid transform");
	wrongBone.SetExperimentalWaistSupportEnabled(false);

	IdolController replaced;
	Wire(replaced, clip);
	replaced.SetExperimentalWaistSupportEnabled(true);
	Apply(replaced, clip);
	nodes[11].name = "LeftWaist_H";
	nodes[11].parent = &nodes[5];
	nodes[11].position = {.01f, -.12f, .03f};
	nodes[11].rotation = Quaternion::FromEulerDegreesXYZ(7, 0, 3);
	const auto replacementPosition = nodes[11].position;
	const auto replacementRotation = nodes[11].rotation;
	nodes[5].children = {&nodes[11]};
	nodes[6].live = false;
	writes.clear();
	Apply(replaced, clip);
	replaced.SetExperimentalWaistSupportEnabled(false);
	Check(staleCalls == 0 && WritesTo(&nodes[6]) == 0 && WritesTo(&nodes[11]) == 4 &&
	      Near(nodes[11].position, replacementPosition) && SameRotation(nodes[11].rotation, replacementRotation),
	      "same-actor helper replacement reacquires and snapshots the new chain without touching the old pointer");

	IdolController referenceReplaced;
	Wire(referenceReplaced, clip);
	referenceReplaced.SetExperimentalWaistSupportEnabled(true);
	Apply(referenceReplaced, clip);
	nodes[12].name = "LeftThigh_O";
	nodes[12].parent = &nodes[3];
	nodes[12].position = {0, -.23f, .02f};
	nodes[3].children = {&nodes[12]};
	nodes[4].live = false;
	writes.clear();
	Apply(referenceReplaced, clip);
	referenceReplaced.SetExperimentalWaistSupportEnabled(false);
	Check(staleCalls == 0 && Near(nodes[6].position, originalPosition) && SameRotation(
		      nodes[6].rotation, originalRotation),
	      "reference-only replacement preserves the same helper's original snapshot instead of recapturing experimental output");
	IdolController referenceThenDisable;
	Wire(referenceThenDisable, clip);
	referenceThenDisable.SetExperimentalWaistSupportEnabled(true);
	Apply(referenceThenDisable, clip);
	nodes[12].name = "LeftThigh_O";
	nodes[12].parent = &nodes[3];
	nodes[12].position = {0, -.23f, .02f};
	nodes[3].children = {&nodes[12]};
	nodes[4].live = false;
	writes.clear();
	referenceThenDisable.SetExperimentalWaistSupportEnabled(false);
	Check(staleCalls == 0 && Near(nodes[6].position, originalPosition) && SameRotation(
		      nodes[6].rotation, originalRotation),
	      "disable after reference replacement restores only the freshly validated unchanged helper and parent");

	IdolController referenceAbsentDisable;
	Wire(referenceAbsentDisable, clip);
	referenceAbsentDisable.SetExperimentalWaistSupportEnabled(true);
	Apply(referenceAbsentDisable, clip);
	nodes[3].children.clear();
	nodes[4].live = false;
	writes.clear();
	referenceAbsentDisable.SetExperimentalWaistSupportEnabled(false);
	Check(staleCalls == 0 && WritesTo(&nodes[6]) == 2 && Near(nodes[6].position, originalPosition) &&
	      SameRotation(nodes[6].rotation, originalRotation),
	      "disable restores the unchanged live target when its solver Thigh_O reference has disappeared");

	IdolController referenceAbsentApply;
	Wire(referenceAbsentApply, clip);
	referenceAbsentApply.SetExperimentalWaistSupportEnabled(true);
	Apply(referenceAbsentApply, clip);
	nodes[3].children.clear();
	nodes[4].live = false;
	writes.clear();
	Apply(referenceAbsentApply, clip);
	Check(staleCalls == 0 && WritesTo(&nodes[6]) == 2 && Near(nodes[6].position, originalPosition) &&
	      SameRotation(nodes[6].rotation, originalRotation),
	      "reference loss during playback restores the original local target before declining the solver");
	nodes[4].live = true;
	nodes[3].children = {&nodes[4]};
	Apply(referenceAbsentApply, clip);
	referenceAbsentApply.SetExperimentalWaistSupportEnabled(false);
	Check(staleCalls == 0 && Near(nodes[6].position, originalPosition) && SameRotation(
		      nodes[6].rotation, originalRotation),
	      "a returning solver reference captures the restored target rather than the previous experimental output");

	IdolController authoritativeMismatch;
	Wire(authoritativeMismatch, clip);
	authoritativeMismatch.SetExperimentalWaistSupportEnabled(true);
	Apply(authoritativeMismatch, clip);
	liveLeftUpper = &nodes[12];
	writes.clear();
	Apply(authoritativeMismatch, clip);
	Check(staleCalls == 0 && WritesTo(&nodes[6]) == 2 && Near(nodes[6].position, originalPosition) &&
	      SameRotation(nodes[6].rotation, originalRotation),
	      "authoritative UpLeg mismatch declines solving but restores the independently validated local target");
	authoritativeMismatch.SetExperimentalWaistSupportEnabled(false);
	IdolController authoritativeMismatchDisable;
	Wire(authoritativeMismatchDisable, clip);
	authoritativeMismatchDisable.SetExperimentalWaistSupportEnabled(true);
	Apply(authoritativeMismatchDisable, clip);
	liveLeftUpper = &nodes[12];
	writes.clear();
	authoritativeMismatchDisable.SetExperimentalWaistSupportEnabled(false);
	Check(staleCalls == 0 && WritesTo(&nodes[6]) == 2 && Near(nodes[6].position, originalPosition) &&
	      SameRotation(nodes[6].rotation, originalRotation),
	      "direct disable after authoritative UpLeg mismatch requires only the unchanged live target chain");

	MotionPlayer referenceGoneStop;
	Wire(referenceGoneStop.controller, clip);
	referenceGoneStop.clip = clip;
	referenceGoneStop.controller.SetExperimentalWaistSupportEnabled(true);
	Apply(referenceGoneStop.controller, clip);
	nodes[3].children.clear();
	nodes[4].live = false;
	writes.clear();
	referenceGoneStop.Stop();
	Check(staleCalls == 0 && WritesTo(&nodes[6]) == 2 && Near(nodes[6].position, originalPosition) &&
	      SameRotation(nodes[6].rotation, originalRotation),
	      "MotionPlayer Stop restores the exact live helper even when the solver reference is gone");

	IdolController hipsChanged;
	Wire(hipsChanged, clip);
	hipsChanged.SetExperimentalWaistSupportEnabled(true);
	Apply(hipsChanged, clip);
	nodes[13].name = "Hips";
	liveHips = &nodes[13];
	nodes[1].live = false;
	writes.clear();
	hipsChanged.SetExperimentalWaistSupportEnabled(false);
	Check(staleCalls == 0 && writes.empty(),
	      "restoration after Hips identity replacement never dereferences the old hierarchy");
	IdolController discarded;
	Wire(discarded, clip);
	discarded.SetExperimentalWaistSupportEnabled(true);
	Apply(discarded, clip);
	for (auto& node : nodes) node.live = false;
	writes.clear();
	discarded.DiscardBindings();
	discarded.SetExperimentalWaistSupportEnabled(false);
	discarded.RestoreMute();
	Check(staleCalls == 0 && writes.empty() && discarded.bindings.empty() && !discarded.currentBinding.actor,
	      "scene discard drops tokens and snapshots without reads, restores, or stale writes");
}
