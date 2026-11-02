#pragma once

// Included in the plugin's private namespace after ActorGroundShadow.
// Native ground AO draw adaptation with bounded, request-driven observation.
// The native cached bounds/AO/job buffers are never mutated. Only the Bounds
// value returned inside the target's synchronous AO draw may be expanded.
namespace GroundShadowProbe
{
	using Clock = std::chrono::steady_clock;
	using Outer = void (*)(void*, void*, void*, void*);
	using Inner = void (*)(void*, void*, void*, void*, void*);
	struct Bounds { UnityVector3 center, extents; };
	static_assert(sizeof(Bounds) == 24 && offsetof(Bounds, extents) == 12);
	using BoundsGetter = Bounds (*)(void*, void*);
	using BodyBoundsOverride = bool (*)(void*, const Bounds&, Bounds&);
	inline BodyBoundsOverride bodyBoundsOverride = nullptr;
	inline Outer originalOuter = nullptr;
	inline Inner originalInner = nullptr;
	inline BoundsGetter originalBounds = nullptr;
	inline std::vector<void*> hooks;
	inline std::atomic<bool> enabled{false};
	inline bool observationReady = false, drawHooksReady = false;
	inline bool boundsFixReady = false, boundsFixEnabled = true;
	inline void* poseActor = nullptr; // Identity only, never dereferenced from this cache.
	inline uint64_t poseSerial = 0;
	inline int poseFrame = -1;
	inline UnityVector3 poseMin{}, poseMax{};
	inline thread_local void* boundsScopeActor = nullptr;
	inline thread_local bool boundsConsumed = false;
	inline thread_local Bounds consumedNative{}, consumedDraw{};
	inline uint64_t lastPostPoseSerial = 0;
	inline std::string lastFailure;
	inline DWORD ownerThread = 0;
	inline thread_local void* scopedCamera = nullptr;
	inline thread_local bool observing = false;
	inline void *actorClass = nullptr, *boundsClass = nullptr, *vectorClass = nullptr;
	inline void *centerField = nullptr, *extentsField = nullptr;
	inline const void *boundsMethod = nullptr, *aliveMethod = nullptr;
	inline const void* (*getVirtualMethod)(void*, const void*) = nullptr;
	inline uint64_t lastSeq = 0, requestSeq = 0, sampleIndex = 0;
	inline int remaining = 0, ioFailures = 0;
	inline bool completionLogged = false;
	inline std::atomic<uint64_t> outerCalls{0}, innerCalls{0};
	inline std::string label, outputRoot, nativeSnapshot;
	inline Clock::time_point lastPoll{}, lastSample{}, requestStarted{};

