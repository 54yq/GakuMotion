#pragma once

// =============================================================================
// GkGuiOverlay.hpp — GakuMotion 图形化控制面板（ImGui）
// =============================================================================
// 构建脚本默认启用 ImGui。面板通过 GakuLoader 的 gakuloader.desktop-ui 协商
// 版本、结构布局、context 和 allocator；本插件不拥有 Present/WndProc/backend。
// 无 ImGui 的离线编译配置仍然可用。
//
// 面板通过 GuiContext 与 Core 解耦：
//   - 只读数据用裸指针暴露（nullptr 安全）；
//   - 需要改动运行时的动作走函数指针回调（self 为 Core*），
//     避免 GuiOverlay 反向依赖 Core（Core 持有 GuiOverlay，会产生循环包含）。
//
// 面板渲染逻辑按 Tab 机械拆分至 src/gui/ 目录下各组件中，保持完全相同的
// 状态契约与渲染行为。
// =============================================================================

#include "GkMotionPlayer.hpp"
#include "GkTransportTime.hpp"
#include "GkLookAtManager.hpp"
#include "GkRuntimeConfig.hpp"
#include "GkFolderLibrary.hpp"
#include "GkBoneOverride.hpp"
#include "GkPlaylist.hpp"
#include "GkSoleMeasurement.hpp"
#include "GkExperimentalSolePitch.hpp"
#include "GkStageManager.hpp"
#include "GkCamera.hpp"
#include "GkSceneActors.hpp"
#include "GkPhysicsMonitor.hpp"
#include "GkLiveFrameMonitor.hpp"
#include "GkLiveEndLoopContract.hpp"
#include "gui/GuiOverlayCommon.hpp"

#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <cctype>
#include <cmath>
#include <algorithm>

#if defined(GKMOTION_ENABLE_IMGUI) || defined(HAS_IMGUI)
#include <imgui.h>
#define GKMOTION_GUI_ACTIVE 1
#endif
#include "gui/GuiSwingCheckpoint.hpp"

namespace GakuMotion
{
	// -----------------------------------------------------------------------------
	// 面板与 Core 之间的数据/动作契约
	// -----------------------------------------------------------------------------
	struct CameraImportRequest
	{
		std::string source, python, motionKey, motionDirectory, script, log;
		float fps{60}, scale{0.083f};
		bool detectCuts{true};
	};
	struct GuiEnsembleFollower
	{
		uint64_t actorToken{0};
		std::string motionKey;
		float offsetSeconds{0};
		bool active{false};
		uint64_t bindingGeneration{0};
		Vector3 position{};
		float yawDegrees{0};
		float wristTwistShare{0}, forearmAxisDegrees{45};
		bool armLocked{false};
		std::string physicsStatus;
	};

	struct GuiArmCalibration
	{
		uint64_t token{0};
		float share{0}, degrees{45};
		bool locked{false}, bound{false}, primary{false};
	};

