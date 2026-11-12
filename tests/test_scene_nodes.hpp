#pragma once

#include "GakuMotionSceneNodes.h"
#include "GkStageManager.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace SceneNodesTest
{
    namespace api = GakuMotion::scene_nodes::v1;

    struct Object
    {
        void* pointer{};
        int instanceId{};
        bool active{true};
    };

    // All Unity operations are observable here. Query tests reset the counters
    // after Refresh, proving that copied catalog reads never enter the engine.
    struct Fixture
    {
        GakuMotion::StageManager manager;
        std::vector<Object> objects;
        std::uint64_t generation{17};
        int reads{}, writes{}, matches{}, accesses{}, ends{};
        int failWrite{-1};
        bool mutateThenFail{}, readable{true}, accessReady{true}, wrongReadback{};
        void* wrongReadbackObject{};
        inline static Fixture* current{};

        Fixture()
        {
            current = this;
            manager.api.fnBeginAccess = [] {
                ++current->accesses;
                return current->accessReady;
            };
            manager.api.fnEndAccess = [] { ++current->ends; };
            manager.api.fnGeneration = [] { return current->generation; };
            manager.api.fnMatches = [](void* pointer, int id, std::uint64_t generation) {
                ++current->matches;
                const auto* value = current->Find(pointer);
                return value && value->instanceId == id && generation == current->generation;
            };
            manager.api.fnTryGetActiveSelf = [](void* pointer, bool& active) {
                ++current->reads;
                const auto* value = current->Find(pointer);
                if (!current->readable || !value) return false;
                const bool corrupt = current->wrongReadback &&
                    (!current->wrongReadbackObject || current->wrongReadbackObject == pointer);
                active = corrupt ? !value->active : value->active;
                return true;
            };
            manager.api.fnGetActiveSelf = [](void* pointer) {
                ++current->reads;
                const auto* value = current->Find(pointer);
                return value && value->active;
            };
            manager.api.fnSetActive = [](void* pointer, bool active) {
                auto* value = current->Find(pointer);
                if (!value) return false;
                const int call = current->writes++;
                const bool failed = call == current->failWrite;
                if (!failed || current->mutateThenFail) value->active = active;
                return !failed;
            };
            manager.currentSceneName = "Photography";
            manager.candidatesDiscovered = true;
            manager.visibilityRefreshRequested = false;
            manager.SetInjectionEnabled(true);
        }

        Object* Find(void* pointer)
        {
            for (auto& object : objects) if (object.pointer == pointer) return &object;
            return nullptr;
        }

        const Object* Find(void* pointer) const
        {
            for (const auto& object : objects) if (object.pointer == pointer) return &object;
            return nullptr;
        }

        int Add(const char* name, int parent = -1, bool active = true)
        {
            const int index = static_cast<int>(manager.candidates.size());
            const auto pointer = reinterpret_cast<void*>(std::uintptr_t{0x7100} + index * 16);
            objects.push_back({pointer, 100 + index, active});
            GakuMotion::StageCandidate candidate;
            candidate.gameObject = candidate.transform = pointer;
            candidate.instanceId = 100 + index;
            candidate.generation = generation;
            candidate.displayName = name;
            candidate.name = parent < 0 ? name : manager.candidates[parent].name + " / " + name;
            candidate.parentIndex = parent;
            candidate.depth = parent < 0 ? 0 : manager.candidates[parent].depth + 1;
            candidate.isIdentifiedBackground = true;
            // Deliberately stale discovery defaults: acquire must read current
            // native activeSelf, including objects hidden before any injection.
            candidate.originalActive = candidate.currentlyActive = true;
            if (parent >= 0) manager.candidates[parent].hasChildren = true;
            manager.candidates.push_back(candidate);
            return index;
        }

        void StandardTree()
        {
            const int root = Add("env_3d_photo_fixture");
            const int group = Add("cmn-parts", root);
            const int first = Add("Banner", group);
            Add("TinyMesh", first); // A grandchild is outside the fixed scope.
            Add("Desk", group, false);
            Add("Floor", group);
            Add("mesh_fLoOr_surface", group);
            const int collider = Add("BoxCollider", group);
            manager.candidates[collider].isColliderLocked = true;
            Add("Root_Face", group);
            const int lighting = Add("Lighting&PostEffects", group);
            manager.candidates[lighting].isLightingRoot = true;
            const int lightingChild = Add("Bloom", group);
            manager.candidates[lightingChild].isLightingNode = true;
            const int unrelated = Add("cmn-parts-extra", root);
            Add("WrongScopeProp", unrelated);
            const int wrongCase = Add("CMN-parts-extra", root);
            Add("WrongSuffixProp", wrongCase);
        }

        void ResetCounters() { reads = writes = matches = accesses = ends = 0; }

        api::State Refresh(bool allowed = true)
        {
            manager.RefreshSceneNodes(allowed);
            return State();
        }

        api::State State()
        {
            api::State state{};
            Check(manager.ReadSceneNodesState(sizeof(state), &state) == api::Status::Ok,
                "scene node state copies a synchronized value snapshot");
            return state;
        }

        std::vector<api::Node> Nodes(const api::State& state)
        {
            std::vector<api::Node> nodes(state.count);
            for (std::uint32_t index = 0; index < state.count; ++index)
                Check(manager.ReadSceneNode(state.catalogRevision, index, sizeof(api::Node), &nodes[index]) == api::Status::Ok,
                    "scene node catalog entry is readable by revision and ordinal");
            return nodes;
        }

        std::uint64_t Acquire(const api::State& state)
        {
            std::uint64_t lease{};
            Check(manager.AcquireSceneNodes(state.catalogRevision, &lease) == api::Status::Ok && lease != 0,
                "ready fixed catalog grants a nonzero exclusive lease");
            return lease;
        }
    };

    inline std::vector<api::Visibility> Values(const std::vector<api::Node>& nodes, bool active)
    {
        std::vector<api::Visibility> values;
        for (const auto& node : nodes) values.push_back({node.handle, active ? 1u : 0u, 0});
        return values;
    }
}

