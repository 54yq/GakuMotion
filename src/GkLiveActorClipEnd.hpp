#pragma once

// The role graph keeps its outputs and native jobs. Only verified animation
// playables have their local playback advanced beyond the end and held there.
class LiveActorClipEnd
{
    struct alignas(16) Handle {
        std::array<unsigned char, 16> bytes{};
        bool operator==(const Handle& other) const
        {
            return std::memcmp(bytes.data(), other.bytes.data(), sizeof(uintptr_t) + sizeof(uint32_t)) == 0;
        }
    };
    struct Clip {
        Handle handle;
        double time{}, speed{}, end{}, animationLength{};
        int wrap{};
        unsigned char done{};
        bool touched{};
    };
    struct Queued {
        Handle handle;
        double inheritedAnimationLength{};
    };
    std::vector<Clip> clips;
    std::vector<Handle> outputs;
    const void *outputAt{}, *outputSource{}, *nodeValid{}, *nodeType{}, *inputCount{}, *inputAt{},
        *duration{}, *time{}, *speed{}, *wrap{}, *done{}, *setTime{}, *setSpeed{}, *setWrap{}, *setDone{},
        *scriptInstance{}, *clipLength{}, *clipAnimationLength{};
    void *nativeClipType{}, *motionClipType{}, *clipParamType{}, *clipParamField{};
    using ClassFromType = void*(*)(void*);
    ClassFromType classFromType{};
    double nextCheck{};
    static constexpr double oneFrame = 1.0 / 60.0; // This path has no source clip FPS API.
    static constexpr int holdWrap = 0; // Unity DirectorWrapMode.Hold

    static bool HasEnd(double length)
    {
        return std::isfinite(length) && length > 0.0 && length + oneFrame > length;
    }

    static bool Fail(const char* reason)
    {
        if (g_hostContext.logInfo)
            g_hostContext.logInfo("[GakuMotion][LiveActorClipEnd] unavailable=%s", reason);
        return false;
    }
    static void* Call(const void* method, void* object, void** args = nullptr)
    {
        void* ex = nullptr;
        void* boxed = g_faceRuntime.invoke(method, object, args, &ex);
        return ex ? nullptr : boxed;
    }
    template<class T>
    static bool Read(const void* method, void* object, T& value, void** args = nullptr)
    {
        void* boxed = Call(method, object, args);
        void* data = boxed ? g_safeBones.unbox(boxed) : nullptr;
        if (!data) return false;
        std::memcpy(&value, data, sizeof(T));
        return true;
    }
    template<class T>
    static bool Write(const void* method, Handle& handle, T value)
    {
        void* ex = nullptr;
        void* args[]{&value};
        g_faceRuntime.invoke(method, handle.bytes.data(), args, &ex);
        return !ex;
    }

    bool ReadOutput(void* graph, int index, Handle& source) const
    {
        Handle handle;
        unsigned char present{};
        void* args[]{&index, handle.bytes.data()};
        return Read(outputAt, graph, present, args) && present &&
            Read(outputSource, handle.bytes.data(), source);
    }

    template<class Metadata>
    bool ResolveMotionClip(const Metadata& metadata, void* playableHandle)
    {
        motionClipType = FindClass("vl-unity.Runtime.dll", "VL.Animation", "MotionClipPlayable");
        if (!motionClipType) return true; // Other runtimes may only use Unity clip playables.
        clipParamType = FindClass("vl-unity.Runtime.dll", "VL.Animation", "ClipParam");
        scriptInstance = metadata.Find(playableHandle, "GetScriptInstance", false, {0x1c}, {});
        clipLength = metadata.Find(clipParamType, "get_length", false, {0x0d}, {});
        clipAnimationLength = metadata.Find(clipParamType, "get_animationLength", false, {0x0c}, {});
        if (!clipParamType || !scriptInstance || !clipLength || !g_faceRuntime.findField ||
            !g_faceRuntime.fieldType || !g_faceRuntime.fieldFlags || !g_faceRuntime.readField ||
            !g_faceRuntime.objectClass || !metadata.classFromType) return false;
        clipParamField = g_faceRuntime.findField(motionClipType, "clipParam");
        void* type = clipParamField ? g_faceRuntime.fieldType(clipParamField) : nullptr;
        return type && !(g_faceRuntime.fieldFlags(clipParamField) & 0x10) &&
            metadata.classFromType(type) == clipParamType;
    }

