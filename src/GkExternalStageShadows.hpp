#pragma once

namespace ExternalStage
{
    inline constexpr size_t maxShadowContacts = 60;

    inline void UpdateContactShadows(GakuMotion::Core& core)
    {
        auto& controller = core.player.controller;
        static int countId{}, sizeId{};
        static Handle contacts{};
        using Clock = std::chrono::steady_clock;
        constexpr auto maxAge = std::chrono::milliseconds(100);
        struct ShadowCenter
        {
            float x{}, z{};
            Clock::time_point measured{};
            uint64_t token{};
            const char* failure{};
            bool held{};
        };
        static std::unordered_map<void*, ShadowCenter> centers, previous;
        static auto lastSample = std::chrono::steady_clock::time_point{};
        static int sampleScene{}, sampleOwner{}, sampleSeek{-1};
        static uintptr_t samplePresenter{};
        static bool wasScrubbing{};
        static const char* shaderFailure{};
        const auto reportShader = [&](const char* error) {
            if (shaderFailure == error) return;
            GakuMotion::host::Info("[GakuMotion][ContactShadow] shader=%s stage=%d",
                error ? error : "recovered", mountedScene);
            shaderFailure = error;
        };
        const bool enabled = GakuMotion::externalStageContactShadows.load();
        static bool previouslyEnabled = true;
        if (previouslyEnabled != enabled)
        {
            GakuMotion::host::Info("[GakuMotion][ContactShadow] test enabled=%d stage=%d",
                enabled, mountedScene);
            previouslyEnabled = enabled;
        }
        const bool eligible = enabled && status.load() == 1 && alignedBackground && controller.fnGetBoneTransform &&
            g_getWorldPosNative;
        if (!eligible)
        {
            centers.clear();
            previous.clear();
            lastSample = {};
            if (contacts || shaderFailure)
            {
                const bool cleared = SetStageShaderFloat("_GakuExternalStageContactCount", 0, countId);
                reportShader(cleared ? nullptr : "clear-count");
                if (contacts) StageLoader::freeHandle(contacts);
                contacts = {};
            }
            return;
        }
        static void* shader = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Shader");
        static void* vector = FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Vector4");
        if (!shader || !vector) { reportShader("resolve-types"); return; }
        static const auto arrayClass = reinterpret_cast<void* (*)(void*, uint32_t)>(
            GetProcAddress(g_gameAssembly, "il2cpp_array_class_get"));
        static const auto arrayNew = reinterpret_cast<void* (*)(void*, uintptr_t)>(
            GetProcAddress(g_gameAssembly, "il2cpp_array_new"));
        static void* vectors = arrayClass ? arrayClass(vector, 1) : nullptr;
        if (!vectors || !arrayNew) { reportShader("resolve-array"); return; }
        auto& api = StageLoader::metadata;
        static const void* setter = api.Find(shader, "SetGlobalVectorArray", true, {0x01}, {{0x08}, {0x1d, vectors}});
        static const void* propertyId = api.Find(shader, "PropertyToID", true, {0x08}, {{0x0e}});
        uint32_t alignment = 0;
        if (!setter || !propertyId || !StageLoader::valueSize ||
            StageLoader::valueSize(vector, &alignment) != 4 * sizeof(float)) { reportShader("resolve-setter"); return; }
        static int id = [&]() {
            void* name = StageLoader::stringNew("_GakuExternalStageContacts");
            void* args[]{name};
            return name ? StageLoader::ReadInt(propertyId, nullptr, 0, args) : 0;
        }();
        if (!id) { reportShader("resolve-property"); return; }
        if (!contacts)
        {
            if (void* array = arrayNew(vector, maxShadowContacts))
                contacts = StageLoader::newHandle(array, false);
        }
        if (!contacts) { reportShader("allocate-array"); return; }
        std::array<std::array<float, 4>, maxShadowContacts> measurements{};
        size_t count = 0;
        const auto now = std::chrono::steady_clock::now();
        const int seek = core.player.SeekCount();
        const bool scrubbing = core.player.scrubbing;
        if (sampleScene != mountedScene || sampleOwner != owner.sceneHandle ||
            samplePresenter != owner.presenterIdentity || sampleSeek != seek || scrubbing || wasScrubbing)
        {
            centers.clear();
            previous.clear();
        }
        const float dt = std::chrono::duration<float>(now - lastSample).count();
        const float blend = 1.0f - std::exp(-std::max(0.0f, dt) / 0.045f);
        lastSample = now;
        sampleScene = mountedScene;
        sampleOwner = owner.sceneHandle;
        samplePresenter = owner.presenterIdentity;
        sampleSeek = seek;
        wasScrubbing = scrubbing;
        previous.swap(centers);
        centers.clear();
        const auto add = [&](void* actor, uint64_t token) {
            if (!actor || count >= measurements.size() || core.actorModelHidden.contains(token)) return;
            ShadowCenter center{};
            if (const auto prior = previous.find(actor); prior != previous.end() && prior->second.token == token)
                center = prior->second;
            center.token = token;
            const bool recent = center.measured != Clock::time_point{} && now - center.measured < maxAge;
            void* object = StageLoader::Invoke(StageLoader::componentGo, actor);
            const int actorScene = object ? StageLoader::SceneOfFreshGo(object) : 0;
            const char* failure = nullptr;
            if (!object) failure = "game-object-unavailable";
            else if (actorScene != owner.sceneHandle) failure = "actor-scene-mismatch";
            else if (void* hips = controller.fnGetBoneTransform(actor, 0))
            {
                const auto p = g_getWorldPosNative(hips);
                if (!std::isfinite(p.x) || !std::isfinite(p.z)) failure = "nonfinite-hips";
                else
                {
                    const bool smooth = recent && std::hypot(p.x - center.x, p.z - center.z) < 0.8f;
                    center.x = smooth ? std::lerp(center.x, p.x, blend) : p.x;
                    center.z = smooth ? std::lerp(center.z, p.z, blend) : p.z;
                    center.measured = now;
                }
            }
            else failure = "hips-unavailable";

            const bool held = failure && recent;
            if (failure != center.failure || held != center.held)
            {
                GakuMotion::host::Info("[GakuMotion][ContactShadow] actor=%p token=%llu state=%s reason=%s actorScene=%d ownerScene=%d stage=%d seek=%d",
                    actor, static_cast<unsigned long long>(token),
                    failure ? (held ? "holding" : "omitted") : "recovered",
                    failure ? failure : center.failure, actorScene, owner.sceneHandle, mountedScene, seek);
            }
            center.failure = failure;
            center.held = held;
            centers.emplace(actor, center);
            if (!failure || held)
                measurements[count++] = {center.x, alignedFloor, center.z, -0.72f};
        };
        const auto* targetEntry = core.sceneActorRegistry.Find(controller.targetActor);
        add(controller.targetActor, targetEntry ? targetEntry->token : 0);
        for (const auto& entry : core.sceneActorRegistry.Entries())
            if (entry.actor != controller.targetActor) add(entry.actor, entry.token);
        void* array = SceneOwnerRuntime::handleTarget(contacts);
        if (!array) { reportShader("array-handle"); return; }
        std::memcpy(static_cast<char*>(array) + 32, measurements.data(), sizeof(measurements));
        void* args[]{&id, array};
        const bool arrayWritten = StageLoader::TryInvokeStaticVoid(setter, args);
        const bool sizeWritten = SetStageShaderFloat("_GakuExternalStageContactSize",
            GakuMotion::externalStageShadowSize.load(), sizeId);
        // The previous vector array remains valid when Unity rejects an update.
        // Keep its count too, so a rejected frame cannot blank all contacts.
        const bool countWritten = arrayWritten && SetStageShaderFloat("_GakuExternalStageContactCount",
            static_cast<float>(count), countId);
        reportShader(!arrayWritten ? "write-array" : !countWritten ? "write-count" :
            !sizeWritten ? "write-size" : nullptr);
        static int lastScene{};
        static size_t lastCount{};
        if (lastScene != mountedScene || lastCount != count)
        {
            GakuMotion::host::Info("[GakuMotion][ExternalStage] contact shadows scene=%d samples=%zu",
                mountedScene, count);
            lastScene = mountedScene;
            lastCount = count;
        }
    }
}
