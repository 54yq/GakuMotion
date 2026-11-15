// Synthetic headless contract tests for gakumotion.web-panel/v1.
// No game, no real user assets: a local Core plus fake host hooks drive the
// bridge exactly like the Loader's UI thread would. Wire shapes mirror
// GakuLoader frontend/src/motion/protocol.ts (v1).
//
// Build/run: tests/run_web_panel_tests.ps1 (MSVC, /std:c++20 /utf-8).

#include "GkWebPanelBridge.hpp"

#include <cstdio>
#include <future>
#include <memory>
#include <string>
#include <vector>

using namespace GakuMotion;
using namespace GakuMotion::webpanel;

// The offline binary links this TU alone; provide the log facade symbol.
namespace GakuMotion::Log
{
	void Write(Level, const char*, ...) {}
}

static int g_pass = 0;
static int g_fail = 0;
static std::string g_current;

static void Check(bool condition, const std::string& what)
{
	if (condition) ++g_pass;
	else
	{
		++g_fail;
		std::printf("  [FAIL] %s :: %s\n", g_current.c_str(), what.c_str());
	}
}

static void BeginTest(const char* name)
{
	g_current = name;
	std::printf("[TEST] %s\n", name);
}

static jsonlite::Value ParseJson(const std::string& text)
{
	jsonlite::Value value;
	std::string error;
	if (!jsonlite::ParseText(text, value, error))
		std::printf("  [WARN] parse error: %s\n", error.c_str());
	return value;
}

// -----------------------------------------------------------------------------
// Fixture: fresh Core + bridge + captured event sink per test group.
// -----------------------------------------------------------------------------
struct Fixture
{
	Core core;
	std::vector<std::string> events;
	std::unique_ptr<Bridge> bridge;

	Fixture()
	{
		core.enforceInjectionPolicy = true;
		core.injectionPolicy.SetSceneAllowed(true);
		core.injectionPolicy.SetCaptureHooksReady(true);
		core.motionsRoot = "synthetic/motions";
		HostHooks hooks;
		hooks.releaseFileDialog = [] {};
		hooks.pluginVersion = "0.1.1-test";
		bridge = std::make_unique<Bridge>(core, hooks);
		bridge->SetEventSink(
			[](void* userData, const char* json) {
				static_cast<std::vector<std::string>*>(userData)->push_back(json);
			},
			&events);
		bridge->OnShow();
	}

	std::string Req(const std::string& method, const std::string& params = "{}")
	{
		return bridge->HandleMessageForTest(
			"{\"v\":1,\"id\":7,\"type\":\"request\",\"method\":\"" + method + "\",\"params\":" + params + "}");
	}

	jsonlite::Value ReqJson(const std::string& method, const std::string& params = "{}")
	{
		return ParseJson(Req(method, params));
	}

	bool Ok(const std::string& response)
	{
		const auto value = ParseJson(response);
		const auto* ok = value.Find("ok");
		return ok && ok->IsBool() && ok->boolValue;
	}

	std::string ErrorCode(const std::string& response)
	{
		const auto value = ParseJson(response);
		const auto* error = value.Find("error");
		const auto* code = error ? error->Find("code") : nullptr;
		return code && code->IsString() ? code->stringValue : "";
	}

	const Core::GuiCommand* FindCommand(Core::GuiAction action) const
	{
		for (const auto& command : core.guiCommands)
			if (command.action == action) return &command;
		return nullptr;
	}

	void ClearCommands() { core.guiCommands.clear(); }

	int CountEvents(const std::string& name) const
	{
		int count = 0;
		for (const auto& json : events)
		{
			const auto value = ParseJson(json);
			const auto* eventName = value.Find("name");
			if (eventName && eventName->IsString() && eventName->stringValue == name) ++count;
		}
		return count;
	}

	void LoadSyntheticClip()
	{
		core.player.clip.total_frames = 120;
		core.player.clip.fps = 30.0f;
		core.player.clip.duration_seconds = 4.0f;
		core.player.loadedMotionFile = "synthetic.gkmotion";
		core.loadedMotionKey = "synthetic";
		core.loadedMotionGeneration = 3;
		core.injectionSession = 2;
	}

