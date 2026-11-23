#include "GkWebPanelBridge.hpp"
#include <cstdio>

using namespace GakuMotion;
using namespace GakuMotion::webpanel;

namespace GakuMotion::Log { void Write(Level, const char*, ...) {} }

int main()
{
	Core core;
	core.injectionSession = 12;
	core.liveOwnerToken = 19;
	core.liveObservation.confirmed = true;
	core.liveObservation.duration = 180;
	core.liveViewingConfirmed = true;
	core.liveTakeoverAvailable = true;
	MotionEntry track;
	track.name = "fixture";
	track.title = "Synthetic ensemble";
	track.libraryFolder = "synthetic/sources";
	track.externalFolder = true;
	core.motions.push_back(track);
	int primary{}, secondary{};
	core.player.controller.targetActor = &primary;
	core.sceneActorRegistry.Publish({{&primary, 41, "fktn", "Custom preset"}, {&secondary, 42, "ssmk", {}}});
	Bridge bridge(core, {});
	bridge.OnShow();

	const auto request = [&](const char* method, const std::string& params = "{}") {
		const auto text = bridge.HandleMessageForTest(std::string("{\"v\":1,\"id\":7,\"type\":\"request\",\"method\":\"") +
			method + "\",\"params\":" + params + "}");
		jsonlite::Value value;
		std::string error;
		if (!jsonlite::ParseText(text, value, error)) std::fprintf(stderr, "%s\n", error.c_str());
		return value;
	};
	const auto ok = [](const auto& reply) { return reply.BoolPath("ok", false); };
	int cases = 0, failures = 0;
	const auto check = [&](bool condition, const char* description) {
		++cases;
		if (!condition) { ++failures; std::fprintf(stderr, "%s\n", description); }
	};
	const auto queued = [&](Core::GuiAction action) {
		return std::any_of(core.guiCommands.begin(), core.guiCommands.end(),
			[action](const auto& command) { return command.action == action; });
	};

	const auto snapshot = request("fork.get");
	check(ok(snapshot) && snapshot.IntPath("result.owner", 0) == 19, "Live state exposes its opaque owner token");
	check(snapshot.StringPath("result.primaryPhysics", "") == "等待播放", "Primary physics waits for a runtime sample");
	core.player.state = PlaybackState::Playing;
	core.physicsMonitor.observed = true;
	core.physicsMonitor.actor = reinterpret_cast<uintptr_t>(&primary);
	core.physicsMonitor.session = core.injectionSession;
	core.physicsMonitor.requested = core.guiPhysicsBackend.load();
	core.physicsMonitor.actual = 2;
	core.physicsMonitorAt = std::chrono::steady_clock::now();
	check(request("fork.get").StringPath("result.primaryPhysics", "") == "原生物理运行中",
		"Primary physics reflects a current native sample");
	core.physicsMonitor.actual = 1;
	check(request("fork.get").StringPath("result.primaryPhysics", "") == "原生待确认，当前内建",
		"Primary physics reports the current fallback while native is pending");
	core.player.state = PlaybackState::Stopped;
	check(request("fork.get").StringPath("result.primaryPhysics", "") == "暂无当前数据",
		"Primary physics stops reporting a sample when playback stops");
	core.player.controller.currentBinding.actor = &primary;
	const auto state = request("state.get");
	check(state.StringPath("result.library.notice", "") == core.playlistNotice,
		"Upstream state snapshot remains available with the fork bridge");
	check(state.StringPath("result.actors.targetLabel", "") == "Custom preset · 1" &&
		request("telemetry.get").StringPath("result.actor.actorLabel", "") == "Custom preset · 1",
		"Actor roster and playback status use the same preset label");
	core.player.controller.currentBinding.actor = nullptr;
	check(!ok(request("fork.command", R"({"action":"live.takeover","session":12,"owner":18,"value":true})")) &&
		!core.liveTakeoverRequest, "A previous Live owner cannot start takeover");
	check(ok(request("fork.command", R"({"action":"live.takeover","session":12,"owner":19,"value":true})")) &&
		core.liveTakeoverRequest == 1 && core.liveTakeoverRequestOwner == 19, "Web starts takeover through the existing owner-scoped command");
	check(!ok(request("fork.command", R"({"action":"live.fixedActorLighting","session":12,"owner":19,"value":true})")),
		"Fixed actor lighting requires active takeover");
	core.liveTakeoverActive = true;
	check(!ok(request("fork.command", R"({"action":"live.fixedActorLighting","session":12,"owner":18,"value":true})")),
		"A stale Live owner cannot change the actor lighting");
	check(ok(request("fork.command", R"({"action":"live.fixedActorLighting","session":12,"owner":19,"value":true})")) &&
		request("fork.get").BoolPath("result.fixedActorLighting", false),
		"Web exposes the current Live actor lighting request");
	core.liveParaffinAvailable = true;
	check(!ok(request("fork.command", R"({"action":"live.paraffin","session":12,"owner":18,"value":false})")) &&
		core.liveParaffinEnabled, "A stale Live owner cannot toggle Paraffin");
	check(ok(request("fork.command", R"({"action":"live.paraffin","session":12,"owner":19,"value":false})")) &&
		!request("fork.get").BoolPath("result.livePostEffects.paraffinEnabled", true),
		"Paraffin can be disabled independently in the current Live");
	check(ok(request("fork.command", R"({"action":"live.depthOfField","session":12,"owner":19,"value":true})")) &&
		core.ShouldDisableLiveDof(), "Manual depth of field control works with native camera mode");
	core.camera.settings.enabled = true;
	core.camera.settings.mode = CameraMode::Track;
	check(ok(request("fork.command", R"({"action":"live.depthOfField","session":12,"owner":19,"value":false})")) &&
		!core.ShouldDisableLiveDof(), "Manual override can restore depth of field in VMD mode");
	check(ok(request("fork.command", R"({"action":"live.depthOfFieldAuto","session":12,"owner":19})")) &&
		core.ShouldDisableLiveDof() && core.liveDepthOfFieldOverride < 0, "Automatic VMD depth of field behavior can be restored");
	check(ok(request("fork.command", R"({"action":"live.actorLightingColor","session":12,"owner":19,"channel":"key","value":{"x":1,"y":0.5,"z":0.25}})")) &&
		core.actorLightingStyle.colors[0].y == .5f && core.actorLightingStyle.colors[1].y == 1.f,
		"Only the chosen lighting channel receives the custom color");
	check(ok(request("fork.command", R"({"action":"live.actorLightingWeight","session":12,"owner":19,"value":0.4})")) &&
		core.actorLightingStyle.weight == .4f, "Fixed lighting can be blended with native lighting");
	const auto defaults = core.actorLightingStrengths;
	check(defaults.key == .40f && defaults.fill == .035f && defaults.rim == .25f,
		"The new lighting defaults match the selected strengths");
	check(!ok(request("fork.command", R"({"action":"live.actorLightingSample","session":12,"owner":18})")) &&
		!core.actorLightingSampleActor, "A different Live cannot request lighting capture");
	check(ok(request("fork.command", R"({"action":"live.actorLightingSample","session":12,"owner":19})")) &&
		core.actorLightingSampleActor == core.sceneActorRegistry.Find(&primary)->token &&
		core.actorLightingSampleOwner == 19, "Lighting capture queues the selected primary identity");
	check(ok(request("fork.command", R"({"action":"live.actorLightingStrength","session":12,"owner":19,"channel":"rim","value":0})")) &&
		core.actorLightingStrengths.rim == 0 && core.actorLightingStrengths.key == defaults.key &&
		core.actorLightingStrengths.fill == defaults.fill, "Rim light can be turned off independently");
	for (const auto* channel : {"key", "fill", "rim"}) {
		const auto prefix = std::string(R"({"action":"live.actorLightingStrength","session":12,"owner":19,"channel":")") +
			channel + R"(","value":)";
		check(ok(request("fork.command", prefix + "10}")), "HDR intensity is accepted above the old limits");
		check(!ok(request("fork.command", prefix + "10.01}")), "The 1000 percent limit is enforced");
	}
	check(core.actorLightingStrengths == GakuMotion::Core::ActorLightingStrengths{10.f, 10.f, 10.f},
		"HDR intensities are stored for all three channels");
	check(ok(request("fork.command", R"({"action":"live.actorLightingDirection","session":12,"owner":19,"channel":"rim","axis":"x","value":-58})")) &&
		core.actorLightingStyle.rimAngle.x == -58 && core.actorLightingStyle.rimAngle.y == 15,
		"Rim direction edits only the selected axis");
	check(!ok(request("fork.command", R"({"action":"live.actorLightingDirection","session":12,"owner":19,"channel":"fill","axis":"x","value":42})")),
		"The additive fill channel has no direction");
	check(ok(request("fork.command", R"({"action":"live.actorLightingReset","session":12,"owner":19})")) &&
		core.actorLightingStrengths == defaults && !core.actorLightingSampleActor, "Reset restores strengths and cancels a pending capture");
	check(ok(request("fork.command", R"({"action":"live.fixedActorLighting","session":12,"owner":19,"value":false})")) &&
		!core.fixedActorLighting, "The current Live owner can clear the actor lighting request");
	check(ok(request("fork.command", R"({"action":"live.takeover","session":12,"owner":19,"value":false})")) &&
		core.liveTakeoverRequest == 2, "Web exits takeover through the existing restoration request");
	const auto epoch = std::to_string(core.sceneActorRegistry.Epoch());
	const auto token = std::to_string(core.sceneActorRegistry.Find(&primary)->token);
	const auto placement = R"(,"slot":-1,"actorToken":)" + token + R"(,"position":{"x":1.25,"y":0,"z":0},"yaw":24})";
	check(!ok(request("fork.command", R"({"action":"formation.place","session":11,"epoch":)" + epoch + placement)),
		"Placement rejects the previous scene session");
	check(ok(request("fork.command", R"({"action":"formation.place","session":12,"epoch":)" + epoch + placement)) &&
		queued(Core::GuiAction::InitialPosition) && queued(Core::GuiAction::InitialYaw),
		"Placement queues both position and yaw through the existing Core");
	check(ok(request("fork.command", R"({"action":"actor.hideModel","session":12,"epoch":)" + epoch +
		R"(,"actorToken":)" + token + R"(,"value":true})")) && queued(Core::GuiAction::SetActorModelHidden),
		"Model visibility uses the current recognized actor");
	core.guiCommands.clear();
	check(ok(request("fork.command", R"({"action":"formation.place","session":12,"epoch":)" + epoch +
		R"(,"slot":-1,"actorToken":)" + token + R"(,"yaw":-24})")) &&
		queued(Core::GuiAction::InitialYaw) && !queued(Core::GuiAction::InitialPosition),
		"Editing heading independently leaves XYZ alone");
	check(!ok(request("fork.command", R"({"action":"formation.save","session":12,"epoch":)" + epoch + R"(,"name":"old"})")),
		"The obsolete ensemble preset command is unavailable");
	GuiActorProp prop{core.sceneActorRegistry.Find(&primary)->token, "root", "mic", "primary", 82, true, true, false};
	core.actorProps.push_back(prop);
	check(ok(request("fork.command", R"({"action":"stage.prop","session":12,"epoch":)" + epoch +
		R"(,"actorToken":)" + token + R"(,"key":)" + JString(ActorPropKey(prop)) + R"(,"value":true})")) &&
		queued(Core::GuiAction::SetActorPropHidden), "Prop visibility uses the recognized prop identity");
	core.guiCommands.clear();
	GuiActorProp timeline{0, "Timeline", "Geo_baton_01", "舞台灯柱", 83, true, true, false, true};
	core.actorProps.push_back(timeline);
	std::vector<StageCandidate> stageNodes(4);
	stageNodes[0].instanceId = 80;
	stageNodes[1].instanceId = 83;
	stageNodes[1].parentIndex = 0;
	stageNodes[2].instanceId = 84;
	stageNodes[2].parentIndex = 1;
	stageNodes[3].instanceId = 85;
	stageNodes[3].parentIndex = 0;
	check(core.StagePropOwnership(stageNodes) == std::vector<unsigned char>{2, 1, 1, 0},
		"Fixed assets exclude moving subtrees and protect their shared ancestors");
	const auto propsReply = request("fork.get");
	const auto* result = propsReply.Find("result");
	const auto* props = result ? result->Find("props") : nullptr;
	check(props && props->arrayItems.size() == 2 && props->arrayItems[1].BoolPath("timeline", false),
		"Live Timeline props are distinguishable from actor-mounted props");
	const auto timelineToggle = R"(,"epoch":)" + epoch + R"(,"actorToken":0,"key":)" + JString(ActorPropKey(timeline));
	check(!ok(request("fork.command", R"({"action":"stage.prop","session":12,"owner":18,"value":true)" + timelineToggle + "}")) &&
		!queued(Core::GuiAction::SetActorPropHidden), "A previous Live owner cannot hide Timeline props");
	check(ok(request("fork.command", R"({"action":"stage.prop","session":12,"owner":19,"value":true)" + timelineToggle + "}")),
		"Timeline props can be hidden without an actor registry token");
	core.ProcessPendingGuiCommands();
	check(core.actorPropHidden.contains(ActorPropKey(timeline)) && core.actorPropScanRequested,
		"Timeline visibility is applied on the main-thread queue and requests a refresh");
	check(ok(request("fork.command", R"({"action":"stage.prop","session":12,"owner":19,"value":false)" + timelineToggle + "}")),
		"Timeline props can be restored");
	core.ProcessPendingGuiCommands();
	check(!core.actorPropHidden.contains(ActorPropKey(timeline)), "Restoring Timeline visibility clears the hidden key");
	core.liveTakeoverActive = false;
	core.actorPropScanRequested = false;
	check(!ok(request("fork.command", R"({"action":"stage.prop","session":12,"owner":19,"value":true)" + timelineToggle + "}")),
		"Timeline props are read-only outside takeover");
	check(!ok(request("fork.command", R"({"action":"stage.propsScan","session":12,"owner":18})")) && !core.actorPropScanRequested,
		"A previous Live owner cannot trigger a prop scan");
	check(ok(request("fork.command", R"({"action":"stage.propsScan","session":12,"owner":19})")) && core.actorPropScanRequested,
		"The current Live can request a prop scan before takeover");
	core.liveTakeoverActive = true;
	check(ok(request("fork.command", R"({"action":"motion.arm","session":12,"epoch":)" + epoch +
		R"(,"actorToken":)" + token + R"(,"operation":1,"value":43})")) &&
		queued(Core::GuiAction::SceneArm), "Advanced motion uses the existing actor calibration queue");
	check(ok(request("player.setSpeed", R"({"speed":1.25})")) && queued(Core::GuiAction::Speed),
		"The upstream Web transport uses the existing Core speed control");
	const auto libraryRevision = std::to_string(core.libraryRevision);
	const auto extraRole = std::to_string(core.sceneActorRegistry.Find(&secondary)->token);
	check(ok(request("fork.command", R"({"action":"formation.bind","session":12,"epoch":)" + epoch +
		R"(,"slot":3,"revision":)" + libraryRevision + R"(,"actorToken":)" + extraRole +
		R"(,"motionKey":"fixture","offsetSeconds":0})")) &&
		std::any_of(core.guiCommands.begin(), core.guiCommands.end(), [](const auto& command) {
			return command.action == Core::GuiAction::SetEnsembleFollower && command.candidateIndex == 3;
		}), "The fourth follower is queued without an upstream three-slot limit");
	check(ok(request("library.select", R"({"key":"fixture","autoplay":false,"revision":)" + libraryRevision + "}")),
		"The upstream Web library can select a track from an external folder");
	check(!ok(request("library.delete", R"({"key":"fixture","revision":)" + libraryRevision + "}")) &&
		!queued(Core::GuiAction::DeleteFiles), "The Web library cannot delete an imported external track");
	std::printf("Web/fork bridge: %d checks, %d failures\n", cases, failures);
	return failures ? 1 : 0;
}