    bool ReadGameClipDuration(Clip& clip, double& length) const
    {
        void* instance = Call(scriptInstance, clip.handle.bytes.data());
        if (!instance || g_faceRuntime.objectClass(instance) != motionClipType) return false;
        void* param = nullptr;
        g_faceRuntime.readField(instance, clipParamField, &param);
        if (!param || g_faceRuntime.objectClass(param) != clipParamType ||
            !Read(clipLength, param, length)) return false;
        float animationLength{};
        if (clipAnimationLength && Read(clipAnimationLength, param, animationLength) && HasEnd(animationLength))
            clip.animationLength = animationLength;
        if (!HasEnd(length)) length = clip.animationLength;
        if (HasEnd(length) && clip.animationLength < length) clip.animationLength = length;
        return HasEnd(length);
    }

    bool AddClip(Handle handle, bool motionClip, double& inheritedAnimationLength)
    {
        const auto existing = std::find_if(clips.begin(), clips.end(), [&](const Clip& item) { return item.handle == handle; });
        if (existing != clips.end()) {
            if (motionClip) inheritedAnimationLength = existing->animationLength;
            return true;
        }
        Clip clip{handle};
        double length{};
        if (motionClip) {
            if (!ReadGameClipDuration(clip, length)) return false;
            inheritedAnimationLength = clip.animationLength;
        } else {
            // Unity clip playables commonly report double.MaxValue. The game
            // supplies the source animation length on their enclosing clip.
            if (!Read(duration, clip.handle.bytes.data(), length) || !HasEnd(length))
                length = inheritedAnimationLength;
            if (!HasEnd(length)) return false;
        }
        clip.end = length + oneFrame;
        auto* value = clip.handle.bytes.data();
        if (!Read(time, value, clip.time) || !std::isfinite(clip.time) ||
            !Read(speed, value, clip.speed) || !std::isfinite(clip.speed) ||
            !Read(wrap, value, clip.wrap) || clip.wrap < 0 || clip.wrap > 2 ||
            !Read(done, value, clip.done)) return false;
        clips.push_back(clip);
        return true;
    }

public:
    bool Prepare(void* graphClass, void* graph, unsigned actorOutputCount)
    {
        *this = {};
        const auto metadata = GroundShadowProbeBootstrap::Resolve(g_gameAssembly).method;
        auto unityType = [](const char* name) {
            void* type = FindClass("UnityEngine.CoreModule.dll", "UnityEngine.Playables", name);
            return type ? type : FindClass("UnityEngine.DirectorModule.dll", "UnityEngine.Playables", name);
        };
        void* handle = unityType("PlayableHandle");
        void* output = unityType("PlayableOutputHandle");
        void* wrapType = unityType("DirectorWrapMode");
        nativeClipType = FindClass("UnityEngine.AnimationModule.dll", "UnityEngine.Animations", "AnimationClipPlayable");
        using ValueSize = int(*)(void*, uint32_t*);
        auto size = reinterpret_cast<ValueSize>(GetProcAddress(g_gameAssembly, "il2cpp_class_value_size"));
        classFromType = reinterpret_cast<ClassFromType>(GetProcAddress(g_gameAssembly, "il2cpp_class_from_system_type"));
        uint32_t alignment{};
        if (!handle || !output || !wrapType || !nativeClipType || !size || !classFromType ||
            size(handle, &alignment) != sizeof(Handle) || size(output, &alignment) != sizeof(Handle))
            return Fail("playable_types");
        outputAt = metadata.Find(graphClass, "GetOutputInternal", false, {0x02}, {{0x08}, {0x11, output, true}});
        outputSource = metadata.Find(output, "GetSourcePlayable", false, {0x11, handle}, {});
        nodeValid = metadata.Find(handle, "IsValid", false, {0x02}, {});
        nodeType = metadata.Find(handle, "GetPlayableType", false, {0x12}, {});
        inputCount = metadata.Find(handle, "GetInputCount", false, {0x08}, {});
        inputAt = metadata.Find(handle, "GetInputHandle", false, {0x11, handle}, {{0x08}});
        duration = metadata.Find(handle, "GetDuration", false, {0x0d}, {});
        time = metadata.Find(handle, "GetTime", false, {0x0d}, {});
        speed = metadata.Find(handle, "GetSpeed", false, {0x0d}, {});
        wrap = metadata.Find(handle, "GetTimeWrapMode", false, {0x11, wrapType}, {});
        done = metadata.Find(handle, "IsDone", false, {0x02}, {});
        setTime = metadata.Find(handle, "SetTime", false, {0x01}, {{0x0d}});
        setSpeed = metadata.Find(handle, "SetSpeed", false, {0x01}, {{0x0d}});
        setWrap = metadata.Find(handle, "SetTimeWrapMode", false, {0x01}, {{0x11, wrapType}});
        setDone = metadata.Find(handle, "SetDone", false, {0x01}, {{0x02}});
        if (!outputAt || !outputSource || !nodeValid || !nodeType || !inputCount || !inputAt ||
            !duration || !time || !speed || !wrap || !done || !setTime || !setSpeed || !setWrap || !setDone)
            return Fail("clip_playback_methods");
        const bool gameClipReady = ResolveMotionClip(metadata, handle);

        std::vector<Queued> pending;
        for (unsigned index = 0; index < actorOutputCount; ++index) {
            Handle source;
            if (!ReadOutput(graph, static_cast<int>(index), source)) return Fail("actor_output");
            outputs.push_back(source);
            pending.push_back({source, 0.0});
        }
        for (size_t index = 0; index < pending.size(); ++index) {
            auto [node, inheritedAnimationLength] = pending[index];
            auto* value = node.bytes.data();
            unsigned char valid{};
            if (!Read(nodeValid, value, valid) || !valid) return Fail("actor_graph_node");
            void* type = Call(nodeType, value);
            void* klass = type ? classFromType(type) : nullptr;
            if (!klass) return Fail("actor_graph_node_type");
            const bool motionClip = klass == motionClipType;
            if (motionClip && !gameClipReady) return Fail("game_clip_metadata");
            if ((motionClip || klass == nativeClipType) && !AddClip(node, motionClip, inheritedAnimationLength))
                return Fail(motionClip ? "game_clip_duration_or_state" : "unity_clip_duration_or_state");
            int count{};
            if (!Read(inputCount, value, count) || count < 0 || count > 64) return Fail("actor_graph_inputs");
            for (int port = 0; port < count; ++port) {
                Handle child;
                void* args[]{&port};
                if (!Read(inputAt, value, child, args)) return Fail("actor_graph_input");
                if (child == Handle{}) continue;
                if (std::find_if(pending.begin(), pending.end(), [&](const Queued& item) {
                    return item.handle == child && item.inheritedAnimationLength == inheritedAnimationLength;
                }) != pending.end()) continue;
                if (pending.size() >= 512) return Fail("actor_graph_size");
                pending.push_back({child, inheritedAnimationLength});
            }
        }
        return !clips.empty() || Fail("no_finite_animation_clips");
    }