	void AddSyntheticLibrary()
	{
		MotionEntry first;
		first.name = "alpha";
		first.dir = "synthetic/motions/alpha";
		first.title = "Alpha";
		first.durationSeconds = 4.0f;
		first.totalFrames = 120;
		MotionEntry second = first;
		second.name = "beta";
		second.dir = "synthetic/motions/beta";
		second.title = "Beta";
		core.motions = {first, second};
		core.libraryRevision = 5;
	}
};

static void TestWireFraming()
{
	BeginTest("wire_framing");
	Fixture f;
	const auto malformed = ParseJson(f.bridge->HandleMessageForTest("{not json"));
	Check(malformed.Find("ok") && !malformed.Find("ok")->boolValue, "malformed JSON rejected");
	Check(malformed.Find("error") && malformed.Find("error")->Find("code") &&
		malformed.Find("error")->Find("code")->stringValue == "invalid_params", "malformed -> invalid_params");

	const auto badVersion = ParseJson(f.bridge->HandleMessageForTest(
		"{\"v\":2,\"id\":1,\"type\":\"request\",\"method\":\"bridge.ping\"}"));
	Check(badVersion.Find("error") && badVersion.Find("error")->Find("code")->stringValue == "invalid_params",
		"v!=1 rejected");

	const auto unknown = f.ReqJson("no.such.method");
	Check(unknown.Find("error") && unknown.Find("error")->Find("code")->stringValue == "unknown_method",
		"unknown method -> unknown_method");
	const auto* id = unknown.Find("id");
	Check(id && id->numberValue == 7, "response id correlates with request");

	const auto reserved = f.ReqJson("ensemble.set");
	Check(reserved.Find("error") && reserved.Find("error")->Find("code")->stringValue == "not_implemented",
		"reserved method -> not_implemented");
	const auto reservedDomain = f.ReqJson("gaze.setMode");
	Check(reservedDomain.Find("error") && reservedDomain.Find("error")->Find("code")->stringValue == "not_implemented",
		"reserved domain -> not_implemented");

	const auto badParams = ParseJson(f.bridge->HandleMessageForTest(
		"{\"v\":1,\"id\":3,\"type\":\"request\",\"method\":\"bridge.ping\",\"params\":42}"));
	Check(badParams.Find("error") && badParams.Find("error")->Find("code")->stringValue == "invalid_params",
		"non-object params rejected");
}

static void TestHelloCapabilities()
{
	BeginTest("hello_capabilities");
	Fixture f;
	const auto hello = f.ReqJson("bridge.hello");
	Check(f.Ok(f.Req("bridge.hello")), "bridge.hello ok");
	const auto* result = hello.Find("result");
	Check(result && result->Find("protocol") && result->Find("protocol")->numberValue == 1, "protocol=1");
	Check(result && result->Find("sessionId") && result->Find("sessionId")->numberValue > 0, "sessionId present");
	Check(f.ReqJson("bridge.ping").Find("result")->Find("pong")->boolValue, "bridge.ping pong");

	const auto motionHello = f.ReqJson("motion.hello");
	const auto* methods = motionHello.Find("result")->Find("supportedMethods");
	Check(methods && methods->IsArray(), "supportedMethods array");
	const auto hasMethod = [&](const char* name) {
		for (size_t i = 0; i < methods->Size(); ++i)
			if (methods->At(i)->stringValue == name) return true;
		return false;
	};
	Check(hasMethod("player.play") && hasMethod("state.get") && hasMethod("library.delete") &&
		hasMethod("camera.set") && hasMethod("stage.set"), "core methods advertised");
	Check(!hasMethod("dialog.pickFile") && !hasMethod("drop.poll") && !hasMethod("library.import") &&
		!hasMethod("ensemble.set"), "gated methods not advertised");
	const auto* events = motionHello.Find("result")->Find("supportedEvents");
	const auto hasEvent = [&](const char* name) {
		for (size_t i = 0; i < events->Size(); ++i)
			if (events->At(i)->stringValue == name) return true;
		return false;
	};
	Check(hasEvent("player.stateChanged") && hasEvent("bridge.closed") && hasEvent("library.changed"),
		"events advertised");
	Check(motionHello.Find("result")->Find("pluginVersion")->stringValue == "0.1.1-test",
		"pluginVersion forwarded from hooks");
}