static void TestSceneNodesCatalog()
{
    using namespace SceneNodesTest;
    g_current = "scene nodes fixed exact cmn-parts direct child catalog";
    Fixture fixture;
    fixture.StandardTree();
    fixture.ResetCounters();
    const auto state = fixture.Refresh();
    Check(fixture.reads == 0 && fixture.writes == 0 && fixture.matches == 0 && fixture.accesses == 0,
        "catalog refresh uses published candidate metadata rather than re-entering Unity each frame");
    Check(state.available && state.ready && state.count == 2 && state.catalogRevision != 0,
        "catalog exposes only eligible direct children of exact cmn-parts names");
    const auto nodes = fixture.Nodes(state);
    if (nodes.size() != 2) return;
    Check(std::strcmp(nodes[0].name, "Banner") == 0 && std::strcmp(nodes[1].name, "Desk") == 0,
        "provider preserves detected child order rather than imposing trailer reverse order");
    Check(nodes[0].ordinal == 0 && nodes[1].ordinal == 1 && nodes[0].active == 1 && nodes[1].active == 1,
        "unleased catalog reports hierarchy ordinals and the provider's cached activeSelf snapshot");
    Check(nodes[0].handle && nodes[1].handle && nodes[0].handle != nodes[1].handle,
        "each catalog object has a distinct nonzero opaque handle");
    Check(std::strstr(nodes[0].name, " / ") == nullptr,
        "public node name is a short label and never an arbitrary hierarchy path");
    fixture.ResetCounters();
    fixture.State();
    fixture.Nodes(state);
    Check(fixture.reads == 0 && fixture.writes == 0 && fixture.matches == 0 && fixture.accesses == 0,
        "copied state and node queries never invoke Unity functions");
    const auto unchanged = fixture.Refresh();
    Check(unchanged.catalogRevision == state.catalogRevision,
        "refreshing the same object identities preserves the catalog revision");
    api::Node output{};
    Check(fixture.manager.ReadSceneNode(state.catalogRevision + 1, 0, sizeof(output), &output) == api::Status::StaleCatalog,
        "node query rejects a stale catalog revision");
    Check(fixture.manager.ReadSceneNode(state.catalogRevision, state.count, sizeof(output), &output) != api::Status::Ok,
        "node query rejects an out-of-range ordinal");

    Fixture mixedCase;
    const int mixedRoot = mixedCase.Add("env_3d_photo_fixture");
    const int mixedGroup = mixedCase.Add("CmN-PaRtS", mixedRoot);
    mixedCase.Add("CaseIndependentProp", mixedGroup);
    Check(mixedCase.Refresh().count == 1,
        "cmn-parts parent matching ignores ASCII case while still requiring an exact name");
}