    bool Park(double now)
    {
        nextCheck = now + 0.25;
        for (auto& clip : clips) {
            clip.touched = true;
            if (!Write(setWrap, clip.handle, holdWrap) || !Write(setSpeed, clip.handle, 0.0) ||
                !Write(setTime, clip.handle, clip.end)) return false;
        }
        return true;
    }

    bool Check(void* graph, double now)
    {
        if (now < nextCheck) return true;
        nextCheck = now + 0.25;
        for (size_t index = 0; index < outputs.size(); ++index) {
            Handle source;
            if (!ReadOutput(graph, static_cast<int>(index), source) || !(source == outputs[index]))
                return Fail("actor_output_changed");
        }
        for (auto& clip : clips) {
            double current{}, rate{};
            int wrapMode{};
            unsigned char valid{};
            auto* node = clip.handle.bytes.data();
            if (!Read(nodeValid, node, valid) || !valid || !Read(time, node, current) ||
                !std::isfinite(current) || std::abs(current - clip.end) > 0.001 ||
                !Read(speed, node, rate) || rate != 0.0 || !Read(wrap, node, wrapMode) || wrapMode != holdWrap)
                return Fail("animation_clip_no_longer_at_end");
        }
        return true;
    }

    bool Restore()
    {
        bool restored = true;
        for (auto& clip : clips) {
            if (!clip.touched) continue;
            unsigned char valid{};
            if (!Read(nodeValid, clip.handle.bytes.data(), valid) || !valid) {
                restored = false;
                continue;
            }
            bool clipRestored = Write(setTime, clip.handle, clip.time);
            clipRestored = Write(setWrap, clip.handle, clip.wrap) && clipRestored;
            clipRestored = Write(setDone, clip.handle, clip.done) && clipRestored;
            clipRestored = Write(setSpeed, clip.handle, clip.speed) && clipRestored;
            clip.touched = !clipRestored;
            restored = clipRestored && restored;
        }
        if (restored) *this = {};
        return restored;
    }

    size_t ClipCount() const { return clips.size(); }
};
