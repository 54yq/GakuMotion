#pragma once

// Included in the plugin's private namespace after SceneOwnerRuntime.
// Rewrite a Photography resource-name result BEFORE native bundle/scene load.
// Native asynchronous tasks, cancellation and scene ownership are untouched.
namespace StageLoadOverride
{
    using GetEnvFn = void* (*)(void*, int, const void*);
    inline GetEnvFn original = nullptr;
    inline std::atomic<bool> enabled{false};
    inline const void* envMethod = nullptr;
    inline void* target = nullptr;
    inline bool attempted = false;
    inline void* (*nestedTypes)(void*, void**) = nullptr;
    inline const char* (*className)(void*) = nullptr;
    inline uint32_t samples = 0, changes = 0;
    inline std::atomic<uint32_t> entries[3]{};
    inline int observedReplacementScene = 0;

    inline void ObserveActiveScene(int sceneHandle, const std::string& name)
    {
        if (!sceneHandle || !g_activeSceneVerified ||
            g_sceneOwner.AllowedContext() != GakuMotion::injection::Context::Photography) return;
        const bool replacement = GakuMotion::stage_load::IsTarget(name) &&
            GakuMotion::stage_load::receipt.Context(name, g_verifiedOwnerSceneName, g_sceneOwner) ==
                GakuMotion::injection::Context::Photography;
        if (replacement && observedReplacementScene != sceneHandle)
        {
            observedReplacementScene = sceneHandle;
            GakuMotion::host::Info("[GakuMotion][StageLoad] replacement Active Scene=%s handle=%d owner_scene=%d (visual acceptance pending)",
                name.c_str(), sceneHandle, g_sceneOwner.sceneHandle);
        }
        else if (!replacement && observedReplacementScene &&
            GakuMotion::injection::PreviewContext(name, g_verifiedOwnerSceneName, g_sceneOwner) ==
                GakuMotion::injection::Context::Photography)
        {
            observedReplacementScene = 0;
            GakuMotion::host::Info("[GakuMotion][StageLoad] native photography Active Scene=%s handle=%d owner_scene=%d",
                name.c_str(), sceneHandle, g_sceneOwner.sceneHandle);
        }
    }

    inline bool ReadKey(void* value, std::string& key)
    {
        if (!value) return false;
        const auto* text = static_cast<const Il2CppString*>(value);
        if (text->length <= 0 || text->length > 256) return false;
        key.clear();
        for (int32_t i = 0; i < text->length; ++i)
        {
            if (text->chars[i] <= 0 || text->chars[i] > 127) return false;
            key += static_cast<char>(text->chars[i]);
        }
        return true;
    }

    inline bool Allowed()
    {
        if (!enabled.load(std::memory_order_acquire) ||
            !g_runtimeReady.load(std::memory_order_acquire) || !ProtectionOnUnityThread()) return false;
        RefreshProtectedContext(true);
        return g_activeSceneVerified &&
            GakuMotion::stage_load::receipt.Context(SceneOwnerRuntime::cachedActiveScene,
                g_verifiedOwnerSceneName, g_sceneOwner) == GakuMotion::injection::Context::Photography;
    }

