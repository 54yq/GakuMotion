#pragma once

#include "GkMotionData.hpp"
#include <filesystem>
#include <fstream>
#include <optional>

namespace GakuMotion::stage_prop_timeline
{
    enum class Anchor { Grip, Camera, Stage, Hidden, CameraToStage };
    enum class Movement { Hold, Linear, Smooth, Throw, Handoff, GripThrow, SourceAttachment };

    struct Cue
    {
        int prop{}, place{};
        bool left{};
        int first{}, last{};
        Vector3 position{}, rotation{};
        Anchor anchor{Anchor::Grip};
        Movement movement{Movement::Hold};
        int bone{-1};
        int blendFrames{10};
        Vector3 endPosition{}, endRotation{};
        float arcHeight{0.5f};
        bool hideAfter{};

    };

    // Meni Shuki Rush preset, aligned to the reference video at 30 fps.
    inline const std::vector<Cue> legacyDefaults{
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

    inline Vector3 StageEuler(const Quaternion& q)
    {
        // Invert FromEulerDegreesXYZ (Rx * Ry * Rz).
        const auto right = q.Rotate({1, 0, 0});
        const auto up = q.Rotate({0, 1, 0});
        const auto forward = q.Rotate({0, 0, 1});
        const float cosY = std::hypot(forward.y, forward.z);
        constexpr float degrees = 57.295779513f;
        return {
            degrees * (cosY > 1e-6f ? std::atan2(-forward.y, forward.z) : std::atan2(up.z, up.y)),
            degrees * std::atan2(forward.x, cosY),
            degrees * (cosY > 1e-6f ? std::atan2(-up.x, right.x) : 0.f),
        };
    }

    inline void UpgradeLegacy(std::vector<Cue>& cues)
    {
        // Preserve authored v1 offsets. Convert the stock camera shot only when
        // both of its original clips still have their stock values.
        const auto stock = [&](int place, int first, int last) {
            return std::find_if(cues.begin(), cues.end(), [&](const Cue& c) {
                return c.prop == 11 && c.place == place && c.left == (place == 4) &&
                    c.first == first && c.last == last && c.position.x == 0 && c.position.y == 0 && c.position.z == 0 &&
                    c.rotation.x == 0 && c.rotation.y == 0 && c.rotation.z == 0;
            });
        };
        const auto grip = stock(1, 1400, 1690);
        const auto exit = stock(4, 1690, 1710);
        if (grip != cues.end() && exit != cues.end() && cues.size() < maxCues)
        {
            Cue hold = *grip;
            grip->last = 1484;
            hold.first = 1484;
            hold.anchor = Anchor::Camera;
            hold.position = {-0.42f, -0.45f, 0.86f};
            hold.rotation = {-90.f, 0, 0};
            *exit = hold;
            hold.last = 1665;
            exit->first = hold.last;
            exit->last = 1741;
            exit->anchor = Anchor::CameraToStage;
            exit->hideAfter = true;
            cues.push_back(hold);
        }
        for (auto& cue : cues)
        {
            if (cue.anchor != Anchor::Grip) continue;
            const auto prior = std::find_if(cues.begin(), cues.end(), [&](const Cue& c) {
                return c.prop == cue.prop && c.anchor == Anchor::Grip && c.place != cue.place &&
                    c.last <= cue.first && cue.first - c.last <= 2;
            });
            if (prior != cues.end())
            {
                cue.movement = Movement::Handoff;
                prior->last = cue.first;
            }
        }
    }

    inline const std::vector<Cue> meniDefaults = [] {
        auto cues = legacyDefaults;
        UpgradeLegacy(cues);
        auto& heldDog = cues.front();
        auto launch = heldDog;
        heldDog.last = 1305;
        launch.first = heldDog.last;
        launch.last = 1353;
        launch.movement = Movement::GripThrow;
        launch.hideAfter = true;
        cues.push_back(launch);
        return cues;
    }();

    inline float FrameAtTime(float seconds)
    {
        const float frame = seconds * 30.f;
        const float boundary = std::round(frame);
        return std::abs(frame - boundary) < 0.001f ? boundary : frame;
    }

    inline float ArcOffset(const Cue& cue, float progress)
    {
        return cue.movement == Movement::Throw ? 4.f * progress * (1.f - progress) * cue.arcHeight : 0.f;
    }

    inline float Progress(const Cue& cue, float frame)
    {
        const float length = static_cast<float>(cue.movement == Movement::Handoff ?
            std::min(cue.blendFrames, cue.last - cue.first) : cue.last - cue.first);
        const float t = std::clamp((frame - cue.first) / length, 0.f, 1.f);
        return cue.movement == Movement::Smooth || cue.movement == Movement::Handoff ? t*t*(3.f-2.f*t) : t;
    }

    inline bool Valid(const Cue& cue)
    {
        const auto within = [](const Vector3& value, float limit) {
            return std::abs(value.x) <= limit && std::abs(value.y) <= limit && std::abs(value.z) <= limit;
        };
        return cue.prop >= 1 && cue.prop <= 11 && cue.place >= 1 && cue.place <= 10 &&
            cue.first >= 0 && cue.first < cue.last && cue.last <= lastFrame &&
            cue.anchor >= Anchor::Grip && cue.anchor <= Anchor::CameraToStage &&
            (cue.anchor != Anchor::CameraToStage || cue.movement == Movement::Hold) &&
            cue.movement >= Movement::Hold && cue.movement <= Movement::SourceAttachment &&
            (cue.movement != Movement::SourceAttachment || (cue.anchor == Anchor::Grip && cue.bone == -1)) &&
            (cue.movement != Movement::Handoff || cue.anchor == Anchor::Grip) &&
            (cue.movement != Movement::GripThrow ||
                (cue.prop == 1 && cue.place == 1 && !cue.left && cue.bone == -1 && cue.anchor == Anchor::Grip)) &&
            cue.bone >= -1 && cue.bone <= 54 && cue.blendFrames >= 1 && cue.blendFrames <= lastFrame &&
            cue.arcHeight >= 0 && cue.arcHeight <= 20 &&
            within(cue.position, cue.anchor == Anchor::Grip ? 2.f : 200.f) &&
            within(cue.endPosition, cue.anchor == Anchor::Grip ? 2.f : 200.f) &&
            within(cue.rotation, 360) && within(cue.endRotation, 360);
    }

    inline bool Overlap(const Cue& a, const Cue& b)
    {
        return a.prop == b.prop && a.first < b.last && b.first < a.last;
    }

    inline bool ContainsFrame(const Cue& cue, float frame)
    {
        return frame >= cue.first && frame < cue.last;
    }

    inline std::optional<std::vector<Cue>> Read(std::istream& file)
    {
        file.imbue(std::locale::classic());
        std::string version;
        size_t count{};
        if (!(file >> version >> count) || (version != "MeniPropTimeline1" && version != "MeniPropTimeline2" && version != "MeniPropTimeline3") || count > maxCues) return std::nullopt;
        std::vector<Cue> cues;
        for (size_t index = 0; index < count; ++index)
        {
            Cue cue;
            if (!(file >> cue.prop >> cue.place >> cue.left >> cue.first >> cue.last
                >> cue.position.x >> cue.position.y >> cue.position.z >> cue.rotation.x >> cue.rotation.y >> cue.rotation.z))
                return std::nullopt;
            if (version != "MeniPropTimeline1")
            {
                int anchor{}, movement{};
                if (!(file >> anchor >> movement >> cue.bone >> cue.blendFrames
                    >> cue.endPosition.x >> cue.endPosition.y >> cue.endPosition.z
                    >> cue.endRotation.x >> cue.endRotation.y >> cue.endRotation.z >> cue.arcHeight >> cue.hideAfter))
                    return std::nullopt;
                cue.anchor = static_cast<Anchor>(anchor);
                cue.movement = static_cast<Movement>(movement);
            }
            if (!Valid(cue) || std::any_of(cues.begin(), cues.end(), [&](const Cue& other) { return Overlap(cue, other); }))
                return std::nullopt;
            cues.push_back(cue);
        }
        if (version == "MeniPropTimeline1") UpgradeLegacy(cues);
        return cues;
    }

    inline std::vector<Cue> Load(const std::filesystem::path& path)
    {
        std::ifstream file(path);
        if (auto cues = Read(file)) return std::move(*cues);
        return meniDefaults;
    }

    inline std::vector<Cue> LoadSourceTrial(const std::filesystem::path& path)
    {
        std::ifstream file(path);
        auto cues = Read(file);
        if (!cues || (file >> std::ws).peek() != std::char_traits<char>::eof() ||
            std::any_of(cues->begin(), cues->end(), [](const Cue& cue) {
                return cue.movement != Movement::SourceAttachment;
            })) return {};
        return std::move(*cues);
    }

    inline bool Save(const std::filesystem::path& path, const std::vector<Cue>& cues)
    {
        auto temp = path;
        temp += ".tmp";
        std::ofstream file(temp, std::ios::trunc);
        file.imbue(std::locale::classic());
        file << "MeniPropTimeline3 " << cues.size() << '\n';
        for (const auto& c : cues)
            file << c.prop << ' ' << c.place << ' ' << c.left << ' ' << c.first << ' ' << c.last << ' '
                << c.position.x << ' ' << c.position.y << ' ' << c.position.z << ' '
                << c.rotation.x << ' ' << c.rotation.y << ' ' << c.rotation.z << ' '
                << static_cast<int>(c.anchor) << ' ' << static_cast<int>(c.movement) << ' ' << c.bone << ' ' << c.blendFrames << ' '
                << c.endPosition.x << ' ' << c.endPosition.y << ' ' << c.endPosition.z << ' '
                << c.endRotation.x << ' ' << c.endRotation.y << ' ' << c.endRotation.z << ' '
                << c.arcHeight << ' ' << c.hideAfter << '\n';
        file.close();
        std::error_code error;
        if (file) std::filesystem::rename(temp, path, error);
        if (file && !error) return true;
        std::filesystem::remove(temp, error);
        return false;
    }
}
