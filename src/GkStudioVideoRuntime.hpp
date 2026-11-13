#pragma once

// Included inside the plugin's private namespace, after StudioRuntime. Only
// value settings/owner tokens and strong GC handles survive a Unity-thread call.
namespace StudioVideo
{
    using Settings = GakuMotion::studio::Video;
    using Context = StudioRuntime::Context;
    enum class Phase { Disabled, Preparing, WaitingFrame, Playing, Paused, Faulted, Retiring };
    struct Frame
    {
        bool prepared{}, texture{};
        int64_t frame{-1};
        uint32_t width{}, height{};
    };
    struct LayerChoice
    {
        int layer{-1};
        const char* reason{"no_environment_mesh"};
    };
    // A layer is selected only from observed, active environment geometry and
    // only when exactly one observed layer is visible to the sampled camera.
    // No semantic layer name or fallback bit is assumed.
    inline LayerChoice SelectVisibleLayer(const std::vector<int>& observed, uint32_t cameraMask)
    {
        if (observed.empty()) return {};
        std::array<bool, 32> seen{};
        int selected = -1;
        for (int layer : observed)
        {
            if (layer < 0 || layer > 31) return {-1, "invalid_environment_layer"};
            if (seen[layer] || !(cameraMask & (uint32_t{1} << layer))) continue;
            seen[layer] = true;
            if (selected >= 0) return {-1, "multiple_camera_visible_environment_layers"};
            selected = layer;
        }
        return selected < 0 ? LayerChoice{-1, "camera_mask_excludes_environment_layers"} :
            LayerChoice{selected, "unique_camera_visible_environment_layer"};
    }
    struct Backend
    {
        void* state{};
        bool (*create)(void*, const Settings&, const Context&, std::string&){};
        bool (*prepare)(void*, std::string&){};
        bool (*poll)(void*, Frame&, std::string&){};
        bool (*pause)(void*, bool, std::string&){};
        bool (*update)(void*, const Settings&, const Frame&, std::string&){};
        bool (*destroy)(void*, std::string&){};
        bool (*hasObjects)(void*){};
        double (*now)(void*){};
        bool Complete() const
        { return create && prepare && poll && pause && update && destroy && hasObjects && now; }
    };

    class Runtime
    {
        Phase phase_{Phase::Disabled};
        Context owner_{}, failedOwner_{}, observedOwner_{};
        Settings applied_{}, failedSettings_{};
        bool haveOwner_{}, faultLatched_{}, playStateApplied_{};
        bool appliedPaused_{};
        double started_{}, preparedAt_{};
        std::string fault_;
        Frame frame_{};