    inline void* GetEnvAssetId(void* background, int timeType, const void* method)
    {
        // Preserve all original argument validation and normal key generation.
        void* result = original(background, timeType, method);
        if (method && method != envMethod) return result;
        const bool allowed = Allowed();
        const bool main = ProtectionOnUnityThread();
        const bool runtime = g_runtimeReady.load(std::memory_order_acquire);
        const int category = allowed ? 0 : main && runtime ? 1 : 2;
        if (entries[category].fetch_add(1, std::memory_order_relaxed) < 6)
        {
            std::string observed;
            if (main && runtime) ReadKey(result, observed);
            GakuMotion::host::Info(
                "[GakuMotion][StageLoad] key entry allowed=%d main=%d runtime=%d original=%s time=%d",
                allowed, main, runtime, observed.c_str(), timeType);
        }
        if (!allowed) return result;
        const std::string replacement = GakuMotion::Core::Instance().stageManager.GetLoadReplacementKey();
        if (!GakuMotion::stage_load::IsTarget(replacement)) return result;
        std::string key;
        if (!ReadKey(result, key) || !key.starts_with("env_3d_")) return result;
        if (samples++ < 8) GakuMotion::host::Info(
            "[GakuMotion][StageLoad] photo resource request original=%s target=%s time=%d owner_scene=%d",
            key.c_str(), replacement.c_str(), timeType, g_sceneOwner.sceneHandle);
        // Even an equal target needs a receipt: it can be a subsequent request
        // in the same native load. Allocation failure always preserves result.
        void* changed = key == replacement ? result : StageLoader::stringNew(replacement.c_str());
        if (!changed) return result;
        GakuMotion::stage_load::receipt.Record(g_sceneOwner, replacement);
        if (changes++ < 8) GakuMotion::host::Info(
            "[GakuMotion][StageLoad] resource key redirected %s -> %s owner_scene=%d (native load completion/visual pending)",
            key.c_str(), replacement.c_str(), g_sceneOwner.sceneHandle);
        return changed;
    }

    inline void Shutdown()
    {
        enabled.store(false, std::memory_order_release);
        if (target)
        {
            const auto status = GakuMotion::host::hooks.release
                ? GakuMotion::host::hooks.release(GakuMotion::host::hooks.context, target) : GAKU_E_UNAVAILABLE;
            if (status == GAKU_OK) target = nullptr;
            else GakuMotion::host::Warn("[GakuMotion][StageLoad] hook release status=%d; forwarding retained", status);
        }
        if (!target) attempted = false;
        // Original trampoline is process-lifetime, including failed release.
    }

    inline bool Install()
    {
        if (attempted) return enabled.load(std::memory_order_acquire);
        attempted = true;
        auto& api = StageLoader::metadata;
        void* rules = FindClass("Assembly-CSharp.dll", "Campus.Common.AssetNames", "ABRule");
        void* photo = nullptr;
        if (rules && nestedTypes && className)
        {
            void* iterator = nullptr;
            while (void* candidate = nestedTypes(rules, &iterator))
            {
                const char* name = className(candidate);
                if (!name || std::strcmp(name, "PhotoBackground")) continue;
                if (photo) { photo = nullptr; break; }
                photo = candidate;
            }
        }
        void* timeClass = FindClass("Assembly-CSharp.dll", "Campus.Common.Proto.Client.Enums", "PhotoBackgroundTimeType");
        uint32_t alignment = 0;
        const bool enumLayout = timeClass && StageLoader::valueSize &&
            StageLoader::valueSize(timeClass, &alignment) == sizeof(int) && alignment == alignof(int);
        envMethod = photo && enumLayout ? api.Find(photo, "GetEnvAssetId", true,
            {0x0e}, {{0x0e}, {0x11, timeClass}}) : nullptr;
        if (!envMethod || !StageLoader::stringNew || !GakuMotion::host::hooks.acquireBatch ||
            !GakuMotion::host::hooks.release)
        {
            GakuMotion::host::Warn("[GakuMotion][StageLoad] disabled: exact photo key seam unavailable photo=%p enum_layout=%d method=%p",
                photo, enumLayout, envMethod);
            return false;
        }
        void* address = *static_cast<void* const*>(envMethod);
        const GakuHookRequest request{address, reinterpret_cast<void*>(GetEnvAssetId), reinterpret_cast<void**>(&original)};
        const auto status = GakuMotion::host::hooks.acquireBatch(GakuMotion::host::hooks.context, 1, &request);
        if (status == GAKU_OK || original) target = address;
        const bool ready = status == GAKU_OK && original;
        enabled.store(ready, std::memory_order_release);
        GakuMotion::host::Info("[GakuMotion][StageLoad] photography key hook ready=%d status=%d target=%p (default native; load-time only)",
            ready, status, address);
        if (!ready) Shutdown();
        return ready;
    }
}