static void TestSceneNodesLeaseAndBatch()
{
    using namespace SceneNodesTest;
    g_current = "scene nodes capture real baseline and prevalidate complete visibility batches";
    Fixture fixture;
    fixture.StandardTree();
    const auto state = fixture.Refresh();
    const auto nodes = fixture.Nodes(state);
    if (nodes.size() != 2) return;
    const auto lease = fixture.Acquire(state);
    const auto freshlyCaptured = fixture.Nodes(fixture.State());
    Check(freshlyCaptured[0].active == 1 && freshlyCaptured[1].active == 0,
        "acquire replaces stale discovery activation with a freshly read native baseline");
    Check(fixture.manager.HasCustomizations(), "an active scene lease remains owned even before the first visibility change");
    std::uint64_t secondLease{};
    Check(fixture.manager.AcquireSceneNodes(state.catalogRevision, &secondLease) == api::Status::Busy,
        "another consumer cannot acquire the already leased catalog");
    auto valid = Values(nodes, false);
    valid[1].active = 1;
    const auto initialWrites = fixture.writes;
    auto invalid = valid;
    invalid[1].handle = invalid[0].handle;
    Check(fixture.manager.ApplySceneNodes(lease, state.catalogRevision, static_cast<std::uint32_t>(invalid.size()), invalid.data()) != api::Status::Ok,
        "duplicate handles are refused before the first setter");
    invalid = valid;
    invalid[1].handle = UINT64_MAX;
    Check(fixture.manager.ApplySceneNodes(lease, state.catalogRevision, static_cast<std::uint32_t>(invalid.size()), invalid.data()) != api::Status::Ok,
        "unknown opaque handle is refused before the first setter");
    invalid = valid;
    invalid[1].active = 2;
    Check(fixture.manager.ApplySceneNodes(lease, state.catalogRevision, static_cast<std::uint32_t>(invalid.size()), invalid.data()) != api::Status::Ok,
        "visibility values outside boolean range are refused before the first setter");
    Check(fixture.manager.ApplySceneNodes(lease, state.catalogRevision, 1, valid.data()) != api::Status::Ok,
        "an incomplete batch cannot omit any catalog state");
    Check(fixture.manager.ApplySceneNodes(lease, state.catalogRevision, 2, nullptr) != api::Status::Ok,
        "a null batch cannot authorize any write");
    Check(fixture.manager.ApplySceneNodes(lease, state.catalogRevision + 1, 2, valid.data()) == api::Status::StaleCatalog,
        "an outdated catalog revision cannot apply through a live lease");
    Check(fixture.writes == initialWrites, "all validation failures leave the complete native state untouched");
    const auto afterInvalid = fixture.State();
    Check(afterInvalid.owned && !afterInvalid.restorePending,
        "pure argument failures preserve a usable lease without creating restoration debt");
    std::reverse(valid.begin(), valid.end());
    Check(fixture.manager.ApplySceneNodes(lease, state.catalogRevision, 2, valid.data()) == api::Status::Ok,
        "complete batches address handles rather than relying on array order");
    const auto applied = fixture.Nodes(fixture.State());
    Check(applied[0].active == 0 && applied[1].active == 1,
        "successful application publishes the full absolute state");
    const int firstIndex = 2, secondIndex = 4;
    Check(!fixture.objects[firstIndex].active && fixture.objects[secondIndex].active,
        "per-node target values reach the matching mock Unity objects");
    fixture.manager.OnLateUpdate(nullptr, .016f, "Photography", nullptr);
    fixture.manager.OnLateUpdate(nullptr, .016f, "Photography", nullptr);
    Check(!fixture.objects[firstIndex].active && fixture.objects[secondIndex].active,
        "ordinary stage updates do not restore or overwrite a plugin-owned lease");
    Check(fixture.manager.ReleaseSceneNodes(lease) == api::Status::Ok,
        "release restores the real activeSelf baseline");
    Check(fixture.objects[firstIndex].active && !fixture.objects[secondIndex].active,
        "release preserves an object that was natively hidden before acquire");
    Check(!fixture.State().owned && !fixture.State().restorePending,
        "successful release clears ownership and restoration debt");
}

