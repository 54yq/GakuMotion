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
	core.sceneActorRegistry.Publish({{&primary, 41, "fktn", {}}, {&secondary, 42, "ssmk", {}}});
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
	check(request("state.get").StringPath("result.library.notice", "") == core.playlistNotice,
		"Upstream state snapshot remains available with the fork bridge");
	check(!ok(request("fork.command", R"({"action":"live.takeover","session":12,"owner":18,"value":true})")) &&
		!core.liveTakeoverRequest, "A previous Live owner cannot start takeover");
	check(ok(request("fork.command", R"({"action":"live.takeover","session":12,"owner":19,"value":true})")) &&
		core.liveTakeoverRequest == 1 && core.liveTakeoverRequestOwner == 19, "Web starts takeover through the existing owner-scoped command");
	core.liveTakeoverActive = true;
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
