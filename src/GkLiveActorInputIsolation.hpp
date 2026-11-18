#pragma once

// Included in the plugin namespace. Touch only the verified actor's physics
// job inputs, so the job itself still receives a live Unity AnimationStream.
class LiveActorInputIsolation
{
    struct alignas(16) Handle {
        std::array<unsigned char, 16> data{};
        bool operator==(const Handle& other) const
        {
            // A Unity handle is a pointer and version. Ignore trailing padding.
            return std::memcmp(data.data(), other.data.data(), sizeof(uintptr_t) + sizeof(uint32_t)) == 0;
        }
    };
    struct Input {
        Handle job, source;
        int port{};
        int sourcePort{-1};
        float weight{};
        bool changed{};
    };
    struct OutputRoute {
        Handle original, physics;
        int originalPort{}, physicsPort{};
        bool active{}, changed{};
    } route;

    const void *getOutput{}, *getSource{}, *isNull{}, *valid{}, *getType{}, *getJobType{},
        *countInputs{}, *getInput{}, *countOutputs{}, *getOutputHandle{}, *connect{}, *disconnect{},
        *readWeight{}, *writeWeight{}, *getSourcePort{}, *setOutputSource{};
    using ClassFromSystemType = void*(*)(void*);
    ClassFromSystemType classFromType{};
    void *scriptClass{}, *physicsClass{}, *mixerClass{}, *layerMixerClass{};
    std::vector<Handle> outputs, jobs;
    std::vector<Input> inputs;
    double nextCheck{};

    static bool Unavailable(const char* reason)
    {
        if (g_hostContext.logInfo) g_hostContext.logInfo("[GakuMotion][LiveActorIsolation] unavailable=%s", reason);
        return false;
    }

    static void* Call(const void* method, void* self, void** args = nullptr)
    {
        void* ex = nullptr;
        void* result = g_faceRuntime.invoke(method, self, args, &ex);
        return ex ? nullptr : result;
    }
    template<class T>
    static bool Read(const void* method, void* self, T& value, void** args = nullptr)
    {
        void* boxed = Call(method, self, args);
        void* raw = boxed ? g_safeBones.unbox(boxed) : nullptr;
        if (!raw) return false;
        std::memcpy(&value, raw, sizeof(T));
        return true;
    }

    bool ReadNull(Handle& handle, unsigned char& empty) const
    {
        if (isNull) return Read(isNull, handle.data.data(), empty);
        empty = handle == Handle{};
        return true;
    }

    bool ReadOutput(void* graph, int index, Handle& output) const
    {
        unsigned char found{};
        void* args[]{&index, output.data.data()};
        return Read(getOutput, graph, found, args) && found;
    }

    bool OutputSource(void* graph, int index, Handle& source) const
    {
        Handle output;
        return ReadOutput(graph, index, output) && Read(getSource, output.data.data(), source);
    }

    bool ReadRoute(void* graph, Handle& source, int& port) const
    {
        Handle output;
        return ReadOutput(graph, 0, output) && Read(getSource, output.data.data(), source) &&
            Read(getSourcePort, output.data.data(), port);
    }

    bool WriteRoute(void* graph, Handle& source, int port) const
    {
        Handle output;
        if (!ReadOutput(graph, 0, output)) return false;
        void* args[]{source.data.data(), &port};
        void* ex = nullptr;
        g_faceRuntime.invoke(setOutputSource, output.data.data(), args, &ex);
        Handle actual;
        int actualPort{};
        return !ex && ReadRoute(graph, actual, actualPort) && actual == source && actualPort == port;
    }

    bool NodeType(Handle& node, void*& type) const
    {
        unsigned char present{};
        if (!Read(valid, node.data.data(), present) || !present) return false;
        void* managedType = Call(getType, node.data.data());
        type = managedType ? classFromType(managedType) : nullptr;
        return type != nullptr;
    }

    bool FindJobs(Handle node, unsigned depth, unsigned& visited, std::vector<Handle>& result)
    {
        if (depth > 12 || ++visited > 128) return false;
        void* type{};
        if (!NodeType(node, type)) return false;
        if (type == scriptClass) {
            void* managedType = Call(getJobType, node.data.data());
            if (!managedType || classFromType(managedType) != physicsClass) return false;
            if (std::find(result.begin(), result.end(), node) == result.end()) result.push_back(node);
            return true;
        }
        // Only pure animation mixers may relay the job's output. Any other
        // connected branch could still write the original Live animation.
        if (type != mixerClass && type != layerMixerClass) return false;
        int count{};
        if (!Read(countInputs, node.data.data(), count) || count <= 0 || count > 32) return false;
        bool connected = false;
        for (int index = 0; index < count; ++index) {
            Handle child;
            unsigned char empty{};
            void* args[]{&index};
            if (!Read(getInput, node.data.data(), child, args) || !ReadNull(child, empty)) return false;
            if (!empty) {
                if (!FindJobs(child, depth + 1, visited, result)) return false;
                connected = true;
            }
        }
        return connected;
    }

