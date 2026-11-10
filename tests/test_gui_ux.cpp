// Headless interaction regression for the active product pages. Mouse/key events
// drive actual ImGui widgets; callbacks and commands use the real Core consumer.
#include "GakuMotion.hpp"
#include "../preview/GkMockState.hpp"
#include "imgui_internal.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace GakuMotion;

namespace
{
	struct Item
	{
		ImRect rect;
		std::string label;
		ImGuiItemFlags flags{0};
		ImGuiItemStatusFlags status{0};
		bool inComboPopup{false};
	};
	std::map<ImGuiID, Item> items;
	std::vector<ImGuiID> order;
	int passed = 0, failed = 0, audioPlays = 0, audioResumes = 0, audioPauses = 0;

	void Check(bool condition, const char* message)
	{
		if (condition) { ++passed; std::printf("PASS: %s\n", message); }
		else { ++failed; std::printf("FAIL: %s\n", message); }
	}
	void AudioPlay(const std::string&) { ++audioPlays; }
	void AudioResume(float) { ++audioResumes; }
	void AudioPause() { ++audioPauses; }
	void AudioSeekPaused(float) {}
	int importRequests = 0;
	void CountImportRequest(void*, const MotionImportRequest&) { ++importRequests; }
	int actorSelections = 0;
	uint64_t selectedActorToken = 0, selectedActorEpoch = 0;
	void SelectSceneActor(void*, uint64_t token, uint64_t epoch)
	{
		++actorSelections;
		selectedActorToken = token;
		selectedActorEpoch = epoch;
	}

	// A host extension boundary fake. The production library still submits its
	// actual table rectangle, performs both poll calls and opens the real modal.
	struct FileDropHost
	{
		GakuFileDropService previous{host::fileDrop};
		GakuFileDropTarget target{};
		std::vector<std::string> queued;
		bool enabled{false};
		int consumed{0}, polls{0}, registrations{0};
		FileDropHost() { host::fileDrop = {GAKU_FILE_DROP_VERSION, sizeof(GakuFileDropService), this, SetTarget, Poll}; }
		~FileDropHost() { host::fileDrop = previous; }
		static GakuStatus SetTarget(void* context, const GakuFileDropTarget* value)
		{
			auto& self = *static_cast<FileDropHost*>(context);
			self.enabled = value != nullptr;
			if (value) { self.target = *value; ++self.registrations; }
			else self.queued.clear();
			return GAKU_OK;
		}
		static GakuStatus Poll(void* context, uint32_t size, GakuFileDropResult* result, uint32_t capacity, char* paths)
		{
			auto& self = *static_cast<FileDropHost*>(context);
			++self.polls;
			if (!result || size < sizeof(*result)) return GAKU_E_SIZE;
			*result = {GAKU_FILE_DROP_VERSION, sizeof(*result), static_cast<uint32_t>(self.queued.size()), 0};
			for (const auto& path : self.queued) result->requiredBytes += static_cast<uint32_t>(path.size() + 1);
			if (!paths && !capacity) return GAKU_OK;
			if (!paths || capacity < result->requiredBytes) return GAKU_E_SIZE;
			size_t offset = 0;
			for (const auto& path : self.queued) { std::memcpy(paths + offset, path.c_str(), path.size() + 1); offset += path.size() + 1; }
			self.queued.clear();
			++self.consumed;
			return GAKU_OK;
		}
	};

