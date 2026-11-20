#pragma once

// Use the native 16:9 target and fit the UI to it. Extend the original camera
// framing vertically for rendering and leave the animated lens under native control.
namespace LiveAspectPreview
{
    constexpr float normalRatio = 2.35f;
    constexpr float previewRatio = 16.0f / 9.0f;

    struct Lease
    {
        uintptr_t owner{};
        int scene{};
        StageLoader::Handle camera{};
        StageLoader::Handle helper{};
        StageLoader::Handle fitter{};
        const void* setCamera{};
        const void* setRatio{};
        const void* getRatio{};
        float originalRatio{}, originalCameraAspect{};
        bool restoring{};
    };

    inline Lease lease;
    using Projection = GakuMotion::live_aspect::Projection;
    inline const void *getProjection{}, *setProjection{}, *resetProjection{}, *getAspect{}, *getStereo{};
    inline CameraBridge::Render managerEnd{};
    inline StageLoader::Handle projectedCamera{};
    inline Projection writtenProjection{};
    inline bool projectionObserved{};
    using SetupParaffin = void (*)(void*, void*, float, void*, const void*);
    using ValidParaffinAspect = bool (*)(void*, float, const void*);
    inline SetupParaffin setupParaffin{};
    inline ValidParaffinAspect validParaffinAspect{};
    inline bool flareHooksAttempted{}, flareAspectHookReady{}, flareAspectObserved{};
    inline std::atomic_bool suppressParaffin{false};

    inline bool Invoke(const void* method, void* object, void* argument)
    {
        if (!method || !object || !g_faceRuntime.invoke) return false;
        void* exception = nullptr;
        void* args[]{argument};
        g_faceRuntime.invoke(method, object, argument ? args : nullptr, &exception);
        return exception == nullptr;
    }

    inline bool Matches(float a, float b) { return std::abs(a - b) < 0.005f; }

    inline float ParaffinAspect(float aspect)
    {
        if (!CameraBridge::OnUnityThread() || !projectedCamera || !Matches(aspect, previewRatio))
            return aspect;
        if (!flareAspectObserved && g_hostContext.logInfo) {
            flareAspectObserved = true;
            g_hostContext.logInfo("[GakuMotion][LiveAspectPreview] paraffin_aspect=%.4f target_aspect=%.4f",
                lease.originalCameraAspect, aspect);
        }
        return lease.originalCameraAspect;
    }

    inline void SetupParaffinWithOriginalAspect(void* self, void* cmd, float aspect, void* volume, const void* method)
    {
        setupParaffin(self, cmd, ParaffinAspect(aspect), volume, method);
    }

    inline bool CheckOriginalParaffinAspect(void* self, float aspect, const void* method)
    {
        if (suppressParaffin.load(std::memory_order_acquire) && CameraBridge::OnUnityThread() &&
            GakuMotion::Core::Instance().injectionPolicy.Read().captureDepth == 0) return false;
        return validParaffinAspect(self, ParaffinAspect(aspect), method);
    }

    inline void PrepareParaffin(const GakuMotion::ExactMethodMetadata& md)
    {
        if (flareHooksAttempted) return;
        flareHooksAttempted = true;
        constexpr const char* pipeline = "Unity.RenderPipelines.Universal.Runtime.dll";
        void* pass = FindClass(pipeline, "VL.Rendering.Internal", "VLPostProcessPass");
        void* volume = FindClass(pipeline, "VL.Rendering", "VLParaffin");
        void* command = FindClass("UnityEngine.CoreModule.dll", "UnityEngine.Rendering", "CommandBuffer");
        const bool setup = pass && volume && command &&
            md.Find(pass, "SetupVLParaffin", false, {0x01}, {{0x12, command}, {0x0c}, {0x12, volume}}) &&
            CameraBridge::InstallHook("VLPostProcessPass.SetupVLParaffin", FindMethodPointer(pass, "SetupVLParaffin", 3),
                reinterpret_cast<void*>(SetupParaffinWithOriginalAspect), reinterpret_cast<void**>(&setupParaffin));
        const void* aspectCheck = volume ? md.Find(volume, "IsValidAspect", false, {0x02}, {{0x0c}}) : nullptr;
        const bool check = aspectCheck &&
            CameraBridge::InstallHook("VLParaffin.IsValidAspect", GroundShadowProbeBootstrap::MethodPointer(aspectCheck),
                reinterpret_cast<void*>(CheckOriginalParaffinAspect), reinterpret_cast<void**>(&validParaffinAspect));
        flareAspectHookReady = check && validParaffinAspect;
        const auto log = setup && check ? g_hostContext.logInfo : g_hostContext.logWarn;
        if (log) log("[GakuMotion][LiveAspectPreview] paraffin hooks setup=%d aspect_check=%d", int(setup), int(check));
    }

