#pragma once
// Unity thread only. Capture the command and two sides of the existing body write.
// BakeMesh targets a private temporary Mesh and never replaces the renderer's mesh.
namespace LiveSleeveProbe
{
    using namespace ShoeMeshProbe;

    struct VertexSample { int index{}, arm{}; };
    struct MeshSamples
    {
        void* renderer{};
        void* mesh{};
        int vertexCount{};
        std::vector<VertexSample> vertices;
        std::vector<int> weightedBones;
    };
    struct ActorSamples
    {
        uint64_t token{}, generation{};
        bool baseline{};
        int beforeFrame{-1}, afterFrame{-1};
        std::vector<MeshSamples> meshes;
    };

    inline std::array<ActorSamples, 3> actors;
    struct OpeningSamples
    {
        ActorSamples mesh;
        unsigned phases{};
        bool bindingPending{};
    };
    inline std::unordered_map<uint64_t, OpeningSamples> opening;
    inline uintptr_t openingOwner{};
    inline int openingScene{};
    inline uint64_t session{}, epoch{}, event{}, nextEvent{}, nextSample{};
    inline int frame{-1}, eventFrame{-1}, remaining{};
    inline const char* action{};
    inline bool commandSample{}, resolved{}, faulted{};
    inline std::chrono::steady_clock::time_point lastDrag{};
    inline void* weightClass{};
    inline void* weightFields[8]{};

    inline bool Enabled(const GakuMotion::Core& core)
    {
        return !faulted && g_hostContext.logInfo && core.liveOwnerToken && core.liveViewingConfirmed &&
            core.liveObservation.confirmed && !core.liveTakeoverFault && !core.ensembleRestoreUnconfirmed &&
            core.config.secondaryPhysicsBackend == 2 &&
            !core.captureSuspended && core.injectionPolicy.Read().captureDepth == 0 &&
            GakuMotion::capture::LiveAutoPhotoIdle() && ProtectionOnUnityThread();
    }

    struct Arm
    {
        void* start{};
        void* end{};
        UnityVector3 a{}, b{};
        Matrix worldToArm{};
        bool valid{};
    };

