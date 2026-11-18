#pragma once

// Included only by the plugin after StageLoader and CameraBridge. This observer
// never writes an eye Transform or changes any native gaze controller.
namespace EyeGazeProbe
{
    using Clock = std::chrono::steady_clock;
    using RenderLoop = void (*)(void*, intptr_t, void*, void*);
    using Quaternion = GakuMotion::Quaternion;
    inline RenderLoop originalRenderLoop = nullptr;
    inline void* hookTarget = nullptr;
    inline thread_local unsigned renderDepth = 0;
    inline DWORD ownerThread = 0;
    inline uint64_t lastSequence = 0, sequence = 0, sampleIndex = 0;
    inline int remaining = 0, ioFailures = 0;
    inline std::string outputRoot, label, hookFailure;
    inline std::string lastReject, lastBoundary;
    struct Counters
    {
        uint64_t beforeCalls = 0, prepared = 0, afterWritten = 0;
        uint64_t endCalls = 0, endSameFrame = 0, cameraMainMatch = 0;
    };
    inline Counters counters{};
    inline Clock::time_point lastPoll{}, started{}, lastSample{};
    struct Sample
    {
        void* actor = nullptr;
        void* transforms[2]{};
        Quaternion before[2]{}, target[2]{}, immediate[2]{};
        int frame = -1, motionIndex = -1, seeks = -1;
        float beforeTime = 0, time = 0;
        uint64_t serial = 0;
        bool prepared = false, written = false;
    };
    inline Sample sample{};

