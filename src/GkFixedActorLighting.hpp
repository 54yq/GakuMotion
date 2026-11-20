#pragma once

// Included in GakuMotionPlugin.cpp after the scene-owner and stage adapters.
namespace FixedActorLighting
{
	using Type = GakuMotion::ExactMethodMetadata::Type;
	struct Color { float r, g, b, a; };
	struct Angles { float x, y; };
	struct Sample {
		Color key, shade, fill, rim;
		Angles keyAngle, rimAngle;
		float rimPower;
	};
	inline Sample sampledLight{};
	inline uintptr_t sampledOwner{};
	inline int sampledScene{};
	inline bool appliedSampled{};
	struct Root {
		StageLoader::Handle handle{};
		void* Get() const { return handle ? SceneOwnerRuntime::handleTarget(handle) : nullptr; }
		bool Keep(void* object) {
			if (object) handle = StageLoader::newHandle(object, false);
			return Get() != nullptr;
		}
		void Reset() {
			if (handle) StageLoader::freeHandle(handle);
			handle = nullptr;
		}
		~Root() { Reset(); }
		Root() = default;
		Root(const Root&) = delete;
		Root& operator=(const Root&) = delete;
	};
	inline Root volumeObject, profileObject, parameterObject, volumeComponent;
	inline uint64_t appliedStyleRevision{};
	inline uintptr_t owner{};
	inline int scene{}, layer{-1};
	inline bool ready{};
	inline GakuMotion::Core::ActorLightingStrengths appliedStrengths;
	inline std::chrono::steady_clock::time_point nextCameraCheck{};
	inline void *actorClass{}, *volumeClass{}, *profileClass{}, *colorClass{}, *anglesClass{}, *cameraDataClass{};
	inline void* (*objectNew)(void*){};
	inline const void *ctor{}, *alive{}, *destroy{}, *addComponent{}, *getComponent{}, *setLayer{},
		*createProfile{}, *addParameter{}, *setProfile{}, *setGlobal{}, *getVolumeMask{};

