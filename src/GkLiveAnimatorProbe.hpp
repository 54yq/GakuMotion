#pragma once
// Included inside the plugin's anonymous namespace. Read-only snapshots on the
// Unity thread; no Animator/graph mutations or cached managed instances.
namespace LiveAnimatorProbe
{
	inline const void *speedMethod = nullptr, *graphMethod = nullptr, *graphValidMethod = nullptr, *graphPlayingMethod = nullptr, *graphModeMethod = nullptr;
	inline bool resolved = false;
	inline uintptr_t observedPlayer{}, observedActor{}, observedOwner{};
	inline int observedScene{};
	inline void Resolve()
	{
		if (resolved || !g_gameAssembly || !g_animatorKlass) return;
		resolved = true;
		const auto api = GroundShadowProbeBootstrap::Resolve(g_gameAssembly);
		void* graph = FindClass("UnityEngine.DirectorModule.dll", "UnityEngine.Playables", "PlayableGraph");
		if (!graph) graph = FindClass("UnityEngine.CoreModule.dll", "UnityEngine.Playables", "PlayableGraph");
		speedMethod = api.method.Find(g_animatorKlass, "get_speed", false, {0x0c}, {});
		if (!graph) return;
		graphMethod = api.method.Find(g_animatorKlass, "get_playableGraph", false, {0x11, graph}, {});
		graphValidMethod = api.method.Find(graph, "IsValid", false, {0x02}, {});
		graphPlayingMethod = api.method.Find(graph, "IsPlaying", false, {0x02}, {});
		void* mode = FindClass("UnityEngine.DirectorModule.dll", "UnityEngine.Playables", "DirectorUpdateMode");
		if (!mode) mode = FindClass("UnityEngine.CoreModule.dll", "UnityEngine.Playables", "DirectorUpdateMode");
		if (mode) graphModeMethod = api.method.Find(graph, "GetTimeUpdateMode", false, {0x11, mode}, {});
	}
	inline int ReadBool(const void* method, void* object)
	{
		if (!method || !object || !g_faceRuntime.invoke || !g_safeBones.unbox) return -1;
		void* ex = nullptr;
		void* boxed = g_faceRuntime.invoke(method, object, nullptr, &ex);
		void* raw = boxed && !ex ? g_safeBones.unbox(boxed) : nullptr;
		return raw ? int(*static_cast<bool*>(raw)) : -1;
	}
	// The actor's physics job is inserted into VL.Animation's separately owned
	// graph. Animator.playableGraph alone does not describe this graph. Resolve
	// the component and player afresh, without retaining scene objects or writes.
	inline void SampleActorGraph(void* actor, int phase, int clockSlot = 0, LiveDynamicFollowerClock* followerClock = nullptr)
	{
		if (!actor || !g_getComponentInChildrenNative || !il2cpp_class_get_type ||
			!il2cpp_type_get_object || !il2cpp_class_get_parent || !g_faceRuntime.objectClass ||
			!g_faceRuntime.findField || !g_faceRuntime.fieldFlags || !g_faceRuntime.fieldType ||
			!g_faceRuntime.typeCode || !g_faceRuntime.readField || !g_faceRuntime.invoke || !g_safeBones.unbox) return;
		void* componentClass = FindClass("campus-submodule.Runtime.dll", "Campus.Common", "CampusActorAnimation");
		if (!componentClass) componentClass = FindClass("Assembly-CSharp.dll", "Campus.Common", "CampusActorAnimation");
		void* playerBase = nullptr;
		void* graphClass = FindClass("UnityEngine.DirectorModule.dll", "UnityEngine.Playables", "PlayableGraph");
		if (!graphClass) graphClass = FindClass("UnityEngine.CoreModule.dll", "UnityEngine.Playables", "PlayableGraph");
		void* type = componentClass ? il2cpp_class_get_type(componentClass) : nullptr;
		void* typeObject = type ? il2cpp_type_get_object(type) : nullptr;
		void* component = typeObject ? g_getComponentInChildrenNative(actor, typeObject) : nullptr;
		void* player = nullptr;
		if (component && g_faceRuntime.objectClass(component) == componentClass)
		{
			void* field = g_faceRuntime.findField(componentClass, "_animationPlayer");
			void* fieldType = field ? g_faceRuntime.fieldType(field) : nullptr;
			const int code = fieldType ? g_faceRuntime.typeCode(fieldType) : 0;
			if (field && !(g_faceRuntime.fieldFlags(field) & 0x10) && (code == 0x12 || code == 0x1c))
				g_faceRuntime.readField(component, field, &player);
		}
		void* actualClass = player ? g_faceRuntime.objectClass(player) : nullptr;
		using ClassText = const char* (*)(void*);
		const auto className = reinterpret_cast<ClassText>(GetProcAddress(g_gameAssembly, "il2cpp_class_get_name"));
		const auto classNamespace = reinterpret_cast<ClassText>(GetProcAddress(g_gameAssembly, "il2cpp_class_get_namespace"));
		if (className && classNamespace)
		{
			for (void* c = actualClass; c; c = il2cpp_class_get_parent(c))
			{
				const char* name = className(c); const char* ns = classNamespace(c);
				if (name && ns && std::strcmp(name, "AnimationPlayerBase") == 0 &&
					std::strcmp(ns, "VL.Animation") == 0) { playerBase = c; break; }
			}
		}
		const bool isPlayer = playerBase != nullptr;
		const auto metadata = GroundShadowProbeBootstrap::Resolve(g_gameAssembly).method;
		void* motionClass = nullptr;
		if (className && classNamespace) for (void* c = actualClass; c; c = il2cpp_class_get_parent(c)) {
			const char* name = className(c); const char* ns = classNamespace(c);
			if (name && ns && std::strcmp(name, "MotionDefinePlayer") == 0 &&
				std::strcmp(ns, "VL.Animation") == 0) { motionClass = c; break; }
		}
		LiveCodeCapture::Class(motionClass);
		LiveCodeCapture::Capture(componentClass, actualClass, playerBase,
            reinterpret_cast<void*>(GakuMotion::Core::Instance().liveOwnerToken));
		LiveSchedulingProbe::InstallActor(motionClass, metadata);
		// Identify exact candidate methods without invoking or detouring them.
		// The scene end check has to be verified before any suppression can be
		// considered; an async end sequence cannot be cancelled by guesswork.
		static bool reportedCandidates = false;
		if (!reportedCandidates && actualClass && playerBase && g_liveApi.presenterClass && className && classNamespace)
		{
			const bool endBool = metadata.Find(g_liveApi.presenterClass, "CheckLiveEnd", false, {0x02}, {{0x0c}}) != nullptr;
			const bool endVoid = metadata.Find(g_liveApi.presenterClass, "CheckLiveEnd", false, {0x01}, {{0x0c}}) != nullptr;
			const bool onEndVoid = metadata.Find(g_liveApi.presenterClass, "OnEndLiveSequence", false, {0x01}, {}) != nullptr;
			const bool evalDelta = motionClass && metadata.Find(motionClass, "Evaluate", false, {0x01}, {{0x0c}});
			const bool evalNoArgs = motionClass && metadata.Find(motionClass, "Evaluate", false, {0x01}, {});
			LiveSchedulingProbe::InstallEnd(g_liveApi.presenterClass, metadata);
			const char* name = className(actualClass); const char* ns = classNamespace(actualClass);
			if (g_hostContext.logInfo) g_hostContext.logInfo("[GakuMotion][LiveSchedulingCandidates] player=%s.%s motion_define=%d evaluate_float_void=%d evaluate_void=%d check_end_float_bool=%d check_end_float_void=%d on_end_void=%d; metadata only",
				ns ? ns : "?", name ? name : "?", int(motionClass != nullptr), int(evalDelta), int(evalNoArgs), int(endBool), int(endVoid), int(onEndVoid));
			reportedCandidates = true;
		}
		const void* getGraph = isPlayer && graphClass ? metadata.Find(playerBase, "GetPlayableGraph", false, {0x11, graphClass}, {}) : nullptr;
		int valid = -1, playing = -1, mode = -1, outputs = -1, targetMatches = -1;
		if (getGraph)
		{
			void* ex = nullptr; void* boxed = g_faceRuntime.invoke(getGraph, player, nullptr, &ex);
			void* graph = boxed && !ex ? g_safeBones.unbox(boxed) : nullptr;
			if (graph)
			{
				valid = ReadBool(graphValidMethod, graph);
				if (valid == 1)
				{
					playing = ReadBool(graphPlayingMethod, graph);
					if (graphModeMethod) { void* modeEx = nullptr; void* result = g_faceRuntime.invoke(graphModeMethod, graph, nullptr, &modeEx);
						void* raw = result && !modeEx ? g_safeBones.unbox(result) : nullptr; if (raw) mode = *static_cast<int*>(raw); }
					// Inspect a single output only. Multiple or unrecognised outputs
					// remain unknown; no ownership or exclusivity is assumed.
					void* outputClass = FindClass("UnityEngine.DirectorModule.dll", "UnityEngine.Playables", "PlayableOutput");
					if (!outputClass) outputClass = FindClass("UnityEngine.CoreModule.dll", "UnityEngine.Playables", "PlayableOutput");
					void* animOutput = FindClass("UnityEngine.AnimationModule.dll", "UnityEngine.Animations", "AnimationPlayableOutput");
					const void* outputMethod = outputClass ? metadata.Find(graphClass, "GetOutput", false, {0x11, outputClass}, {{0x08}}) : nullptr;
					const void* castMethod = outputClass && animOutput ? metadata.Find(animOutput, "op_Explicit", true, {0x11, animOutput}, {{0x11, outputClass}}) : nullptr;
					const void* targetMethod = animOutput && g_animatorKlass ? metadata.Find(animOutput, "GetTarget", false, {0x12, g_animatorKlass}, {}) : nullptr;
					// This game exposes no non-generic GetOutputCount. Inspect index
					// zero, but never equate its presence with a single/exclusive output.
					if (outputMethod && castMethod && targetMethod)
					{
						int index = 0; void* args[]{&index}; void* e = nullptr;
						void* boxedOutput = g_faceRuntime.invoke(outputMethod, graph, args, &e);
						void* output = boxedOutput && !e ? g_safeBones.unbox(boxedOutput) : nullptr;
						if (output) { outputs = 1; void* castArgs[]{output}; e = nullptr; void* converted = g_faceRuntime.invoke(castMethod, nullptr, castArgs, &e);
							void* animation = converted && !e ? g_safeBones.unbox(converted) : nullptr;
							if (animation) { e = nullptr; void* target = g_faceRuntime.invoke(targetMethod, animation, nullptr, &e);
								if (!e && target) targetMatches = target == HelperGetActorAnimator(actor) ? 1 : 0; }
						}
					}
					if (phase>=0) {
						alignas(16) std::array<unsigned char, 16> actorGraph{};
						std::memcpy(actorGraph.data(), graph, actorGraph.size());
						if (followerClock) followerClock->Offer(actorGraph.data(),graphClass,actor,player,targetMatches==1);
						else if (!GakuMotion::Core::Instance().player.controller.targetActor || GakuMotion::Core::Instance().player.controller.targetActor == actor)
							LiveActorClockMvp::Offer(actorGraph.data(),graphClass,actor,player,targetMatches==1);
					}
				}
			}
		}
		auto& currentCore = GakuMotion::Core::Instance();
		if (phase >= 0 && targetMatches == 1 && currentCore.liveViewingConfirmed && currentCore.liveOwnerToken &&
			(!currentCore.player.controller.targetActor || currentCore.player.controller.targetActor == actor))
		{
			observedPlayer = reinterpret_cast<uintptr_t>(player); observedActor = reinterpret_cast<uintptr_t>(actor);
			observedOwner = currentCore.liveOwnerToken; observedScene = currentCore.liveOwnerScene;
		}
		g_hostContext.logInfo("[GakuMotion][LiveActorGraph] phase=%d component_class=%d component=%d player_base=%d player=%d compatible=%d getter=%d valid=%d playing=%d update_mode=%d outputs=%d actor_target=%d",
			phase, int(componentClass != nullptr), int(component != nullptr), int(playerBase != nullptr), int(player != nullptr),
			int(isPlayer), int(getGraph != nullptr), valid, playing, mode, outputs, targetMatches);
		auto& core = GakuMotion::Core::Instance();
		auto& monitor = core.physicsMonitor;
		if (phase < 0)
		{
			monitor.photoGraphSampled = true; monitor.photoGraphPlayerFound = player != nullptr;
			monitor.photoGraphGetterFound = getGraph != nullptr; monitor.photoGraphValid = valid;
			monitor.photoGraphPlaying = playing; monitor.photoGraphUpdateMode = mode;
			return;
		}
		monitor.graphSampled = true; monitor.graphPhase = phase;
		monitor.graphOwner = core.liveOwnerToken; monitor.graphSession = core.injectionSession;
		monitor.graphPlayerFound = player != nullptr; monitor.graphGetterFound = getGraph != nullptr;
		monitor.graphValid = valid; monitor.graphPlaying = playing; monitor.graphUpdateMode = mode;
		monitor.graphOutputs = outputs; monitor.graphActorTarget = targetMatches;
	}
	inline void Sample(void* actor, bool livePaused, bool takeover, int playbackState)
	{
		if (!actor || !g_hostContext.logInfo || !g_faceRuntime.invoke || !g_safeBones.unbox) return;
		static auto last = std::chrono::steady_clock::time_point{};
		static int previousPhase = -1;
		const int phase = (livePaused ? 1 : 0) | (takeover ? 2 : 0);
		const auto now = std::chrono::steady_clock::now();
		if (phase == previousPhase && now - last < std::chrono::seconds(5)) return;
		previousPhase = phase; last = now;
		Resolve();
		void* animator = HelperGetActorAnimator(actor);
		const int enabled = animator && g_getAnimatorEnabledNative ? int(HelperGetAnimatorEnabled(animator)) : -1;
		void* controller = animator && g_getAnimatorControllerNative ? HelperGetAnimatorController(animator) : nullptr;
		float speed = 0.0f; bool speedValid = false;
		if (animator && speedMethod)
		{
			void* ex = nullptr; void* boxed = g_faceRuntime.invoke(speedMethod, animator, nullptr, &ex);
			void* raw = boxed && !ex ? g_safeBones.unbox(boxed) : nullptr;
			if (raw) { speed = *static_cast<float*>(raw); speedValid = std::isfinite(speed); }
		}
		int valid = -1, playing = -1;
		if (animator && graphMethod)
		{
			void* ex = nullptr; void* boxed = g_faceRuntime.invoke(graphMethod, animator, nullptr, &ex);
			void* graph = boxed && !ex ? g_safeBones.unbox(boxed) : nullptr;
			if (graph) { valid = ReadBool(graphValidMethod, graph); if (valid == 1) playing = ReadBool(graphPlayingMethod, graph); }
		}
		g_hostContext.logInfo("[GakuMotion][LiveAnimatorProbe] actor=%p animator=%p live_paused=%d takeover=%d motion_state=%d enabled=%d controller=%p speed_valid=%d speed=%.3f graph_valid=%d graph_playing=%d",
		    actor, animator, int(livePaused), int(takeover), playbackState, enabled, controller,
		    int(speedValid), speed, valid, playing);
		SampleActorGraph(actor, phase);
	}
	inline void SamplePhotography(void* actor)
	{
		if (!actor || !g_hostContext.logInfo || !g_faceRuntime.invoke || !g_safeBones.unbox) return;
		static auto last = std::chrono::steady_clock::time_point{};
		const auto now = std::chrono::steady_clock::now();
		if (now - last < std::chrono::seconds(5)) return;
		last = now; Resolve(); SampleActorGraph(actor, -1);
	}
}