	struct GuiContext
	{
		// ---- 只读状态 ----
		bool injectionAllowed{false};
		std::string injectionNotice;
		bool liveObserved{false}, livePaused{false};
		bool liveViewingConfirmed{false};
        LiveFrameMonitor::Snapshot liveFrameMonitor{};
        uint32_t liveFrameFollowers{};
		bool liveTakeoverActive{false}, liveTakeoverAvailable{false};
		bool liveAspectPreviewRequested{true};
		void (*fnSetLiveAspectPreview)(void*, bool){nullptr};
		bool liveLoopEnabled{false};
		float liveLoopLead{1.0f};
		uint64_t liveLoopCycles{};
		bool liveActorClockAvailable{},liveActorClockActive{};
		std::string liveActorClockNotice;
		void (*fnActorClockTrial)(void*,bool){nullptr};
		void (*fnSetLiveLoopLead)(void*,float){nullptr};
		void (*fnSetLiveLoop)(void*, bool){nullptr};
		bool liveDofDisabled{false};
		int liveDofDisableStatus{0};
		void (*fnSetLiveDofDisabled)(void*, bool){nullptr};
		float liveTime{0}, liveDuration{0};
		bool liveAudioAvailable{false};float liveAudioTime{0},liveAudioDuration{0};unsigned liveAudioSources{0};
		std::string liveAudioNotice;void (*fnSeekLiveAudio)(void*,float){nullptr};
		std::string liveControlNotice;
		void (*fnSetLivePaused)(void*, bool){nullptr};
		void (*fnSeekLive)(void*, float){nullptr};
		void (*fnRecordLiveState)(void*){nullptr};
        bool liveContinuousArmDiagnostics{false};
        void (*fnSetContinuousArmDiagnostics)(void*, bool){nullptr};
		void (*fnLoadSwingReport)(void*){nullptr};
		void (*fnSetLiveTakeover)(void*, bool){nullptr};
		MotionPlayer* player{nullptr};
		CameraController::Snapshot cameraSnapshot;
		void (*fnSetCameraSettings)(void*, const CameraSettings&){nullptr};
		void (*fnImportCamera)(void*, const CameraImportRequest&){nullptr};
		void (*fnSaveCameraSettings)(void*){nullptr};
		void (*fnReloadCamera)(void*){nullptr};
		bool cameraImportBusy{false};
		std::string cameraNotice, cameraSource;
		uint32_t cameraFrames{0};
		float cameraFps{0};
		LookAtManager* lookAt{nullptr};
		RuntimeConfig* config{nullptr};
		const std::vector<MotionEntry>* motions{nullptr};
		const BoneOverrideSet* overrides{nullptr};
		bool* masterOverridesEnabled{nullptr};
		const int* selectedMotionIndex{nullptr};
        std::vector<std::string> folderLibraryRoots;
        std::string folderLibraryNotice;
        void (*fnAddLibraryFolder)(void*, const char*){nullptr};
        void (*fnRemoveLibraryFolder)(void*, const char*){nullptr};
		std::string statusLine;
		int physicsBackend{2}; // Published selection, not the render thread's config.
		CostumeCaptureStatus costumeCaptureStatus{};
		void (*fnCaptureCostumePhysics)(void*){};
		PhysicsMonitor physicsMonitor{};
		bool physicsMonitorFresh{false};
		bool physicsGraphCurrent{false};
		bool physicsLiveObservationCurrent{false};
		bool placementResetExperiment{true};
		void (*fnSetPlacementResetExperiment)(void*, bool){nullptr};
		bool beforeQuartzExperiment{true};
		void (*fnSetBeforeQuartzExperiment)(void*, bool){nullptr};
		bool experimentalWaistSupport{true};
		void (*fnSetExperimentalWaistSupport)(void*, bool){nullptr};
		int playlistMode{1};
		bool importBusy{false};
		const ImportProgressState* importProgress{nullptr};
		std::string playlistNotice;
		uint64_t libraryRevision{0}, loadedMotionGeneration{0}, injectionSession{0};
		std::string libraryNotice, loadedMotionKey;
		bool initializeBeforeMotion{false};
		void (*fnSetInitializeBeforeMotion)(void*, bool){nullptr};
		bool libraryRefreshing{false}, loadedSourceMissing{false}, loadedSourceChanged{false};
		std::string boundActorLabel, targetActorLabel;
		std::string initialPositionNotice;
		std::string facialWeightsNotice;
		void (*fnSaveFacialWeights)(void*){nullptr};
		SoleMeasurementSettings soleSettings;
		SoleMeasurementSnapshot soleSnapshot;
		sole_pitch::Settings experimentalSolePitchSettings;
		sole_pitch::Snapshot experimentalSolePitchSnapshot;
		void (*fnSetExperimentalSolePitch)(void*, const sole_pitch::Settings&){nullptr};
		std::string soleNotice;
		uint64_t soleCalibrationGeneration{0};
		void (*fnSetSoleSettings)(void*, const SoleMeasurementSettings&){nullptr};
		void (*fnSaveSoleSettings)(void*){nullptr};
		void (*fnLoadSoleSettings)(void*){nullptr};
		void (*fnPlaylistAction)(void*, int action, const char* key, int value){nullptr};
		void (*fnImportMotion)(void*, const MotionImportRequest&){nullptr};
		bool (*fnPickFile)(int, std::string&){nullptr};
		// Async begin returns immediately; poll: 0=pending, 1=accepted,
		// 2=cancelled, 3=failed. The caller owns no native dialog/window.
		bool (*fnBeginPickFile)(int, std::string&){nullptr};
		int (*fnPollPickFile)(std::string&, std::string&){nullptr};
		void (*fnSetMuted)(void*, bool){nullptr};
		void (*fnSetSpeed)(void*, float){nullptr};
		void (*fnSetMusicVolume)(void*, float){nullptr};
		void (*fnSaveMusicVolume)(void*){nullptr};
		int musicVolumeSupport{0};
		std::string musicVolumeNotice;
		void (*fnSetInitialPosition)(void*, const Vector3&){nullptr};
		void (*fnSetInitialYaw)(void*, float){nullptr};
		void (*fnSaveInitialPosition)(void*){nullptr};

		// ---- 场景与舞台 (Phase 1 & 2 官方舞台挂载与绿幕) ----
		StageSettings stageSettings;
		std::vector<StageCandidate> stageCandidates;
		std::string stageSceneName;
		std::string stageMountedKey;
		std::string stageStatusNotice;
		std::string stageLoadNotice;
		void (*fnSetStageLoadReplacement)(void*, const char*){nullptr};
		void (*fnSetStageSettings)(void*, const StageSettings&){nullptr};
		void (*fnSetStagePreset)(void*, int){nullptr};
		void (*fnToggleStageCandidate)(void*, int, bool){nullptr};
		void (*fnScanStageCandidates)(void*){nullptr};
		void (*fnMountCustomStage)(void*, const char*){nullptr};
		void (*fnUnloadCustomStage)(void*){nullptr};
		void (*fnRestoreCameraSkybox)(void*){nullptr};

