#pragma once

namespace ExternalStage
{
    inline constexpr char placeholder[] = "env_3d_live_courtyard-00-00-noon";
    inline StageLoader::Handle instance{};
    inline int attemptedScene{};
    inline const void* destroy{};
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
    }

    inline void Tick(bool admitted, int activeScene)
    {
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
        auto& metadata = StageLoader::metadata;
        void* bundleClass = FindClass("UnityEngine.AssetBundleModule.dll", "UnityEngine", "AssetBundle");
        void* objectClass = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Object");
        if (!bundleClass || !objectClass || !StageLoader::newHandle || !StageLoader::freeHandle ||
            !StageLoader::stringNew || !SceneOwnerRuntime::handleTarget) return;
        auto load = metadata.Find(bundleClass, "LoadFromFile", true, {0x12, bundleClass}, {{0x0e}});
        auto asset = metadata.Find(bundleClass, "LoadAsset", false, {0x12, objectClass}, {{0x0e}});
        auto unload = metadata.Find(bundleClass, "Unload", false, {0x01}, {{0x02}});
        auto instantiate = metadata.Find(objectClass, "Instantiate", true, {0x12, objectClass}, {{0x12, objectClass}});
        destroy = metadata.Find(objectClass, "Destroy", true, {0x01}, {{0x12, objectClass}});
        if (!load || !asset || !unload || !instantiate || !destroy) return;

        void* path = StageLoader::stringNew("GakuLoader/plugins/GakuMotion.ExternalStage/meni-shuki-rush.bundle");
        if (!path) return;
        void* loadArgs[]{path};
        void* bundle = StageLoader::Invoke(load, nullptr, loadArgs);
        if (!bundle)
        {
            GakuMotion::host::Warn("[GakuMotion][ExternalStage] cannot open meni-shuki-rush.bundle");
            return;
        }
        bool unloadObjects = false;
        void* unloadArgs[]{&unloadObjects};
        void* address = StageLoader::stringNew("meni-shuki-rush");
        void* assetArgs[]{address};
        void* prefab = address ? StageLoader::Invoke(asset, bundle, assetArgs) : nullptr;
        void* instantiateArgs[]{prefab};
        void* root = prefab ? StageLoader::Invoke(instantiate, nullptr, instantiateArgs) : nullptr;
        if (root) instance = StageLoader::newHandle(root, false);
        if (root && !instance)
        {
            void* destroyArgs[]{root};
            StageLoader::Invoke(destroy, nullptr, destroyArgs);
        }
        StageLoader::Invoke(unload, bundle, unloadArgs);
        if (instance) status.store(1);
        GakuMotion::host::Info("[GakuMotion][ExternalStage] placeholder=%s scene=%d instantiated=%d",
            placeholder, activeScene, !!instance);
    }
}
