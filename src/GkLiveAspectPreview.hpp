#pragma once

// Temporary A/B test. Change the native Live render target, then fit the Live
// UI to the same ratio. Leave the game's camera lens and gate fit in control.
namespace LiveAspectPreview
{
    constexpr float normalRatio = 2.35f;
    constexpr float previewRatio = 16.0f / 9.0f;

    struct Lease
    {
        uintptr_t owner{};
        int scene{};
        StageLoader::Handle helper{};
        StageLoader::Handle fitter{};
        const void* setCamera{};
        const void* setRatio{};
        const void* getRatio{};
        float originalRatio{};
    };

    inline Lease lease;

    inline bool Invoke(const void* method, void* object, void* argument)
    {
        if (!method || !object || !g_faceRuntime.invoke) return false;
        void* exception = nullptr;
        void* args[]{argument};
        g_faceRuntime.invoke(method, object, args, &exception);
        return exception == nullptr;
    }

    inline bool Matches(float a, float b) { return std::abs(a - b) < 0.005f; }

    inline void DropLease()
    {
        if (lease.helper) StageLoader::freeHandle(lease.helper);
        if (lease.fitter) StageLoader::freeHandle(lease.fitter);
        lease = {};
    }

    inline bool Restore(bool sameOwner)
    {
        if (!lease.owner) return true;
        const auto target = LiveActorProps::Target();
        if (!target) return false;
        void* helper = target(lease.helper);
        void* fitter = target(lease.fitter);
        bool useNativeWide = false;
        const bool cameraOk = !sameOwner || Invoke(lease.setCamera, helper, &useNativeWide);
        float ratio = 0.0f;
        const bool uiOk = !fitter || !CameraBridge::IsAlive(fitter) ||
            (Invoke(lease.setRatio, fitter, &lease.originalRatio) &&
             LiveLetterboxProbe::Read(lease.getRatio, fitter, ratio) && Matches(ratio, lease.originalRatio));
        const bool ok = cameraOk && uiOk;
        if (g_hostContext.logInfo)
            g_hostContext.logInfo("[GakuMotion][LiveAspectPreview] restore scene=%d same_owner=%d camera=%d ui=%d",
                lease.scene, int(sameOwner), int(cameraOk), int(uiOk));
        if (ok || !sameOwner) DropLease();
        return ok;
    }

    inline bool Apply(uintptr_t owner, int scene, const LiveLetterboxProbe::Fitters& ui)
    {
        if (ui.count != 1 || !Matches(ui.ratio, normalRatio) || !g_gameAssembly ||
            !StageLoader::newHandle || !StageLoader::freeHandle || !LiveActorProps::Target() ||
            !g_faceRuntime.objectClass || !g_faceRuntime.readField) return false;

        const auto md = GroundShadowProbeBootstrap::Resolve(g_gameAssembly).method;
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

        lease = {owner, scene, StageLoader::newHandle(helper, false), StageLoader::newHandle(ui.fitter, false),
            setCamera, setRatio, ui.getRatio, ui.ratio};
        if (!lease.helper || !lease.fitter) {
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
        const bool requested = owner && owner == core.liveOwnerToken && scene == core.liveOwnerScene &&
            core.liveTakeoverActive && !core.liveTakeoverFault && core.liveAspectPreviewRequested;
        const bool sameOwner = lease.owner && lease.owner == owner && lease.scene == scene;
        const bool protectedCamera = core.injectionPolicy.Read().captureDepth != 0 ||
            !GakuMotion::capture::LiveAutoPhotoIdle();

        if (lease.owner && (!requested || !sameOwner)) {
            if (!sameOwner || (!protectedCamera && now >= restoreAt)) {
                restoreAt = now + std::chrono::milliseconds(300);
                if (Restore(sameOwner)) {
                    attemptedOwner = 0;
                    attemptedScene = 0;
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
                LiveLetterboxProbe::Report(scene);
            }
            return;
        }
        if (attemptedOwner == owner && attemptedScene == scene) return;
        attemptedOwner = owner;
        attemptedScene = scene;
        const auto ui = LiveLetterboxProbe::FindFitters(scene);
        LiveLetterboxProbe::Report(scene);
        const bool applied = Apply(owner, scene, ui);
        if (g_hostContext.logInfo)
            g_hostContext.logInfo("[GakuMotion][LiveAspectPreview] scene=%d wrote_16x9=%d fitters=%u original=%.4f live_time=%.3f",
                scene, int(applied), ui.count, ui.ratio, core.liveObservation.time);
        inspectAt = now + std::chrono::milliseconds(300);
    }
}
