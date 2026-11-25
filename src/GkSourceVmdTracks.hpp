#pragma once
#include "GkMotionData.hpp"
#include <array>
#include <span>

namespace GakuMotion::source_vmd
{
    struct BoneKey
    {
        uint32_t frame;
        Vector3 position;
        Quaternion rotation;
        std::array<uint8_t, 16> interpolation;
    };

    inline float Interpolate(float progress, const std::array<uint8_t, 16>& values, size_t channel)
    {
        const auto cubic = [](float t, uint8_t a, uint8_t b) {
            const float u = 1.f - t;
            return (3.f*u*u*t*a + 3.f*u*t*t*b) / 127.f + t*t*t;
        };
        float lower = 0.f, upper = 1.f;
        for (int i = 0; i < 18; ++i)
        {
            const float t = (lower + upper) * 0.5f;
            if (cubic(t, values[channel], values[channel+8]) < progress) lower = t;
            else upper = t;
        }
        return cubic((lower+upper)*0.5f, values[channel+4], values[channel+12]);
    }

    inline BoneKey Sample(std::span<const BoneKey> keys, float frame)
    {
        const auto next = std::upper_bound(keys.begin(), keys.end(), frame,
            [](float t, const BoneKey& key) { return t < key.frame; });
        if (next == keys.begin()) return keys.front();
        const auto& previous = *(next - 1);
        if (next == keys.end() || frame == previous.frame) return previous;
        const float t = (frame - previous.frame) / (next->frame - previous.frame);
        const auto& curve = next->interpolation;
        return {previous.frame,
            {std::lerp(previous.position.x, next->position.x, Interpolate(t, curve, 0)),
             std::lerp(previous.position.y, next->position.y, Interpolate(t, curve, 1)),
             std::lerp(previous.position.z, next->position.z, Interpolate(t, curve, 2))},
            Quaternion::Slerp(previous.rotation, next->rotation, Interpolate(t, curve, 3)), {}};
    }

    // Read these named tracks from the imported original, without adding humanoid bones.
    inline std::array<std::vector<BoneKey>, 2> LoadHandAttachments(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        char header[50]{};
        uint32_t count{};
        if (!input.read(header, sizeof(header)) ||
            std::memcmp(header, "Vocaloid Motion Data 0002", 25) != 0 ||
            !input.read(reinterpret_cast<char*>(&count), sizeof(count))) return {};
        constexpr size_t recordSize = 111;
        if (count > 2'000'000) return {};
        constexpr char left[] = "\x8d\xb6\x83\x5f\x83\x7e\x81\x5b";
        constexpr char right[] = "\x89\x45\x83\x5f\x83\x7e\x81\x5b";
        std::array<std::vector<BoneKey>, 2> tracks;
        std::array<char, recordSize> record;
        for (uint32_t i = 0; i < count; ++i)
        {
            if (!input.read(record.data(), record.size())) return {};
            if (record[8] != 0) continue;
            const bool isLeft = std::memcmp(record.data(), left, 8) == 0;
            if (!isLeft && std::memcmp(record.data(), right, 8) != 0) continue;
            BoneKey key{};
            std::memcpy(&key.frame, record.data() + 15, 4);
            std::memcpy(&key.position, record.data() + 19, 12);
            std::memcpy(&key.rotation, record.data() + 31, 16);
            std::memcpy(key.interpolation.data(), record.data() + 47, 16);
            const auto& p = key.position;
            const auto& q = key.rotation;
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
                !std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w) ||
                q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w < 1e-8f ||
                std::any_of(key.interpolation.begin(), key.interpolation.end(), [](uint8_t c) { return c > 127; })) return {};
            key.rotation = q.Normalized();
            tracks[isLeft ? 0 : 1].push_back(key);
        }
        for (auto& track : tracks)
            std::stable_sort(track.begin(), track.end(), [](const BoneKey& a, const BoneKey& b) { return a.frame < b.frame; });
        return tracks;
    }
}
