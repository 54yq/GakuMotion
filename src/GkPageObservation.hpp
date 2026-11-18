#pragma once

// Diagnostic reads only. No result is consumed by admission or capture policy.
#ifdef GKMOTION_CAPTURE_DIAGNOSTICS
namespace PageObservation
{
	inline const void *activeScene = nullptr, *sceneCount = nullptr, *sceneAt = nullptr;
	inline void *sceneClass = nullptr, *windowClass = nullptr;
	inline void* queryClasses[3]{};
	inline const char* (*className)(void*) = nullptr;
	inline const char* (*classNamespace)(void*) = nullptr;
	inline bool ready = false;
	inline ULONGLONG nextPoll = 0, nextHeartbeat = 0;
	inline std::string previous;

	inline void Resolve()
	{
		auto& api = StageLoader::metadata;
		void* manager = FindClass("UnityEngine.CoreModule.dll", "UnityEngine.SceneManagement", "SceneManager");
		sceneClass = FindClass("UnityEngine.CoreModule.dll", "UnityEngine.SceneManagement", "Scene");
		windowClass = FindClass("quaunity-ui.Runtime.dll", "Qua.UI", "WindowPresenterBase");
		// Raw observation queries, not an inferred current-page selector. Keep all
		// returned candidates, including zero/multiple roots, visible in the log.
		queryClasses[0] = FindClass("quaunity-ui.Runtime.dll", "Qua.UI", "QuaScenePresenterBase");
		queryClasses[1] = FindClass("Assembly-CSharp.dll", "Campus.Photography", "PhotographyScenePresenter");
		queryClasses[2] = windowClass;
		className = reinterpret_cast<decltype(className)>(GetProcAddress(g_gameAssembly, "il2cpp_class_get_name"));
		classNamespace = reinterpret_cast<decltype(classNamespace)>(GetProcAddress(g_gameAssembly, "il2cpp_class_get_namespace"));
		if (sceneClass)
		{
			activeScene = api.Find(manager, "GetActiveScene", true, {0x11, sceneClass}, {});
			sceneCount = api.Find(manager, "get_sceneCount", true, {0x08}, {});
			sceneAt = api.Find(manager, "GetSceneAt", true, {0x11, sceneClass}, {{0x08}});
		}
		uint32_t alignment = 0;
		ready = sceneClass && StageLoader::valueSize &&
			StageLoader::valueSize(sceneClass, &alignment) == sizeof(int) &&
			activeScene && sceneCount && sceneAt && StageLoader::sceneName && StageLoader::sceneLoaded &&
			StageLoader::findAll && StageLoader::arrayLength && StageLoader::newHandle && StageLoader::freeHandle &&
			StageLoader::assignable && StageLoader::componentClass && StageLoader::componentGo && StageLoader::goScene &&
			il2cpp_class_get_type && il2cpp_type_get_object && g_faceRuntime.invoke &&
			g_faceRuntime.objectClass && g_safeBones.unbox && className && classNamespace;
		if (g_hostContext.logInfo) g_hostContext.logInfo(
			"[GakuMotion][PageObservation] metadata_ready=%d active=%d count=%d at=%d presenter_queries=%d/%d/%d; observation_only=1",
			ready, !!activeScene, !!sceneCount, !!sceneAt, !!queryClasses[0], !!queryClasses[1], !!queryClasses[2]);
	}

	inline void* Invoke(const void* method, void* object = nullptr, void** args = nullptr)
	{
		if (!method) return nullptr;
		void* exception = nullptr;
		void* result = g_faceRuntime.invoke(method, object, args, &exception);
		return exception ? nullptr : result;
	}

	inline std::string TypeName(void* klass)
	{
		if (!klass) return "unavailable";
		const char* ns = classNamespace(klass), *name = className(klass);
		return std::string(ns ? ns : "") + "." + (name ? name : "unavailable");
	}

	inline std::string Scene(void* boxed)
	{
		if (!boxed || g_faceRuntime.objectClass(boxed) != sceneClass) return "unavailable";
		void* value = g_safeBones.unbox(boxed);
		if (!value) return "unavailable";
		int handle = *static_cast<int*>(value);
		void* args[]{&handle};
		void* name = Invoke(StageLoader::sceneName, nullptr, args);
		void* loaded = Invoke(StageLoader::sceneLoaded, nullptr, args);
		void* loadedValue = loaded ? g_safeBones.unbox(loaded) : nullptr;
		return "name=" + (name ? Il2CppStringToUtf8(name) : std::string("unavailable")) +
			",handle=" + std::to_string(handle) + ",loaded=" +
			(loadedValue ? (*static_cast<bool*>(loadedValue) ? "1" : "0") : "unknown");
	}

