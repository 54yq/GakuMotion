#pragma once

// One experiment-owned graph. The engine still owns the actor, costume and
// its initialized rig. The two graphs must never evaluate that rig together.
struct IndependentLiveActorGraph
{
    using Handle = LiveIndependentGraphProbe::Handle;
    using Clock = LiveActorClockMvp;

    alignas(16) Handle graph{};
    StageLoader::Handle builderPin{};
    const void *getOriginalGraph{}, *destroy{}, *rootAt{}, *nodeTime{};
    bool owned{}, confirmedSwing{}, originalRigChanged{};
    alignas(16) Handle suspendedOriginal{};
    bool originalSuspended{}, restoredRunningOriginal{};
    double nextCheck{};
    uint64_t fallbackSession = UINT64_MAX;
    const char* reason{};

    static bool Invoke(const void* method, void* self, void** args = nullptr)
    {
        if (!method) return false;
        void* exception = nullptr;
        g_faceRuntime.invoke(method, self, args, &exception);
        return !exception;
    }

    int State(const void* method)
    {
        if (!owned) return -1;
        unsigned char value{};
        return LiveIndependentGraphProbe::Read(method, graph.data(), value) ? int(value != 0) : -1;
    }

    bool OriginalPaused() const
    {
        alignas(16) Handle current{};
        return Clock::SameOwner() && Clock::Read(Clock::valid, true) == 1 &&
            Clock::Read(Clock::mode) == 3 && Clock::Read(Clock::playing, true) == 0 &&
            LiveIndependentGraphProbe::Read(getOriginalGraph, reinterpret_cast<void*>(Clock::player), current) &&
            LiveIndependentGraphProbe::SameHandle(current, Clock::graph);
    }

    bool OriginalRestored() const
    {
        if (!restoredRunningOriginal) return OriginalPaused();
        alignas(16) Handle current{};
        return Clock::SameOwner() && Clock::Read(Clock::valid, true) == 1 &&
            Clock::Read(Clock::mode) == 1 && Clock::Read(Clock::playing, true) == 1 &&
            LiveIndependentGraphProbe::Read(getOriginalGraph, reinterpret_cast<void*>(Clock::player), current) &&
            LiveIndependentGraphProbe::SameHandle(current, Clock::graph);
    }

    bool SuspendRunningOriginal(const GakuMotion::ExactMethodMetadata& metadata)
    {
        using namespace LiveIndependentGraphProbe;
        auto& core = GakuMotion::Core::Instance();
        alignas(16) Handle current{};
        if (!Clock::SameOwner() || !core.liveTakeoverActive || !core.liveObservation.confirmed ||
            !core.liveObservation.paused || !Clock::verifiedOutputs ||
            Clock::Read(Clock::valid, true) != 1 || Clock::Read(Clock::mode) != 1 ||
            Clock::Read(Clock::playing, true) != 1 ||
            !Read(getOriginalGraph, reinterpret_cast<void*>(Clock::player), current) ||
            !SameHandle(current, Clock::graph)) return false;
        const auto scanned = Inspect(Clock::graphClass, current, metadata);
        if (!scanned.complete || scanned.campusJobs != 2 || scanned.motionClips != 0) return false;

        suspendedOriginal = current;
        originalSuspended = true;
        int manualMode = 3;
        void* args[]{&manualMode};
        const bool ok = Invoke(Clock::stop, current.data()) && Invoke(Clock::setMode, current.data(), args) && OriginalPaused();
        if (g_hostContext.logInfo)
            g_hostContext.logInfo("[GakuMotion][IndependentActorGraph] suspended_running_original=%d campus_jobs=%d",
                int(ok), static_cast<int>(scanned.campusJobs));
        return ok;
    }

    bool Fail(const char* message)
    {
        reason = message;
        return false;
    }

    // The graph can be torn down even after the old scene is gone. Never invoke
    // the actor or destroy the borrowed rig from this cleanup path.
    bool Release()
    {
        if (owned) {
            const int valid = State(Clock::valid);
            if (valid != 0) {
                // An invalidated Unity graph is already stopped and released.
                if (!GakuMotion::live_physics_clock::StopBeforeDestroy(
                    [&] { return State(Clock::playing); },
                    [&] { return Invoke(Clock::stop, graph.data()); },
                    [&] { return Invoke(destroy, graph.data()); })) return false;
            }
        }
        if (builderPin) StageLoader::freeHandle(builderPin);
        builderPin = nullptr;
        owned = confirmedSwing = false;
        graph = {};
        if (originalSuspended) {
            unsigned char valid{};
            if (!LiveIndependentGraphProbe::Read(Clock::valid, suspendedOriginal.data(), valid)) return false;
            if (valid) {
                int gameTime = 1;
                void* args[]{&gameTime};
                unsigned char playing{};
                int mode{};
                if (!Invoke(Clock::setMode, suspendedOriginal.data(), args) ||
                    !Invoke(Clock::play, suspendedOriginal.data()) ||
                    !LiveIndependentGraphProbe::Read(Clock::playing, suspendedOriginal.data(), playing) || !playing ||
                    !LiveIndependentGraphProbe::Read(Clock::mode, suspendedOriginal.data(), mode) || mode != gameTime)
                    return false;
            }
            restoredRunningOriginal = valid != 0;
            suspendedOriginal = {};
            originalSuspended = false;
        }
        return true;
    }