    inline bool ReleaseProjection(void* renderedCamera = nullptr)
    {
        if (!projectedCamera) return true;
        auto target = LiveActorProps::Target();
        if (!target || !CameraBridge::OnUnityThread()) return false;
        void* camera = target(projectedCamera);
        if (renderedCamera && renderedCamera != camera) return true;
        if (CameraBridge::IsAlive(camera)) {
            Projection actual{};
            if (!LiveLetterboxProbe::Read(getProjection, camera, actual)) return false;
            // Another writer owns a different matrix. Leave its write intact.
            if (actual == writtenProjection) {
                // Restore Unity's automatic projection. Reassigning the previous
                // matrix here freezes subsequent native lens updates.
                if (!Invoke(resetProjection, camera, nullptr)) return false;
            }
        }
        StageLoader::freeHandle(projectedCamera);
        projectedCamera = {};
        return true;
    }

    inline void AfterCamera(void* context, void* camera, void* method)
    {
        if (managerEnd) managerEnd(context, camera, method);
        ReleaseProjection(camera);
    }

    inline void BeforeCamera(void* camera)
    {
        if (!lease.owner || lease.restoring || lease.owner != g_sceneOwner.presenterIdentity ||
            lease.scene != g_sceneOwner.sceneHandle || projectedCamera || !CameraBridge::getMain ||
            camera != CameraBridge::getMain(nullptr) || !CameraBridge::IsAlive(camera) ||
            GakuMotion::camera_handoff::Requested() ||
            GakuMotion::Core::Instance().injectionPolicy.Read().captureDepth != 0 ||
            !GakuMotion::capture::LiveAutoPhotoIdle()) return;
        bool stereo{};
        float aspect{};
        Projection native{};
        if (!LiveLetterboxProbe::Read(getStereo, camera, stereo) || stereo ||
            !LiveLetterboxProbe::Read(getAspect, camera, aspect) || !Matches(aspect, previewRatio) ||
            !LiveLetterboxProbe::Read(getProjection, camera, native)) return;

        auto widened = native;
        if (!GakuMotion::live_aspect::PreserveHorizontalFraming(widened, lease.originalCameraAspect, aspect)) return;
        projectedCamera = StageLoader::newHandle(camera, false);
        if (!projectedCamera) return;
        writtenProjection = widened;
        if (!Invoke(setProjection, camera, widened.data())) {
            ReleaseProjection(camera);
            return;
        }
        if (!projectionObserved) {
            Projection actual{};
            projectionObserved = LiveLetterboxProbe::Read(getProjection, camera, actual) && actual == widened;
            if (projectionObserved && g_hostContext.logInfo)
                g_hostContext.logInfo("[GakuMotion][LiveAspectPreview] horizontal framing scale_x=%.5f scale_y=%.5f",
                    actual[0], actual[5]);
        }
    }

    inline bool PrepareProjection(const GakuMotion::ExactMethodMetadata& md)
    {
        if (!CameraBridge::ready) return false;
        if (managerEnd) return true;
        void* cameraClass = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Camera");
        void* matrixClass = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Matrix4x4");
        void* manager = FindClass("UnityEngine.CoreModule.dll", "UnityEngine.Rendering", "RenderPipelineManager");
        if (!cameraClass || !LiveLetterboxProbe::HasValueSize(matrixClass, sizeof(Projection))) return false;
        getProjection = md.Find(cameraClass, "get_projectionMatrix", false, {0x11, matrixClass}, {});
        setProjection = md.Find(cameraClass, "set_projectionMatrix", false, {0x01}, {{0x11, matrixClass}});
        resetProjection = md.Find(cameraClass, "ResetProjectionMatrix", false, {0x01}, {});
        getAspect = md.Find(cameraClass, "get_aspect", false, {0x0c}, {});
        getStereo = md.Find(cameraClass, "get_stereoEnabled", false, {0x02}, {});
        return getProjection && setProjection && resetProjection && getAspect && getStereo &&
            CameraBridge::InstallHook("RenderPipelineManager.EndCameraRendering",
                FindMethodPointer(manager, "EndCameraRendering", 2), reinterpret_cast<void*>(AfterCamera),
                reinterpret_cast<void**>(&managerEnd));
    }