    inline void ResolveWeights()
    {
        weightClass = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "BoneWeight");
        if (!weightClass || !getBoneWeights || !g_faceRuntime.findField || !g_faceRuntime.fieldType ||
            !g_faceRuntime.typeCode || !g_faceRuntime.fieldFlags || !g_faceRuntime.readField)
        {
            weightClass = nullptr;
            return;
        }
        for (int i = 0; i < 8; ++i)
        {
            const std::string name = (i < 4 ? "m_BoneIndex" : "m_Weight") + std::to_string(i % 4);
            auto* field = g_faceRuntime.findField(weightClass, name.c_str());
            auto* type = field ? g_faceRuntime.fieldType(field) : nullptr;
            if (!type || g_faceRuntime.typeCode(type) != (i < 4 ? 0x08 : 0x0c) ||
                (g_faceRuntime.fieldFlags(field) & 0x10))
            {
                weightClass = nullptr;
                return;
            }
            weightFields[i] = field;
        }
    }

    inline void LogBone(uint64_t seq, void* bone, const char* label, int rendererIndex, int boneIndex)
    {
        if (!bone) return;
        void* parent = g_getParentNative(bone);
        float local[3]{}, world[3]{}, localRot[4]{}, worldRot[4]{};
        HelperGetLocalPos(bone, local);
        HelperGetWorldPos(bone, world);
        HelperGetLocalRot(bone, localRot);
        HelperGetWorldRot(bone, worldRot);
        g_hostContext.logInfo(
            "[GakuMotion][LiveSleeveBone] sample=%llu renderer_index=%d bone_index=%d name=%s bone=%p parent=%p local=%.6f,%.6f,%.6f q=%.6f,%.6f,%.6f,%.6f world=%.6f,%.6f,%.6f wq=%.6f,%.6f,%.6f,%.6f",
            static_cast<unsigned long long>(seq), rendererIndex, boneIndex, label, bone, parent,
            local[0], local[1], local[2], localRot[0], localRot[1], localRot[2], localRot[3],
            world[0], world[1], world[2], worldRot[0], worldRot[1], worldRot[2], worldRot[3]);
    }

    inline Arm ReadArm(void* start, void* end)
    {
        Arm arm{start, end};
        if (!start || !end) return arm;
        float a[3]{}, b[3]{};
        HelperGetWorldPos(start, a);
        HelperGetWorldPos(end, b);
        arm.a = {a[0], a[1], a[2]};
        arm.b = {b[0], b[1], b[2]};
        std::string error;
        arm.valid = Distance(arm.a, arm.b) > 0.02f && ReadMatrix(getWorldToLocal, start, arm.worldToArm, error);
        return arm;
    }

    inline bool IsArmBone(std::string name)
    {
        for (char& c : name)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
        return name.find("forearm") != std::string::npos || name.find("upperarm") != std::string::npos ||
            name.find("sleeve") != std::string::npos || name.find("cuff") != std::string::npos ||
            name.find("hand_h") != std::string::npos;
    }

    inline bool ReadVertex(void* array, int index, UnityVector3& v)
    {
        std::string error;
        Pin boxed(Element(array, index, error));
        return error.empty() && ReadVector(boxed.Get(), v, error);
    }

    inline bool ReadWeights(void* weights, int index, int (&bones)[4], float (&values)[4])
    {
        if (!weights || !weightClass) return false;
        std::string error;
        Pin boxed(Element(weights, index, error));
        if (!boxed.Get() || !error.empty() || ObjectClass(boxed.Get()) != weightClass) return false;
        for (int i = 0; i < 4; ++i)
        {
            g_faceRuntime.readField(boxed.Get(), weightFields[i], &bones[i]);
            g_faceRuntime.readField(boxed.Get(), weightFields[i + 4], &values[i]);
            if (!std::isfinite(values[i]) || values[i] < 0.f || values[i] > 1.f) return false;
        }
        return true;
    }

    // Choose once from the original pose. Keep the same vertex indices when the sleeve moves.
    // Arms 0/1 are left upper/forearm, 2/3 right upper/forearm.
    inline std::vector<VertexSample> SelectVertices(void* vertices, int count, const Matrix& toWorld,
                                                     const std::array<Arm, 4>& arms)
    {
        constexpr int candidates = 768, bins = 6;
        struct Hit { int nearIndex{-1}, outerIndex{-1}; float nearest{0.12f}, outermost{}; };
        Hit hits[4][bins];
        const int steps = (std::min)(count, candidates);
        for (int step = 0; step < steps; ++step)
        {
            const int index = steps > 1 ? step * (count - 1) / (steps - 1) : 0;
            UnityVector3 local{};
            if (!ReadVertex(vertices, index, local)) continue;
            const auto p = Point(toWorld, local);
            for (int region = 0; region < static_cast<int>(arms.size()); ++region)
            {
                const auto& arm = arms[region];
                if (!arm.valid) continue;
                const float dx = arm.b.x - arm.a.x, dy = arm.b.y - arm.a.y, dz = arm.b.z - arm.a.z;
                const float t = ((p.x - arm.a.x) * dx + (p.y - arm.a.y) * dy + (p.z - arm.a.z) * dz) /
                    (dx * dx + dy * dy + dz * dz);
                if (t < 0.10f || t >= 0.95f) continue;
                const float distance = Distance(p, {arm.a.x + t * dx, arm.a.y + t * dy, arm.a.z + t * dz});
                auto& hit = hits[region][static_cast<int>(t * bins)];
                if (distance < hit.nearest) { hit.nearIndex = index; hit.nearest = distance; }
                if (distance > hit.outermost && distance < 0.12f) { hit.outerIndex = index; hit.outermost = distance; }
            }
        }
        std::vector<VertexSample> result;
        for (int region = 0; region < static_cast<int>(arms.size()); ++region)
            for (const auto& hit : hits[region])
            {
                if (hit.nearIndex >= 0) result.push_back({hit.nearIndex, region});
                if (hit.outerIndex >= 0 && hit.outerIndex != hit.nearIndex) result.push_back({hit.outerIndex, region});
            }
        return result;
    }

    inline const char* SampleRenderer(void* renderer, int rendererIndex, uint64_t seq, ActorSamples& cache,
                                      const std::array<Arm, 4>& arms)
    {
        std::string error;
        bool enabled{}, active{};
        Pin gameObject(Invoke(getGameObject, renderer, nullptr, error));
        if (!gameObject.Get() || !Primitive(getActive, gameObject.Get(), 0x02, &active, error) || !active ||
            !Primitive(getEnabled, renderer, 0x02, &enabled, error) || !enabled) return "inactive";
        Pin bones(Invoke(getBones, renderer, nullptr, error));
        const uintptr_t boneCount = ArrayLength(bones.Get());
        if (!boneCount || boneCount > 512) return "bones_unavailable";
        bool hasArm = false;
        for (int i = 0; i < static_cast<int>(boneCount); ++i)
        {
            Pin bone(Element(bones.Get(), i, error));
            if (!bone.Get()) continue;
            const auto name = Name(bone.Get());
            if (!IsArmBone(name) && std::none_of(arms.begin(), arms.end(), [&](const Arm& arm) {
                return arm.start == bone.Get() || arm.end == bone.Get();
            })) continue;
            hasArm = true;
            LogBone(seq, bone.Get(), name.c_str(), rendererIndex, i);
        }
        if (!hasArm) return "no_arm_bones";
        Pin shared(Invoke(getSharedMesh, renderer, nullptr, error));
        int count{};
        if (!shared.Get() || !Primitive(getVertexCount, shared.Get(), 0x08, &count, error) || count <= 0 || count > 80000)
            return "shared_mesh_unavailable";
        Pin transform(Invoke(getTransform, renderer, nullptr, error));
        Matrix toWorld;
        if (!transform.Get() || !ReadMatrix(getLocalToWorld, transform.Get(), toWorld, error)) return "renderer_transform_unavailable";
        Json scratchEvidence = Object();
        Scratch scratch(api.objectNew(meshClass), scratchEvidence);
        if (!scratch.mesh.Get()) return "scratch_allocation_failed";
        error.clear();
        Invoke(meshCtor, scratch.mesh.Get(), nullptr, error);
        if (!error.empty()) return "scratch_constructor_failed";
        bool useScale = false;
        void* args[] = {scratch.mesh.Get(), &useScale};
        Invoke(bakeMesh, renderer, args, error);
        if (!error.empty()) return "bake_failed";
        Pin vertices(Invoke(getVertices, scratch.mesh.Get(), nullptr, error));
        if (!vertices.Get() || ArrayLength(vertices.Get()) != static_cast<uintptr_t>(count)) return "vertex_count_mismatch";

        auto selected = std::find_if(cache.meshes.begin(), cache.meshes.end(), [&](const MeshSamples& mesh) {
            return mesh.renderer == renderer && mesh.mesh == shared.Get() && mesh.vertexCount == count;
        });
        const bool newIndices = selected == cache.meshes.end();
        if (newIndices)
        {
            if (cache.meshes.size() >= 24) return "sample_cache_limit";
            cache.meshes.push_back({renderer, shared.Get(), count, SelectVertices(vertices.Get(), count, toWorld, arms), {}});
            selected = std::prev(cache.meshes.end());
        }
        error.clear();
        Pin weights(newIndices && weightClass ? Invoke(getBoneWeights, shared.Get(), nullptr, error) : nullptr);
        const bool weightsAvailable = weights.Get() && ArrayLength(weights.Get()) == static_cast<uintptr_t>(count);
        const auto rendererName = Name(renderer);
        const auto meshName = Name(shared.Get());
        g_hostContext.logInfo("[GakuMotion][LiveSleeveMesh] sample=%llu renderer_index=%d renderer=%p name=%s mesh=%p mesh_name=%s vertices=%d chosen=%zu new_indices=%d weights=%s",
            static_cast<unsigned long long>(seq), rendererIndex, renderer, rendererName.c_str(), shared.Get(),
            meshName.c_str(), count, selected->vertices.size(), int(newIndices),
            newIndices ? (weightsAvailable ? "legacy4" : "unavailable") : "see_first_sample");

        for (const auto& vertex : selected->vertices)
        {
            const auto& arm = arms[vertex.arm];
            UnityVector3 local{};
            if (!ReadVertex(vertices.Get(), vertex.index, local)) continue;
            const auto world = Point(toWorld, local);
            const auto relative = arm.valid ? Point(arm.worldToArm, world) : UnityVector3{};
            int indices[4]{-1, -1, -1, -1};
            float values[4]{};
            const bool haveWeights = weightsAvailable && ReadWeights(weights.Get(), vertex.index, indices, values);
            if (haveWeights)
                for (int i = 0; i < 4; ++i)
                    if (values[i] > 0.f && indices[i] >= 0 && indices[i] < static_cast<int>(boneCount) &&
                        std::find(selected->weightedBones.begin(), selected->weightedBones.end(), indices[i]) ==
                            selected->weightedBones.end())
                        selected->weightedBones.push_back(indices[i]);
            g_hostContext.logInfo(
                "[GakuMotion][LiveSleeveVertex] sample=%llu renderer_index=%d vertex=%d arm=%d renderer_local=%.6f,%.6f,%.6f world=%.6f,%.6f,%.6f arm_valid=%d arm_local=%.6f,%.6f,%.6f weights_known=%d influences=%d:%.5f,%d:%.5f,%d:%.5f,%d:%.5f",
                static_cast<unsigned long long>(seq), rendererIndex, vertex.index, vertex.arm,
                local.x, local.y, local.z, world.x, world.y, world.z, int(arm.valid), relative.x, relative.y, relative.z,
                int(haveWeights), indices[0], values[0], indices[1], values[1], indices[2], values[2], indices[3], values[3]);
        }
        for (int index : selected->weightedBones)
        {
            error.clear();
            Pin bone(Element(bones.Get(), index, error));
            if (bone.Get()) LogBone(seq, bone.Get(), Name(bone.Get()).c_str(), rendererIndex, index);
        }
        return selected->vertices.empty() ? "no_arm_vertices_in_sample" : "ok";
    }

    inline void RecordMeshes(void* actor, uint64_t seq, ActorSamples& cache)
    {
        if (!resolved)
        {
            resolved = true;
            if (!available) Resolve(g_gameAssembly, "");
            if (available) ResolveWeights();
        }
        if (!available)
        {
            g_hostContext.logInfo("[GakuMotion][LiveSleeveMeshStatus] sample=%llu status=mesh_api_unavailable", static_cast<unsigned long long>(seq));
            return;
        }
        auto* type = api.classType(rendererClass);
        Pin managedType(type ? api.typeObject(type) : nullptr);
        bool includeInactive = false;
        void* args[] = {managedType.Get(), &includeInactive};
        std::string error;
        Pin renderers(managedType.Get() ? Invoke(getComponents, actor, args, error) : nullptr);
        const uintptr_t count = ArrayLength(renderers.Get());
        void* left = HelperGetBoneTransform(actor, 15);
        void* right = HelperGetBoneTransform(actor, 16);
        const std::array<Arm, 4> arms{
            ReadArm(HelperGetBoneTransform(actor, 13), left), ReadArm(left, HelperGetBoneTransform(actor, 17)),
            ReadArm(HelperGetBoneTransform(actor, 14), right), ReadArm(right, HelperGetBoneTransform(actor, 18))};
        const int limit = static_cast<int>((std::min)(count, uintptr_t{16}));
        int baked = 0;
        for (int i = 0; i < limit && baked < 4; ++i)
        {
            Pin renderer(Element(renderers.Get(), i, error));
            if (!renderer.Get()) continue;
            const char* result = SampleRenderer(renderer.Get(), i, seq, cache, arms);
            if (std::strcmp(result, "ok") == 0 || std::strcmp(result, "no_arm_vertices_in_sample") == 0) ++baked;
            g_hostContext.logInfo("[GakuMotion][LiveSleeveMeshStatus] sample=%llu renderer_index=%d renderer=%p status=%s",
                static_cast<unsigned long long>(seq), i, renderer.Get(), result);
        }
        g_hostContext.logInfo("[GakuMotion][LiveSleeveMeshStatus] sample=%llu enumerated=%zu baked=%d status=%s capped=%d",
            static_cast<unsigned long long>(seq), count, baked, renderers.Get() ? "complete" : "enumeration_failed",
            int(count > static_cast<uintptr_t>(limit) || baked == 4));
    }

    inline void GuardedMeshes(void* actor, uint64_t seq, ActorSamples* cache)
    {
        __try { RecordMeshes(actor, seq, *cache); }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            faulted = true;
            g_hostContext.logInfo("[GakuMotion][LiveSleeveMeshStatus] sample=%llu status=native_exception_probe_disabled",
                static_cast<unsigned long long>(seq));
        }
    }

    inline void RecordPose(void* actor, uint64_t seq, ActorSamples& cache, bool mesh)
    {
        if (!g_getParentNative || !g_getPosNative || !g_getRotNative || !g_getWorldPosNative ||
            !g_getWorldRotNative || !g_getNameNative) return;
        const int ids[]{0, 11, 12, 13, 14, 15, 16, 17, 18};
        for (int id : ids)
            if (auto* bone = HelperGetBoneTransform(actor, id))
                LogBone(seq, bone, HelperGetTransformName(bone).c_str(), -1, id);
        const char* aux[2][3]{{"LeftForeArm_Roll_H", "LeftForeArm_H", "LeftHand_H"},
                              {"RightForeArm_Roll_H", "RightForeArm_H", "RightHand_H"}};
        for (int side = 0; side < 2; ++side)
        {
            void* fore = HelperGetBoneTransform(actor, 15 + side);
            if (!fore) continue;
            for (const char* name : aux[side])
                LogBone(seq, LiveActorProps::UniqueChild(fore, name), name, -1, -1);
        }
        if (mesh) GuardedMeshes(actor, seq, &cache);
    }

    enum class OpeningPhase : unsigned
    {
        NativeBeforeTakeover, TakeoverBeforeMotion, PreviewBeforeBody, PreviewAfterActor,
        FirstPlayingBeforeBody, FirstPlayingAfterActor, BeforeBinding, BindingCommitted,
        BindingAfterBody, BindingAfterSecondary
    };
    inline void RecordOpening(GakuMotion::Core& core, void* actor, OpeningPhase phase, bool wroteBody = false)
    {
        if (!Enabled(core) || !actor) return;
        const auto* entry = core.sceneActorRegistry.Find(actor);
        if (!entry) return;
        if (openingOwner != core.liveOwnerToken || openingScene != core.liveOwnerScene)
        {
            openingOwner = core.liveOwnerToken;
            openingScene = core.liveOwnerScene;
            opening.clear();
        }
        const auto found = opening.find(entry->token);
        const unsigned bit = 1u << static_cast<unsigned>(phase);
        if ((found != opening.end() && (found->second.phases & bit)) ||
            (found == opening.end() && opening.size() >= 16)) return;
        if (!HelperGetBoneTransform(actor, 15) && !HelperGetBoneTransform(actor, 16)) return;
        auto& sample = opening[entry->token];
        sample.phases |= bit;

        const auto& followers = core.ensemble.Followers();
        size_t slot = core.IsTargetActor(actor) ? 1 : 0;
        int actual = slot && g_physicsActor == actor ? g_physicsActual : -1;
        for (size_t i = 0; i < followers.size(); ++i)
        {
            const auto& follower = followers[i];
            if (!follower.player || follower.actorToken != entry->token ||
                follower.player->controller.targetActor != actor) continue;
            slot = i + 2;
            if (i < g_followerPhysics.size() && g_followerPhysics[i].actor == actor &&
                g_followerPhysics[i].bindingGeneration == follower.bindingGeneration)
                actual = g_followerPhysics[i].actual;
            break;
        }
        const char* names[]{"native_before_takeover", "takeover_before_motion", "preview_before_body",
            "preview_after_actor", "first_playing_before_body", "first_playing_after_actor",
            "before_binding", "binding_committed", "binding_after_body", "binding_after_secondary"};
        const auto seq = ++nextSample;
        SYSTEMTIME wall{};
        GetLocalTime(&wall);
        g_hostContext.logInfo(
            "[GakuMotion][LiveSleeveStart] sample=%llu phase=%s frame=%d slot=%zu actor=%p token=%llu character=%s scene=%d session=%llu state=%d motion=%.4f live=%.4f live_paused=%d takeover=%d bound_followers=%zu physics_requested=%d actor_actual=%d before_quartz_requested=%d body_written=%d time=%04u-%02u-%02uT%02u:%02u:%02u.%03u",
            static_cast<unsigned long long>(seq), names[static_cast<unsigned>(phase)],
            g_faceUnityFrame ? g_faceUnityFrame() : frame, slot, actor,
            static_cast<unsigned long long>(entry->token), entry->character.c_str(), core.liveOwnerScene,
            static_cast<unsigned long long>(core.injectionSession), int(core.player.state), core.player.currentTime,
            core.liveObservation.time, int(core.liveObservation.paused), int(core.liveTakeoverActive),
            static_cast<size_t>(std::count_if(followers.begin(), followers.end(), [](const auto& f) { return f.player != nullptr; })),
            core.config.secondaryPhysicsBackend, actual, int(core.config.beforeQuartzExperiment), int(wroteBody),
            wall.wYear, wall.wMonth, wall.wDay, wall.wHour, wall.wMinute, wall.wSecond, wall.wMilliseconds);
        RecordPose(actor, seq, sample.mesh, true);
    }

    inline void BeforeTakeover(GakuMotion::Core& core)
    {
        for (const auto& entry : core.sceneActorRegistry.Entries())
            RecordOpening(core, entry.actor, OpeningPhase::NativeBeforeTakeover);
    }

    inline void Binding(GakuMotion::Core& core, void* actor, bool before)
    {
        if (!Enabled(core) || !core.liveTakeoverActive) return;
        const auto* entry = core.sceneActorRegistry.Find(actor);
        if (!entry) return;
        if (before && openingOwner == core.liveOwnerToken && openingScene == core.liveOwnerScene)
        {
            const auto found = opening.find(entry->token);
            if (found != opening.end())
            {
                constexpr unsigned bindingMask = (1u << static_cast<unsigned>(OpeningPhase::BeforeBinding)) - 1;
                found->second.phases &= bindingMask;
                found->second.bindingPending = false;
            }
        }
        RecordOpening(core, actor, before ? OpeningPhase::BeforeBinding : OpeningPhase::BindingCommitted);
        if (!before)
            if (const auto found = opening.find(entry->token); found != opening.end())
                found->second.bindingPending = true;
    }

    inline void BindingPose(GakuMotion::Core& core, void* actor, bool afterSecondary)
    {
        if (openingOwner != core.liveOwnerToken || openingScene != core.liveOwnerScene || !core.liveTakeoverActive) return;
        const auto* entry = core.sceneActorRegistry.Find(actor);
        if (!entry) return;
        const auto found = opening.find(entry->token);
        if (found == opening.end() || !found->second.bindingPending) return;
        RecordOpening(core, actor, afterSecondary ? OpeningPhase::BindingAfterSecondary : OpeningPhase::BindingAfterBody, true);
        if (afterSecondary) found->second.bindingPending = false;
    }

    inline void ObserveOpening(GakuMotion::Core& core, void* actor, bool afterBody, bool wroteBody)
    {
        using State = GakuMotion::PlaybackState;
        if (!core.liveTakeoverActive) return;
        switch (core.player.state)
        {
        case State::Stopped:
            if (!afterBody) RecordOpening(core, actor, OpeningPhase::TakeoverBeforeMotion);
            break;
        case State::Preview:
            RecordOpening(core, actor, afterBody ? OpeningPhase::PreviewAfterActor : OpeningPhase::PreviewBeforeBody, wroteBody);
            break;
        case State::Playing:
            RecordOpening(core, actor, afterBody ? OpeningPhase::FirstPlayingAfterActor : OpeningPhase::FirstPlayingBeforeBody, wroteBody);
            break;
        default: break;
        }
    }

    struct TimelineRole
    {
        uint64_t token{}, generation{};
        size_t slot{};
        const GakuMotion::MotionPlayer* player{};
        int actual{-1}, physicsFrame{-1};
        const GbbStatus* status{};
        const GakuMotion::FallbackPhysics* fallback{};
    };
    struct TimelineActor
    {
        uint64_t token{}, generation{};
        int bodyFrame{-1}, nativeFrame{-1};
        bool fallbackSeen{}, nativeWaitSeen{}, pendingBodySeen{}, nativeSeen{}, settledSeen{};
    };
    struct TimelineCapture
    {
        uint64_t id{}, session{}, epoch{};
        uintptr_t owner{};
        const char* edit{};
        float from{}, target{};
        std::chrono::steady_clock::time_point expires;
        std::array<TimelineActor, 4> roles{};
    };
    inline TimelineCapture timeline;
    inline bool scrubBackwardCaptured{};

    inline TimelineRole ReadTimelineRole(const GakuMotion::Core& core, void* actor)
    {
        const auto* entry = core.sceneActorRegistry.Find(actor);
        if (!entry) return {};
        if (core.IsTargetActor(actor))
        {
            TimelineRole role{entry->token, 0, 1, &core.player};
            if (g_physicsActor == actor)
            {
                role.actual = g_physicsActual;
                role.physicsFrame = g_physicsFrame;
                role.status = &g_burstBridge.status;
                role.fallback = &g_fallbackPhysics;
            }
            return role;
        }
        const auto& followers = core.ensemble.Followers();
        for (size_t i = 0; i < followers.size() && i + 1 < timeline.roles.size(); ++i)
        {
            const auto& follower = followers[i];
            if (!follower.player || follower.actorToken != entry->token ||
                follower.player->controller.targetActor != actor) continue;
            TimelineRole role{entry->token, follower.bindingGeneration, i + 2, follower.player.get()};
            if (i < g_followerPhysics.size() && g_followerPhysics[i].actor == actor &&
                g_followerPhysics[i].bindingGeneration == follower.bindingGeneration)
            {
                role.actual = g_followerPhysics[i].actual;
                role.physicsFrame = g_followerPhysics[i].frame;
                role.status = &g_followerBridge.State(i).status;
                role.fallback = &g_followerPhysics[i].fallback;
            }
            return role;
        }
        return {};
    }

    inline void RecordTimeline(GakuMotion::Core& core, void* actor, const TimelineRole& role, const char* phase)
    {
        if (!Enabled(core) || !role.player) return;
        if (openingOwner != core.liveOwnerToken || openingScene != core.liveOwnerScene)
        {
            openingOwner = core.liveOwnerToken;
            openingScene = core.liveOwnerScene;
            opening.clear();
        }
        if (opening.size() >= 16 && opening.find(role.token) == opening.end()) return;
        auto& cache = opening[role.token].mesh;
        const auto* entry = core.sceneActorRegistry.Find(actor);
        const auto seq = ++nextSample;
        SYSTEMTIME wall{};
        GetLocalTime(&wall);
        const auto* status = role.status;
        const bool native = role.actual == 2 && status && status->actor == actor && status->injected && status->writes;
        const auto solved = role.actual == 1 && role.fallback ? role.fallback->GetStats().solvedBones : 0;
        g_hostContext.logInfo(
            "[GakuMotion][LiveSleeveTimeline] event=%llu sample=%llu phase=%s edit=%s frame=%d slot=%zu actor=%p token=%llu generation=%llu character=%s scene=%d session=%llu state=%d from=%.4f target=%.4f motion=%.4f source=%.4f live=%.4f seeks=%d scrubbing=%d before_quartz_requested=%d actual=%d physics_frame=%d native_frame=%d native_seq=%llu fallback_solved=%zu pose_serial=%llu time=%04u-%02u-%02uT%02u:%02u:%02u.%03u",
            static_cast<unsigned long long>(timeline.id), static_cast<unsigned long long>(seq), phase, timeline.edit,
            g_faceUnityFrame ? g_faceUnityFrame() : frame, role.slot, actor, static_cast<unsigned long long>(role.token),
            static_cast<unsigned long long>(role.generation), entry ? entry->character.c_str() : "unknown",
            core.liveOwnerScene, static_cast<unsigned long long>(timeline.session), int(core.player.state),
            timeline.from, timeline.target, core.player.currentTime, role.player->MotionSourceTime(),
            core.liveObservation.time, core.player.SeekCount(), int(core.player.scrubbing),
            int(core.config.beforeQuartzExperiment), role.actual, role.physicsFrame, native ? status->source_frame : -1,
            static_cast<unsigned long long>(native ? status->sequence : 0), static_cast<size_t>(solved),
            static_cast<unsigned long long>(role.player->controller.footPoseWriteStamp.serial),
            wall.wYear, wall.wMonth, wall.wDay, wall.wHour, wall.wMinute, wall.wSecond, wall.wMilliseconds);
        RecordPose(actor, seq, cache, true);
    }

    inline void FinishTimeline(const char* reason)
    {
        for (size_t i = 0; i < timeline.roles.size(); ++i)
        {
            const auto& actor = timeline.roles[i];
            if (!actor.token || actor.settledSeen || !g_hostContext.logInfo) continue;
            g_hostContext.logInfo("[GakuMotion][LiveSleeveTimelineEnd] event=%llu slot=%zu token=%llu reason=%s body=%d fallback=%d native=%d",
                static_cast<unsigned long long>(timeline.id), i + 1, static_cast<unsigned long long>(actor.token),
                reason, int(actor.bodyFrame >= 0), int(actor.fallbackSeen), int(actor.nativeSeen));
        }
        timeline = {};
    }

    inline void BeforeTimeline(GakuMotion::MotionPlayer::TimelineEdit edit, float target)
    {
        using Edit = GakuMotion::MotionPlayer::TimelineEdit;
        auto& core = GakuMotion::Core::Instance();
        if (!Enabled(core) || !core.liveTakeoverActive) return;
        const bool backwards = target + 0.0005f < core.player.currentTime;
        if (edit == Edit::ScrubUpdate && (scrubBackwardCaptured || !backwards)) return;
        scrubBackwardCaptured = (edit == Edit::ScrubBegin || edit == Edit::ScrubUpdate) && backwards;
        if (timeline.id) FinishTimeline("superseded_by_timeline_edit");
        const char* names[]{"seek", "scrub_begin", "scrub_backward", "scrub_commit"};
        timeline.id = ++nextEvent;
        timeline.owner = core.liveOwnerToken;
        timeline.session = core.injectionSession;
        timeline.epoch = core.sceneActorRegistry.Epoch();
        timeline.edit = names[static_cast<unsigned>(edit)];
        timeline.from = core.player.currentTime;
        timeline.target = target;
        timeline.expires = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        for (const auto& entry : core.sceneActorRegistry.Entries())
        {
            const auto role = ReadTimelineRole(core, entry.actor);
            if (!role.slot) continue;
            timeline.roles[role.slot - 1] = {role.token, role.generation};
            RecordTimeline(core, entry.actor, role, "before_timeline_edit");
        }
    }

    inline void TimelinePose(GakuMotion::Core& core, void* actor, bool afterSecondary, int currentFrame)
    {
        if (!timeline.id || timeline.owner != core.liveOwnerToken || timeline.session != core.injectionSession ||
            timeline.epoch != core.sceneActorRegistry.Epoch() || !Enabled(core) || !core.liveTakeoverActive) return;
        const auto role = ReadTimelineRole(core, actor);
        if (!role.slot) return;
        auto& observed = timeline.roles[role.slot - 1];
        if (observed.token != role.token || observed.generation != role.generation || observed.settledSeen) return;
        if (!afterSecondary)
        {
            if (observed.bodyFrame < 0)
            {
                observed.bodyFrame = currentFrame;
                RecordTimeline(core, actor, role, "after_body");
            }
            else if (observed.fallbackSeen && !observed.pendingBodySeen && observed.bodyFrame != currentFrame)
            {
                observed.pendingBodySeen = true;
                RecordTimeline(core, actor, role, "after_body_following_fallback");
            }
            return;
        }
        if (observed.bodyFrame < 0 || role.physicsFrame != currentFrame) return;
        if (role.actual == 0 && !observed.nativeWaitSeen)
        {
            observed.nativeWaitSeen = true;
            RecordTimeline(core, actor, role, "after_native_wait");
        }
        if (role.actual == 1 && !observed.fallbackSeen)
        {
            observed.fallbackSeen = true;
            RecordTimeline(core, actor, role, "after_fallback");
        }
        const auto* status = role.status;
        if (role.actual != 2 || !status || status->actor != actor || !status->injected || !status->writes ||
            status->source_frame < observed.bodyFrame) return;
        if (!observed.nativeSeen)
        {
            observed.nativeSeen = true;
            observed.nativeFrame = currentFrame;
            RecordTimeline(core, actor, role, "native_confirmed");
        }
        else if (observed.nativeFrame != currentFrame)
        {
            observed.settledSeen = true;
            RecordTimeline(core, actor, role, "native_next_frame");
            if (std::all_of(timeline.roles.begin(), timeline.roles.end(), [](const auto& item) {
                return !item.token || item.settledSeen;
            })) FinishTimeline("complete");
        }
    }

    inline void Record(GakuMotion::Core& core, size_t index, const char* phase, bool mesh)
    {
        const auto& follower = core.ensemble.Followers()[index];
        void* actor = core.sceneActorRegistry.Resolve(follower.actorToken, core.sceneActorRegistry.Epoch());
        if (!actor || !follower.player || follower.player->controller.targetActor != actor) return;
        auto& cache = actors[index];
        if (cache.token != follower.actorToken || cache.generation != follower.bindingGeneration)
        {
            cache = {};
            cache.token = follower.actorToken;
            cache.generation = follower.bindingGeneration;
        }
        const auto& ctl = follower.player->controller;
        const auto& main = core.player.controller;
        const auto& pos = ctl.initialPositionOffset;
        const auto& mainPos = main.initialPositionOffset;
        const auto& status = g_followerBridge.State(index).status;
        const bool observed = g_followerPhysics.size() > index && g_followerPhysics[index].actor == actor &&
            g_followerPhysics[index].bindingGeneration == follower.bindingGeneration;
        const int actual = observed ? g_followerPhysics[index].actual : -1;
        const bool paired = actual == 2 && g_followerPhysics[index].quartz && status.actor == actor &&
            status.injected && status.writes;
        const auto* entry = core.sceneActorRegistry.Find(actor);
        const auto seq = ++nextSample;
        SYSTEMTIME wall{};
        GetLocalTime(&wall);
        g_hostContext.logInfo(
            "[GakuMotion][LiveSleeveSample] sample=%llu event=%llu action=%s phase=%s frame=%d event_frame=%d slot=%zu actor=%p token=%llu generation=%llu character=%s scene=%d motion=%.4f source=%.4f live=%.4f seek=%d actual=%d paired=%d native_frame=%d native_seq=%llu pose_serial=%llu offset=%.4f,%.4f,%.4f yaw=%.3f main_offset=%.4f,%.4f,%.4f main_yaw=%.3f time=%04u-%02u-%02uT%02u:%02u:%02u.%03u",
            static_cast<unsigned long long>(seq), static_cast<unsigned long long>(event), action ? action : "baseline",
            phase, frame, eventFrame, index + 2, actor, static_cast<unsigned long long>(cache.token),
            static_cast<unsigned long long>(cache.generation), entry ? entry->character.c_str() : "unknown",
            core.liveOwnerScene, core.player.currentTime, follower.player->MotionSourceTime(), core.liveObservation.time,
            core.player.SeekCount(), actual, int(paired), paired ? status.source_frame : -1,
            static_cast<unsigned long long>(paired ? status.sequence : 0),
            static_cast<unsigned long long>(ctl.footPoseWriteStamp.serial), pos.x, pos.y, pos.z, ctl.initialYawDegrees,
            mainPos.x, mainPos.y, mainPos.z, main.initialYawDegrees, wall.wYear, wall.wMonth, wall.wDay,
            wall.wHour, wall.wMinute, wall.wSecond, wall.wMilliseconds);

        RecordPose(actor, seq, cache, mesh);
    }

    inline void Frame(GakuMotion::Core& core, int current)
    {
        if (timeline.id && (timeline.owner != core.liveOwnerToken || timeline.session != core.injectionSession ||
                            timeline.epoch != core.sceneActorRegistry.Epoch() || !core.liveTakeoverActive ||
                            core.config.secondaryPhysicsBackend != 2)) FinishTimeline("context_changed");
        if (timeline.id && std::chrono::steady_clock::now() >= timeline.expires) FinishTimeline("capture_window_elapsed");
        if (session != core.injectionSession || epoch != core.sceneActorRegistry.Epoch())
        {
            session = core.injectionSession;
            epoch = core.sceneActorRegistry.Epoch();
            actors = {};
            event = 0;
            eventFrame = -1;
            frame = -1;
            remaining = 0;
            action = nullptr;
            commandSample = false;
            lastDrag = {};
        }
        if (frame != current && remaining > 0 && --remaining == 0)
        {
            event = 0;
            action = nullptr;
            eventFrame = -1;
        }
        frame = current;
    }

    inline void Placement(GakuMotion::Core& core, const char* operation, bool before)
    {
        if (!Enabled(core) || !core.liveTakeoverActive || frame < 0) return;
        const bool align = std::strcmp(operation, "align_formation") == 0;
        if (before)
        {
            commandSample = false;
            const auto now = std::chrono::steady_clock::now();
            if (!align && now - lastDrag < std::chrono::milliseconds(600)) return;
            if (!align) lastDrag = now;
            commandSample = true;
            event = ++nextEvent;
            action = operation;
            eventFrame = frame;
            remaining = align ? 6 : 3;
        }
        if (!commandSample) return;
        for (size_t i = 0; i < actors.size() && i < core.ensemble.Followers().size(); ++i)
            Record(core, i, before ? "before_command" : "after_command", before);
        if (!before) commandSample = false;
    }

    inline void Actor(GakuMotion::Core& core, void* actor, bool afterBody, bool wroteBody = false)
    {
        if (!Enabled(core) || !core.liveTakeoverActive || !actor || frame < 0) return;
        const auto& followers = core.ensemble.Followers();
        for (size_t i = 0; i < followers.size() && i < actors.size(); ++i)
        {
            const auto& follower = followers[i];
            if (!follower.player || follower.player->controller.targetActor != actor ||
                core.sceneActorRegistry.Resolve(follower.actorToken, epoch) != actor) continue;
            auto& cache = actors[i];
            const auto& status = g_followerBridge.State(i).status;
            const bool missing = cache.token != follower.actorToken || cache.generation != follower.bindingGeneration || !cache.baseline;
            if (!event && !afterBody && missing && core.player.state == GakuMotion::PlaybackState::Playing &&
                i < g_followerPhysics.size() && g_followerPhysics[i].actor == actor &&
                g_followerPhysics[i].bindingGeneration == follower.bindingGeneration &&
                g_followerPhysics[i].actual == 2 && status.actor == actor &&
                status.injected && status.writes)
            {
                Record(core, i, "baseline_before_body", true);
                cache.baseline = true;
            }
            if (event)
            {
                int& seen = afterBody ? cache.afterFrame : cache.beforeFrame;
                if (seen != frame)
                {
                    const char* phase = !afterBody ? "before_body_write" :
                        wroteBody ? "after_body_write" : "after_actor_no_body_write";
                    Record(core, i, phase, remaining >= 4 || frame == eventFrame);
                    seen = frame;
                }
            }
            break;
        }
    }
}