static void TestSceneNodesQueryValidation()
{
    using namespace SceneNodesTest;
    g_current = "scene node public snapshot queries validate ABI without touching output or Unity";
    Fixture fixture;
    fixture.StandardTree();
    const auto catalog = fixture.Refresh();
    fixture.ResetCounters();
    api::State state{};
    state.count = 123;
    state.catalogRevision = 0xfeed;
    auto savedState = state;
    Check(fixture.manager.ReadSceneNodesState(sizeof(state) - 1, &state) == api::Status::SizeMismatch &&
        std::memcmp(&state, &savedState, sizeof(state)) == 0,
        "state query rejects a wrong buffer size without modifying output");
    state.apiVersion = api::Version + 1;
    savedState = state;
    Check(fixture.manager.ReadSceneNodesState(sizeof(state), &state) == api::Status::VersionMismatch &&
        std::memcmp(&state, &savedState, sizeof(state)) == 0,
        "state query rejects another ABI version without modifying output");
    state.apiVersion = api::Version;
    state.structSize = sizeof(state) - 1;
    savedState = state;
    Check(fixture.manager.ReadSceneNodesState(sizeof(state), &state) == api::Status::SizeMismatch &&
        std::memcmp(&state, &savedState, sizeof(state)) == 0,
        "state query rejects a mismatching embedded structure size without modifying output");
    Check(fixture.manager.ReadSceneNodesState(sizeof(state), nullptr) == api::Status::InvalidArgument,
        "state query rejects a null output pointer");
    api::Node node{};
    node.handle = 0xbeef;
    auto savedNode = node;
    Check(fixture.manager.ReadSceneNode(catalog.catalogRevision, 0, sizeof(node) - 1, &node) == api::Status::SizeMismatch &&
        std::memcmp(&node, &savedNode, sizeof(node)) == 0,
        "node query rejects a wrong buffer size without modifying output");
    node.apiVersion = api::Version + 1;
    savedNode = node;
    Check(fixture.manager.ReadSceneNode(catalog.catalogRevision, 0, sizeof(node), &node) == api::Status::VersionMismatch &&
        std::memcmp(&node, &savedNode, sizeof(node)) == 0,
        "node query rejects another ABI version without modifying output");
    node.apiVersion = api::Version;
    node.structSize = sizeof(node) - 1;
    savedNode = node;
    Check(fixture.manager.ReadSceneNode(catalog.catalogRevision, 0, sizeof(node), &node) == api::Status::SizeMismatch &&
        std::memcmp(&node, &savedNode, sizeof(node)) == 0,
        "node query rejects a mismatching embedded structure size without modifying output");
    Check(fixture.manager.ReadSceneNode(catalog.catalogRevision, 0, sizeof(node), nullptr) == api::Status::InvalidArgument,
        "node query rejects a null output pointer");
    Check(fixture.reads == 0 && fixture.writes == 0 && fixture.matches == 0 && fixture.accesses == 0,
        "all ABI validation failures remain copied snapshot operations with no Unity calls");
}

