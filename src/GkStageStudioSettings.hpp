#pragma once

#include "GkJsonLite.hpp"
#include "GkMotionData.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <locale>
#include <set>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace GakuMotion::studio
{
    struct Lighting
    {
        bool enabled{false}, actorStyle{true}, decalEnabled{false};
        std::array<float, 3> keyColor{1, .9f, .8f}, fillColor{.65f, .78f, 1}, rimColor{.6f, .7f, 1}, decalTint{1, 1, 1};
        float keyIntensity{1.4f}, fillIntensity{.5f}, rimIntensity{1};
        float keyYaw{-30}, keyPitch{35}, fillYaw{40}, fillPitch{20}, rimYaw{170}, rimPitch{40};
        float actorIntensity{1}, rimPower{.5f}, decalIntensityScale{1};
        bool operator==(const Lighting&) const = default;
    };

    struct Video
    {
        bool enabled{false}, paused{false}, loop{true};
        std::string path;
        float distance{1.5f}, width{1.2f}, offsetX{0}, offsetY{0};
        bool operator==(const Video&) const = default;
    };

    struct Settings
    {
        Lighting lighting;
        Video video;
        bool operator==(const Settings&) const = default;

        bool Validate(std::string& error) const
        {
            error.clear();
            const auto range = [&](float value, float low, float high, const char* name) {
                if (std::isfinite(value) && value >= low && value <= high) return true;
                error = std::string("Invalid studio setting: ") + name;
                return false;
            };
            for (const auto* color : {&lighting.keyColor, &lighting.fillColor, &lighting.rimColor, &lighting.decalTint})
                for (float value : *color) if (!range(value, 0, 1, "RGB must be finite and in [0,1]")) return false;
            if (!range(lighting.keyIntensity, 0, 8, "keyIntensity") ||
                !range(lighting.fillIntensity, 0, 8, "fillIntensity") ||
                !range(lighting.rimIntensity, 0, 8, "rimIntensity") ||
                !range(lighting.actorIntensity, 0, 4, "actorIntensity") ||
                !range(lighting.rimPower, 0, 8, "rimPower") ||
                !range(lighting.decalIntensityScale, 0, 10, "decalIntensityScale")) return false;
            for (float value : {lighting.keyYaw, lighting.fillYaw, lighting.rimYaw})
                if (!range(value, -180, 180, "light yaw")) return false;
            for (float value : {lighting.keyPitch, lighting.fillPitch, lighting.rimPitch})
                if (!range(value, -90, 90, "light pitch")) return false;
            if (!range(video.distance, .25f, 100, "video distance") ||
                !range(video.width, .1f, 100, "video width") ||
                !range(video.offsetX, -50, 50, "video offsetX") ||
                !range(video.offsetY, -50, 50, "video offsetY")) return false;
            if (video.path.size() > 32767 || video.path.find('\0') != std::string::npos ||
                video.path.find("://") != std::string::npos || video.path.starts_with("\\\\") ||
                video.path.starts_with("//"))
            { error = "Video input must be a local file path"; return false; }
            if (video.enabled && video.path.empty())
            { error = "Choose a local video file before enabling video"; return false; }
            try
            {
                if (!video.path.empty() && !MotionPathFromUtf8(video.path).is_absolute())
                { error = "Video input must use an absolute local file path"; return false; }
            }
            catch (const std::exception&)
            { error = "Video input path encoding is invalid"; return false; }
            return true;
        }

        bool Load(const std::string& path, std::string& error)
        {
            jsonlite::Value root;
            if (!jsonlite::ParseFile(path, root, error)) return false;
            const auto known = [&](const jsonlite::Value& object, std::initializer_list<const char*> names) {
                if (!object.IsObject()) { error = "Studio settings must contain JSON objects"; return false; }
                std::set<std::string> seen;
                for (const auto& [name, value] : object.objectMembers)
                {
                    bool allowed = false;
                    for (const char* field : names) if (name == field) allowed = true;
                    if (!allowed || !seen.insert(name).second)
                    { error = "Unknown or duplicate studio setting: " + name; return false; }
                }
                return true;
            };
            if (!known(root, {"version", "lighting", "video"})) return false;
            const auto* version = root.Find("version");
            if (!version || !version->IsNumber() || version->numberValue != 1)
            { error = "Unsupported studio settings version"; return false; }
            Settings candidate;
            const auto boolean = [&](const jsonlite::Value& object, const char* name, bool& target) {
                const auto* value = object.Find(name);
                if (!value) return true;
                if (!value->IsBool()) { error = std::string("Expected boolean studio setting: ") + name; return false; }
                target = value->boolValue; return true;
            };
            const auto number = [&](const jsonlite::Value& object, const char* name, float& target) {
                const auto* value = object.Find(name);
                if (!value) return true;
                if (!value->IsNumber() || !std::isfinite(value->numberValue))
                { error = std::string("Expected finite numeric studio setting: ") + name; return false; }
                target = static_cast<float>(value->numberValue); return true;
            };
            const auto color = [&](const jsonlite::Value& object, const char* name, std::array<float, 3>& target) {
                const auto* value = object.Find(name);
                if (!value) return true;
                if (!value->IsArray() || value->Size() != 3)
                { error = std::string("Expected RGB studio setting: ") + name; return false; }
                for (size_t i = 0; i < 3; ++i)
                {
                    const auto* component = value->At(i);
                    if (!component->IsNumber() || !std::isfinite(component->numberValue))
                    { error = std::string("Expected finite RGB studio setting: ") + name; return false; }
                    target[i] = static_cast<float>(component->numberValue);
                }
                return true;
            };
            if (const auto* object = root.Find("lighting"))
            {
                if (!known(*object, {"enabled", "actorStyle", "decalEnabled", "keyColor", "fillColor", "rimColor", "decalTint",
                    "keyIntensity", "fillIntensity", "rimIntensity", "keyYaw", "keyPitch", "fillYaw", "fillPitch", "rimYaw", "rimPitch",
                    "actorIntensity", "rimPower", "decalIntensityScale"})) return false;
                auto& l = candidate.lighting;
                if (!boolean(*object, "enabled", l.enabled) || !boolean(*object, "actorStyle", l.actorStyle) ||
                    !boolean(*object, "decalEnabled", l.decalEnabled) || !color(*object, "keyColor", l.keyColor) ||
                    !color(*object, "fillColor", l.fillColor) || !color(*object, "rimColor", l.rimColor) ||
                    !color(*object, "decalTint", l.decalTint)) return false;
                for (auto [name, value] : {std::pair{"keyIntensity", &l.keyIntensity}, {"fillIntensity", &l.fillIntensity},
                    {"rimIntensity", &l.rimIntensity}, {"keyYaw", &l.keyYaw}, {"keyPitch", &l.keyPitch},
                    {"fillYaw", &l.fillYaw}, {"fillPitch", &l.fillPitch}, {"rimYaw", &l.rimYaw}, {"rimPitch", &l.rimPitch},
                    {"actorIntensity", &l.actorIntensity}, {"rimPower", &l.rimPower}, {"decalIntensityScale", &l.decalIntensityScale}})
                    if (!number(*object, name, *value)) return false;
            }
            if (const auto* object = root.Find("video"))
            {
                if (!known(*object, {"enabled", "paused", "loop", "path", "distance", "width", "offsetX", "offsetY"})) return false;
                auto& v = candidate.video;
                if (!boolean(*object, "enabled", v.enabled) || !boolean(*object, "paused", v.paused) || !boolean(*object, "loop", v.loop)) return false;
                if (const auto* file = object->Find("path"))
                {
                    if (!file->IsString()) { error = "Video path must be a JSON string"; return false; }
                    v.path = file->stringValue;
                }
                if (!number(*object, "distance", v.distance) || !number(*object, "width", v.width) ||
                    !number(*object, "offsetX", v.offsetX) || !number(*object, "offsetY", v.offsetY)) return false;
            }
            if (!candidate.Validate(error)) return false;
            *this = std::move(candidate);
            return true;
        }

        bool Save(const std::string& path, std::string& error) const
        {
            if (!Validate(error)) return false;
            if (path.empty()) { error = "Studio settings path is unavailable"; return false; }
            std::ostringstream out;
            out.imbue(std::locale::classic());
            out << std::setprecision(9) << std::boolalpha << "{\n  \"version\": 1,\n  \"lighting\": {\n";
            const auto& l = lighting;
            out << "    \"enabled\": " << l.enabled << ",\n    \"actorStyle\": " << l.actorStyle << ",\n    \"decalEnabled\": " << l.decalEnabled;
            for (const auto& [name, value] : {std::pair{"keyColor", &l.keyColor}, {"fillColor", &l.fillColor}, {"rimColor", &l.rimColor}, {"decalTint", &l.decalTint}})
                out << ",\n    \"" << name << "\": [" << (*value)[0] << ", " << (*value)[1] << ", " << (*value)[2] << ']';
            for (const auto [name, value] : {std::pair{"keyIntensity", l.keyIntensity}, {"fillIntensity", l.fillIntensity},
                {"rimIntensity", l.rimIntensity}, {"keyYaw", l.keyYaw}, {"keyPitch", l.keyPitch}, {"fillYaw", l.fillYaw},
                {"fillPitch", l.fillPitch}, {"rimYaw", l.rimYaw}, {"rimPitch", l.rimPitch}, {"actorIntensity", l.actorIntensity},
                {"rimPower", l.rimPower}, {"decalIntensityScale", l.decalIntensityScale}})
                out << ",\n    \"" << name << "\": " << value;
            out << "\n  },\n  \"video\": {\n    \"enabled\": " << video.enabled << ",\n    \"paused\": " << video.paused <<
                ",\n    \"loop\": " << video.loop << ",\n    \"path\": \"" << jsonlite::EscapeString(video.path) << "\"" <<
                ",\n    \"distance\": " << video.distance << ",\n    \"width\": " << video.width << ",\n    \"offsetX\": " << video.offsetX <<
                ",\n    \"offsetY\": " << video.offsetY << "\n  }\n}\n";
            const auto bytes = out.str();
            const auto destination = MotionPathFromUtf8(path);
            std::error_code ec;
            if (!destination.parent_path().empty()) std::filesystem::create_directories(destination.parent_path(), ec);
            if (ec) { error = ec.message(); return false; }
            static std::atomic<uint64_t> sequence{0};
            auto temporary = destination;
            temporary += "." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "." + std::to_string(++sequence) + ".tmp";
            const auto cleanup = [&] { std::error_code ignored; std::filesystem::remove(temporary, ignored); };
#ifdef _WIN32
            HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) { error = std::error_code(GetLastError(), std::system_category()).message(); return false; }
            DWORD written = 0;
            bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size();
            if (ok) ok = FlushFileBuffers(file) != 0;
            if (!ok) error = std::error_code(GetLastError(), std::system_category()).message();
            if (!CloseHandle(file) && ok) { ok = false; error = std::error_code(GetLastError(), std::system_category()).message(); }
            if (!ok) { cleanup(); return false; }
#else
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            file << bytes;
            file.close();
            if (!file) { error = "Cannot write studio settings"; cleanup(); return false; }
#endif
            const bool exists = std::filesystem::exists(destination, ec);
            if (ec) { error = ec.message(); cleanup(); return false; }
            if (exists)
            {
                auto backup = destination; backup += ".bak";
                std::filesystem::copy_file(destination, backup, std::filesystem::copy_options::overwrite_existing, ec);
                if (ec) { error = ec.message(); cleanup(); return false; }
            }
#ifdef _WIN32
            if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            { error = std::error_code(GetLastError(), std::system_category()).message(); cleanup(); return false; }
#else
            std::filesystem::rename(temporary, destination, ec);
            if (ec) { error = ec.message(); cleanup(); return false; }
#endif
            error.clear();
            return true;
        }
    };
}