		// ---- 多角色场景 ----
		// Unity 主线程发布实际 active actor 名单；GUI 仅持有值和带 epoch 的 token。
		// 普通动作页始终选择单个角色，空名单不表示驱动全部。
		unsigned knownActorCount{0};
		std::vector<GuiSceneActor> sceneActors;
		std::vector<GuiArmCalibration> sceneArmCalibrations;
		void (*fnSetSceneArm)(void*, uint64_t, uint64_t, int, float){nullptr};
		uint64_t actorRosterEpoch{0};
		void (*fnSelectTargetActor)(void*, uint64_t token, uint64_t epoch){nullptr};
		void (*fnSetActorModelHidden)(void*, uint64_t token, uint64_t epoch, bool hidden){nullptr};
		bool actorPropsEditable{false};
		std::vector<GuiActorProp> actorProps;
		void (*fnSetActorPropHidden)(void*, uint64_t, uint64_t, const char*, bool){nullptr};
		void (*fnClearTargetActor)(void* self){nullptr}; // Legacy developer callback: reselect bound/first actor
		void (*fnNextTargetActor)(void* self){nullptr}; // Legacy developer callback: cycle current actors
		void (*fnPickFocusedActor)(void* self){nullptr}; // Legacy developer callback: select bound actor
		std::vector<GuiEnsembleFollower> ensembleFollowers;
        uint64_t ensembleAutoGeneration{0};
        std::vector<uint64_t> ensembleAutoActors;
		std::string ensembleNotice;
		void (*fnAlignEnsemble)(void*){nullptr};
        void (*fnAutoAssignEnsemble)(void*){nullptr};
		std::vector<std::string> ensemblePresetNames;
		std::string ensemblePresetNotice;
		void (*fnSaveEnsemblePreset)(void*, const char*){nullptr};
		void (*fnApplyEnsemblePreset)(void*, const char*){nullptr};
		void (*fnSetEnsembleFollower)(void*, int, uint64_t, uint64_t, const char*, float, uint64_t){nullptr};
		void (*fnClearEnsembleFollower)(void*, int, uint64_t){nullptr};
		void (*fnSetEnsemblePosition)(void*, int, uint64_t, uint64_t, uint64_t, const Vector3&){nullptr};
		void (*fnSetEnsembleYaw)(void*, int, uint64_t, uint64_t, uint64_t, float){nullptr};
		void (*fnSetFollowerArm)(void*, int, uint64_t, uint64_t, uint64_t, int, float){nullptr};

		// ---- 写动作（回调到 Core） ----
		void* self{nullptr};
		bool (*fnSelectMotion)(void* self, int index, bool autoplay){nullptr};
		bool (*fnCycleMotion)(void* self, int delta){nullptr};
		void (*fnReloadOverrides)(void* self){nullptr};
		bool (*fnToggleOverrides)(void* self){nullptr};
		int (*fnCycleGaze)(void* self){nullptr};
		bool (*fnRefreshMotions)(void* self){nullptr};
		void (*fnPlay)(void* self){nullptr};
		void (*fnPause)(void* self){nullptr};
		void (*fnResume)(void* self){nullptr};
		void (*fnStop)(void* self){nullptr};
		void (*fnSeek)(void* self, float seconds){nullptr};
		void (*fnSeekFrame)(void* self, int64_t frame){nullptr};
		void (*fnStepFrame)(void* self, int delta){nullptr};
		void (*fnReloadMotion)(void* self){nullptr};
		void (*fnSetPhysicsBackend)(void* self, int backend){nullptr};
		void (*fnSetAuxDriveMode)(void* self, int mode){nullptr};
		void (*fnCaptureWristDiagnostic)(void* self){nullptr};
		void (*fnSetWristTwistShare)(void* self, float value){nullptr};
		void (*fnSetForearmAxisAdjustment)(void* self, float degrees){nullptr};
		bool forearmAxisLocked{false};
		std::string forearmAxisNotice;
		void (*fnSetForearmAxisLock)(void* self, bool locked){nullptr};
		// 进度拖动三段式：拖动中冻结主时钟（否则滑条每帧被播放时钟覆盖回原位而回弹）
		void (*fnScrubBegin)(void* self, float seconds){nullptr};
		void (*fnScrubUpdate)(void* self, float seconds){nullptr};
		void (*fnScrubEnd)(void* self, float seconds){nullptr};

		// ---- 探针通道（PROBE S1）----
		void (*fnProbe)(void* self, const char* line){nullptr};

		// ---- 骨骼调整动作（MMDD 规范实时调节与保存） ----
		void (*fnSetBoneOverride)(void* self, int hid, const char* name, float pitch, float yaw, float roll, bool world,
		                          bool enabled, bool symmetrical){nullptr};
		void (*fnResetBoneOverride)(void* self, int hid, const char* name, bool symmetrical){nullptr};
		void (*fnResetAllOverrides)(void* self){nullptr};
		bool (*fnSaveOverridesToMotion)(void* self){nullptr};
		bool (*fnSaveOverridesToGlobal)(void* self){nullptr};
		bool (*fnGetBoneOverride)(void* self, int hid, const char* name, float* outPitch, float* outYaw, float* outRoll,
		                          bool* outWorld, bool* outEnabled){nullptr};
	};

	class GuiOverlay;
	void DrawShell(GuiOverlay& overlay, const GuiContext& ctx);
	void DrawLibraryPage(GuiOverlay& overlay, const GuiContext& ctx);
	void DrawStagePage(GuiOverlay& overlay, const GuiContext& ctx);
	void DrawStageLoadReplacementControl(const GuiContext& ctx);
	void DrawPhysicsDiagnostics(const GuiContext& ctx);

	class GuiOverlay
	{
	public:
		bool showWindow{true};
		TransportNavigationEditState transportNavigation;
		float transportMeasuredHeight{0}, transportMeasuredWidth{0}, transportMeasuredFontSize{0};
		std::string selectedLibraryKey, libraryUiNotice;
		std::string pendingDeleteKey, pendingDeleteDirectory;
		uint64_t pendingDeleteRevision{0};
		uint64_t librarySyncedRevision{UINT64_MAX};
		std::string motionsRoot{"gakumas-local/motions"};