	void WriteDropVmd(const std::filesystem::path& path, uint32_t bones, uint32_t morphs, uint32_t cameras)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream out(path, std::ios::binary);
		const auto number = [&](uint32_t value) { out.write(reinterpret_cast<const char*>(&value), sizeof(value)); };
		char header[50]{};
		std::memcpy(header, "Vocaloid Motion Data 0002", 25);
		out.write(header, sizeof(header)); number(bones);
		for (uint32_t i = 0; i < bones; ++i) { char frame[111]{}; std::memcpy(frame, "Center", 6); out.write(frame, sizeof(frame)); }
		number(morphs);
		for (uint32_t i = 0; i < morphs; ++i) { char frame[23]{}; std::memcpy(frame, "smile", 5); out.write(frame, sizeof(frame)); }
		number(cameras);
		for (uint32_t i = 0; i < cameras; ++i) { char frame[61]{}; out.write(frame, sizeof(frame)); }
	}

	std::map<std::filesystem::path, std::string> SnapshotFiles(const std::filesystem::path& root)
	{
		std::map<std::filesystem::path, std::string> files;
		for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
			if (entry.is_regular_file())
			{
				std::ifstream in(entry.path(), std::ios::binary);
				files[entry.path().lexically_relative(root)] = std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
			}
		return files;
	}

	struct Harness
	{
		Core core;
		MockGuiState* previewState{nullptr};
		std::filesystem::path fixture;
		bool captureText{false};
		std::string renderedText;
		float windowWidth{1120.0f}, windowHeight{900.0f}, fontScale{1.0f};
		std::function<void(GuiContext&)> contextPatch;

		static void WriteClip(const std::filesystem::path& path)
		{
			std::filesystem::create_directories(path.parent_path());
			std::ofstream out(path, std::ios::binary | std::ios::trunc);
			const auto write = [&](const auto& value) { out.write(reinterpret_cast<const char*>(&value), sizeof(value)); };
			out.write("GKCLIPCL", 8);
			write(uint32_t{4}); write(60.0f); write(uint32_t{901}); write(901.0f / 60.0f); write(uint32_t{1}); write(.083f);
			write(uint16_t{0}); write(uint8_t{4}); write(uint8_t{1}); out.write("Hips", 4);
			for (uint32_t i = 0; i < 901; ++i) { write(Vector3{}); write(Quaternion{}); }
			write(uint8_t{0}); write(uint32_t{0});
		}

		explicit Harness(const std::filesystem::path& root)
		{
			fixture = root / "build/tests" / ("gui-ux-fixture-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
			std::filesystem::create_directories(fixture);
			core.playlistPath = (fixture / "playlist.json").string();
			core.motionsRoot = fixture.string();
			core.gui.motionsRoot = fixture.string();
			core.loadedMotionKey = "A";
			core.loadedMotionGeneration = 1;
			core.currentMotionIndex = 0;
			core.libraryRevision = 1;
			for (const char* name : {"A", "B"})
			{
				MotionEntry entry;
				entry.name = name;
				entry.title = std::string("Song ") + name;
				entry.dir = (fixture / name).string();
				entry.motionFile = (fixture / name / "motion.gkclip").string();
				WriteClip(fixture / name / "motion.gkclip");
				entry.totalFrames = 901;
				entry.durationSeconds = 901.0f / 60.0f;
				core.motions.push_back(entry);
			}
			core.player.currentMotionDir = core.motions[0].dir;
			core.player.loadedMotionFile = core.motions[0].motionFile;
			core.player.clip.fps = 60.0f;
			core.player.clip.total_frames = 901;
			core.player.clip.duration_seconds = 901.0f / 60.0f;
			core.player.state = PlaybackState::Paused;
			core.player.audioStarted = true;
			core.player.fnPlayAudio = AudioPlay;
			core.player.fnResumeAudio = AudioResume;
			core.player.fnPauseAudio = AudioPause;
			core.player.fnSeekPausedAudio = AudioSeekPaused;
			core.gui.selectedLibraryKey = "A";
			core.gui.activePage = GuiPage::Library;
		}

		~Harness()
		{
			core.player.Stop();
			core.WaitForLibraryRefresh();
			std::filesystem::remove_all(fixture); // Unique synthetic directory owned by this harness.
		}

		void Frame()
		{
			items.clear();
			order.clear();
			ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
			ImGui::GetIO().FontGlobalScale = fontScale;
			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(0, 0));
			ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight));
			ImGui::Begin("UX fixture", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
			if (captureText) ImGui::LogToBuffer();
			auto context = previewState ? previewState->BuildContext() : core.BuildGuiContext();
			if (contextPatch) contextPatch(context);
			core.gui.Draw(context, true);
			if (captureText)
			{
				renderedText = GImGui->LogBuffer.c_str();
				ImGui::LogFinish();
				captureText = false;
			}
			ImGui::End();
			ImGui::Render();
			if (!previewState) core.ProcessPendingGuiCommands();
		}

		std::string Text()
		{
			// ImGui ends logging when a tooltip window closes. Keep the synthetic
			// mouse off widgets while collecting rendered text, without touching OS input.
			ImGui::GetIO().AddMousePosEvent(-100.0f, -100.0f);
			captureText = true;
			Frame();
			return renderedText;
		}

		void Settle() { for (int i = 0; i < 6; ++i) Frame(); }

		ImGuiWindow* TransportWindow()
		{
			for (ImGuiWindow* window : GImGui->Windows)
				if (std::string_view(window->Name).find("/##gkmotion_global_transport_") != std::string_view::npos) return window;
			return nullptr;
		}

		ImGuiWindow* LibraryTableWindow()
		{
			for (ImGuiWindow* window : GImGui->Windows)
				if (std::string_view(window->Name).find("/##gkmotion_library_table_") != std::string_view::npos && window->Active) return window;
			return nullptr;
		}

		bool Find(const char* label, ImRect& rectangle, int occurrence = 0)
		{
			for (ImGuiID id : order)
			{
				const auto& item = items.at(id);
				if (item.label == label && occurrence-- == 0) { rectangle = item.rect; return true; }
			}
			std::printf("MISSING ITEM: %s\n", label);
			for (ImGuiID id : order) if (!items[id].label.empty()) std::printf("  item: %s\n", items[id].label.c_str());
			++failed;
			return false;
		}

		const Item* ItemWithLabel(const char* label) const
		{
			for (ImGuiID id : order)
				if (items.at(id).label == label) return &items.at(id);
			return nullptr;
		}

		bool Click(const char* label, int occurrence = 0)
		{
			ImRect rect;
			if (!Find(label, rect, occurrence)) return false;
			const auto center = rect.GetCenter();
			auto& io = ImGui::GetIO();
			io.AddMousePosEvent(center.x, center.y);
			Frame();
			io.AddMouseButtonEvent(0, true);
			Frame();
			io.AddMouseButtonEvent(0, false);
			Frame();
			Frame();
			return true;
		}

		void Key(ImGuiKey key)
		{
			ImGui::GetIO().AddKeyEvent(key, true); Frame();
			ImGui::GetIO().AddKeyEvent(key, false); Frame();
		}

		bool Type(const char* label, const char* text)
		{
			if (!Click(label)) return false;
			auto& io = ImGui::GetIO();
			io.AddKeyEvent(ImGuiMod_Ctrl, true);
			io.AddKeyEvent(ImGuiKey_A, true); Frame();
			io.AddKeyEvent(ImGuiKey_A, false);
			io.AddKeyEvent(ImGuiMod_Ctrl, false); Frame();
			io.AddInputCharactersUTF8(text); Frame();
			return true;
		}
	};

	#include "test_timing_gui.hpp"
	#include "test_responsive_layout.hpp"

	void TestCameraPursuitControls(const std::filesystem::path& root)
	{
		Harness app(root);
		app.core.gui.activePage = GuiPage::Camera;
		app.core.camera.settings.mode = CameraMode::Follow;
		app.core.camera.settings.followBehavior = FollowBehavior::FollowMove;
		app.core.camera.settings.followRate = {0.2f, 0.3f, 0.4f};
		app.core.camera.settings.followFactor = {0.25f, 0.5f, 0.75f};
		app.core.camera.Publish();
		app.Settle();
		const auto mainText = app.Text();
		Check(mainText.find("追随速率 (Target Rate)") != std::string::npos &&
			mainText.find("0 停止追随，0.1 平滑追随，1 即时追随") != std::string::npos &&
			app.ItemWithLabel("##rate_x") && app.ItemWithLabel("##rate_y") && app.ItemWithLabel("##rate_z") &&
			!app.ItemWithLabel("##ff_x"),
			"camera page exposes pursuit rates as primary controls with displacement fractions initially folded away");

		const auto setCameraFloat = [&](const char* label, const char* text)
		{
			// Ctrl-click enters DragFloat's real temporary text editor.
			auto& io = ImGui::GetIO();
			io.AddKeyEvent(ImGuiMod_Ctrl, true);
			if (!app.Click(label)) { io.AddKeyEvent(ImGuiMod_Ctrl, false); app.Frame(); return false; }
			io.AddKeyEvent(ImGuiKey_A, true); app.Frame();
			io.AddKeyEvent(ImGuiKey_A, false);
			io.AddKeyEvent(ImGuiMod_Ctrl, false); app.Frame();
			io.AddInputCharactersUTF8(text); app.Frame();
			app.Key(ImGuiKey_Enter); app.Settle();
			return true;
		};
		setCameraFloat("##rate_x", "0");
		setCameraFloat("##rate_y", "1");
		setCameraFloat("##rate_z", "0.6");
		Check(app.core.camera.settings.followRate.x == 0.0f && app.core.camera.settings.followRate.y == 1.0f &&
			std::fabs(app.core.camera.settings.followRate.z - 0.6f) < 1e-6f &&
			app.core.camera.settings.followFactor.x == 0.25f && app.core.camera.settings.followFactor.y == 0.5f,
			"editing actual camera rate widgets applies zero, immediate and smooth values without changing displacement fractions");
		app.Click("重置追随速率"); app.Settle();
		Check(app.core.camera.settings.followRate.x == 0.1f && app.core.camera.settings.followRate.y == 0.1f &&
			app.core.camera.settings.followRate.z == 0.1f && app.core.camera.settings.followFactor.z == 0.75f,
			"camera pursuit reset restores all rates to 0.1 while preserving legacy displacement fractions");

		app.Click("高级镜头设置"); app.Settle();
		const auto advancedText = app.Text();
		Check(advancedText.find("位移比例（旧版跟随幅度）") != std::string::npos &&
			app.ItemWithLabel("##ff_x") && app.ItemWithLabel("##ff_y") && app.ItemWithLabel("##ff_z"),
			"camera advanced settings explicitly label and expose the independent legacy displacement controls");
		setCameraFloat("##ff_x", "0.9");
		Check(std::fabs(app.core.camera.settings.followFactor.x - 0.9f) < 1e-6f &&
			app.core.camera.settings.followRate.x == 0.1f && app.core.camera.settings.followRate.y == 0.1f &&
			app.core.camera.settings.followRate.z == 0.1f,
			"editing a legacy camera displacement widget preserves all pursuit rates");
		app.Click("恢复完整位移"); app.Settle();
		Check(app.core.camera.settings.followFactor.x == 1.0f && app.core.camera.settings.followFactor.y == 1.0f &&
			app.core.camera.settings.followFactor.z == 1.0f && app.core.camera.settings.followRate.y == 0.1f,
			"restoring full camera displacement preserves the separate smooth pursuit setting");
	}

	void TestCameraFollowTargetControls(const std::filesystem::path& root)
	{
		Harness app(root);
		app.core.gui.activePage = GuiPage::Camera;
		app.core.camera.settings.mode = CameraMode::Follow;
		app.core.camera.settings.followBehavior = FollowBehavior::FollowMove;
		app.core.camera.settings.followTarget = CameraFollowTarget::Hips;
		app.core.camera.settings.followOffset = {0.2f, 0.7f, 2.5f};
		app.core.camera.settings.targetOffset = {0.1f, 0.25f, -0.2f};
		app.core.camera.Publish();
		app.Settle();
		Check(app.ItemWithLabel("跟随部位") != nullptr,
			"camera FollowMove page exposes the actual body-part selector");
		for (const auto& choice : std::vector<std::pair<const char*, CameraFollowTarget>>{
			{"胸部", CameraFollowTarget::Chest}, {"头部", CameraFollowTarget::Head}, {"髋部", CameraFollowTarget::Hips}})
		{
			app.Click("跟随部位"); app.Click(choice.first); app.Settle();
			Check(app.core.camera.settings.followTarget == choice.second &&
				app.core.camera.settings.followOffset.x == 0.2f && app.core.camera.settings.followOffset.y == 0.7f &&
				app.core.camera.settings.followOffset.z == 2.5f && app.core.camera.settings.targetOffset.x == 0.1f &&
				app.core.camera.settings.targetOffset.y == 0.25f && app.core.camera.settings.targetOffset.z == -0.2f,
				"actual camera body-part choices reach Core without changing camera or target offsets");
		}
		app.Click("跟随部位"); app.Click("头部"); app.Settle();
		app.Click("##camera_mode"); app.Click("注视"); app.Settle();
		Check(app.core.camera.settings.followBehavior == FollowBehavior::LookAt &&
			app.core.camera.settings.followTarget == CameraFollowTarget::Head && app.ItemWithLabel("跟随部位"),
			"switching the real camera mode to LookAt preserves its shared selected body part");
		app.Click("跟随部位"); app.Click("胸部"); app.Settle();
		app.Click("##camera_mode"); app.Click("跟随"); app.Settle();
		Check(app.core.camera.settings.followBehavior == FollowBehavior::FollowMove &&
			app.core.camera.settings.followTarget == CameraFollowTarget::Chest,
			"FollowMove reuses the body part selected by the real LookAt controls");
		for (const char* mode : {"VMD", "游戏"})
		{
			app.Click("##camera_mode"); app.Click(mode); app.Settle();
			app.core.camera.settings.followActor = true;
			app.core.camera.Publish(); app.Settle();
			Check(app.core.camera.settings.followTarget == CameraFollowTarget::Chest && !app.ItemWithLabel("跟随部位"),
				"VMD and Game preserve the shared body part but hide its selector even while translating with hips");
		}
		app.Click("##camera_mode"); app.Click("注视"); app.Settle();
		Check(app.core.camera.settings.followTarget == CameraFollowTarget::Chest && app.ItemWithLabel("跟随部位"),
			"returning to a follow camera restores access to the retained body-part selection");
		app.contextPatch = [](GuiContext& context) { context.cameraSnapshot.followTargetError = "selected follow bone unavailable"; };
		const auto errorText = app.Text();
		Check(errorText.find("跟随目标错误") != std::string::npos && errorText.find("selected follow bone unavailable") != std::string::npos,
			"camera page displays follow-target binding failures separately from track errors");
	}

	void TestUnifiedCameraModeControls(const std::filesystem::path& root)
	{
		Harness app(root);
		app.core.gui.activePage = GuiPage::Camera;
		app.core.camera.settings.enabled = true;
		app.core.camera.settings.mode = CameraMode::Track;
		app.core.camera.settings.transformTrack.position = {0.2f, 0.3f, 0.4f};
		app.core.camera.Publish();
		app.Settle();
		Check(!app.ItemWithLabel("启用 GakuMotion 镜头接管") && !app.ItemWithLabel("启用 MMD 镜头接管"),
			"camera page has no separate takeover checkbox");
		app.Click("##camera_mode");
		Check(app.ItemWithLabel("游戏") && app.ItemWithLabel("VMD") && app.ItemWithLabel("跟随") &&
			app.ItemWithLabel("注视") && !app.ItemWithLabel("固定接管时镜头"),
			"camera page offers the same four modes as the player without Hold");
		const auto lagging = app.core.camera.ReadSnapshot().settings;
		app.contextPatch = [lagging](GuiContext& context) { context.cameraSnapshot.settings = lagging; };
		app.Click("跟随"); app.Settle();
		Check(app.core.camera.settings.enabled && app.core.camera.settings.mode == CameraMode::Follow &&
			app.core.camera.settings.followBehavior == FollowBehavior::FollowMove &&
			CameraControlModeIndex(app.core.gui.cameraDraft) == 2 && app.core.gui.cameraSettingsPending &&
			app.Text().find("镜头: 跟随") != std::string::npos,
			"page selection reaches Core and both controls retain the pending choice while snapshots lag");
		app.Click("##camera_quick_mode"); app.Click("镜头: 注视"); app.Settle();
		Check(app.core.camera.settings.enabled && app.core.camera.settings.followBehavior == FollowBehavior::LookAt &&
			CameraControlModeIndex(app.core.gui.cameraDraft) == 3 &&
			app.core.gui.cameraExpected == app.core.camera.settings,
			"player mode changes update the camera page's pending draft without reverting to an old snapshot");
		app.contextPatch = {};
		app.core.camera.Publish(); app.Settle();
		Check(!app.core.gui.cameraSettingsPending && CameraControlModeIndex(app.core.gui.cameraDraft) == 3,
			"both camera entry points acknowledge the new mode after the runtime snapshot arrives");
		app.Click("##camera_mode"); app.Click("游戏"); app.Settle();
		const auto* advanced = app.ItemWithLabel("高级镜头设置");
		Check(!app.core.camera.settings.enabled && !app.ItemWithLabel("##cam_pos_x") && !app.ItemWithLabel("##rate_x") &&
			advanced && (advanced->flags & ImGuiItemFlags_Disabled) &&
			app.Text().find("镜头: 游戏") != std::string::npos,
			"page Game selection releases takeover, synchronizes the player and disables inactive camera parameters");
	}
}

