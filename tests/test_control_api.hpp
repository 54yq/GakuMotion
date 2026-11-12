#pragma once

#include "GakuMotionControl.h"
#include "GkControlRequests.hpp"
#include <atomic>
#include <cstring>
#include <set>
#include <thread>

namespace ControlApiTest
{
    namespace api = GakuMotion::control::v0;
    inline int plays{}, pauses{}, seeks{}, stopped{};
    inline float audioPosition{};
    inline std::thread::id callbackThread;
    inline void Play(const std::string&) { ++plays; callbackThread = std::this_thread::get_id(); }
    inline void Pause() { ++pauses; callbackThread = std::this_thread::get_id(); }
    inline void Seek(float value) { ++seeks; audioPosition = value; callbackThread = std::this_thread::get_id(); }
    inline void Stop() { ++stopped; callbackThread = std::this_thread::get_id(); }
    inline void Resume(float value) { audioPosition = value; callbackThread = std::this_thread::get_id(); }

    inline void Prepare(GakuMotion::Core& core)
    {
        core.initialized = true;
        core.SetControlActive(true);
        core.currentActor = reinterpret_cast<void*>(1);
        core.player.clip = MakeSmallClip();
        core.player.clip.duration_seconds = 10.0f;
        core.player.loadedMotionFile = "control-fixture";
        core.loadedMotionKey = "fixture";
        core.loadedMotionGeneration = 7;
        core.libraryRevision = 11;
        core.player.fnPlayAudio = Play;
        core.player.fnPauseAudio = Pause;
        core.player.fnStopAudio = Stop;
        core.player.fnSeekAudio = Seek;
        core.player.fnSeekPausedAudio = Seek;
        core.player.fnResumeAudio = Resume;
        plays = pauses = seeks = stopped = 0;
        audioPosition = 0;
        callbackThread = {};
    }

    inline api::State State(GakuMotion::Core& core)
    {
        api::State state{};
        Check(core.ReadControlState(sizeof(state), &state) == api::Error::Ok,
            "state query returns an owned value snapshot");
        return state;
    }

    inline api::Command Command(GakuMotion::Core& core, api::CommandKind kind)
    {
        const auto state = State(core);
        api::Command command{};
        command.kind = kind;
        command.session = state.session;
        command.motionGeneration = state.generation;
        command.libraryRevision = state.libraryRevision;
        return command;
    }

    inline api::Error Run(GakuMotion::Core& core, const api::Command& command)
    {
        uint64_t ticket = 0;
        const auto submitted = core.SubmitControl(&command, &ticket);
        if (submitted != api::Error::Pending) return submitted;
        Check(ticket != 0, "accepted command receives a nonzero ticket");
        api::Result result{};
        Check(core.PollControl(ticket, sizeof(result), &result) == api::Error::Ok && result.status == api::Error::Pending,
            "submission remains pending before the Unity command pump");
        core.ProcessPendingGuiCommands();
        Check(core.PollControl(ticket, sizeof(result), &result) == api::Error::Ok, "terminal result lookup succeeds");
        const auto status = result.status;
        Check(result.ticket == ticket && result.status == status && result.message[0] != '\0',
            "poll reports matching ticket, terminal status and diagnostic");
        Check(core.ReleaseControl(ticket) == api::Error::Ok, "terminal ticket is explicitly released");
        return status;
    }
}