	struct Root
	{
		StageLoader::Handle handle;
		~Root() { if (handle) StageLoader::freeHandle(handle); }
	};

	inline std::string Presenters()
	{
		std::vector<void*> seen;
		std::vector<std::string> entries;
		for (void* klass : queryClasses)
		{
			if (!klass || !StageLoader::assignable(StageLoader::componentClass, klass)) continue;
			void* type = il2cpp_type_get_object(il2cpp_class_get_type(klass));
			Root typeRoot{type ? StageLoader::newHandle(type, false) : nullptr};
			if (!typeRoot.handle) { entries.push_back(TypeName(klass) + ":type-unavailable"); continue; }
			bool inactive = false;
			void* args[]{type, &inactive};
			void* array = Invoke(StageLoader::findAll, nullptr, args);
			Root arrayRoot{array ? StageLoader::newHandle(array, false) : nullptr};
			if (!arrayRoot.handle) { entries.push_back(TypeName(klass) + ":query-unavailable"); continue; }
			const uintptr_t count = StageLoader::arrayLength(array);
			if (count > 64) { entries.push_back(TypeName(klass) + ":count-over-64"); continue; }
			void** items = reinterpret_cast<void**>(static_cast<char*>(array) + 32);
			for (uintptr_t i = 0; i < count; ++i)
			{
				void* root = items[i];
				if (!root || std::find(seen.begin(), seen.end(), root) != seen.end()) continue;
				seen.push_back(root);
				void* actual = g_faceRuntime.objectClass(root);
				void* go = Invoke(StageLoader::componentGo, root);
				std::string entry = TypeName(actual) + "@{" + Scene(go ? Invoke(StageLoader::goScene, go) : nullptr) + "}";
				if (actual && windowClass && StageLoader::assignable(windowClass, actual))
				{
					void* top = Invoke(GakuMotion::capture::topScreenMetadata, root);
					entry += ",top=" + (top ? TypeName(g_faceRuntime.objectClass(top)) : std::string("null-or-unavailable"));
				}
				else entry += ",top=not-window";
				entries.push_back(entry);
			}
		}
		std::sort(entries.begin(), entries.end());
		std::string text;
		for (const auto& entry : entries) { if (!text.empty()) text += ";"; text += entry; }
		return text.empty() ? "none-observed" : text;
	}

	inline void Poll(bool guardActive)
	{
		if (!ready || !ProtectionOnUnityThread() || !g_hostContext.logInfo) return;
		const ULONGLONG now = GetTickCount64();
		if (now < nextPoll) return;
		nextPoll = now + 1000;
		const auto& core = GakuMotion::Core::Instance();
		std::string loaded;
		void* countObject = Invoke(sceneCount);
		void* countValue = countObject ? g_safeBones.unbox(countObject) : nullptr;
		const int count = countValue ? *static_cast<int*>(countValue) : -1;
		if (count < 0 || count > 64) loaded = "unavailable";
		else for (int i = 0; i < count; ++i)
		{
			void* args[]{&i};
			if (!loaded.empty()) loaded += ";";
			loaded += "{" + Scene(Invoke(sceneAt, nullptr, args)) + "}";
		}
		const bool playback = core.player.state != GakuMotion::PlaybackState::Stopped;
		const bool sessionActive = playback || core.camera.owned || core.stageManager.injectionEnabled;
		const std::string signature = "active={" + Scene(Invoke(activeScene)) + "} loaded_scenes=[" + loaded +
			"] presenters=[" + Presenters() + "] gm_session=" + (sessionActive ? "active" : "inactive") +
			" session=" + std::to_string(core.injectionSession) + " playback_state=" +
			std::to_string(static_cast<int>(core.player.state)) + " stage_applied=" + std::to_string(core.stageManager.injectionEnabled) +
			" camera_owned=" + std::to_string(core.camera.owned) + " capture_suspended=" +
			std::to_string(core.captureSuspended) + " guard_active=" + std::to_string(guardActive);
		if (signature != previous || now >= nextHeartbeat)
		{
			g_hostContext.logInfo("[GakuMotion][PageObservation] t_ms=%llu %s", now, signature.c_str());
			previous = signature;
			nextHeartbeat = now + 10000;
		}
	}
}
#endif