void ImGuiTestEngineHook_ItemAdd(ImGuiContext* context, ImGuiID id, const ImRect& rect, const ImGuiLastItemData* data)
{
	if (!id) return;
	if (items.find(id) == items.end()) order.push_back(id);
	items[id].rect = rect;
	items[id].flags = data ? data->InFlags : 0;
	items[id].inComboPopup = context->CurrentWindow && id != context->CurrentWindow->ID &&
		std::string_view(context->CurrentWindow->Name).find("##Combo_") != std::string_view::npos;
	// This vendored BeginCombo emits ItemAdd but no ItemInfo. Identify its real
	// submitted item by the current ID stack without changing production code.
	if (context->CurrentWindow && !context->CurrentWindow->IDStack.empty())
		for (const char* label : {"##play_mode", "##playback_speed", "##camera_quick_mode", "##physics_engine_select", "##drive_actor_combo", "##camera_mode", "跟随部位"})
			if (ImHashStr(label, 0, context->CurrentWindow->IDStack.back()) == id) items[id].label = label;
}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID id, const char* label, ImGuiItemStatusFlags status)
{
	if (id)
	{
		// Tabs report ItemInfo before ItemAdd in the vendored ImGui build.
		if (items.find(id) == items.end()) order.push_back(id);
		items[id].label = label ? label : "";
		items[id].status = status;
	}
}
void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID id)
{
	const auto found = items.find(id);
	return found == items.end() ? nullptr : found->second.label.c_str();
}

