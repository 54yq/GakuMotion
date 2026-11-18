#pragma once

// Read-only evidence for an independent native graph on the existing actor.
// In particular, do not call the native custom-player getter or setup methods.
namespace LiveIndependentGraphProbe
{
    using Handle = std::array<unsigned char, 16>;

    inline void* Call(const void* method, void* receiver, void** args = nullptr)
    {
        if (!method || !receiver) return nullptr;
        void* exception = nullptr;
        void* result = g_faceRuntime.invoke(method, receiver, args, &exception);
        return exception ? nullptr : result;
    }

    template<typename T>
    bool Read(const void* method, void* receiver, T& result, void** args = nullptr)
    {
        void* boxed = Call(method, receiver, args);
        void* raw = boxed ? g_safeBones.unbox(boxed) : nullptr;
        if (!raw) return false;
        std::memcpy(&result, raw, sizeof(result));
        return true;
    }

    inline void* ReferenceField(void* object, const char* name)
    {
        if (!object) return nullptr;
        for (void* klass = g_faceRuntime.objectClass(object); klass; klass = il2cpp_class_get_parent(klass)) {
            void* field = g_faceRuntime.findField(klass, name);
            if (!field) continue;
            void* type = g_faceRuntime.fieldType(field);
            if (!type || (g_faceRuntime.fieldFlags(field) & 0x10)) return nullptr;
            const int code = g_faceRuntime.typeCode(type);
            if (code != 0x12 && code != 0x1c) return nullptr;
            void* value = nullptr;
            g_faceRuntime.readField(object, field, &value);
            return value;
        }
        return nullptr;
    }

    inline void* PlayableClass(const char* name)
    {
        void* type = FindClass("UnityEngine.CoreModule.dll", "UnityEngine.Playables", name);
        return type ? type : FindClass("UnityEngine.DirectorModule.dll", "UnityEngine.Playables", name);
    }

    inline bool SameHandle(const Handle& a, const Handle& b)
    {
        return std::memcmp(a.data(), b.data(), sizeof(uintptr_t) + sizeof(uint32_t)) == 0;
    }

    struct Counts {
        int nodes{}, scriptJobs{}, campusJobs{}, motionClips{};
        bool complete{};
    };

    template<typename Metadata>
    Counts Inspect(void* graphClass, const Handle& graphHandle, const Metadata& metadata)
    {
        alignas(16) Handle graph = graphHandle;
        Counts result;
        const auto fromReflection = reinterpret_cast<void*(*)(void*)>(
            GetProcAddress(g_gameAssembly, "il2cpp_class_from_system_type"));
        const auto valueSize = reinterpret_cast<int(*)(void*, uint32_t*)>(
            GetProcAddress(g_gameAssembly, "il2cpp_class_value_size"));
        void* handleClass = PlayableClass("PlayableHandle");
        void* typeClass = FindClass("mscorlib.dll", "System", "Type");
        void* scriptPlayable = FindClass("UnityEngine.AnimationModule.dll", "UnityEngine.Animations", "AnimationScriptPlayable");
        void* animationClip = FindClass("UnityEngine.AnimationModule.dll", "UnityEngine.Animations", "AnimationClipPlayable");
        void* motionClip = FindClass("vl-unity.Runtime.dll", "VL.Animation", "MotionClipPlayable");
        void* campusJob = FindClass("campus-submodule.Runtime.dll", "ActorAnimation", "CampusActorAnimationJob");
        uint32_t alignment{};
        if (!fromReflection || !valueSize || !handleClass || !typeClass || !scriptPlayable || !animationClip ||
            !motionClip || !campusJob || valueSize(handleClass, &alignment) != static_cast<int>(sizeof(Handle))) return result;
        const void* rootCount = metadata.Find(graphClass, "GetRootPlayableCount", false, {0x08}, {});
        const void* rootAt = metadata.Find(graphClass, "GetRootPlayableInternal", false, {0x11, handleClass}, {{0x08}});
        const void* valid = metadata.Find(handleClass, "IsValid", false, {0x02}, {});
        const void* inputCount = metadata.Find(handleClass, "GetInputCount", false, {0x08}, {});
        const void* inputAt = metadata.Find(handleClass, "GetInputHandle", false, {0x11, handleClass}, {{0x08}});
        const void* playableType = metadata.Find(handleClass, "GetPlayableType", false, {0x12, typeClass}, {});
        const void* jobType = metadata.Find(handleClass, "GetJobType", false, {0x12, typeClass}, {});
        if (!rootCount || !rootAt || !valid || !inputCount || !inputAt || !playableType || !jobType) return result;

        constexpr int limit = 256;
        alignas(16) std::array<Handle, limit> nodes{};
        int roots{};
        if (!Read(rootCount, graph.data(), roots) || roots < 0 || roots > limit) return result;
        int count = 0;
        auto enqueue = [&](const Handle& node) {
            if (SameHandle(node, {})) return true;
            for (int i = 0; i < count; ++i)
                if (SameHandle(nodes[i], node)) return true;
            if (count == limit) return false;
            nodes[count++] = node;
            return true;
        };
        for (int index = 0; index < roots; ++index) {
            Handle node{};
            void* args[]{&index};
            if (!Read(rootAt, graph.data(), node, args) || SameHandle(node, {}) || !enqueue(node)) return result;
        }
        for (int index = 0; index < count; ++index) {
            auto* node = nodes[index].data();
            unsigned char live{};
            if (!Read(valid, node, live) || !live) return result;
            void* reflected = Call(playableType, node);
            void* type = reflected ? fromReflection(reflected) : nullptr;
            if (!type) return result;
            ++result.nodes;
            if (type == animationClip || type == motionClip) ++result.motionClips;
            if (type == scriptPlayable) {
                void* job = Call(jobType, node);
                if (!job) return result;
                ++result.scriptJobs;
                if (fromReflection(job) == campusJob) ++result.campusJobs;
            }
            int inputs{};
            if (!Read(inputCount, node, inputs) || inputs < 0 || inputs > limit) return result;
            for (int i = 0; i < inputs; ++i) {
                Handle input{};
                void* args[]{&i};
                if (!Read(inputAt, node, input, args) || !enqueue(input)) return result;
            }
        }
        result.complete = true;
        return result;
    }