static void TestStateSnapshot()
{
	BeginTest("state_snapshot");
	Fixture f;
	f.AddSyntheticLibrary();
	const auto snapshot = f.ReqJson("state.get");
	Check(f.Ok(f.Req("state.get")), "state.get ok");
	const auto* result = snapshot.Find("result");
	for (const char* key : {"permission", "player", "library", "loaded", "playlist", "actors", "camera",
		"stage", "facial", "position", "ensemble", "physicsBackend", "musicVolumeSupport",
		"musicVolumeNotice", "importProgress", "motionsRoot"})
		Check(result && result->Find(key), std::string("snapshot carries ") + key);
	Check(result->Find("player")->Find("state")->stringValue == "stopped", "idle player stopped");
	Check(result->Find("player")->Find("title")->stringValue == "未加载曲目", "idle title");
	Check(result->Find("library")->Find("revision")->numberValue == 5, "library revision exposed");
	Check(result->Find("library")->Find("entries")->Size() == 2, "library entries exposed");
	Check(result->Find("library")->Find("entries")->At(0)->Find("id")->stringValue == "alpha", "entry id is key");
	Check(result->Find("importProgress")->IsNull(), "no import -> null progress");
	Check(result->Find("motionsRoot")->stringValue == "synthetic/motions", "motionsRoot exposed");
	Check(result->Find("permission")->Find("injectionAllowed")->boolValue, "permission allowed");
}

static void TestTransportQueue()
{
	BeginTest("transport_queue");
	Fixture f;
	f.LoadSyntheticClip();
	f.AddSyntheticLibrary();

	Check(f.Ok(f.Req("player.play")), "play accepted");
	Check(f.FindCommand(Core::GuiAction::Play) != nullptr, "play queued as GuiAction::Play");
	Check(f.ErrorCode(f.Req("player.pause")) == "not_ready", "pause while stopped -> not_ready");
	f.core.player.state = PlaybackState::Playing;
	Check(f.Ok(f.Req("player.pause")), "pause while playing accepted");
	Check(f.FindCommand(Core::GuiAction::Pause) != nullptr, "pause queued");
	Check(f.Ok(f.Req("player.resume")), "resume accepted");
	Check(f.Ok(f.Req("player.stop")), "stop accepted without clip requirement");

	f.ClearCommands();
	Check(f.Ok(f.Req("player.seek", "{\"seconds\":1.5}")), "seek accepted");
	const auto* seek = f.FindCommand(Core::GuiAction::Seek);
	Check(seek && seek->value == 1.5f, "seek value forwarded");
	Check(seek && seek->key == "synthetic.gkmotion" && seek->loadedGeneration == 3 && seek->sceneSession == 2,
		"seek carries position-command revision guards");
	Check(f.ErrorCode(f.Req("player.seek", "{\"seconds\":99}")) == "invalid_params", "seek beyond duration rejected");
	Check(f.ErrorCode(f.Req("player.seek", "{\"seconds\":-1}")) == "invalid_params", "negative seek rejected");

	Check(f.Ok(f.Req("player.seekFrame", "{\"frame\":119}")), "last frame accepted");
	const auto* seekFrame = f.FindCommand(Core::GuiAction::SeekFrame);
	Check(seekFrame && seekFrame->frame == 119, "seekFrame 0-based frame forwarded");
	Check(f.ErrorCode(f.Req("player.seekFrame", "{\"frame\":120}")) == "invalid_params", "frame out of range rejected");
	Check(f.Ok(f.Req("player.stepFrame", "{\"delta\":1}")), "stepFrame +1 accepted");
	Check(f.ErrorCode(f.Req("player.stepFrame", "{\"delta\":2}")) == "invalid_params", "stepFrame delta must be ±1");

	Check(f.Ok(f.Req("player.setSpeed", "{\"speed\":1.5}")), "speed accepted");
	const auto* speed = f.FindCommand(Core::GuiAction::Speed);
	Check(speed && speed->value == 1.5f, "speed value forwarded");
	Check(f.ErrorCode(f.Req("player.setSpeed", "{\"speed\":0.05}")) == "invalid_params", "speed below range rejected");
	Check(f.Ok(f.Req("player.setVolume", "{\"volume\":0.4}")), "volume accepted");
	const auto* volume = f.FindCommand(Core::GuiAction::MusicVolume);
	Check(volume && volume->value == 0.4f, "volume value forwarded");
	Check(f.ErrorCode(f.Req("player.setVolume", "{\"volume\":2}")) == "invalid_params", "volume above 1 rejected");
	Check(f.Ok(f.Req("player.saveVolume")), "saveVolume accepted");
	Check(f.FindCommand(Core::GuiAction::SaveMusicVolume) != nullptr, "saveVolume queued");

	Check(f.Ok(f.Req("player.cycle", "{\"delta\":1}")), "cycle accepted");
	const auto* cycle = f.FindCommand(Core::GuiAction::Cycle);
	Check(cycle && cycle->value == 1.0f, "cycle delta forwarded");
	Check(f.ErrorCode(f.Req("player.cycle", "{\"delta\":0}")) == "invalid_params", "cycle zero rejected");
	Check(f.Ok(f.Req("player.reload")), "reload accepted");
	Check(f.FindCommand(Core::GuiAction::ReloadMotion) != nullptr, "reload queued");

	Check(f.Ok(f.Req("playlist.setMode", "{\"mode\":2}")), "setMode accepted");
	const auto* mode = f.FindCommand(Core::GuiAction::Mode);
	Check(mode && mode->value == 2.0f, "mode forwarded");
	f.ClearCommands();
	Check(f.Ok(f.Req("playlist.setMode", "{\"mode\":9}")), "invalid mode clamped per protocol");
	const auto* clampedMode = f.FindCommand(Core::GuiAction::Mode);
	Check(clampedMode && clampedMode->value == 1.0f, "invalid mode -> 1");

	Check(f.Ok(f.Req("player.scrubBegin", "{\"seconds\":0.5}")), "scrubBegin accepted");
	Check(f.Ok(f.Req("player.scrubUpdate", "{\"seconds\":1.0}")), "scrubUpdate accepted");
	Check(f.Ok(f.Req("player.scrubEnd", "{\"seconds\":1.0}")), "scrubEnd accepted");
	Check(f.FindCommand(Core::GuiAction::ScrubBegin) && f.FindCommand(Core::GuiAction::ScrubUpdate) &&
		f.FindCommand(Core::GuiAction::ScrubEnd), "scrub trio queued in order");
}

