#pragma once

namespace NativeSwingRenderPreview
{
    using GakuMotion::Quaternion;
    struct Joint {
        void* transform{};
        std::string name;
        GakuMotion::native_swing::RotationSmoother filter{};
        Quaternion preview{}, restore{}, shown{};
        float previewTime{};
        bool ready{}, written{}, detailed{};
    };
    inline std::vector<Joint> joints;
    inline GakuMotion::FallbackPhysics boneLookup;
    inline GakuMotion::IdolController::GetRotationFn read{};
    inline GakuMotion::IdolController::SetRotationFn write{};
    inline void* actor{};
    inline void* activeCamera{};
    inline uint64_t epoch{};
    inline int frame{-1}, seeks{};
    inline float latestTime{};
    inline bool armed{}, hookAttempted{}, logged{};
    inline const char* lastStatus{};
    inline GakuMotion::native_swing::CameraPair cameraPair{};
    inline GakuLocalifyCameraRenderService renderService{};
    inline CameraBridge::Render pipelineEnd{};
    inline int observerToken{};
    inline bool pairReported{};
    inline size_t lastBoundCount{};

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

    inline void Restore(void* camera, const char* phase)
    {
        if (!activeCamera || camera != activeCamera || !CameraBridge::OnUnityThread()) return;
        const LivePhaseTimer timer{g_livePerf.renderRestore};
        auto& core = GakuMotion::Core::Instance();
        const bool recording = g_costumeCapture.Active() && CostumeCaptureMatches(core);
        unsigned written = 0, restoredCount = 0;
        for (auto& joint : joints) {
            if (!joint.written) continue;
            const bool alive = CameraBridge::IsAlive(joint.transform);
            Quaternion restored{};
            if (alive) {
                Write(joint.transform,joint.restore);
                if (recording) restored = Read(joint.transform);
            }
            ++written;
            const bool verified = alive && recording &&
                GakuMotion::native_swing::Angle(joint.restore.Normalized(),restored.Normalized()) < .1f;
            if (verified) ++restoredCount;
            if (recording && (joint.detailed || !verified))
                g_costumeCapture.RecordSwingRender(joint.name.c_str(),actor,frame,latestTime,joint.previewTime,
                    joint.restore,joint.shown,restored,phase,verified);
            joint.written = false;
        }
        if (recording)
            g_costumeCapture.RecordSwingRenderSummary(actor,frame,latestTime,joints.size(),written,restoredCount,phase);
        activeCamera = nullptr;
    }

    inline void End(void* camera)
    {
        if (!CameraBridge::OnUnityThread() || !g_faceUnityFrame || !cameraPair.End(camera,g_faceUnityFrame())) return;
        Restore(camera,"camera_end");
        if (!pairReported && g_hostContext.logInfo) {
            pairReported = true;
            g_hostContext.logInfo("[GakuMotion][NativeSwingRenderPreview] render_pair_observed frame=%d",frame);
        }
    }

    inline void EndDetour(void* context, void* camera, void* method)
    {
        pipelineEnd(context,camera,method);
        End(camera);
    }

    inline bool ConnectEnd()
    {
        void* pipeline = FindClass("UnityEngine.CoreModule.dll","UnityEngine.Rendering","RenderPipeline");
        void* target = pipeline ? FindMethodPointer(pipeline,"EndCameraRendering",2) : nullptr;
        if (!target) return false;
        const auto& api = GakuMotion::host::api;
        GakuLocalifyCameraRenderService service{};
        const auto queried = api.queryService ? api.queryService(api.hostContext,
            GAKULOCALIFY_SERVICE_CAMERA_RENDER,GAKULOCALIFY_CAMERA_RENDER_VERSION,sizeof(service),&service) : GAKU_E_UNAVAILABLE;
        if (queried == GAKU_OK) {
            if (service.abiVersion != GAKULOCALIFY_CAMERA_RENDER_VERSION || service.structSize < sizeof(service) ||
                !service.getTarget || !service.subscribe || !service.unsubscribe) return false;
            if (void* owned = service.getTarget(service.context,GAKULOCALIFY_CAMERA_RENDER_END)) {
                if (owned != target) return false;
                GakuLocalifyCameraRenderCallbacks callbacks{GAKULOCALIFY_CAMERA_RENDER_VERSION,sizeof(callbacks),
                    &observerToken, [](void*) -> uint32_t {
                        // The provider has one observer covering three entries.
                        // Only take its callbacks during a pending main-camera render.
                        return CameraBridge::OnUnityThread() && cameraPair.pending ? 1u : 0u;
                    }, [](void*,void**) {}, [](void*,void*,void*) {}, [](void*,void*) {},
                    [](void*,void* camera) { End(camera); }};
                if (service.subscribe(service.context,&callbacks) != GAKU_OK) return false;
                renderService = service;
                if (g_hostContext.logInfo) g_hostContext.logInfo(
                    "[GakuMotion][NativeSwingRenderPreview] render_end=RenderPipeline.EndCameraRendering route=localify");
                return true;
            }
        } else if (queried != GAKU_E_UNAVAILABLE) return false;
        const bool installed = CameraBridge::InstallHook("RenderPipeline.EndCameraRendering",target,
            reinterpret_cast<void*>(EndDetour),reinterpret_cast<void**>(&pipelineEnd));
        if (installed && g_hostContext.logInfo) g_hostContext.logInfo(
            "[GakuMotion][NativeSwingRenderPreview] render_end=RenderPipeline.EndCameraRendering route=direct");
        return installed;
    }

