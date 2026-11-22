#pragma once

namespace ExternalStageProfiles
{
    struct Profile
    {
        const char* scenePath;
        const char* sceneName;
        const char* bundleFile;
        const char* rootName;
        const char* lightFrameProperty;
        UnityVector3 rootOffset;
        float travelStart, travelEnd, travelZ;
        std::array<std::string_view, 2> doorNames;
        std::array<std::string_view, 10> propNames;
    };

    inline constexpr Profile meniShukiRush{
        "Assets/MeniShukiRushStage.unity", "MeniShukiRushStage", "meni-shuki-rush-scene.bundle",
        "Meni Shuki Rush Stage", "_GakuExternalStageLightFrame",
        {-31.2703667f * 0.07795f, 0.0f, 87.4059677f * 0.07795f},
        3024.0f, 3367.0f, -116.1299973f * 0.07795f,
        {"StageDoor_iron_grate_l", "StageDoor_iron_grate_r"},
        {"pfb_chr_prop1205_00", "pfb_chr_prop1205_01", "pfb_chr_prop1205_02", "pfb_chr_prop1205_03",
         "pfb_chr_prop1500_00", "pfb_chr_prop1500_01", "pfb_chr_prop1500_02",
         "pfb_chr_prop1548_00", "pfb_chr_prop1548_01", "pfb_toon_prop1108_00"}
    };
}