        bool Clean(const Backend& api, std::string& notice)
        {
            if (api.hasObjects(api.state) && !api.destroy(api.state, notice))
            { phase_ = Phase::Retiring; return false; }
            haveOwner_ = false;
            playStateApplied_ = false;
            frame_ = {};
            phase_ = faultLatched_ ? Phase::Faulted : Phase::Disabled;
            return true;
        }
        void Fault(const Backend& api, const Settings& settings, std::string message, std::string& notice)
        {
            faultLatched_ = true;
            failedSettings_ = settings;
            failedOwner_ = observedOwner_;
            fault_ = message.empty() ? "视频操作失败" : std::move(message);
            notice = fault_;
            Clean(api, notice);
            if (phase_ != Phase::Retiring) notice = fault_;
        }
        static bool Valid(const Settings& s)
        {
            return !s.path.empty() && s.path.size() <= 32767 && s.path.find('\0') == std::string::npos &&
                s.path.find("://") == std::string::npos && !s.path.starts_with("\\\\") && !s.path.starts_with("//") &&
                std::isfinite(s.distance) && s.distance >= .25f && s.distance <= 100 &&
                std::isfinite(s.width) && s.width >= .1f && s.width <= 100 &&
                std::isfinite(s.offsetX) && std::fabs(s.offsetX) <= 50 &&
                std::isfinite(s.offsetY) && std::fabs(s.offsetY) <= 50;
        }
    public:
        Phase GetPhase() const { return phase_; }
        bool DecodedFrameObserved() const
        { return frame_.prepared && frame_.texture && frame_.frame >= 0; }
        bool Revoke(const Backend& api, std::string& notice)
        {
            if (!api.Complete()) return true;
            faultLatched_ = false;
            fault_.clear();
            if (!Clean(api, notice)) return false;
            notice = "视频已停用；独立幕布已释放";
            return true;
        }
        void Tick(const Backend& api, const Settings& settings, const Context& context, std::string& notice)
        {
            observedOwner_ = context;
            if (!api.Complete()) { notice = "视频运行时不可用"; return; }
            if (!settings.enabled || !context.allowed)
            {
                if (Revoke(api, notice) && settings.enabled) notice = "视频设置已记忆；进入偶像摄影后生效";
                return;
            }
            if (!Valid(settings)) { Fault(api, settings, "视频设置无效，请选择本地文件并检查幕布参数", notice); return; }
            const bool ownerChanged = haveOwner_ && !owner_.SameOwner(context);
            const bool sourceChanged = haveOwner_ && applied_.path != settings.path;
            if (ownerChanged || sourceChanged)
            {
                faultLatched_ = false;
                if (!Clean(api, notice)) return;
            }
            if (phase_ == Phase::Retiring && !Clean(api, notice)) return;
            if (faultLatched_)
            {
                if (settings == failedSettings_ && failedOwner_.SameOwner(context))
                { notice = fault_; return; }
                faultLatched_ = false;
                phase_ = Phase::Disabled;
            }
            if (!haveOwner_)
            {
                owner_ = context;
                applied_ = settings;
                haveOwner_ = true;
                frame_ = {};
                playStateApplied_ = false;
                std::string error;
                if (!api.create(api.state, settings, context, error) || !api.prepare(api.state, error))
                { Fault(api, settings, error, notice); return; }
                started_ = api.now(api.state);
                preparedAt_ = 0;
                phase_ = Phase::Preparing;
            }
            std::string error;
            Frame current;
            if (!api.poll(api.state, current, error)) { Fault(api, settings, error, notice); return; }
            frame_ = current;
            const double now = api.now(api.state);
            if (!std::isfinite(now) || !std::isfinite(started_) || now < started_)
            { Fault(api, settings, "视频时钟无效，已停止本次准备", notice); return; }
            if (!current.prepared)
            {
                if (now - started_ >= 20) { Fault(api, settings, "视频准备超时；请检查文件编码或重新启用", notice); return; }
                phase_ = Phase::Preparing;
                notice = "正在准备本地视频（独立幕布）";
            }
            else
            {
                // Prepare alone can reserve a texture without decoding a frame.
                // Even a session requested paused must briefly Play until the
                // first decoded frame is observable, then apply Pause once.
                const bool frameReady = current.texture && current.frame >= 0;
                const bool desiredPaused = settings.paused && frameReady;
                if (!playStateApplied_ || appliedPaused_ != desiredPaused)
                {
                    if (!api.pause(api.state, desiredPaused, error)) { Fault(api, settings, error, notice); return; }
                    appliedPaused_ = desiredPaused;
                    playStateApplied_ = true;
                }
                if (phase_ == Phase::Preparing) preparedAt_ = now;
                if (frameReady)
                {
                    phase_ = settings.paused ? Phase::Paused : Phase::Playing;
                    notice = settings.paused ? "视频已暂停；已取得解码帧，幕布画面待视觉确认" :
                        "视频播放中；已取得解码帧，幕布画面待视觉确认";
                }
                else
                {
                    if (now - preparedAt_ >= 10) { Fault(api, settings, "视频已准备但解码帧超时，请重新启用", notice); return; }
                    phase_ = Phase::WaitingFrame;
                    notice = "视频已准备，等待解码帧（独立幕布）";
                }
            }
            if (!api.update(api.state, settings, current, error)) { Fault(api, settings, error, notice); return; }
            applied_ = settings;
        }
    };

#ifndef GK_STUDIO_VIDEO_FAKE_RUNTIME
    namespace Native
    {
        using Root = StudioRuntime::Root;
        using Scope = StudioRuntime::ScopedRoots;
        using Type = GakuMotion::ExactMethodMetadata::Type;
        inline Root root, quad, material, player, renderer, collider, shader;
        inline void *videoClass{}, *materialClass{}, *rendererClass{}, *meshRendererClass{}, *shaderClass{}, *textureClass{}, *colliderClass{};
        inline void *typeClass{}, *primitiveClass{}, *sourceClass{}, *audioClass{}, *renderModeClass{}, *vectorClass{}, *rotationClass{};
        inline void* (*objectNew)(void*){};
        inline void (*staticFieldGet)(void*, void*){};
        inline bool (*isEnum)(void*){};
        inline const void *createPrimitive{}, *addComponent{}, *getComponent{}, *shaderFind{}, *shaderSupported{};
        inline const void *materialCtor{}, *materialHasProperty{}, *materialSetTexture{}, *rendererSetMaterial{};
        inline const void *setParent{}, *setLocalPosition{}, *setLocalRotation{}, *setLocalScale{};
        inline const void *getPosition{}, *getRotation{}, *setPosition{}, *setRotation{}, *cameraMain{};
        inline const void *getLayer{}, *setLayer{}, *getCameraMask{}, *getRendererEnabled{};
        inline const void *setUrl{}, *setSource{}, *setAudio{}, *setRenderMode{}, *setAwake{}, *setLoop{};
        inline const void *prepare{}, *getPrepared{}, *getTexture{}, *getFrame{}, *getWidth{}, *getHeight{}, *play{}, *pause{}, *stop{}, *destroy{};
        inline int quadValue{}, urlValue{}, noAudioValue{}, apiOnlyValue{};
        inline UnityVector3 anchorPosition{};
        inline UnityQuaternion anchorRotation{};
        inline std::string textureProperty;
        inline bool loopValue{};
        inline uintptr_t assignedTexture{};
        inline Context nativeOwner;
        inline int selectedLayer{-1}, anchorCameraId{}, observedMeshCount{};
        inline uint32_t sampledCameraMask{};
        inline std::string shaderName, diagnostics, layerReason;
        inline double lastDiagnostics{-1};
        inline Frame decodedSample;

