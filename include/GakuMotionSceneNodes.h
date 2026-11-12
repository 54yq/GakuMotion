#pragma once
#include <cstdint>
#include <type_traits>

// In-process fixed-scope background control. No Unity pointer, hierarchy path,
// reflection operation or consumer callback crosses this boundary.
namespace GakuMotion::scene_nodes::v1
{
    inline constexpr char ServiceName[] = "gakumotion.scene-nodes/v1";
    inline constexpr std::uint32_t Version = 1;
    enum class Status : std::uint32_t
    {
        Ok, Unavailable, InvalidArgument, VersionMismatch, SizeMismatch,
        NotReady, StaleCatalog, Busy, Protected, Failed, WrongThread
    };
    struct State
    {
        std::uint32_t apiVersion{Version}, structSize{sizeof(State)};
        std::uint32_t available{}, ready{}, count{}, owned{}, restorePending{};
        Status status{Status::NotReady};
        std::uint64_t catalogRevision{};
        char message[160]{};
    };
    struct Node
    {
        std::uint32_t apiVersion{Version}, structSize{sizeof(Node)};
        std::uint64_t handle{};
        std::uint32_t active{}, ordinal{};
        char name[256]{}; // UTF-8 short name, never a Unity path.
    };
    struct Visibility { std::uint64_t handle{}; std::uint32_t active{}, reserved{}; };
    // Queries copy synchronized snapshots, invoking no Unity. Mutations are
    // synchronous Unity-main-thread-only (WrongThread elsewhere); plugins call
    // them from onTick/onActorTick, never onDesktopUi/runtime-ready workers.
    // Only direct children of exact cmn-parts nodes are exposed, excluding
    // case-insensitive floor names, actors, lighting and protected/collider nodes.
    // Order is the provider's detected hierarchy order. Handles are revision-
    // scoped opaque IDs and cannot authorize another object or hierarchy level.
    // acquire captures every current activeSelf before writing and returns an
    // exclusive lease. apply requires every catalog handle exactly once; it
    // prevalidates the entire batch. Engine setter/readback failure can partially
    // mutate Unity: returns Failed, retains baseline, blocks further apply until
    // release restores. Never interpret Failed as atomic engine success.
    // release restores captured activeSelf. Restoration failures retain ownership
    // for retry and block new leases. Stop/protection/suspension revoke the lease
    // and restore through the existing stage lifecycle. Unloaded identities are
    // abandoned rather than written into a new scene. Re-query/reacquire after
    // StaleCatalog. An apply detecting partial identity loss may keep ownership
    // for surviving objects: always release the old lease before discarding it,
    // even after apply returned StaleCatalog. ready/Node.active are cached hints;
    // acquire refreshes native values and each mutation validates fresh identity.
    // No persisted configuration or change to capture protections.
    struct Api
    {
        std::uint32_t apiVersion{Version}, structSize{sizeof(Api)};
        Status (*getState)(std::uint32_t size, State* output){};
        Status (*getNode)(std::uint64_t revision, std::uint32_t index,
                          std::uint32_t size, Node* output){};
        Status (*acquire)(std::uint64_t revision, std::uint64_t* lease){};
        Status (*apply)(std::uint64_t lease, std::uint64_t revision,
                        std::uint32_t count, const Visibility* values){};
        Status (*release)(std::uint64_t lease){};
    };
    static_assert(std::is_standard_layout_v<Api> && std::is_trivially_copyable_v<Api>);
    static_assert(std::is_trivially_copyable_v<State> && std::is_trivially_copyable_v<Node>);
    static_assert(sizeof(State) == 200 && sizeof(Node) == 280 && sizeof(Visibility) == 16);
    static_assert(sizeof(void*) != 8 || sizeof(Api) == 48);
}