static void TestControlApi()
{
    using namespace ControlApiTest;
    BeginTest("control_api_value_queries_and_validation");
    GakuMotion::Core core;
    Prepare(core);
    auto state = State(core);
    Check(state.available && state.generation == 7 && state.libraryRevision == 11 &&
        state.duration == 10.0f && std::strcmp(state.loadedMotionKey, "fixture") == 0,
        "state carries session, loaded generation, library revision and timeline values");
    api::Capabilities capabilities{};
    Check(core.ReadControlCapabilities(sizeof(capabilities), &capabilities) == api::Error::Ok &&
        (capabilities.supportedMask & api::Mask(api::CommandKind::Seek)) && capabilities.maxOutstanding > 0,
        "capabilities distinguish implemented controls and bounded request capacity");
    Check(!(capabilities.availableMask & api::Mask(api::CommandKind::Pause)),
        "Pause is unavailable when the loaded transport is stopped");
    core.player.state = PlaybackState::Preview;
    api::Capabilities previewCapabilities{};
    Check(core.ReadControlCapabilities(sizeof(previewCapabilities), &previewCapabilities) == api::Error::Ok &&
        !(previewCapabilities.availableMask & api::Mask(api::CommandKind::Pause)) &&
        (previewCapabilities.availableMask & api::Mask(api::CommandKind::Play)),
        "preview advertises Play while withholding Pause that cannot execute");
    core.player.state = PlaybackState::Stopped;
    Check(core.ReadControlState(sizeof(state) - 1, &state) == api::Error::SizeMismatch &&
        core.ReadControlState(sizeof(state), nullptr) == api::Error::InvalidArgument,
        "query rejects undersized output and null buffers");
    auto invalid = Command(core, api::CommandKind::Seek);
    uint64_t ticket = 77;
    invalid.apiVersion = api::Version + 1;
    Check(core.SubmitControl(&invalid, &ticket) == api::Error::VersionMismatch && ticket == 0,
        "foreign command version is rejected without issuing a ticket");
    invalid.apiVersion = api::Version;
    invalid.structSize = sizeof(invalid) - 1;
    Check(core.SubmitControl(&invalid, &ticket) == api::Error::SizeMismatch,
        "undersized command is rejected");
    invalid.structSize = sizeof(invalid);
    invalid.kind = static_cast<api::CommandKind>(999);
    Check(core.SubmitControl(&invalid, &ticket) == api::Error::Unsupported,
        "unimplemented command kind is explicit rather than a silent no-op");
    invalid.kind = api::CommandKind::Seek;
    invalid.value = (std::numeric_limits<float>::quiet_NaN)();
    Check(core.SubmitControl(&invalid, &ticket) == api::Error::InvalidArgument,
        "non-finite seek cannot reach the player");
    invalid.value = -1;
    Check(core.SubmitControl(&invalid, &ticket) == api::Error::InvalidArgument,
        "negative seek is rejected");
    invalid.kind = api::CommandKind::Speed;
    invalid.value = 0;
    Check(core.SubmitControl(&invalid, &ticket) == api::Error::InvalidArgument,
        "speed outside the current transport range is rejected");
    invalid.kind = api::CommandKind::Load;
    std::memset(invalid.motionKey, 'x', sizeof(invalid.motionKey));
    Check(core.SubmitControl(&invalid, &ticket) == api::Error::InvalidArgument,
        "unterminated library key is rejected without reading beyond the buffer");

    BeginTest("control_api_library_identity_and_real_preview_load");
    {
        GakuMotion::Core loader;
        Prepare(loader);
        const auto fixture = std::filesystem::temp_directory_path() /
            ("gkmotion_control_api_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ActorlessLibraryFixture::WriteClip(fixture / "sample" / "motion.gkclip");
        MotionEntry entry;
        entry.name = "sample";
        entry.dir = MotionPathToUtf8(fixture / "sample");
        loader.motions.push_back(entry);
        api::Motion motion{};
        Check(loader.ReadControlMotion(loader.libraryRevision, 0, sizeof(motion), &motion) == api::Error::Ok &&
            std::strcmp(motion.key, "sample") == 0,
            "library enumeration returns only an owned stable UTF-8 key");
        Check(loader.ReadControlMotion(loader.libraryRevision + 1, 0, sizeof(motion), &motion) == api::Error::StaleMotion &&
            loader.ReadControlMotion(loader.libraryRevision, 1, sizeof(motion), &motion) == api::Error::NotFound,
            "library revision and bounds are checked before returning an item");
        auto load = Command(loader, api::CommandKind::Load);
        std::snprintf(load.motionKey, sizeof(load.motionKey), "%s", entry.dir.c_str());
        Check(Run(loader, load) == api::Error::NotFound && loader.loadedMotionKey == "fixture",
            "arbitrary filesystem path cannot be used as a load key");
        std::snprintf(load.motionKey, sizeof(load.motionKey), "sample");
        Check(loader.SubmitControl(&load, &ticket) == api::Error::Pending,
            "known library key queues an asynchronous preview load");
        ++loader.libraryRevision;
        loader.ProcessPendingGuiCommands();
        api::Result loadResult{};
        Check(loader.PollControl(ticket, sizeof(loadResult), &loadResult) == api::Error::Ok &&
            loadResult.status == api::Error::StaleMotion && loader.loadedMotionKey == "fixture",
            "a library refresh invalidates a queued load instead of redirecting it");
        loader.ReleaseControl(ticket);
        load = Command(loader, api::CommandKind::Load);
        std::snprintf(load.motionKey, sizeof(load.motionKey), "sample");
        loader.config.autoPlayOnSelect = true;
        loader.player.state = PlaybackState::Playing;
        Check(Run(loader, load) == api::Error::Ok && loader.loadedMotionKey == "sample" &&
            loader.loadedMotionGeneration == 8 && loader.player.state == PlaybackState::Preview,
            "Load reuses the real file loader and guarantees silent Preview despite GUI autoplay preferences");
        load = Command(loader, api::CommandKind::Load);
        std::snprintf(load.motionKey, sizeof(load.motionKey), "sample");
        Check(loader.SubmitControl(&load, &ticket) == api::Error::Pending,
            "reload is accepted before its source file disappears");
        std::filesystem::remove(fixture / "sample" / "motion.gkclip");
        loader.ProcessPendingGuiCommands();
        Check(loader.PollControl(ticket, sizeof(loadResult), &loadResult) == api::Error::Ok &&
            loadResult.status == api::Error::Failed && loader.loadedMotionGeneration == 9,
            "real loader failure reports Failed and advances generation before further queued work");
        loader.ReleaseControl(ticket);
        loader.SetControlActive(false);
        std::filesystem::remove_all(fixture);
    }

    BeginTest("control_api_main_thread_transport_and_existing_audio_path");
    plays = pauses = seeks = stopped = 0;
    audioPosition = 0;
    callbackThread = {};
    const auto mainThread = std::this_thread::get_id();
    auto play = Command(core, api::CommandKind::Play);
    api::Error submitted = api::Error::Failed;
    ticket = 0;
    std::thread consumer([&] { submitted = core.SubmitControl(&play, &ticket); });
    consumer.join();
    Check(submitted == api::Error::Pending && ticket != 0 && plays == 0 &&
        core.player.state == PlaybackState::Stopped,
        "external consumer thread only queues the command and performs no player/audio work");
    Check(core.ReleaseControl(ticket) == api::Error::Busy, "pending ticket cannot be discarded before execution");
    core.ProcessPendingGuiCommands();
    api::Result result{};
    Check(core.PollControl(ticket, sizeof(result), &result) == api::Error::Ok && result.status == api::Error::Ok && plays == 1 &&
        callbackThread == mainThread && core.player.state == PlaybackState::Playing,
        "existing Unity command pump invokes the player on its own thread");
    core.ReleaseControl(ticket);
    Check(Run(core, Command(core, api::CommandKind::Play)) == api::Error::Ok && plays == 1,
        "Play is idempotent while playing");
    auto seek = Command(core, api::CommandKind::Seek);
    seek.value = 3.25f;
    Check(Run(core, seek) == api::Error::Ok && core.player.currentTime == 3.25f &&
        audioPosition == 3.25f && seeks == 1,
        "seconds seek delegates to the existing player and audio seek path");
    Check(Run(core, Command(core, api::CommandKind::Pause)) == api::Error::Ok &&
        core.player.state == PlaybackState::Paused && pauses == 1,
        "Pause preserves the existing paused transport behavior");
    Check(Run(core, Command(core, api::CommandKind::Pause)) == api::Error::Ok && pauses == 1,
        "Pause is idempotent while paused");
    seek = Command(core, api::CommandKind::Seek);
    seek.value = 4.5f;
    Check(Run(core, seek) == api::Error::Ok && core.player.state == PlaybackState::Paused &&
        audioPosition == 4.5f && seeks == 2,
        "paused seek uses the existing silent audio positioning callback");
    Check(Run(core, Command(core, api::CommandKind::Play)) == api::Error::Ok &&
        core.player.state == PlaybackState::Playing && plays == 1 && audioPosition == 4.5f,
        "Play resumes a paused session at its current time without reopening audio");
    auto speed = Command(core, api::CommandKind::Speed);
    speed.value = 2.0f;
    Check(Run(core, speed) == api::Error::Ok && core.player.playbackSpeedMultiplier == 2.0f &&
        !core.player.audioStarted && !core.player.AudioRateSupported() && stopped == 1,
        "speed delegates to the current unsupported-audio-rate degradation mechanism");
    seek = Command(core, api::CommandKind::Seek);
    seek.value = 11.0f;
    Check(Run(core, seek) == api::Error::InvalidArgument && core.player.currentTime == 4.5f,
        "seek beyond the queried duration reports an error and preserves time");

    BeginTest("control_api_session_capture_and_motion_lifetime");
    auto stale = Command(core, api::CommandKind::Seek);
    stale.value = 1.0f;
    Check(core.SubmitControl(&stale, &ticket) == api::Error::Pending, "queue a seek against the current generation");
    ++core.loadedMotionGeneration;
    core.ProcessPendingGuiCommands();
    Check(core.PollControl(ticket, sizeof(result), &result) == api::Error::Ok && result.status == api::Error::StaleMotion &&
        core.player.currentTime == 4.5f, "queued seek cannot affect a subsequently loaded motion");
    core.ReleaseControl(ticket);
    stale = Command(core, api::CommandKind::Offset);
    stale.offset = {1, 2, 3};
    Check(core.SubmitControl(&stale, &ticket) == api::Error::Pending, "queue a placement before session invalidation");
    core.InvalidateControlSession();
    core.ProcessPendingGuiCommands();
    Check(core.PollControl(ticket, sizeof(result), &result) == api::Error::Ok && result.status == api::Error::StaleSession &&
        core.player.controller.initialPositionOffset.x == 0,
        "placement cannot leak across a session boundary");
    core.ReleaseControl(ticket);

    auto copied = Command(core, api::CommandKind::Offset);
    copied.offset = {.125f, 0, 0};
    Check(core.SubmitControl(&copied, &ticket) == api::Error::Pending,
        "placement command is copied into the provider queue");
    copied.offset.x = 99;
    core.ProcessPendingGuiCommands();
    Check(core.PollControl(ticket, sizeof(result), &result) == api::Error::Ok && result.status == api::Error::Ok &&
        core.player.controller.initialPositionOffset.x == .125f,
        "consumer mutations after submit cannot alter the queued payload");
    core.ReleaseControl(ticket);
    core.player.controller.SetInitialPositionOffset({0, 0, 0});

    stale = Command(core, api::CommandKind::Offset);
    stale.offset = {7, 0, 0};
    Check(core.SubmitControl(&stale, &ticket) == api::Error::Pending,
        "queue a request before its actor disappears");
    core.currentActor = nullptr;
    core.RejectObsoleteControlCommands();
    Check(core.PollControl(ticket, sizeof(result), &result) == api::Error::Ok &&
        result.status == api::Error::NotReady && core.player.controller.initialPositionOffset.x == 0,
        "actor-free host tick rejects invalid queued work without touching the old actor");
    core.ReleaseControl(ticket);
    core.currentActor = reinterpret_cast<void*>(1);
    core.enforceInjectionPolicy = true;
    core.injectionPolicy.SetContext(injection::Context::Photography);
    core.injectionPolicy.SetCaptureHooksReady(true);
    auto guarded = Command(core, api::CommandKind::Scene);
    guarded.scene = 1;
    Check(core.SubmitControl(&guarded, &ticket) == api::Error::Pending,
        "scene setting is queued while its owner is admitted");
    core.injectionPolicy.SetSceneAllowed(false);
    core.ProcessPendingGuiCommands();
    Check(core.PollControl(ticket, sizeof(result), &result) == api::Error::Ok &&
        result.status == api::Error::Protected && !core.stageManager.settings.greenScreenActive,
        "execution rechecks current scene protection after an accepted submission");
    core.ReleaseControl(ticket);
    core.injectionPolicy.SetSceneAllowed(true);
    core.fnBeforeInjection = [] { return false; };
    Check(Run(core, Command(core, api::CommandKind::Play)) == api::Error::Protected,
        "existing pre-injection recovery refusal is preserved by API execution");
    core.fnBeforeInjection = nullptr;
    stale = Command(core, api::CommandKind::Offset);
    stale.offset = {3, 0, 0};
    Check(core.SubmitControl(&stale, &ticket) == api::Error::Pending, "queue a control before native capture");
    core.injectionPolicy.BeginCapture();
    core.SuspendForCapture();
    const auto protectedState = State(core);
    auto protectedCommand = Command(core, api::CommandKind::Play);
    uint64_t protectedTicket = 0;
    Check(!protectedState.available && protectedState.captureSuspended &&
        core.SubmitControl(&protectedCommand, &protectedTicket) == api::Error::Protected && protectedTicket == 0,
        "capture denies new API mutations and exposes the protected state");
    core.injectionPolicy.EndCapture();
    core.ResumeAfterCapture();
    core.ProcessPendingGuiCommands();
    Check(core.PollControl(ticket, sizeof(result), &result) == api::Error::Ok && result.status == api::Error::StaleSession &&
        core.player.controller.initialPositionOffset.x == 0,
        "capture invalidates old API requests even when playback resumes the same injection session");
    core.ReleaseControl(ticket);
    core.injectionPolicy.SetSceneAllowed(false);
    auto protectedScene = Command(core, api::CommandKind::Scene);
    protectedScene.scene = 1;
    Check(core.SubmitControl(&protectedScene, &ticket) == api::Error::Protected,
        "unverified scene ownership cannot be bypassed through the domain API");
    core.injectionPolicy.SetSceneAllowed(true);

    BeginTest("control_api_settings_stop_shutdown_and_capacity");
    auto offset = Command(core, api::CommandKind::Offset);
    offset.offset = {0.2f, -0.1f, 0.3f};
    Check(Run(core, offset) == api::Error::Ok && core.player.controller.initialPositionOffset.x == 0.2f &&
        core.player.controller.initialPositionOffset.y == -0.1f,
        "placement uses the existing initial-position setter");
    auto camera = Command(core, api::CommandKind::Camera);
    camera.camera.enabled = 0;
    camera.camera.mode = 2;
    camera.camera.followTarget = 2;
    camera.camera.rateXYZ[0] = .2f;
    camera.camera.rateXYZ[1] = .3f;
    camera.camera.rateXYZ[2] = .4f;
    Check(Run(core, camera) == api::Error::Ok && !core.camera.settings.enabled &&
        core.camera.settings.mode == CameraMode::Follow && core.camera.settings.followTarget == CameraFollowTarget::Head &&
        core.camera.settings.followRate.y == .3f,
        "bounded camera settings patch the existing controller while retaining its restoration path");
    camera.camera.followTarget = 99;
    Check(core.SubmitControl(&camera, &ticket) == api::Error::InvalidArgument,
        "unknown camera target cannot reach Unity");
    auto scene = Command(core, api::CommandKind::Scene);
    scene.scene = 1;
    Check(Run(core, scene) == api::Error::Ok && core.stageManager.settings.greenScreenActive,
        "known scene preset applies through the existing stage settings pipeline");
    scene.scene = 6;
    Check(core.SubmitControl(&scene, &ticket) == api::Error::InvalidArgument,
        "external API does not expose custom stage/node operations");
    const auto sessionBeforeStop = State(core).session;
    Check(Run(core, Command(core, api::CommandKind::Stop)) == api::Error::Ok &&
        core.player.state == PlaybackState::Stopped && core.stageStoppedByTransport &&
        State(core).session != sessionBeforeStop,
        "Stop uses full performance teardown and invalidates the prior session");

    auto producerCommand = Command(core, api::CommandKind::Offset);
    producerCommand.offset = State(core).offset;
    std::vector<uint64_t> producerTickets(32);
    std::vector<api::Error> producerStatus(32, api::Error::Failed);
    std::vector<std::thread> producers;
    for (size_t producer = 0; producer < 4; ++producer)
        producers.emplace_back([&, producer] {
            for (size_t i = producer * 8; i < producer * 8 + 8; ++i)
                producerStatus[i] = core.SubmitControl(&producerCommand, &producerTickets[i]);
        });
    for (auto& producer : producers) producer.join();
    std::set<uint64_t> uniqueTickets(producerTickets.begin(), producerTickets.end());
    Check(std::all_of(producerStatus.begin(), producerStatus.end(), [](auto status) { return status == api::Error::Pending; }) &&
        uniqueTickets.size() == producerTickets.size() && !uniqueTickets.count(0),
        "simultaneous external producers receive distinct owned requests");
    core.ProcessPendingGuiCommands();
    for (const auto id : producerTickets)
    {
        Check(core.PollControl(id, sizeof(result), &result) == api::Error::Ok && result.status == api::Error::Ok,
            "every concurrent producer command reaches the existing drain");
        core.ReleaseControl(id);
    }

    std::vector<uint64_t> tickets;
    auto pending = Command(core, api::CommandKind::Offset);
    for (uint32_t i = 0; i < capabilities.maxOutstanding; ++i)
    {
        uint64_t id = 0;
        if (core.SubmitControl(&pending, &id) == api::Error::Pending) tickets.push_back(id);
    }
    Check(tickets.size() == capabilities.maxOutstanding &&
        core.SubmitControl(&pending, &ticket) == api::Error::Busy && ticket == 0,
        "unpolled requests are bounded and exert backpressure without evicting results");
    core.SetControlActive(false);
    for (const auto id : tickets)
    {
        Check(core.PollControl(id, sizeof(result), &result) == api::Error::Ok && result.status == api::Error::Unavailable,
            "shutdown completes pending requests with an explicit unavailable result");
        core.ReleaseControl(id);
    }
    Check(!State(core).available && core.SubmitControl(&pending, &ticket) == api::Error::Unavailable,
        "resident API queries remain safe after shutdown while execution is unavailable");
    core.ProcessPendingGuiCommands();
    Check(core.player.controller.initialPositionOffset.x == .2f,
        "shutdown prevents already queued requests from mutating state");
    Check(core.PollControl(1234567, sizeof(result), &result) == api::Error::NotFound,
        "unknown or released request IDs have an explicit result");

    BeginTest("control_api_restore_failure_is_retained");
    {
        GakuMotion::Core restore;
        Prepare(restore);
        restore.camera.captureCheckEnforced = true;
        restore.camera.owned = true;
        restore.player.state = PlaybackState::Playing;
        const auto sessionBeforeFailure = State(restore).session;
        Check(Run(restore, Command(restore, api::CommandKind::Stop)) == api::Error::Failed &&
            restore.player.state == PlaybackState::Stopped && restore.stageStoppedByTransport &&
            State(restore).session != sessionBeforeFailure && restore.camera.captureRestorePending,
            "Stop stops playback but reports unconfirmed camera restoration and retains recovery evidence");
        restore.SetControlActive(false);
    }
    {
        GakuMotion::Core restore;
        Prepare(restore);
        restore.camera.captureCheckEnforced = true;
        restore.camera.owned = true;
        auto disable = Command(restore, api::CommandKind::Camera);
        disable.camera.enabled = 0;
        Check(Run(restore, disable) == api::Error::Failed && !restore.camera.settings.enabled &&
            restore.camera.captureRestorePending,
            "camera disable reports restoration failure without clearing its pending recovery evidence");
        restore.SetControlActive(false);
    }

    BeginTest("scene_nodes_core_admission_and_existing_scene_control_ownership");
    {
        SceneNodesTest::Fixture fixture;
        fixture.StandardTree();
        GakuMotion::Core sceneCore;
        Prepare(sceneCore);
        sceneCore.player.state = PlaybackState::Preview;
        sceneCore.stageManager.api = fixture.manager.api;
        sceneCore.stageManager.candidates = fixture.manager.candidates;
        sceneCore.stageManager.candidatesDiscovered = true;
        sceneCore.RefreshSceneNodes();
        GakuMotion::scene_nodes::v1::State scene;
        sceneCore.stageManager.ReadSceneNodesState(sizeof(scene), &scene);
        std::uint64_t lease{};
        Check(sceneCore.AcquireSceneNodes(scene.catalogRevision, &lease) == GakuMotion::scene_nodes::v1::Status::Ok && lease,
            "Core admits scoped scene capture during an allowed performance preview");
        Check(Run(sceneCore, Command(sceneCore, api::CommandKind::Scene)) == api::Error::Busy,
            "existing Scene commands report Busy rather than false success while the node scope is leased");
        api::Capabilities sceneCaps;
        sceneCore.ReadControlCapabilities(sizeof(sceneCaps), &sceneCaps);
        Check(!(sceneCaps.availableMask & api::Mask(api::CommandKind::Scene)),
            "existing capabilities withhold conflicting scene presets during node ownership");
        sceneCore.captureSuspended = true;
        Check(sceneCore.ApplySceneNodes(lease, scene.catalogRevision, 0, nullptr) == GakuMotion::scene_nodes::v1::Status::Protected,
            "Core rechecks existing protection before any scene-node apply");
        sceneCore.RefreshSceneNodes();
        Check(sceneCore.stageManager.ReleaseSceneNodes(lease) == GakuMotion::scene_nodes::v1::Status::StaleCatalog,
            "protection revokes the lease through normal restoration and invalidates its old token");
        sceneCore.SetControlActive(false);
    }
}
