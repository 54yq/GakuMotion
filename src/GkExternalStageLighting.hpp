#pragma once

namespace ExternalStage
{
    inline std::array<float, 3> SampleAmbient(float frame)
    {
        const auto* begin = profile.ambientBegin;
        const auto* end = profile.ambientEnd;
        if (begin == end) return {1, 1, 1};
        const auto* next = std::upper_bound(begin, end, frame,
            [](float f, const auto& key) { return f < key.frame; });
        if (next == begin) return begin->rgb;
        if (next == end) return (end - 1)->rgb;
        const auto* previous = next - 1;
        const float t = (frame - previous->frame) / (next->frame - previous->frame);
        std::array<float, 3> rgb{};
        for (size_t i = 0; i < rgb.size(); ++i)
            rgb[i] = std::lerp(previous->rgb[i], next->rgb[i], t);
        return rgb;
    }

    inline void TickActorAmbient(GakuMotion::Core& core)
    {
        using namespace FixedActorLighting;
        static int appliedScene{};
        static int failedScene{};
        static Sample original{};
        static float previousFrame = -1;
        if (!ProtectionOnUnityThread()) return;
        const bool eligible = status.load() == 1 && mountedScene &&
            GakuMotion::externalStageActorAmbient.load() &&
            core.injectionPolicy.Read().captureDepth == 0 && !GakuMotion::visual_handoff::Requested();
        if (!eligible || appliedScene != mountedScene)
        {
            if (appliedScene && !Release()) return;
            appliedScene = 0;
            if (!eligible)
            {
                failedScene = 0;
                FixedActorLighting::Tick(core);
                return;
            }
        }
        if (failedScene == mountedScene) return;
        if (!appliedScene || !volumeObject.handle)
        {
            const int layer = Resolve() ? CameraLayer() : -1;
            if (layer < 0 || !Release() || !ReadNativeSample(original) || !Create(layer, core, true))
            {
                failedScene = mountedScene;
                Release();
                GakuMotion::host::Warn("[GakuMotion][ExternalStage] photography camera volume unavailable; keeping native actor lighting");
                return;
            }
            appliedScene = mountedScene;
            previousFrame = -1;
            GakuMotion::host::Info("[GakuMotion][ExternalStage] actor ambient bound to stage clock");
        }
        const float frame = GakuMotion::externalStageTime.load() * 30.0f;
        if (frame == previousFrame) return;
        const auto rgb = SampleAmbient(frame);
        const auto tint = [&](Color color) { return Color{color.r * rgb[0], color.g * rgb[1], color.b * rgb[2], color.a}; };
        const auto key = tint(original.key), shade = tint(original.shade);
        const auto fill = tint(original.fill), rim = tint(original.rim);
        if (Parameter("lightColor", {0x11, colorClass}, &key) &&
            Parameter("shadeColorMultiply", {0x11, colorClass}, &shade) &&
            Parameter("shadeColorAdditive", {0x11, colorClass}, &fill) &&
            Parameter("rimColor", {0x11, colorClass}, &rim))
            previousFrame = frame;
        else
        {
            failedScene = mountedScene;
            if (Release()) appliedScene = 0;
            GakuMotion::host::Warn("[GakuMotion][ExternalStage] actor ambient write failed");
        }
    }
}