		// ---- Developer Preview 新信息架构状态 ----
		GuiPage activePage{GuiPage::Library};
		bool developerMode{false};
		std::vector<LibraryEntry> libraryEntries;
        std::string libraryFolderFilter;
		int selectedLibraryIndex{0};
		float initialRotationEdit[3]{0.0f, 0.0f, 0.0f};
		float cameraBaseRotation[3]{0.0f, 0.0f, 0.0f};
		bool environmentShadowEnabled{true};
		bool customNodeControl{false};
		bool isImporting{false};
		float importProgress{0.0f};
		double importStartTime{0.0};
		std::string importStatusText;
		LibraryEntry pendingImportEntry;
		int selectedActorChoice{0};
#ifdef GKMOTION_GUI_ACTIVE
		ImGuiTextFilter libraryFilter;
#endif

		// 兼容旧字段：面板首次绘制前的曲目名缓存
		std::vector<std::string> availableMotions;
		int selectedMotionIndex{-1};

		// MMDD 骨骼调校状态
		int selectedBoneHid{13};
		std::string selectedBoneName{"LeftUpperArm"};
		bool symmetricalMode{true};
		std::string saveStatusNotice;

		// ---- 进度条拖动状态机 ----
		struct ScrubState
		{
			bool active{false};
			float value{0.0f};
		} scrub;

		float s_scrubValue{0.0f};
		float liveSeekValue{0.0f};
		bool livePageOpen{false}, livePageAwaitingTakeover{false}, liveDiagnosticsOpen{false};
		bool liveSeekEditing{false};
		std::string scrubMotion;
		char importVmd[2048]{};
		char importAudio[2048]{};
		char importTitle[256]{};
		int importMotionFpsIndex{0}; // 0 = 自动检测, 1 = 30 fps, 2 = 60 fps
		int importSourceFpsIndex{0};
		char playlistFile[2048]{};
		float seekInput{0};
		bool importOpen{false};
		int browsePendingKind{-1};
		std::string browseNotice;
		char importCameraSource[4096]{};
		int importCameraFpsIndex{1}; // 60 fps
		float importCameraScale{0.083f};
		bool importCameraDetectCuts{true};
		CameraSettings cameraDraft, cameraExpected, cameraLastSnapshot;
		bool cameraDraftReady{false}, cameraSettingsPending{false};
		SoleMeasurementSettings soleDraft;
		bool soleDraftReady{false};
		uint64_t soleDraftGeneration{0};
		float solePlotSpanM{.65f};
		float initialPositionEdit[3]{};
		bool initialPositionEditing{false};
		float initialYawEdit{0};
		bool initialYawEditing{false};
		uint64_t initialPlacementActorToken{0};
		uint64_t armCalibrationToken{0};
		uint64_t armCalibrationEpoch{0};
		char ensemblePresetName[128] = "默认队形";
		uint64_t ensembleDraftEpoch{0}, ensembleSeenAutoGeneration{0};
		size_t ensembleVisibleSlots{0};
		std::vector<bool> ensembleHiddenSlots, ensembleRemovePending;
		std::vector<unsigned> ensembleRemoveFrames;
		std::vector<uint64_t> ensembleDraftActor;
		std::vector<std::string> ensembleDraftMotion;
		std::vector<float> ensembleDraftOffset;
		std::vector<uint64_t> ensemblePositionGeneration;
		std::vector<Vector3> ensemblePositionDraft;
		std::vector<float> ensembleYawDraft;
#ifdef GKMOTION_GUI_ACTIVE
		ImGuiTextFilter motionFilter;
#ifdef GKMOTION_GUI_TEST
		ImVec2 scrubRectMin, scrubRectMax;
		ImVec2 beforeQuartzRectMin, beforeQuartzRectMax;
		ImVec2 fallbackPhysicsRectMin, fallbackPhysicsRectMax;
		ImVec2 nativePhysicsRectMin, nativePhysicsRectMax;
		void DrawSoleDiagnosticsForTest(const GuiContext& ctx) { DrawSoleTab(ctx); }
		void DrawExperimentalSolePitchForTest(const GuiContext& ctx) { DrawExperimentalSolePitchTab(ctx); }
		void DrawConfigForTest(const GuiContext& ctx) { DrawConfigTab(ctx); }
		void BrowseForTest(const GuiContext& ctx, int kind)
		{
			char* target = kind == 0 ? importVmd : kind == 1 ? importAudio : importCameraSource;
			Browse(ctx, kind, target, kind == 0 ? sizeof(importVmd) : kind == 1 ? sizeof(importAudio) : sizeof(importCameraSource));
		}
#endif
#endif

		// ---- PROBE S1：拖动链路探针 ----
		int probeScrubBegin{0};
		int probeScrubUpdate{0};
		int probeScrubEnd{0};
		float probeLastScrubValue{0.0f};
		float probeLastLoggedScrub{-999.0f};
		bool probeDrawLogged{false};

		void ProbeScrub(const GuiContext& ctx, const char* tag, float value, int dragging = 0)
		{
			if (!ctx.fnProbe) return;
			probeLastScrubValue = value;
			char line[320];
			std::snprintf(line, sizeof(line),
			              "[GakuMotion][ProbeS1][GUI] %s v=%.3f | playerTime=%.3f dur=%.3f state=%d "
			              "scrubActive=%d dragging=%d | cb(begin=%d upd=%d end=%d seek=%d)",
			              tag, value, ctx.player ? ctx.player->currentTime : -1.0f,
			              ctx.player ? ctx.player->clip.duration_seconds : -1.0f,
			              ctx.player ? static_cast<int>(ctx.player->state) : -1,
			              scrub.active ? 1 : 0, dragging,
			              ctx.fnScrubBegin ? 1 : 0, ctx.fnScrubUpdate ? 1 : 0,
			              ctx.fnScrubEnd ? 1 : 0, ctx.fnSeek ? 1 : 0);
			ctx.fnProbe(ctx.self, line);
		}

