#pragma once
// Read-only evidence for distinguishing camera viewport and Live UI sizing.
// Runs on the Unity thread; never changes the camera or a scene object.
namespace LiveLetterboxProbe
{
    template<class T>
    inline bool Read(const void* method, void* object, T& value)
    {
        if (!method || !object || !g_faceRuntime.invoke || !g_safeBones.unbox) return false;
        void* ex = nullptr;
        void* boxed = g_faceRuntime.invoke(method, object, nullptr, &ex);
        void* raw = boxed && !ex ? g_safeBones.unbox(boxed) : nullptr;
        if (!raw) return false;
        std::memcpy(&value, raw, sizeof(value));
        return true;
    }

    inline bool HasValueSize(void* klass, int size)
    {
        using ValueSize = int (*)(void*, uint32_t*);
        auto valueSize = reinterpret_cast<ValueSize>(GetProcAddress(g_gameAssembly, "il2cpp_class_value_size"));
        uint32_t alignment = 0;
        return klass && valueSize && valueSize(klass, &alignment) == size;
    }

    inline void ReportCamera()
    {
        const auto md = GroundShadowProbeBootstrap::Resolve(g_gameAssembly).method;
        void* cameraClass = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Camera");
        void* rectClass = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Rect");
        if (!cameraClass || !HasValueSize(rectClass, 16)) return;
        const void* getRect = md.Find(cameraClass, "get_rect", false, {0x11, rectClass}, {});
        const void* getPixelRect = md.Find(cameraClass, "get_pixelRect", false, {0x11, rectClass}, {});
        const void* getAspect = md.Find(cameraClass, "get_aspect", false, {0x0c}, {});
        void* camera = HelperCameraGetMain();
        if (!camera || !CameraBridge::IsAlive(camera) || !getRect || !getPixelRect || !getAspect) return;
        float rect[4]{}, pixel[4]{}, aspect{};
        if (!Read(getRect, camera, rect) || !Read(getPixelRect, camera, pixel) ||
            !Read(getAspect, camera, aspect)) return;
        for (float f : rect) if (!std::isfinite(f)) return;
        for (float f : pixel) if (!std::isfinite(f)) return;
        if (!std::isfinite(aspect)) return;
        g_hostContext.logInfo("[GakuMotion][LiveLetterboxProbe] camera_rect=%.4f,%.4f,%.4f,%.4f pixel_rect=%.1f,%.1f,%.1f,%.1f aspect=%.4f read_only=1",
            rect[0],rect[1],rect[2],rect[3],pixel[0],pixel[1],pixel[2],pixel[3],aspect);

        void* matrixClass = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Matrix4x4");
        if (!HasValueSize(matrixClass, 64)) return;
        const void* getProjection = md.Find(cameraClass, "get_projectionMatrix", false, {0x11, matrixClass}, {});
        float matrix[16]{};
        if (Read(getProjection, camera, matrix) && std::isfinite(matrix[0]) && std::isfinite(matrix[5]) &&
            std::isfinite(matrix[8]) && std::isfinite(matrix[9]))
            g_hostContext.logInfo("[GakuMotion][LiveLetterboxProbe] projection scale_x=%.5f scale_y=%.5f shift_x=%.5f shift_y=%.5f",
                matrix[0], matrix[5], matrix[8], matrix[9]);
    }

    struct Fitters
    {
        void* fitterClass{};
        void* fitter{};
        const void* getRatio{};
        unsigned count{};
        float ratio{};
    };

    inline Fitters FindFitters(int ownerScene)
    {
        if (!ownerScene || !g_gameAssembly) return {};
        const auto md = GroundShadowProbeBootstrap::Resolve(g_gameAssembly).method;
        void* viewClass = FindClass("Assembly-CSharp.dll", "Campus.Live", "LiveSceneContentView");
        void* fitterClass = FindClass("UnityEngine.UI.dll", "UnityEngine.UI", "AspectRatioFitter");
        if (!viewClass || !fitterClass || !il2cpp_class_get_type || !il2cpp_type_get_object ||
            !StageLoader::findAll || !StageLoader::componentGo || !StageLoader::goScene || !StageLoader::arrayLength ||
            !StageLoader::newHandle || !StageLoader::freeHandle || !g_faceRuntime.findField || !g_faceRuntime.fieldType ||
            !g_faceRuntime.fieldFlags || !g_faceRuntime.typeCode || !g_faceRuntime.readField || !md.classFromType) return {};
        void* field = g_faceRuntime.findField(viewClass, "_aspectRatioFitter");
        void* fieldType = field ? g_faceRuntime.fieldType(field) : nullptr;
        const void* getRatio = md.Find(fitterClass, "get_aspectRatio", false, {0x0c}, {});
        if (!fieldType || (g_faceRuntime.fieldFlags(field) & 0x10) || g_faceRuntime.typeCode(fieldType) != 0x12 ||
            md.classFromType(fieldType) != fitterClass || !getRatio) return {};
        void* type = il2cpp_class_get_type(viewClass);
        void* typeObject = type ? il2cpp_type_get_object(type) : nullptr;
        bool includeInactive = false; void* args[]{typeObject, &includeInactive};
        void* array = typeObject ? StageLoader::Invoke(StageLoader::findAll, nullptr, args) : nullptr;
        auto pin = array ? StageLoader::newHandle(array, false) : 0;
        if (!pin) return {};
        struct Release { StageLoader::Handle pin; ~Release() { StageLoader::freeHandle(pin); } } release{pin};
        const uintptr_t count = StageLoader::arrayLength(array);
        if (count > 128) return {};
        auto items = reinterpret_cast<void**>(static_cast<char*>(array) + 32);
        Fitters result{fitterClass, nullptr, getRatio};
        for (uintptr_t i = 0; i < count; ++i) {
            void* view = items[i];
            void* go = view ? StageLoader::Invoke(StageLoader::componentGo, view) : nullptr;
            if (!go || StageLoader::SceneOfFreshGo(go) != ownerScene) continue;
            void* fitter = nullptr; g_faceRuntime.readField(view, field, &fitter);
            float current = 0;
            if (fitter && CameraBridge::IsAlive(fitter) && Read(getRatio, fitter, current) && std::isfinite(current)) {
                result.fitter = fitter;
                result.ratio = current;
                ++result.count;
            }
        }
        return result;
    }

    inline void Report(int ownerScene)
    {
        if (!ownerScene || !g_gameAssembly || !g_hostContext.logInfo) return;
        ReportCamera();
        const auto ui = FindFitters(ownerScene);
        g_hostContext.logInfo("[GakuMotion][LiveLetterboxProbe] ui_fitters=%u ui_aspect=%.4f read_only=1", ui.count, ui.ratio);
    }
}