    bool RootTime(double& time)
    {
        if (!owned) return false;
        alignas(16) Handle root{};
        int index = 0;
        void* args[]{&index};
        return LiveIndependentGraphProbe::Read(rootAt, graph.data(), root, args) &&
            !LiveIndependentGraphProbe::SameHandle(root, {}) &&
            LiveIndependentGraphProbe::Read(nodeTime, root.data(), time) && std::isfinite(time);
    }

    bool Check(double now)
    {
        if (now < nextCheck) return true;
        nextCheck = now + 0.25;
        return OriginalPaused() && State(Clock::valid) == 1 && State(Clock::playing) == 1;
    }

    static bool RigInitialized(void* rig)
    {
        for (void* klass = g_faceRuntime.objectClass(rig); klass; klass = il2cpp_class_get_parent(klass)) {
            void* field = g_faceRuntime.findField(klass, "<isInitialized>k__BackingField");
            if (!field) continue;
            void* type = g_faceRuntime.fieldType(field);
            if (!type || g_faceRuntime.typeCode(type) != 0x02 || (g_faceRuntime.fieldFlags(field) & 0x10)) return false;
            unsigned char initialized{};
            g_faceRuntime.readField(rig, field, &initialized);
            return initialized == 1;
        }
        return false;
    }

    bool VerifyOutputs(void* animator, const Handle& job, const GakuMotion::ExactMethodMetadata& metadata)
    {
        using namespace LiveIndependentGraphProbe;
        void* output = PlayableClass("PlayableOutput");
        void* outputHandle = PlayableClass("PlayableOutputHandle");
        void* handle = PlayableClass("PlayableHandle");
        void* animation = FindClass("UnityEngine.AnimationModule.dll", "UnityEngine.Animations", "AnimationPlayableOutput");
        uint32_t align{};
        if (!output || !outputHandle || !handle || !animation || StageLoader::valueSize(output, &align) != 16 ||
            StageLoader::valueSize(outputHandle, &align) != 16 || StageLoader::valueSize(handle, &align) != 16 ||
            StageLoader::valueSize(animation, &align) != 16) return false;
        const auto get = metadata.Find(Clock::graphClass, "GetOutputInternal", false, {0x02}, {{0x08}, {0x11, outputHandle, true}});
        const auto cast = metadata.Find(animation, "op_Explicit", true, {0x11, animation}, {{0x11, output}});
        const auto target = metadata.Find(animation, "GetTarget", false, {0x12, g_animatorKlass}, {});
        const auto source = metadata.Find(outputHandle, "GetSourcePlayable", false, {0x11, handle}, {});
        if (!get || !cast || !target || !source) return false;
        bool jobConnected = false;
        for (int index = 0; index < 32; ++index) {
            alignas(16) Handle value{}, typed{}, sourceHandle{};
            unsigned char present{};
            void* args[]{&index, value.data()};
            if (!Read(get, graph.data(), present, args)) return false;
            if (!present) return jobConnected;
            void* castArgs[]{value.data()};
            void* exception = nullptr;
            void* boxed = g_faceRuntime.invoke(cast, nullptr, castArgs, &exception);
            void* raw = boxed && !exception ? g_safeBones.unbox(boxed) : nullptr;
            if (!raw) return false;
            std::memcpy(typed.data(), raw, typed.size());
            if (Call(target, typed.data()) != animator || !Read(source, value.data(), sourceHandle)) return false;
            jobConnected |= SameHandle(sourceHandle, job);
        }
        return false;
    }