    template<typename Metadata>
    int FirstOutputMatches(void* graphClass, Handle& graph, void* animator, const Metadata& metadata)
    {
        void* output = PlayableClass("PlayableOutput");
        void* animation = FindClass("UnityEngine.AnimationModule.dll", "UnityEngine.Animations", "AnimationPlayableOutput");
        if (!output || !animation || !animator || !g_animatorKlass) return -1;
        const auto valueSize = reinterpret_cast<int(*)(void*, uint32_t*)>(
            GetProcAddress(g_gameAssembly, "il2cpp_class_value_size"));
        uint32_t alignment{};
        if (!valueSize || valueSize(output, &alignment) != 16 || valueSize(animation, &alignment) != 16) return -1;
        const void* get = metadata.Find(graphClass, "GetOutput", false, {0x11, output}, {{0x08}});
        const void* cast = metadata.Find(animation, "op_Explicit", true, {0x11, animation}, {{0x11, output}});
        const void* target = metadata.Find(animation, "GetTarget", false, {0x12, g_animatorKlass}, {});
        int index = 0;
        void* args[]{&index};
        alignas(16) Handle publicOutput{};
        if (!cast || !Read(get, graph.data(), publicOutput, args) || SameHandle(publicOutput, {})) return -1;
        void* castArgs[]{publicOutput.data()};
        void* exception = nullptr;
        void* boxed = g_faceRuntime.invoke(cast, nullptr, castArgs, &exception);
        void* raw = boxed && !exception ? g_safeBones.unbox(boxed) : nullptr;
        alignas(16) Handle typedOutput{};
        if (!raw) return -1;
        std::memcpy(typedOutput.data(), raw, typedOutput.size());
        void* bound = Call(target, typedOutput.data());
        return bound ? int(bound == animator) : -1;
    }