		void ProbeDrawReady(const GuiContext& ctx)
		{
			if (probeDrawLogged) return;
			probeDrawLogged = true;
			if (!ctx.fnProbe) return;
			char line[320];
			std::snprintf(line, sizeof(line),
			              "[GakuMotion][ProbeS1][GUI] DRAW-READY tab=motion totalFrames=%u dur=%.3f time=%.3f "
			              "state=%d | cb(begin=%d upd=%d end=%d seek=%d play=%d)",
			              ctx.player ? ctx.player->clip.total_frames : 0u,
			              ctx.player ? ctx.player->clip.duration_seconds : -1.0f,
			              ctx.player ? ctx.player->currentTime : -1.0f,
			              ctx.player ? static_cast<int>(ctx.player->state) : -1,
			              ctx.fnScrubBegin ? 1 : 0, ctx.fnScrubUpdate ? 1 : 0,
			              ctx.fnScrubEnd ? 1 : 0, ctx.fnSeek ? 1 : 0,
			              ctx.fnPlay ? 1 : 0);
			ctx.fnProbe(ctx.self, line);
		}

		void RefreshMotions()
		{
			std::vector<MotionEntry> found;
			std::string err;
			ScanMotionLibrary(motionsRoot, found, err);
			availableMotions.clear();
			for (const auto& m : found) availableMotions.push_back(m.name);
			if (selectedMotionIndex >= static_cast<int>(availableMotions.size()))
			{
				selectedMotionIndex = availableMotions.empty() ? -1 : 0;
			}
		}

#ifdef GKMOTION_GUI_ACTIVE
		void DrawLiveAspectPreview(const GuiContext& ctx)
		{
			bool enabled = ctx.liveAspectPreviewRequested;
			if (ctx.fnSetLiveAspectPreview && ImGui::Checkbox("接管时使用 16:9（测试）", &enabled))
				ctx.fnSetLiveAspectPreview(ctx.self, enabled);
		}

