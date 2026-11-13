#pragma once

#include "GkStageStudioSettings.hpp"

static void TestStageStudioSettings(const std::string& repoRoot)
{
    BeginTest("developer studio settings: independent persisted lighting and local video intent");
    using namespace GakuMotion;
    namespace fs = std::filesystem;
    const auto root = fs::path(repoRoot) / "build/tests" / ("studio-settings-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto read = [](const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };
    const auto path = root / "GakuMotion/stage_studio.json";
    studio::Settings defaults;
    std::string error;
    Check(defaults.Validate(error) && !defaults.lighting.enabled && !defaults.video.enabled,
        "a fresh rig and video are disabled without requiring a video file");
    Core core;
    core.enforceInjectionPolicy = true;
    core.Initialize(MotionPathToUtf8(root));
    Check(core.studioSettings == defaults && !fs::exists(path),
        "missing studio settings preserve disabled defaults without creating enabled intent");

    studio::Settings custom;
    custom.lighting.enabled = true;
    custom.lighting.actorStyle = false;
    custom.lighting.decalEnabled = true;
    custom.lighting.keyColor = {.12f, .34f, .56f};
    custom.lighting.fillColor = {.78f, .65f, .43f};
    custom.lighting.rimColor = {.21f, .55f, .97f};
    custom.lighting.decalTint = {.4f, .6f, .8f};
    custom.lighting.keyIntensity = 2.3f;
    custom.lighting.fillIntensity = .4f;
    custom.lighting.rimIntensity = 3.2f;
    custom.lighting.keyYaw = -72;
    custom.lighting.keyPitch = 25;
    custom.lighting.fillYaw = 66;
    custom.lighting.fillPitch = -12;
    custom.lighting.rimYaw = 145;
    custom.lighting.rimPitch = 73;
    custom.lighting.actorIntensity = 1.2f;
    custom.lighting.rimPower = .7f;
    custom.lighting.decalIntensityScale = 2.6f;
    custom.video.enabled = true;
    custom.video.paused = true;
    custom.video.loop = false;
    custom.video.path = MotionPathToUtf8(fs::absolute(root / MotionPathFromUtf8("不存在的 本地视频.mp4")));
    custom.video.distance = 11;
    custom.video.width = 7.5f;
    custom.video.offsetX = -2;
    custom.video.offsetY = 1.4f;
    const auto revision = core.studioRevision;
    Check(core.QueueStudioSettings(custom), "a valid declaration can be queued before a Photography actor exists");
    core.QueueStageSettings(StageSettings{});
    core.ProcessPendingStageLoadCommands();
    Check(core.studioSettings == custom && core.studioRevision == revision + 1 &&
        core.guiCommands.size() == 1 && core.guiCommands.front().action == Core::GuiAction::StageSettings,
        "no-actor pump consumes only studio declarations and retains normal scene commands");
    core.ProcessPendingGuiCommands();
    Check(!core.InjectionAllowed() && core.player.state == PlaybackState::Stopped &&
        !core.stageManager.HasCustomizations(), "saving studio intent grants no injection and creates no current scene objects");
    studio::Settings loaded;
    Check(loaded.Load(MotionPathToUtf8(path), error) && loaded == custom,
        "all RGB, numeric, boolean and Unicode video path fields round-trip exactly");
    Core restarted;
    restarted.enforceInjectionPolicy = true;
    restarted.Initialize(MotionPathToUtf8(root));
    Check(restarted.studioSettings == custom && !restarted.InjectionAllowed(),
        "explicit studio intent survives restart without creating admission to a different scene");
    restarted.AbandonScene();
    Check(restarted.studioSettings == custom, "leaving a region retains the desired rig and local-video configuration");

    auto lightingOnly = restarted.studioSettings;
    lightingOnly.lighting.enabled = false;
    Check(restarted.SetStudioSettings(lightingOnly) && restarted.studioSettings.video == custom.video,
        "disabling lighting preserves the independent video declaration");
    const auto beforeRejected = read(path);
    const auto rejectedRevision = restarted.studioRevision;
    auto invalid = lightingOnly;
    invalid.lighting.keyIntensity = std::numeric_limits<float>::quiet_NaN();
    Check(!restarted.SetStudioSettings(invalid) && !restarted.QueueStudioSettings(invalid) &&
        restarted.studioSettings == lightingOnly && restarted.studioRevision == rejectedRevision && read(path) == beforeRejected,
        "invalid light input is rejected without overwriting persisted or in-memory intent");
#ifdef _WIN32
    HANDLE locked = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    Check(locked != INVALID_HANDLE_VALUE, "lock studio file against atomic replacement");
    if (locked != INVALID_HANDLE_VALUE)
    {
        restarted.ResetStudioSettings();
        Check(restarted.studioSettings == lightingOnly && restarted.studioRevision == rejectedRevision &&
            read(path) == beforeRejected, "failed reset save preserves the prior settings, bytes and runtime revision");
        CloseHandle(locked);
        bool temporaryLeft = false;
        for (const auto& entry : fs::directory_iterator(path.parent_path()))
            if (entry.path().filename().string().starts_with("stage_studio.json.") && entry.path().extension() == ".tmp") temporaryLeft = true;
        Check(!temporaryLeft, "failed save removes its own temporary studio file");
    }
#endif
    restarted.QueueStudioReset();
    restarted.ProcessPendingGuiCommands();
    Check(restarted.studioSettings == defaults && restarted.studioRevision == rejectedRevision + 1 &&
        read(path.string() + ".bak") == beforeRejected,
        "normal denied GUI pump persists reset before injection gating and backs up the old bytes");
    Core resetRestart;
    resetRestart.Initialize(MotionPathToUtf8(root));
    Check(resetRestart.studioSettings == defaults, "reset defaults remain disabled after restart");

    for (const auto& text : {
        "{", "[]", "{}", "{\"version\":2}", "{\"version\":1.5}",
        "{\"version\":1,\"lighting\":{\"enabled\":1}}",
        "{\"version\":1,\"lighting\":{\"keyIntensity\":9}}",
        "{\"version\":1,\"lighting\":{\"keyColor\":[1,0]}}",
        "{\"version\":1,\"lighting\":{\"fillColor\":[0,0,true]}}",
        "{\"version\":1,\"lighting\":{\"rimPitch\":91}}",
        "{\"version\":1,\"lighting\":{\"enabled\":true,\"enabled\":false}}",
        "{\"version\":1,\"lighting\":{\"unknown\":1}}",
        "{\"version\":1,\"video\":{\"path\":false}}",
        "{\"version\":1,\"video\":{\"enabled\":true}}",
        "{\"version\":1,\"video\":{\"path\":\"https://example.com/video.mp4\"}}",
        "{\"version\":1,\"video\":{\"path\":\"relative.mp4\"}}",
        "{\"version\":1,\"video\":{\"distance\":0}}"})
    {
        WriteTextFile(path, text);
        studio::Settings retained = custom;
        Check(!retained.Load(MotionPathToUtf8(path), error) && retained == custom,
            "malformed, mistyped, unknown, remote and out-of-range settings cannot partially overwrite a loaded preset");
        Core rejected;
        rejected.Initialize(MotionPathToUtf8(root));
        Check(rejected.studioSettings == defaults && read(path) == text && !rejected.studioVideoNotice.empty(),
            "invalid persisted studio settings fail closed without rewriting user bytes");
    }
    WriteTextFile(path, "{\"version\":1}");
    loaded = custom;
    Check(loaded.Load(MotionPathToUtf8(path), error) && loaded == defaults,
        "a versioned partial preset starts from disabled defaults rather than stale prior fields");
    fs::remove_all(root);
}