	inline bool Call(const void* method, void* object, void** args = nullptr, void** result = nullptr)
	{
		if (!method || !g_faceRuntime.invoke) return false;
		void* exception = nullptr;
		void* value = g_faceRuntime.invoke(method, object, args, &exception);
		if (result) *result = value;
		return !exception;
	}
	inline bool Resolve()
	{
		if (ready) return true;
		if (!StageLoader::metadata.byRef || !StageLoader::metadata.typeCode || !StageLoader::newHandle || !StageLoader::freeHandle ||
			!SceneOwnerRuntime::handleTarget || !StageLoader::stringNew || !StageLoader::setActive ||
			!g_safeBones.unbox || !g_faceRuntime.readField || !g_faceRuntime.writeField) return false;
		const auto unity = [](const char* name) { return FindClass("UnityEngine.CoreModule.dll", "UnityEngine", name); };
		const auto urp = [](const char* space, const char* name) { return FindClass("Unity.RenderPipelines.Universal.Runtime.dll", space, name); };
		const auto core = [](const char* name) { return FindClass("Unity.RenderPipelines.Core.Runtime.dll", "UnityEngine.Rendering", name); };
		const auto find = [](void* klass, const char* name, bool stat, Type result, std::initializer_list<Type> args) {
			if (!klass) return static_cast<const void*>(nullptr);
			if ((result.code == 0x11 || result.code == 0x12) && !result.klass) return static_cast<const void*>(nullptr);
			for (auto type : args) if ((type.code == 0x11 || type.code == 0x12) && !type.klass) return static_cast<const void*>(nullptr);
			return StageLoader::metadata.Find(klass, name, stat, result, args);
		};
		void* typeClass = FindClass("mscorlib.dll", "System", "Type");
		void* objectClass = unity("Object");
		void* scriptable = unity("ScriptableObject");
		void* maskClass = unity("LayerMask");
		cameraDataClass = urp("UnityEngine.Rendering.Universal", "UniversalAdditionalCameraData");
		actorClass = urp("VL.Rendering", "VLActorParameter");
		volumeClass = core("Volume"); profileClass = core("VolumeProfile");
		colorClass = unity("Color"); anglesClass = unity("Vector2");
		const auto layout = [](void* klass, size_t size) {
			uint32_t alignment{};
			return klass && StageLoader::valueSize && StageLoader::valueSize(klass, &alignment) == static_cast<int>(size) &&
				alignment == alignof(float);
		};
		if (!layout(colorClass, sizeof(Color)) || !layout(anglesClass, sizeof(Angles)) || !layout(maskClass, sizeof(uint32_t))) return false;
		ctor = find(StageLoader::goClass, ".ctor", false, {0x01}, {{0x0e}});
		alive = find(objectClass, "op_Implicit", true, {0x02}, {{0x12, objectClass}});
		destroy = find(objectClass, "Destroy", true, {0x01}, {{0x12, objectClass}});
		addComponent = find(StageLoader::goClass, "AddComponent", false, {0x12, StageLoader::componentClass}, {{0x12, typeClass}});
		getComponent = find(StageLoader::goClass, "GetComponent", false, {0x12, StageLoader::componentClass}, {{0x12, typeClass}});
		setLayer = find(StageLoader::goClass, "set_layer", false, {0x01}, {{0x08}});
		createProfile = find(scriptable, "CreateInstance", true, {0x12, scriptable}, {{0x12, typeClass}});
		addParameter = find(profileClass, "Add", false, {0x12, core("VolumeComponent")}, {{0x12, typeClass}, {0x02}});
		setProfile = find(volumeClass, "set_profile", false, {0x01}, {{0x12, profileClass}});
		setGlobal = find(volumeClass, "set_isGlobal", false, {0x01}, {{0x02}});
		getVolumeMask = find(cameraDataClass, "get_volumeLayerMask", false, {0x11, maskClass}, {});
		objectNew = reinterpret_cast<decltype(objectNew)>(GetProcAddress(g_gameAssembly, "il2cpp_object_new"));
		ready = actorClass && ctor && alive && destroy && addComponent && getComponent && setLayer &&
			createProfile && addParameter && setProfile && setGlobal && getVolumeMask && objectNew &&
			StageLoader::cameraMain && StageLoader::componentGo && il2cpp_type_get_object && il2cpp_class_get_type;
		return ready;
	}
	inline void* ManagedType(void* klass)
	{
		return il2cpp_type_get_object(il2cpp_class_get_type(klass));
	}
	inline bool Destroy(Root& root, bool deactivate = false)
	{
		if (!root.handle) return true;
		void* object = root.Get();
		void* args[]{object};
		void* boxed = nullptr;
		if (!object || !Call(alive, nullptr, args, &boxed) || !boxed || !g_safeBones.unbox(boxed)) return false;
		if (*static_cast<bool*>(g_safeBones.unbox(boxed))) {
			bool off = false;
			void* offArgs[]{&off};
			if (deactivate && !Call(StageLoader::setActive, object, offArgs)) return false;
			if (!Call(destroy, nullptr, args)) return false;
		}
		root.Reset();
		return true;
	}
	inline bool Release()
	{
		if ((volumeObject.handle || profileObject.handle || parameterObject.handle) && !ProtectionOnUnityThread()) return false;
		if (!Destroy(volumeObject, true)) return false;
		volumeComponent.Reset();
		const bool parameter = Destroy(parameterObject);
		const bool profile = Destroy(profileObject);
		if (!parameter || !profile) return false;
		owner = 0; scene = 0; layer = -1; nextCameraCheck = {};
		return true;
	}
	inline int CameraLayer()
	{
		void* camera = StageLoader::Invoke(StageLoader::cameraMain);
		void* go = camera ? StageLoader::Invoke(StageLoader::componentGo, camera) : nullptr;
		Root type;
		if (!go || !type.Keep(ManagedType(cameraDataClass))) return -1;
		void* args[]{type.Get()};
		void* data = StageLoader::Invoke(getComponent, go, args);
		void* boxed = data ? StageLoader::Invoke(getVolumeMask, data) : nullptr;
		const auto* mask = boxed ? static_cast<const uint32_t*>(g_safeBones.unbox(boxed)) : nullptr;
		if (!mask || !*mask) return -1;
		for (int bit = 0; bit < 32; ++bit) if ((*mask & (uint32_t{1} << bit)) != 0) return bit;
		return -1;
	}
	inline bool Write(void* object, const char* name, Type type, const void* value)
	{
		void* field = object ? g_faceRuntime.InstanceField(object, name, type.code) : nullptr;
		if (!field || !StageLoader::metadata.Matches(g_faceRuntime.fieldType(field), type)) return false;
		g_faceRuntime.writeField(object, field, const_cast<void*>(value));
		return true;
	}
	inline void* FindParameter(void* actor, const char* name)
	{
		if (!actor) return nullptr;
		void* field = g_faceRuntime.InstanceField(actor, name, 0x12);
		if (!field) field = g_faceRuntime.InstanceField(actor, name, 0x15);
		if (!field) return nullptr;
		void* parameter = nullptr;
		g_faceRuntime.readField(actor, field, &parameter);
		return parameter;
	}
	inline bool Parameter(const char* name, Type type, const void* value)
	{
		Root root;
		const bool overrideState = true;
		return root.Keep(FindParameter(parameterObject.Get(), name)) && Write(root.Get(), "m_Value", type, value) &&
			Write(root.Get(), "m_OverrideState", {0x02}, &overrideState);
	}
	template<class T>
	inline bool ReadParameter(void* actor, const char* name, Type type, T& value)
	{
		Root parameter;
		if (!parameter.Keep(FindParameter(actor, name))) return false;
		void* field = g_faceRuntime.InstanceField(parameter.Get(), "m_Value", type.code);
		if (!field || !StageLoader::metadata.Matches(g_faceRuntime.fieldType(field), type)) return false;
		g_faceRuntime.readField(parameter.Get(), field, &value);
		return true;
	}
	inline float Strength(Color value)
	{
		return (std::max)({std::fabs(value.r), std::fabs(value.g), std::fabs(value.b)});
	}
	inline bool ReadNativeSample(Sample& sample)
	{
		if (!Resolve()) return false;
		const auto rendering = [](const char* name) {
			return FindClass("Unity.RenderPipelines.Core.Runtime.dll", "UnityEngine.Rendering", name);
		};
		auto& m = StageLoader::metadata;
		void* managerClass = rendering("VolumeManager");
		void* stackClass = rendering("VolumeStack");
		void* componentClass = rendering("VolumeComponent");
		void* typeClass = FindClass("mscorlib.dll", "System", "Type");
		void* maskClass = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "LayerMask");
		void* transformClass = StageLoader::transformClass;
		if (!managerClass || !stackClass || !componentClass || !typeClass || !maskClass || !transformClass) return false;
		const auto instance = m.Find(managerClass, "get_instance", true, {0x12, managerClass}, {});
		const auto create = m.Find(managerClass, "CreateStack", false, {0x12, stackClass}, {});
		const auto dispose = m.Find(managerClass, "DestroyStack", false, {0x01}, {{0x12, stackClass}});
		const auto update = m.Find(managerClass, "Update", false, {0x01},
			{{0x12, stackClass}, {0x12, transformClass}, {0x11, maskClass}});
		const auto getActor = m.Find(stackClass, "GetComponent", false, {0x12, componentClass}, {{0x12, typeClass}});
		const auto getTrigger = m.Find(cameraDataClass, "get_volumeTrigger", false, {0x12, transformClass}, {});
		if (!instance || !create || !dispose || !update || !getActor || !getTrigger) return false;
		Root camera, go, cameraType, data, trigger, manager, stack, actorType, actor;
		if (!camera.Keep(StageLoader::Invoke(StageLoader::cameraMain)) ||
			!go.Keep(StageLoader::Invoke(StageLoader::componentGo, camera.Get())) ||
			!cameraType.Keep(ManagedType(cameraDataClass))) return false;
		void* cameraArgs[]{cameraType.Get()};
		if (!data.Keep(StageLoader::Invoke(getComponent, go.Get(), cameraArgs))) return false;
		void* boxed = StageLoader::Invoke(getVolumeMask, data.Get());
		const auto* maskBits = boxed ? static_cast<const uint32_t*>(g_safeBones.unbox(boxed)) : nullptr;
		if (!maskBits || !*maskBits) return false;
		uint32_t mask = *maskBits;
		void* customTrigger = nullptr;
		if (!Call(getTrigger, data.Get(), nullptr, &customTrigger) ||
			!trigger.Keep(customTrigger ? customTrigger : StageLoader::Invoke(StageLoader::goTransform, go.Get())) ||
			!manager.Keep(StageLoader::Invoke(instance)) || !actorType.Keep(ManagedType(actorClass))) return false;
		// Use an independent stack in this tick and exclude only our owned Volume.
		void* own = volumeObject.Get();
		bool disabled = false, enabled = true;
		void* disableArgs[]{&disabled};
		void* enableArgs[]{&enabled};
		if (own && !Call(StageLoader::setActive, own, disableArgs)) return false;
		const bool created = stack.Keep(StageLoader::Invoke(create, manager.Get()));
		void* updateArgs[]{stack.Get(), trigger.Get(), &mask};
		void* actorArgs[]{actorType.Get()};
		const bool read = created && Call(update, manager.Get(), updateArgs) &&
			actor.Keep(StageLoader::Invoke(getActor, stack.Get(), actorArgs)) &&
			ReadParameter(actor.Get(), "lightColor", {0x11, colorClass}, sample.key) &&
			ReadParameter(actor.Get(), "shadeColorMultiply", {0x11, colorClass}, sample.shade) &&
			ReadParameter(actor.Get(), "shadeColorAdditive", {0x11, colorClass}, sample.fill) &&
			ReadParameter(actor.Get(), "rimColor", {0x11, colorClass}, sample.rim) &&
			ReadParameter(actor.Get(), "mainLightAngle", {0x11, anglesClass}, sample.keyAngle) &&
			ReadParameter(actor.Get(), "rimAngle", {0x11, anglesClass}, sample.rimAngle) &&
			ReadParameter(actor.Get(), "rimPower", {0x0c}, sample.rimPower);
		void* disposeArgs[]{stack.Get()};
		const bool disposed = !created || Call(dispose, manager.Get(), disposeArgs);
		const bool restored = !own || Call(StageLoader::setActive, own, enableArgs);
		if (!restored) Release();
		const auto finiteColor = [](Color c) {
			return std::isfinite(c.r) && std::isfinite(c.g) && std::isfinite(c.b) && std::isfinite(c.a);
		};
		return read && disposed && restored && finiteColor(sample.key) && finiteColor(sample.shade) &&
			finiteColor(sample.fill) && finiteColor(sample.rim) && std::isfinite(sample.keyAngle.x) &&
			std::isfinite(sample.keyAngle.y) && std::isfinite(sample.rimAngle.x) &&
			std::isfinite(sample.rimAngle.y) && std::isfinite(sample.rimPower);
	}
	inline bool ApplyStrengths(const GakuMotion::Core& core)
	{
		const auto& strengths = core.actorLightingStrengths;
		const auto& style = core.actorLightingStyle;
		const bool sampled = core.actorLightingSampled;
		const auto tint = [](GakuMotion::Vector3 color, float strength, float alpha) {
			return Color{color.x * strength, color.y * strength, color.z * strength, alpha};
		};
		const Color key = tint(style.colors[0], strengths.key, sampled ? sampledLight.key.a : 1.f);
		const Color shade = sampled ? sampledLight.shade : Color{strengths.key, strengths.key, strengths.key, 1.f};
		const Color fill = tint(style.colors[1], strengths.fill, sampled ? sampledLight.fill.a : 1.f);
		const Color rim = tint(style.colors[2], strengths.rim, sampled ? sampledLight.rim.a : 1.f);
		const Angles keyAngle = sampled ? sampledLight.keyAngle : Angles{-15.f, 25.f};
		const Angles rimAngle = sampled ? sampledLight.rimAngle : Angles{35.f, 15.f};
		const float rimPower = sampled ? sampledLight.rimPower : 8.f;
		if (!Parameter("lightColor", {0x11, colorClass}, &key) ||
			!Parameter("shadeColorMultiply", {0x11, colorClass}, &shade) ||
			!Parameter("shadeColorAdditive", {0x11, colorClass}, &fill) ||
			!Parameter("rimColor", {0x11, colorClass}, &rim) ||
			!Parameter("mainLightAngle", {0x11, anglesClass}, &keyAngle) ||
			!Parameter("rimAngle", {0x11, anglesClass}, &rimAngle) ||
			!Parameter("rimPower", {0x0c}, &rimPower)) return false;
		appliedStrengths = strengths;
		appliedStyleRevision = core.actorLightingStyleRevision;
		appliedSampled = sampled;
		return true;
	}
	inline bool Create(int targetLayer, const GakuMotion::Core& core)
	{
		Root name, profileType, parameterType, volumeType;
		if (!name.Keep(StageLoader::stringNew("GakuMotion Fixed Actor Lighting")) ||
			!profileType.Keep(ManagedType(profileClass)) || !parameterType.Keep(ManagedType(actorClass)) ||
			!volumeType.Keep(ManagedType(volumeClass))) return false;
		void* args[]{name.Get()};
		if (!volumeObject.Keep(objectNew(StageLoader::goClass)) || !Call(ctor, volumeObject.Get(), args)) return false;
		bool active = false;
		void* activeArgs[]{&active};
		void* layerArgs[]{&targetLayer};
		if (!Call(StageLoader::setActive, volumeObject.Get(), activeArgs) || !Call(setLayer, volumeObject.Get(), layerArgs)) return false;
		void* profileArgs[]{profileType.Get()};
		void* profile = nullptr;
		if (!Call(createProfile, nullptr, profileArgs, &profile) || !profileObject.Keep(profile)) return false;
		bool overrideAll = false;
		void* addArgs[]{parameterType.Get(), &overrideAll};
		void* parameter = nullptr;
		if (!Call(addParameter, profileObject.Get(), addArgs, &parameter) || !parameterObject.Keep(parameter)) return false;
		const float noStageAdditive = 0.f;
		if (!ApplyStrengths(core) ||
			!Parameter("additiveLightScale", {0x0c}, &noStageAdditive)) return false;
		void* volumeArgs[]{volumeType.Get()};
		if (!volumeComponent.Keep(StageLoader::Invoke(addComponent, volumeObject.Get(), volumeArgs))) return false;
		bool global = true;
		float weight = core.actorLightingStyle.weight, priority = 10000.f;
		void* globalArgs[]{&global};
		void* sharedArgs[]{profileObject.Get()};
		if (!Call(setGlobal, volumeComponent.Get(), globalArgs) || !Call(setProfile, volumeComponent.Get(), sharedArgs) ||
			!Write(volumeComponent.Get(), "weight", {0x0c}, &weight) || !Write(volumeComponent.Get(), "priority", {0x0c}, &priority)) return false;
		active = true;
		return Call(StageLoader::setActive, volumeObject.Get(), activeArgs);
	}
	inline void Tick(GakuMotion::Core& core)
	{
		if (!ProtectionOnUnityThread()) return;
		const bool eligible = core.liveTakeoverActive && !core.liveTakeoverFault && core.liveOwnerToken &&
			core.InjectionAllowed() && core.injectionPolicy.Read().captureDepth == 0 &&
			!GakuMotion::visual_handoff::Requested();
		if (!eligible || (owner && (owner != core.liveOwnerToken || scene != core.liveOwnerScene)))
			core.fixedActorLighting = false;
		if (core.actorLightingSampled && (sampledOwner != core.liveOwnerToken || sampledScene != core.liveOwnerScene)) {
			core.actorLightingSampled = false;
			core.actorLightingStrengths = {};
			core.actorLightingStyle = {};
			++core.actorLightingStyleRevision;
			core.actorLightingLimits = {1.f, .25f, .25f};
		}
		if (const auto requestedActor = core.actorLightingSampleActor) {
			core.actorLightingSampleActor = 0;
			const auto* selected = core.sceneActorRegistry.Find(core.player.controller.targetActor);
			Sample next{};
			if (eligible && core.liveOwnerToken == core.actorLightingSampleOwner && selected &&
				selected->token == requestedActor && ReadNativeSample(next)) {
				sampledLight = next;
				sampledOwner = core.liveOwnerToken; sampledScene = core.liveOwnerScene;
				core.actorLightingStrengths = {Strength(next.key), Strength(next.fill), Strength(next.rim)};
				const auto rgb = [](Color color, float strength) {
					if (!strength) return GakuMotion::Vector3{1,1,1};
					return GakuMotion::Vector3{color.r / strength, color.g / strength, color.b / strength};
				};
				auto& style = core.actorLightingStyle;
				style.colors[0] = rgb(next.key, core.actorLightingStrengths.key);
				style.colors[1] = rgb(next.fill, core.actorLightingStrengths.fill);
				style.colors[2] = rgb(next.rim, core.actorLightingStrengths.rim);
				style.weight = 1.f;
				++core.actorLightingStyleRevision;
				const auto upper = [](float standard, float value) {
					return (std::max)(standard, std::ceil(value * 1000.f) / 1000.f);
				};
				const auto& value = core.actorLightingStrengths;
				core.actorLightingLimits = {upper(1.f, value.key), upper(.25f, value.fill), upper(.25f, value.rim)};
				core.actorLightingSampled = true;
				appliedSampled = false;
				core.fixedActorLighting = true;
				core.fixedActorLightingNotice = "已读取当前 Live 的角色光照";
			} else core.fixedActorLightingNotice = "读取原生角色光照失败";
		}
		if (!core.fixedActorLighting) {
			if (!Release()) core.fixedActorLightingNotice = "角色光照覆盖清理失败，正在重试";
			return;
		}
		if (volumeObject.handle && (core.actorLightingStrengths != appliedStrengths || core.actorLightingSampled != appliedSampled ||
			core.actorLightingStyleRevision != appliedStyleRevision) &&
			(!ApplyStrengths(core) || !Write(volumeComponent.Get(), "weight", {0x0c}, &core.actorLightingStyle.weight))) {
			core.fixedActorLighting = false;
			core.fixedActorLightingNotice = Release() ? "无法更新角色光照，已恢复原打光" : "角色光照覆盖清理失败，正在重试";
			return;
		}
		const auto now = std::chrono::steady_clock::now();
		if (volumeObject.handle && now < nextCameraCheck) return;
		const int targetLayer = Resolve() ? CameraLayer() : -1;
		if (volumeObject.handle && owner == core.liveOwnerToken && layer == targetLayer) {
			nextCameraCheck = now + std::chrono::milliseconds(500);
			return;
		}
		if (!Release() || targetLayer < 0 || !Create(targetLayer, core)) {
			core.fixedActorLighting = false;
			core.fixedActorLightingNotice = Release() ? "当前 Live 的角色光照接口不可用" : "角色光照覆盖清理失败，正在重试";
			return;
		}
		owner = core.liveOwnerToken; scene = core.liveOwnerScene; layer = targetLayer;
		nextCameraCheck = now + std::chrono::milliseconds(500);
	}
}