		void Draw(const GuiContext& ctx, bool hostedPanel = false)
		{
			PollBrowse(ctx);
            if (!ctx.liveObserved) { livePageOpen=false; livePageAwaitingTakeover=false; liveDiagnosticsOpen=false; }
            if (livePageAwaitingTakeover && ctx.liveTakeoverActive && ctx.injectionAllowed) {
                livePageAwaitingTakeover=false; livePageOpen=false; liveDiagnosticsOpen=false; activePage=GuiPage::Library;
            }
			if (!ctx.injectionAllowed || livePageOpen)
			{
				transportNavigation = {};
				// Release the transport before switching panels. Across a scene or
				// capture boundary the runtime owns cancellation.
				if (ctx.injectionAllowed) CommitScrub(ctx);
				else scrub.active = false;
				const bool visible = hostedPanel || ImGui::Begin("GakuMotion 控制面板", &showWindow);
				if (visible)
				{
                    if (!livePageOpen) {
                        DrawStatusBar(ctx);
                        ImGui::TextWrapped("%s", ctx.injectionNotice.empty() ? "当前场景未启用动作控制。" : ctx.injectionNotice.c_str());
                        if (ctx.liveObserved && ImGui::Button("进入 Live 接管")) livePageOpen=true;
                    }
                    if (livePageOpen) {
                        if (liveDiagnosticsOpen) {
                            if (ImGui::Button("返回 Live 接管")) liveDiagnosticsOpen=false;
                            ImGui::SameLine(); ImGui::TextUnformatted("诊断与测试");
                            ImGui::Separator();
                            if (ctx.fnLoadSwingReport && ImGui::Button("读取上次 Swing 报告")) ctx.fnLoadSwingReport(ctx.self);
                            DrawSwingCheckpointPanel(ctx.physicsMonitor,ctx.physicsLiveObservationCurrent && ctx.physicsMonitor.checkpointReportOwner!=0 && ctx.physicsMonitor.checkpointReportOwner==ctx.physicsMonitor.liveObserveOwner);
                            ImGui::BeginDisabled(!ctx.liveViewingConfirmed || !ctx.fnRecordLiveState);
                            if (ImGui::Button("记录当前 Live 状态（只读）") && ctx.fnRecordLiveState) ctx.fnRecordLiveState(ctx.self);
                            ImGui::EndDisabled();
                            bool continuousArmDiagnostics = ctx.liveContinuousArmDiagnostics;
                            ImGui::BeginDisabled(!ctx.fnSetContinuousArmDiagnostics);
                            if (ImGui::Checkbox("连续记录手臂与服装骨（影响性能）", &continuousArmDiagnostics) && ctx.fnSetContinuousArmDiagnostics)
                                ctx.fnSetContinuousArmDiagnostics(ctx.self, continuousArmDiagnostics);
                            ImGui::EndDisabled();
                            ImGui::BeginDisabled(!ctx.fnActorClockTrial||ctx.liveTakeoverActive||(!ctx.liveActorClockActive&&(!ctx.liveActorClockAvailable||!ctx.livePaused)));
                            if(ImGui::Button(ctx.liveActorClockActive?"停止角色图实验并恢复":"角色图运行 3 秒（MVP）")&&ctx.fnActorClockTrial)ctx.fnActorClockTrial(ctx.self,!ctx.liveActorClockActive);
                            ImGui::EndDisabled();
                            if(!ctx.liveActorClockNotice.empty()) ImGui::TextWrapped("%s",ctx.liveActorClockNotice.c_str());
                            if(ImGui::CollapsingHeader("详细物理运行监控")) DrawPhysicsDiagnostics(ctx);
                        } else {
                            if (ImGui::Button("返回上一步")) { livePageOpen=false; livePageAwaitingTakeover=false; liveDiagnosticsOpen=false; }
                            ImGui::SameLine(); ImGui::TextUnformatted("Live 接管");
                            ImGui::SameLine(); ImGui::TextDisabled(ctx.liveViewingConfirmed?"[已识别 Live]":"[等待识别]");
                            ImGui::Spacing();
                            const float h=ImGui::GetFrameHeight();
                            const auto& style=ImGui::GetStyle();
                            const float cardHeight=5*h+5*style.ItemSpacing.y+2*style.WindowPadding.y+ImGui::GetTextLineHeight();
                            if(ImGui::BeginChild("##live_progress_card",ImVec2(0,cardHeight),true,ImGuiWindowFlags_NoScrollbar)) {
                                ImGui::TextUnformatted("Live 进度");
                                char cur[24],total[24]; FormatTimeMmSs(liveSeekEditing?liveSeekValue:ctx.liveTime,cur,sizeof(cur)); FormatTimeMmSs(ctx.liveDuration,total,sizeof(total));
                                ImGui::Text("%s    %s / %s",ctx.livePaused?"已暂停":"播放中",cur,total);
                                const bool canSeek=ctx.liveViewingConfirmed&&!ctx.liveTakeoverActive&&ctx.fnSeekLive&&ctx.liveDuration>0.1f;
                                if(!canSeek)liveSeekEditing=false;
                                if(!liveSeekEditing)liveSeekValue=ctx.liveTime;
                                ImGui::BeginDisabled(!canSeek);
                                const bool pressed=ImGui::Button("##live_play_pause",ImVec2(h,h));
                                const ImVec2 mn=ImGui::GetItemRectMin(),mx=ImGui::GetItemRectMax(); const float x=(mn.x+mx.x)*0.5f,y=(mn.y+mx.y)*0.5f;
                                const ImU32 color=ImGui::GetColorU32(ImGuiCol_Text);
                                if(ctx.livePaused)ImGui::GetWindowDrawList()->AddTriangleFilled(ImVec2(x-4,y-6),ImVec2(x-4,y+6),ImVec2(x+6,y),color);
                                else { ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x-5,y-6),ImVec2(x-2,y+6),color); ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x+2,y-6),ImVec2(x+5,y+6),color); }
                                if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",ctx.livePaused?"播放 Live":"暂停 Live");
                                if(pressed&&ctx.fnSetLivePaused)ctx.fnSetLivePaused(ctx.self,!ctx.livePaused);
                                ImGui::SameLine(); ImGui::SetNextItemWidth(-1.0f);
                                if(ImGui::SliderFloat("##live_seek",&liveSeekValue,0.0f,(std::max)(0.0f,ctx.liveDuration-0.05f),""))liveSeekEditing=true;
                                if(liveSeekEditing&&ImGui::IsItemDeactivatedAfterEdit()){ctx.fnSeekLive(ctx.self,liveSeekValue);liveSeekEditing=false;}
                                bool loop=ctx.liveLoopEnabled;
                                if(ImGui::Checkbox("结束前自动循环 Live",&loop)&&ctx.fnSetLiveLoop)ctx.fnSetLiveLoop(ctx.self,loop);
                                ImGui::EndDisabled();
                                float lead=ctx.liveLoopLead; ImGui::SetNextItemWidth((std::min)(220.0f,ImGui::GetContentRegionAvail().x*0.48f));
                                if(ImGui::SliderFloat("循环提前量（秒）",&lead,0.5f,3.0f,"%.2f")&&ctx.fnSetLiveLoopLead)ctx.fnSetLiveLoopLead(ctx.self,lead);
                                ImGui::TextDisabled("暂停或接管期间不循环；提前量过小可能错过跳转。");
                            }
                            ImGui::EndChild();
                            if (ImGui::CollapsingHeader("Live 帧数监测")) {
                                const auto& frame = ctx.liveFrameMonitor;
                                if (!frame.available) ImGui::TextWrapped("等待 Live 场景确认和帧采样。");
                                else {
                                    ImGui::Text("Unity 更新频率：%.1f FPS", frame.fps);
                                    ImGui::Text("最近帧间隔：%.1f ms", frame.frameMs);
                                    ImGui::Text("近期基准：%.1f ms", frame.baselineMs);
                                    ImGui::Text("本次 Live 卡顿次数：%llu", static_cast<unsigned long long>(frame.stalls));
                                    ImGui::Text("最近卡顿：%.1f ms", frame.lastStallMs);
                                    ImGui::Text("绑定的副角色：%u", ctx.liveFrameFollowers);
                                }
                                ImGui::TextWrapped("帧间隔达到 50 ms 且不低于近期基准两倍时记录线索；这些指标不能单独证明卡顿原因。此处为 Unity 更新频率，并非显示器输出帧率。");
                            }
                            if(!ctx.liveAudioNotice.empty())ImGui::TextWrapped("%s",ctx.liveAudioNotice.c_str());
                            ImGui::Spacing();
                            const char* physics=!ctx.physicsMonitorFresh?"待播放":ctx.physicsMonitor.actual==2?"原生运行中":ctx.physicsMonitor.actual==1?"内建物理":
                                ctx.physicsMonitor.requested==2?"等待原生任务":"未运行";
                            ImGui::TextWrapped("接管：%s    Live：%s    物理：%s",ctx.liveTakeoverAvailable?"可用":"等待确认",ctx.liveViewingConfirmed?"已识别":"等待确认",physics);
                            DrawLiveAspectPreview(ctx);
                            ImGui::Spacing();
                            ImGui::BeginDisabled(!ctx.liveTakeoverAvailable||!ctx.fnSetLiveTakeover);
                            if(ImGui::Button(ctx.liveTakeoverActive?"退出 Live 动作接管":"启动 Live 接管",ImVec2((std::min)(260.0f,ImGui::GetContentRegionAvail().x),h*1.35f))&&ctx.fnSetLiveTakeover){
                                livePageAwaitingTakeover=!ctx.liveTakeoverActive;ctx.fnSetLiveTakeover(ctx.self,!ctx.liveTakeoverActive);
                            }
                            ImGui::EndDisabled();
                            if (!ctx.liveTakeoverActive)
                            {
                                ImGui::SameLine();
                                ImGui::BeginDisabled();
                                ImGui::Button("恢复 Live");
                                ImGui::EndDisabled();
                            }
                            if(ctx.liveTakeoverActive&&ImGui::Button("进入 MMD 页面")){livePageOpen=false;activePage=GuiPage::Library;}
                            ImGui::TextDisabled(ctx.liveTakeoverActive?"退出后恢复原先 Live 状态。":"启动后暂停舞台并进入 MMD 曲库。");
                            ImGui::TextWrapped("如果 MMD 播放出现物理运算异常，请退出 Live 接管并重新接入，重置物理状态。");
                            if(!ctx.liveControlNotice.empty())ImGui::TextWrapped("%s",ctx.liveControlNotice.c_str());
                            if(!ctx.liveTakeoverAvailable)ImGui::TextWrapped("等待角色、拍照保护和 Live 状态就绪。");
                            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                            if(ImGui::Button("诊断与测试"))liveDiagnosticsOpen=true;
                            ImGui::SameLine(); ImGui::TextDisabled("Swing 监控、角色图实验、历史报告");
                        }
                    } // livePageOpen
                    if (!livePageOpen) {
					if (!ctx.stageSettings.loadReplacementKey.empty())
						ImGui::TextWrapped("已保留实验舞台选择；可在下方开发者选项中修改或恢复原生背景。");
					if (ImGui::CollapsingHeader("开发者选项"))
					{
						ImGui::Checkbox("开发者模式", &developerMode);
						if (developerMode) DrawStageLoadReplacementControl(ctx);
						else ImGui::TextWrapped("开启开发者模式后可配置尚未完成的摄影舞台替换。");
					}
                    }
				}
				if (!hostedPanel) ImGui::End();
				return;
			}
			if (!hostedPanel && !showWindow)
			{
				CommitScrub(ctx);
				if (ImGui::Begin("GakuMotion", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
				{
					if (ImGui::Button("打开 GakuMotion 控制面板")) showWindow = true;
				}
				ImGui::End();
				return;
			}
			if (!hostedPanel)
			{
				const auto display = ImGui::GetIO().DisplaySize;
				ImGui::SetNextWindowPos(ImVec2(8, 8), ImGuiCond_FirstUseEver);
				ImGui::SetNextWindowSize(ImVec2((std::min)(720.0f, (std::max)(240.0f, display.x - 16.0f)),
					(std::min)(720.0f, (std::max)(240.0f, display.y - 16.0f))), ImGuiCond_FirstUseEver);
			}
			const bool visible = hostedPanel || ImGui::Begin("GakuMotion 控制面板", &showWindow);
			if (visible)
			{
				if (browsePendingKind >= 0) ImGui::TextDisabled("等待文件选择…");
				if (!ctx.injectionNotice.empty()) ImGui::TextWrapped("%s", ctx.injectionNotice.c_str());
				if (!browseNotice.empty()) ImGui::TextWrapped("%s", browseNotice.c_str());
				DrawShell(*this, ctx);
			}
			if (!hostedPanel) ImGui::End();
			if (scrub.active && ((!hostedPanel && !showWindow) || (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !
				ImGui::IsAnyItemActive())))
				CommitScrub(ctx);
		}

		void CommitScrub(const GuiContext& ctx)
		{
			if (!scrub.active) return;
			scrub.active = false;
			++probeScrubEnd;
			if (ctx.fnScrubEnd) ctx.fnScrubEnd(ctx.self, scrub.value);
			else if (ctx.fnSeek) ctx.fnSeek(ctx.self, scrub.value);
		}

		void PlaylistAction(const GuiContext& ctx, int action, const char* key = "", int value = 0)
		{
			if (ctx.fnPlaylistAction) ctx.fnPlaylistAction(ctx.self, action, key, value);
		}
		void PlaylistAction(const GuiContext& ctx, GuiPlaylistAction action, const char* key = "", int value = 0)
		{
			PlaylistAction(ctx, static_cast<int>(action), key, value);
		}

		void RequestMotionDelete(const GuiContext& ctx, const char* key)
		{
			if (!ctx.fnPlaylistAction || !ctx.motions || !key || ctx.importBusy || ctx.libraryRefreshing) return;
			const int index = FindPlaylistIndex(*ctx.motions, key);
			if (index < 0) return;
			pendingDeleteKey = key;
			pendingDeleteDirectory = (*ctx.motions)[index].dir;
			pendingDeleteRevision = ctx.libraryRevision;
			ImGui::OpenPopup("删除选中确认");
		}

		void DrawMotionDeleteConfirmation(const GuiContext& ctx)
		{
			bool open = true;
			ImGui::SetNextWindowSize(ImVec2(540, 0), ImGuiCond_Appearing);
			if (ImGui::BeginPopupModal("删除选中确认", &open, ImGuiWindowFlags_AlwaysAutoResize))
			{
				ImGui::TextWrapped("删除曲目：%s", pendingDeleteKey.c_str());
				ImGui::TextWrapped("将永久删除整个曲目目录及其中全部文件：%s", pendingDeleteDirectory.c_str());
				ImGui::TextWrapped("此操作无法撤销，文件不会进入回收站。确定删除选中曲目？");
				const int index = ctx.motions ? FindPlaylistIndex(*ctx.motions, pendingDeleteKey) : -1;
				const bool changed = pendingDeleteRevision != ctx.libraryRevision || index < 0 ||
					(*ctx.motions)[index].dir != pendingDeleteDirectory;
				if (changed) ImGui::TextWrapped("曲库已变化，请取消后重新选择曲目。");
				ImGui::BeginDisabled(changed || ctx.importBusy || ctx.libraryRefreshing);
				if (ImGui::Button("永久删除"))
				{
					PlaylistAction(ctx, GuiPlaylistAction::DeleteFiles, pendingDeleteKey.c_str(), 1);
					pendingDeleteKey.clear();
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndDisabled();
				ImGui::SameLine();
				if (ImGui::Button("取消删除") || ImGui::IsKeyPressed(ImGuiKey_Escape))
				{
					pendingDeleteKey.clear();
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
			if (!open) pendingDeleteKey.clear();
		}

		static const char* Core_PlaybackStateName(PlaybackState state)
		{
			switch (state)
			{
			case PlaybackState::Stopped: return "Stopped";
			case PlaybackState::Playing: return "Playing";
			case PlaybackState::Paused: return "Paused";
			case PlaybackState::Preview: return "Preview(帧0)";
			}
			return "?";
		}

		// ---- 各 Tab 渲染接口（实现位于 src/gui/ 对应子模块中） ----
		void DrawStatusBar(const GuiContext& ctx);
		void DrawMotionTab(const GuiContext& ctx);
		void DrawGazeTab(const GuiContext& ctx);
		void DrawCameraTab(const GuiContext& ctx);
		void DrawStageTab(const GuiContext& ctx);
		void DrawBoneTab(const GuiContext& ctx);
		void DrawBoneOverridesSection(const GuiContext& ctx);
		void DrawSoleTab(const GuiContext& ctx);
		void DrawExperimentalSolePitchTab(const GuiContext& ctx);
		void DrawConfigTab(const GuiContext& ctx);

		void DrawImport(const GuiContext& ctx);
		void DrawSolePlot(const GuiContext& ctx, bool sideView, float width);

	private:
		void Browse(const GuiContext& ctx, int kind, char* buffer, size_t size)
		{
			if (ctx.fnBeginPickFile)
			{
				if (browsePendingKind >= 0) return;
				browseNotice.clear();
				if (ctx.fnBeginPickFile(kind, browseNotice)) browsePendingKind = kind;
				return;
			}
			if (!ctx.fnPickFile) return;
			std::string path;
			if (ctx.fnPickFile(kind, path)) std::snprintf(buffer, size, "%s", path.c_str());
		}

		void PollBrowse(const GuiContext& ctx)
		{
			if (browsePendingKind < 0 || !ctx.fnPollPickFile) return;
			std::string path;
			const int state = ctx.fnPollPickFile(path, browseNotice);
			if (state == 0) return;
			if (state == 1)
			{
				char* target = browsePendingKind == 0 ? importVmd : browsePendingKind == 1 ? importAudio : importCameraSource;
				const size_t capacity = browsePendingKind == 0 ? sizeof(importVmd) : browsePendingKind == 1 ? sizeof(importAudio) : sizeof(importCameraSource);
				if (path.size() >= capacity) browseNotice = "所选路径过长，未修改输入路径";
				else std::memcpy(target, path.c_str(), path.size() + 1);
			}
			browsePendingKind = -1;
		}
#endif // GKMOTION_GUI_ACTIVE
	};
} // namespace GakuMotion

// 机械拆分的 Tab 渲染实现包含
#ifdef GKMOTION_GUI_ACTIVE
#include "gui/GuiStageLoadReplacement.hpp"
#include "gui/GkGuiStatusBar.hpp"
#include "gui/GkGuiTabMotion.hpp"
#include "gui/GkGuiTabGaze.hpp"
#include "gui/GkGuiTabCamera.hpp"
#include "gui/GkGuiTabStage.hpp"
#include "gui/GkGuiTabBone.hpp"
#include "gui/GkGuiTabSole.hpp"
#include "gui/GkGuiTabExperimentalSolePitch.hpp"
#include "gui/GkGuiTabConfig.hpp"

#include "gui/GuiTransportBar.hpp"
#include "gui/GuiLibraryPage.hpp"
#include "gui/GuiMotionPage.hpp"
#include "gui/GuiCameraPage.hpp"
#include "gui/GuiStagePage.hpp"
#include "gui/GuiSettingsPage.hpp"
#include "gui/GuiDeveloperPage.hpp"
#include "gui/GuiOverlayShell.hpp"
#endif