    bool Start(void* actor, double now)
    {
        using namespace LiveIndependentGraphProbe;
        reason = nullptr;
        originalRigChanged = false;
        restoredRunningOriginal = false;
        auto metadata = GroundShadowProbeBootstrap::Resolve(g_gameAssembly).method;
        const auto objectNew = reinterpret_cast<void*(*)(void*)>(GetProcAddress(g_gameAssembly, "il2cpp_object_new"));
        void* playerBase = FindClass("vl-unity.Runtime.dll", "VL.Animation", "AnimationPlayerBase");
        void* componentClass = FindClass("campus-submodule.Runtime.dll", "Campus.Common", "CampusActorAnimation");
        void* rigClass = FindClass("campus-submodule.Runtime.dll", "ActorAnimation", "CampusActorAnimationRig");
        void* builderClass = FindClass("campus-submodule.Runtime.dll", "ActorAnimation", "CampusActorAnimationBuilder");
        void* initClass = FindClass("campus-submodule.Runtime.dll", "ActorAnimation", "CampusActorAnimationInitializeData");
        void* scriptClass = FindClass("UnityEngine.AnimationModule.dll", "UnityEngine.Animations", "AnimationScriptPlayable");
        void* handleClass = PlayableClass("PlayableHandle");
        if (owned || originalSuspended || builderPin || !objectNew || !componentClass || !rigClass || !builderClass || !initClass ||
            !scriptClass || !handleClass || !StageLoader::newHandle || !StageLoader::freeHandle ||
            !StageLoader::valueSize || !g_getComponentInChildrenNative || !Clock::graphClass) return Fail("native_types");
        uint32_t align{};
        if (StageLoader::valueSize(scriptClass, &align) != 16 || StageLoader::valueSize(handleClass, &align) != 16)
            return Fail("handle_size");
        getOriginalGraph = metadata.Find(playerBase, "GetPlayableGraph", false, {0x11, Clock::graphClass}, {});
        void* classType = il2cpp_class_get_type(componentClass);
        void* type = classType ? il2cpp_type_get_object(classType) : nullptr;
        void* component = type ? g_getComponentInChildrenNative(actor, type) : nullptr;
        void* animator = HelperGetActorAnimator(actor);
        void* rig = ReferenceField(component, "_actorAnimationRig");
        void* initData = ReferenceField(component, "_initializeData");
        void* stockBuilder = ReferenceField(component, "_builder");
        void* job = ReferenceField(rig, "_job");
        void* campusJob = FindClass("campus-submodule.Runtime.dll", "ActorAnimation", "CampusActorAnimationJob");
        if (!animator || !rig || !initData || !stockBuilder || !job || !campusJob ||
            g_faceRuntime.objectClass(rig) != rigClass || g_faceRuntime.objectClass(initData) != initClass ||
            g_faceRuntime.objectClass(stockBuilder) != builderClass || g_faceRuntime.objectClass(job) != campusJob ||
            ReferenceField(component, "_animationPlayer") != reinterpret_cast<void*>(Clock::player) ||
            ReferenceField(rig, "_animator") != animator || !RigInitialized(rig)) return Fail("actor_rig_ownership");

        if (!OriginalPaused() && !SuspendRunningOriginal(metadata)) return Fail("original_graph_not_paused");

        const auto create = metadata.Find(Clock::graphClass, "Create", true, {0x11, Clock::graphClass}, {});
        destroy = metadata.Find(Clock::graphClass, "Destroy", false, {0x01}, {});
        const auto ctor = metadata.Find(builderClass, ".ctor", false, {0x01}, {});
        const auto build = metadata.Find(builderClass, "Build", false, {0x01},
            {{0x12, g_animatorKlass}, {0x11, Clock::graphClass}, {0x12, rigClass}});
        const auto playable = metadata.Find(builderClass, "get_playable", false, {0x11, scriptClass}, {});
        const auto getHandle = metadata.Find(scriptClass, "GetHandle", false, {0x11, handleClass}, {});
        rootAt = metadata.Find(Clock::graphClass, "GetRootPlayableInternal", false, {0x11, handleClass}, {{0x08}});
        nodeTime = metadata.Find(handleClass, "GetTime", false, {0x0d}, {});
        if (!create || !destroy || !ctor || !build || !playable || !getHandle) return Fail("graph_methods");
        void* exception = nullptr;
        void* box = g_faceRuntime.invoke(create, nullptr, nullptr, &exception);
        void* raw = box && !exception ? g_safeBones.unbox(box) : nullptr;
        if (!raw) return Fail("create");
        std::memcpy(graph.data(), raw, graph.size());
        if (SameHandle(graph, Clock::graph) || SameHandle(graph, {})) return Fail("graph_handle_alias");
        owned = true;
        if (State(Clock::valid) != 1 || State(Clock::playing) != 0) return Fail("new_graph_state");
        void* builder = objectNew(builderClass);
        builderPin = builder ? StageLoader::newHandle(builder, false) : nullptr;
        if (!builderPin || !Invoke(ctor, builder)) return Fail("builder");
        void* args[]{animator, graph.data(), rig};
        const bool built = Invoke(build, builder, args);
        originalRigChanged = ReferenceField(component, "_actorAnimationRig") != rig ||
            ReferenceField(rig, "_animator") != animator || ReferenceField(rig, "_job") != job || !RigInitialized(rig);
        if (!built || originalRigChanged || !OriginalPaused())
            return Fail("build_or_rig_changed");
        alignas(16) Handle script{}, jobHandle{};
        if (!Read(playable, builder, script) || !Read(getHandle, script.data(), jobHandle) || SameHandle(jobHandle, {}) ||
            !VerifyOutputs(animator, jobHandle, metadata)) return Fail("job_output_binding");
        const auto scanned = Inspect(Clock::graphClass, graph, metadata);
        if (!scanned.complete || scanned.campusJobs != 1 || scanned.motionClips != 0) return Fail("graph_contents");
        int gameTime = 1;
        void* modeArgs[]{&gameTime};
        if (!Invoke(Clock::setMode, graph.data(), modeArgs) || !OriginalPaused() ||
            !Invoke(Clock::play, graph.data()) || State(Clock::playing) != 1) return Fail("graph_play");
        nextCheck = now;
        return true;
    }
};