static void TestPermissionGate()
{
	BeginTest("permission_gate");
	Fixture f;
	f.LoadSyntheticClip();
	f.core.injectionPolicy.SetSceneAllowed(false);
	Check(f.ErrorCode(f.Req("player.play")) == "not_allowed", "transport gated by injection policy");
	Check(f.ErrorCode(f.Req("camera.save")) == "not_allowed", "camera gated by injection policy");
	Check(f.Ok(f.Req("library.refresh")), "library refresh not gated");
	Check(f.Ok(f.Req("state.get")), "state.get not gated");
	f.core.injectionPolicy.SetSceneAllowed(true);
	Check(f.Ok(f.Req("player.play")), "transport restored with permission");
}

static void TestLibraryGuards()
{
	BeginTest("library_guards");
	Fixture f;
	f.AddSyntheticLibrary();

	Check(f.ErrorCode(f.Req("library.select", "{\"key\":\"alpha\",\"autoplay\":true,\"revision\":4}")) ==
		"stale_revision", "select with stale revision rejected");
	Check(f.ErrorCode(f.Req("library.select", "{\"key\":\"missing\",\"autoplay\":true,\"revision\":5}")) ==
		"invalid_params", "select unknown key rejected");
	const auto selected = f.ReqJson("library.select", "{\"key\":\"alpha\",\"autoplay\":true,\"revision\":5}");
	Check(f.Ok(f.Req("library.select", "{\"key\":\"alpha\",\"autoplay\":true,\"revision\":5}")), "select accepted");
	Check(selected.Find("result")->Find("key")->stringValue == "alpha", "select result echoes key");
	const auto* select = f.FindCommand(Core::GuiAction::Select);
	Check(select && select->key == "alpha" && select->flag, "select queued with autoplay");

	Check(f.ErrorCode(f.Req("library.delete", "{\"key\":\"alpha\",\"revision\":9}")) == "stale_revision",
		"delete with stale revision rejected");
	Check(f.Ok(f.Req("library.delete", "{\"key\":\"alpha\",\"revision\":5}")), "delete accepted");
	const auto* deleteCommand = f.FindCommand(Core::GuiAction::DeleteFiles);
	Check(deleteCommand && deleteCommand->key == "alpha" && deleteCommand->flag &&
		deleteCommand->libraryRevision == 5, "delete queued confirmed with revision guard");

	f.core.importBusy = true;
	Check(f.ErrorCode(f.Req("library.delete", "{\"key\":\"beta\",\"revision\":5}")) == "busy",
		"delete during import -> busy");
	f.core.importBusy = false;

	f.ClearCommands();
	const auto refresh = f.ReqJson("library.refresh");
	Check(refresh.Find("result")->Find("started")->boolValue, "refresh started");
	Check(f.FindCommand(Core::GuiAction::Refresh) != nullptr, "refresh queued");
	f.core.libraryScan = std::async(std::launch::deferred, [] { return LibraryScanSnapshot{}; });
	const auto refreshAgain = f.ReqJson("library.refresh");
	Check(!refreshAgain.Find("result")->Find("started")->boolValue, "refresh while scanning -> started:false");
}