    inline void DropLease()
    {
        CameraBridge::projectionBegin = nullptr;
        if (lease.camera) StageLoader::freeHandle(lease.camera);
        if (lease.helper) StageLoader::freeHandle(lease.helper);
        if (lease.fitter) StageLoader::freeHandle(lease.fitter);
        lease = {};
    }

    inline bool Restore(bool sameOwner)
    {
        if (!lease.owner) return ReleaseProjection();
        lease.restoring = true;
        if (!ReleaseProjection()) return false;
        const auto target = LiveActorProps::Target();
        if (!target) return false;
        void* helper = target(lease.helper);
        void* fitter = target(lease.fitter);
        void* camera = target(lease.camera);
        // The native output camera can survive a Live presenter change.
        const bool reusedCamera = CameraBridge::IsAlive(camera) && CameraBridge::getMain &&
            camera == CameraBridge::getMain(nullptr);
        float currentAspect{};
        const bool needsCameraRestore = sameOwner || (reusedCamera &&
            LiveLetterboxProbe::Read(getAspect, camera, currentAspect) && Matches(currentAspect, previewRatio));
        bool useNativeWide = false;
        const bool cameraOk = !needsCameraRestore || Invoke(lease.setCamera, helper, &useNativeWide);
        float ratio = 0.0f;
        const bool uiOk = !fitter || !CameraBridge::IsAlive(fitter) ||
            (Invoke(lease.setRatio, fitter, &lease.originalRatio) &&
             LiveLetterboxProbe::Read(lease.getRatio, fitter, ratio) && Matches(ratio, lease.originalRatio));
        const bool ok = cameraOk && uiOk;
        if (g_hostContext.logInfo)
            g_hostContext.logInfo("[GakuMotion][LiveAspectPreview] restore scene=%d same_owner=%d camera_requested=%d camera_ok=%d ui=%d",
                lease.scene, int(sameOwner), int(needsCameraRestore), int(cameraOk), int(uiOk));
        if (ok) DropLease();
        return ok;
    }

    inline bool Apply(uintptr_t owner, int scene, const LiveLetterboxProbe::Fitters& ui)
    {
        if (ui.count != 1 || !Matches(ui.ratio, normalRatio) || !g_gameAssembly ||
            !StageLoader::newHandle || !StageLoader::freeHandle || !LiveActorProps::Target() ||
            !g_faceRuntime.objectClass || !g_faceRuntime.readField) return false;

        const auto md = GroundShadowProbeBootstrap::Resolve(g_gameAssembly).method;
        if (!PrepareProjection(md)) return false;
        PrepareParaffin(md);
        float originalCameraAspect{};
        void* camera = CameraBridge::getMain ? CameraBridge::getMain(nullptr) : nullptr;
        if (!CameraBridge::IsAlive(camera) ||
            !LiveLetterboxProbe::Read(getAspect, camera, originalCameraAspect) ||
            !Matches(originalCameraAspect, normalRatio)) return false;
        void* helperClass = FindClass("Assembly-CSharp.dll", "Campus.Live", "LiveSceneCameraHelper");
        void* presenterClass = FindClass("Assembly-CSharp.dll", "Campus.Live", "LiveScenePresenter");
        void* presenter = reinterpret_cast<void*>(owner);
        void* field = presenterClass && g_faceRuntime.objectClass(presenter) == presenterClass
            ? g_faceRuntime.InstanceField(presenter, "_cameraHelper", 0x12) : nullptr;
        void* helper = nullptr;
        if (field) g_faceRuntime.readField(presenter, field, &helper);
        if (!helper || !helperClass || g_faceRuntime.objectClass(helper) != helperClass) return false;

        const void* setCamera = md.Find(helperClass, "SetVLCameraAspectRatio", false, {0x01}, {{0x02}});
        const void* setRatio = md.Find(ui.fitterClass, "set_aspectRatio", false, {0x01}, {{0x0c}});
        if (!setCamera || !setRatio) return false;

        lease = {owner, scene, StageLoader::newHandle(camera, false),
            StageLoader::newHandle(helper, false), StageLoader::newHandle(ui.fitter, false),
            setCamera, setRatio, ui.getRatio, ui.ratio, originalCameraAspect};
        if (!lease.camera || !lease.helper || !lease.fitter) {
            DropLease();
            return false;
        }

        bool useNative169 = true;
        float target = previewRatio;
        float actual = 0.0f;
        if (!Invoke(setCamera, helper, &useNative169) || !Invoke(setRatio, ui.fitter, &target) ||
            !LiveLetterboxProbe::Read(ui.getRatio, ui.fitter, actual) || !Matches(actual, target)) {
            Restore(true);
            return false;
        }
        projectionObserved = false;
        flareAspectObserved = false;
        CameraBridge::projectionBegin = BeforeCamera;
        return true;
    }