	inline std::string Quote(const std::string& text)
	{
		return "\"" + GakuMotion::jsonlite::EscapeString(text) + "\"";
	}
	inline std::string Pointer(void* value)
	{
		std::ostringstream out;
		out << "\"0x" << std::hex << reinterpret_cast<uintptr_t>(value) << "\"";
		return out.str();
	}
	inline std::ostringstream JsonStream()
	{
		std::ostringstream out;
		out.imbue(std::locale::classic());
		out << std::setprecision(9);
		return out;
	}
	inline bool OnThread() { return ownerThread && GetCurrentThreadId() == ownerThread; }
	inline bool IsAlive(void* object)
	{
		if (!object || !aliveMethod || !g_faceRuntime.invoke || !g_safeBones.unbox) return false;
		void* args[]{object};
		void* exception = nullptr;
		void* boxed = g_faceRuntime.invoke(aliveMethod, nullptr, args, &exception);
		void* value = !exception && boxed ? g_safeBones.unbox(boxed) : nullptr;
		return value && *static_cast<bool*>(value);
	}
	inline bool IsActorClass(void* actor)
	{
		if (!actor || !g_faceRuntime.objectClass || !g_faceRuntime.parentClass) return false;
		void* klass = g_faceRuntime.objectClass(actor);
		for (int i = 0; klass && i < 64; ++i, klass = g_faceRuntime.parentClass(klass))
			if (klass == actorClass) return true;
		return false;
	}
	inline bool Eligible(void* actor)
	{
		if (!enabled.load(std::memory_order_acquire) || !OnThread() || !actor || !g_motionSceneAllowed) return false;
		auto& player = GakuMotion::Core::Instance().player;
		const auto& controller = player.controller;
		void* target = controller.targetActor ? controller.targetActor : controller.currentBinding.actor;
		const auto binding = controller.bindings.find(actor);
		return actor == target && player.state != GakuMotion::PlaybackState::Stopped &&
			controller.CanDriveActor(actor) && binding != controller.bindings.end() && binding->second.bindPoseCaptured;
	}
	inline bool Finite(const UnityVector3& value)
	{
		return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
	}
	inline bool Valid(const Bounds& value)
	{
		return Finite(value.center) && Finite(value.extents) &&
			value.extents.x >= 0 && value.extents.y >= 0 && value.extents.z >= 0;
	}
	inline Bounds ExpandForPose(const Bounds& native)
	{
		if (!Valid(native) || !Finite(poseMin) || !Finite(poseMax)) return native;
		// Preserve the native volume, then include the final skeleton. Its own
		// horizontal half-widths supply the footprint margin; there is no global
		// projection-range multiplier or fixed scene-origin assumption.
		const UnityVector3 low{
			(std::min)(native.center.x - native.extents.x, poseMin.x - native.extents.x),
			(std::min)(native.center.y - native.extents.y, poseMin.y),
			(std::min)(native.center.z - native.extents.z, poseMin.z - native.extents.z)};
		const UnityVector3 high{
			(std::max)(native.center.x + native.extents.x, poseMax.x + native.extents.x),
			(std::max)(native.center.y + native.extents.y, poseMax.y),
			(std::max)(native.center.z + native.extents.z, poseMax.z + native.extents.z)};
		Bounds result{{low.x * .5f + high.x * .5f, low.y * .5f + high.y * .5f, low.z * .5f + high.z * .5f},
			{high.x * .5f - low.x * .5f, high.y * .5f - low.y * .5f, high.z * .5f - low.z * .5f}};
		return Valid(result) ? result : native;
	}
	inline bool PoseCurrent(void* actor)
	{
		const auto& stamp = GakuMotion::Core::Instance().player.controller.footPoseWriteStamp;
		// Core clears the shared stamp's actor/feet before every actor callback,
		// including unrelated actors that never write a new pose. CapturePose has
		// already verified this actor's complete write. Keep that same-frame
		// snapshot while its serial is unchanged; a later failed target callback
		// clears it through AfterPose/CapturePose before the AO draw.
		return boundsFixReady && boundsFixEnabled && Eligible(actor) && poseActor == actor &&
			poseSerial == stamp.serial && poseFrame >= 0 &&
			g_faceUnityFrame && poseFrame == g_faceUnityFrame();
	}
	inline Bounds GetBounds(void* actor, void* method)
	{
		// C++'s 24-byte structure return uses the same Win64 hidden return buffer
		// observed in get_bounds and its interface caller. Forward every call;
		// only this AO draw's target receives a different temporary return value.
		const Bounds native = originalBounds ? originalBounds(actor, method) : Bounds{};
		if (observing || !OnThread()) return native;
		// The separate body-shadow module owns its own synchronous Update scope
		// and A/B switch. No actor cache is changed, and AO has no body scope.
		Bounds body = native;
		try { if (bodyBoundsOverride && bodyBoundsOverride(actor,native,body) && Valid(body)) return body; }
		catch (...) {}
		if (boundsScopeActor != actor || !PoseCurrent(actor)) return native;
		const Bounds draw = ExpandForPose(native);
		boundsConsumed = true; consumedNative = native; consumedDraw = draw;
		return draw;
	}
	inline std::string BoundsJson(const Bounds& value)
	{
		if (!Valid(value)) return "{\"ok\":false}";
		auto out = JsonStream();
		out << "{\"ok\":true,\"center\":[" << value.center.x << ',' << value.center.y << ',' << value.center.z
			<< "],\"extents\":[" << value.extents.x << ',' << value.extents.y << ',' << value.extents.z << "]}";
		return out.str();
	}
	inline void CapturePose(void* actor, uint64_t serialBefore)
	{
		poseActor = nullptr; poseSerial = 0; poseFrame = -1;
		if (!boundsFixReady || !Eligible(actor) || !g_faceUnityFrame) return;
		const auto& controller = GakuMotion::Core::Instance().player.controller;
		const auto& stamp = controller.footPoseWriteStamp;
		if (!controller.fnGetWorldPosition || stamp.actor != actor || stamp.serial == serialBefore ||
			stamp.mask != 3 || !stamp.transforms[0] || !stamp.transforms[1]) return;
		const auto binding = controller.bindings.find(actor);
		if (binding == controller.bindings.end() || binding->second.boneTransforms.size() > 256) return;
		UnityVector3 low{INFINITY, INFINITY, INFINITY}, high{-INFINITY, -INFINITY, -INFINITY};
		int count = 0;
		auto include = [&](void* transform)
		{
			if (!transform) return false;
			float values[3]{NAN, NAN, NAN};
			controller.fnGetWorldPosition(transform, values);
			if (!std::isfinite(values[0]) || !std::isfinite(values[1]) || !std::isfinite(values[2])) return false;
			low.x = (std::min)(low.x, values[0]); low.y = (std::min)(low.y, values[1]); low.z = (std::min)(low.z, values[2]);
			high.x = (std::max)(high.x, values[0]); high.y = (std::max)(high.y, values[1]); high.z = (std::max)(high.z, values[2]);
			++count; return true;
		};
		// These transforms were used by the completed consumer in this callback;
		// no additional managed instances are retained beyond its existing binding.
		if (!include(stamp.transforms[0]) || !include(stamp.transforms[1])) return;
		for (void* transform : binding->second.boneTransforms) if (transform && !include(transform)) return;
		if (count < 3) return;
		poseMin = low; poseMax = high; poseActor = actor; poseSerial = stamp.serial; poseFrame = g_faceUnityFrame();
	}
	inline std::string Vector(void* transform, bool rotation = false)
	{
		auto out = JsonStream();
		auto& controller = GakuMotion::Core::Instance().player.controller;
		if (!IsAlive(transform)) return "{\"ok\":false,\"value\":null}";
		float values[4]{NAN, NAN, NAN, NAN};
		if (rotation)
		{
			if (!controller.fnGetWorldRotation) return "{\"ok\":false,\"value\":null}";
			controller.fnGetWorldRotation(transform, values);
		}
		else
		{
			if (!controller.fnGetWorldPosition) return "{\"ok\":false,\"value\":null}";
			controller.fnGetWorldPosition(transform, values);
		}
		const int count = rotation ? 4 : 3;
		for (int i = 0; i < count; ++i) if (!std::isfinite(values[i])) return "{\"ok\":false,\"value\":null}";
		out << "{\"ok\":true,\"value\":[";
		for (int i = 0; i < count; ++i) out << (i ? "," : "") << values[i];
		out << "]}";
		return out.str();
	}
	inline std::string ReadBounds(void* actor)
	{
		const void* getter = getVirtualMethod ? getVirtualMethod(actor, boundsMethod) : boundsMethod;
		if (!getter) return "{\"ok\":false,\"center\":null,\"extents\":null}";
		void* exception = nullptr;
		void* boxed = g_faceRuntime.invoke(getter, actor, nullptr, &exception);
		if (exception || !boxed) return "{\"ok\":false,\"center\":null,\"extents\":null}";
		// Field API reads the boxed Bounds without guessing its payload/header.
		UnityVector3 center{NAN, NAN, NAN}, extents{NAN, NAN, NAN};
		g_faceRuntime.readField(boxed, centerField, &center);
		g_faceRuntime.readField(boxed, extentsField, &extents);
		const float values[]{center.x, center.y, center.z, extents.x, extents.y, extents.z};
		for (float value : values) if (!std::isfinite(value)) return "{\"ok\":false,\"center\":null,\"extents\":null}";
		if (extents.x < 0 || extents.y < 0 || extents.z < 0) return "{\"ok\":false,\"center\":null,\"extents\":null}";
		auto out = JsonStream();
		out << "{\"ok\":true,\"source\":\"" << (getVirtualMethod ? "virtual_getter" : "base_getter")
			<< "\",\"getter\":" << Pointer(const_cast<void*>(getter)) << ",\"center\":[" << center.x << ',' << center.y << ',' << center.z
			<< "],\"extents\":[" << extents.x << ',' << extents.y << ',' << extents.z << "]}";
		return out.str();
	}
	inline std::string NativeMethod(const char* name, const void* method)
	{
		const std::string fullName(name);
		const auto separator = fullName.rfind('.');
		const std::string className = separator == std::string::npos ? "" : fullName.substr(0, separator);
		const std::string methodName = separator == std::string::npos ? fullName : fullName.substr(separator + 1);
		void* pointer = method ? static_cast<const Il2CppMethodInfo*>(method)->methodPointer : nullptr;
		MEMORY_BASIC_INFORMATION region{};
		const bool queried = pointer && VirtualQuery(pointer, &region, sizeof(region)) == sizeof(region);
		const DWORD access = region.Protect & 0xff;
		const bool readable = queried && region.State == MEM_COMMIT && !(region.Protect & PAGE_GUARD) &&
			(access == PAGE_EXECUTE_READ || access == PAGE_EXECUTE_READWRITE || access == PAGE_EXECUTE_WRITECOPY);
		std::vector<unsigned char> bytes;
		SIZE_T copied = 0;
		bool read = false;
		if (readable)
		{
			const auto address = reinterpret_cast<uintptr_t>(pointer);
			const auto end = reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;
			const size_t count = end > address ? std::min<size_t>(2048, end - address) : 0;
			bytes.resize(count);
			read = count && ReadProcessMemory(GetCurrentProcess(), pointer, bytes.data(), count, &copied) && copied == count;
		}
		auto out = JsonStream();
		out << "{\"class\":" << Quote(className) << ",\"name\":" << Quote(methodName)
			<< ",\"method\":" << Pointer(const_cast<void*>(method))
			<< ",\"pointer\":" << Pointer(pointer) << ",\"query_ok\":" << (queried ? "true" : "false")
			<< ",\"read_ok\":" << (read ? "true" : "false") << ",\"protection\":" << region.Protect
			<< ",\"requested_bytes\":2048,\"window_only\":true,\"function_extent_known\":false,\"rva\":\"0x" << std::hex;
		if (queried && region.AllocationBase == g_gameAssembly)
			out << reinterpret_cast<uintptr_t>(pointer) - reinterpret_cast<uintptr_t>(g_gameAssembly);
		else out << '0';
		out << "\",\"read_bytes\":" << std::dec << (read ? copied : 0) << ",\"bytes\":\"";
		if (read) for (unsigned char byte : bytes) out << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(byte);
		out << "\"}";
		return out.str();
	}
	inline void WriteNativeSnapshot()
	{
		if (nativeSnapshot.empty()) return;
		std::ofstream out(GakuMotion::MotionPathFromUtf8(outputRoot + "/ground_shadow_methods.json"), std::ios::binary | std::ios::trunc);
		if (!out) { ++ioFailures; return; }
		out << nativeSnapshot;
		if (!out) ++ioFailures;
	}
	inline void PollRequest(Clock::time_point now)
	{
		if (lastPoll.time_since_epoch().count() && now - lastPoll < std::chrono::milliseconds(500)) return;
		lastPoll = now;
		if (remaining > 0 && now - requestStarted > std::chrono::seconds(30))
		{
			remaining = 0;
			completionLogged = true;
			if (g_hostContext.logInfo) g_hostContext.logInfo(
				"[GakuMotion][GroundShadowProbe] seq=%llu timeout samples=%llu outer_calls=%llu inner_calls=%llu io_failures=%d",
				static_cast<unsigned long long>(requestSeq), static_cast<unsigned long long>(sampleIndex),
				static_cast<unsigned long long>(outerCalls.load()), static_cast<unsigned long long>(innerCalls.load()), ioFailures);
		}
		auto& core = GakuMotion::Core::Instance();
		GakuMotion::jsonlite::Value request;
		std::string error;
		if (!GakuMotion::jsonlite::ParseFile(core.configRoot + "/ground_shadow_probe.request.json", request, error) || !request.IsObject()) return;
		const auto* sequence = request.Find("seq");
		const double number = sequence && sequence->IsNumber() ? sequence->AsDouble(0) : 0;
		if (!std::isfinite(number) || number < 1 || number > 9007199254740991.0 || std::floor(number) != number) return;
		const uint64_t seq = static_cast<uint64_t>(number);
		const double pid = request.Find("pid") ? request.Find("pid")->AsDouble(-1) : 0;
		if (seq <= lastSeq || (pid != 0 && pid != GetCurrentProcessId())) return;
		lastSeq = requestSeq = seq;
		const auto* samples = request.Find("samples");
		const double count = samples && samples->IsNumber() ? samples->AsDouble(1) : 1;
		remaining = std::isfinite(count) ? static_cast<int>(std::clamp(count, 1.0, 16.0)) : 1;
		label = request.StringPath("label", "");
		if (const auto* fix = request.Find("bounds_fix"); fix && fix->IsBool()) boundsFixEnabled = fix->AsBool(true);
		if (label.size() > 256) label = "label-too-long";
		outputRoot = core.configRoot;
		requestStarted = now;
		lastSample = {};
		sampleIndex = 0;
		completionLogged = false;
		WriteNativeSnapshot();
		if (g_hostContext.logInfo) g_hostContext.logInfo(
			"[GakuMotion][GroundShadowProbe] seq=%llu armed samples=%d outer_calls=%llu inner_calls=%llu native_output=%s/ground_shadow_methods.json io_failures=%d",
			static_cast<unsigned long long>(requestSeq), remaining, static_cast<unsigned long long>(outerCalls.load()),
			static_cast<unsigned long long>(innerCalls.load()), outputRoot.c_str(), ioFailures);
	}
	inline void Observe(void* pass, void* cmd, void* actor, void* planes,
		const char* phase = "RenderActorAO.entry", bool cameraFromParent = true)
	{
		if (observing || !Eligible(actor)) return;
		const auto now = Clock::now();
		PollRequest(now);
		if (remaining <= 0 || now - requestStarted > std::chrono::seconds(30) ||
			(lastSample.time_since_epoch().count() && now - lastSample < std::chrono::milliseconds(250))) return;
		observing = true;
		struct Finish { ~Finish() { observing = false; } } finish;
		if (!IsActorClass(actor) || !IsAlive(actor)) return;
		lastSample = now;
		--remaining;
		++sampleIndex;
		auto& core = GakuMotion::Core::Instance();
		auto& controller = core.player.controller;
		auto bone = [&](int hid) { return controller.fnGetBoneTransform ? controller.fnGetBoneTransform(actor, hid) : nullptr; };
		void* actorTransform = controller.fnGetComponentTransform ? controller.fnGetComponentTransform(actor) : nullptr;
		void* cameraTransform = scopedCamera && IsAlive(scopedCamera) && controller.fnGetComponentTransform
			? controller.fnGetComponentTransform(scopedCamera) : nullptr;
		// Native RenderActorAO receives ref CameraPlanes, a borrowed value-type
		// address. Keep it opaque; it is never a managed Plane[] object.
		const auto offset = controller.initialPositionOffset;
		auto out = JsonStream();
		out << "{\"schema\":1,\"phase\":" << Quote(phase) << ",\"pid\":" << GetCurrentProcessId()
			<< ",\"seq\":" << requestSeq << ",\"sample\":" << sampleIndex << ",\"label\":" << Quote(label)
			<< ",\"frame\":" << (g_faceUnityFrame ? g_faceUnityFrame() : -1)
			<< ",\"clip_time\":" << core.player.currentTime << ",\"scene\":" << Quote(g_currentSceneName)
			<< ",\"pose_serial\":" << controller.footPoseWriteStamp.serial << ",\"pass\":" << Pointer(pass)
			<< ",\"command_buffer\":" << Pointer(cmd) << ",\"actor\":" << Pointer(actor)
			<< ",\"culling_planes\":" << Pointer(planes) << ",\"culling_planes_kind\":\"ref CameraPlanes\""
			<< ",\"camera\":" << Pointer(scopedCamera)
			<< ",\"planes_count_ok\":false,\"planes_count\":0"
			<< ",\"outer_calls\":" << outerCalls.load() << ",\"inner_calls\":" << innerCalls.load()
			<< ",\"camera_from_parent\":" << (scopedCamera && cameraFromParent ? "true" : "false")
			<< ",\"actor_shadow_mode\":" << core.camera.settings.actorShadowExperimentMode
			<< ",\"initial_offset\":[" << offset.x << ',' << offset.y << ',' << offset.z << ']'
			<< ",\"actor_position\":" << Vector(actorTransform) << ",\"hips_position\":" << Vector(bone(0))
			<< ",\"left_foot_position\":" << Vector(bone(5)) << ",\"right_foot_position\":" << Vector(bone(6))
			<< ",\"camera_position\":" << Vector(cameraTransform) << ",\"camera_rotation\":" << Vector(cameraTransform, true)
			<< ",\"bounds\":" << ReadBounds(actor)
			<< ",\"bounds_fix_enabled\":" << (boundsFixEnabled ? "true" : "false")
			<< ",\"ao_bounds_consumed\":" << (boundsConsumed && boundsScopeActor == actor ? "true" : "false");
		if (boundsConsumed && boundsScopeActor == actor)
			out << ",\"ao_native_bounds\":" << BoundsJson(consumedNative) << ",\"ao_draw_bounds\":" << BoundsJson(consumedDraw);
		out << ",\"io_failures\":" << ioFailures << "}\n";
		std::ofstream file(GakuMotion::MotionPathFromUtf8(outputRoot + "/ground_shadow_probe.jsonl"), std::ios::binary | std::ios::app);
		if (file) file << out.str();
		if (!file) ++ioFailures;
		if (!remaining && !completionLogged)
		{
			completionLogged = true;
			if (g_hostContext.logInfo) g_hostContext.logInfo(
				"[GakuMotion][GroundShadowProbe] seq=%llu samples=%llu io_failures=%d output=%s/ground_shadow_probe.jsonl",
				static_cast<unsigned long long>(requestSeq), static_cast<unsigned long long>(sampleIndex), ioFailures, outputRoot.c_str());
		}
	}
	// This fallback records the exact getter after the final body/AO update. It
	// is explicitly a post-pose observation, never evidence of GPU consumption.
	inline void AfterPose(void* actor, uint64_t serialBefore)
	{
		if (!OnThread()) return;
		if (!observationReady || !Eligible(actor))
		{
			// The host dispatches every actor. An unrelated actor's later callback
			// must not erase the target's completed pose before the AO draw.
			if (!observationReady || actor == poseActor) poseActor = nullptr;
			return;
		}
		try { PollRequest(Clock::now()); CapturePose(actor, serialBefore); }
		catch (...) { poseActor = nullptr; ++ioFailures; }
		if (drawHooksReady) return;
		const auto& stamp = GakuMotion::Core::Instance().player.controller.footPoseWriteStamp;
		if (stamp.actor != actor || stamp.serial == serialBefore || stamp.serial == lastPostPoseSerial) return;
		lastPostPoseSerial = stamp.serial;
		void* previous = scopedCamera;
		// Without a parent draw there is no verified rendering camera. Keep the
		// field null rather than assigning Camera.main to a different consumer.
		scopedCamera = nullptr;
		try { Observe(nullptr, nullptr, actor, nullptr, "post_pose", false); }
		catch (...) { ++ioFailures; }
		scopedCamera = previous;
	}
	inline void RenderActorAO(void* pass, void* cmd, void* actor, void* planes, void* method)
	{
		++innerCalls;
		const auto previousActor = boundsScopeActor;
		const bool previousConsumed = boundsConsumed;
		const Bounds previousNative = consumedNative, previousDraw = consumedDraw;
		boundsScopeActor = OnThread() && PoseCurrent(actor) ? actor : nullptr;
		boundsConsumed = false;
		struct Restore
		{
			void* actor; bool consumed; Bounds native, draw;
			~Restore() { boundsScopeActor = actor; boundsConsumed = consumed; consumedNative = native; consumedDraw = draw; }
		} restore{previousActor, previousConsumed, previousNative, previousDraw};
		if (originalInner) originalInner(pass, cmd, actor, planes, method);
		try { if (drawHooksReady) Observe(pass, cmd, actor, planes, "RenderActorAO.exit"); } catch (...) { ++ioFailures; }
	}
	inline void RenderActorAOs(void* pass, void* cmd, void* camera, void* method)
	{
		++outerCalls;
		void* previous = scopedCamera;
		if (OnThread()) scopedCamera = camera;
		try
		{
			if (drawHooksReady && enabled.load(std::memory_order_acquire) && OnThread())
			{
				auto& controller = GakuMotion::Core::Instance().player.controller;
				void* target = controller.targetActor ? controller.targetActor : controller.currentBinding.actor;
				if (Eligible(target)) PollRequest(Clock::now());
			}
		}
		catch (...) { ++ioFailures; }
		try { if (originalOuter) originalOuter(pass, cmd, camera, method); }
		catch (...) { scopedCamera = previous; throw; }
		scopedCamera = previous;
	}
	inline void Shutdown()
	{
		bodyBoundsOverride = nullptr;
		enabled.store(false, std::memory_order_release);
		observationReady = drawHooksReady = false;
		boundsFixReady = false; boundsFixEnabled = true;
		poseActor = boundsScopeActor = nullptr; poseSerial = 0; poseFrame = -1; boundsConsumed = false;
		lastPostPoseSerial = 0;
		std::vector<void*> retained;
		for (void* target : hooks)
		{
			bool released = false;
			if (GakuMotion::host::active)
				released = GakuMotion::host::hooks.release &&
					GakuMotion::host::hooks.release(GakuMotion::host::hooks.context, target) == GAKU_OK;
			else
			{
				const auto disabled = MH_DisableHook(target);
				if (disabled == MH_ERROR_NOT_CREATED) released = true;
				else if (disabled == MH_OK || disabled == MH_ERROR_DISABLED)
				{
					const auto removed = MH_RemoveHook(target);
					released = removed == MH_OK || removed == MH_ERROR_NOT_CREATED;
				}
			}
			if (!released) retained.push_back(target);
		}
		hooks = std::move(retained);
		// If release failed, a detour may still be installed. Keep its trampoline
		// so the disabled observer continues forwarding the native draw.
		if (hooks.empty()) { originalOuter = nullptr; originalInner = nullptr; originalBounds = nullptr; }
		else if (g_hostContext.logWarn) g_hostContext.logWarn(
			"[GakuMotion][GroundShadowProbe] hook release incomplete; observation disabled, native forwarding retained");
		ownerThread = 0; scopedCamera = nullptr; observing = false;
		actorClass = boundsClass = vectorClass = centerField = extentsField = nullptr;
		boundsMethod = aliveMethod = nullptr;
		getVirtualMethod = nullptr;
		lastSeq = requestSeq = sampleIndex = 0; remaining = ioFailures = 0; completionLogged = false;
		outerCalls = innerCalls = 0;
		label.clear(); outputRoot.clear(); nativeSnapshot.clear();
		lastPoll = lastSample = requestStarted = {};
	}
	inline bool Acquire(void* target, void* detour, void** original)
	{
		auto fail = [](const std::string& detail) { lastFailure = detail; return false; };
		if (!target || !detour || !original) return fail("missing_pointer");
		if (GakuMotion::host::active)
		{
			if (!GakuMotion::host::hooks.acquire || !GakuMotion::host::hooks.release) return fail("host_api_missing");
			const auto result = GakuMotion::host::hooks.acquire(GakuMotion::host::hooks.context, target, detour, original);
			if (result != GAKU_OK) return fail("host_result=" + std::to_string(result));
			hooks.push_back(target);
			return *original ? true : fail("host_null_trampoline");
		}
		const auto init = MH_Initialize();
		if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) return fail("minhook_init=" + std::to_string(init));
		const auto created = MH_CreateHook(target, detour, original);
		if (created != MH_OK) return fail("minhook_create=" + std::to_string(created));
		hooks.push_back(target);
		if (!*original) return fail("minhook_null_trampoline");
		const auto activated = MH_EnableHook(target);
		return activated == MH_OK ? true : fail("minhook_enable=" + std::to_string(activated));
	}
	inline bool Reject(const std::string& reason)
	{
		lastFailure = reason;
		return false;
	}
	inline bool ResolveObservationMetadata(void* concreteActor, void* bounds, void* vector, void* unityObject)
	{
		observationReady = false;
		actorClass = boundsClass = vectorClass = centerField = extentsField = nullptr;
		boundsMethod = aliveMethod = nullptr;
		const auto& metadata = StageLoader::metadata;
		if (!concreteActor) return Reject("class:VLActorController");
		if (!bounds) return Reject("class:Bounds");
		if (!vector) return Reject("class:Vector3");
		if (!unityObject) return Reject("class:Object");
		if (!StageLoader::valueSize || !g_faceRuntime.findField ||
			!g_faceRuntime.fieldType || !g_faceRuntime.fieldFlags || !g_faceRuntime.readField ||
			!g_faceRuntime.invoke || !g_safeBones.unbox || !g_faceRuntime.objectClass || !g_faceRuntime.parentClass ||
			!metadata.classFromType || !metadata.typeCode || !metadata.byRef || !il2cpp_field_get_offset)
			return Reject("observation:required_api");
		uint32_t alignment = 0;
		const int size = StageLoader::valueSize(vector, &alignment);
		if (size != sizeof(UnityVector3)) return Reject("Vector3:size=" + std::to_string(size));
		const char* names[]{"x", "y", "z"};
		int firstOffset = -1;
		for (int i = 0; i < 3; ++i)
		{
			void* f = g_faceRuntime.findField(vector, names[i]);
			void* type = f ? g_faceRuntime.fieldType(f) : nullptr;
			if (!f || (g_faceRuntime.fieldFlags(f) & 0x10) || !metadata.Matches(type, {0x0c}))
				return Reject(std::string("Vector3:field:") + names[i]);
			const int offset = il2cpp_field_get_offset(f);
			if (!i) firstOffset = offset;
			if ((firstOffset != 0 && firstOffset != 16) || offset != firstOffset + i * sizeof(float))
				return Reject(std::string("Vector3:offset:") + names[i] + "=" + std::to_string(offset));
		}
		auto field = [&](const char* name) -> void*
		{
			void* f = g_faceRuntime.findField(bounds, name);
			void* type = f ? g_faceRuntime.fieldType(f) : nullptr;
			return f && !(g_faceRuntime.fieldFlags(f) & 0x10) && metadata.Matches(type, {0x11, vector}) ? f : nullptr;
		};
		void* center = field("m_Center");
		void* extents = field("m_Extents");
		if (StageLoader::valueSize(bounds, &alignment) != sizeof(Bounds)) return Reject("Bounds:size");
		if (center && extents)
		{
			const int offset = il2cpp_field_get_offset(center);
			if ((offset != 0 && offset != 16) || il2cpp_field_get_offset(extents) != offset + 12)
				return Reject("Bounds:field_offsets");
		}
		const void* getter = metadata.Find(concreteActor, "get_bounds", false, {0x11, bounds}, {});
		const void* alive = metadata.Find(unityObject, "op_Implicit", true, {0x02}, {{0x12, unityObject}});
		if (!center) return Reject("Bounds:field:m_Center");
		if (!extents) return Reject("Bounds:field:m_Extents");
		if (!getter) return Reject("method:VLActorController.get_bounds");
		if (!alive) return Reject("method:Object.op_Implicit");
		actorClass = concreteActor; boundsClass = bounds; vectorClass = vector;
		centerField = center; extentsField = extents; boundsMethod = getter; aliveMethod = alive;
		observationReady = true;
		lastFailure.clear();
		return true;
	}
	inline bool ResolveMetadata(void* downscale, void* command, void* camera, void* actorInterface,
		void* cameraPlanes, void* concreteActor, void* bounds, void* vector, void* unityObject,
		const void*& outer, const void*& inner)
	{
		outer = inner = nullptr;
		if (!ResolveObservationMetadata(concreteActor, bounds, vector, unityObject)) return false;
		if (!downscale) return Reject("class:DownscalePass");
		if (!command) return Reject("class:CommandBuffer");
		if (!camera) return Reject("class:Camera");
		if (!actorInterface) return Reject("class:IVLActorController");
		if (!cameraPlanes) return Reject("class:CameraPlanes");
		const auto& metadata = StageLoader::metadata;
		const void* parent = metadata.Find(downscale, "RenderActorAOs", false, {0x01}, {{0x12, command}, {0x12, camera}});
		const void* child = metadata.Find(downscale, "RenderActorAO", false, {0x01},
			{{0x12, command}, {0x12, actorInterface}, {0x11, cameraPlanes, true}});
		if (!parent) return Reject("method:DownscalePass.RenderActorAOs");
		if (!child) return Reject("method:DownscalePass.RenderActorAO");
		outer = parent; inner = child;
		return true;
	}
	inline bool Install()
	{
		if (enabled.load(std::memory_order_acquire)) return drawHooksReady;
		if (!hooks.empty()) return Reject("hook:retained_lease");
		ownerThread = GetCurrentThreadId();
		bool bootstrapWritten = false;
		try { bootstrapWritten = GroundShadowProbeBootstrap::Capture(g_gameAssembly, GakuMotion::Core::Instance().configRoot); }
		catch (...) { ++ioFailures; }
		auto finish = [&]()
		{
			enabled.store(observationReady, std::memory_order_release);
			uint64_t seq = 0;
			const auto& root = GakuMotion::Core::Instance().configRoot;
			if (GroundShadowProbeBootstrap::Request(root, seq))
			{
				auto status = JsonStream();
				status << "{\"schema\":1,\"pid\":" << GetCurrentProcessId() << ",\"seq\":" << seq
					<< ",\"bootstrap_written\":" << (bootstrapWritten ? "true" : "false")
					<< ",\"post_pose_ready\":" << (observationReady ? "true" : "false")
					<< ",\"draw_ready\":" << (drawHooksReady ? "true" : "false")
					<< ",\"bounds_fix_ready\":" << (boundsFixReady ? "true" : "false")
					<< ",\"reason\":" << Quote(lastFailure.empty() ? "ready" : lastFailure) << "}\n";
				if (!GroundShadowProbeBootstrap::Write(root + "/ground_shadow_status.json", status.str())) ++ioFailures;
			}
			if (g_hostContext.logInfo) g_hostContext.logInfo(
				"[GakuMotion][GroundShadowProbe] bootstrap_written=%d post_pose_ready=%d draw_ready=%d bounds_fix_ready=%d reason=%s",
				bootstrapWritten, observationReady, drawHooksReady, boundsFixReady, lastFailure.empty() ? "ready" : lastFailure.c_str());
			return drawHooksReady;
		};
		getVirtualMethod = reinterpret_cast<decltype(getVirtualMethod)>(GetProcAddress(
			g_gameAssembly, "il2cpp_object_get_virtual_method"));
		auto unity = [](const char* name) { return FindClass("UnityEngine.CoreModule.dll", "UnityEngine", name); };
		void* downscale = FindClass("vl-unity.Runtime.dll", "VL.Rendering", "DownscalePass");
		void* concreteActor = FindClass("vl-unity.Runtime.dll", "VL.Core", "VLActorController");
		void* interfaceClass = FindClass("vl-unity.Runtime.dll", "VL", "IVLActorController");
		void* command = FindClass("UnityEngine.CoreModule.dll", "UnityEngine.Rendering", "CommandBuffer");
		void* cameraPlanes = FindClass("Unity.RenderPipelines.Universal.Runtime.dll", "VL.Rendering", "CameraPlanes");
		const void *outer = nullptr, *inner = nullptr;
		if (!ResolveMetadata(downscale, command, unity("Camera"), interfaceClass, cameraPlanes,
			concreteActor, unity("Bounds"), unity("Vector3"), unity("Object"), outer, inner))
			return finish();
		const auto& metadata = StageLoader::metadata;
		void* calc = FindClass("vl-unity.Runtime.dll", "VL.AABB", "AABBCalcJob");
		void* ao = FindClass("vl-unity.Runtime.dll", "VL", "VLActorAO");
		auto snapshot = JsonStream();
		snapshot << "{\"schema\":1,\"pid\":" << GetCurrentProcessId()
			<< ",\"captured_before_own_hooks\":true,\"methods\":["
			<< NativeMethod("DownscalePass.RenderActorAO", inner) << ','
			<< NativeMethod("DownscalePass.RenderActorAOs", outer) << ','
			<< NativeMethod("VLActorController.get_bounds", boundsMethod) << ','
			<< NativeMethod("VLActorController.RunAABBJob", metadata.Find(concreteActor, "RunAABBJob", false, {0x01}, {})) << ','
			<< NativeMethod("VLActorController.ApplyAABB", metadata.Find(concreteActor, "ApplyAABB", false, {0x01}, {})) << ','
			<< NativeMethod("AABBCalcJob.Execute", metadata.Find(calc, "Execute", false, {0x01}, {{0x08}})) << ','
			<< NativeMethod("VLActorAO.OnLateUpdate", metadata.Find(ao, "OnLateUpdate", false, {0x01}, {})) << "]}\n";
		// The independent bootstrap owns the broader raw window output. Only
		// use this exact-signature snapshot when it did not write both files.
		if (!bootstrapWritten) nativeSnapshot = snapshot.str();
		void* outerPointer = static_cast<const Il2CppMethodInfo*>(outer)->methodPointer;
		void* innerPointer = static_cast<const Il2CppMethodInfo*>(inner)->methodPointer;
		const bool acquiredOuter = Acquire(outerPointer, reinterpret_cast<void*>(RenderActorAOs), reinterpret_cast<void**>(&originalOuter));
		const bool acquiredInner = acquiredOuter && Acquire(innerPointer, reinterpret_cast<void*>(RenderActorAO), reinterpret_cast<void**>(&originalInner));
		void* boundsPointer = static_cast<const Il2CppMethodInfo*>(boundsMethod)->methodPointer;
		const bool acquiredBounds = acquiredInner && Acquire(boundsPointer, reinterpret_cast<void*>(GetBounds), reinterpret_cast<void**>(&originalBounds));
		if (!acquiredOuter || !acquiredInner || !acquiredBounds)
		{
			const std::string reason = std::string("hook:") + (!acquiredOuter ? "outer:" : !acquiredInner ? "inner:" : "bounds:") + lastFailure;
			Shutdown();
			ownerThread = GetCurrentThreadId();
			getVirtualMethod = reinterpret_cast<decltype(getVirtualMethod)>(GetProcAddress(
				g_gameAssembly, "il2cpp_object_get_virtual_method"));
			ResolveObservationMetadata(concreteActor, unity("Bounds"), unity("Vector3"), unity("Object"));
			lastFailure = reason;
			return finish();
		}
		drawHooksReady = true;
		boundsFixReady = true;
		lastFailure.clear();
		return finish();
	}
}