static void TestSceneNodesFailureRestore()
{
    using namespace SceneNodesTest;
    g_current = "scene node engine failure retains baseline and release can retry";
    Fixture fixture;
    fixture.StandardTree();
    const auto state = fixture.Refresh();
    const auto nodes = fixture.Nodes(state);
    if (nodes.size() != 2) return;
    const auto lease = fixture.Acquire(state);
    auto desired = Values(nodes, false);
    desired[1].active = 1;
    fixture.failWrite = fixture.writes;
    fixture.mutateThenFail = true;
    Check(fixture.manager.ApplySceneNodes(lease, state.catalogRevision, 2, desired.data()) == api::Status::Failed,
        "a Unity setter that mutates and fails is reported as an engine failure");
    const auto failed = fixture.State();
    Check(failed.owned && failed.restorePending,
        "uncertain engine writes retain ownership and require restoration");
    const int writesAfterFailure = fixture.writes;
    Check(fixture.manager.ApplySceneNodes(lease, state.catalogRevision, 2, desired.data()) != api::Status::Ok &&
        fixture.writes == writesAfterFailure,
        "further applications are blocked while restoration is pending");
    std::uint64_t blockedLease{};
    Check(fixture.manager.AcquireSceneNodes(state.catalogRevision, &blockedLease) == api::Status::Busy,
        "restoration debt blocks a replacement lease");
    fixture.failWrite = fixture.writes;
    Check(fixture.manager.ReleaseSceneNodes(lease) == api::Status::Failed,
        "failed release cannot discard the captured baseline");
    Check(fixture.State().owned && fixture.State().restorePending,
        "failed release retains its lease for an explicit retry");
    fixture.failWrite = -1;
    Check(fixture.manager.ReleaseSceneNodes(lease) == api::Status::Ok,
        "a confirmed release retry completes restoration");
    Check(fixture.objects[2].active && !fixture.objects[4].active,
        "retry restores both originally visible and originally hidden objects");
}

static void TestSceneNodesAdmissionAndLifecycle()
{
    using namespace SceneNodesTest;
    g_current = "scene nodes respect admission and existing Stop or capture restoration";
    Fixture fixture;
    fixture.StandardTree();
    auto state = fixture.Refresh(false);
    const int writesBefore = fixture.writes;
    std::uint64_t deniedLease{};
    Check(!state.ready && fixture.manager.AcquireSceneNodes(state.catalogRevision, &deniedLease) != api::Status::Ok,
        "a known tree outside an admitted context does not grant writable access");
    Check(fixture.writes == writesBefore, "denied scene admission never writes Unity state");
    state = fixture.Refresh(true);
    const auto nodes = fixture.Nodes(state);
    if (nodes.size() != 2) return;
    const auto lease = fixture.Acquire(state);
    auto desired = Values(nodes, false);
    desired[1].active = 1;
    Check(fixture.manager.ApplySceneNodes(lease, state.catalogRevision, 2, desired.data()) == api::Status::Ok,
        "admitted scene can apply the restricted catalog");
    Check(fixture.manager.SetInjectionEnabled(false),
        "existing Stop or capture suspension revokes and restores the node lease");
    Check(fixture.objects[2].active && !fixture.objects[4].active,
        "stage suspension restores the lease's captured native state");
    Check(!fixture.State().owned, "stage suspension removes plugin ownership");
    const int afterStop = fixture.writes;
    Check(fixture.manager.ApplySceneNodes(lease, state.catalogRevision, 2, desired.data()) != api::Status::Ok &&
        fixture.writes == afterStop,
        "a revoked lease cannot write after Stop");
}