static void TestCameraStage()
{
	BeginTest("camera_stage");
	Fixture f;
	Check(f.Ok(f.Req("camera.set", "{\"settings\":{\"mode\":2,\"followActor\":true,\"followFov\":50}}")),
		"camera.set accepted");
	const auto* camera = f.FindCommand(Core::GuiAction::CameraSettings);
	Check(camera && camera->cameraSettings.mode == CameraMode::Follow && camera->cameraSettings.followActor &&
		camera->cameraSettings.followFov == 50.0f, "camera patch applied over current settings");
	Check(f.ErrorCode(f.Req("camera.set", "{\"settings\":{\"mode\":9}}")) == "invalid_params",
		"camera mode out of range rejected");
	Check(f.ErrorCode(f.Req("camera.set", "{\"settings\":{\"followRate\":{\"x\":2,\"y\":0,\"z\":0}}")) ==
		"invalid_params", "camera rate out of range rejected");
	Check(f.Ok(f.Req("camera.save")), "camera.save accepted");
	Check(f.FindCommand(Core::GuiAction::SaveCameraSettings) != nullptr, "camera.save queued");

	f.ClearCommands();
	Check(f.Ok(f.Req("stage.set", "{\"settings\":{\"lightingEffectsEnabled\":false}}")), "stage.set accepted");
	const auto* stage = f.FindCommand(Core::GuiAction::StageSettings);
	Check(stage && !stage->stageSettings.lightingEffectsEnabled, "stage settings queued");
	const auto* cameraLink = f.FindCommand(Core::GuiAction::CameraSettings);
	Check(cameraLink && cameraLink->cameraSettings.actorShadowExperimentMode == 2,
		"lighting off links camera shadow mode 2");

	f.ClearCommands();
	Check(f.Ok(f.Req("stage.set", "{\"settings\":{\"hideBackground\":true}}")), "hideBackground accepted");
	const auto* hidden = f.FindCommand(Core::GuiAction::StageSettings);
	Check(hidden && hidden->stageSettings.hideBackground && hidden->stageSettings.greenScreenActive &&
		hidden->stageSettings.overrideCameraClear, "hideBackground forces green screen + camera clear");

	// Simulate the Unity drain having applied the previous command.
	f.core.stageManager.settings.hideBackground = true;
	f.ClearCommands();
	Check(f.Ok(f.Req("stage.set", "{\"settings\":{\"hideBackground\":false}}")), "show background accepted");
	const auto* shown = f.FindCommand(Core::GuiAction::StageSettings);
	Check(shown && !shown->stageSettings.greenScreenActive && !shown->stageSettings.overrideCameraClear,
		"show background clears green screen");
	Check(f.FindCommand(Core::GuiAction::RestoreCameraSkybox) != nullptr, "skybox restore queued");
}

