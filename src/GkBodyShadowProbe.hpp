#pragma once

// Included after GroundShadowProbe. Request-driven body-only producer scope;
// never queries Shader globals or writes cached bounds/shadow-data fields.
namespace BodyShadowProbe
{
	using Clock = std::chrono::steady_clock;
	using Metadata = GroundShadowProbeBootstrap::Access;
	inline Metadata api{};
	inline bool (*isValueType)(void*) = nullptr;
	inline uint32_t (*instanceSize)(void*) = nullptr;
	inline DWORD ownerThread = 0;
	inline bool ready = false;
	using UpdateFunction = bool (*)(void*,void*,void*,int32_t,void*);
	inline UpdateFunction originalUpdate = nullptr;
	inline bool updateReady = false, boundsFixEnabled = true;
	inline thread_local bool inUpdate = false, updateConsumed = false, updateAdapted = false;
	inline thread_local GroundShadowProbe::Bounds updateNative{}, updateDraw{};
	struct UpdateRecord
	{
		void *pass = nullptr, *data = nullptr;
		int frame = -1;
		bool called = false, result = false, consumed = false, adapted = false;
		GroundShadowProbe::Bounds native{}, draw{};
		uint64_t poseSerial = 0;
	};
	inline thread_local UpdateRecord lastUpdate{};
	inline void *featureClass = nullptr, *passClass = nullptr, *dataClass = nullptr;
	inline int dataSize = -1, renderingSize = -1, cameraSize = -1;
	inline int featurePass = -1, passData = -1, supports = -1;
	inline int rdCamera = -1, cameraPosition = -1, cameraView = -1, cameraObject = -1;
	inline int matrixOffsets[4]{-1,-1,-1,-1}, scalarOffsets[6]{-1,-1,-1,-1,-1,-1}, resolutionOffset = -1;
	inline const char* matrixNames[]{"viewMatrix", "projMatrix", "shadowTransform", "lightToWorldMatrix"};
	inline const char* scalarNames[]{"startDistance2", "endDistance2", "fade", "strength", "useOffset", "facePartsStrength"};
	inline std::string root, failure, label;
	inline uint64_t lastSeq = 0, sequence = 0, sampleIndex = 0;
	inline int remaining = 0, ioFailures = 0;
	inline Clock::time_point lastPoll{}, lastSample{}, started{};
	inline thread_local int depth = 0;
	inline thread_local bool pending = false;
	inline thread_local void *pendingFeature = nullptr, *pendingData = nullptr;
	inline thread_local std::string entry, native;
	inline thread_local int entryFrame = -1;
	inline thread_local void* entryActor = nullptr;
	inline auto Json() { return GroundShadowProbe::JsonStream(); }
	inline auto Quote(const std::string& value) { return GroundShadowProbeBootstrap::Quote(value); }
	inline auto Pointer(const void* value) { return GroundShadowProbeBootstrap::Pointer(value); }
	inline bool OnThread() { return ownerThread && GetCurrentThreadId() == ownerThread; }
	inline void* TargetActor()
	{
		// Playback identity is independent of camera ownership. Photography can
		// release its controlled camera while the same MMD actor keeps rendering.
		const auto& controller = GakuMotion::Core::Instance().player.controller;
		return controller.targetActor ? controller.targetActor : controller.currentBinding.actor;
	}
	inline bool InRange(int offset, size_t count, int size)
	{
		return offset >= 0 && size >= 0 && count <= static_cast<size_t>(size) &&
			static_cast<size_t>(offset) <= static_cast<size_t>(size) - count;
	}
	inline bool Read(const void* source, void* output, size_t bytes)
	{
		SIZE_T copied = 0;
		return source && output && bytes && ReadProcessMemory(GetCurrentProcess(), source, output, bytes, &copied) && copied == bytes;
	}
	inline bool Finite(const float* values, size_t count)
	{
		for (size_t i = 0; i < count; ++i) if (!std::isfinite(values[i])) return false;
		return true;
	}
	inline int ValueSize(void* klass)
	{
		uint32_t alignment = 0;
		if (!klass || !isValueType || !isValueType(klass) || !api.valueSize) return -1;
		const int size = api.valueSize(klass, &alignment);
		return size > 0 && size <= 65536 && alignment > 0 && alignment <= 16 ? size : -1;
	}
	inline int Field(void* owner, const char* name, int code, void* typeClass, size_t bytes, bool value)
	{
		if (!owner || !api.field || !api.fieldType || !api.fieldFlags || !api.fieldOffset ||
			!api.method.typeCode || !api.method.byRef || !api.method.classFromType) return -1;
		void* field = api.field(owner, name);
		void* type = field ? api.fieldType(field) : nullptr;
		if (!type || (api.fieldFlags(field) & 0x10) || api.method.byRef(type) || api.method.typeCode(type) != code ||
			(typeClass && api.method.classFromType(type) != typeClass)) return -1;
		const int offset = api.fieldOffset(field) - (value ? static_cast<int>(2 * sizeof(void*)) : 0);
		const int size = value ? ValueSize(owner) : (instanceSize ? static_cast<int>(instanceSize(owner)) : -1);
		return InRange(offset, bytes, size) && (value || offset >= static_cast<int>(2 * sizeof(void*))) ? offset : -1;
	}
	inline bool MatrixLayout(void* matrix)
	{
		if (ValueSize(matrix) != 64) return false;
		for (int column = 0; column < 4; ++column) for (int row = 0; row < 4; ++row)
		{
			const char name[]{'m', static_cast<char>('0' + row), static_cast<char>('0' + column), '\0'};
			if (Field(matrix, name, 0x0c, nullptr, 4, true) != (column * 4 + row) * 4) return false;
		}
		return true;
	}
	inline bool Resolve(HMODULE module)
	{
		failure = "missing_metadata_or_layout";
		api = GroundShadowProbeBootstrap::Resolve(module);
		isValueType = reinterpret_cast<decltype(isValueType)>(GetProcAddress(module, "il2cpp_class_is_valuetype"));
		instanceSize = reinterpret_cast<decltype(instanceSize)>(GetProcAddress(module, "il2cpp_class_instance_size"));
		auto unity = [](const char* name) { return FindClass("UnityEngine.CoreModule.dll", "UnityEngine", name); };
		auto urp = [](const char* name) { return FindClass("Unity.RenderPipelines.Universal.Runtime.dll", "UnityEngine.Rendering.Universal", name); };
		featureClass = FindClass("vl-unity.Runtime.dll", "VL.Rendering", "DrawActorShadowPass");
		passClass = FindClass("vl-unity.Runtime.dll", "VL.Rendering", "ActorShadowPass");
		void* dataField = passClass && api.field ? api.field(passClass, "_actorShadowData") : nullptr;
		void* dataType = dataField && api.fieldType ? api.fieldType(dataField) : nullptr;
		dataClass = dataType && api.method.classFromType ? api.method.classFromType(dataType) : nullptr;
		const char* dataName = dataClass && api.className ? api.className(dataClass) : nullptr;
		if (!dataName || std::strcmp(dataName,"ActorShadowData")) return false;
		void *rd = urp("RenderingData"), *camera = urp("CameraData"), *matrix = unity("Matrix4x4"), *vector = unity("Vector3");
		if (!featureClass || !passClass || !dataClass || !g_faceRuntime.objectClass || !MatrixLayout(matrix) || ValueSize(vector) != 12) return false;
		const char* components[]{"x","y","z"};
		for (int i = 0; i < 3; ++i) if (Field(vector, components[i], 0x0c, nullptr, 4, true) != 4*i) return false;
		dataSize = ValueSize(dataClass); renderingSize = ValueSize(rd); cameraSize = ValueSize(camera);
		if (dataSize < 256 || dataSize > 4096 || renderingSize < 1 || cameraSize < 1) return false;
		featurePass = Field(featureClass, "_pass", 0x12, passClass, sizeof(void*), false);
		passData = Field(passClass, "_actorShadowData", 0x11, dataClass, dataSize, false);
		supports = Field(passClass, "_supportsShadow", 0x02, nullptr, 1, false);
		rdCamera = Field(rd, "cameraData", 0x11, camera, cameraSize, true);
		cameraPosition = Field(camera, "worldSpaceCameraPos", 0x11, vector, 12, true);
		cameraView = Field(camera, "m_ViewMatrix", 0x11, matrix, 64, true);
		if (cameraView < 0) cameraView = Field(camera, "viewMatrix", 0x11, matrix, 64, true);
		cameraObject = Field(camera, "camera", 0x12, unity("Camera"), sizeof(void*), true);
		if (featurePass < 0 || passData < 0 || supports < 0 || rdCamera < 0 || cameraPosition < 0 || cameraView < 0 || cameraObject < 0) return false;
		for (int i = 0; i < 4; ++i)
			if ((matrixOffsets[i] = Field(dataClass, matrixNames[i], 0x11, matrix, 64, true)) < 0) return false;
		for (int i = 0; i < 6; ++i)
			if ((scalarOffsets[i] = Field(dataClass, scalarNames[i], 0x0c, nullptr, 4, true)) < 0) return false;
		resolutionOffset = Field(dataClass, "resolution", 0x08, nullptr, 4, true);
		if (resolutionOffset < 0) return false;
		failure.clear();
		return true;
	}
	inline std::string Floats(const float* values, size_t count, bool read)
	{
		auto out = Json();
		const bool valid = read && Finite(values, count);
		out << "{\"ok\":" << (valid ? "true" : "false") << ",\"value\":";
		if (!valid) out << "null";
		else { out << '['; for (size_t i = 0; i < count; ++i) out << (i ? "," : "") << values[i]; out << ']'; }
		out << '}'; return out.str();
	}
	inline bool AdaptBounds(void* actor, const GroundShadowProbe::Bounds& input, GroundShadowProbe::Bounds& output)
	{
		if (!OnThread() || !ready || !inUpdate || !GroundShadowProbe::Eligible(actor) ||
			!GroundShadowProbe::boundsFixReady || !GroundShadowProbe::Valid(input)) return false;
		const auto& stamp = GakuMotion::Core::Instance().player.controller.footPoseWriteStamp;
		if (GroundShadowProbe::poseActor != actor || GroundShadowProbe::poseSerial != stamp.serial ||
			GroundShadowProbe::poseFrame < 0 || !g_faceUnityFrame || GroundShadowProbe::poseFrame != g_faceUnityFrame()) return false;
		updateConsumed = true; updateNative = input; updateDraw = input;
		if (!boundsFixEnabled) return false;
		output = GroundShadowProbe::ExpandForPose(input);
		updateDraw = output; updateAdapted = true;
		return true;
	}
	inline bool Update(void* pass, void* data, void* settings, int32_t resolution, void* method)
	{
		if (!OnThread() || !ready) return originalUpdate ? originalUpdate(pass,data,settings,resolution,method) : false;
		struct Restore
		{
			bool inside, consumed, adapted;
			GroundShadowProbe::Bounds native, draw;
			~Restore() { inUpdate=inside; updateConsumed=consumed; updateAdapted=adapted; updateNative=native; updateDraw=draw; }
		} restore{inUpdate,updateConsumed,updateAdapted,updateNative,updateDraw};
		const bool nested = inUpdate;
		inUpdate = true; updateConsumed = updateAdapted = false;
		const bool result = originalUpdate ? originalUpdate(pass,data,settings,resolution,method) : false;
		if (!nested)
		{
			lastUpdate = {pass,data,g_faceUnityFrame ? g_faceUnityFrame() : -1,true,result,updateConsumed,updateAdapted,
				updateNative,updateDraw,GroundShadowProbe::poseSerial};
		}
		return result;
	}
	inline std::string Camera(void* rd)
	{
		if (!ready || !rd) return "{\"ok\":false}";
		auto* cam = static_cast<unsigned char*>(rd) + rdCamera;
		float position[3]{}, view[16]{}; void* object = nullptr;
		const bool positionOk = Read(cam + cameraPosition, position, sizeof(position));
		const bool viewOk = Read(cam + cameraView, view, sizeof(view));
		const bool objectOk = Read(cam + cameraObject, &object, sizeof(object));
		auto out = Json();
		out << "{\"position\":" << Floats(position, 3, positionOk) << ",\"view\":" << Floats(view, 16, viewOk)
			<< ",\"camera_ok\":" << (objectOk ? "true" : "false") << ",\"camera\":" << (objectOk ? Pointer(object) : "null") << '}';
		return out.str();
	}
	inline std::string Pass(void* feature)
	{
		if (!ready || !feature || g_faceRuntime.objectClass(feature) != featureClass) return "{\"ok\":false}";
		void* pass = nullptr;
		if (!Read(static_cast<unsigned char*>(feature) + featurePass, &pass, sizeof(pass)) || !pass ||
			g_faceRuntime.objectClass(pass) != passClass) return "{\"ok\":false}";
		std::vector<unsigned char> data(static_cast<size_t>(dataSize));
		const bool read = Read(static_cast<unsigned char*>(pass) + passData, data.data(), data.size());
		uint8_t supported = 0;
		const bool supportedOk = Read(static_cast<unsigned char*>(pass) + supports, &supported, 1) && supported <= 1;
		auto out = Json();
		out << "{\"ok\":" << (read ? "true" : "false") << ",\"pass\":" << Pointer(pass)
			<< ",\"supports_ok\":" << (supportedOk ? "true" : "false") << ",\"supports_shadow\":"
			<< (supportedOk ? (supported ? "true" : "false") : "null");
		for (int i = 0; i < 4; ++i)
		{
			float values[16]{}; if (read) std::memcpy(values, data.data() + matrixOffsets[i], sizeof(values));
			out << ',' << Quote(matrixNames[i]) << ':' << Floats(values, 16, read);
		}
		for (int i = 0; i < 6; ++i)
		{
			float value = 0; if (read) std::memcpy(&value, data.data() + scalarOffsets[i], sizeof(value));
			out << ',' << Quote(scalarNames[i]) << ':' << Floats(&value, 1, read);
		}
		int resolution = 0; if (read) std::memcpy(&resolution, data.data() + resolutionOffset, sizeof(resolution));
		out << ",\"resolution_ok\":" << (read ? "true" : "false") << ",\"resolution\":";
		if (read) out << resolution; else out << "null";
		out << '}'; return out.str();
	}
	inline void Poll(Clock::time_point now)
	{
		if (!OnThread() || (lastPoll.time_since_epoch().count() && now-lastPoll < std::chrono::milliseconds(500))) return;
		lastPoll = now;
		if (remaining && now-started > std::chrono::seconds(30)) remaining = 0;
		uint64_t seq = 0;
		std::ifstream file(GakuMotion::MotionPathFromUtf8(root + "/body_shadow_probe.request.json"), std::ios::binary | std::ios::ate);
		if (!file) return;
		const auto length = file.tellg();
		if (length <= 0 || length > 65536) return;
		std::string text(static_cast<size_t>(length), '\0'); file.seekg(0);
		if (!file.read(text.data(), static_cast<std::streamsize>(text.size()))) return;
		GakuMotion::jsonlite::Value request;
		std::string error;
		if (!GakuMotion::jsonlite::ParseText(text, request, error) || !request.IsObject()) return;
		const auto* sequenceValue = request.Find("seq");
		const auto* pid = request.Find("pid");
		const double number = sequenceValue && sequenceValue->IsNumber() ? sequenceValue->AsDouble(0) : 0;
		if (!std::isfinite(number) || number < 1 || number > 9007199254740991.0 || std::floor(number) != number ||
			(pid && (!pid->IsNumber() || (pid->AsDouble(-1) != 0 && pid->AsDouble(-1) != GetCurrentProcessId())))) return;
		seq = static_cast<uint64_t>(number); if (seq <= lastSeq) return;
		const auto* samples = request.Find("samples");
		const double count = samples && samples->IsNumber() ? samples->AsDouble(1) : 1;
		if (!std::isfinite(count) || std::floor(count) != count) return;
		remaining = static_cast<int>(std::clamp(count, 1.0, 16.0));
		if (const auto* fix=request.Find("body_bounds_fix"); fix && fix->IsBool()) boundsFixEnabled=fix->AsBool(false);
		label = request.StringPath("label", ""); if (label.size() > 256) label = "label-too-long";
		lastSeq = sequence = seq; started = now; sampleIndex = 0; lastSample = {};
		// One overwritten status per accepted request separates polling from
		// sample eligibility without adding a permanent per-frame log stream.
		void* target = TargetActor();
		auto status = Json();
		status << "{\"schema\":1,\"pid\":" << GetCurrentProcessId() << ",\"seq\":" << sequence
			<< ",\"request_accepted\":true,\"samples\":" << remaining
			<< ",\"camera_actor\":" << Pointer(CameraBridge::Controller().actor)
			<< ",\"target_actor\":" << Pointer(target)
			<< ",\"target_eligible\":" << (GroundShadowProbe::Eligible(target) ? "true" : "false")
			<< ",\"body_bounds_fix_enabled\":" << (boundsFixEnabled ? "true" : "false") << "}\n";
		if (!GroundShadowProbeBootstrap::Write(root + "/body_shadow_request_status.json", status.str())) ++ioFailures;
	}
	inline void ObserveImpl(void* feature, void* rd, const char* phase)
	{
		if (!OnThread() || !phase) return;
		if (std::strcmp(phase, "entry") == 0)
		{
			++depth;
			if (depth != 1) return;
			lastUpdate = {};
			pending = false;
			const auto now = Clock::now(); Poll(now);
			void* actor = TargetActor();
			if (!ready || remaining <= 0 || now-started > std::chrono::seconds(30) ||
				(lastSample.time_since_epoch().count() && now-lastSample < std::chrono::milliseconds(250)) ||
				!GroundShadowProbe::Eligible(actor)) return;
			entry = Camera(rd); native.clear(); entryFrame = g_faceUnityFrame ? g_faceUnityFrame() : -1;
			entryActor = actor; pendingFeature = feature; pendingData = rd; pending = true;
			return;
		}
		if (std::strcmp(phase, "native") == 0)
		{
			if (depth == 1 && pending && pendingFeature == feature && pendingData == rd) native = Camera(rd);
			return;
		}
		if (std::strcmp(phase, "aborted") == 0)
		{
			// An original-call exception has no completed producer state. Retire
			// only that nesting level; an inner abort must not erase an outer call.
			if (depth == 1) pending = false;
			if (depth > 0) --depth;
			return;
		}
		if (std::strcmp(phase, "exit") && std::strcmp(phase, "bypass")) return;
		if (depth == 1 && pending && pendingFeature == feature && pendingData == rd)
		{
			pending = false;
			auto& core = GakuMotion::Core::Instance(); auto& ctrl = CameraBridge::Controller();
			void* actor = TargetActor();
			const int frame = g_faceUnityFrame ? g_faceUnityFrame() : -1;
			if (frame == entryFrame && entryActor == actor && GroundShadowProbe::Eligible(actor))
			{
				auto out = Json();
				GakuMotion::Vector3 actorPosition{}, hips{}; GakuMotion::Quaternion rotation{};
				const bool poseOk = ctrl.fnActorPose && ctrl.fnActorPose(actor, actorPosition, rotation, hips);
				const float actorValues[]{actorPosition.x,actorPosition.y,actorPosition.z}, hipsValues[]{hips.x,hips.y,hips.z};
				out << "{\"schema\":1,\"pid\":" << GetCurrentProcessId() << ",\"seq\":" << sequence
					<< ",\"sample\":" << ++sampleIndex << ",\"label\":" << Quote(label) << ",\"frame\":" << frame
					<< ",\"phase\":" << Quote(std::string("AddRenderPasses.") + phase) << ",\"gpu_constants\":false"
					<< ",\"mode\":" << ctrl.settings.actorShadowExperimentMode << ",\"time\":" << core.player.currentTime
					<< ",\"actor\":" << Pointer(actor) << ",\"camera_actor\":" << Pointer(ctrl.actor)
					<< ",\"feature\":" << Pointer(feature) << ",\"rendering_data\":" << Pointer(rd)
					<< ",\"actor_position\":" << Floats(actorValues,3,poseOk) << ",\"hips\":" << Floats(hipsValues,3,poseOk)
					<< ",\"entry\":" << entry << ",\"native\":" << (native.empty() ? "null" : native)
					<< ",\"exit\":" << Camera(rd) << ",\"shadow_data\":" << Pass(feature);
				// Append producer consumption only when pass/data/frame identities
				// match this feature call. This is not GPU constant readback.
				void* featurePassValue=nullptr;
				const bool passRead=feature && Read(static_cast<unsigned char*>(feature)+featurePass,&featurePassValue,sizeof(featurePassValue));
				const bool matched=passRead && lastUpdate.called && lastUpdate.pass==featurePassValue && lastUpdate.data==rd && lastUpdate.frame==frame;
				auto producer=Json();
				producer << ",\"body_bounds_fix_enabled\":" << (boundsFixEnabled ? "true" : "false")
					<< ",\"update_matched\":" << (matched ? "true" : "false")
					<< ",\"update_result\":" << (matched ? (lastUpdate.result ? "true" : "false") : "null")
					<< ",\"body_bounds_consumed\":" << (matched && lastUpdate.consumed ? "true" : "false")
					<< ",\"body_bounds_adapted\":" << (matched && lastUpdate.adapted ? "true" : "false")
					<< ",\"pose_serial\":" << (matched ? lastUpdate.poseSerial : 0)
					<< ",\"body_native_bounds\":" << (matched && lastUpdate.consumed ? GroundShadowProbe::BoundsJson(lastUpdate.native) : "null")
					<< ",\"body_draw_bounds\":" << (matched && lastUpdate.consumed ? GroundShadowProbe::BoundsJson(lastUpdate.draw) : "null") << "}\n";
				std::ofstream file(GakuMotion::MotionPathFromUtf8(root + "/body_shadow_probe.jsonl"), std::ios::binary | std::ios::app);
				if (file) { file << out.str() << producer.str(); file.flush(); }
				if (!file) ++ioFailures;
				--remaining; lastSample = Clock::now();
			}
		}
		if (depth > 0) --depth;
	}
	inline void Observe(void* feature, void* rd, const char* phase) noexcept
	{
		if (!OnThread()) return;
		try { ObserveImpl(feature, rd, phase); }
		catch (...) { pending = false; depth = 0; ++ioFailures; }
	}
	inline bool Install()
	{
		ownerThread = GetCurrentThreadId(); root = GakuMotion::Core::Instance().configRoot;
		try { ready = Resolve(g_gameAssembly); } catch (...) { ready = false; failure = "metadata_exception"; }
		if (ready)
		{
			void* settingsField=api.field ? api.field(featureClass,"settings") : nullptr;
			void* settingsType=settingsField && api.fieldType ? api.fieldType(settingsField) : nullptr;
			void* settingsClass=settingsType && api.method.classFromType ? api.method.classFromType(settingsType) : nullptr;
			void* data=FindClass("Unity.RenderPipelines.Universal.Runtime.dll","UnityEngine.Rendering.Universal","RenderingData");
			const void* method=settingsClass && data ? api.method.Find(passClass,"UpdateShadowData",false,
				{0x02},{{0x11,data,true},{0x12,settingsClass,true},{0x08}}) : nullptr;
			void* pointer=GroundShadowProbeBootstrap::MethodPointer(method);
			updateReady=pointer && CameraBridge::InstallHook("ActorShadowPass.UpdateShadowData",pointer,
				reinterpret_cast<void*>(Update),reinterpret_cast<void**>(&originalUpdate)) && originalUpdate;
			if (updateReady) GroundShadowProbe::bodyBoundsOverride=AdaptBounds;
		}
		auto status = Json();
		status << "{\"schema\":1,\"pid\":" << GetCurrentProcessId() << ",\"ready\":" << (ready ? "true" : "false")
			<< ",\"update_hook_ready\":" << (updateReady ? "true" : "false")
			<< ",\"reason\":" << Quote(failure) << ",\"data_size\":" << dataSize << ",\"rd_camera\":" << rdCamera
			<< ",\"camera_size\":" << cameraSize << ",\"camera_position\":" << cameraPosition << ",\"camera_view\":" << cameraView << "}\n";
		if (!GroundShadowProbeBootstrap::Write(root + "/body_shadow_layout.json", status.str())) ++ioFailures;
		CameraBridge::shadowObserver = Observe;
		CameraBridge::shadowOffsetsResolved = true;
		CameraBridge::renderingDataCameraDataOffset = -1;
		CameraBridge::cameraDataViewMatrixOffset = -1;
		CameraBridge::cameraDataWorldPosOffset = -1;
		if (ready)
		{
			// The legacy experiment may borrow CameraData only after this exact
			// value-type/field/matrix layout has independently passed validation.
			CameraBridge::renderingDataCameraDataOffset = rdCamera;
			CameraBridge::cameraDataViewMatrixOffset = cameraView;
			CameraBridge::cameraDataWorldPosOffset = cameraPosition;
			CameraBridge::shadowOffsetsResolved = true;
		}
		return ready;
	}
	inline void Shutdown()
	{
		CameraBridge::shadowObserver = nullptr; ready = false; ownerThread = 0;
		GroundShadowProbe::bodyBoundsOverride=nullptr; updateReady=false; boundsFixEnabled=true; inUpdate=false; lastUpdate={};
		pending = false; depth = 0; remaining = 0; lastSeq = sequence = sampleIndex = 0;
		entry.clear(); native.clear(); lastPoll = lastSample = started = {};
	}
}