    inline void Shutdown()
    {
        if (activeCamera) Restore(activeCamera,"shutdown");
        cameraPair = {};
        CameraBridge::secondaryPoseBegin = nullptr;
        if (renderService.unsubscribe) renderService.unsubscribe(renderService.context,&observerToken);
        renderService = {};
    }

    inline void Apply(void* camera)
    {
        auto& core = GakuMotion::Core::Instance();
        if (!armed || activeCamera || !camera || !CameraBridge::getMain || camera != CameraBridge::getMain(nullptr) ||
            !g_faceUnityFrame || g_faceUnityFrame() != frame || !core.liveTakeoverActive || core.captureSuspended ||
            core.sceneActorRegistry.Epoch() != epoch || core.player.controller.targetActor != actor ||
            core.player.state != GakuMotion::PlaybackState::Playing) return;
        if (!cameraPair.Begin(camera,frame)) return;
        const LivePhaseTimer timer{g_livePerf.renderApply};
        const bool recording = g_costumeCapture.Active() && CostumeCaptureMatches(core);
        for (auto& joint : joints) {
            if (!joint.ready || !CameraBridge::IsAlive(joint.transform)) continue;
            joint.restore = Read(joint.transform);
            joint.written = true;
            joint.shown = joint.preview;
            activeCamera = camera;
            Write(joint.transform,joint.preview);
            if (recording && joint.detailed) joint.shown = Read(joint.transform);
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
        if (currentFrame == frame && target == actor && currentEpoch == epoch && seekCount == seeks) return;
        if (cameraPair.pending) {
            if (activeCamera) {
                Restore(activeCamera,"next_actor_tick");
                if (g_hostContext.logWarn) g_hostContext.logWarn("[GakuMotion][NativeSwingRenderPreview] missing_render_end frame=%d",frame);
            } else Report("awaiting_main_camera_end");
            cameraPair = {};
        }
        armed = false;
        if (!eligible || actor != target || currentEpoch != epoch || seekCount != seeks ||
            currentFrame != frame+1 || time <= latestTime || time-latestTime > 0.1f) {
            joints.clear();
            cameraPair = {};
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
            if (ConnectEnd()) {
                CameraBridge::secondaryPoseBegin = Apply;
            } else if (g_hostContext.logWarn) g_hostContext.logWarn("[GakuMotion][NativeSwingRenderPreview] end_hook_unavailable");
        }
        if (!CameraBridge::secondaryPoseBegin) {
            Report("camera_render_pair_unavailable");
            return;
        }
        read = controller.fnGetLocalRotation;
        write = controller.fnSetLocalRotation;
        const LivePhaseTimer timer{g_livePerf.smooth};
        size_t index = 0;
        boneLookup.VisitBoundSecondary(binding->second,controller,[&](void* transform,const std::string& name) {
            if (index == joints.size()) joints.emplace_back();
            auto& joint = joints[index++];
            if (joint.transform != transform || joint.name != name) {
                joint = {};
                joint.transform = transform;
                joint.name = name;
                joint.detailed = GakuMotion::CostumePhysicsCapture::DetailedSwingBone(name);
            }
            const auto raw = Read(transform);
            const float norm = Quaternion::Dot(raw,raw);
            if (!std::isfinite(norm) || norm < 0.5f || norm > 1.5f) {
                joint.filter.Reset();
                joint.ready = false;
                return;
            }
            joint.ready = joint.filter.Push(time,raw.Normalized(),joint.preview,joint.previewTime);
        });
        joints.resize(index);
        if (index != lastBoundCount) {
            lastBoundCount = index;
            if (g_hostContext.logInfo)
                g_hostContext.logInfo("[GakuMotion][NativeSwingRenderPreview] bound_secondary=%zu",index);
        }
        latestTime = time;
        actor = target;
        epoch = currentEpoch;
        frame = currentFrame;
        seeks = seekCount;
        armed = true;
        if (joints.empty())
            Report("target_bones_unavailable");
        else if (std::none_of(joints.begin(),joints.end(),[](const Joint& joint) { return joint.ready; }))
            Report("warming_up");
        else
            Report("queued_for_main_camera");
    }
}