        inline const void* Find(void* klass, const char* name, bool stat, Type result, std::initializer_list<Type> args)
        {
            const auto needsClass = [](Type t) { return (t.code == 0x12 || t.code == 0x11 || t.code == 0x1d) && !t.klass; };
            if (!klass || needsClass(result)) return nullptr;
            for (Type arg : args) if (needsClass(arg)) return nullptr;
            return StageLoader::metadata.Find(klass, name, stat, result, args);
        }
        inline bool Invoke(const void* method, void* self, void** args, void*& result)
        {
            result = nullptr;
            if (!method || !g_faceRuntime.invoke || !ProtectionOnUnityThread()) return false;
            void* exception = nullptr;
            result = g_faceRuntime.invoke(method, self, args, &exception);
            return !exception;
        }
        inline bool Call(const void* method, void* self = nullptr, void** args = nullptr)
        { void* ignored = nullptr; return Invoke(method, self, args, ignored); }
        template<class T> inline bool Read(const void* method, void* self, T& value)
        {
            void* boxed = nullptr;
            if (!Invoke(method, self, nullptr, boxed) || !boxed || !g_safeBones.unbox) return false;
            void* raw = g_safeBones.unbox(boxed);
            if (!raw) return false;
            std::memcpy(&value, raw, sizeof(value));
            return true;
        }
        inline bool Enum(void* klass, const char* name, int& value)
        {
            uint32_t alignment = 0;
            if (!klass || !isEnum || !isEnum(klass) || !StageLoader::valueSize ||
                StageLoader::valueSize(klass, &alignment) != sizeof(int) || alignment != alignof(int) ||
                !g_faceRuntime.findField || !g_faceRuntime.fieldFlags || !g_faceRuntime.fieldType || !staticFieldGet) return false;
            void* field = g_faceRuntime.findField(klass, name);
            if (!field || (g_faceRuntime.fieldFlags(field) & 0x50) != 0x50 ||
                !StageLoader::metadata.Matches(g_faceRuntime.fieldType(field), {0x11, klass})) return false;
            staticFieldGet(field, &value);
            return true;
        }
        inline void* ManagedType(void* klass, Scope& roots)
        {
            void* raw = klass && il2cpp_class_get_type ? il2cpp_class_get_type(klass) : nullptr;
            void* type = raw && il2cpp_type_get_object ? il2cpp_type_get_object(raw) : nullptr;
            return type && roots.Keep(type) ? type : nullptr;
        }
        inline void* Component(void* go, void* klass, bool add, Scope& roots)
        {
            void* type = ManagedType(klass, roots);
            void* value = nullptr;
            void* args[]{type};
            return type && Invoke(add ? addComponent : getComponent, go, args, value) && value &&
                StageLoader::assignable && g_faceRuntime.objectClass &&
                StageLoader::assignable(klass, g_faceRuntime.objectClass(value)) && roots.Keep(value)
                ? value : nullptr;
        }
        inline bool Resolve(std::string& error)
        {
            if (!ProtectionOnUnityThread()) { error = "视频操作必须在 Unity 主线程执行"; return false; }
            const auto core = [](const char* name) { return FindClass("UnityEngine.CoreModule.dll", "UnityEngine", name); };
            const auto video = [](const char* name) { return FindClass("UnityEngine.VideoModule.dll", "UnityEngine.Video", name); };
            videoClass = video("VideoPlayer"); sourceClass = video("VideoSource"); audioClass = video("VideoAudioOutputMode");
            renderModeClass = video("VideoRenderMode"); primitiveClass = core("PrimitiveType");
            materialClass = core("Material"); rendererClass = core("Renderer"); meshRendererClass = core("MeshRenderer"); shaderClass = core("Shader");
            textureClass = core("Texture"); colliderClass = FindClass("UnityEngine.PhysicsModule.dll", "UnityEngine", "Collider");
            vectorClass = core("Vector3"); rotationClass = core("Quaternion"); typeClass = FindClass("mscorlib.dll", "System", "Type");
            objectNew = reinterpret_cast<decltype(objectNew)>(GetProcAddress(g_gameAssembly, "il2cpp_object_new"));
            staticFieldGet = reinterpret_cast<decltype(staticFieldGet)>(GetProcAddress(g_gameAssembly, "il2cpp_field_static_get_value"));
            isEnum = reinterpret_cast<decltype(isEnum)>(GetProcAddress(g_gameAssembly, "il2cpp_class_is_enum"));
            if (!objectNew || !colliderClass || !Enum(primitiveClass, "Quad", quadValue) || !Enum(sourceClass, "Url", urlValue) ||
                !Enum(audioClass, "None", noAudioValue) || !Enum(renderModeClass, "APIOnly", apiOnlyValue))
            { error = "视频类型或枚举契约不可用"; return false; }
            uint32_t alignment = 0;
            if (!vectorClass || !rotationClass || StageLoader::valueSize(vectorClass, &alignment) != sizeof(UnityVector3) ||
                StageLoader::valueSize(rotationClass, &alignment) != sizeof(UnityQuaternion))
            { error = "视频幕布 Transform 值类型不匹配"; return false; }
            void* go = StageLoader::goClass;
            void* component = StageLoader::componentClass;
            void* transform = StageLoader::transformClass;
            void* camera = core("Camera");
            createPrimitive = Find(go, "CreatePrimitive", true, {0x12, go}, {{0x11, primitiveClass}});
            addComponent = Find(go, "AddComponent", false, {0x12, component}, {{0x12, typeClass}});
            getComponent = Find(go, "GetComponent", false, {0x12, component}, {{0x12, typeClass}});
            shaderFind = Find(shaderClass, "Find", true, {0x12, shaderClass}, {{0x0e}});
            shaderSupported = Find(shaderClass, "get_isSupported", false, {0x02}, {});
            materialCtor = Find(materialClass, ".ctor", false, {0x01}, {{0x12, shaderClass}});
            materialHasProperty = Find(materialClass, "HasProperty", false, {0x02}, {{0x0e}});
            materialSetTexture = Find(materialClass, "SetTexture", false, {0x01}, {{0x0e}, {0x12, textureClass}});
            rendererSetMaterial = Find(rendererClass, "set_sharedMaterial", false, {0x01}, {{0x12, materialClass}});
            setParent = Find(transform, "SetParent", false, {0x01}, {{0x12, transform}, {0x02}});
            setLocalPosition = Find(transform, "set_localPosition", false, {0x01}, {{0x11, vectorClass}});
            setLocalRotation = Find(transform, "set_localRotation", false, {0x01}, {{0x11, rotationClass}});
            setLocalScale = Find(transform, "set_localScale", false, {0x01}, {{0x11, vectorClass}});
            getPosition = Find(transform, "get_position", false, {0x11, vectorClass}, {});
            getRotation = Find(transform, "get_rotation", false, {0x11, rotationClass}, {});
            setPosition = Find(transform, "set_position", false, {0x01}, {{0x11, vectorClass}});
            setRotation = Find(transform, "set_rotation", false, {0x01}, {{0x11, rotationClass}});
            cameraMain = Find(camera, "get_main", true, {0x12, camera}, {});
            getLayer = Find(go, "get_layer", false, {0x08}, {});
            setLayer = Find(go, "set_layer", false, {0x01}, {{0x08}});
            getCameraMask = Find(camera, "get_cullingMask", false, {0x08}, {});
            getRendererEnabled = Find(rendererClass, "get_enabled", false, {0x02}, {});
            setUrl = Find(videoClass, "set_url", false, {0x01}, {{0x0e}});
            setSource = Find(videoClass, "set_source", false, {0x01}, {{0x11, sourceClass}});
            setAudio = Find(videoClass, "set_audioOutputMode", false, {0x01}, {{0x11, audioClass}});
            setRenderMode = Find(videoClass, "set_renderMode", false, {0x01}, {{0x11, renderModeClass}});
            setAwake = Find(videoClass, "set_playOnAwake", false, {0x01}, {{0x02}});
            setLoop = Find(videoClass, "set_isLooping", false, {0x01}, {{0x02}});
            prepare = Find(videoClass, "Prepare", false, {0x01}, {});
            getPrepared = Find(videoClass, "get_isPrepared", false, {0x02}, {});
            getTexture = Find(videoClass, "get_texture", false, {0x12, textureClass}, {});
            getFrame = Find(videoClass, "get_frame", false, {0x0a}, {});
            getWidth = Find(videoClass, "get_width", false, {0x09}, {});
            getHeight = Find(videoClass, "get_height", false, {0x09}, {});
            play = Find(videoClass, "Play", false, {0x01}, {});
            pause = Find(videoClass, "Pause", false, {0x01}, {});
            stop = Find(videoClass, "Stop", false, {0x01}, {});
            destroy = Find(core("Object"), "Destroy", true, {0x01}, {{0x12, core("Object")}});
            for (const void* method : {createPrimitive, addComponent, getComponent, shaderFind, shaderSupported, materialCtor,
                materialHasProperty, materialSetTexture, rendererSetMaterial, setParent, setLocalPosition, setLocalRotation,
                setLocalScale, getPosition, getRotation, setPosition, setRotation, cameraMain, getLayer, setLayer, getCameraMask,
                getRendererEnabled, setUrl, setSource, setAudio,
                setRenderMode, setAwake, setLoop, prepare, getPrepared, getTexture, getFrame, getWidth, getHeight, play, pause, stop, destroy})
                if (!method) { error = "视频或幕布所需精确 API 不完整"; return false; }
            return true;
        }
        inline bool FreshOwner()
        {
            const auto current = StudioRuntime::Current();
            return current.allowed && nativeOwner.allowed && nativeOwner.SameOwner(current) && ProtectionOnUnityThread();
        }
        inline void RefreshDiagnostics(bool force = false)
        {
            if (!FreshOwner()) return;
            const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
            if (!force && lastDiagnostics >= 0 && now - lastDiagnostics < .5) return;
            lastDiagnostics = now;
            Scope roots;
            std::ostringstream out;
            out << std::fixed << std::setprecision(3);
            const auto pose = [&](const char* label, void* go) {
                void* transform = nullptr;
                UnityVector3 position{}; UnityQuaternion rotation{};
                if (!go || !Invoke(StageLoader::goTransform, go, nullptr, transform) || !roots.Keep(transform) ||
                    !Read(getPosition, transform, position) || !Read(getRotation, transform, rotation))
                { out << ' ' << label << "_pose=unavailable"; return; }
                out << ' ' << label << "_xyz=" << position.x << ',' << position.y << ',' << position.z
                    << ' ' << label << "_q=" << rotation.x << ',' << rotation.y << ',' << rotation.z << ',' << rotation.w;
            };
            void* camera = nullptr;
            if (Invoke(cameraMain, nullptr, nullptr, camera) && roots.Keep(camera))
            {
                const int id = StageLoader::ReadInt(StageLoader::instanceId, camera);
                int mask = 0; const bool readMask = Read(getCameraMask, camera, mask);
                out << "camera_id=" << id << " camera_mask=";
                if (readMask) out << "0x" << std::hex << static_cast<uint32_t>(mask) << std::dec;
                else out << "unavailable";
                void* go = nullptr;
                if (Invoke(StageLoader::componentGo, camera, nullptr, go) && roots.Keep(go)) pose("camera", go);
            }
            else out << "camera=unavailable";
            out << " anchor_camera_id=" << anchorCameraId << " anchor_xyz=" << anchorPosition.x << ',' << anchorPosition.y << ',' << anchorPosition.z
                << " anchor_q=" << anchorRotation.x << ',' << anchorRotation.y << ',' << anchorRotation.z << ',' << anchorRotation.w
                << " selected_layer=" << selectedLayer << " layer_reason=" << layerReason << " environment_meshes=" << observedMeshCount
                << " sampled_mask=0x" << std::hex << sampledCameraMask << std::dec << " environment_scene=" << nativeOwner.activeScene
                << " shader=" << shaderName << " texture_property=" << textureProperty
                << " root_id=" << root.instanceId << " quad_id=" << quad.instanceId
                << " video_frame=" << decodedSample.frame << " video_size=" << decodedSample.width << 'x' << decodedSample.height
                << " video_prepared=" << decodedSample.prepared << " video_texture=" << decodedSample.texture;
            pose("root", root.Get()); pose("quad", quad.Get());
            diagnostics = out.str();
        }
        inline bool SelectEnvironmentLayer(void* camera, std::string& error)
        {
            if (!FreshOwner() || !meshRendererClass || !StageLoader::findAll || !StageLoader::arrayLength)
            { error = "无法在当前摄影场景确认幕布可见层"; return false; }
            int cameraMask = 0;
            if (!Read(getCameraMask, camera, cameraMask)) { error = "无法读取摄影相机可见层"; return false; }
            sampledCameraMask = static_cast<uint32_t>(cameraMask);
            Scope roots;
            void* type = ManagedType(meshRendererClass, roots);
            bool inactive = false; void* array = nullptr;
            void* args[]{type, &inactive};
            if (!type || !Invoke(StageLoader::findAll, nullptr, args, array) || !roots.Keep(array))
            { error = "无法取得鲜活环境 MeshRenderer 样本"; return false; }
            const uintptr_t count = StageLoader::arrayLength(array);
            if (count > 8192) { error = "环境 MeshRenderer 样本超过单次有界采样上限"; return false; }
            // Same current IL2CPP array layout already used by StageLoader.
            void** items = reinterpret_cast<void**>(static_cast<char*>(array) + 32);
            std::vector<int> layers;
            observedMeshCount = 0;
            for (uintptr_t i = 0; i < count; ++i)
            {
                if (!items[i]) continue;
                void* go = nullptr;
                if (!Invoke(StageLoader::componentGo, items[i], nullptr, go) || !roots.Keep(go) ||
                    StageLoader::SceneOfFreshGo(go) != nativeOwner.activeScene) continue;
                bool enabled = false;
                if (!Read(getRendererEnabled, items[i], enabled)) { error = "无法验证环境 Renderer 启用状态"; return false; }
                if (!enabled) continue;
                int layer = -1;
                if (!Read(getLayer, go, layer)) { error = "无法读取环境几何层"; return false; }
                ++observedMeshCount;
                layers.push_back(layer);
            }
            const auto choice = SelectVisibleLayer(layers, sampledCameraMask);
            selectedLayer = choice.layer;
            layerReason = choice.reason;
            if (selectedLayer < 0)
            {
                RefreshDiagnostics(true);
                error = std::string("无法唯一确认相机可见的环境几何层：") + choice.reason;
                return false;
            }
            return true;
        }
        inline bool SetVisibleLayer(void* go, std::string& error)
        {
            int layer = selectedLayer; void* args[]{&layer}; int readback = -1;
            if (layer < 0 || !Call(setLayer, go, args) || !Read(getLayer, go, readback) || readback != layer)
            { error = "自有幕布可见层设置或读回失败"; return false; }
            return true;
        }
        inline void DestroyUnrooted(void* object)
        { if (object) { void* args[]{object}; Call(destroy, nullptr, args); } }
        inline bool HasObjects() { return root.Has() || quad.Has() || material.Has() || collider.Has(); }
        inline bool Destroy(std::string& error)
        {
            if (!ProtectionOnUnityThread()) { error = "等待 Unity 主线程清理视频"; return false; }
            if (void* vp = player.Get()) Call(stop, vp);
            bool complete = true;
            // Each is self-owned. A failed native Destroy preserves its strong
            // root for the next cleanup attempt, even after Photography exits.
            for (Root* value : {&collider, &quad, &root, &material})
                if (!StudioRuntime::DestroyOwned(*value)) complete = false;
            if (complete)
            {
                player.Reset(); renderer.Reset(); shader.Reset();
                textureProperty.clear(); assignedTexture = 0;
                shaderName.clear(); diagnostics.clear(); layerReason.clear(); nativeOwner = {};
                selectedLayer = -1; anchorCameraId = observedMeshCount = 0; lastDiagnostics = -1;
                decodedSample = {};
            }
            else error = "视频资源清理尚未完成，正在重试";
            return complete;
        }
        inline bool Geometry(const Settings& settings, const Frame& frame, std::string& error)
        {
            Scope roots;
            void* rootGo = root.Get(); void* quadGo = quad.Get();
            void *rootTransform = nullptr, *quadTransform = nullptr;
            if (!rootGo || !quadGo || !Invoke(StageLoader::goTransform, rootGo, nullptr, rootTransform) || !roots.Keep(rootTransform) ||
                !Invoke(StageLoader::goTransform, quadGo, nullptr, quadTransform) || !roots.Keep(quadTransform))
            { error = "视频幕布对象已丢失"; return false; }
            const auto& q = anchorRotation;
            const UnityVector3 right{1 - 2 * (q.y*q.y + q.z*q.z), 2 * (q.x*q.y + q.z*q.w), 2 * (q.x*q.z - q.y*q.w)};
            const UnityVector3 up{2 * (q.x*q.y - q.z*q.w), 1 - 2 * (q.x*q.x + q.z*q.z), 2 * (q.y*q.z + q.x*q.w)};
            const UnityVector3 forward{2 * (q.x*q.z + q.y*q.w), 2 * (q.y*q.z - q.x*q.w), 1 - 2 * (q.x*q.x + q.y*q.y)};
            UnityVector3 position{anchorPosition.x + right.x*settings.offsetX + up.x*settings.offsetY + forward.x*settings.distance,
                anchorPosition.y + right.y*settings.offsetX + up.y*settings.offsetY + forward.y*settings.distance,
                anchorPosition.z + right.z*settings.offsetX + up.z*settings.offsetY + forward.z*settings.distance};
            float aspect = frame.width && frame.height ? static_cast<float>(frame.width) / frame.height : 16.0f / 9;
            if (!std::isfinite(aspect) || aspect < .05f || aspect > 20) { error = "视频宽高比无效"; return false; }
            UnityVector3 scale{settings.width, settings.width / aspect, 1};
            void* positionArgs[]{&position}; void* rotationArgs[]{&anchorRotation}; void* scaleArgs[]{&scale};
            if (!Call(setPosition, rootTransform, positionArgs) || !Call(setRotation, rootTransform, rotationArgs) ||
                !Call(setLocalScale, quadTransform, scaleArgs))
            { error = "视频幕布定位失败"; return false; }
            if (loopValue != settings.loop)
            {
                bool loop = settings.loop; void* args[]{&loop};
                if (!Call(setLoop, player.Get(), args)) { error = "视频循环设置失败"; return false; }
                loopValue = loop;
            }
            RefreshDiagnostics();
            return true;
        }
        inline bool Create(const Settings& settings, const Context& context, std::string& error)
        {
            if (!context.allowed || HasObjects() || !Resolve(error)) return false;
            nativeOwner = context;
            if (!FreshOwner()) { error = "摄影场景所有权已变化"; return false; }
            std::error_code fileError;
            const auto path = GakuMotion::MotionPathFromUtf8(settings.path);
            if (!path.is_absolute() || !std::filesystem::is_regular_file(path, fileError) || fileError)
            { error = "本地视频文件不存在或无法读取"; return false; }
            Scope roots;
            void *camera = nullptr, *cameraTransform = nullptr;
            if (!Invoke(cameraMain, nullptr, nullptr, camera) || !roots.Keep(camera) ||
                !Invoke(StageLoader::componentGo, camera, nullptr, cameraTransform) || !roots.Keep(cameraTransform))
            { error = "摄影主相机尚未就绪"; return false; }
            void* cameraGo = cameraTransform;
            if (!Invoke(StageLoader::goTransform, cameraGo, nullptr, cameraTransform) || !roots.Keep(cameraTransform) ||
                !Read(getPosition, cameraTransform, anchorPosition) || !Read(getRotation, cameraTransform, anchorRotation))
            { error = "无法读取摄影相机位姿"; return false; }
            const float length = anchorRotation.x*anchorRotation.x + anchorRotation.y*anchorRotation.y +
                anchorRotation.z*anchorRotation.z + anchorRotation.w*anchorRotation.w;
            if (!std::isfinite(length) || length < 1e-8f || !std::isfinite(anchorPosition.x) ||
                !std::isfinite(anchorPosition.y) || !std::isfinite(anchorPosition.z))
            { error = "摄影相机位姿无效"; return false; }
            const float inverse = 1 / std::sqrt(length);
            anchorRotation.x*=inverse; anchorRotation.y*=inverse; anchorRotation.z*=inverse; anchorRotation.w*=inverse;
            anchorCameraId = StageLoader::ReadInt(StageLoader::instanceId, camera);
            if (!anchorCameraId || !SelectEnvironmentLayer(camera, error)) return false;
            if (!StudioRuntime::CreateOwnedRoot("GakuMotion.Studio.Video", context, root))
            { error = "无法创建摄影自有视频节点"; return false; }
            if (!SetVisibleLayer(root.Get(), error)) return false;
            void* primitive = nullptr; void* primitiveArgs[]{&quadValue};
            if (!Invoke(createPrimitive, nullptr, primitiveArgs, primitive) || !primitive)
            { error = "无法创建独立视频幕布"; return false; }
            if (!quad.Keep(primitive)) { DestroyUnrooted(primitive); error = "无法保持视频幕布引用"; return false; }
            quad.instanceId = StageLoader::ReadInt(StageLoader::instanceId, quad.Get());
            bool inactive = false; void* inactiveArgs[]{&inactive};
            if (!quad.instanceId || !Call(StageLoader::setActive, quad.Get(), inactiveArgs))
            { error = "无法安全停用新建视频幕布"; return false; }
            if (!SetVisibleLayer(quad.Get(), error)) return false;
            void *parentTransform = nullptr, *childTransform = nullptr;
            if (!Invoke(StageLoader::goTransform, root.Get(), nullptr, parentTransform) || !roots.Keep(parentTransform) ||
                !Invoke(StageLoader::goTransform, quad.Get(), nullptr, childTransform) || !roots.Keep(childTransform))
            { error = "幕布 Transform 不可用"; return false; }
            bool worldPositionStays = false;
            void* parentArgs[]{parentTransform, &worldPositionStays};
            UnityVector3 zero{}; UnityQuaternion identity{0,0,0,1};
            void* zeroArgs[]{&zero}; void* identityArgs[]{&identity};
            if (!Call(setParent, childTransform, parentArgs) || !Call(setLocalPosition, childTransform, zeroArgs) ||
                !Call(setLocalRotation, childTransform, identityArgs))
            { error = "无法绑定独立幕布层级"; return false; }
            if (StageLoader::SceneOfFreshGo(quad.Get()) != context.owner.sceneHandle)
            { error = "独立幕布未归属当前摄影场景"; return false; }
            if (void* value = Component(quad.Get(), colliderClass, false, roots))
            {
                if (!collider.Keep(value)) { error = "无法保持自有幕布碰撞体引用"; return false; }
                collider.instanceId = StageLoader::ReadInt(StageLoader::instanceId, value);
                if (!collider.instanceId || !StudioRuntime::DestroyOwned(collider))
                { error = "无法移除自有幕布碰撞体"; return false; }
            }
            else { error = "无法确认自有幕布碰撞体身份"; return false; }
            void* rendererObject = Component(quad.Get(), rendererClass, false, roots);
            void* playerObject = Component(root.Get(), videoClass, true, roots);
            if (!rendererObject || !renderer.Keep(rendererObject) || !playerObject || !player.Keep(playerObject))
            { error = "无法创建视频组件或幕布 Renderer"; return false; }
            for (const char* name : {"Universal Render Pipeline/Unlit", "Unlit/Texture", "UI/Default"})
            {
                void* text = StageLoader::stringNew(name); void* candidate = nullptr;
                if (!roots.Keep(text)) continue;
                void* args[]{text}; bool supported = false;
                if (Invoke(shaderFind, nullptr, args, candidate) && candidate && roots.Keep(candidate) &&
                    Read(shaderSupported, candidate, supported) && supported && shader.Keep(candidate))
                { shaderName = name; break; }
            }
            if (!shader.Get()) { error = "找不到支持视频纹理的 Unlit Shader"; return false; }
            void* mat = objectNew(materialClass);
            if (!mat || !material.Keep(mat)) { if (mat) DestroyUnrooted(mat); error = "无法保持自有视频材质"; return false; }
            void* shaderArgs[]{shader.Get()};
            if (!Call(materialCtor, material.Get(), shaderArgs)) { error = "创建自有视频材质失败"; return false; }
            material.instanceId = StageLoader::ReadInt(StageLoader::instanceId, material.Get());
            if (!material.instanceId) { error = "自有视频材质身份不可用"; return false; }
            for (const char* name : {"_BaseMap", "_MainTex"})
            {
                void* text = StageLoader::stringNew(name); void* boxed = nullptr;
                if (!roots.Keep(text)) continue;
                void* args[]{text};
                if (Invoke(materialHasProperty, material.Get(), args, boxed) && boxed && g_safeBones.unbox)
                    if (void* raw = g_safeBones.unbox(boxed); raw && *static_cast<bool*>(raw))
                    { textureProperty = name; break; }
            }
            if (textureProperty.empty()) { error = "自有幕布材质没有可写视频纹理属性"; return false; }
            void* matArgs[]{material.Get()};
            if (!Call(rendererSetMaterial, renderer.Get(), matArgs)) { error = "独立幕布材质绑定失败"; return false; }
            bool awake = false; bool loop = settings.loop;
            void* awakeArgs[]{&awake}; void* loopArgs[]{&loop}; void* sourceArgs[]{&urlValue};
            void* audioArgs[]{&noAudioValue}; void* renderArgs[]{&apiOnlyValue};
            void* url = StageLoader::stringNew(settings.path.c_str());
            if (!roots.Keep(url)) { error = "无法创建本地视频路径"; return false; }
            void* urlArgs[]{url};
            if (!Call(setAwake, player.Get(), awakeArgs) || !Call(setAudio, player.Get(), audioArgs) ||
                !Call(setRenderMode, player.Get(), renderArgs) || !Call(setUrl, player.Get(), urlArgs) ||
                !Call(setSource, player.Get(), sourceArgs) || !Call(setLoop, player.Get(), loopArgs))
            { error = "本地视频组件配置失败"; return false; }
            loopValue = loop; assignedTexture = 0;
            if (!Geometry(settings, {}, error)) return false;
            bool active = true; void* activeArgs[]{&active};
            if (!Call(StageLoader::setActive, quad.Get(), activeArgs) || !Call(StageLoader::setActive, root.Get(), activeArgs))
            { error = "独立视频幕布激活失败"; return false; }
            return true;
        }
        inline bool Poll(Frame& frame, std::string& error)
        {
            void* vp = player.Get();
            if (!vp || !Read(getPrepared, vp, frame.prepared)) { error = "视频播放器状态不可用"; return false; }
            if (!frame.prepared) { decodedSample = frame; return true; }
            if (!Read(getFrame, vp, frame.frame) || !Read(getWidth, vp, frame.width) || !Read(getHeight, vp, frame.height))
            { error = "无法读取解码帧状态"; return false; }
            void* texture = nullptr; Scope roots;
            if (!Invoke(getTexture, vp, nullptr, texture)) { error = "无法读取视频纹理"; return false; }
            frame.texture = texture != nullptr;
            decodedSample = frame;
            if (!texture || frame.frame < 0) return true;
            if (!roots.Keep(texture)) { error = "无法保持当前视频纹理"; return false; }
            if (assignedTexture != reinterpret_cast<uintptr_t>(texture))
            {
                void* property = StageLoader::stringNew(textureProperty.c_str());
                if (!roots.Keep(property)) { error = "视频纹理属性名不可用"; return false; }
                void* args[]{property, texture};
                if (!Call(materialSetTexture, material.Get(), args)) { error = "视频纹理接入独立幕布失败"; return false; }
                assignedTexture = reinterpret_cast<uintptr_t>(texture); // Equality token only.
            }
            return true;
        }
        inline Backend MakeBackend()
        {
            return {nullptr,
                [](void*, const Settings& s, const Context& c, std::string& e) { return Create(s,c,e); },
                [](void*, std::string& e) { if (Call(prepare, player.Get())) return true; e="视频准备调用失败"; return false; },
                [](void*, Frame& f, std::string& e) { return Poll(f,e); },
                [](void*, bool paused, std::string& e) { if (Call(paused ? pause : play, player.Get())) return true; e="视频播放状态设置失败"; return false; },
                [](void*, const Settings& s, const Frame& f, std::string& e) { return Geometry(s,f,e); },
                [](void*, std::string& e) { return Destroy(e); },
                [](void*) { return HasObjects(); },
                [](void*) { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }};
        }
    }
    inline Backend backend = Native::MakeBackend();
#else
    inline Backend backend;
#endif
    inline Runtime runtime;
    inline void Tick(const Settings& settings, const Context& context, std::string& notice)
    {
#ifndef GK_STUDIO_VIDEO_FAKE_RUNTIME
        if (!context.allowed && !Native::HasObjects()) Native::diagnostics.clear();
#endif
        runtime.Tick(backend, settings, context, notice);
    }
    inline bool Revoke(std::string& notice)
    {
        const bool result = runtime.Revoke(backend, notice);
#ifndef GK_STUDIO_VIDEO_FAKE_RUNTIME
        if (result) Native::diagnostics.clear();
#endif
        return result;
    }
    inline bool Shutdown(std::string& notice) { return Revoke(notice); }
    inline bool HasObjects() { return backend.hasObjects && backend.hasObjects(backend.state); }
    inline std::string Diagnostics()
    {
#ifndef GK_STUDIO_VIDEO_FAKE_RUNTIME
        return Native::diagnostics; // Cached values only; safe for a status reader.
#else
        return {};
#endif
    }
}