    bool Weight(Input& input, float& value) const
    {
        void* args[]{&input.port};
        return Read(readWeight, input.job.data.data(), value, args) && std::isfinite(value);
    }
    bool SetWeight(Input& input, float weight) const
    {
        void* args[]{&input.port, &weight};
        void* ex = nullptr;
        g_faceRuntime.invoke(writeWeight, input.job.data.data(), args, &ex);
        float actual{};
        return !ex && Weight(input, actual) && actual == weight;
    }
    bool InspectInput(Input& input, Handle& source, unsigned char& empty) const
    {
        unsigned char current{};
        int count{};
        void* args[]{&input.port};
        auto* job = input.job.data.data();
        return Read(valid, job, current) && current && Read(countInputs, job, count) &&
            input.port < count && Read(getInput, job, source, args) &&
            ReadNull(source, empty);
    }

    bool FindSourcePort(Input& input) const
    {
        unsigned char present{};
        auto* source = input.source.data.data();
        if (!Read(valid, source, present) || !present) return false;
        int count = 1;
        if (countOutputs && (!Read(countOutputs, source, count) || count <= 0 || count > 32)) return false;
        for (int port = 0; port < count; ++port) {
            Handle destination;
            void* args[]{&port};
            if (!Read(getOutputHandle, source, destination, args)) return false;
            if (!(destination == input.job)) continue;
            if (input.sourcePort >= 0) return false; // Ambiguous connection.
            input.sourcePort = port;
        }
        return input.sourcePort >= 0;
    }

    bool FindPhysicsOutput(Handle source, const Handle& physics, int& port) const
    {
        std::vector<Handle> pending{source};
        for (size_t index = 0; index < pending.size(); ++index) {
            Handle node = pending[index];
            unsigned char present{};
            int count{};
            if (!Read(valid, node.data.data(), present) || !present ||
                !Read(countInputs, node.data.data(), count) || count < 0 || count > 32) return false;
            for (int inputPort = 0; inputPort < count; ++inputPort) {
                Handle child;
                unsigned char empty{};
                void* args[]{&inputPort};
                if (!Read(getInput, node.data.data(), child, args) || !ReadNull(child, empty)) return false;
                if (empty) continue;
                if (child == physics) {
                    Input edge{node, child, inputPort};
                    if (!FindSourcePort(edge)) return false;
                    port = edge.sourcePort;
                    return true;
                }
                if (std::find(pending.begin(), pending.end(), child) != pending.end()) continue;
                if (pending.size() >= 128) return false;
                pending.push_back(child);
            }
        }
        return false;
    }

    bool PreparePhysicsOutput(void* graph, Handle physics)
    {
        if (!getSourcePort || !setOutputSource) return false;
        void* type = nullptr;
        if (!NodeType(physics, type) || type != scriptClass) return false;
        Handle original;
        int originalPort{};
        if (!ReadRoute(graph, original, originalPort) || originalPort < 0) return false;
        int physicsPort = originalPort;
        if (!(physics == original) && !FindPhysicsOutput(original, physics, physicsPort)) return false;
        route = {original, physics, originalPort, physicsPort, true, false};
        outputs.push_back(original);
        jobs.push_back(physics);
        return true;
    }

    bool PlanInputConnections()
    {
        for (auto& input : inputs) {
            unsigned char empty{};
            if (!ReadNull(input.source, empty)) return Unavailable("physics_job_input_handle");
            if (empty) continue;
            // Source output enumeration cannot distinguish two connections
            // from one playable to different inputs of the same job.
            const auto connections = std::count_if(inputs.begin(), inputs.end(), [&](const Input& other) {
                return other.job == input.job && other.source == input.source;
            });
            if (connections != 1) return Unavailable("physics_job_source_port_ambiguous");
            if (!FindSourcePort(input)) return Unavailable("physics_job_source_port");
            if (!Weight(input, input.weight)) return Unavailable("physics_job_input_weight");
        }
        return true;
    }