    inline void Tick(GakuMotion::Core& core)
    {
        using Clock = std::chrono::steady_clock;
        static uintptr_t attemptedOwner{};
        static int attemptedScene{};
        static auto inspectAt = Clock::time_point{};
        static auto restoreAt = Clock::time_point{};
        const auto now = Clock::now();
        const auto owner = g_sceneOwner.Valid() && g_sceneOwner.role == GakuMotion::injection::OwnerRole::Live
            ? g_sceneOwner.presenterIdentity : 0;
        const int scene = owner ? g_sceneOwner.sceneHandle : 0;
        const bool takeover = owner && owner == core.liveOwnerToken && scene == core.liveOwnerScene &&
            core.liveTakeoverActive && !core.liveTakeoverFault && !GakuMotion::camera_handoff::Requested();
        const bool requested = takeover && core.liveAspectPreviewRequested;
        const bool sameOwner = lease.owner && lease.owner == owner && lease.scene == scene;
        const bool protectedCamera = core.injectionPolicy.Read().captureDepth != 0 ||
            !GakuMotion::capture::LiveAutoPhotoIdle();
        if (takeover && !protectedCamera && !flareHooksAttempted && g_gameAssembly)
            PrepareParaffin(GroundShadowProbeBootstrap::Resolve(g_gameAssembly).method);
        core.liveParaffinAvailable = flareAspectHookReady;
        suppressParaffin.store(takeover && !protectedCamera && core.liveParaffinAvailable && !core.liveParaffinEnabled,
            std::memory_order_release);
        if (!core.liveTakeoverActive) {
            core.liveParaffinEnabled = true;
            core.liveDepthOfFieldOverride = -1;
        }
        const bool disableDepth = takeover && !protectedCamera && core.ShouldDisableLiveDof();
        const bool depthInstalled = disableDepth && CameraBridge::InstallIndependentDof();
        core.liveDepthOfFieldUnavailable = disableDepth && !depthInstalled;
        CameraBridge::independentDofDisabled.store(depthInstalled, std::memory_order_release);
        static auto lastDofTrace = Clock::time_point{};
        if (disableDepth && g_hostContext.logInfo && now - lastDofTrace >= std::chrono::seconds(4)) {
            lastDofTrace = now;
            g_hostContext.logInfo("[GakuMotion][LiveDoFRenderTrace] hook=%d calls=%llu skipped=%llu",
                int(depthInstalled), static_cast<unsigned long long>(CameraBridge::dofRenderCalls.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(CameraBridge::dofRenderSkipped.load(std::memory_order_relaxed)));
        }

        if (lease.owner && (lease.restoring || !requested || !sameOwner)) {
            if (!protectedCamera && now >= restoreAt) {
                restoreAt = now + std::chrono::milliseconds(300);
                if (Restore(sameOwner)) {
                    if (!requested || !sameOwner) {
                        attemptedOwner = 0;
                        attemptedScene = 0;
                    }
                    inspectAt = {};
                    LiveLetterboxProbe::Report(scene);
                }
            }
            return;
        }
        restoreAt = {};
        if (!requested) {
            attemptedOwner = 0;
            attemptedScene = 0;
            return;
        }
        if (protectedCamera) return;

        if (lease.owner) {
            if (inspectAt != Clock::time_point{} && now >= inspectAt) {
                inspectAt = {};
                if (!projectionObserved) {
                    Restore(sameOwner);
                    core.liveControlNotice = "未确认 16:9 构图，已尝试恢复原画幅。";
                    if (g_hostContext.logWarn)
                        g_hostContext.logWarn("[GakuMotion][LiveAspectPreview] no verified render projection; scene=%d", scene);
                }
            }
            return;
        }
        if (attemptedOwner == owner && attemptedScene == scene) return;
        attemptedOwner = owner;
        attemptedScene = scene;
        const auto ui = LiveLetterboxProbe::FindFitters(scene);
        const bool applied = Apply(owner, scene, ui);
        if (g_hostContext.logInfo)
            g_hostContext.logInfo("[GakuMotion][LiveAspectPreview] scene=%d wrote_16x9=%d fitters=%u original=%.4f live_time=%.3f",
                scene, int(applied), ui.count, ui.ratio, core.liveObservation.time);
        if (!applied) core.liveControlNotice = "当前镜头无法启用 16:9 画幅。";
        inspectAt = now + std::chrono::seconds(1);
    }
}
