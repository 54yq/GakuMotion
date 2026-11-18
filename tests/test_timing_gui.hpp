// Included by the GUI interaction harness after Harness is defined.
// Exercise real widgets and Core's queued playback-rate callback.
MotionImportRequest timingGuiSubmittedRequest;
int timingGuiSubmitCount = 0;
void CaptureTimingGuiImport(void*, const MotionImportRequest& request)
{
	timingGuiSubmittedRequest = request;
	++timingGuiSubmitCount;
}

void TestPlaybackAndTrackTimingGui(const std::filesystem::path& root)
{
	Harness app(root);
	app.Settle();
	for (int choice = 0; choice < IM_ARRAYSIZE(kTransportPlaybackSpeeds); ++choice)
	{
		const bool selected = app.Click("##playback_speed") && app.Click(kTransportPlaybackSpeedLabels[choice]);
		app.Settle();
		Check(selected && std::fabs(app.core.player.playbackSpeedMultiplier - kTransportPlaybackSpeeds[choice]) < .0001f,
			"transport playback-rate dropdown submits its supported choice to Core");
	}

	PerformanceTiming configured;
	configured.motion = {1.2f, 3.0f};
	configured.facial = {2.0f, -1.0f};
	configured.camera = {0.0f, -0.5f};
	configured.audio = {1.0f, 2.0f};
	app.core.motions[0].timing = configured;
	++app.core.libraryRevision;
	app.contextPatch = [](GuiContext& context) { context.fnImportMotion = CaptureTimingGuiImport; };
	app.Settle();
	app.Click("管理选中");
	app.Settle();
	Check(g_libraryModal.isOpen && g_libraryModal.timing.motion.speed == 1.2f &&
		g_libraryModal.timing.facial.offsetSeconds == -1.0f && g_libraryModal.timing.camera.speed == 0.0f &&
		g_libraryModal.timing.audio.offsetSeconds == 2.0f,
		"manage modal restores independent timings for all four imported files");

	ImRect file, speed, offset;
	const bool fields = app.Find("##track_speed", speed) && app.Find("##track_offset", offset);
	const auto* modal = ImGui::FindWindowByName("管理曲目##modal");
	Check(fields && modal && modal->Size.x > 800.0f && offset.Min.x > speed.Max.x &&
		offset.Max.x <= modal->InnerClipRect.Max.x + .5f,
		"manage modal is wider and places speed and signed-offset fields to the right of each file");
	int speeds = 0, offsets = 0;
	for (const auto id : order)
	{
		speeds += items[id].label == "##track_speed";
		offsets += items[id].label == "##track_offset";
	}
	Check(speeds == 4 && offsets == 4, "each motion/facial/camera/audio row exposes both timing inputs");

	app.Type("##track_speed", "-2");
	app.Key(ImGuiKey_Enter);
	Check(g_libraryModal.timing.motion.speed == 0.0f, "track speed input clamps negative entries to zero");
	app.Type("##track_offset", "-0.25");
	app.Key(ImGuiKey_Enter);
	Check(g_libraryModal.timing.motion.offsetSeconds == -0.25f, "track initial offset accepts negative seconds");
	timingGuiSubmitCount = 0;
	app.Click("保存修改");
	Check(timingGuiSubmitCount == 1 && timingGuiSubmittedRequest.editKey == "A" &&
		timingGuiSubmittedRequest.timing.motion.speed == 0.0f &&
		timingGuiSubmittedRequest.timing.motion.offsetSeconds == -0.25f &&
		timingGuiSubmittedRequest.timing.facial.speed == 2.0f &&
		timingGuiSubmittedRequest.timing.camera.offsetSeconds == -0.5f &&
		timingGuiSubmittedRequest.timing.audio.offsetSeconds == 2.0f,
		"manage save submits edited and unchanged per-file timing values without requiring replacement files");

	app.core.gui.isImporting = false;
	g_libraryModal.SetupForNew({});
	app.Settle();
	bool defaults = true;
	for (const auto timing : {g_libraryModal.timing.motion, g_libraryModal.timing.facial,
		g_libraryModal.timing.camera, g_libraryModal.timing.audio})
		defaults = defaults && timing.speed == 1.0f && timing.offsetSeconds == 0.0f;
	Check(defaults, "new import resets all per-file timing values to speed one and offset zero");
	app.Click("取消");
	app.Settle();
}
