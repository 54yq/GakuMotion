#pragma once
// Read-only evidence for distinguishing camera viewport and Live UI sizing.
// Runs on the Unity thread; never changes the camera or a scene object.
namespace LiveLetterboxProbe
{
    inline void Report(int ownerScene)
    {
        if (!ownerScene || !g_gameAssembly || !g_hostContext.logInfo || !g_faceRuntime.invoke || !g_safeBones.unbox) return;
        const auto md = GroundShadowProbeBootstrap::Resolve(g_gameAssembly).method;
        void* cameraClass = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Camera");
        void* rectClass = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Rect");
        using ValueSize = int (*)(void*, uint32_t*);
        auto valueSize = reinterpret_cast<ValueSize>(GetProcAddress(g_gameAssembly, "il2cpp_class_value_size"));
        uint32_t alignment = 0;
        if (!cameraClass || !rectClass || !valueSize || valueSize(rectClass, &alignment) != 16) return;
        const void* getRect = md.Find(cameraClass, "get_rect", false, {0x11, rectClass}, {});
        const void* getPixelRect = md.Find(cameraClass, "get_pixelRect", false, {0x11, rectClass}, {});
        const void* getAspect = md.Find(cameraClass, "get_aspect", false, {0x0c}, {});
        void* camera = HelperCameraGetMain();
        if (!camera || !CameraBridge::IsAlive(camera) || !getRect || !getPixelRect || !getAspect) return;
        auto read = [&](const void* method, void* object, void* destination, size_t bytes) {
            void* ex = nullptr;
            void* boxed = g_faceRuntime.invoke(method, object, nullptr, &ex);
            void* raw = boxed && !ex ? g_safeBones.unbox(boxed) : nullptr;
            if (!raw) return false;
            std::memcpy(destination, raw, bytes); return true;
        };
        float rect[4]{}, pixel[4]{}, aspect{};
        if (!read(getRect, camera, rect, sizeof(rect)) || !read(getPixelRect, camera, pixel, sizeof(pixel)) ||
            !read(getAspect, camera, &aspect, sizeof(aspect))) return;
        for (float f : rect) if (!std::isfinite(f)) return;
        for (float f : pixel) if (!std::isfinite(f)) return;
        if (!std::isfinite(aspect)) return;
        g_hostContext.logInfo("[GakuMotion][LiveLetterboxProbe] camera_rect=%.4f,%.4f,%.4f,%.4f pixel_rect=%.1f,%.1f,%.1f,%.1f aspect=%.4f read_only=1",
            rect[0],rect[1],rect[2],rect[3],pixel[0],pixel[1],pixel[2],pixel[3],aspect);

        void* viewClass = FindClass("Assembly-CSharp.dll", "Campus.Live", "LiveSceneContentView");
        void* fitterClass = FindClass("UnityEngine.UI.dll", "UnityEngine.UI", "AspectRatioFitter");
        if (!viewClass || !fitterClass || !il2cpp_class_get_type || !il2cpp_type_get_object ||
            !StageLoader::findAll || !StageLoader::componentGo || !StageLoader::goScene || !StageLoader::arrayLength ||
            !StageLoader::newHandle || !StageLoader::freeHandle || !g_faceRuntime.findField || !g_faceRuntime.fieldType ||
            !g_faceRuntime.fieldFlags || !g_faceRuntime.typeCode || !g_faceRuntime.readField || !md.classFromType) return;
        void* field = g_faceRuntime.findField(viewClass, "_aspectRatioFitter");
        void* fieldType = field ? g_faceRuntime.fieldType(field) : nullptr;
        const void* getRatio = md.Find(fitterClass, "get_aspectRatio", false, {0x0c}, {});
        if (!fieldType || (g_faceRuntime.fieldFlags(field) & 0x10) || g_faceRuntime.typeCode(fieldType) != 0x12 ||
            md.classFromType(fieldType) != fitterClass || !getRatio) return;
        void* type = il2cpp_class_get_type(viewClass);
        void* typeObject = type ? il2cpp_type_get_object(type) : nullptr;
        bool includeInactive = false; void* args[]{typeObject, &includeInactive};
        void* array = typeObject ? StageLoader::Invoke(StageLoader::findAll, nullptr, args) : nullptr;
        auto pin = array ? StageLoader::newHandle(array, false) : 0;
        if (!pin) return;
        struct Release { StageLoader::Handle pin; ~Release() { StageLoader::freeHandle(pin); } } release{pin};
        const uintptr_t count = StageLoader::arrayLength(array);
        if (count > 128) return;
        auto items = reinterpret_cast<void**>(static_cast<char*>(array) + 32);
        unsigned matched = 0; float ratio = 0;
        for (uintptr_t i = 0; i < count; ++i) {
            void* view = items[i];
            void* go = view ? StageLoader::Invoke(StageLoader::componentGo, view) : nullptr;
            if (!go || StageLoader::SceneOfFreshGo(go) != ownerScene) continue;
            void* fitter = nullptr; g_faceRuntime.readField(view, field, &fitter);
            float current = 0;
            if (fitter && CameraBridge::IsAlive(fitter) && read(getRatio, fitter, &current, sizeof(current)) && std::isfinite(current)) {
                ratio = current; ++matched;
            }
        }
        g_hostContext.logInfo("[GakuMotion][LiveLetterboxProbe] ui_fitters=%u ui_aspect=%.4f read_only=1", matched, ratio);
    }
}
