#pragma once

namespace NativeSwingRenderPreview
{
    using GakuMotion::Quaternion;
    constexpr const char* bones[]{"LeftFrontHair1_S", "RigntBackCloth1_S", "RigntBackCloth2_S"};
    struct Joint {
        void* transform{};
        Quaternion older{}, latest{}, preview{}, restore{}, shown{};
        unsigned count{};
        bool written{};
    };
    inline std::array<Joint, std::size(bones)> joints{};
    inline GakuMotion::FallbackPhysics boneLookup;
    inline GakuMotion::IdolController::GetRotationFn read{};
    inline GakuMotion::IdolController::SetRotationFn write{};
    inline void* actor{};
    inline void* activeCamera{};
    inline uint64_t epoch{};
    inline int frame{-1}, seeks{};
    inline float olderTime{}, latestTime{}, previewTime{};
    inline bool armed{}, hookAttempted{}, logged{};
    inline const char* lastStatus{};

    inline void Report(const char* reason)
    {
        if (reason == lastStatus) return;
        lastStatus = reason;
        if (g_hostContext.logInfo)
            g_hostContext.logInfo("[GakuMotion][NativeSwingRenderPreview] status=%s frame=%d",reason,frame);
    }

    inline Quaternion Read(void* transform)
    {
        float q[4]{};
        read(transform,q);
        return {q[0],q[1],q[2],q[3]};
    }

    inline void Write(void* transform, const Quaternion& q)
    {
        write(transform,q.x,q.y,q.z,q.w);
    }

    inline void Restore(void* camera)
    {
        if (!activeCamera || camera != activeCamera || !CameraBridge::OnUnityThread()) return;
        auto& core = GakuMotion::Core::Instance();
        const bool recording = g_costumeCapture.Active() && CostumeCaptureMatches(core);
        for (size_t i=0; i<joints.size(); ++i) {
            auto& joint = joints[i];
            if (!joint.written) continue;
            const bool alive = CameraBridge::IsAlive(joint.transform);
            Quaternion restored{};
            if (alive) {
                Write(joint.transform,joint.restore);
                if (recording) restored = Read(joint.transform);
            }
            if (recording)
                g_costumeCapture.RecordSwingRender(bones[i],actor,frame,latestTime,previewTime,
                    joint.restore,joint.shown,restored,alive && GakuMotion::native_swing::Angle(joint.restore.Normalized(),restored.Normalized()) < .1f);
            joint.written = false;
        }
        activeCamera = nullptr;
    }

    inline void Apply(void* camera)
    {
        auto& core = GakuMotion::Core::Instance();
        if (!armed || activeCamera || !camera || !CameraBridge::getMain || camera != CameraBridge::getMain(nullptr) ||
            !g_faceUnityFrame || g_faceUnityFrame() != frame || !core.liveTakeoverActive || core.captureSuspended ||
            core.sceneActorRegistry.Epoch() != epoch || core.player.controller.targetActor != actor ||
            core.player.state != GakuMotion::PlaybackState::Playing) return;
        for (auto& joint : joints) {
            if (joint.count < 3 || !CameraBridge::IsAlive(joint.transform)) continue;
            joint.restore = Read(joint.transform);
            joint.written = true;
            activeCamera = camera;
            Write(joint.transform,joint.preview);
            if (g_costumeCapture.Active()) joint.shown = Read(joint.transform);
        }
        if (activeCamera && !logged && g_hostContext.logInfo) {
            logged = true;
            g_hostContext.logInfo("[GakuMotion][NativeSwingRenderPreview] enabled frame=%d",frame);
        }
    }

    inline void Sample(GakuMotion::Core& core, void* target, int currentFrame, bool native)
    {
        auto& player = core.player;
        auto& controller = player.controller;
        if (target != controller.targetActor) return;
        const auto binding = controller.bindings.find(target);
        const auto currentEpoch = core.sceneActorRegistry.Epoch();
        const float time = player.currentTime;
        const int seekCount = player.SeekCount();
        const bool eligible = native && ThreeActorNativePlayback(core) && currentFrame > 0 && std::isfinite(time) && binding != controller.bindings.end() &&
            binding->second.bindPoseCaptured && controller.fnGetLocalRotation && controller.fnSetLocalRotation;
        // An incomplete render pair disables the preview before the next actor
        // tick. Restore through the last bound transforms while they are alive.
        if (activeCamera) {
            Restore(activeCamera);
            CameraBridge::secondaryPoseBegin = nullptr;
            if (g_hostContext.logWarn) g_hostContext.logWarn("[GakuMotion][NativeSwingRenderPreview] missing_render_end frame=%d",frame);
        }
        armed = false;
        if (!eligible || actor != target || currentEpoch != epoch || seekCount != seeks ||
            currentFrame != frame+1 || time <= latestTime || time-latestTime > 0.1f) {
            joints = {};
            if (actor != target || currentEpoch != epoch) boneLookup.Clear();
        }
        if (!eligible || !CameraBridge::ready) {
            if (ThreeActorNativePlayback(core))
                Report(!native ? "awaiting_native_source_frame" : !eligible ? "binding_unavailable" : "camera_bridge_unavailable");
            frame = -1;
            return;
        }
        if (!hookAttempted) {
            hookAttempted = true;
            if (LiveAspectPreview::EnsureCameraEnd()) {
                LiveAspectPreview::secondaryPoseEnd = Restore;
                CameraBridge::secondaryPoseBegin = Apply;
            } else if (g_hostContext.logWarn) g_hostContext.logWarn("[GakuMotion][NativeSwingRenderPreview] end_hook_unavailable");
        }
        if (!CameraBridge::secondaryPoseBegin) {
            Report("camera_render_pair_unavailable");
            return;
        }
        read = controller.fnGetLocalRotation;
        write = controller.fnSetLocalRotation;
        for (size_t i = 0; i < joints.size(); ++i) {
            auto& joint = joints[i];
            void* transform = boneLookup.FindSampledSecondary(binding->second,controller,bones[i]);
            if (joint.transform != transform) joint = {transform};
            if (!transform) continue;
            const auto raw = Read(transform);
            const float norm = Quaternion::Dot(raw,raw);
            if (!std::isfinite(norm) || norm < 0.5f || norm > 1.5f) {
                joint.count = 0;
                continue;
            }
            const auto current = raw.Normalized();
            if (joint.count >= 2) {
                const float fraction = (latestTime-olderTime)/(time-olderTime);
                joint.preview = GakuMotion::native_swing::FilterMiddle(joint.older,joint.latest,current,fraction);
            }
            joint.older = joint.latest;
            joint.latest = current;
            if (joint.count < 3) ++joint.count;
        }
        previewTime = latestTime;
        olderTime = latestTime;
        latestTime = time;
        actor = target;
        epoch = currentEpoch;
        frame = currentFrame;
        seeks = seekCount;
        armed = true;
        if (std::none_of(joints.begin(),joints.end(),[](const Joint& joint) { return joint.transform != nullptr; }))
            Report("target_bones_unavailable");
        else if (std::none_of(joints.begin(),joints.end(),[](const Joint& joint) { return joint.count >= 3; }))
            Report("warming_up");
        else
            Report("queued_for_main_camera");
    }
}