static void TestSceneNodesAcquisitionChecks()
{
    using namespace SceneNodesTest;
    g_current = "scene nodes refuse unconfirmed engine baselines before writing";
    Fixture fixture;
    fixture.StandardTree();
    const auto state = fixture.Refresh();
    std::uint64_t lease{};
    fixture.readable = false;
    const int writesBefore = fixture.writes;
    Check(fixture.manager.AcquireSceneNodes(state.catalogRevision, &lease) != api::Status::Ok && lease == 0,
        "an unreadable native baseline cannot grant a visibility lease");
    Check(fixture.writes == writesBefore, "baseline capture failure performs no Unity writes");
    fixture.readable = true;
    fixture.accessReady = false;
    Check(fixture.manager.AcquireSceneNodes(state.catalogRevision, &lease) != api::Status::Ok && lease == 0,
        "unavailable scene access cannot grant a lease");
    Check(fixture.writes == writesBefore, "access refusal performs no Unity writes");
    fixture.accessReady = true;
    fixture.objects[4].instanceId += 1000;
    Check(fixture.manager.AcquireSceneNodes(state.catalogRevision, &lease) != api::Status::Ok && lease == 0,
        "a replaced child identity invalidates acquisition even when its pointer is reused");
    Check(fixture.writes == writesBefore, "replaced-identity capture never writes an old object");
}

static void TestSceneNodesReadbackFailure()
{
    using namespace SceneNodesTest;
    g_current = "scene node successful setters still require native readback confirmation";
    Fixture fixture;
    fixture.StandardTree();
    const auto state = fixture.Refresh();
    const auto nodes = fixture.Nodes(state);
    if (nodes.size() != 2) return;
    const auto lease = fixture.Acquire(state);
    auto desired = Values(nodes, false);
    desired[1].active = 1;
    fixture.wrongReadbackObject = fixture.objects[2].pointer;
    fixture.wrongReadback = true;
    Check(fixture.manager.ApplySceneNodes(lease, state.catalogRevision, 2, desired.data()) == api::Status::Failed,
        "a setter success with mismatching readback is not reported as a complete state application");
    Check(fixture.State().restorePending, "unconfirmed readback retains restoration debt");
    fixture.wrongReadback = false;
    Check(fixture.manager.ReleaseSceneNodes(lease) == api::Status::Ok &&
        fixture.objects[2].active && !fixture.objects[4].active,
        "release can restore a state whose previous write could not be confirmed");
}

static void TestSceneNodesIdentityPreflight()
{
    using namespace SceneNodesTest;
    g_current = "scene nodes validate every object identity before any batch setter";
    Fixture fixture;
    fixture.StandardTree();
    const auto state = fixture.Refresh();
    const auto nodes = fixture.Nodes(state);
    if (nodes.size() != 2) return;
    const auto lease = fixture.Acquire(state);
    const auto desired = Values(nodes, false);
    fixture.objects[4].instanceId += 1000; // Last child pointer now denotes another object.
    const int writesBefore = fixture.writes;
    Check(fixture.manager.ApplySceneNodes(lease, state.catalogRevision, 2, desired.data()) == api::Status::StaleCatalog,
        "a replaced child invalidates the entire apply transaction");
    Check(fixture.writes == writesBefore && fixture.objects[2].active,
        "identity mismatch on a later child cannot partially mutate an earlier valid child");

    Fixture replacedScene;
    replacedScene.StandardTree();
    const auto oldState = replacedScene.Refresh();
    const auto oldNodes = replacedScene.Nodes(oldState);
    if (oldNodes.size() != 2) return;
    const auto oldLease = replacedScene.Acquire(oldState);
    const auto oldDesired = Values(oldNodes, false);
    ++replacedScene.generation;
    const int beforeSceneCall = replacedScene.writes;
    Check(replacedScene.manager.ApplySceneNodes(oldLease, oldState.catalogRevision, 2, oldDesired.data()) == api::Status::StaleCatalog,
        "scene generation replacement is rejected without waiting for explicit abandonment");
    replacedScene.manager.ReleaseSceneNodes(oldLease);
    Check(replacedScene.writes == beforeSceneCall,
        "stale apply and release never write to another scene reusing old addresses");
}