static void TestFacialPositionSettings()
{
	BeginTest("facial_position_settings");
	Fixture f;
	Check(f.Ok(f.Req("facial.setWeights", "{\"overall\":1.5,\"brow\":0.5,\"eye\":1,\"mouth\":2}")),
		"facial.setWeights accepted");
	Check(f.core.player.controller.facialWeights.overall == 1.5f &&
		f.core.player.controller.facialWeights.brow == 0.5f, "facial weights written under stateMutex");
	Check(f.ErrorCode(f.Req("facial.setWeights", "{\"overall\":2.5,\"brow\":1,\"eye\":1,\"mouth\":1}")) ==
		"invalid_params", "facial weight above 2 rejected");
	Check(f.Ok(f.Req("facial.save")), "facial.save accepted");
	Check(f.FindCommand(Core::GuiAction::SaveFacialWeights) != nullptr, "facial.save queued");

	Check(f.Ok(f.Req("position.set", "{\"x\":0.5,\"y\":1,\"z\":-2}")), "position.set accepted");
	const auto* position = f.FindCommand(Core::GuiAction::InitialPosition);
	Check(position && position->position.y == 1.0f && position->position.z == -2.0f, "position forwarded");
	Check(f.ErrorCode(f.Req("position.set", "{\"x\":11,\"y\":0,\"z\":0}")) == "invalid_params",
		"position beyond 10m rejected");
	Check(f.Ok(f.Req("position.save")), "position.save accepted");
	Check(f.FindCommand(Core::GuiAction::SaveInitialPosition) != nullptr, "position.save queued");

	Check(f.Ok(f.Req("settings.setPhysics", "{\"backend\":1}")), "setPhysics accepted");
	const auto* physics = f.FindCommand(Core::GuiAction::PhysicsBackend);
	Check(physics && physics->value == 1.0f, "physics backend forwarded");
	Check(f.ErrorCode(f.Req("settings.setPhysics", "{\"backend\":3}")) == "invalid_params",
		"physics backend out of range rejected");

	Check(f.Ok(f.Req("shell.setTextInputActive", "{\"active\":true}")), "setTextInputActive accepted");
	Check(f.core.guiTextInputActive.load(), "text input flag stored");
	Check(f.Ok(f.Req("shell.setTextInputActive", "{\"active\":false}")), "setTextInputActive false accepted");
	Check(!f.core.guiTextInputActive.load(), "text input flag cleared");

	const auto appearance = f.ReqJson("appearance.get");
	Check(appearance.Find("result")->Find("theme") != nullptr &&
		appearance.Find("result")->Find("uiScale") != nullptr, "appearance defaults without host service");
}

static void TestActorsToken()
{
	BeginTest("actors_token");
	Fixture f;
	SceneActorObservation observation;
	observation.actor = reinterpret_cast<void*>(0x1234);
	observation.instanceId = 1;
	observation.character = "saki";
	observation.displayName = "咲季";
	f.core.sceneActorRegistry.Publish({observation});
	const uint64_t epoch = f.core.sceneActorRegistry.Epoch();
	const uint64_t token = f.core.sceneActorRegistry.Entries().front().token;

	Check(f.ErrorCode(f.Req("actors.selectTarget", "{\"token\":1,\"epoch\":99}")) == "stale_token",
		"stale epoch rejected");
	Check(f.ErrorCode(f.Req("actors.selectTarget",
		"{\"token\":999,\"epoch\":" + std::to_string(epoch) + "}")) == "stale_token", "unknown token rejected");
	Check(f.Ok(f.Req("actors.selectTarget",
		"{\"token\":" + std::to_string(token) + ",\"epoch\":" + std::to_string(epoch) + "}")),
		"valid token accepted");
	const auto* select = f.FindCommand(Core::GuiAction::SelectTarget);
	Check(select && select->actorToken == token && select->actorEpoch == epoch, "token+epoch queued");

	const auto snapshot = f.ReqJson("state.get");
	const auto* actors = snapshot.Find("result")->Find("actors");
	Check(actors->Find("epoch")->numberValue == static_cast<double>(epoch), "actors epoch exposed");
	Check(actors->Find("actors")->Size() == 1, "actor roster exposed");
}

