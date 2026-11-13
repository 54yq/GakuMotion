#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

// In-process, provider-owned C++ control contract. No Unity object, allocator,
// STL container, callback supplied by a consumer, or executable payload crosses
// this boundary. Query this table through GakuLoader's existing service registry
// using ServiceName and Version; this does not change the loader ABI.
namespace GakuMotion::control::v0
{
    inline constexpr char ServiceName[] = "gakumotion.control/v0";
    // Product prototype is v0. Its initial wire structure version is 1 because
    // the existing GakuLoader registry reserves/rejects abiVersion == 0.
    inline constexpr std::uint32_t Version = 1;
    inline constexpr std::uint32_t MaxOutstanding = 128;
    inline constexpr std::uint32_t KeyCapacity = 512;

    enum class Error : std::uint32_t
    {
        Ok, Pending, Unavailable, InvalidArgument, VersionMismatch, SizeMismatch,
        Unsupported, NotFound, NotReady, Protected, StaleSession, StaleMotion,
        Busy, Failed
    };

    enum class CommandKind : std::uint32_t
    {
        Load, Play, Pause, Stop, Seek, Speed, Offset, Camera, Scene
    };

    enum class Playback : std::uint32_t { Stopped = 0, Playing = 1, Paused = 2, Preview = 3 };
    enum class ScenePreset : std::uint32_t
    {
        Native = 0, Green = 1, Blue = 2, Black = 3, White = 4, Gray = 5
    };

    constexpr std::uint64_t Mask(CommandKind kind) noexcept
    {
        return static_cast<std::uint32_t>(kind) <= static_cast<std::uint32_t>(CommandKind::Scene)
            ? std::uint64_t{1} << static_cast<std::uint32_t>(kind) : 0;
    }
    inline constexpr std::uint64_t AllCommandsMask = (std::uint64_t{1} << 9) - 1;

    struct Vec3 { float x{}, y{}, z{}; };

    // Partial camera configuration. Other provider camera settings are retained.
    // mode: Track=0, Hold=1, Follow=2; behavior: LookAt=0, FollowMove=1;
    // target: Hips=0, Chest=1, Head=2. Rates are fractions in [0,1].
    struct CameraConfig
    {
        std::uint32_t enabled{};
        std::uint32_t mode{};
        std::uint32_t followBehavior{};
        std::uint32_t followTarget{};
        float rateXYZ[3]{0.1f, 0.1f, 0.1f};
    };

    // Start from Command{} and copy all three revisions from a fresh State.
    // Session and motion generation are always checked; Load also checks the
    // library revision. The provider rechecks on the Unity thread. Zero is a real revision,
    // never a wildcard. Load uses an exact UTF-8 library key, never a path, and
    // loads a preview without autoplay. Play resumes Paused, starts otherwise.
    // Seek uses seconds; Speed uses the existing transport range [0.1,5].
    // Offset is the actor's initial placement in metres, not a track time offset.
    // Camera/Scene are transient settings; no command saves configuration.
    struct Command
    {
        std::uint32_t apiVersion{Version};
        std::uint32_t structSize{sizeof(Command)};
        CommandKind kind{CommandKind::Stop};
        std::uint64_t session{};
        std::uint64_t motionGeneration{};
        std::uint64_t libraryRevision{};
        char motionKey[KeyCapacity]{};
        float value{};
        Vec3 offset{};
        CameraConfig camera{};
        std::uint32_t scene{};
    };

    struct State
    {
        std::uint32_t apiVersion{Version};
        std::uint32_t structSize{sizeof(State)};
        std::uint32_t available{};
        std::uint32_t protectedContext{};
        std::uint32_t captureSuspended{};
        std::uint64_t session{};
        std::uint64_t generation{};
        std::uint64_t libraryRevision{};
        std::uint32_t playback{};
        float time{};
        float duration{};
        float speed{1.0f};
        Vec3 offset{};
        CameraConfig camera{};
        std::uint32_t scene{};
        std::uint32_t motionCount{};
        char loadedMotionKey[KeyCapacity]{};
        std::uint32_t cameraAvailable{};
        std::uint32_t cameraOwned{};
        char cameraError[160]{};
        std::uint32_t cameraRestorePending{};
        std::uint32_t sceneRestorePending{};
    };

