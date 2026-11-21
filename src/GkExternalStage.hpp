#pragma once

namespace ExternalStage
{
    inline constexpr char placeholder[] = "env_3d_live_courtyard-00-00-noon";
    inline StageLoader::Handle instance{};
    inline int attemptedScene{};
    inline const void *destroy{}, *done{}, *getBundle{}, *asset{}, *unload{}, *instantiate{};
    inline StageLoader::Handle request{};
    inline StageLoader::Handle mountedBundle{};
    inline int requestScene{};

    inline void Unload(void* bundle)
    {
        if (!bundle) return;
        bool all = true;
        void* args[]{&all};
        StageLoader::Invoke(unload, bundle, args);
    }
    inline std::atomic<int> status{0};

    inline void Release()
    {
        if (instance)
        {
            if (void* root = SceneOwnerRuntime::handleTarget(instance))
            {
                void* args[]{root};
                StageLoader::Invoke(destroy, nullptr, args);
            }
            StageLoader::freeHandle(instance);
            instance = {};
        }
        if (mountedBundle)
        {
            Unload(SceneOwnerRuntime::handleTarget(mountedBundle));
            StageLoader::freeHandle(mountedBundle);
            mountedBundle = {};
        }
    }

    inline void Tick(bool admitted, int activeScene)
    {
        if (request)
        {
            void* pending = SceneOwnerRuntime::handleTarget(request);
            if (pending && !StageLoader::ReadBool(done, pending, false)) return;
            void* bundle = StageLoader::Invoke(getBundle, pending);
            StageLoader::freeHandle(request);
            request = {};
            if (!admitted || activeScene != requestScene)
                Unload(bundle);
            else
            {
                void* address = StageLoader::stringNew("meni-shuki-rush");
                void* args[]{address, StageLoader::goType};
                void* prefab = bundle && address ? StageLoader::Invoke(asset, bundle, args) : nullptr;
                void* spawnArgs[]{prefab};
                void* root = prefab ? StageLoader::Invoke(instantiate, nullptr, spawnArgs) : nullptr;
                if (root)
                {
                    instance = StageLoader::newHandle(root, false);
                    mountedBundle = StageLoader::newHandle(bundle, false);
                    if (!instance || !mountedBundle)
                    {
                        void* destroyArgs[]{root};
                        if (!instance) StageLoader::Invoke(destroy, nullptr, destroyArgs);
                        if (!mountedBundle) Unload(bundle);
                        Release();
                    }
                }
                else Unload(bundle);
                status.store(instance ? 1 : -1);
                GakuMotion::host::Info("[GakuMotion][ExternalStage] scene=%d bundle=%d prefab=%d instantiated=%d",
                    activeScene, !!bundle, !!prefab, !!instance);
                return;
            }
        }
        if (!admitted)
        {
            Release();
            attemptedScene = 0;
            status.store(0);
            return;
        }
        if (!activeScene || attemptedScene == activeScene) return;
        Release();
        attemptedScene = activeScene;
        status.store(-1);
        GakuMotion::host::Info("[GakuMotion][ExternalStage] loading Meni Shuki Rush scene=%d", activeScene);
        auto& metadata = StageLoader::metadata;
        void* bundleClass = FindClass("UnityEngine.AssetBundleModule.dll", "UnityEngine", "AssetBundle");
        void* objectClass = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Object");
        if (!bundleClass || !objectClass || !StageLoader::newHandle || !StageLoader::freeHandle ||
            !StageLoader::stringNew || !SceneOwnerRuntime::handleTarget)
        {
            GakuMotion::host::Warn("[GakuMotion][ExternalStage] Unity classes or GC handles unavailable");
            return;
        }
        void* requestClass = FindClass("UnityEngine.AssetBundleModule.dll", "UnityEngine", "AssetBundleCreateRequest");
        void* operation = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "AsyncOperation");
        auto load = metadata.Find(bundleClass, "LoadFromFileAsync", true, {0x12, requestClass}, {{0x0e}});
        if (!load) load = metadata.Find(bundleClass, "LoadFromFileAsync_Internal", true,
            {0x12, requestClass}, {{0x0e}, {0x09}, {0x0b}});
        const bool internal = load && metadata.count(load) == 3;
        done = metadata.Find(operation, "get_isDone", false, {0x02}, {});
        getBundle = metadata.Find(requestClass, "get_assetBundle", false, {0x12, bundleClass}, {});
        asset = metadata.Find(bundleClass, "LoadAsset_Internal", false, {0x12, objectClass}, {{0x0e}, {0x12}});
        if (!asset) asset = metadata.Find(bundleClass, "LoadAsset", false, {0x12, objectClass}, {{0x0e}, {0x12}});
        unload = metadata.Find(bundleClass, "Unload", false, {0x01}, {{0x02}});
        instantiate = metadata.Find(objectClass, "Instantiate", true, {0x12, objectClass}, {{0x12, objectClass}});
        destroy = metadata.Find(objectClass, "Destroy", true, {0x01}, {{0x12, objectClass}});
        if (!load || !done || !getBundle || !StageLoader::goType || !asset || !unload || !instantiate || !destroy)
        {
            GakuMotion::host::Warn("[GakuMotion][ExternalStage] Unity methods unavailable load=%d asset=%d unload=%d instantiate=%d destroy=%d",
                !!load, !!asset, !!unload, !!instantiate, !!destroy);
            return;
        }

        wchar_t executable[32768];
        const DWORD length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
        if (!length || length >= std::size(executable))
        {
            GakuMotion::host::Warn("[GakuMotion][ExternalStage] game directory unavailable");
            return;
        }
        const auto assetPath = (std::filesystem::path(executable).parent_path() /
            "GakuLoader/plugins/GakuMotion.ExternalStage/meni-shuki-rush.bundle").u8string();
        void* path = StageLoader::stringNew(reinterpret_cast<const char*>(assetPath.c_str()));
        if (!path) return;
        uint32_t crc = 0;
        uint64_t offset = 0;
        void* loadArgs[]{path, &crc, &offset};
        void* pending = StageLoader::Invoke(load, nullptr, loadArgs);
        request = pending ? StageLoader::newHandle(pending, false) : nullptr;
        requestScene = activeScene;
        GakuMotion::host::Info("[GakuMotion][ExternalStage] async request=%d internal=%d scene=%d",
            !!request, internal, activeScene);
    }
}