static void TestBufferContract()
{
	BeginTest("buffer_contract");
	Fixture f;
	const char* request = "{\"v\":1,\"id\":1,\"type\":\"request\",\"method\":\"bridge.hello\"}";
	char tinyBuffer[8]; // "small" is a Windows RPC macro
	std::memset(tinyBuffer, 0x7E, sizeof(tinyBuffer));
	uint32_t required = 0;
	Check(f.bridge->HandleMessage(request, sizeof(tinyBuffer), tinyBuffer, &required) == GAKU_E_SIZE,
		"short buffer -> GAKU_E_SIZE");
	Check(required > sizeof(tinyBuffer), "requiredBytes reports full size");
	Check(tinyBuffer[0] == 0x7E, "short buffer leaves output untouched");

	std::vector<char> buffer(required, 0);
	Check(f.bridge->HandleMessage(request, static_cast<uint32_t>(buffer.size()), buffer.data(), &required) == GAKU_OK,
		"sized buffer -> GAKU_OK");
	Check(buffer[required - 1] == '\0', "requiredBytes includes NUL");
	Check(std::strlen(buffer.data()) == required - 1, "response NUL-terminated");
	const auto response = ParseJson(buffer.data());
	Check(response.Find("ok") && response.Find("ok")->boolValue, "buffered response valid");

	Check(f.bridge->HandleMessage(request, 0, nullptr, &required) == GAKU_E_SIZE,
		"metadata query returns size");
	Check(f.bridge->HandleMessage(nullptr, 0, nullptr, &required) == GAKU_E_INVALID_ARGUMENT,
		"null request rejected");
}

static void TestEventsEnvelope()
{
	BeginTest("events_envelope");
	Fixture f;
	f.AddSyntheticLibrary();
	f.bridge->OnUiTick();
	Check(!f.events.empty(), "baseline tick emits events");
	uint64_t lastSeq = 0;
	bool envelopeOk = true;
	for (const auto& json : f.events)
	{
		const auto value = ParseJson(json);
		const auto* v = value.Find("v");
		const auto* type = value.Find("type");
		const auto* seq = value.Find("seq");
		const auto* sid = value.Find("sid");
		if (!v || v->numberValue != 1 || !type || type->stringValue != "event" || !seq || !sid ||
			seq->numberValue <= static_cast<double>(lastSeq) ||
			sid->numberValue != static_cast<double>(f.bridge->SessionId()))
			envelopeOk = false;
		lastSeq = static_cast<uint64_t>(seq->numberValue);
	}
	Check(envelopeOk, "every event carries v=1/type=event/monotonic seq/session sid");
	Check(f.CountEvents("player.stateChanged") >= 1, "baseline player event");
	Check(f.CountEvents("permission.changed") >= 1, "baseline permission event");
	Check(f.CountEvents("library.changed") >= 1, "baseline library event");

	const int libraryBefore = f.CountEvents("library.changed");
	++f.core.libraryRevision;
	f.bridge->OnUiTick();
	Check(f.CountEvents("library.changed") == libraryBefore + 1, "library revision change pushes event");

	f.core.player.state = PlaybackState::Playing;
	f.bridge->OnUiTick();
	const int playerBefore = f.CountEvents("player.stateChanged");
	f.bridge->OnUiTick();
	Check(f.CountEvents("player.stateChanged") == playerBefore, "playing state throttled to ~10Hz");

	Fixture second;
	Check(second.bridge->SessionId() > f.bridge->SessionId(), "new bridge advances sessionId");
}

