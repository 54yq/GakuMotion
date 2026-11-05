#pragma once

namespace ActorlessLibraryFixture
{
    // Staging owns this two-frame synthetic clip; no recorded motion fixture is used.
    inline void WriteClip(const std::filesystem::path& path)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        const auto write = [&](const auto& value) { file.write(reinterpret_cast<const char*>(&value), sizeof(value)); };
        file.write("GKCLIPCL", 8);
        write(uint32_t{4}); write(60.0f); write(uint32_t{2}); write(2.0f / 60.0f); write(uint32_t{1}); write(.083f);
        write(uint16_t{0}); write(uint8_t{4}); write(uint8_t{0}); file.write("Hips", 4);
        for (uint32_t frame = 0; frame < 2; ++frame)
        {
            write(GakuMotion::Vector3{});
            write(GakuMotion::Quaternion::Identity());
        }
        write(uint8_t{0}); write(uint32_t{0});
    }

    inline int starts = 0, polls = 0, outcome = 1, cameraStarts = 0, cameraPolls = 0;
    inline bool startAllowed = true;
    inline GakuMotion::MotionImportRequest submitted;
    inline bool Start(const GakuMotion::MotionImportRequest& request, std::string& message)
    {
        ++starts;
        submitted = request;
        message = startAllowed ? "" : "synthetic backend unavailable";
        return startAllowed;
    }
    inline int Poll(std::string& message, GakuMotion::ImportProgressState* progress)
    {
        ++polls;
        if (polls == 1)
        {
            progress->progress = .4f;
            progress->stage = "PARSE";
            progress->statusText = "synthetic parsing";
            return 0;
        }
        message = outcome > 0 ? "synthetic import finished" : "synthetic import failed";
        return outcome;
    }
    inline bool StartCamera(const GakuMotion::CameraImportRequest&, std::string&)
    {
        ++cameraStarts;
        return true;
    }
    inline int PollCamera(std::string& message)
    {
        ++cameraPolls;
        message = "synthetic camera finished";
        return 1;
    }
}