int main(int argc, char** argv)
{
	const auto root = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::current_path();
	ImGui::CreateContext();
	auto& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(1120, 900);
	io.IniFilename = nullptr;
	io.LogFilename = nullptr;
	unsigned char* pixels = nullptr;
	int width = 0, height = 0;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	GImGui->TestEngineHookItems = true;
	TestPlaybackAndTrackTimingGui(root);
	TestResponsiveLibraryAndStageLayout(root);
	TestCameraPursuitControls(root);
	TestCameraFollowTargetControls(root);
	TestUnifiedCameraModeControls(root);
	{
		const ImGuiConfigFlags originalConfigFlags = io.ConfigFlags;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Escape closes native combo popups.
		Harness app(root);
		app.core.gui.activePage = GuiPage::Motion;
		std::vector<GuiSceneActor> roster;
		actorSelections = 0; selectedActorToken = 0; selectedActorEpoch = 0;
		app.contextPatch = [&](GuiContext& context) {
			context.injectionAllowed = true;
			context.sceneActors = roster;
			context.actorRosterEpoch = 77;
			context.fnSelectTargetActor = SelectSceneActor;
			context.targetActorLabel.clear();
			for (auto& actor : context.sceneActors)
			{
				actor.selected = actor.token == selectedActorToken;
				if (actor.selected) context.targetActorLabel = actor.label;
			}
		};
		const auto rowCount = [] {
			int result = 0;
			for (ImGuiID id : order)
				if (items.at(id).inComboPopup && !items.at(id).label.empty()) ++result;
			return result;
		};
		app.Settle();
		const auto* emptyCombo = app.ItemWithLabel("##drive_actor_combo");
		Check(emptyCombo && (emptyCombo->flags & ImGuiItemFlags_Disabled) &&
			app.Text().find("当前场景没有可驱动角色") != std::string::npos,
			"empty scene disables the real actor selector and explains why");
		app.Click("##drive_actor_combo");
		Check(actorSelections == 0 && rowCount() == 0, "empty actor selector cannot submit a target or open phantom rows");

		roster = {{101, "花海咲季", true}}; selectedActorToken = 101;
		app.Settle(); app.Click("##drive_actor_combo");
		Check(rowCount() == 1 && app.ItemWithLabel("花海咲季##actor_101"),
			"one scene actor produces exactly one named dropdown row");
		app.Key(ImGuiKey_Escape); app.Settle();
		roster.push_back({202, "月村手毬", false});
		app.Settle(); app.Click("##drive_actor_combo");
		Check(rowCount() == 2 && app.ItemWithLabel("花海咲季##actor_101") && app.ItemWithLabel("月村手毬##actor_202"),
			"two scene actors produce exactly two rows without all-actors or identity choices");
		app.Click("月村手毬##actor_202");
		Check(actorSelections == 1 && selectedActorToken == 202 && selectedActorEpoch == 77,
			"actual actor choice submits that actor token and the displayed roster epoch exactly once");
		Check(app.Text().find("月村手毬") != std::string::npos,
			"actor selector displays the selected scene character name after dispatch");
		roster = {{202, "月村手毬", true}};
		app.Settle(); app.Click("##drive_actor_combo");
		Check(rowCount() == 1 && !app.ItemWithLabel("花海咲季##actor_101"),
			"actor dropdown removes departed scene actors on the next snapshot");
		app.Key(ImGuiKey_Escape); app.Settle();
		const auto ordinaryText = app.Text();
		Check(!app.ItemWithLabel("只驱动当前绑定角色") && !app.ItemWithLabel("下一个场景角色") &&
			!app.ItemWithLabel("驱动全部场景角色") && !app.ItemWithLabel("##facial_actor_combo") &&
			!app.ItemWithLabel("实验：四角色 Miku 嘴形拟合") && ordinaryText.find("表情适配角色") == std::string::npos &&
			ordinaryText.find("实验：视线") == std::string::npos && ordinaryText.find("表情随驱动角色自动适配") != std::string::npos,
			"ordinary Motion page exposes one actor selector and automatic facial adaptation without experimental controls");

		app.core.gui.activePage = GuiPage::Developer;
		app.core.gui.developerMode = false;
		app.Settle();
		Check(!app.ItemWithLabel("实验：四角色 Miku 嘴形拟合") && !app.ItemWithLabel("DesktopOn（强制桌面追焦）"),
			"developer motion experiments stay hidden while developer mode is off");
		app.core.gui.developerMode = true;
		app.Settle(); app.Click("动作", 1); app.Settle();
		const auto* waistCheckbox = app.ItemWithLabel("实验：腰部支撑（默认开启）");
		Check(waistCheckbox && (waistCheckbox->status & ImGuiItemStatusFlags_Checked), "developer waist support is checked by default");
		app.Click("实验：腰部支撑（默认开启）");
		Check(!app.core.player.controller.IsExperimentalWaistSupportEnabled(), "developer waist checkbox disables support");
		app.Click("实验：腰部支撑（默认开启）");
		Check(app.core.player.controller.IsExperimentalWaistSupportEnabled(), "developer waist checkbox reenables support");
		const auto* fitCheckbox = app.ItemWithLabel("实验：四角色 Miku 嘴形拟合");
		Check(fitCheckbox && (fitCheckbox->status & ImGuiItemStatusFlags_Checked) && app.core.player.controller.facialMikuMouthFit,
			"developer Motion tab exposes the Miku experiment checked by default");
		app.Click("实验：四角色 Miku 嘴形拟合");
		Check(app.core.config.facialMikuMouthFit == 0, "developer Miku experiment checkbox actually disables the configured fit");
		app.Click("实验：四角色 Miku 嘴形拟合");
		Check(app.core.config.facialMikuMouthFit == 1, "developer Miku experiment checkbox can reenable the configured fit");
		const auto gazeText = app.Text();
		const auto* gazeControl = app.ItemWithLabel("DesktopOn（强制桌面追焦）");
		const auto* gazeWeight = app.ItemWithLabel("##eye_slider");
		Check(gazeText.find("实验：视线") != std::string::npos && gazeText.find("尚未接入视线控制器") != std::string::npos &&
			gazeText.find("尚未完成实机验收") != std::string::npos && gazeText.find("独立对视") == std::string::npos &&
			gazeControl && (gazeControl->flags & ImGuiItemFlags_Disabled) && gazeWeight && (gazeWeight->flags & ImGuiItemFlags_Disabled),
			"developer gaze reports its real unavailable experimental state and disables modes and weights");
		const auto gazeMode = app.core.lookAt.gazeMode.load();
		app.Click("DesktopOn（强制桌面追焦）");
		Check(app.core.lookAt.gazeMode.load() == gazeMode && app.core.config.gazeMode == -1,
			"clicking unavailable gaze cannot change the live mode or configuration");
		io.ConfigFlags = originalConfigFlags;
	}
	{
		Harness app(root);
		app.Frame(); app.Frame();
		app.core.configRoot = (app.fixture / "config").string();
		app.Settle();
		ImRect volumeRect;
		if (app.Find("音乐音量", volumeRect))
		{
			auto& input = ImGui::GetIO();
			input.AddMousePosEvent(volumeRect.Min.x + 35.0f, volumeRect.GetCenter().y); app.Frame();
			input.AddMouseButtonEvent(0, true); app.Frame();
			input.AddMouseButtonEvent(0, false); app.Frame(); app.Frame();
			Check(app.core.player.musicVolume < 0.5f &&
				std::filesystem::exists(app.fixture / "config" / "music_volume.json"),
				"actual music volume slider changes the player and saves on release");
			Core restored; restored.configRoot = app.core.configRoot;
			Check(restored.LoadMusicVolume() && std::fabs(restored.player.musicVolume - app.core.player.musicVolume) < .001f,
				"actual slider value is restored by a fresh player");
		}
		app.contextPatch = [](GuiContext& context) { context.musicVolumeSupport = -1; };
		app.Settle();
		Check(app.Text().find("当前音乐后端不支持独立音量") != std::string::npos,
			"unavailable audio volume control is disclosed in the player");
		app.contextPatch = {};
		app.Click("##row", 1);
		Check(app.core.gui.selectedLibraryKey == "B", "actual library single click browses B");
		Check(app.core.currentMotionIndex == 0 && app.core.loadedMotionKey == "A" &&
			app.core.player.loadedMotionFile == app.core.motions[0].motionFile,
			"browsing B preserves Core loaded identity A");

		app.Click("##play_mode");
		app.Click("列表循环");
		Check(app.core.playlist.mode == PlaylistMode::ListLoop && !app.core.player.loop,
			"actual play-mode combo dispatches SetMode rather than SaveList");

		app.Click("设置");
		for (const auto& choice : std::vector<std::pair<const char*, int>>{{"关闭", 0}, {"原生物理（默认，实验性）", 2}, {"内建物理", 1}})
		{
			app.Click("##physics_engine_select");
			app.Click(choice.first);
			Check(app.core.config.secondaryPhysicsBackend == choice.second && app.core.guiPhysicsBackend.load() == choice.second,
				"actual physics combo value reaches the Core consumer");
		}

		app.Click("##camera_quick_mode");
		Check(!app.ItemWithLabel("镜头: 固定") && app.ItemWithLabel("镜头: 游戏") && app.ItemWithLabel("镜头: VMD") &&
			app.ItemWithLabel("镜头: 跟随") && app.ItemWithLabel("镜头: 注视"),
			"quick camera offers four modes and removes the fixed-camera choice");
		app.Click("镜头: 注视");
		app.Click("##camera_quick_mode"); app.Click("镜头: 游戏");
		Check(!app.core.camera.settings.enabled && app.core.camera.settings.mode == CameraMode::Follow &&
			app.core.camera.settings.followBehavior == FollowBehavior::LookAt,
			"quick game-camera choice disables takeover while preserving GM mode");
		Check(app.Text().find("镜头: 游戏") != std::string::npos, "quick camera displays disabled takeover as game camera");
		app.Click("##camera_quick_mode"); app.Click("镜头: VMD");
		Check(app.core.camera.settings.enabled && app.core.camera.settings.mode == CameraMode::Track && !app.core.camera.settings.followActor,
			"quick VMD choice reenables Track mode");
		app.Click("##camera_quick_mode"); app.Click("镜头: 跟随");
		Check(app.core.camera.settings.mode == CameraMode::Follow && app.core.camera.settings.followActor &&
			app.core.camera.settings.followBehavior == FollowBehavior::FollowMove, "quick follow choice consumes FollowMove");
		app.Click("##camera_quick_mode"); app.Click("镜头: 注视");
		Check(app.core.camera.settings.mode == CameraMode::Follow && app.core.camera.settings.followActor &&
			app.core.camera.settings.followBehavior == FollowBehavior::LookAt, "quick look-at choice consumes LookAt");

		app.core.player.state = PlaybackState::Paused;
		app.core.player.audioStarted = true;
		app.core.player.currentTime = 1.25f;
		app.Frame();
		app.Click("##play");
		Check(app.core.player.state == PlaybackState::Playing && audioResumes == 1 && audioPlays == 0,
			"paused play button consumes Resume without reopening audio");
		app.Click("##pause");
		Check(app.core.player.state == PlaybackState::Paused && audioPauses == 1, "pause button consumes Pause");

		app.Type("##transport_time", "00:12.500");
		app.Click("定位##transport_time");
		Check(app.core.player.currentTime == 12.5f, "time input survives focus moving to its actual locate button");
		app.Type("##transport_time", "5.125"); app.Key(ImGuiKey_Enter);
		Check(app.core.player.currentTime == 5.125f, "decimal seconds commit through actual InputText Enter");
		app.Type("##transport_time", "NaN"); app.Key(ImGuiKey_Enter);
		Check(app.core.player.currentTime == 5.125f && !app.core.gui.transportNavigation.error.empty(),
			"invalid time preserves position and displays inline error");
		app.Type("##transport_frame", "750"); app.Key(ImGuiKey_Enter);
		Check(app.core.player.currentTime == 12.5f && app.core.player.state == PlaybackState::Paused,
			"actual sample-frame input dispatches SeekFrame at clip fps");
		app.Click("下一帧");
		Check(app.core.player.navigationFrame == 751, "next-frame button reaches actual relative command consumer");
		app.Click("上一帧");
		Check(app.core.player.navigationFrame == 750, "previous-frame button reaches actual relative command consumer");
		app.Type("##transport_frame", "9000"); app.Key(ImGuiKey_Enter);
		Check(app.core.player.navigationFrame == 750 && !app.core.gui.transportNavigation.error.empty(),
			"out-of-range frame input cannot alter current position");

		app.Type("##transport_time", "9.750");
		++app.core.injectionSession; app.Frame();
		Check(!app.core.gui.transportNavigation.editingTime && std::string(app.core.gui.transportNavigation.time) == "00:12.500",
			"scene session change clears an unsubmitted time draft");
		app.Click("定位##transport_time");
		Check(app.core.player.currentTime == 12.5f, "old session time draft cannot move the current session");
		app.Type("##transport_frame", "100");
		app.Click("##stop");
		Check(app.core.player.state == PlaybackState::Stopped && !app.core.gui.transportNavigation.editingFrame &&
			std::string(app.core.gui.transportNavigation.frame) == "0", "Stop invalidates the pending frame draft");

		app.Click("曲库");
		app.core.player.state = PlaybackState::Playing;
		app.core.player.currentTime = 4.0f;
		app.core.config.autoPlayOnSelect = true;
		app.Frame();
		app.Click("加载预览");
		Check(app.core.loadedMotionKey == "B" && app.core.currentMotionIndex == 1 && app.core.player.state == PlaybackState::Preview &&
			app.core.player.currentTime == 0 && audioPlays == 0,
			"explicit preview of B stays still even while A played and autoplay-on-select is enabled");
		app.core.gui.activePage = GuiPage::Motion;

		for (const float width : {720.0f, 1024.0f, 360.0f})
		{
			app.windowWidth = width;
			app.core.gui.transportNavigation.error.clear();
			app.Settle();
			auto* transport = app.TransportWindow();
			Check(transport != nullptr, "actual transport child is available for layout measurements");
			if (!transport) continue;
			const float normalHeight = transport->Size.y;
			const float blank = transport->Size.y - transport->ContentSize.y - transport->WindowPadding.y * 2.0f;
			std::printf("LAYOUT: host=%.0f transport=%.0f height=%.0f content=%.0f blank=%.0f\n",
				width, transport->Size.x, transport->Size.y, transport->ContentSize.y, blank);
			Check(blank >= -.5f && blank <= ImGui::GetFrameHeightWithSpacing(),
				"settled normal transport has at most one row of trailing blank space");
			Check(transport->ScrollMax.y <= .5f && std::fabs(app.core.gui.transportMeasuredHeight - normalHeight) <= .5f,
				"shell reservation and actual transport content fit the measured height");
			app.core.loadedSourceChanged = true;
			app.Settle();
			transport = app.TransportWindow();
			const auto updateText = app.Text();
			std::printf("SOURCE LAYOUT: host=%.0f height=%.0f content=%.0f scroll=%.0f found=%d\n", width,
				transport->Size.y, transport->ContentSize.y, transport->ScrollMax.y, updateText.find("曲目文件已更新") != std::string::npos);
			Check(transport->Size.y > normalHeight && transport->ScrollMax.y <= .5f &&
				updateText.find("曲目文件已更新") != std::string::npos,
				"source update is actionable in the transport and expands without clipping");
			app.core.loadedSourceChanged = false;
			app.core.loadedSourceMissing = true;
			app.Settle();
			const auto missingText = app.Text();
			std::printf("MISSING LAYOUT: host=%.0f height=%.0f content=%.0f scroll=%.0f found=%d\n", width,
				transport->Size.y, transport->ContentSize.y, transport->ScrollMax.y, missingText.find("曲目源文件已移除") != std::string::npos);
			Check(app.TransportWindow()->ScrollMax.y <= .5f && missingText.find("曲目源文件已移除") != std::string::npos,
				"missing source is visible outside the library page without clipping");
			app.core.loadedSourceMissing = false;
			app.core.gui.transportNavigation.error = "时间格式无效：请输入非负秒数或 mm:ss.mmm。请检查输入后重新定位。";
			app.Settle();
			transport = app.TransportWindow();
			Check(transport->Size.y > normalHeight && transport->ScrollMax.y <= .5f,
				"inline error expands actual transport height without clipping");
			const float originalDuration = app.core.player.clip.duration_seconds;
			app.core.player.clip.duration_seconds = .5f;
			app.Settle();
			transport = app.TransportWindow();
			Check(transport->Size.y > normalHeight && transport->ScrollMax.y <= .5f,
				"combined timebase warning and error remain fully visible");
			app.core.player.clip.duration_seconds = originalDuration;
			app.core.gui.transportNavigation.error.clear();
			app.Settle();
			Check(std::fabs(app.TransportWindow()->Size.y - normalHeight) <= .5f,
				"removing messages restores compact transport height");
		}
		app.windowWidth = 720.0f;
		app.fontScale = 1.35f;
		app.Settle();
		if (auto* transport = app.TransportWindow())
		{
			const float blank = transport->Size.y - transport->ContentSize.y - transport->WindowPadding.y * 2.0f;
			Check(transport->ScrollMax.y <= .5f && blank <= ImGui::GetFrameHeightWithSpacing() &&
				std::fabs(app.core.gui.transportMeasuredFontSize - ImGui::GetFontSize()) < .01f,
				"font-size change invalidates the cache and settles without clipping or excess blank rows");
		}
		app.windowWidth = 360.0f;
		app.fontScale = 1.0f;
		app.core.gui.activePage = GuiPage::Library;
		app.core.loadedSourceChanged = true;
		app.Settle();
		const auto narrowLibraryText = app.Text();
		if (auto* transport = app.TransportWindow())
		{
			std::printf("LIBRARY SOURCE LAYOUT: y=%.0f height=%.0f content=%.0f scroll=%.0f clip=%.0f..%.0f found=%d\n", transport->Pos.y, transport->Size.y,
				transport->ContentSize.y, transport->ScrollMax.y, transport->InnerClipRect.Min.y, transport->InnerClipRect.Max.y,
				narrowLibraryText.find("曲目文件已更新") != std::string::npos);
			Check(transport->ScrollMax.y <= .5f && narrowLibraryText.find("曲目文件已更新") != std::string::npos,
				"narrow library also retains the visible source-update transport notice");
		}
		app.windowWidth = 1120.0f;
		app.core.loadedSourceChanged = false;
		app.Settle();
		const auto read = [](const std::filesystem::path& path) {
			std::ifstream in(path, std::ios::binary);
			return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		};
		app.core.SavePlaylist();
		const auto playlistBeforeCancel = read(app.core.playlistPath);
		const auto clipBeforeCancel = read(app.fixture / "B" / "motion.gkclip");
		app.Click("删除选中");
		ImRect confirmationButton;
		Check(app.core.gui.pendingDeleteDirectory == (app.fixture / "B").string() && app.Find("永久删除", confirmationButton),
			"actual delete confirmation displays the selected directory");
		app.Click("取消删除");
		Check(read(app.core.playlistPath) == playlistBeforeCancel && read(app.fixture / "B" / "motion.gkclip") == clipBeforeCancel &&
			!std::filesystem::exists(app.fixture / ".gakumotion-trash"), "actual cancel performs zero disk/list writes");
		app.Click("删除选中");
		app.Key(ImGuiKey_Escape);
		Check(read(app.core.playlistPath) == playlistBeforeCancel && std::filesystem::exists(app.fixture / "B"),
			"Escape cancels deletion without changing files");
		app.Click("删除选中");
		++app.core.libraryRevision;
		app.Frame();
		app.Click("永久删除");
		Check(std::filesystem::exists(app.fixture / "B") && FindPlaylistIndex(app.core.motions, "B") >= 0,
			"library revision change disables stale confirmation");
		app.Click("取消删除");
		Check(!app.ItemWithLabel("从列表移除") && !app.ItemWithLabel("恢复已移除曲目") && !app.ItemWithLabel("恢复最近删除"),
            "library has no list-removal or recovery controls");
		app.Settle();
		app.Click("##row", 1);
		app.Click("删除选中");
		app.Click("永久删除");
		Check(!std::filesystem::exists(app.fixture / "B") && FindPlaylistIndex(app.core.motions, "B") < 0,
			"actual explicit confirmation permanently deletes the selected track");
		app.core.RefreshMotionLibrary();
        app.Settle();
        Check(!std::filesystem::exists(app.fixture / "B") && FindPlaylistIndex(app.core.motions, "B") < 0 &&
            !std::filesystem::exists(app.fixture / ".gakumotion-trash"),
            "refresh cannot resurrect permanently deleted files and creates no recovery area");
	}
	{
		Harness app(root);
		FileDropHost drop;
		app.contextPatch = [](GuiContext& context) { context.fnImportMotion = CountImportRequest; };
		app.Settle();
		const auto validTableTarget = [&]() {
			const auto* table = app.LibraryTableWindow();
			return table && drop.enabled && drop.target.right > drop.target.left && drop.target.bottom > drop.target.top &&
				drop.target.left >= table->OuterRectClipped.Min.x - .5f && drop.target.top >= table->OuterRectClipped.Min.y - .5f &&
				drop.target.right <= table->OuterRectClipped.Max.x + .5f && drop.target.bottom <= table->OuterRectClipped.Max.y + .5f;
		};
		Check(validTableTarget(), "file drop registers the actual visible library table rectangle");
		std::snprintf(app.core.gui.libraryFilter.InputBuf, sizeof(app.core.gui.libraryFilter.InputBuf), "no matching song");
		app.core.gui.libraryFilter.Build(); app.Settle();
		Check(validTableTarget(), "fully filtered library retains a nonempty visible table drop target");
		app.core.gui.libraryFilter.Clear();
		const auto originalMotions = app.core.motions;
		app.core.motions.clear(); ++app.core.libraryRevision; app.Settle();
		Check(validTableTarget(), "empty library retains a nonempty visible table drop target");
		app.core.motions = originalMotions; ++app.core.libraryRevision; app.Settle();

		const auto sources = app.fixture / L"拖入来源 日本語";
		const auto motion = sources / L"舞蹈动作.vmd", facial = sources / L"表情.vmd", camera = sources / L"カメラ.vmd";
		const auto audio = sources / L"歌曲.wav", unsupported = sources / L"说明.txt";
		WriteDropVmd(motion, 1, 0, 0); WriteDropVmd(facial, 0, 1, 0); WriteDropVmd(camera, 0, 0, 1);
		std::ofstream(audio, std::ios::binary).write("RIFF synthetic test audio", 25);
		std::ofstream(unsupported).write("unsupported fixture", 19);
		const auto utf8 = [](const std::filesystem::path& path) { return WideToUtf8(path.wstring()); };
		app.core.SavePlaylist();
		const auto before = SnapshotFiles(app.fixture);
		const auto revision = app.core.libraryRevision;
		const int requestsBefore = importRequests;
		drop.queued = {utf8(motion), utf8(facial), utf8(camera), utf8(audio), utf8(motion), utf8(unsupported), utf8(sources), utf8(sources / L"不存在.vmd")};
		app.Frame();
		const auto* addModal = ImGui::FindWindowByName("添加到曲库##modal");
		Check(drop.consumed == 1 && g_libraryModal.isOpen && !g_libraryModal.isEditMode && addModal && addModal->Active,
			"one multi-file UTF-8 drop opens the existing add-to-library modal");
		const auto containsOnce = [](const auto& list, const std::string& path) { return std::count(list.begin(), list.end(), path) == 1; };
		Check(containsOnce(g_libraryModal.motionCandidates, utf8(motion)) && containsOnce(g_libraryModal.facialCandidates, utf8(facial)) &&
			containsOnce(g_libraryModal.cameraCandidates, utf8(camera)) && containsOnce(g_libraryModal.audioCandidates, utf8(audio)) &&
			g_libraryModal.motionCandidates[g_libraryModal.motionSelection] == utf8(motion),
			"drop preserves Unicode candidates, deduplicates files and uses the existing VMD classifier");
		Check(!drop.enabled && g_libraryModal.probeCache.find(utf8(unsupported)) == g_libraryModal.probeCache.end() &&
			app.core.gui.libraryUiNotice.find("3") != std::string::npos,
			"drop ignores unsupported files, directories and missing files and disables competing drops");
		Check(importRequests == requestsBefore && app.core.motions.size() == originalMotions.size() && app.core.libraryRevision == revision &&
			app.core.loadedMotionKey == "A" && SnapshotFiles(app.fixture) == before,
			"opening the drop modal does not submit an import, change playback or mutate the library files");
		const auto candidates = g_libraryModal.motionCandidates;
		drop.queued = {utf8(facial)}; app.Frame();
		Check(drop.consumed == 1 && g_libraryModal.motionCandidates == candidates && drop.queued.empty(),
			"a second drop cannot replace candidates in an already-open add modal");
		app.Click("取消"); app.Settle();
		Check(!g_libraryModal.isOpen && importRequests == requestsBefore && SnapshotFiles(app.fixture) == before,
			"cancel after file drop performs zero library writes or import requests");

		for (int busy = 0; busy < 2; ++busy)
		{
			app.Settle(); drop.queued = {utf8(motion)};
			app.contextPatch = [busy](GuiContext& context) { context.fnImportMotion = CountImportRequest; context.importBusy = busy == 0; context.libraryRefreshing = busy == 1; };
			const int pollsBefore = drop.polls;
			app.Frame();
			Check(!drop.enabled && drop.queued.empty() && drop.polls == pollsBefore && !g_libraryModal.isOpen,
				busy == 0 ? "busy import clears and rejects file drops" : "library refresh clears and rejects file drops");
			app.contextPatch = [](GuiContext& context) { context.fnImportMotion = CountImportRequest; };
		}
		app.Settle(); app.Click("管理选中");
		const auto editKey = g_libraryModal.editTargetKey;
		drop.queued = {utf8(motion)}; app.Frame();
		Check(g_libraryModal.isOpen && g_libraryModal.isEditMode && g_libraryModal.editTargetKey == editKey && !drop.enabled && drop.consumed == 1,
			"an edit modal rejects file drops without replacing its selected song");
		app.Click("取消"); app.Settle();
		app.Click("删除选中");
		drop.queued = {utf8(motion)}; app.Frame();
		Check(!drop.enabled && !g_libraryModal.isOpen && drop.queued.empty() && drop.consumed == 1,
			"another modal rejects file drops without opening an import modal");
		app.Click("取消删除"); app.Settle();
		Check(importRequests == requestsBefore && SnapshotFiles(app.fixture) == before,
			"all blocked drop paths preserve source bytes and playlist state");

		// Missing-glyph fallback widths conceal Chinese-label overflow. Exercise
		// the actual Windows CJK font when it is available on the test machine.
		ImFont* originalFont = io.FontDefault;
		wchar_t windowsDirectory[MAX_PATH]{};
		const UINT windowsLength = GetWindowsDirectoryW(windowsDirectory, MAX_PATH);
		const auto cjkPath = std::filesystem::path(windowsDirectory) / L"Fonts" / L"msyh.ttc";
		if (windowsLength && windowsLength < MAX_PATH && std::filesystem::exists(cjkPath))
		{
			io.FontDefault = io.Fonts->AddFontFromFileTTF(cjkPath.string().c_str(), 18.0f, nullptr, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
			io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		}
		app.windowWidth = 720.0f;
		for (const float scale : {1.25f, 1.50f})
		{
			app.fontScale = scale; app.Settle();
			bool fits = true;
			int buttons = 0;
			for (ImGuiWindow* window : GImGui->Windows)
				if (window->Active && std::string_view(window->Name).find("/##gkmotion_main_content_") != std::string_view::npos)
					for (ImGuiID id : order)
						if (std::string_view(items[id].label) == "打开曲库文件夹" || items[id].label == "管理选中" || items[id].label == "删除选中" || items[id].label == "播放选中")
						{ ++buttons; fits = fits && items[id].rect.Max.x <= window->ContentRegionRect.Max.x + .5f; }
			Check(fits && buttons >= 3, scale == 1.25f ? "icon library toolbar wraps Chinese labels within a 720px window at 125%" : "icon library toolbar wraps Chinese labels within a 720px window at 150%");
		}
		app.fontScale = 1.50f;
		app.windowHeight = 820.0f;
		app.core.camera.settings.enabled = true;
		app.core.camera.settings.mode = CameraMode::Follow;
		app.core.camera.settings.followBehavior = FollowBehavior::FollowMove;
		app.core.camera.Publish();
		for (const float transportWidth : {720.0f, 1120.0f})
		{
			app.windowWidth = transportWidth; app.Settle();
			ImRect playMode, cameraMode, previousFrame, nextFrame, timeButton, frameButton;
			const bool controls = app.Find("##play_mode", playMode) && app.Find("##camera_quick_mode", cameraMode) &&
				app.Find("上一帧", previousFrame) && app.Find("下一帧", nextFrame) &&
				app.Find("定位##transport_time", timeButton) && app.Find("定位##transport_frame", frameButton);
			const auto& style = ImGui::GetStyle();
			Check(controls && playMode.GetWidth() - ImGui::GetFrameHeight() - 2.0f * style.FramePadding.x >= ImGui::CalcTextSize("单曲循环").x - .5f &&
				cameraMode.GetWidth() - ImGui::GetFrameHeight() - 2.0f * style.FramePadding.x >= ImGui::CalcTextSize("镜头: 跟随").x - .5f,
				"150% CJK transport combo previews retain full labels beside their arrow buttons");
			Check(controls && previousFrame.GetWidth() >= ImGui::CalcTextSize("上一帧").x + 2.0f * style.FramePadding.x - .5f &&
				nextFrame.GetWidth() >= ImGui::CalcTextSize("下一帧").x + 2.0f * style.FramePadding.x - .5f,
				"150% CJK frame-step buttons retain complete text labels");
			const auto* transport = app.TransportWindow();
			bool allVisible = controls && transport && transport->ScrollMax.y <= .5f;
			if (transport)
				for (const auto& rectangle : {playMode, cameraMode, previousFrame, nextFrame, timeButton, frameButton})
					allVisible = allVisible && rectangle.Min.x >= transport->InnerClipRect.Min.x - .5f &&
						rectangle.Max.x <= transport->InnerClipRect.Max.x + .5f && rectangle.Min.y >= transport->InnerClipRect.Min.y - .5f &&
						rectangle.Max.y <= transport->InnerClipRect.Max.y + .5f;
			Check(allVisible, transportWidth == 720.0f ? "150% CJK transport wraps all controls inside a 720x820 host" : "150% CJK transport keeps all controls visible inside a 1120x820 host");
		}
		io.FontDefault = originalFont;
		io.FontGlobalScale = 1.0f;
	}
    {
        MockGuiState preview;
        Harness app(root);
        app.previewState = &preview;
        app.core.gui.selectedLibraryKey = preview.motions.front().name;
        const auto key = preview.motions.front().name;
        const auto original = preview.motions.front();
        const auto revision = preview.libraryRevision;
        app.Settle();
        app.Click("删除选中");
        ImRect confirmationButton;
        Check(app.core.gui.pendingDeleteDirectory == original.dir && app.Find("永久删除", confirmationButton),
            "standalone mock supplies the same confirmation key and target-directory contract");
        app.Click("取消删除");
        Check(preview.motions.size() == 3 && preview.libraryRevision == revision,
            "standalone mock cancel preserves all memory state");
        auto context = preview.BuildContext();
        context.fnPlaylistAction(context.self, static_cast<int>(GuiPlaylistAction::DeleteFiles), key.c_str(), 0);
        Check(preview.motions.size() == 3, "standalone mock rejects deletion without explicit confirmation");
        app.Click("删除选中");
        ++preview.libraryRevision; app.Frame();
        app.Click("永久删除");
        Check(preview.motions.size() == 3, "standalone mock revision invalidates an open confirmation");
        app.Click("取消删除");
        app.Click("删除选中");
        app.Click("永久删除");
        Check(preview.motions.size() == 2 && preview.selectedMotionIndex == -1 &&
            preview.player.state == PlaybackState::Stopped && preview.player.loadedMotionFile.empty(),
            "standalone mock confirm simulates deletion and releases the loaded identity");
        Check(preview.playlistNotice.find("未操作本地文件") != std::string::npos,
            "standalone mock explicitly labels file operations as memory-only");
        Check(!app.ItemWithLabel("恢复最近删除") && !app.ItemWithLabel("恢复已移除曲目") && !app.ItemWithLabel("从列表移除"),
            "standalone preview has no removal or recovery controls");
        preview.Reset();
        const auto secondKey = preview.motions.front().name;
        context = preview.BuildContext();
        preview.importBusy = true;
        context.fnPlaylistAction(context.self, static_cast<int>(GuiPlaylistAction::DeleteFiles), secondKey.c_str(), 1);
        Check(preview.motions.size() == 3, "standalone mock import blocks competing deletion");
        preview.importBusy = false;
        context.fnPlaylistAction(context.self, static_cast<int>(GuiPlaylistAction::DeleteFiles), secondKey.c_str(), 1);
        Check(preview.motions.size() == 2, "standalone mock deletes after import is no longer busy");
    }
	ImGui::DestroyContext();
	std::printf("GUI UX interaction checks: %d passed, %d failed\n", passed, failed);
	return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