static void TestDialogHostService()
{
	BeginTest("dialog_host_service");
	Fixture f;
	// Core's pick-file hooks are plain function pointers: use file-scope state.
	static int beginCalls = 0;
	static int pollState = GAKU_FILE_DIALOG_PENDING;
	static std::string pollPath;
	beginCalls = 0;
	pollState = GAKU_FILE_DIALOG_PENDING;
	pollPath.clear();
	f.core.fnBeginPickFile = [](int, std::string& notice) -> bool {
		++beginCalls;
		notice.clear();
		return true;
	};
	f.core.fnPollPickFile = [](std::string& path, std::string&) -> int {
		path = pollPath;
		return pollState;
	};

	Check(f.ErrorCode(f.Req("dialog.pickFile", "{\"kind\":0,\"multiple\":true}")) == "invalid_params",
		"multiple selection unsupported");
	Check(f.ErrorCode(f.Req("dialog.pickFile", "{\"kind\":9,\"multiple\":false}")) == "invalid_params",
		"invalid kind rejected");
	const auto picked = f.ReqJson("dialog.pickFile", "{\"kind\":0,\"multiple\":false}");
	Check(picked.Find("ok") && picked.Find("ok")->boolValue, "pickFile begins host dialog");
	Check(picked.Find("result") && picked.Find("result")->Find("dialogId") &&
		picked.Find("result")->Find("dialogId")->numberValue == 1, "dialogId issued");
	Check(beginCalls == 1, "host beginOpen called once");
	Check(f.ErrorCode(f.Req("dialog.pickFile", "{\"kind\":1,\"multiple\":false}")) == "busy",
		"second dialog while pending -> busy");

	auto poll = f.ReqJson("dialog.poll", "{\"dialogId\":1}");
	Check(poll.Find("result")->Find("state")->stringValue == "pending", "poll pending");
	pollState = GAKU_FILE_DIALOG_ACCEPTED;
	pollPath = "C:\\synthetic\\motion.vmd";
	f.bridge->OnUiTick();
	poll = f.ReqJson("dialog.poll", "{\"dialogId\":1}");
	Check(poll.Find("result")->Find("state")->stringValue == "accepted", "onUiTick drives dialog completion");
	Check(poll.Find("result")->Find("paths")->At(0)->stringValue == "C:\\synthetic\\motion.vmd",
		"accepted path returned");
	Check(f.ErrorCode(f.Req("dialog.poll", "{\"dialogId\":42}")) == "invalid_params", "wrong dialogId rejected");
}

static void TestShutdownInvalidation()
{
	BeginTest("shutdown_invalidation");
	Fixture f;
	f.bridge->OnUiTick();
	f.bridge->OnShutdown();
	Check(f.bridge->Closed(), "onShutdown closes bridge");
	Check(f.CountEvents("bridge.closed") == 1, "bridge.closed emitted once");
	const auto closed = ParseJson(f.events.back());
	Check(closed.Find("name")->stringValue == "bridge.closed", "last event is bridge.closed");
	Check(closed.Find("payload")->Find("reason") != nullptr, "closure reason present");
	Check(f.ErrorCode(f.Req("bridge.ping")) == "not_allowed", "requests after close rejected");
	const int eventCount = static_cast<int>(f.events.size());
	f.bridge->OnUiTick();
	Check(static_cast<int>(f.events.size()) == eventCount, "no events after close");

	Fixture silent;
	silent.bridge->Invalidate("plugin-shutdown");
	Check(silent.bridge->Closed(), "silent invalidate closes bridge");
	Check(silent.events.empty(), "Unity-thread invalidate never calls the sink");
}

static void TestScrubResetOnPermissionLoss()
{
	BeginTest("scrub_reset_on_permission_loss");
	Fixture f;
	f.LoadSyntheticClip();
	f.core.player.state = PlaybackState::Playing;
	f.core.player.scrubbing = true;
	f.core.player.currentTime = 1.25f;
	f.bridge->OnUiTick();
	f.core.injectionPolicy.SetSceneAllowed(false);
	f.bridge->OnUiTick();
	Check(f.CountEvents("permission.changed") >= 2, "permission flip pushed");
	const auto* scrubEnd = f.FindCommand(Core::GuiAction::ScrubEnd);
	Check(scrubEnd != nullptr, "native scrub finished through the queue on permission loss");
	Check(scrubEnd && scrubEnd->value == 1.25f, "scrub end preserves current time");
}

int main()
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	TestWireFraming();
	TestHelloCapabilities();
	TestStateSnapshot();
	TestTransportQueue();
	TestPermissionGate();
	TestLibraryGuards();
	TestCameraStage();
	TestFacialPositionSettings();
	TestActorsToken();
	TestBufferContract();
	TestEventsEnvelope();
	TestDialogHostService();
	TestShutdownInvalidation();
	TestScrubResetOnPermissionLoss();
	std::printf("\nweb-panel bridge contracts: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail == 0 ? 0 : 1;
}
