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
        float weight{};
        bool changed{};
    };

    const void *getOutput{}, *getSource{}, *isNull{}, *valid{}, *getType{}, *getJobType{},
        *countInputs{}, *getInput{}, *readWeight{}, *writeWeight{};
    using ClassFromSystemType = void*(*)(void*);
    ClassFromSystemType classFromType{};
    void *scriptClass{}, *physicsClass{}, *mixerClass{}, *layerMixerClass{};
    std::vector<Handle> outputs, jobs;
    std::vector<Input> inputs;
    double nextCheck{};

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

    bool OutputSource(void* graph, int index, Handle& source) const
    {
        Handle output;
        unsigned char found{};
        void* args[]{&index, output.data.data()};
        return Read(getOutput, graph, found, args) && found &&
            Read(getSource, output.data.data(), source);
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
            if (!Read(getInput, node.data.data(), child, args) ||
                !Read(isNull, child.data.data(), empty)) return false;
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
    bool SameInput(Input& input) const
    {
        unsigned char current{};
        int count{};
        Handle source;
        void* args[]{&input.port};
        auto* job = input.job.data.data();
        return Read(valid, job, current) && current && Read(countInputs, job, count) &&
            input.port < count && Read(getInput, job, source, args) && source == input.source;
    }

public:
    bool Prepare(void* graphClass, void* graph, unsigned actorOutputCount)
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
            return false;

        getOutput = metadata.Find(graphClass, "GetOutputInternal", false, {0x02}, {{0x08}, {0x11, output, true}});
        getSource = metadata.Find(output, "GetSourcePlayable", false, {0x11, handle}, {});
        isNull = metadata.Find(handle, "IsNull", false, {0x02}, {});
        valid = metadata.Find(handle, "IsValid", false, {0x02}, {});
        getType = metadata.Find(handle, "GetPlayableType", false, {0x12}, {});
        getJobType = metadata.Find(handle, "GetJobType", false, {0x12}, {});
        countInputs = metadata.Find(handle, "GetInputCount", false, {0x08}, {});
        getInput = metadata.Find(handle, "GetInputHandle", false, {0x11, handle}, {{0x08}});
        readWeight = metadata.Find(handle, "GetInputWeightFromIndex", false, {0x0c}, {{0x08}});
        writeWeight = metadata.Find(handle, "SetInputWeightFromIndex", false, {0x01}, {{0x08}, {0x0c}});
        if (!getOutput || !getSource || !isNull || !valid || !getType || !getJobType ||
            !countInputs || !getInput || !readWeight || !writeWeight || !actorOutputCount) return false;

        unsigned visited = 0;
        for (unsigned index = 0; index < actorOutputCount; ++index) {
            Handle source;
            if (!OutputSource(graph, static_cast<int>(index), source) || !FindJobs(source, 0, visited, jobs))
                return false;
            outputs.push_back(source);
        }
        for (auto& job : jobs) {
            int count{};
            if (!Read(countInputs, job.data.data(), count) || count < 0 || count > 32) return false;
            for (int port = 0; port < count; ++port) {
                Input input{job, {}, port};
                void* args[]{&port};
                if (!Read(getInput, job.data.data(), input.source, args) || !Weight(input, input.weight))
                    return false;
                inputs.push_back(input);
            }
        }
        return !jobs.empty();
    }

    bool Mute(double now)
    {
        nextCheck = now + 0.25;
        for (auto& input : inputs) {
            if (input.weight == 0.0f) continue;
            // Mark before mutating so a failed read-back can still be undone.
            input.changed = true;
            if (!SetWeight(input, 0.0f)) return false;
        }
        return true;
    }

    bool Check(void* graph, double now)
    {
        if (now < nextCheck) return true;
        nextCheck = now + 0.25;
        unsigned visited = 0;
        std::vector<Handle> currentJobs;
        currentJobs.reserve(jobs.size());
        for (size_t index = 0; index < outputs.size(); ++index) {
            Handle source;
            if (!OutputSource(graph, static_cast<int>(index), source) || !(source == outputs[index]) ||
                !FindJobs(source, 0, visited, currentJobs)) return false;
        }
        if (currentJobs != jobs) return false;
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
            float weight{};
            if (!SameInput(input) || !Weight(input, weight) || weight != 0.0f) return false;
        }
        return true;
    }

    bool Restore()
    {
        bool restored = true;
        for (auto& input : inputs) {
            if (input.changed && (!SameInput(input) || !SetWeight(input, input.weight))) restored = false;
        }
        *this = {};
        return restored;
    }

    size_t JobCount() const { return jobs.size(); }
    size_t InputCount() const { return inputs.size(); }
};
