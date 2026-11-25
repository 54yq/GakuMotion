static void TestSceneActorSelection()
{
	BeginTest("scene actor roster and target selection lifetime");
	{
		live_aspect::Projection p{};
		p[5] = 2.08607f;
		p[0] = p[5] / (16.0f / 9.0f);
		p[8] = 0.15f; p[9] = -0.1f; p[10] = -1.01f; p[11] = -1.0f; p[14] = -0.1f;
		const auto original = p;
		Check(live_aspect::PreserveHorizontalFraming(p, 3840.0f / 1634.0f, 16.0f / 9.0f) &&
			std::abs(p[0] - original[5] * (1634.0f / 3840.0f)) < 0.0001f &&
			std::abs(p[5] * 2160.0f - original[5] * 1634.0f) < 0.001f &&
			p[8] == original[8] && std::abs(p[9] * 2160.0f - original[9] * 1634.0f) < 0.001f &&
			p[10] == original[10] && p[11] == original[11] && p[14] == original[14],
			"taller Live target preserves the old content scale, width, lens shift and depth");
		Check(!live_aspect::PreserveHorizontalFraming(p, 16.0f / 9.0f, 16.0f / 9.0f),
			"unchanged aspect is not expanded");
		const float wide = 2.35f, target = 16.0f / 9.0f;
		auto wideNative = original;
		wideNative[0] = wideNative[5] / wide;
		auto resized = wideNative;
		resized[5] = resized[0] * target;
		const float scale = live_aspect::HorizontalScaleAfterSwitch(wideNative, resized, wide, target);
		Check(std::abs(scale - 1.f) < 0.0001f &&
			live_aspect::PreserveHorizontalFraming(resized, wide, target, scale) &&
			std::abs(resized[0] - wideNative[0]) < 0.0001f,
			"native 16:9 lens already preserving width does not receive a second zoom correction");
		auto explicitWide = wideNative;
		Check(live_aspect::PreserveHorizontalFraming(explicitWide, wide, target) &&
			explicitWide[0] == wideNative[0] &&
			std::abs(explicitWide[5] / explicitWide[0] - target) < 0.0001f,
			"wide projection surviving the native switch is adapted without changing horizontal magnification");
	}
	Check(live_actor_props::IsSupportedPair("Root_Microphone","Prop_Microphone") &&
		live_actor_props::IsSupportedPair("Root_Penlightklljduo","Prop_Penlightklljduo") &&
		!live_actor_props::IsSupportedPair("Root_Body","Prop_Body") &&
		!live_actor_props::IsSupportedPair("Root_Microphone","Prop_Body"),
		"only matched prop roots, not protected actor body nodes, are supported");

    {
        struct Node { std::string name; std::vector<void*> children; };
        Node microphone{"Prop_Microphone"};
        Node suitcase{"Prop_Hknesuitcase"}, geometry{"Geo_Hknesuitcase"};
        Node penlight{"Prop_Penlightklljduo"};
        Node waterGun{"Prop_Water_Gun"}, candy{"Prop_CharacterCandy"}, pinwheel{"Prop_Pinwheel"};
        Node body{"Prop_Body"}, duplicate{"Prop_Duplicate"}, duplicate2{"Prop_Duplicate"};
        Node unrelated{"Prop_StageOnly"}, foreignProp{"Prop_Penlight"};
        Node attachedLight{"Prop_Lightstick(Clone)"}, cloneCandy{"Prop_Lollipop(Clone)"};
        Node rightHand{"Root_Hand_R", {&attachedLight}}, cloneRoot{"Root_Lollipop(Clone)", {&cloneCandy}};
        Node rootMic{"Root_Microphone", {&microphone}};
        Node rootBag{"Root_Hknesuitcase", {&suitcase, &geometry}};
        Node rootLight{"Root_Penlightklljduo", {&penlight}};
        Node rootGun{"Root_Water_Gun", {&waterGun}}, rootCandy{"Root_CharacterCandy", {&candy}};
        Node rootPinwheel{"Root_Pinwheel", {&pinwheel}};
        Node rootBody{"Root_Body", {&body}}, rootDuplicate{"Root_Duplicate", {&duplicate, &duplicate2}};
        Node rootForeign{"Root_Penlight", {&foreignProp}};
        Node foreignActor{"Actor", {&rootForeign}}, wrapper{"Model", {&rootBag, &rootLight, &rootGun, &rootCandy, &rootPinwheel, &rootBody, &rootDuplicate}};
        Node actor{"Actor", {&wrapper, &rootMic, &unrelated, &foreignActor, &rightHand, &cloneRoot}};
        const auto props = live_actor_props::ScanActorProps(&actor,
            [](void* node) { return static_cast<Node*>(node)->children; },
            [](void* node) { return static_cast<Node*>(node)->name; },
            [&](void* node) { return node == &foreignActor; });
        const bool complete = props.size() == 8 && std::all_of(props.begin(), props.end(), [&](const auto& prop) {
            return prop.object == &microphone || prop.object == &suitcase || prop.object == &penlight ||
                prop.object == &waterGun || prop.object == &candy || prop.object == &pinwheel ||
                (prop.object == &attachedLight && prop.root == &rightHand && prop.rootName.empty()) ||
                (prop.object == &cloneCandy && prop.root == &cloneRoot && prop.rootName == cloneRoot.name);
        });
        Check(complete, "find direct and nested character props, excluding protected, ambiguous and other-actor nodes");
        Check(live_actor_props::DisplayName("Prop_Water_Gun") == "水枪（Water_Gun）" &&
            live_actor_props::DisplayName("Prop_CharacterCandy") == "糖果（CharacterCandy）" &&
            live_actor_props::DisplayName("Prop_Pinwheel") == "Prop_Pinwheel",
            "known prop types are localized and other names remain identifiable");
    }
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
		nameCore.actorProps = {
			{named.sceneActors[0].token,"Root_Microphone","Prop_Microphone","秦谷美铃",1010,true,true},
			{named.sceneActors[1].token,"Root_Penlightklljduo","Prop_Penlightklljduo","姬崎莉波",1020,false,true}
		};
		auto propContext = nameCore.BuildGuiContext();
		const auto micKey = ActorPropKey(propContext.actorProps[0]);
		const auto lightKey = ActorPropKey(propContext.actorProps[1]);
		propContext.fnSetActorPropHidden(propContext.self,named.sceneActors[1].token,named.actorRosterEpoch,lightKey.c_str(),true);
		nameCore.ProcessPendingGuiCommands();
		propContext = nameCore.BuildGuiContext();
		Check(!propContext.actorProps[0].hidden && propContext.actorProps[1].hidden,
			"prop visibility is isolated to the discovered actor and object");
		propContext.fnSetActorPropHidden(propContext.self,named.sceneActors[1].token,named.actorRosterEpoch+1,lightKey.c_str(),false);
		propContext.fnSetActorPropHidden(propContext.self,named.sceneActors[0].token,named.actorRosterEpoch,micKey.c_str(),true);
		nameCore.actorProps[0].instanceId = 1011;
		nameCore.ProcessPendingGuiCommands();
		propContext = nameCore.BuildGuiContext();
		Check(!nameCore.actorPropHidden.contains(micKey) && propContext.actorProps[1].hidden,
			"replacement props and stale actor roster commands cannot alter visibility");
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
