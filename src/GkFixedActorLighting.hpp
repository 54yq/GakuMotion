#pragma once

// Included in GakuMotionPlugin.cpp after the scene-owner and stage adapters.
namespace FixedActorLighting
{
	using Type = GakuMotion::ExactMethodMetadata::Type;
	struct Color { float r, g, b, a; };
	struct Angles { float x, y; };
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
	inline Root volumeObject, profileObject, parameterObject;
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
	inline bool Parameter(const char* name, Type type, const void* value)
	{
		void* actor = parameterObject.Get();
		void* field = g_faceRuntime.InstanceField(actor, name, 0x12);
		if (!field) field = g_faceRuntime.InstanceField(actor, name, 0x15);
		if (!field) return false;
		void* parameter = nullptr;
		g_faceRuntime.readField(actor, field, &parameter);
		Root root;
		const bool overrideState = true;
		return root.Keep(parameter) && Write(root.Get(), "m_Value", type, value) &&
			Write(root.Get(), "m_OverrideState", {0x02}, &overrideState);
	}
	inline bool ApplyStrengths(const GakuMotion::Core::ActorLightingStrengths& strengths)
	{
		const auto gray = [](float value) { return Color{value, value, value, 1.f}; };
		const Color key = gray(strengths.key), fill = gray(strengths.fill), rim = gray(strengths.rim);
		if (!Parameter("lightColor", {0x11, colorClass}, &key) ||
			!Parameter("shadeColorMultiply", {0x11, colorClass}, &key) ||
			!Parameter("shadeColorAdditive", {0x11, colorClass}, &fill) ||
			!Parameter("rimColor", {0x11, colorClass}, &rim)) return false;
		appliedStrengths = strengths;
		return true;
	}
	inline bool Create(int targetLayer, const GakuMotion::Core::ActorLightingStrengths& strengths)
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
		const Angles keyAngle{-15.f, 25.f}, rimAngle{35.f, 15.f};
		const float noStageAdditive = 0.f, rimPower = 8.f;
		if (!ApplyStrengths(strengths) ||
			!Parameter("mainLightAngle", {0x11, anglesClass}, &keyAngle) ||
			!Parameter("rimAngle", {0x11, anglesClass}, &rimAngle) ||
			!Parameter("rimPower", {0x0c}, &rimPower) ||
			!Parameter("additiveLightScale", {0x0c}, &noStageAdditive)) return false;
		void* volumeArgs[]{volumeType.Get()};
		Root volume;
		if (!volume.Keep(StageLoader::Invoke(addComponent, volumeObject.Get(), volumeArgs))) return false;
		bool global = true;
		float weight = 1.f, priority = 10000.f;
		void* globalArgs[]{&global};
		void* sharedArgs[]{profileObject.Get()};
		if (!Call(setGlobal, volume.Get(), globalArgs) || !Call(setProfile, volume.Get(), sharedArgs) ||
			!Write(volume.Get(), "weight", {0x0c}, &weight) || !Write(volume.Get(), "priority", {0x0c}, &priority)) return false;
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
		if (!core.fixedActorLighting) {
			if (!Release()) core.fixedActorLightingNotice = "角色光照覆盖清理失败，正在重试";
			return;
		}
		if (volumeObject.handle && core.actorLightingStrengths != appliedStrengths &&
			!ApplyStrengths(core.actorLightingStrengths)) {
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
		if (!Release() || targetLayer < 0 || !Create(targetLayer, core.actorLightingStrengths)) {
			core.fixedActorLighting = false;
			core.fixedActorLightingNotice = Release() ? "当前 Live 的角色光照接口不可用" : "角色光照覆盖清理失败，正在重试";
			return;
		}
		owner = core.liveOwnerToken; scene = core.liveOwnerScene; layer = targetLayer;
		nextCameraCheck = now + std::chrono::milliseconds(500);
		core.fixedActorLightingNotice.clear();
	}
}
