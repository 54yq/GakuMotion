#pragma once

namespace ExternalStageProfiles
{
    struct AmbientKey { int frame; std::array<float, 3> rgb; };

    // BgBL keys sampled from the same data used in the stage material.
    inline constexpr AmbientKey meniAmbient[]{
        {0, {0.99483764f, 0.99483764f, 0.99483764f}},
        {1833, {0.99483764f, 0.99483764f, 0.99483764f}},
        {1834, {0.84622848f, 0.81182504f, 0.99483764f}},
        {1914, {0.84622848f, 0.81182504f, 0.99483764f}},
        {1915, {0.96668184f, 0.87092566f, 0.90926522f}},
        {3038, {0.96668184f, 0.87092566f, 0.90926522f}},
        {3143, {0.29407579f, 0.23719923f, 0.35663992f}},
        {3451, {0.29407579f, 0.23719923f, 0.35663992f}},
        {3452, {0.46035624f, 0.38232982f, 0.54618531f}},
        {3550, {0.46035624f, 0.38232982f, 0.54618531f}},
        {3551, {0.49037430f, 0.38232973f, 0.54618531f}},
        {3561, {0.49037430f, 0.38232973f, 0.54618531f}},
        {3562, {0.38232973f, 0.50860578f, 0.54618531f}},
        {3572, {0.38232973f, 0.50860578f, 0.54618531f}},
        {3573, {0.54618531f, 0.46773008f, 0.38232973f}},
        {3583, {0.54618531f, 0.46773008f, 0.38232973f}},
        {3584, {0.38232973f, 0.54618531f, 0.44060832f}},
        {3595, {0.38232973f, 0.54618531f, 0.44060832f}},
        {3596, {0.49037430f, 0.38232973f, 0.54618531f}},
        {3607, {0.49037430f, 0.38232973f, 0.54618531f}},
        {3608, {0.38232973f, 0.50860578f, 0.54618531f}},
        {3619, {0.38232973f, 0.50860578f, 0.54618531f}},
        {3620, {0.54618531f, 0.46773008f, 0.38232973f}},
        {3630, {0.54618531f, 0.46773008f, 0.38232973f}},
        {3631, {0.38232973f, 0.54618531f, 0.44060832f}},
        {3639, {0.38232973f, 0.54618531f, 0.44060832f}},
        {3648, {0.49037430f, 0.38232973f, 0.54618531f}},
        {3693, {0.49037430f, 0.38232973f, 0.54618531f}},
        {3694, {0.45935631f, 0.38258222f, 0.54434514f}},
        {3840, {0.45935631f, 0.38258222f, 0.54434514f}},
        {3881, {0.91170365f, 0.88690710f, 0.99483764f}},
        {4298, {0.91170365f, 0.88690710f, 0.99483764f}},
        {4299, {0.99483764f, 0.99483764f, 0.99483764f}},
        {4317, {0.41520920f, 0.43524057f, 0.77897668f}},
        {4369, {0.41520920f, 0.43524057f, 0.77897668f}},
        {4370, {0.99483764f, 0.99483764f, 0.99483764f}},
    };

    struct Profile
    {
        const char* scenePath;
        const char* sceneName;
        const char* bundleFile;
        const char* rootName;
        const char* lightFrameProperty;
        UnityVector3 rootOffset;
        float travelStart, travelEnd, travelZ;
        float visibilityRadius;
        const AmbientKey* ambientBegin;
        const AmbientKey* ambientEnd;
        std::array<std::string_view, 2> doorNames;
        std::array<std::string_view, 10> propNames;
    };

    inline constexpr Profile meniShukiRush{
        "Assets/MeniShukiRushStage.unity", "MeniShukiRushStage", "meni-shuki-rush-scene.bundle",
        "Meni Shuki Rush Stage", "_GakuExternalStageLightFrame",
        {-31.2703667f * 0.07795f, 0.0f, 87.4059677f * 0.07795f},
        3024.0f, 3367.0f, -116.1299973f * 0.07795f,
        116.0f, // 106 m from Place01 to the outer mesh, plus 9.1 m of stage travel.
        std::begin(meniAmbient), std::end(meniAmbient),
        {"StageDoor_iron_grate_l", "StageDoor_iron_grate_r"},
        {"pfb_chr_prop1205_00", "pfb_chr_prop1205_01", "pfb_chr_prop1205_02", "pfb_chr_prop1205_03",
         "pfb_chr_prop1500_00", "pfb_chr_prop1500_01", "pfb_chr_prop1500_02",
         "pfb_chr_prop1548_00", "pfb_chr_prop1548_01", "pfb_toon_prop1108_00"}
    };
}
