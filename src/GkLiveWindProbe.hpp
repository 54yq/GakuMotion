#pragma once
// Unity-thread, read-only samples. Never changes force, wind or solver state.
namespace LiveWindProbe
{
    inline void Sample(void* actor, int scene, const GakuMotion::Core& core)
    {
        auto unavailable = [](const char* reason) {
            static std::string last;
            if (last != reason && g_hostContext.logInfo) { last = reason; g_hostContext.logInfo("[GakuMotion][LiveWindProbe] unavailable=%s read_only=1", reason); }
        };
        if (!actor || !scene || !g_gameAssembly || !g_hostContext.logInfo || !g_getComponentInChildrenNative ||
            !il2cpp_class_get_type || !il2cpp_type_get_object || !g_faceRuntime.findField ||
            !g_faceRuntime.fieldType || !g_faceRuntime.typeCode || !g_faceRuntime.fieldFlags ||
            !g_faceRuntime.readField || !g_faceRuntime.parentClass || !g_componentKlass) return;
        auto api = GroundShadowProbeBootstrap::Resolve(g_gameAssembly);
        using Size = int (*)(void*, uint32_t*);
        auto size = reinterpret_cast<Size>(GetProcAddress(g_gameAssembly, "il2cpp_class_value_size"));
        void* managerClass = FindClass("ActorAnimation.Runtime.dll", "ActorAnimation", "ActorAnimationManageData");
        void* dataClass = FindClass("ActorAnimation.Runtime.dll", "ActorAnimation", "ActorAnimationManagePropertyData");
        if (!size || !api.fieldOffset || !api.method.classFromType || !managerClass || !dataClass) { unavailable("metadata"); return; }
        bool component = false;
        for (void* klass = managerClass; klass; klass = g_faceRuntime.parentClass(klass))
            if (klass == g_componentKlass) { component = true; break; }
        if (!component) {
            static bool logged{}; if (!logged) { logged = true; g_hostContext.logInfo("[GakuMotion][LiveWindProbe] manager_not_component read_only=1"); }
            return;
        }
        void* property = g_faceRuntime.findField(managerClass, "propertyData");
        void* propertyType = property ? g_faceRuntime.fieldType(property) : nullptr;
        uint32_t align{}; int bytes = size(dataClass, &align);
        if (!propertyType || g_faceRuntime.typeCode(propertyType) != 0x11 ||
            (g_faceRuntime.fieldFlags(property) & 0x10) || api.method.classFromType(propertyType) != dataClass ||
            bytes <= 0 || bytes > 1024) { unavailable("property_type"); return; }
        auto offset = [&](const char* name, int type, size_t length) -> int {
            void* field = g_faceRuntime.findField(dataClass, name);
            void* ft = field ? g_faceRuntime.fieldType(field) : nullptr;
            if (!ft || g_faceRuntime.typeCode(ft) != type || (g_faceRuntime.fieldFlags(field) & 0x10)) return -1;
            if (type == 0x11) {
                void* vectorClass = api.method.classFromType(ft);
                uint32_t vectorAlign{};
                if (!vectorClass || size(vectorClass, &vectorAlign) != 12) return -1;
            }
            int value = static_cast<int>(api.fieldOffset(field)) - 16;
            return value >= 0 && static_cast<size_t>(value) + length <= static_cast<size_t>(bytes) ? value : -1;
        };
        const int force = offset("globalForce", 0x11, 12), direction = offset("windDirection", 0x11, 12);
        const int power = offset("windPower", 0x0c, 4), fluctuation = offset("windFluctuation", 0x0c, 4);
        if (force < 0 || direction < 0 || power < 0 || fluctuation < 0) { unavailable("field_layout"); return; }
        void* type = il2cpp_class_get_type(managerClass);
        void* typeObject = type ? il2cpp_type_get_object(type) : nullptr;
        void* manager = typeObject ? g_getComponentInChildrenNative(actor, typeObject) : nullptr;
        auto report = [&](void* object, void* owner, int objectScene) {
            if (!object || !CameraBridge::IsAlive(object)) return;
            alignas(16) std::array<unsigned char, 1024> buffer{};
            g_faceRuntime.readField(object, property, buffer.data());
            float f[3]{}, d[3]{}, p{}, wave{};
            std::memcpy(f, buffer.data()+force, sizeof(f)); std::memcpy(d, buffer.data()+direction, sizeof(d));
            std::memcpy(&p, buffer.data()+power, sizeof(p)); std::memcpy(&wave, buffer.data()+fluctuation, sizeof(wave));
            for (float value : f) if (!std::isfinite(value)) return;
            for (float value : d) if (!std::isfinite(value)) return;
            if (!std::isfinite(p) || !std::isfinite(wave)) return;
            g_hostContext.logInfo("[GakuMotion][LiveWindProbe] actor=%p scene=%d manager_scene=%d manager=%p paused=%d takeover=%d motion=%d force=%.4f,%.4f,%.4f wind_dir=%.4f,%.4f,%.4f wind_power=%.4f wind_fluctuation=%.4f read_only=1",
                owner, scene, objectScene, object, int(core.liveObservation.paused), int(core.liveTakeoverActive), int(core.player.state),
                f[0],f[1],f[2],d[0],d[1],d[2],p,wave);
        };
        if (manager && CameraBridge::IsAlive(manager)) { report(manager, actor, scene); return; }
        static auto nextGlobal = std::chrono::steady_clock::time_point{};
        auto now = std::chrono::steady_clock::now();
        if (now < nextGlobal) return;
        nextGlobal = now + std::chrono::seconds(5);
        if (!typeObject || !StageLoader::findAll || !StageLoader::arrayLength || !StageLoader::newHandle ||
            !StageLoader::freeHandle || !StageLoader::componentGo || !StageLoader::goScene) { unavailable("global_finder"); return; }
        bool includeInactive = false; void* args[]{typeObject, &includeInactive};
        void* array = StageLoader::Invoke(StageLoader::findAll, nullptr, args);
        auto pin = array ? StageLoader::newHandle(array, false) : 0;
        if (!pin) { unavailable("global_array"); return; }
        struct Release { StageLoader::Handle pin; ~Release() { StageLoader::freeHandle(pin); } } release{pin};
        const uintptr_t count = StageLoader::arrayLength(array);
        if (count > 64) { unavailable("global_count"); return; }
        g_hostContext.logInfo("[GakuMotion][LiveWindProbe] actor_manager_missing=1 global_count=%u read_only=1", static_cast<unsigned>(count));
        auto items = reinterpret_cast<void**>(static_cast<char*>(array) + 32);
        for (uintptr_t i = 0; i < count; ++i) {
            void* object = items[i];
            void* go = object ? StageLoader::Invoke(StageLoader::componentGo, object) : nullptr;
            if (go) report(object, nullptr, StageLoader::SceneOfFreshGo(go));
        }
    }
}
