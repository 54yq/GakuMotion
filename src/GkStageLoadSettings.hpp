#pragma once

#include "GkStageLoadPolicy.hpp"
#include "GkJsonLite.hpp"
#include "GkMotionData.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace GakuMotion::stage_load
{
    // Persist only desired native-load selection. Receipts and live scene
    // identities belong to this process and must never survive a restart.
    struct Settings
    {
        std::string key;

        bool Load(const std::string& path, std::string& error)
        {
            jsonlite::Value root;
            if (!jsonlite::ParseFile(path, root, error)) return false;
            const auto* version = root.Find("version");
            const auto* target = root.Find("load_replacement_key");
            if (!root.IsObject() || !version || !version->IsNumber() || version->numberValue != 1 ||
                !target || !target->IsString() || (!target->stringValue.empty() && !IsTarget(target->stringValue)))
            {
                error = "Invalid photography stage settings (version/load_replacement_key)";
                return false;
            }
            key = target->stringValue;
            error.clear();
            return true;
        }

        bool Save(const std::string& path, std::string& error) const
        {
            error.clear();
            if (path.empty() || (!key.empty() && !IsTarget(key)))
            { error = "Invalid photography stage settings"; return false; }
            const std::filesystem::path destination(MotionPathFromUtf8(path));
            std::error_code ec;
            if (!destination.parent_path().empty()) std::filesystem::create_directories(destination.parent_path(), ec);
            if (ec) { error = ec.message(); return false; }
            const std::string bytes = "{\n  \"version\": 1,\n  \"load_replacement_key\": \"" + key + "\"\n}\n";
            static std::atomic<uint64_t> sequence{0};
            std::filesystem::path temp = destination;
            temp += "." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "." +
                std::to_string(++sequence) + ".tmp";
            auto cleanup = [&] { std::error_code ignored; std::filesystem::remove(temp, ignored); };
#ifdef _WIN32
            HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE)
            { error = std::error_code(GetLastError(), std::system_category()).message(); return false; }
            DWORD written = 0;
            bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size();
            if (ok) ok = FlushFileBuffers(file) != 0;
            if (!ok) error = std::error_code(GetLastError(), std::system_category()).message();
            if (!CloseHandle(file) && ok)
            { error = std::error_code(GetLastError(), std::system_category()).message(); ok = false; }
            if (!ok) { cleanup(); return false; }
#else
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out << bytes;
            out.close();
            if (!out) { error = "Cannot write photography stage settings"; cleanup(); return false; }
#endif
            // Keep the previous bytes recoverable. Never remove the current
            // destination first: a locked/failed replacement leaves it intact.
            const bool exists = std::filesystem::exists(destination, ec);
            if (ec) { error = ec.message(); cleanup(); return false; }
            if (exists)
            {
                auto backup = destination;
                backup += ".bak";
                std::filesystem::copy_file(destination, backup, std::filesystem::copy_options::overwrite_existing, ec);
                if (ec) { error = ec.message(); cleanup(); return false; }
            }
#ifdef _WIN32
            if (!MoveFileExW(temp.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            { error = std::error_code(GetLastError(), std::system_category()).message(); cleanup(); return false; }
#else
            std::filesystem::rename(temp, destination, ec);
            if (ec) { error = ec.message(); cleanup(); return false; }
#endif
            return true;
        }
    };
}
