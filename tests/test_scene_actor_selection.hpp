static void TestSceneActorSelection()
{
	BeginTest("scene actor roster and target selection lifetime");
	Check(live_actor_props::IsSupportedPair("Root_Microphone","Prop_Microphone") &&
		live_actor_props::IsSupportedPair("Root_Penlightklljduo","Prop_Penlightklljduo") &&
		!live_actor_props::IsSupportedPair("Root_Body","Prop_Body") &&
		!live_actor_props::IsSupportedPair("Root_Microphone","Prop_Body"),
		"only matched prop roots, not protected actor body nodes, are supported");
	Check(DetectIdolCharacterCode("MDL_CHR_KRNH-base-0000_face(Clone)") == "krnh" &&
		FormatIdolDisplayName("krnh").find("贺阳燐羽") != std::string::npos,
		"replacement identity and Chinese naming do not depend on a facial baseline existing");
	Check(DetectIdolCharacterCode("xkrnhx").empty() && DetectIdolCharacterCode("chr_krnh_ttmr").empty(),
		"unknown substrings and ambiguous character codes are not assigned a character name");
	Check(GakuActorsPresetName("krnh | GakuActors[2] | preset:我的燐羽 hski") == "我的燐羽 hski" &&
		GakuActorsPresetName("unknown | GakuActors[0] | preset:特殊人物") == "特殊人物" &&
		GakuActorsPresetName("gakuactors | GakuActors[3] | preset:自定义") == "自定义" &&
		GakuActorsPresetName("krnh | GakuActors[2] | preset:").empty() &&
		GakuActorsPresetName("krnh | GakuActors[x] | preset:错误").empty() &&
		GakuActorsPresetName("正常人物").empty(), "custom preset name is decoded only from the actor marker");
	const IdolName expected[] = {
		{"amao", "有村麻央"}, {"andk", "蓝井抚子"}, {"atbm", "雨夜燕"},
		{"fktn", "藤田琴音"}, {"hkne", "冰渡香名江"}, {"hmsz", "秦谷美铃"},
		{"hrnm", "姬崎莉波"}, {"hski", "花海咲季"}, {"hume", "花海佑芽"},
		{"jkno", "十王邦夫"}, {"jsna", "十王星南"}, {"kcna", "仓本千奈"},
		{"kllj", "葛城莉莉娅"}, {"krnh", "贺阳燐羽"}, {"ktko", "黑井崇男"},
		{"myu0", "真城优"}, {"nasr", "根绪亚纱里"}, {"sgka", "白草月花"},
		{"shro", "筱泽广"}, {"ssmk", "紫云清夏"}, {"sson", "白草四音"},
		{"trda", "舞蹈训练员"}, {"trvi", "视觉训练员"}, {"trvo", "歌唱训练员"},
		{"ttmr", "月村手毬"}
	};
	Check(std::size(expected) == std::size(kIdolNames), "character mapping covers every local character code");
	for (const auto& item : expected) {
		const std::string model = std::string("mdl_chr_") + item.code + "-base-0000_face(Clone)";
		const char* name = LookupIdolChineseName(item.code);
		Check(DetectIdolCharacterCode(model) == item.code && name && std::string(name) == item.chinese &&
			FormatIdolDisplayName(item.code) == std::string(item.chinese) + " (" + item.code + ")",
			"model code resolves to its matching local character name");
	}
	Core core;
	void* a = reinterpret_cast<void*>(0x7100);
	void* b = reinterpret_cast<void*>(0x7200);
	{
		Core nameCore;
		nameCore.PublishSceneActors({{a, 101, "hmsz"}, {b, 102, "hrnm"}});
		const auto named = nameCore.BuildGuiContext();
		Check(named.sceneActors.size() == 2 && named.sceneActors[0].label.find("秦谷美铃") != std::string::npos &&
			named.sceneActors[1].label.find("姬崎莉波") != std::string::npos &&
			nameCore.sceneActorRegistry.Resolve(named.sceneActors[0].token, named.actorRosterEpoch) == a &&
			nameCore.sceneActorRegistry.Resolve(named.sceneActors[1].token, named.actorRosterEpoch) == b,
			"Misuzu and Rinami labels remain attached to their own actor tokens");
		nameCore.PublishSceneActors({{a,101,"hmsz","我的预设"},{b,102,"hrnm"}});
		const auto customized=nameCore.BuildGuiContext();
		Check(customized.sceneActors[0].label.find("我的预设")!=std::string::npos &&
			customized.sceneActors[1].label.find("姬崎莉波")!=std::string::npos &&
			nameCore.sceneActorRegistry.Entries()[0].character=="hmsz", "custom display keeps the actual character identity and other actors unchanged");
		const auto unchangedEpoch=nameCore.sceneActorRegistry.Epoch();
		nameCore.PublishSceneActors({{a,101,"hmsz","改名后"},{b,102,"hrnm"}});
		Check(nameCore.sceneActorRegistry.Epoch()==unchangedEpoch && nameCore.BuildGuiContext().sceneActors[0].label.find("改名后")!=std::string::npos,
			"renaming updates display without invalidating actor tokens");
		nameCore.liveTakeoverActive=true;
		nameCore.actorProps.push_back({named.sceneActors[1].token,"Root_Microphone","Prop_Microphone","姬崎莉波",false});
		named.fnSetMicrophoneHidden(named.self,named.sceneActors[0].token,named.actorRosterEpoch,true);
		nameCore.ProcessPendingGuiCommands();
		auto micContext=nameCore.BuildGuiContext();
		Check(micContext.sceneActors[0].microphoneHidden && !micContext.sceneActors[1].microphoneHidden,
			"microphone choice is isolated to its actor token");
		const auto propKey=ActorPropKey(named.sceneActors[1].token,"Root_Microphone","Prop_Microphone");
		micContext.fnSetActorPropHidden(micContext.self,named.sceneActors[1].token,named.actorRosterEpoch,propKey.c_str(),true);
		nameCore.ProcessPendingGuiCommands(); micContext=nameCore.BuildGuiContext();
		Check(micContext.sceneActors[1].microphoneHidden && micContext.actorProps[0].hidden,
			"stage prop control and microphone switch share the same actor state");
		const auto forbidden=ActorPropKey(named.sceneActors[1].token,"Root_Body","Prop_Body");
		micContext.fnSetActorPropHidden(micContext.self,named.sceneActors[1].token,named.actorRosterEpoch,forbidden.c_str(),true);
		micContext.fnSetMicrophoneHidden(micContext.self,named.sceneActors[1].token,named.actorRosterEpoch+1,false);
		nameCore.ProcessPendingGuiCommands(); micContext=nameCore.BuildGuiContext();
		Check(!nameCore.actorPropHidden.count(forbidden) && micContext.sceneActors[1].microphoneHidden,
			"unknown props and stale roster commands cannot alter actor state");
		nameCore.PublishSceneActors({{b, 102, "hrnm"}});
		Check(nameCore.actorPropHidden.empty() && nameCore.actorProps.empty(), "actor roster changes discard stale prop choices");
	}
	core.PublishSceneActors({{a, 101, "hski"}, {b, 102, "ttmr"}, {a, 101, "hski"}});
	auto context = core.BuildGuiContext();
	Check(context.sceneActors.size() == 2 && core.player.controller.targetActor == a,
		"fresh actual actors are deduplicated and the first actor is selected automatically");
	Check(context.sceneActors[0].label.find("花海咲季") != std::string::npos && context.sceneActors[1].label.find("月村手毬") != std::string::npos,
		"scene entries use the existing character-name table");
	const auto token = context.sceneActors[1].token;
	const auto epoch = context.actorRosterEpoch;
	core.PublishSceneActors({{b, 102, "ttmr"}, {a, 101, "hski"}});
	Check(core.sceneActorRegistry.Epoch() == epoch && core.sceneActorRegistry.Resolve(token, epoch) == b,
		"Unity search order changes preserve stable choices");
	core.player.state = PlaybackState::Paused;
	core.player.currentTime = .45f;
	core.player.controller.bindings[a].actor = a;
	core.player.controller.currentBinding.actor = a;
	core.fnBeforeTargetChange = [] { return false; };
	context.fnSelectTargetActor(context.self, token, epoch);
	core.ProcessPendingGuiCommands();
	Check(core.player.controller.targetActor == a && !core.player.controller.bindings.empty(),
		"unconfirmed secondary physics restoration rejects switching without discarding ownership");
	core.fnBeforeTargetChange = nullptr;
	context.fnSelectTargetActor(context.self, token, epoch);
	Check(core.player.controller.targetActor == a, "GUI selection waits for the main-thread queue");
	core.ProcessPendingGuiCommands();
	Check(core.player.controller.targetActor == b && core.player.controller.bindings.empty() &&
		!core.player.controller.currentBinding.actor && core.player.currentTime == .45f,
		"switching releases the previous body/face binding while keeping the playback position");
	context.fnSelectTargetActor(context.self, context.sceneActors[0].token, epoch);
	core.PublishSceneActors({{b, 102, "ttmr"}});
	core.ProcessPendingGuiCommands();
	Check(core.player.controller.targetActor == b && core.KnownActorCount() == 1,
		"a removed actor disappears and a queued old-roster choice is rejected");
	const auto old = core.BuildGuiContext();
	core.PublishSceneActors({{b, 202, "hmsz"}}); // Same native address, new Unity instance.
	Check(core.player.state == PlaybackState::Stopped && core.sceneActorRegistry.Resolve(old.sceneActors[0].token, old.actorRosterEpoch) == nullptr,
		"pointer reuse is a new actor and cannot inherit the old playback or token");
	core.PublishSceneActors({});
	Check(core.BuildGuiContext().sceneActors.empty() && !core.player.controller.targetActor && !core.IsTargetActor(a),
		"an empty scene offers no entries and never means drive all actors");
	core.PublishSceneActors({{a, 301, ""}, {b, 302, ""}});
	const auto priorScene = core.BuildGuiContext();
	core.AbandonScene();
	core.PublishSceneActors({{a, 301, "hski"}});
	Check(!core.sceneActorRegistry.Resolve(priorScene.sceneActors[0].token, priorScene.actorRosterEpoch),
		"scene boundaries invalidate old commands even if addresses and instance IDs are reused");
}