    template<typename Metadata>
    void Sample(void* actor, void* component, void* originalPlayer, void* playerBase,
                void* graphClass, const Handle& originalGraph, int phase, int slot, const Metadata& metadata)
    {
        const auto& core = GakuMotion::Core::Instance();
        if (!core.liveViewingConfirmed || !core.liveOwnerToken || !g_hostContext.logInfo || slot < 0 || slot > 1) return;
        static uintptr_t owner = 0;
        static int scene = 0;
        static std::array<std::array<void*, 2>, 2> sampled{};
        if (owner != core.liveOwnerToken || scene != core.liveOwnerScene) {
            owner = core.liveOwnerToken;
            scene = core.liveOwnerScene;
            sampled = {};
        }
        auto& previous = sampled[slot][phase == 3 ? 1 : 0];
        if (previous == actor) return;
        previous = actor;

        void* rig = ReferenceField(component, "_actorAnimationRig");
        void* builder = ReferenceField(component, "_builder");
        void* initData = ReferenceField(component, "_initializeData");
        void* customPlayer = ReferenceField(actor, "_customGraphAnimationPlayer");
        void* animator = HelperGetActorAnimator(actor);
        void* rigAnimator = ReferenceField(rig, "_animator");
        void* customClass = customPlayer ? g_faceRuntime.objectClass(customPlayer) : nullptr;
        for (void* type = customClass; type; type = il2cpp_class_get_parent(type)) {
            if (type == playerBase) {
                const void* graphMethod = metadata.Find(playerBase, "GetPlayableGraph", false, {0x11, graphClass}, {});
                alignas(16) Handle alternateGraph{};
                const bool read = Read(graphMethod, customPlayer, alternateGraph);
                const bool distinct = read && !SameHandle(alternateGraph, originalGraph);
                unsigned char valid{};
                const void* isValid = metadata.Find(graphClass, "IsValid", false, {0x02}, {});
                const auto alternate = distinct && Read(isValid, alternateGraph.data(), valid) && valid
                    ? Inspect(graphClass, alternateGraph, metadata) : Counts{};
                const int targetMatches = valid ? FirstOutputMatches(graphClass, alternateGraph, animator, metadata) : -1;
                const void* isPlaying = metadata.Find(graphClass, "IsPlaying", false, {0x02}, {});
                unsigned char playing{};
                const int running = valid && Read(isPlaying, alternateGraph.data(), playing) ? int(playing) : -1;
                g_hostContext.logInfo("[GakuMotion][IndependentActorGraph] slot=%d phase=%d custom_read=%d distinct=%d valid=%d playing=%d first_output_matches=%d scan=%d nodes=%d campus_jobs=%d clips=%d",
                    slot, phase, int(read), int(distinct), int(valid), running, targetMatches, int(alternate.complete),
                    alternate.nodes, alternate.campusJobs, alternate.motionClips);
                break;
            }
        }
        const auto original = Inspect(graphClass, originalGraph, metadata);
        g_hostContext.logInfo("[GakuMotion][IndependentActorGraph] slot=%d phase=%d original_scan=%d nodes=%d script_jobs=%d campus_jobs=%d clips=%d rig=%d rig_animator_matches=%d init_data=%d builder=%d custom=%d custom_is_original=%d",
            slot, phase, int(original.complete), original.nodes, original.scriptJobs, original.campusJobs,
            original.motionClips, int(rig != nullptr), int(animator && rigAnimator == animator),
            int(initData != nullptr), int(builder != nullptr), int(customPlayer != nullptr), int(customPlayer == originalPlayer));

        using TypeName = std::pair<const char*, const char*>;
        const TypeName candidates[]{
            {"ActorAnimation", "CampusActorAnimationBuilder"},
            {"ActorAnimation", "CampusActorAnimationRig"},
            {"ActorAnimation", "ActorAnimationSimpleInitializerUtility"},
            {"Campus.Common.Animation", "CustomGraphAnimationPlayer"}
        };
        for (const auto& candidate : candidates)
            LiveCodeCapture::Class(FindClass("campus-submodule.Runtime.dll", candidate.first, candidate.second));
        if (!customClass)
            customClass = FindClass("Assembly-CSharp.dll", "Campus.Common.Animation", "CustomGraphAnimationPlayer");
        if (rig)
            LiveCodeCapture::Class(il2cpp_class_get_parent(g_faceRuntime.objectClass(rig)));
        LiveCodeCapture::Class(customClass);
        if (customClass && g_campusActorControllerKlass) {
            const void* method = metadata.Find(g_campusActorControllerKlass, "GetCustomGraphAnimationPlayer", false,
                                               {0x12, customClass}, {});
            if (method) LiveCodeCapture::Address("Campus.Common.CampusActorController", "GetCustomGraphAnimationPlayer()",
                                                GroundShadowProbeBootstrap::MethodPointer(method));
        }
    }
}