    bool RestoreInput(void* graph, Input& input) const
    {
        Handle source;
        unsigned char empty{}, present{}, connected{};
        if (!InspectInput(input, source, empty)) return false;
        if (empty) {
            if (!Read(valid, input.source.data.data(), present) || !present) return false;
            void* args[]{input.source.data.data(), &input.sourcePort, input.job.data.data(), &input.port};
            if (!Read(connect, graph, connected, args) || !connected ||
                !InspectInput(input, source, empty)) return false;
        }
        return !empty && source == input.source && SetWeight(input, input.weight);
    }

public:
    bool Prepare(void* graphClass, void* graph, unsigned actorOutputCount, const void* actorPhysics)
    {
        *this = {};
        const auto metadata = GroundShadowProbeBootstrap::Resolve(g_gameAssembly).method;
        auto playableClass = [](const char* name) {
            void* result = FindClass("UnityEngine.DirectorModule.dll", "UnityEngine.Playables", name);
            return result ? result : FindClass("UnityEngine.CoreModule.dll", "UnityEngine.Playables", name);
        };
        auto animationClass = [](const char* name) {
            return FindClass("UnityEngine.AnimationModule.dll", "UnityEngine.Animations", name);
        };
        void* handle = playableClass("PlayableHandle");
        void* output = playableClass("PlayableOutputHandle");
        scriptClass = animationClass("AnimationScriptPlayable");
        mixerClass = animationClass("AnimationMixerPlayable");
        layerMixerClass = animationClass("AnimationLayerMixerPlayable");
        physicsClass = FindClass("campus-submodule.Runtime.dll", "ActorAnimation", "CampusActorAnimationJob");
        using ValueSize = int(*)(void*, uint32_t*);
        const auto size = reinterpret_cast<ValueSize>(GetProcAddress(g_gameAssembly, "il2cpp_class_value_size"));
        classFromType = reinterpret_cast<ClassFromSystemType>(GetProcAddress(g_gameAssembly, "il2cpp_class_from_system_type"));
        uint32_t alignment{};
        if (!handle || !output || !scriptClass || !physicsClass || !size || !classFromType ||
            size(handle, &alignment) != sizeof(Handle) || size(output, &alignment) != sizeof(Handle))
            return Unavailable("playable_handle_classes_or_size");

        getOutput = metadata.Find(graphClass, "GetOutputInternal", false, {0x02}, {{0x08}, {0x11, output, true}});
        getSource = metadata.Find(output, "GetSourcePlayable", false, {0x11, handle}, {});
        getSourcePort = metadata.Find(output, "GetSourceOutputPort", false, {0x08}, {});
        setOutputSource = metadata.Find(output, "SetSourcePlayable", false, {0x01}, {{0x11, handle}, {0x08}});
        isNull = metadata.Find(handle, "IsNull", false, {0x02}, {});
        valid = metadata.Find(handle, "IsValid", false, {0x02}, {});
        getType = metadata.Find(handle, "GetPlayableType", false, {0x12}, {});
        getJobType = metadata.Find(handle, "GetJobType", false, {0x12}, {});
        countInputs = metadata.Find(handle, "GetInputCount", false, {0x08}, {});
        getInput = metadata.Find(handle, "GetInputHandle", false, {0x11, handle}, {{0x08}});
        countOutputs = metadata.Find(handle, "GetOutputCount", false, {0x08}, {});
        getOutputHandle = metadata.Find(handle, "GetOutputHandle", false, {0x11, handle}, {{0x08}});
        connect = metadata.Find(graphClass, "ConnectInternal", false, {0x02},
            {{0x11, handle}, {0x08}, {0x11, handle}, {0x08}});
        disconnect = metadata.Find(graphClass, "DisconnectInternal", false, {0x01}, {{0x11, handle}, {0x08}});
        readWeight = metadata.Find(handle, "GetInputWeightFromIndex", false, {0x0c}, {{0x08}});
        writeWeight = metadata.Find(handle, "SetInputWeightFromIndex", false, {0x01}, {{0x08}, {0x0c}});
        const std::pair<const char*, const void*> methods[]{
            {"GetOutputInternal", getOutput}, {"GetSourcePlayable", getSource},
            {"PlayableHandle.IsValid", valid},
            {"GetPlayableType", getType},
            {"GetInputCount", countInputs}, {"GetInputHandle", getInput},
            {"GetOutputHandle", getOutputHandle},
            {"ConnectInternal", connect}, {"DisconnectInternal", disconnect},
            {"GetInputWeightFromIndex", readWeight}, {"SetInputWeightFromIndex", writeWeight}
        };
        for (const auto& [name, method] : methods) {
            if (!method) return Unavailable(name);
        }
        if (!actorOutputCount) return Unavailable("no_actor_outputs");

        Handle physics;
        if (actorPhysics) std::memcpy(physics.data.data(), actorPhysics, sizeof(physics));
        if (actorOutputCount != 1 || physics == Handle{} || !PreparePhysicsOutput(graph, physics)) {
            if (!getJobType) return Unavailable("GetJobType");
            unsigned visited = 0;
            for (unsigned index = 0; index < actorOutputCount; ++index) {
                Handle source;
                if (!OutputSource(graph, static_cast<int>(index), source)) return Unavailable("actor_output_source");
                if (!FindJobs(source, 0, visited, jobs)) return Unavailable("actor_output_job_chain");
                outputs.push_back(source);
            }
        }
        for (auto& job : jobs) {
            int count{};
            if (!Read(countInputs, job.data.data(), count) || count < 0 || count > 32)
                return Unavailable("physics_job_input_count");
            for (int port = 0; port < count; ++port) {
                Input input{job, {}, port};
                unsigned char empty{};
                if (!InspectInput(input, input.source, empty)) return Unavailable("physics_job_input_handle");
                inputs.push_back(input);
            }
        }
        // Current game builds omit PlayableHandle.GetOutputCount. In that
        // case verify port zero in the reverse direction and only accept a
        // unique incoming connection from that source. No other port is guessed.
        return !jobs.empty() && PlanInputConnections();
    }