    struct Capabilities
    {
        std::uint32_t apiVersion{Version};
        std::uint32_t structSize{sizeof(Capabilities)};
        std::uint64_t supportedMask{};
        std::uint64_t availableMask{};
        std::uint32_t maxOutstanding{MaxOutstanding};
    };

    struct Motion
    {
        std::uint32_t apiVersion{Version};
        std::uint32_t structSize{sizeof(Motion)};
        char key[KeyCapacity]{};
    };

    struct Result
    {
        std::uint32_t apiVersion{Version};
        std::uint32_t structSize{sizeof(Result)};
        std::uint64_t ticket{};
        Error status{Error::Pending};
        char message[160]{}; // UTF-8 diagnostic; use status for program logic.
    };

    // All functions may be called from any thread. Queries copy a synchronized
    // snapshot and never invoke Unity. getState can return a historical snapshot
    // with available=0 after shutdown. Capability availability is advisory and
    // can change before execution; Camera's bit describes enable readiness,
    // while disabling remains permitted without a ready camera bridge.
    // submit copies Command before returning;
    // Pending means accepted with a nonzero ticket, not executed. Other submit
    // errors return ticket zero. Mutations execute only at the existing
    // main-thread command drain and can remain Pending when the host has no
    // eligible actor tick. Protection is checked again at execution.
    //
    // poll returns Ok when Result was copied, including Result.status=Pending.
    // Keep polling until terminal, then release. release(Pending) returns Busy.
    // At most maxOutstanding accepted records exist, including unread terminal
    // results. No automatic eviction, synchronous wait, or consumer callbacks.
    // Shutdown makes queued results terminal Unavailable. Tickets are not reused.
    //
    // v0 uses exact struct sizes, default native alignment, x64 and fixed-width
    // fields. Do not pack these structs. Wrong version/size is rejected without
    // consuming a ticket. Callers retain their own buffers. Service functions
    // remain callable while the provider module is resident, including shutdown;
    // do not retain this table across unloading the module/process.
    struct Api
    {
        std::uint32_t apiVersion{Version};
        std::uint32_t structSize{sizeof(Api)};
        Error (*getState)(std::uint32_t outputSize, State* output){};
        Error (*getCapabilities)(std::uint32_t outputSize, Capabilities* output){};
        Error (*getMotion)(std::uint64_t libraryRevision, std::uint32_t index,
            std::uint32_t outputSize, Motion* output){};
        Error (*submit)(const Command* command, std::uint64_t* ticket){};
        Error (*poll)(std::uint64_t ticket, std::uint32_t outputSize, Result* output){};
        Error (*release)(std::uint64_t ticket){};
    };

    static_assert(std::is_standard_layout_v<Command> && std::is_trivially_copyable_v<Command>);
    static_assert(std::is_standard_layout_v<State> && std::is_trivially_copyable_v<State>);
    static_assert(std::is_standard_layout_v<Capabilities> && std::is_trivially_copyable_v<Capabilities>);
    static_assert(std::is_standard_layout_v<Motion> && std::is_trivially_copyable_v<Motion>);
    static_assert(std::is_standard_layout_v<Result> && std::is_trivially_copyable_v<Result>);
    static_assert(std::is_standard_layout_v<Api> && std::is_trivially_copyable_v<Api>);
    static_assert(sizeof(Vec3) == 12 && sizeof(CameraConfig) == 28);
    static_assert(sizeof(Command) == 600 && sizeof(State) == 800);
    static_assert(sizeof(Capabilities) == 32 && sizeof(Motion) == 520 && sizeof(Result) == 184);
    static_assert(sizeof(void*) != 8 || sizeof(Api) == 56);
    static_assert(offsetof(Api, apiVersion) == 0 && offsetof(Api, structSize) == 4);
}
