#pragma once

// =============================================================================
// GkWebPanelBridge.hpp — gakumotion.web-panel/v1 provider bridge
// =============================================================================
// Implements the GakuWebPanelServiceV1 domain semantics for the Loader's
// WebView2 manager panel. The wire contract mirrors the frontend authority
// (GakuLoader frontend/src/motion/protocol.ts and
// frontend/docs/motion-bridge-protocol.md):
//   request  {v:1, id, type:"request", method, params}
//   response {v:1, type:"response", id, ok, result | error:{code,message,details?}}
//   event    {v:1, type:"event", name, payload, seq, sid}
//
// Threading: every ABI callback except MotionShutdown's silent Invalidate runs
// on the Loader desktop UI thread. Core state is read/written under
// Core::stateMutex; mutations are queued through Core::QueueGui exactly like
// the ImGui overlay (GuiAction + revision/session guards) and drain on the
// Unity main thread. The event sink is only ever invoked from UI-thread
// callbacks (OnUiTick / HandleMessage / OnShutdown), never from Unity.
//
// The header is Windows-free so headless contract tests can drive it with a
// real Core and fake host hooks (tests/test_web_panel_bridge.cpp).
// =============================================================================

#include "../include/GakuWebPanel.h"
#include "GakuMotion.hpp"
#include "GkJsonLite.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