    bool Detach(void* graph, double now)
    {
        nextCheck = now + 0.25;
        for (auto& input : inputs) {
            if (input.sourcePort < 0) continue;
            // A script playable can process a weight-zero input. Disconnect the
            // verified edge itself, including inputs already at zero weight.
            input.changed = true;
            void* args[]{input.job.data.data(), &input.port};
            void* ex = nullptr;
            g_faceRuntime.invoke(disconnect, graph, args, &ex);
            Handle source;
            unsigned char empty{};
            if (ex || !InspectInput(input, source, empty) || !empty) return false;
        }
        if (route.active && !(route.original == route.physics)) {
            route.changed = true;
            if (!WriteRoute(graph, route.physics, route.physicsPort)) return false;
        }
        return true;
    }

    bool Check(void* graph, double now)
    {
        if (now < nextCheck) return true;
        nextCheck = now + 0.25;
        if (route.active) {
            Handle source;
            int port{};
            if (!ReadRoute(graph, source, port) || !(source == route.physics) || port != route.physicsPort)
                return false;
        } else {
            unsigned visited = 0;
            std::vector<Handle> currentJobs;
            currentJobs.reserve(jobs.size());
            for (size_t index = 0; index < outputs.size(); ++index) {
                Handle source;
                if (!OutputSource(graph, static_cast<int>(index), source) || !(source == outputs[index]) ||
                    !FindJobs(source, 0, visited, currentJobs)) return false;
            }
            if (currentJobs != jobs) return false;
        }
        Handle extraOutput;
        unsigned char found{};
        int index = static_cast<int>(outputs.size());
        void* args[]{&index, extraOutput.data.data()};
        if (!Read(getOutput, graph, found, args) || found) return false;
        size_t portCount = 0;
        for (auto& job : jobs) {
            int count{};
            if (!Read(countInputs, job.data.data(), count) || count < 0) return false;
            portCount += count;
        }
        if (portCount != inputs.size()) return false;
        for (auto& input : inputs) {
            Handle source;
            unsigned char empty{};
            if (!InspectInput(input, source, empty) || !empty) return false;
        }
        return true;
    }

    bool Restore(void* graph)
    {
        bool restored = true;
        for (auto& input : inputs) {
            if (input.changed && !RestoreInput(graph, input)) restored = false;
        }
        if (route.changed) {
            Handle source;
            int port{};
            if (!ReadRoute(graph, source, port)) restored = false;
            else if (source == route.physics && port == route.physicsPort)
                restored = WriteRoute(graph, route.original, route.originalPort) && restored;
            else if (!(source == route.original) || port != route.originalPort) restored = false;
        }
        *this = {};
        return restored;
    }

    size_t JobCount() const { return jobs.size(); }
    bool BypassedPlaybackOutput() const { return route.changed; }
    size_t DetachedCount() const
    {
        return std::count_if(inputs.begin(), inputs.end(), [](const Input& input) { return input.changed; });
    }
};