    inline bool OnThread() { return ownerThread && GetCurrentThreadId() == ownerThread; }
    inline std::string Quote(const std::string& value)
    {
        return "\"" + GakuMotion::jsonlite::EscapeString(value) + "\"";
    }
    inline std::string Pointer(const void* value)
    {
        std::ostringstream out;
        out << "\"0x" << std::hex << reinterpret_cast<uintptr_t>(value) << "\"";
        return out.str();
    }
    inline std::ostringstream Json()
    {
        std::ostringstream out;
        out.imbue(std::locale::classic()); out << std::setprecision(9);
        return out;
    }
    inline bool Valid(const Quaternion& q)
    {
        const double norm = double(q.x)*q.x + double(q.y)*q.y + double(q.z)*q.z + double(q.w)*q.w;
        return std::isfinite(norm) && norm > 1e-12;
    }
    inline double Angle(const Quaternion& a, const Quaternion& b)
    {
        const double dot = double(a.x)*b.x + double(a.y)*b.y + double(a.z)*b.z + double(a.w)*b.w;
        const double an = double(a.x)*a.x + double(a.y)*a.y + double(a.z)*a.z + double(a.w)*a.w;
        const double bn = double(b.x)*b.x + double(b.y)*b.y + double(b.z)*b.z + double(b.w)*b.w;
        return 2.0 * std::acos(std::clamp(std::abs(dot) / std::sqrt(an * bn), 0.0, 1.0)) * 180.0 / 3.14159265358979323846;
    }
    inline std::string Rotation(const Quaternion& q)
    {
        auto out = Json();
        out << '[' << q.x << ',' << q.y << ',' << q.z << ',' << q.w << ']';
        return out.str();
    }
    inline bool Read(void* transform, Quaternion& out)
    {
        if (!OnThread() || !transform || !g_getRotNative || !CameraBridge::alive ||
            !CameraBridge::alive(transform, nullptr)) return false;
        float value[4]{};
        HelperGetLocalRot(transform, value);
        out = {value[0], value[1], value[2], value[3]};
        return Valid(out);
    }
    inline void Status(const char* state)
    {
        auto out = Json();
        out << "{\"schema\":1,\"pid\":" << GetCurrentProcessId() << ",\"seq\":" << sequence
            << ",\"state\":" << Quote(state) << ",\"sample_count\":" << sampleIndex
            << ",\"remaining\":" << remaining << ",\"render_hook_ready\":" << (hookTarget ? "true" : "false")
            << ",\"render_loop_hook_ready\":" << (hookTarget ? "true" : "false")
            << ",\"render_boundary\":" << Quote(lastBoundary)
            << ",\"before_calls\":" << counters.beforeCalls << ",\"prepared\":" << counters.prepared
            << ",\"after_written\":" << counters.afterWritten << ",\"end_calls\":" << counters.endCalls
            << ",\"end_same_frame\":" << counters.endSameFrame << ",\"camera_main_match\":" << counters.cameraMainMatch
            << ",\"last_reject\":" << Quote(lastReject)
            << ",\"failure\":" << Quote(hookFailure) << ",\"io_failures\":" << ioFailures << "}\n";
        if (!GroundShadowProbeBootstrap::Write(outputRoot + "/eye_gaze_probe_status.json", out.str())) ++ioFailures;
    }
    inline void ObserveEnd(void* camera, const char* boundary)
    {
        if (!OnThread() || remaining <= 0) return;
        ++counters.endCalls; lastBoundary = boundary;
        if (!sample.written || !g_faceUnityFrame) { lastReject = "end_without_completed_write"; return; }
        auto& core = GakuMotion::Core::Instance();
        std::lock_guard<std::recursive_mutex> stateLock(core.stateMutex);
        const int frame = g_faceUnityFrame();
        if (frame != sample.frame) { lastReject = "end_frame_mismatch"; return; }
        ++counters.endSameFrame;
        // The whole normal SRP loop has returned. Camera is an optional main-
        // camera reference, not a camera callback parameter or attribution.
        if (camera && camera == HelperCameraGetMain()) ++counters.cameraMainMatch;
        if (Clock::now() - started > std::chrono::seconds(30) ||
            core.currentMotionIndex != sample.motionIndex || core.player.SeekCount() != sample.seeks ||
            core.player.state == GakuMotion::PlaybackState::Stopped || !g_motionSceneAllowed ||
            !core.IsTargetActor(sample.actor)) { lastReject = "end_playback_context_changed"; return; }
        auto& controller = core.player.controller;
        const auto binding = controller.bindings.find(sample.actor);
        if (binding == controller.bindings.end() || !binding->second.bindPoseCaptured ||
            !controller.CanDriveActor(sample.actor)) { lastReject = "end_binding_unavailable"; return; }
        for (int eye = 0; eye < 2; ++eye)
        {
            bool same = false;
            for (size_t i = 0; i < binding->second.boneHids.size() && i < binding->second.boneTransforms.size(); ++i)
                if (binding->second.boneHids[i] == 21 + eye && binding->second.boneTransforms[i] == sample.transforms[eye]) same = true;
            if (!same) { lastReject = "end_eye_identity_changed"; return; }
        }
        Quaternion final[2];
        if (!Read(sample.transforms[0], final[0]) || !Read(sample.transforms[1], final[1]))
        { lastReject = "end_eye_read_failed"; return; }
        auto out = Json();
        out << "{\"schema\":1,\"pid\":" << GetCurrentProcessId() << ",\"seq\":" << sequence
            << ",\"sample\":" << ++sampleIndex << ",\"label\":" << Quote(label)
            << ",\"phase\":" << Quote(boundary) << ",\"frame\":" << frame
            << ",\"before_frame\":" << sample.frame << ",\"after_frame\":" << sample.frame
            << ",\"same_unity_frame\":true,\"clip_time_before\":" << sample.beforeTime
            << ",\"clip_time\":" << sample.time << ",\"pose_serial\":" << sample.serial
            << ",\"actor\":" << Pointer(sample.actor) << ",\"camera\":" << Pointer(camera)
            << ",\"scene\":" << Quote(g_currentSceneName) << ",\"motion_index\":" << sample.motionIndex
            << ",\"seek_count\":" << sample.seeks << ",\"eyes\":[";
        for (int i = 0; i < 2; ++i)
        {
            out << (i ? "," : "") << "{\"hid\":" << 21+i << ",\"transform\":" << Pointer(sample.transforms[i])
                << ",\"before_local\":" << Rotation(sample.before[i]) << ",\"target_local\":" << Rotation(sample.target[i])
                << ",\"immediate_local\":" << Rotation(sample.immediate[i]) << ",\"render_end_local\":" << Rotation(final[i])
                << ",\"target_immediate_error_deg\":" << Angle(sample.target[i], sample.immediate[i])
                << ",\"target_render_end_error_deg\":" << Angle(sample.target[i], final[i])
                << ",\"immediate_render_end_error_deg\":" << Angle(sample.immediate[i], final[i]) << '}';
        }
        out << "],\"scope\":\"CPU eye localRotation after whole normal SRP loop return; optional camera is a main-camera reference only; not GPU or visual acceptance\"}\n";
        std::ofstream file(GakuMotion::MotionPathFromUtf8(outputRoot + "/eye_gaze_probe.jsonl"), std::ios::binary | std::ios::app);
        if (file) file << out.str();
        if (!file) ++ioFailures;
        --remaining; sample.written = false; lastSample = Clock::now(); lastReject.clear();
        if (!remaining) Status("complete");
    }
    inline void DoRenderLoop(void* asset, intptr_t loop, void* request, void* method)
    {
        const bool outerNormal = !request && renderDepth == 0;
        struct Depth { Depth() { ++renderDepth; } ~Depth() { --renderDepth; } };
        { Depth depth; if (originalRenderLoop) originalRenderLoop(asset, loop, request, method); }
        if (outerNormal && !renderDepth && OnThread() && remaining > 0)
            try { ObserveEnd(HelperCameraGetMain(), "RenderPipelineManager.DoRenderLoop_Internal.exit"); }
            catch (...) { ++ioFailures; }
    }
    inline bool Install()
    {
        if (hookTarget) return true;
        if (!OnThread() || !GakuMotion::host::active || !GakuMotion::host::hooks.acquire ||
            !GakuMotion::host::hooks.release) { hookFailure = "host_hook_api_unavailable"; return false; }
        void* manager = FindClass("UnityEngine.CoreModule.dll", "UnityEngine.Rendering", "RenderPipelineManager");
        void* asset = FindClass("UnityEngine.CoreModule.dll", "UnityEngine.Rendering", "RenderPipelineAsset");
        void* object = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Object");
        if (!manager || !asset || !object) { hookFailure = "render_loop_classes_unavailable"; return false; }
        // Existing Unity6000 integration independently identifies this static
        // void (RenderPipelineAsset, IntPtr, UnityEngine.Object) native bridge.
        const void* method = StageLoader::metadata.Find(manager, "DoRenderLoop_Internal", true,
            {0x01}, {{0x12, asset}, {0x18}, {0x12, object}});
        void* target = GroundShadowProbeBootstrap::MethodPointer(method);
        if (!target) { hookFailure = "DoRenderLoop_Internal_exact_signature_unavailable"; return false; }
        const auto result = GakuMotion::host::hooks.acquire(GakuMotion::host::hooks.context, target,
            reinterpret_cast<void*>(DoRenderLoop), reinterpret_cast<void**>(&originalRenderLoop));
        if (result != GAKU_OK) { hookFailure = "render_loop_lease=" + std::to_string(result); return false; }
        hookTarget = target;
        if (!originalRenderLoop)
        {
            GakuMotion::host::hooks.release(GakuMotion::host::hooks.context, target);
            hookTarget = nullptr; hookFailure = "null_original"; return false;
        }
        hookFailure.clear(); return true;
    }
    inline void Poll()
    {
        const auto now = Clock::now();
        if (lastPoll.time_since_epoch().count() && now - lastPoll < std::chrono::milliseconds(500)) return;
        lastPoll = now;
        if (remaining && now - started > std::chrono::seconds(30))
        { remaining = 0; sample = {}; Status("timeout"); }
        else if (remaining) Status("armed");
        outputRoot = GakuMotion::Core::Instance().configRoot;
        std::ifstream file(GakuMotion::MotionPathFromUtf8(outputRoot + "/eye_gaze_probe.request.json"), std::ios::binary | std::ios::ate);
        if (!file) return;
        const auto size = file.tellg();
        if (size <= 0 || size > 65536) return;
        std::string text(static_cast<size_t>(size), '\0'); file.seekg(0);
        if (!file.read(text.data(), static_cast<std::streamsize>(text.size()))) return;
        GakuMotion::jsonlite::Value request; std::string error;
        if (!GakuMotion::jsonlite::ParseText(text, request, error) || !request.IsObject()) return;
        const auto* seqValue = request.Find("seq"); const auto* pid = request.Find("pid");
        const double seq = seqValue && seqValue->IsNumber() ? seqValue->AsDouble(0) : 0;
        if (!std::isfinite(seq) || seq < 1 || seq > 9007199254740991.0 || std::floor(seq) != seq ||
            (pid && (!pid->IsNumber() || (pid->AsDouble(-1) != 0 && pid->AsDouble(-1) != GetCurrentProcessId())))) return;
        if (static_cast<uint64_t>(seq) <= lastSequence) return;
        const auto* samples = request.Find("samples");
        const double count = samples && samples->IsNumber() ? samples->AsDouble(1) : 1;
        if (!std::isfinite(count) || std::floor(count) != count) return;
        lastSequence = sequence = static_cast<uint64_t>(seq);
        remaining = static_cast<int>(std::clamp(count, 1.0, 16.0));
        label = request.StringPath("label", ""); if (label.size() > 256) label = "label-too-long";
        sample = {}; counters = {}; lastReject.clear(); lastBoundary.clear(); sampleIndex = 0; started = now; lastSample = {};
        if (!Install()) { remaining = 0; Status("render_hook_unavailable"); return; }
        Status("armed");
    }
    inline void Before(void* actor)
    {
        if (!ownerThread) ownerThread = GetCurrentThreadId();
        if (!OnThread()) return;
        try
        {
            Poll();
            auto& core = GakuMotion::Core::Instance();
            if (remaining <= 0) return;
            ++counters.beforeCalls;
            if (!g_faceUnityFrame || !g_motionSceneAllowed || !core.IsTargetActor(actor) ||
                core.player.state == GakuMotion::PlaybackState::Stopped ||
                (lastSample.time_since_epoch().count() && Clock::now() - lastSample < std::chrono::milliseconds(100)))
            { lastReject = "before_context_or_rate_gate"; return; }
            sample = {};
            auto& controller = core.player.controller;
            if (!controller.fnGetBoneTransform) { lastReject = "before_bone_api_missing"; return; }
            sample.actor = actor; sample.frame = g_faceUnityFrame(); sample.beforeTime = core.player.currentTime;
            if (sample.frame < 0) { lastReject = "before_invalid_frame"; return; }
            for (int i = 0; i < 2; ++i)
            {
                sample.transforms[i] = controller.fnGetBoneTransform(actor, 21+i);
                if (!Read(sample.transforms[i], sample.before[i])) { lastReject = "before_eye_read_failed"; return; }
            }
            sample.prepared = true; ++counters.prepared;
        }
        catch (...) { sample = {}; ++ioFailures; }
    }
    inline void After(void* actor)
    {
        if (!OnThread() || remaining <= 0 || !sample.prepared || sample.actor != actor || !g_faceUnityFrame ||
            g_faceUnityFrame() != sample.frame) return;
        try
        {
            auto& core = GakuMotion::Core::Instance();
            const auto& stamp = core.player.controller.eyePoseWriteStamp;
            if (stamp.actor != actor || stamp.mask != 3) { lastReject = "after_incomplete_eye_stamp"; return; }
            for (int i = 0; i < 2; ++i)
            {
                if (stamp.transforms[i] != sample.transforms[i] || !Valid(stamp.localTargets[i]) ||
                    !Read(sample.transforms[i], sample.immediate[i])) { lastReject = "after_eye_identity_or_read_failed"; return; }
                sample.target[i] = stamp.localTargets[i];
            }
            sample.time = core.player.currentTime; sample.serial = stamp.serial;
            sample.motionIndex = core.currentMotionIndex; sample.seeks = core.player.SeekCount(); sample.written = true;
            ++counters.afterWritten;
        }
        catch (...) { sample = {}; ++ioFailures; }
    }
    inline void Shutdown()
    {
        remaining = 0; sample = {}; ownerThread = 0;
        if (hookTarget && GakuMotion::host::hooks.release)
        {
            const auto result = GakuMotion::host::hooks.release(GakuMotion::host::hooks.context, hookTarget);
            if (result == GAKU_OK) { hookTarget = nullptr; originalRenderLoop = nullptr; }
            else if (g_hostContext.logWarn) g_hostContext.logWarn(
                "[GakuMotion][EyeGazeProbe] hook release failed status=%d; disabled observer keeps forwarding", result);
        }

    }
}