namespace GakuMotion::webpanel
{
	// -------------------------------------------------------------------------
	// Minimal JSON writer (jsonlite is parse-only by design).
	// -------------------------------------------------------------------------
	inline void AppendEscaped(std::string& out, const std::string& text)
	{
		out.push_back('"');
		for (const unsigned char ch : text)
		{
			switch (ch)
			{
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\b': out += "\\b"; break;
			case '\f': out += "\\f"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if (ch < 0x20)
				{
					char buffer[8]{};
					std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(ch));
					out += buffer;
				}
				else out.push_back(static_cast<char>(ch));
			}
		}
		out.push_back('"');
	}

	inline std::string JString(const std::string& text)
	{
		std::string out;
		AppendEscaped(out, text);
		return out;
	}

	inline std::string JNum(double value)
	{
		if (!std::isfinite(value)) return "0";
		char buffer[48]{};
		if (value == std::floor(value) && std::fabs(value) < 9.0e15)
			std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
		else
			std::snprintf(buffer, sizeof(buffer), "%.17g", value);
		return buffer;
	}

	inline std::string JBool(bool value) { return value ? "true" : "false"; }

	inline std::string JVec(const Vector3& value)
	{
		return "{\"x\":" + JNum(value.x) + ",\"y\":" + JNum(value.y) + ",\"z\":" + JNum(value.z) + "}";
	}

	// -------------------------------------------------------------------------
	// Host hooks supplied by the plugin TU (Windows/services) or by tests.
	// -------------------------------------------------------------------------
	struct DropPeek
	{
		bool ok{false};
		std::string visual{"none"}; // none | armed | hover | invalid
		uint32_t pendingCount{0};
		bool hostElevated{false};
		bool hostBlocked{false};
	};

	struct DropBatch
	{
		bool ok{false};
		std::vector<std::string> paths;
		std::string notice;
	};

	struct AppearanceInfo
	{
		bool ok{false};
		int theme{0};
		float uiScale{1.0f};
	};

	struct HostHooks
	{
		std::function<std::wstring()> assetsDir;
		std::function<bool(std::string& notice)> openLibraryFolder;
		std::function<void()> releaseFileDialog;
		std::function<bool(bool enabled, std::string& notice)> dropSetTarget;
		std::function<DropPeek()> dropPeek;
		std::function<DropBatch()> dropConsume;
		std::function<AppearanceInfo()> appearance;
		std::string pluginVersion{"0.1.1"};
	};

	// -------------------------------------------------------------------------
	// Command result helpers.
	// -------------------------------------------------------------------------
	struct CmdResult
	{
		bool ok{true};
		std::string body{"{}"};
		std::string code;
		std::string message;
		std::string details;

		static CmdResult Ok(std::string result = "{}") { CmdResult r; r.body = std::move(result); return r; }
		static CmdResult Err(std::string code_, std::string message_, std::string details_ = "")
		{
			CmdResult r;
			r.ok = false;
			r.code = std::move(code_);
			r.message = std::move(message_);
			r.details = std::move(details_);
			return r;
		}
	};

	inline constexpr const char* kSupportedMethods[] = {
		"bridge.hello", "bridge.ping", "motion.hello", "state.get",
		"player.play", "player.pause", "player.resume", "player.stop", "player.cycle",
		"player.seek", "player.seekFrame", "player.stepFrame",
		"player.scrubBegin", "player.scrubUpdate", "player.scrubEnd",
		"player.setSpeed", "player.setVolume", "player.saveVolume", "player.reload",
		"playlist.setMode",
		"library.refresh", "library.select", "library.delete", "library.openFolder",
		"actors.selectTarget",
		"facial.setWeights", "facial.save",
		"position.set", "position.save",
		"camera.set", "camera.save",
		"stage.set",
		"settings.setPhysics",
		"shell.setTextInputActive",
		"appearance.get",
	};

	inline constexpr const char* kSupportedEvents[] = {
		"player.stateChanged", "permission.changed", "library.changed", "loaded.changed",
		"playlist.changed", "actors.changed", "camera.changed", "stage.changed",
		"facial.changed", "position.changed", "ensemble.changed", "settings.changed",
		"import.progress", "appearance.changed", "bridge.closed",
	};

	// Recognized reserved methods that stay gated off (frontend "待迁移").
	inline constexpr const char* kReservedMethods[] = {
		"library.import", "library.probeFiles",
		"ensemble.set", "ensemble.preset", "ensemble.probe",
		"stage.scan", "stage.toggleNode", "stage.setLoadReplacement",
	};

	inline constexpr const char* kReservedPrefixes[] = {
		"gaze.", "bone.", "sole.", "solePitch.", "studio.", "experiments.", "telemetry.",
	};

	class Bridge
	{
		using GuiAction = Core::GuiAction;
		using GuiCommand = Core::GuiCommand;

	public:
		Bridge(Core& core, HostHooks hooks) : core_(core), hooks_(std::move(hooks))
		{
			static std::atomic<uint64_t> sessionCounter{0};
			sessionId_ = ++sessionCounter;
		}

		uint64_t SessionId() const { return sessionId_; }
		bool Closed() const { return closed_.load(std::memory_order_acquire); }

		// ---- ABI surface -----------------------------------------------------

		GakuStatus GetAssets(GakuWebPanelAssetsV1* output)
		{
			if (!output) return GAKU_E_INVALID_ARGUMENT;
			if (closed_.load(std::memory_order_acquire)) return GAKU_E_SHUTTING_DOWN;
			if (!hooks_.assetsDir) return GAKU_E_UNAVAILABLE;
			std::wstring dir = hooks_.assetsDir();
			if (dir.empty()) return GAKU_E_UNAVAILABLE;
			std::error_code ec;
			const std::filesystem::path entry = std::filesystem::path(dir) / L"index.html";
			if (!std::filesystem::is_regular_file(entry, ec) || ec) return GAKU_E_UNAVAILABLE;
			assetsDirCache_ = std::move(dir);
			output->abiVersion = GAKU_WEB_PANEL_VERSION;
			output->structSize = sizeof(GakuWebPanelAssetsV1);
			output->assetsDir = assetsDirCache_.c_str();
			output->entryPath = L"index.html";
			output->virtualHost = L"gakumotion.invalid";
			return GAKU_OK;
		}

		void OnShow()
		{
			visible_ = true;
			baselinePending_ = true;
		}

		void OnHide() { visible_ = false; }

		void SetEventSink(GakuWebPanelEventSink sink, void* userData)
		{
			std::lock_guard<std::mutex> lock(sinkMutex_);
			sink_ = sink;
			sinkUserData_ = userData;
			if (sink) baselinePending_ = true;
		}

		GakuStatus HandleMessage(const char* requestJson, uint32_t responseCapacity,
			char* responseJson, uint32_t* requiredBytes)
		{
			if (!requestJson) return GAKU_E_INVALID_ARGUMENT;
			std::string response;
			try { response = DispatchRequest(requestJson); }
			catch (...) { response = MakeError(0, "internal", "Unhandled bridge exception"); }
			const uint64_t needed = static_cast<uint64_t>(response.size()) + 1;
			if (needed > 0xFFFFFFFFull) return GAKU_E_LIMIT;
			if (requiredBytes) *requiredBytes = static_cast<uint32_t>(needed);
			if (!responseJson || responseCapacity < needed) return GAKU_E_SIZE;
			std::memcpy(responseJson, response.c_str(), static_cast<size_t>(needed));
			return GAKU_OK;
		}

		void OnUiTick()
		{
			if (closed_.load(std::memory_order_acquire)) return;
			try { Tick(); }
			catch (...) { /* a diagnostic tick must never break the host UI */ }
		}

		// UI thread: announce closure, then invalidate.
		void OnShutdown()
		{
			if (!closed_.load(std::memory_order_acquire))
				Emit("bridge.closed", "{\"reason\":\"service-shutdown\"}");
			Invalidate("service-shutdown");
		}

		// Any thread (MotionShutdown runs on Unity): silent invalidation, no sink.
		void Invalidate(const char* /*reason*/)
		{
			closed_.store(true, std::memory_order_release);
			dialogPending_ = false;
			if (hooks_.releaseFileDialog) hooks_.releaseFileDialog();
			std::lock_guard<std::mutex> lock(sinkMutex_);
			sink_ = nullptr;
			sinkUserData_ = nullptr;
		}

		// ---- test helpers ----------------------------------------------------

		std::string HandleMessageForTest(const std::string& request) { return DispatchRequest(request.c_str()); }

	private:
		// ---- wire framing ----------------------------------------------------

		static std::string MakeOk(uint64_t id, const std::string& result)
		{
			return "{\"v\":1,\"type\":\"response\",\"id\":" + JNum(static_cast<double>(id)) +
				",\"ok\":true,\"result\":" + result + "}";
		}

		static std::string MakeError(uint64_t id, const std::string& code, const std::string& message,
			const std::string& details = "")
		{
			std::string error = "{\"code\":" + JString(code) + ",\"message\":" + JString(message);
			if (!details.empty()) error += ",\"details\":" + details;
			error += "}";
			return "{\"v\":1,\"type\":\"response\",\"id\":" + JNum(static_cast<double>(id)) +
				",\"ok\":false,\"error\":" + error + "}";
		}

		void Emit(const std::string& name, const std::string& payloadJson)
		{
			std::lock_guard<std::mutex> lock(sinkMutex_);
			if (!sink_ || closed_.load(std::memory_order_acquire)) return;
			const std::string json = "{\"v\":1,\"type\":\"event\",\"name\":" + JString(name) +
				",\"payload\":" + payloadJson + ",\"seq\":" + JNum(static_cast<double>(++seq_)) +
				",\"sid\":" + JNum(static_cast<double>(sessionId_)) + "}";
			sink_(sinkUserData_, json.c_str());
		}

		std::string DispatchRequest(const char* requestJson)
		{
			jsonlite::Value root;
			std::string parseError;
			if (!jsonlite::ParseText(requestJson, root, parseError) || !root.IsObject())
				return MakeError(0, "invalid_params", "Malformed request JSON");
			const jsonlite::Value* version = root.Find("v");
			const jsonlite::Value* type = root.Find("type");
			const jsonlite::Value* idValue = root.Find("id");
			const jsonlite::Value* methodValue = root.Find("method");
			uint64_t id = 0;
			if (idValue && idValue->IsNumber() && idValue->numberValue >= 0)
				id = static_cast<uint64_t>(idValue->numberValue);
			if (!version || !version->IsNumber() || version->numberValue != 1 ||
				!type || !type->IsString() || type->stringValue != "request" ||
				!idValue || !idValue->IsNumber() ||
				!methodValue || !methodValue->IsString() || methodValue->stringValue.empty())
				return MakeError(id, "invalid_params", "Request must carry v=1, type=request, numeric id and method");
			if (closed_.load(std::memory_order_acquire))
				return MakeError(id, "not_allowed", "Bridge is closed");
			const jsonlite::Value* params = root.Find("params");
			if (params && !params->IsObject() && !params->IsNull())
				return MakeError(id, "invalid_params", "params must be an object");
			static const jsonlite::Value emptyParams{[] { jsonlite::Value v; v.type = jsonlite::Type::Object; return v; }()};
			const std::string& method = methodValue->stringValue;
			CmdResult result;
			try { result = Dispatch(method, params ? *params : emptyParams); }
			catch (...) { result = CmdResult::Err("internal", "Unhandled method exception"); }
			if (result.ok) return MakeOk(id, result.body);
			return MakeError(id, result.code, result.message, result.details);
		}

		// ---- parameter helpers ------------------------------------------------

		static const jsonlite::Value* Param(const jsonlite::Value& params, const char* name)
		{
			return params.Find(name);
		}

		static bool NumberParam(const jsonlite::Value& params, const char* name, double& out)
		{
			const jsonlite::Value* value = params.Find(name);
			if (!value || !value->IsNumber() || !std::isfinite(value->numberValue)) return false;
			out = value->numberValue;
			return true;
		}

		static bool IntParam(const jsonlite::Value& params, const char* name, int64_t& out)
		{
			double number = 0;
			if (!NumberParam(params, name, number) || number != std::floor(number)) return false;
			out = static_cast<int64_t>(number);
			return true;
		}

		static bool BoolParam(const jsonlite::Value& params, const char* name, bool& out)
		{
			const jsonlite::Value* value = params.Find(name);
			if (!value || !value->IsBool()) return false;
			out = value->boolValue;
			return true;
		}

		static bool StringParam(const jsonlite::Value& params, const char* name, std::string& out)
		{
			const jsonlite::Value* value = params.Find(name);
			if (!value || !value->IsString()) return false;
			out = value->stringValue;
			return true;
		}

		static bool VecParam(const jsonlite::Value& params, const char* name, Vector3& out)
		{
			const jsonlite::Value* value = params.Find(name);
			if (!value || !value->IsObject()) return false;
			double x = 0, y = 0, z = 0;
			if (!NumberParam(*value, "x", x) || !NumberParam(*value, "y", y) || !NumberParam(*value, "z", z))
				return false;
			out = {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
			return true;
		}

		// ---- dispatch ----------------------------------------------------------

		CmdResult Dispatch(const std::string& method, const jsonlite::Value& params)
		{
			if (method == "bridge.hello")
				return CmdResult::Ok("{\"protocol\":1,\"sessionId\":" + JNum(static_cast<double>(sessionId_)) + "}");
			if (method == "bridge.ping")
				return CmdResult::Ok("{\"pong\":true}");
			if (method == "motion.hello")
				return CmdResult::Ok(BuildHello());
			if (method == "state.get")
			{
				std::lock_guard<std::recursive_mutex> lock(core_.stateMutex);
				return CmdResult::Ok(BuildSnapshotLocked());
			}

			// Transport ---------------------------------------------------------
			if (method == "player.play") return TransportSimple(GuiAction::Play, true);
			if (method == "player.pause") return TransportSimple(GuiAction::Pause, false);
			if (method == "player.resume") return TransportSimple(GuiAction::Resume, true);
			if (method == "player.stop") return TransportSimple(GuiAction::Stop, false);
			if (method == "player.reload")
			{
				std::lock_guard<std::recursive_mutex> lock(core_.stateMutex);
				if (const auto guard = MutationGuardLocked(); !guard.ok) return guard;
				if (core_.loadedMotionKey.empty()) return CmdResult::Err("not_ready", "No motion is loaded");
				core_.QueueReloadMotion();
				ForcePlayerEvent();
				return CmdResult::Ok();
			}
			if (method == "player.cycle")
			{
				int64_t delta = 0;
				if (!IntParam(params, "delta", delta) || delta == 0 || delta < -100 || delta > 100)
					return CmdResult::Err("invalid_params", "delta must be a non-zero integer in [-100, 100]");
				std::lock_guard<std::recursive_mutex> lock(core_.stateMutex);
				if (const auto guard = MutationGuardLocked(); !guard.ok) return guard;
				if (core_.EnsembleActive())
					return CmdResult::Err("protected", "Cycle is disabled while the ensemble stage is active");
				if (core_.motions.empty()) return CmdResult::Err("not_ready", "Motion library is empty");
				core_.QueueCycleMotion(static_cast<int>(delta));
				ForcePlayerEvent();
				return CmdResult::Ok();
			}
			if (method == "player.seek") return SeekCommand(params, GuiAction::Seek);
			if (method == "player.scrubBegin") return SeekCommand(params, GuiAction::ScrubBegin);
			if (method == "player.scrubUpdate") return SeekCommand(params, GuiAction::ScrubUpdate);
			if (method == "player.scrubEnd") return SeekCommand(params, GuiAction::ScrubEnd);
			if (method == "player.seekFrame")
			{
				int64_t frame = 0;
				if (!IntParam(params, "frame", frame) || frame < 0)
					return CmdResult::Err("invalid_params", "frame must be a 0-based integer");
				std::lock_guard<std::recursive_mutex> lock(core_.stateMutex);
				if (const auto guard = MutationGuardLocked(); !guard.ok) return guard;
				if (!core_.player.clip.total_frames) return CmdResult::Err("not_ready", "No motion is loaded");
				if (frame >= static_cast<int64_t>(core_.player.clip.total_frames))
					return CmdResult::Err("invalid_params", "frame exceeds totalFrames - 1");
				core_.QueueSeekFrame(frame);
				ForcePlayerEvent();
				return CmdResult::Ok();
			}
			if (method == "player.stepFrame")
			{
				int64_t delta = 0;
				if (!IntParam(params, "delta", delta) || (delta != -1 && delta != 1))
					return CmdResult::Err("invalid_params", "delta must be -1 or 1");
				std::lock_guard<std::recursive_mutex> lock(core_.stateMutex);
				if (const auto guard = MutationGuardLocked(); !guard.ok) return guard;
				if (!core_.player.clip.total_frames) return CmdResult::Err("not_ready", "No motion is loaded");
				core_.QueueStepFrame(static_cast<int>(delta));
				ForcePlayerEvent();
				return CmdResult::Ok();
			}
			if (method == "player.setSpeed")
			{
				double speed = 0;
				if (!NumberParam(params, "speed", speed) || speed < 0.1 || speed > 3.0)
					return CmdResult::Err("invalid_params", "speed must be within 0.1..3.0");
				std::lock_guard<std::recursive_mutex> lock(core_.stateMutex);
				if (const auto guard = MutationGuardLocked(); !guard.ok) return guard;
				core_.QueueGui({GuiAction::Speed, static_cast<float>(speed)});
				ForcePlayerEvent();
				return CmdResult::Ok();
			}
			if (method == "player.setVolume")
			{
				double volume = 0;
				if (!NumberParam(params, "volume", volume) || volume < 0.0 || volume > 1.0)
					return CmdResult::Err("invalid_params", "volume must be within 0..1");
				std::lock_guard<std::recursive_mutex> lock(core_.stateMutex);
				if (const auto guard = MutationGuardLocked(); !guard.ok) return guard;
				core_.QueueGui({GuiAction::MusicVolume, static_cast<float>(volume)});
				ForcePlayerEvent();
				return CmdResult::Ok();
			}
			if (method == "player.saveVolume")
			{
				std::lock_guard<std::recursive_mutex> lock(core_.stateMutex);
				if (const auto guard = MutationGuardLocked(); !guard.ok) return guard;
				core_.QueueGui({GuiAction::SaveMusicVolume});
				return CmdResult::Ok();
			}
			if (method == "playlist.setMode")
			{
				int64_t mode = 1;
				if (!IntParam(params, "mode", mode) || mode < 0 || mode > 3) mode = 1; // protocol §6: invalid -> 1
				std::lock_guard<std::recursive_mutex> lock(core_.stateMutex);
				if (const auto guard = MutationGuardLocked(); !guard.ok) return guard;
				core_.QueueGui({GuiAction::Mode, static_cast<float>(mode)});
				return CmdResult::Ok();
			}

			// Library -----------------------------------------------------------
			if (method == "library.refresh")
			{
				std::lock_guard<std::recursive_mutex> lock(core_.stateMutex);
				if (core_.libraryScan.valid()) return CmdResult::Ok("{\"started\":false}");
				core_.QueueGui({GuiAction::Refresh});
				return CmdResult::Ok("{\"started\":true}");
			}
			if (method == "library.select")
			{
				std::string key;
				bool autoplay = false;
				int64_t revision = -1;
				if (!StringParam(params, "key", key) || key.empty() ||
					!IntParam(params, "revision", revision))
					return CmdResult::Err("invalid_params", "key and revision are required");
				if (!BoolParam(params, "autoplay", autoplay))
					return CmdResult::Err("invalid_params", "autoplay must be a boolean");
				std::lock_guard<std::recursive_mutex> lock(core_.stateMutex);
				if (static_cast<uint64_t>(revision) != core_.libraryRevision)
					return CmdResult::Err("stale_revision", "Motion library changed; refresh before selecting");
				if (const auto guard = MutationGuardLocked(); !guard.ok) return guard;
				const int index = FindPlaylistIndex(core_.motions, key);
				if (index < 0) return CmdResult::Err("invalid_params", "Motion key not found: " + key);
				core_.QueueSelectMotion(index, autoplay);
				ForcePlayerEvent();
				return CmdResult::Ok("{\"key\":" + JString(key) + "}");
			}
			if (method == "library.delete")
			{
				std::string key;
				int64_t revision = -1;
				if (!StringParam(params, "key", key) || key.empty() || !IntParam(params, "revision", revision))
					return CmdResult::Err("invalid_params", "key and revision are required");
				std::lock_guard<std::recursive_mutex> lock(core_.stateMutex);
				if (static_cast<uint64_t>(revision) != core_.libraryRevision)
					return CmdResult::Err("stale_revision", "Motion library changed; reopen the delete confirmation");
				if (core_.importBusy || core_.cameraImportBusy || core_.libraryScan.valid())
					return CmdResult::Err("busy", "Import or refresh is in progress");
				if (const auto guard = MutationGuardLocked(); !guard.ok) return guard;
				if (FindPlaylistIndex(core_.motions, key) < 0)
					return CmdResult::Err("invalid_params", "Motion key not found: " + key);
				if (core_.EnsembleActive())
					for (const auto& slot : core_.ensembleActiveSettings.slots)
						if (slot.enabled && slot.motionKey == key)
							return CmdResult::Err("protected", "Motion is in use by the ensemble stage");
				GuiCommand command{GuiAction::DeleteFiles, 0, true, key};
				command.libraryRevision = core_.libraryRevision;
				core_.QueueGui(std::move(command));
				return CmdResult::Ok();
			}
			if (method == "library.openFolder")
			{
				if (!hooks_.openLibraryFolder)
					return CmdResult::Err("not_implemented", "Host folder opener is unavailable");
				std::string notice;
				if (!hooks_.openLibraryFolder(notice))
					return CmdResult::Err("failed", notice.empty() ? "Cannot open the motions folder" : notice);
				return CmdResult::Ok();
			}

			// Actors ------------------------------------------------------------
			if (method == "actors.selectTarget")
			{
				int64_t token = 0, epoch = -1;
				if (!IntParam(params, "token", token) || token <= 0 || !IntParam(params, "epoch", epoch) || epoch < 0)
					return CmdResult::Err("invalid_params", "token and epoch are required");
				std::lock_guard<std::recursive_mutex> lock(core_.stateMutex);
				if (static_cast<uint64_t>(epoch) != core_.sceneActorRegistry.Epoch())
					return CmdResult::Err("stale_token", "Actor roster changed; refresh before selecting");
				if (const auto guard = MutationGuardLocked(); !guard.ok) return guard;
				if (!core_.sceneActorRegistry.Resolve(static_cast<uint64_t>(token), static_cast<uint64_t>(epoch)))
					return CmdResult::Err("stale_token", "Actor token is no longer present");
				GuiCommand command{GuiAction::SelectTarget};
				command.actorToken = static_cast<uint64_t>(token);
				command.actorEpoch = static_cast<uint64_t>(epoch);
				core_.QueueGui(std::move(command));
				return CmdResult::Ok();
			}

			// Facial / position ---------------------------------------------------
			if (method == "facial.setWeights")
			{
				double overall = 1, brow = 1, eye = 1, mouth = 1;
				if (!NumberParam(params, "overall", overall) || !NumberParam(params, "brow", brow) ||
					!NumberParam(params, "eye", eye) || !NumberParam(params, "mouth", mouth))
					return CmdResult::Err("invalid_params", "overall/brow/eye/mouth numbers are required");
				for (const double value : {overall, brow, eye, mouth})
					if (value < 0.0 || value > 2.0)
						return CmdResult::Err("invalid_params", "Facial weights must be within 0..2");
				std::lock_guard<std::recursive_mutex> lock(core_.stateMutex);
				if (const auto guard = MutationGuardLocked(); !guard.ok) return guard;
				// Same direct write the ImGui overlay performs under stateMutex.
				core_.player.controller.facialWeights = {
					static_cast<float>(overall), static_cast<float>(brow),
					static_cast<float>(eye), static_cast<float>(mouth)};
				return CmdResult::Ok();
			}
			if (method == "facial.save")
			{
				std::lock_guard<std::recursive_mutex> lock(core_.stateMutex);
				if (const auto guard = MutationGuardLocked(); !guard.ok) return guard;
				core_.QueueGui({GuiAction::SaveFacialWeights});
				return CmdResult::Ok();
			}
			if (method == "position.set")
			{
				double x = 0, y = 0, z = 0;
				if (!NumberParam(params, "x", x) || !NumberParam(params, "y", y) || !NumberParam(params, "z", z))
					return CmdResult::Err("invalid_params", "x/y/z numbers are required");
				for (const double value : {x, y, z})
					if (value < -10.0 || value > 10.0)
						return CmdResult::Err("invalid_params", "Position offset must be within -10..10 meters");
				std::lock_guard<std::recursive_mutex> lock(core_.stateMutex);
				if (const auto guard = MutationGuardLocked(); !guard.ok) return guard;
				core_.QueueInitialPosition({static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)});
				return CmdResult::Ok();
			}
			if (method == "position.save")
			{
				std::lock_guard<std::recursive_mutex> lock(core_.stateMutex);
				if (const auto guard = MutationGuardLocked(); !guard.ok) return guard;
				core_.QueueGui({GuiAction::SaveInitialPosition});
				return CmdResult::Ok();
			}

			// Camera / stage ------------------------------------------------------
			if (method == "camera.set")
			{
				const jsonlite::Value* settings = Param(params, "settings");
				if (!settings || !settings->IsObject())
					return CmdResult::Err("invalid_params", "settings object is required");
				std::lock_guard<std::recursive_mutex> lock(core_.stateMutex);
				if (const auto guard = MutationGuardLocked(); !guard.ok) return guard;
				CameraSettings parsed = core_.camera.settings;
				std::string error;
				if (!ParseCameraSettings(*settings, parsed, error))
					return CmdResult::Err("invalid_params", error);
				if (!parsed.IsValid())
					return CmdResult::Err("invalid_params", "Camera settings failed validation");
				core_.QueueCameraSettings(parsed);
				return CmdResult::Ok();
			}
			if (method == "camera.save")
			{
				std::lock_guard<std::recursive_mutex> lock(core_.stateMutex);
				if (const auto guard = MutationGuardLocked(); !guard.ok) return guard;
				core_.QueueGui({GuiAction::SaveCameraSettings});
				return CmdResult::Ok();
			}
			if (method == "stage.set") return StageSet(params);

			// Settings / shell / appearance ---------------------------------------
			if (method == "settings.setPhysics")
			{
				int64_t backend = -1;
				if (!IntParam(params, "backend", backend) || backend < 0 || backend > 2)
					return CmdResult::Err("invalid_params", "backend must be 0, 1 or 2");
				std::lock_guard<std::recursive_mutex> lock(core_.stateMutex);
				if (const auto guard = MutationGuardLocked(); !guard.ok) return guard;
				core_.QueuePhysicsBackend(static_cast<int>(backend));
				return CmdResult::Ok();
			}
			if (method == "shell.setTextInputActive")
			{
				bool active = false;
				if (!BoolParam(params, "active", active))
					return CmdResult::Err("invalid_params", "active must be a boolean");
				core_.guiTextInputActive.store(active, std::memory_order_relaxed);
				return CmdResult::Ok();
			}
			if (method == "appearance.get")
				return CmdResult::Ok(BuildAppearanceJson());

			// Implemented but capability-gated (not advertised in motion.hello).
			if (method == "dialog.pickFile") return DialogPickFile(params);
			if (method == "dialog.poll") return DialogPoll(params);
			if (method == "drop.setTarget") return DropSetTarget(params);
			if (method == "drop.poll") return DropPoll();

			// Reserved / unimplemented -------------------------------------------
			for (const char* reserved : kReservedMethods)
				if (method == reserved)
					return CmdResult::Err("not_implemented", "Method is reserved for a later migration: " + method);
			for (const char* prefix : kReservedPrefixes)
				if (method.rfind(prefix, 0) == 0)
					return CmdResult::Err("not_implemented", "Domain is reserved for a later migration: " + method);
			return CmdResult::Err("unknown_method", "Unknown method: " + method);
		}

		// ---- command helpers ----------------------------------------------------

		// Caller holds stateMutex. Mirrors the ImGui drain admission rules so a
		// refused command reports a structured error instead of silently dropping.
		CmdResult MutationGuardLocked()
		{
			if (!core_.InjectionAllowed())
				return CmdResult::Err("not_allowed",
					core_.injectionStatus.empty() ? "Injection is not allowed in the current scene" : core_.injectionStatus);
			return CmdResult::Ok();
		}

		CmdResult TransportSimple(GuiAction action, bool requiresClip)
		{
			std::lock_guard<std::recursive_mutex> lock(core_.stateMutex);
			if (const auto guard = MutationGuardLocked(); !guard.ok) return guard;
			if (requiresClip && !core_.player.clip.total_frames)
				return CmdResult::Err("not_ready", "No motion is loaded");
			if (action == GuiAction::Pause &&
				core_.player.state != PlaybackState::Playing && core_.player.state != PlaybackState::Paused)
				return CmdResult::Err("not_ready", "Playback is not active");
			core_.QueueGui({action});
			ForcePlayerEvent();
			return CmdResult::Ok();
		}

		CmdResult SeekCommand(const jsonlite::Value& params, GuiAction action)
		{
			double seconds = 0;
			if (!NumberParam(params, "seconds", seconds) || seconds < 0)
				return CmdResult::Err("invalid_params", "seconds must be a finite number >= 0");
			std::lock_guard<std::recursive_mutex> lock(core_.stateMutex);
			if (const auto guard = MutationGuardLocked(); !guard.ok) return guard;
			if (!core_.player.clip.total_frames) return CmdResult::Err("not_ready", "No motion is loaded");
			const float duration = core_.player.TimelineDuration();
			if (seconds > duration)
				return CmdResult::Err("invalid_params", "seconds exceeds the timeline duration");
			switch (action)
			{
			case GuiAction::Seek: core_.QueueSeek(static_cast<float>(seconds)); break;
			case GuiAction::ScrubBegin: core_.QueueScrubBegin(static_cast<float>(seconds)); break;
			case GuiAction::ScrubUpdate: core_.QueueScrubUpdate(static_cast<float>(seconds)); break;
			case GuiAction::ScrubEnd: core_.QueueScrubEnd(static_cast<float>(seconds)); break;
			default: break;
			}
			ForcePlayerEvent();
			return CmdResult::Ok();
		}

		void ForcePlayerEvent() { forcePlayerEvent_ = true; }

		CmdResult StageSet(const jsonlite::Value& params)
		{
			const jsonlite::Value* settings = Param(params, "settings");
			if (!settings || !settings->IsObject())
				return CmdResult::Err("invalid_params", "settings object is required");
			std::lock_guard<std::recursive_mutex> lock(core_.stateMutex);
			if (const auto guard = MutationGuardLocked(); !guard.ok) return guard;
			StageSettings parsed;
			{
				std::lock_guard<std::mutex> stageLock(core_.stageManager.stageMutex);
				parsed = core_.stageManager.settings;
			}
			std::string error;
			if (!ParseStageSettings(*settings, parsed, error))
				return CmdResult::Err("invalid_params", error);
			// Cross-domain side effects formerly owned by the ImGui stage page.
			bool lightingChanged = false;
			bool restoreSkybox = false;
			{
				std::lock_guard<std::mutex> stageLock(core_.stageManager.stageMutex);
				lightingChanged = parsed.lightingEffectsEnabled != core_.stageManager.settings.lightingEffectsEnabled;
				restoreSkybox = core_.stageManager.settings.hideBackground && !parsed.hideBackground;
			}
			if (parsed.hideBackground)
			{
				parsed.greenScreenActive = true;
				parsed.overrideCameraClear = true;
			}
			else
			{
				parsed.greenScreenActive = false;
				parsed.overrideCameraClear = false;
			}
			if (lightingChanged)
			{
				CameraSettings cameraSettings = core_.camera.settings;
				cameraSettings.actorShadowExperimentMode = parsed.lightingEffectsEnabled ? 1 : 2;
				core_.QueueCameraSettings(cameraSettings);
			}
			core_.QueueStageSettings(parsed);
			if (restoreSkybox) core_.QueueRestoreCameraSkybox();
			return CmdResult::Ok();
		}

		// ---- dialogs / drops (implemented, capability-gated) ---------------------

		CmdResult DialogPickFile(const jsonlite::Value& params)
		{
			int64_t kind = -1;
			bool multiple = false;
			if (!IntParam(params, "kind", kind) || kind < 0 || kind > 3)
				return CmdResult::Err("invalid_params", "kind must be 0 (VMD), 1 (WAV), 2 (camera) or 3 (video)");
			if (!BoolParam(params, "multiple", multiple))
				return CmdResult::Err("invalid_params", "multiple must be a boolean");
			if (multiple)
				return CmdResult::Err("invalid_params", "The host file dialog service only supports single selection");
			if (!core_.fnBeginPickFile)
				return CmdResult::Err("not_implemented", "Host file dialog service is unavailable");
			std::lock_guard<std::recursive_mutex> lock(core_.stateMutex);
			if (dialogPending_ || core_.gui.browsePendingKind >= 0)
				return CmdResult::Err("busy", "A file dialog is already open");
			std::string notice;
			if (!core_.fnBeginPickFile(static_cast<int>(kind), notice))
				return CmdResult::Err("failed", notice.empty() ? "Cannot open the host file dialog" : notice);
			dialogPending_ = true;
			dialogKind_ = static_cast<int>(kind);
			dialogId_ += 1;
			dialogState_ = "pending";
			dialogPaths_.clear();
			dialogNotice_.clear();
			return CmdResult::Ok("{\"dialogId\":" + JNum(static_cast<double>(dialogId_)) + "}");
		}

		CmdResult DialogPoll(const jsonlite::Value& params)
		{
			int64_t dialogId = 0;
			if (!IntParam(params, "dialogId", dialogId) || dialogId != static_cast<int64_t>(dialogId_))
				return CmdResult::Err("invalid_params", "dialogId does not match the current dialog");
			PollBridgeDialog();
			std::string paths = "[";
			for (size_t i = 0; i < dialogPaths_.size(); ++i)
			{
				if (i) paths += ",";
				paths += JString(dialogPaths_[i]);
			}
			paths += "]";
			return CmdResult::Ok("{\"state\":" + JString(dialogState_) + ",\"paths\":" + paths +
				",\"notice\":" + JString(dialogNotice_) + "}");
		}

		void PollBridgeDialog()
		{
			if (!dialogPending_ || !core_.fnPollPickFile) return;
			std::string path;
			std::string notice;
			const int state = core_.fnPollPickFile(path, notice);
			if (state == GAKU_FILE_DIALOG_PENDING) return;
			dialogPending_ = false;
			dialogNotice_ = notice;
			if (state == GAKU_FILE_DIALOG_ACCEPTED)
			{
				dialogState_ = "accepted";
				dialogPaths_.assign(1, path);
			}
			else if (state == GAKU_FILE_DIALOG_CANCELLED) dialogState_ = "cancelled";
			else dialogState_ = "failed";
		}

		CmdResult DropSetTarget(const jsonlite::Value& params)
		{
			bool enabled = false;
			if (!BoolParam(params, "enabled", enabled))
				return CmdResult::Err("invalid_params", "enabled must be a boolean");
			if (!hooks_.dropSetTarget)
				return CmdResult::Err("not_implemented", "Host file-drop service is unavailable");
			std::string notice;
			if (!hooks_.dropSetTarget(enabled, notice))
				return CmdResult::Err("failed", notice.empty() ? "Cannot register the drop target" : notice);
			dropEnabled_ = enabled;
			return CmdResult::Ok();
		}

		CmdResult DropPoll()
		{
			if (!hooks_.dropPeek || !hooks_.dropConsume)
				return CmdResult::Err("not_implemented", "Host file-drop service is unavailable");
			const DropPeek peek = hooks_.dropPeek();
			uint32_t accepted = 0;
			if (peek.pendingCount > 0)
			{
				const DropBatch batch = hooks_.dropConsume();
				if (batch.ok)
				{
					accepted = static_cast<uint32_t>(batch.paths.size());
					lastDropPaths_ = batch.paths;
				}
			}
			return CmdResult::Ok("{\"visual\":" + JString(peek.visual) +
				",\"accepted\":" + JNum(accepted) +
				",\"hostElevated\":" + JBool(peek.hostElevated) +
				",\"hostBlocked\":" + JBool(peek.hostBlocked) + "}");
		}

		// ---- parsing structured settings -----------------------------------------

		static bool ParseCameraSettings(const jsonlite::Value& value, CameraSettings& settings, std::string& error)
		{
			const auto boolField = [&](const char* name, bool& target) {
				const jsonlite::Value* field = value.Find(name);
				if (!field) return true;
				if (!field->IsBool()) { error = std::string(name) + " must be a boolean"; return false; }
				target = field->boolValue;
				return true;
			};
			const auto intField = [&](const char* name, int& target, int low, int high) {
				const jsonlite::Value* field = value.Find(name);
				if (!field) return true;
				if (!field->IsNumber() || field->numberValue != std::floor(field->numberValue) ||
					field->numberValue < low || field->numberValue > high)
				{ error = std::string(name) + " is out of range"; return false; }
				target = static_cast<int>(field->numberValue);
				return true;
			};
			const auto floatField = [&](const char* name, float& target) {
				const jsonlite::Value* field = value.Find(name);
				if (!field) return true;
				if (!field->IsNumber() || !std::isfinite(field->numberValue))
				{ error = std::string(name) + " must be a finite number"; return false; }
				target = static_cast<float>(field->numberValue);
				return true;
			};
			const auto vecField = [&](const char* name, Vector3& target) {
				const jsonlite::Value* field = value.Find(name);
				if (!field) return true;
				if (!field->IsObject()) { error = std::string(name) + " must be an {x,y,z} object"; return false; }
				double x = 0, y = 0, z = 0;
				if (!NumberParam(*field, "x", x) || !NumberParam(*field, "y", y) || !NumberParam(*field, "z", z))
				{ error = std::string(name) + " must carry finite x/y/z"; return false; }
				target = {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
				return true;
			};
			const auto transformField = [&](const char* name, CameraModeTransform& target) {
				const jsonlite::Value* field = value.Find(name);
				if (!field) return true;
				if (!field->IsObject()) { error = std::string(name) + " must be an object"; return false; }
				const jsonlite::Value* position = field->Find("position");
				const jsonlite::Value* rotation = field->Find("rotation");
				if (!position || !rotation) { error = std::string(name) + " needs position and rotation"; return false; }
				const auto parseVec = [&](const jsonlite::Value* object, Vector3& out, const char* label) {
					double x = 0, y = 0, z = 0;
					if (!object->IsObject() || !NumberParam(*object, "x", x) ||
						!NumberParam(*object, "y", y) || !NumberParam(*object, "z", z))
					{ error = std::string(name) + label + " must carry finite x/y/z"; return false; }
					out = {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
					return true;
				};
				if (!parseVec(position, target.position, ".position") ||
					!parseVec(rotation, target.rotation, ".rotation"))
					return false;
				return true;
			};
			int mode = static_cast<int>(settings.mode);
			int followBehavior = static_cast<int>(settings.followBehavior);
			int followTarget = static_cast<int>(settings.followTarget);
			if (!boolField("enabled", settings.enabled) ||
				!intField("mode", mode, 0, 2) ||
				!intField("followBehavior", followBehavior, 0, 1) ||
				!intField("followTarget", followTarget, 0, 2) ||
				!boolField("followActor", settings.followActor) ||
				!boolField("limitToScene", settings.limitToScene) ||
				!vecField("boundsMin", settings.boundsMin) ||
				!vecField("boundsMax", settings.boundsMax) ||
				!vecField("followOffset", settings.followOffset) ||
				!vecField("targetOffset", settings.targetOffset) ||
				!vecField("vmdOffset", settings.vmdOffset) ||
				!floatField("vmdRangeScale", settings.vmdRangeScale) ||
				!vecField("followFactor", settings.followFactor) ||
				!vecField("followRate", settings.followRate) ||
				!floatField("followFov", settings.followFov) ||
				!boolField("srpFinalOverwriteFallback", settings.srpFinalOverwriteFallback) ||
				!intField("actorShadowExperimentMode", settings.actorShadowExperimentMode, 0, 2) ||
				!transformField("transformTrack", settings.transformTrack) ||
				!transformField("transformFollowMove", settings.transformFollowMove) ||
				!transformField("transformLookAt", settings.transformLookAt) ||
				!boolField("usePerModeTransforms", settings.usePerModeTransforms))
				return false;
			settings.mode = static_cast<CameraMode>(mode);
			settings.followBehavior = static_cast<FollowBehavior>(followBehavior);
			settings.followTarget = static_cast<CameraFollowTarget>(followTarget);
			return true;
		}

		static bool ParseStageSettings(const jsonlite::Value& value, StageSettings& settings, std::string& error)
		{
			const auto boolField = [&](const char* name, bool& target) {
				const jsonlite::Value* field = value.Find(name);
				if (!field) return true;
				if (!field->IsBool()) { error = std::string(name) + " must be a boolean"; return false; }
				target = field->boolValue;
				return true;
			};
			const auto floatField = [&](const char* name, float& target) {
				const jsonlite::Value* field = value.Find(name);
				if (!field) return true;
				if (!field->IsNumber() || !std::isfinite(field->numberValue))
				{ error = std::string(name) + " must be a finite number"; return false; }
				target = static_cast<float>(field->numberValue);
				return true;
			};
			const jsonlite::Value* color = value.Find("greenScreenColor");
			if (color)
			{
				if (!color->IsArray() || color->Size() != 4)
				{ error = "greenScreenColor must be an RGBA array of 4 numbers"; return false; }
				for (size_t i = 0; i < 4; ++i)
				{
					const jsonlite::Value* component = color->At(i);
					if (!component || !component->IsNumber() || !std::isfinite(component->numberValue) ||
						component->numberValue < 0 || component->numberValue > 1)
					{ error = "greenScreenColor components must be within 0..1"; return false; }
					settings.greenScreenColor[i] = static_cast<float>(component->numberValue);
				}
			}
			const jsonlite::Value* preset = value.Find("selectedPresetIndex");
			if (preset)
			{
				if (!preset->IsNumber() || preset->numberValue != std::floor(preset->numberValue) ||
					preset->numberValue < 0 || preset->numberValue > 64)
				{ error = "selectedPresetIndex is out of range"; return false; }
				settings.selectedPresetIndex = static_cast<int>(preset->numberValue);
			}
			const auto stringField = [&](const char* name, std::string& target) {
				const jsonlite::Value* field = value.Find(name);
				if (!field) return true;
				if (!field->IsString() || field->stringValue.size() > 512 ||
					field->stringValue.find('\0') != std::string::npos)
				{ error = std::string(name) + " must be a short string"; return false; }
				target = field->stringValue;
				return true;
			};
			return boolField("hideBackground", settings.hideBackground) &&
				boolField("greenScreenActive", settings.greenScreenActive) &&
				floatField("screenDistance", settings.screenDistance) &&
				floatField("screenScale", settings.screenScale) &&
				floatField("screenHeightOffset", settings.screenHeightOffset) &&
				boolField("overrideCameraClear", settings.overrideCameraClear) &&
				stringField("customStageKey", settings.customStageKey) &&
				floatField("stageFloorYOffset", settings.stageFloorYOffset) &&
				floatField("stageYawOffset", settings.stageYawOffset) &&
				floatField("stagePosXOffset", settings.stagePosXOffset) &&
				floatField("stagePosZOffset", settings.stagePosZOffset) &&
				boolField("lightingEffectsEnabled", settings.lightingEffectsEnabled) &&
				boolField("customNodeControl", settings.customNodeControl);
		}

		// ---- snapshot serializers (caller holds stateMutex) ------------------------

		std::string BuildHello() const
		{
			std::string methods = "[";
			for (size_t i = 0; i < std::size(kSupportedMethods); ++i)
			{
				if (i) methods += ",";
				methods += JString(kSupportedMethods[i]);
			}
			methods += "]";
			std::string events = "[";
			for (size_t i = 0; i < std::size(kSupportedEvents); ++i)
			{
				if (i) events += ",";
				events += JString(kSupportedEvents[i]);
			}
			events += "]";
			return "{\"protocol\":1,\"sessionId\":" + JNum(static_cast<double>(sessionId_)) +
				",\"pluginVersion\":" + JString(hooks_.pluginVersion) +
				",\"supportedMethods\":" + methods + ",\"supportedEvents\":" + events + "}";
		}

		std::string PlayerStateJsonLocked() const
		{
			const auto& player = core_.player;
			const char* state = "stopped";
			switch (player.state)
			{
			case PlaybackState::Playing: state = "playing"; break;
			case PlaybackState::Paused: state = "paused"; break;
			case PlaybackState::Preview: state = "preview"; break;
			default: break;
			}
			std::string title = "未加载曲目";
			if (!core_.loadedMotionKey.empty())
			{
				const int index = FindPlaylistIndex(core_.motions, core_.loadedMotionKey);
				title = index >= 0 && !core_.motions[index].title.empty()
					? core_.motions[index].title : core_.loadedMotionKey;
			}
			const int playMode = core_.EnsembleActive() ? (player.loop ? 1 : 0)
				: static_cast<int>(core_.playlist.mode);
			return "{\"state\":" + JString(state) + ",\"title\":" + JString(title) +
				",\"time\":" + JNum(player.currentTime) +
				",\"duration\":" + JNum(player.TimelineDuration()) +
				",\"totalFrames\":" + JNum(player.clip.total_frames) +
				",\"fps\":" + JNum(player.clip.fps) +
				",\"speed\":" + JNum(player.playbackSpeedMultiplier) +
				",\"volume\":" + JNum(player.musicVolume) +
				",\"playMode\":" + JNum(playMode) + "}";
		}

		std::string PermissionJsonLocked() const
		{
			const bool allowed = core_.InjectionAllowed();
			const std::string notice = core_.injectionStatus.empty()
				? "仅偶像摄影与更衣室可以使用；其他页面保留设置但不生效。" : core_.injectionStatus;
			return "{\"injectionAllowed\":" + JBool(allowed) + ",\"notice\":" + JString(notice) + "}";
		}

		std::string LibraryJsonLocked() const
		{
			std::string entries = "[";
			for (size_t i = 0; i < core_.motions.size(); ++i)
			{
				const auto& entry = core_.motions[i];
				if (i) entries += ",";
				entries += "{\"id\":" + JString(entry.name) + ",\"title\":" + JString(entry.title) +
					",\"directory\":" + JString(entry.dir) +
					",\"durationSeconds\":" + JNum(entry.durationSeconds) +
					",\"totalFrames\":" + JNum(entry.totalFrames) +
					",\"hasMotion\":true,\"hasFacial\":" + JBool(entry.hasFacial) +
					",\"hasCamera\":" + JBool(entry.hasCamera) + ",\"hasAudio\":" + JBool(entry.hasAudio) + "}";
			}
			entries += "]";
			return "{\"revision\":" + JNum(static_cast<double>(core_.libraryRevision)) +
				",\"entries\":" + entries +
				",\"refreshing\":" + JBool(core_.libraryScan.valid()) +
				",\"notice\":" + JString(core_.playlistNotice) + "}";
		}

		std::string LoadedJsonLocked() const
		{
			return "{\"motionKey\":" + JString(core_.loadedMotionKey) +
				",\"generation\":" + JNum(static_cast<double>(core_.loadedMotionGeneration)) +
				",\"injectionSession\":" + JNum(static_cast<double>(core_.injectionSession)) +
				",\"sourceMissing\":" + JBool(core_.loadedSourceMissing) +
				",\"sourceChanged\":" + JBool(core_.loadedSourceChanged) + "}";
		}

		std::string PlaylistJsonLocked() const
		{
			const int mode = core_.EnsembleActive() ? (core_.player.loop ? 1 : 0)
				: static_cast<int>(core_.playlist.mode);
			return "{\"mode\":" + JNum(mode) + ",\"notice\":" + JString(core_.playlistNotice) + "}";
		}

		std::string ActorsJsonLocked() const
		{
			const auto& entries = core_.sceneActorRegistry.Entries();
			std::string actors = "[";
			for (size_t i = 0; i < entries.size(); ++i)
			{
				const auto& item = entries[i];
				const std::string name = !item.displayName.empty()
					? item.displayName + " (" + item.character + ")"
					: item.character.empty() ? "未识别角色" : FormatIdolDisplayName(item.character.c_str());
				const std::string label = name + " · " + std::to_string(i + 1);
				if (i) actors += ",";
				actors += "{\"token\":" + JNum(static_cast<double>(item.token)) +
					",\"label\":" + JString(label) +
					",\"selected\":" + JBool(item.actor == core_.player.controller.targetActor) + "}";
			}
			actors += "]";
			const auto labelFor = [&](void* actor) -> std::string {
				for (size_t i = 0; i < entries.size(); ++i)
					if (entries[i].actor == actor)
					{
						const auto& item = entries[i];
						const std::string name = !item.displayName.empty()
							? item.displayName + " (" + item.character + ")"
							: item.character.empty() ? "未识别角色" : FormatIdolDisplayName(item.character.c_str());
						return name + " · " + std::to_string(i + 1);
					}
				return "尚未绑定角色";
			};
			return "{\"epoch\":" + JNum(static_cast<double>(core_.sceneActorRegistry.Epoch())) +
				",\"actors\":" + actors +
				",\"targetLabel\":" + JString(labelFor(core_.player.controller.targetActor)) +
				",\"boundLabel\":" + JString(labelFor(core_.player.controller.currentBinding.actor)) + "}";
		}

		static std::string CameraTransformJson(const CameraModeTransform& transform)
		{
			return "{\"position\":" + JVec(transform.position) + ",\"rotation\":" + JVec(transform.rotation) + "}";
		}

		static std::string CameraSettingsJson(const CameraSettings& settings)
		{
			return "{\"enabled\":" + JBool(settings.enabled) +
				",\"mode\":" + JNum(static_cast<int>(settings.mode)) +
				",\"followBehavior\":" + JNum(static_cast<int>(settings.followBehavior)) +
				",\"followTarget\":" + JNum(static_cast<int>(settings.followTarget)) +
				",\"followActor\":" + JBool(settings.followActor) +
				",\"limitToScene\":" + JBool(settings.limitToScene) +
				",\"boundsMin\":" + JVec(settings.boundsMin) +
				",\"boundsMax\":" + JVec(settings.boundsMax) +
				",\"followOffset\":" + JVec(settings.followOffset) +
				",\"targetOffset\":" + JVec(settings.targetOffset) +
				",\"vmdOffset\":" + JVec(settings.vmdOffset) +
				",\"vmdRangeScale\":" + JNum(settings.vmdRangeScale) +
				",\"followFactor\":" + JVec(settings.followFactor) +
				",\"followRate\":" + JVec(settings.followRate) +
				",\"followFov\":" + JNum(settings.followFov) +
				",\"srpFinalOverwriteFallback\":" + JBool(settings.srpFinalOverwriteFallback) +
				",\"actorShadowExperimentMode\":" + JNum(settings.actorShadowExperimentMode) +
				",\"transformTrack\":" + CameraTransformJson(settings.transformTrack) +
				",\"transformFollowMove\":" + CameraTransformJson(settings.transformFollowMove) +
				",\"transformLookAt\":" + CameraTransformJson(settings.transformLookAt) +
				",\"usePerModeTransforms\":" + JBool(settings.usePerModeTransforms) + "}";
		}

		std::string CameraJsonLocked() const
		{
			const auto snapshot = core_.camera.ReadSnapshot();
			return "{\"settings\":" + CameraSettingsJson(snapshot.settings) +
				",\"available\":" + JBool(snapshot.available) +
				",\"owned\":" + JBool(snapshot.owned) +
				",\"hasTrack\":" + JBool(snapshot.hasTrack) +
				",\"error\":" + JString(snapshot.error) +
				",\"followTargetError\":" + JString(snapshot.followTargetError) +
				",\"notice\":" + JString(core_.cameraNotice) + "}";
		}

		static std::string StageSettingsJson(const StageSettings& settings)
		{
			std::string color = "[";
			for (int i = 0; i < 4; ++i)
			{
				if (i) color += ",";
				color += JNum(settings.greenScreenColor[i]);
			}
			color += "]";
			return "{\"hideBackground\":" + JBool(settings.hideBackground) +
				",\"greenScreenActive\":" + JBool(settings.greenScreenActive) +
				",\"greenScreenColor\":" + color +
				",\"screenDistance\":" + JNum(settings.screenDistance) +
				",\"screenScale\":" + JNum(settings.screenScale) +
				",\"screenHeightOffset\":" + JNum(settings.screenHeightOffset) +
				",\"overrideCameraClear\":" + JBool(settings.overrideCameraClear) +
				",\"selectedPresetIndex\":" + JNum(settings.selectedPresetIndex) +
				",\"customStageKey\":" + JString(settings.customStageKey) +
				",\"loadReplacementKey\":" + JString(settings.loadReplacementKey) +
				",\"stageFloorYOffset\":" + JNum(settings.stageFloorYOffset) +
				",\"stageYawOffset\":" + JNum(settings.stageYawOffset) +
				",\"stagePosXOffset\":" + JNum(settings.stagePosXOffset) +
				",\"stagePosZOffset\":" + JNum(settings.stagePosZOffset) +
				",\"lightingEffectsEnabled\":" + JBool(settings.lightingEffectsEnabled) +
				",\"customNodeControl\":" + JBool(settings.customNodeControl) + "}";
		}

		std::string StageJsonLocked() const
		{
			StageSettings settings;
			std::string sceneName, statusNotice;
			{
				std::lock_guard<std::mutex> stageLock(core_.stageManager.stageMutex);
				settings = core_.stageManager.settings;
				sceneName = core_.stageManager.currentSceneName;
				statusNotice = core_.stageManager.statusNotice;
			}
			return "{\"settings\":" + StageSettingsJson(settings) +
				",\"sceneName\":" + JString(sceneName) +
				",\"statusNotice\":" + JString(statusNotice) + "}";
		}

		std::string FacialJsonLocked() const
		{
			const auto& weights = core_.player.controller.facialWeights;
			return "{\"weights\":{\"overall\":" + JNum(weights.overall) +
				",\"brow\":" + JNum(weights.brow) + ",\"eye\":" + JNum(weights.eye) +
				",\"mouth\":" + JNum(weights.mouth) + "},\"notice\":" + JString(core_.facialWeightsNotice) + "}";
		}

		std::string PositionJsonLocked() const
		{
			return "{\"offset\":" + JVec(core_.player.controller.initialPositionOffset) +
				",\"notice\":" + JString(core_.initialPositionNotice) + "}";
		}

		std::string EnsembleJsonLocked() const
		{
			const auto& snapshot = core_.ensembleSnapshot;
			std::string slots = "[";
			for (size_t i = 0; i < snapshot.settings.slots.size(); ++i)
			{
				const auto& slot = snapshot.settings.slots[i];
				if (i) slots += ",";
				slots += "{\"enabled\":" + JBool(slot.enabled) +
					",\"actorToken\":" + JNum(static_cast<double>(slot.actorToken)) +
					",\"actorHint\":" + JString(slot.actorHint) +
					",\"motionKey\":" + JString(slot.motionKey) +
					",\"offset\":" + JVec(slot.offset) +
					",\"yawDegrees\":" + JNum(slot.yawDegrees) + "}";
			}
			slots += "]";
			std::string physics = "[";
			for (size_t i = 0; i < snapshot.physics.size(); ++i)
			{
				if (i) physics += ",";
				physics += JString(snapshot.physics[i]);
			}
			physics += "]";
			return "{\"settings\":{\"enabled\":" + JBool(snapshot.settings.enabled) +
				",\"masterSlot\":" + JNum(snapshot.settings.masterSlot) +
				",\"cameraSlot\":" + JNum(snapshot.settings.cameraSlot) +
				",\"slots\":" + slots + "},\"revision\":" + JNum(static_cast<double>(snapshot.revision)) +
				",\"actorEpoch\":" + JNum(static_cast<double>(core_.sceneActorRegistry.Epoch())) +
				",\"available\":" + JBool(snapshot.available) +
				",\"verified\":" + JBool(snapshot.verified) +
				",\"probeEnabled\":" + JBool(snapshot.probeEnabled) +
				",\"active\":" + JBool(snapshot.active) +
				",\"notice\":" + JString(snapshot.notice) +
				",\"physics\":" + physics + "}";
		}

		std::string ImportProgressJsonLocked() const
		{
			const auto& progress = core_.importProgress;
			if (!progress.active && !progress.finished) return "null";
			return "{\"active\":" + JBool(progress.active) +
				",\"progress\":" + JNum(progress.progress) +
				",\"statusText\":" + JString(progress.statusText) +
				",\"error\":" + JString(progress.errorDetail) + "}";
		}

		std::string BuildAppearanceJson() const
		{
			AppearanceInfo info;
			if (hooks_.appearance) info = hooks_.appearance();
			return "{\"theme\":" + JNum(info.theme) +
				",\"uiScale\":" + JNum(info.uiScale) +
				",\"dpiScale\":1,\"scaleLimited\":false}";
		}

		std::string BuildSnapshotLocked() const
		{
			return "{\"permission\":" + PermissionJsonLocked() +
				",\"player\":" + PlayerStateJsonLocked() +
				",\"library\":" + LibraryJsonLocked() +
				",\"loaded\":" + LoadedJsonLocked() +
				",\"playlist\":" + PlaylistJsonLocked() +
				",\"actors\":" + ActorsJsonLocked() +
				",\"camera\":" + CameraJsonLocked() +
				",\"stage\":" + StageJsonLocked() +
				",\"facial\":" + FacialJsonLocked() +
				",\"position\":" + PositionJsonLocked() +
				",\"ensemble\":" + EnsembleJsonLocked() +
				",\"physicsBackend\":" + JNum(core_.guiPhysicsBackend.load()) +
				",\"musicVolumeSupport\":" + JNum(core_.player.fnAudioVolumeSupport ? core_.player.fnAudioVolumeSupport() : 0) +
				",\"musicVolumeNotice\":" + JString(core_.musicVolumeNotice) +
				",\"importProgress\":" + ImportProgressJsonLocked() +
				",\"motionsRoot\":" + JString(core_.motionsRoot) + "}";
		}

		// ---- UI tick: async polling + change events -------------------------------

		void Tick()
		{
			PollBridgeDialog();
			PollImGuiDialog();
			if (!visible_ && !baselinePending_) return;

			std::string permission, player, library, loaded, playlist, actors, camera, stage,
				facial, position, ensemble, importProgress, appearance;
			int physicsBackend = 0;
			bool injectionAllowed = false;
			bool scrubbing = false;
			float scrubTime = 0.0f;
			uint64_t injectionSession = 0;
			{
				std::lock_guard<std::recursive_mutex> lock(core_.stateMutex);
				injectionAllowed = core_.InjectionAllowed();
				injectionSession = core_.injectionSession;
				scrubbing = core_.player.scrubbing;
				scrubTime = core_.player.currentTime;
				permission = PermissionJsonLocked();
				player = PlayerStateJsonLocked();
				library = LibraryJsonLocked();
				loaded = LoadedJsonLocked();
				playlist = PlaylistJsonLocked();
				actors = ActorsJsonLocked();
				camera = CameraJsonLocked();
				stage = StageJsonLocked();
				facial = FacialJsonLocked();
				position = PositionJsonLocked();
				ensemble = EnsembleJsonLocked();
				importProgress = ImportProgressJsonLocked();
				physicsBackend = core_.guiPhysicsBackend.load();
			}
			appearance = BuildAppearanceJson();

			// Permission loss / session change: the frontend drops its scrub without
			// sending scrubEnd, so finish the native scrub state through the queue.
			if ((lastPermissionAllowed_ && !injectionAllowed) ||
				(injectionSession != lastInjectionSession_ && lastInjectionSession_ != 0))
			{
				if (scrubbing)
				{
					std::lock_guard<std::recursive_mutex> lock(core_.stateMutex);
					core_.QueueScrubEnd(scrubTime);
				}
			}
			lastPermissionAllowed_ = injectionAllowed;
			lastInjectionSession_ = injectionSession;

			const bool baseline = baselinePending_;
			baselinePending_ = false;
			const auto emitChanged = [&](const char* name, const std::string& json, std::string& last) {
				if (baseline || json != last)
				{
					last = json;
					Emit(name, json);
				}
			};
			emitChanged("permission.changed", permission, lastPermission_);
			emitChanged("library.changed", library, lastLibrary_);
			emitChanged("loaded.changed", loaded, lastLoaded_);
			emitChanged("playlist.changed", playlist, lastPlaylist_);
			emitChanged("actors.changed", actors, lastActors_);
			emitChanged("camera.changed", camera, lastCamera_);
			emitChanged("stage.changed", stage, lastStage_);
			emitChanged("facial.changed", facial, lastFacial_);
			emitChanged("position.changed", position, lastPosition_);
			emitChanged("ensemble.changed", ensemble, lastEnsemble_);
			if (importProgress != "null")
				emitChanged("import.progress", importProgress, lastImportProgress_);
			emitChanged("appearance.changed", appearance, lastAppearance_);
			if (baseline || physicsBackend != lastPhysicsBackend_)
			{
				lastPhysicsBackend_ = physicsBackend;
				Emit("settings.changed", "{\"physicsBackend\":" + JNum(physicsBackend) + "}");
			}

			// player.stateChanged: immediate after commands, ~10 Hz while playing,
			// otherwise only on change.
			const auto now = std::chrono::steady_clock::now();
			const bool playing = player.find("\"state\":\"playing\"") != std::string::npos;
			const bool throttled = playing && !forcePlayerEvent_ && !baseline &&
				now - lastPlayerEmit_ < std::chrono::milliseconds(100);
			if (!throttled && (baseline || forcePlayerEvent_ || player != lastPlayer_))
			{
				lastPlayer_ = player;
				lastPlayerEmit_ = now;
				forcePlayerEvent_ = false;
				Emit("player.stateChanged", player);
			}
		}

		// Keep an ImGui-started dialog progressing while the WebView panel is
		// selected (the ImGui draw loop that normally polls it is not running).
		void PollImGuiDialog()
		{
			std::lock_guard<std::recursive_mutex> lock(core_.stateMutex);
			auto& gui = core_.gui;
			if (gui.browsePendingKind < 0 || !core_.fnPollPickFile) return;
			std::string path;
			const int state = core_.fnPollPickFile(path, gui.browseNotice);
			if (state == GAKU_FILE_DIALOG_PENDING) return;
			if (state == GAKU_FILE_DIALOG_ACCEPTED)
			{
				char* target = gui.browsePendingKind == 0 ? gui.importVmd
					: gui.browsePendingKind == 1 ? gui.importAudio
					: gui.browsePendingKind == 3 ? gui.studioVideoPath : gui.importCameraSource;
				const size_t capacity = gui.browsePendingKind == 0 ? sizeof(gui.importVmd)
					: gui.browsePendingKind == 1 ? sizeof(gui.importAudio)
					: gui.browsePendingKind == 3 ? sizeof(gui.studioVideoPath) : sizeof(gui.importCameraSource);
				if (path.size() >= capacity) gui.browseNotice = "所选路径过长，未修改输入路径";
				else std::memcpy(target, path.c_str(), path.size() + 1);
			}
			gui.browsePendingKind = -1;
		}

		// ---- state ---------------------------------------------------------------

		Core& core_;
		HostHooks hooks_;
		uint64_t sessionId_{0};
		uint64_t seq_{0};
		std::atomic<bool> closed_{false};
		bool visible_{false};
		bool baselinePending_{true};
		mutable std::mutex sinkMutex_;
		GakuWebPanelEventSink sink_{nullptr};
		void* sinkUserData_{nullptr};
		std::wstring assetsDirCache_;

		// Dialog state (bridge-owned ticket; host service owns the OS dialog).
		bool dialogPending_{false};
		int dialogKind_{-1};
		uint64_t dialogId_{0};
		std::string dialogState_{"pending"};
		std::vector<std::string> dialogPaths_;
		std::string dialogNotice_;
		bool dropEnabled_{false};
		std::vector<std::string> lastDropPaths_;

		// Event change detection.
		bool forcePlayerEvent_{false};
		bool lastPermissionAllowed_{false};
		uint64_t lastInjectionSession_{0};
		std::chrono::steady_clock::time_point lastPlayerEmit_{};
		std::string lastPermission_, lastPlayer_, lastLibrary_, lastLoaded_, lastPlaylist_,
			lastActors_, lastCamera_, lastStage_, lastFacial_, lastPosition_, lastEnsemble_,
			lastImportProgress_, lastAppearance_;
		int lastPhysicsBackend_{-1};
	};
} // namespace GakuMotion::webpanel