static void TestSceneNodesPartialIdentityRestore()
{
    using namespace SceneNodesTest;
    g_current = "scene node partial identity loss retains restoration responsibility for surviving objects";
    Fixture fixture;
    fixture.StandardTree();
    const auto state = fixture.Refresh();
    const auto nodes = fixture.Nodes(state);
    if (nodes.size() != 2) return;
    const auto lease = fixture.Acquire(state);
    auto desired = Values(nodes, false);
    desired[1].active = 1;
    Check(fixture.manager.ApplySceneNodes(lease, state.catalogRevision, 2, desired.data()) == api::Status::Ok,
        "partial identity fixture begins with visible and hidden nodes changed by the lease");
    fixture.objects[4].instanceId += 1000;
    fixture.objects[4].active = false; // Another object now occupies the same address.
    const int writesBefore = fixture.writes;
    Check(fixture.manager.ApplySceneNodes(lease, state.catalogRevision, 2, desired.data()) == api::Status::StaleCatalog &&
        fixture.writes == writesBefore,
        "stale apply performs no new target writes when one child is replaced");
    const auto pending = fixture.State();
    Check(pending.owned && pending.restorePending,
        "one stale child cannot erase the surviving object's captured baseline");
    const auto released = fixture.manager.ReleaseSceneNodes(lease);
    Check(released == api::Status::Ok || released == api::Status::StaleCatalog,
        "release can retire the stale lease after resolving surviving restoration responsibility");
    Check(fixture.objects[2].active && !fixture.objects[4].active,
        "release restores the surviving node but never writes the replacement child");
    Check(!fixture.State().owned && !fixture.State().restorePending,
        "resolved surviving restoration debt permits a fresh catalog lease");
}

static void TestSceneNodesRecycledIdentity()
{
    using namespace SceneNodesTest;
    g_current = "scene nodes abandon unloaded identities rather than touching pointer-reused objects";
    Fixture fixture;
    fixture.StandardTree();
    const auto oldState = fixture.Refresh();
    const auto nodes = fixture.Nodes(oldState);
    if (nodes.size() != 2) return;
    const auto lease = fixture.Acquire(oldState);
    auto desired = Values(nodes, false);
    Check(fixture.manager.ApplySceneNodes(lease, oldState.catalogRevision, 2, desired.data()) == api::Status::Ok,
        "initial scene lease can change its exact identities");
    ++fixture.generation;
    for (auto& object : fixture.objects) { object.active = true; object.instanceId += 1000; }
    const int writesBefore = fixture.writes;
    fixture.manager.AbandonScene();
    Check(fixture.writes == writesBefore, "abandoning an unloaded scene never restores through stale pointers");
    fixture.manager.candidates.clear();
    fixture.objects.clear();
    fixture.StandardTree(); // Same mock addresses, distinct generation.
    fixture.manager.currentSceneName = "Photography";
    fixture.manager.candidatesDiscovered = true;
    fixture.manager.SetInjectionEnabled(true);
    const auto replacement = fixture.Refresh();
    Check(replacement.catalogRevision != oldState.catalogRevision,
        "a new scene using recycled addresses receives another catalog revision");
    const int beforeOldCalls = fixture.writes;
    Check(fixture.manager.ApplySceneNodes(lease, oldState.catalogRevision, 2, desired.data()) != api::Status::Ok,
        "the old scene's lease and handles cannot authorize the replacement objects");
    fixture.manager.ReleaseSceneNodes(lease);
    Check(fixture.writes == beforeOldCalls, "stale apply and release never write into pointer-reused objects");
    std::uint64_t staleLease{};
    Check(fixture.manager.AcquireSceneNodes(oldState.catalogRevision, &staleLease) == api::Status::StaleCatalog,
        "old catalog revision cannot be reacquired after scene replacement");
}

static void TestSceneNodes()
{
    TestSceneNodesCatalog();
    TestSceneNodesQueryValidation();
    TestSceneNodesLeaseAndBatch();
    TestSceneNodesFailureRestore();
    TestSceneNodesAdmissionAndLifecycle();
    TestSceneNodesAcquisitionChecks();
    TestSceneNodesReadbackFailure();
    TestSceneNodesIdentityPreflight();
    TestSceneNodesPartialIdentityRestore();
    TestSceneNodesRecycledIdentity();
}