static void TestActorlessLibraryCommands(const std::string& repoRoot)
{
    BeginTest("host frames start and finish library jobs before an actor exists");
    using namespace ActorlessLibraryFixture;
    namespace fs = std::filesystem;
    const auto root = fs::path(repoRoot) / "build" / "tests" / ("actorless_import_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "motions");
    fs::create_directories(root / "logs");
    const auto configure = [&](GakuMotion::Core& core) {
        core.motionsRoot = MotionPathToUtf8(root / "motions");
        core.logsRoot = MotionPathToUtf8(root / "logs");
        core.transactionRoot = MotionPathToUtf8(root / "transactions");
        core.playlistPath = MotionPathToUtf8(root / "playlist.json");
        core.fnStartImport = Start;
        core.fnPollImportProgress = Poll;
    };
    starts = polls = cameraStarts = cameraPolls = 0;
    startAllowed = true;
    outcome = 1;
    {
        GakuMotion::Core core;
        configure(core);
        auto context = core.BuildGuiContext();
        GakuMotion::MotionImportRequest request;
        request.vmd = "synthetic-motion.vmd";
        request.title = "actorless import";
        context.fnImportMotion(context.self, request);
        Check(starts == 0 && core.guiCommands.size() == 1 && !core.currentActor,
            "the real GUI import callback queues work without requiring a bound actor");
        core.ProcessPendingLibraryCommands();
        Check(starts == 1 && core.importBusy && polls == 1 && core.guiCommands.empty() &&
            submitted.vmd == request.vmd && submitted.motionsRoot == core.motionsRoot &&
            submitted.log == core.logsRoot + "/import.log",
            "a host-only frame starts the queued importer once with managed destinations");
        Check(core.importProgress.active && !core.importProgress.finished && core.importProgress.progress == .4f,
            "running native progress reaches the Core without actor callbacks");
        core.QueuePlay();
        core.QueueSeek(4.0f);
        core.ProcessPendingLibraryCommands();
        context = core.BuildGuiContext();
        Check(starts == 1 && polls == 2 && !core.importBusy && context.importProgress->finished &&
            context.importProgress->succeeded && context.importProgress->progress == 1.0f,
            "host polling acknowledges completion and exposes terminal progress while actor commands wait");
        const auto revision = core.libraryRevision;
        core.ProcessPendingLibraryCommands();
        Check(starts == 1 && polls == 2 && core.libraryRevision == revision && core.guiCommands.size() == 2 &&
            core.guiCommands[0].action == GakuMotion::Core::GuiAction::Play &&
            core.guiCommands[1].action == GakuMotion::Core::GuiAction::Seek,
            "later host frames neither repeat the import nor consume or reorder playback commands");
    }
    {
        GakuMotion::Core core;
        configure(core);
        starts = polls = 0;
        startAllowed = false;
        GakuMotion::MotionImportRequest request;
        request.vmd = "synthetic-motion.vmd";
        auto context = core.BuildGuiContext();
        context.fnImportMotion(context.self, request);
        core.ProcessPendingLibraryCommands();
        Check(starts == 1 && polls == 0 && !core.importBusy && core.importProgress.finished &&
            !core.importProgress.succeeded && core.importProgress.errorDetail == "synthetic backend unavailable",
            "a backend startup failure becomes a terminal UI error without an actor");
        startAllowed = true;
        outcome = -1;
        context.fnImportMotion(context.self, request);
        core.ProcessPendingLibraryCommands();
        core.ProcessPendingLibraryCommands();
        Check(starts == 2 && polls == 2 && !core.importBusy && core.importProgress.finished &&
            !core.importProgress.succeeded && core.importProgress.errorDetail == "synthetic import failed",
            "a running import failure becomes a terminal UI error without actor callbacks");
    }
    {
        GakuMotion::Core core;
        configure(core);
        starts = polls = 0;
        GakuMotion::MotionImportRequest request;
        request.vmd = "synthetic-motion.vmd";
        core.QueueStop();
        auto context = core.BuildGuiContext();
        context.fnImportMotion(context.self, request);
        core.ProcessPendingLibraryCommands();
        Check(starts == 0 && core.guiCommands.size() == 2 &&
            core.guiCommands.front().action == GakuMotion::Core::GuiAction::Stop &&
            core.guiCommands.back().action == GakuMotion::Core::GuiAction::Import,
            "file commands cannot overtake an earlier Stop or consume its following actor-dependent sequence");
    }
    {
        GakuMotion::Core core;
        configure(core);
        ActorlessLibraryFixture::WriteClip(root / "motions" / "new-song" / "motion.gkclip");
        auto context = core.BuildGuiContext();
        context.fnRefreshMotions(context.self);
        core.ProcessPendingLibraryCommands();
        if (core.libraryScan.valid()) core.libraryScan.wait();
        core.ProcessPendingLibraryCommands();
        Check(!core.libraryScan.valid() && FindPlaylistIndex(core.motions, "new-song") >= 0 && !core.currentActor,
            "host-only frames start and apply asynchronous library refresh without selecting an actor");
        core.fnStartCameraImport = StartCamera;
        core.fnPollCameraImport = PollCamera;
        GakuMotion::CameraImportRequest request;
        request.motionKey = "new-song";
        context = core.BuildGuiContext();
        context.fnImportCamera(context.self, request);
        core.ProcessPendingLibraryCommands();
        core.ProcessPendingLibraryCommands();
        Check(cameraStarts == 1 && cameraPolls == 1 && !core.cameraImportBusy &&
            core.cameraNotice == "synthetic camera finished" && !core.currentActor,
            "camera file import starts and finishes once on host frames without binding a dancer");
    }
    fs::remove_all(root); // Unique synthetic fixture owned by this test.
}
