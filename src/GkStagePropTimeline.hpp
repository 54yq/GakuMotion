#pragma once

#include "GkMotionData.hpp"
#include <filesystem>
#include <fstream>

namespace GakuMotion::stage_prop_timeline
{
    struct Cue
    {
        int prop{}, place{};
        bool left{};
        int first{}, last{};
        Vector3 position{}, rotation{};
    };

    // Meni Shuki Rush preset, aligned to the reference video at 30 fps.
    inline const std::vector<Cue> meniDefaults{
        {1, 1, false, 939, 1353, {}, {0, 180, 0}},
        {2, 8, false, 1095, 1353},
        {5, 3, false, 1060, 1353},
        {6, 8, true, 1095, 1353},
        {7, 2, true, 1038, 1353},
        {8, 7, true, 1100, 1353},
        {10, 4, false, 1353, 1400}, {11, 4, true, 1353, 1400},
        {10, 1, true, 1400, 1690}, {11, 1, false, 1400, 1690},
        {10, 4, false, 1690, 1710}, {11, 4, true, 1690, 1710},
    };

    inline constexpr size_t maxCues = 128;
    inline constexpr int lastFrame = 4806;

    inline bool Valid(const Cue& cue)
    {
        const auto within = [](const Vector3& value, float limit) {
            return std::abs(value.x) <= limit && std::abs(value.y) <= limit && std::abs(value.z) <= limit;
        };
        return cue.prop >= 1 && cue.prop <= 11 && cue.place >= 1 && cue.place <= 10 &&
            cue.first >= 0 && cue.first < cue.last && cue.last <= lastFrame &&
            within(cue.position, 2) && within(cue.rotation, 360);
    }

    inline bool Overlap(const Cue& a, const Cue& b)
    {
        return a.prop == b.prop && a.first < b.last && b.first < a.last;
    }

    inline std::vector<Cue> Load(const std::filesystem::path& path)
    {
        std::ifstream file(path);
        file.imbue(std::locale::classic());
        std::string version;
        size_t count{};
        if (!(file >> version >> count) || version != "MeniPropTimeline1" || count > maxCues) return meniDefaults;
        std::vector<Cue> cues;
        for (size_t index = 0; index < count; ++index)
        {
            Cue cue;
            if (!(file >> cue.prop >> cue.place >> cue.left >> cue.first >> cue.last
                >> cue.position.x >> cue.position.y >> cue.position.z >> cue.rotation.x >> cue.rotation.y >> cue.rotation.z)
                || !Valid(cue) || std::any_of(cues.begin(), cues.end(), [&](const Cue& other) { return Overlap(cue, other); }))
                return meniDefaults;
            cues.push_back(cue);
        }
        return cues;
    }

    inline bool Save(const std::filesystem::path& path, const std::vector<Cue>& cues)
    {
        auto temp = path;
        temp += ".tmp";
        std::ofstream file(temp, std::ios::trunc);
        file.imbue(std::locale::classic());
        file << "MeniPropTimeline1 " << cues.size() << '\n';
        for (const auto& c : cues)
            file << c.prop << ' ' << c.place << ' ' << c.left << ' ' << c.first << ' ' << c.last << ' '
                << c.position.x << ' ' << c.position.y << ' ' << c.position.z << ' '
                << c.rotation.x << ' ' << c.rotation.y << ' ' << c.rotation.z << '\n';
        file.close();
        std::error_code error;
        if (file) std::filesystem::rename(temp, path, error);
        if (file && !error) return true;
        std::filesystem::remove(temp, error);
        return false;
    }
}
