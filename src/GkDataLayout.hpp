#pragma once
#include "GkMotionData.hpp"
#include "GkJsonLite.hpp"
#include <cwctype>

namespace GakuMotion {
// A receipt selects one layout for the entire process. No runtime copy or
// old-directory fallback is performed after a managed layout is selected.
struct DataLayout {
    std::string localRoot, configs, userData, pluginData, logs, temp;
    bool managed{false};
    bool libraryInPluginData{false};

    static DataLayout Legacy(const std::string& root) {
        return {root, root + "/GakuMotion", root, root + "/GakuMotion",
                root + "/GakuMotion", "", false};
    }
    std::string Motions() const { return (libraryInPluginData ? pluginData : userData) + "/motions"; }
    std::string Playlist() const { return pluginData + "/playlist.json"; }

    static bool SamePath(const std::string& left, const std::string& right) {
        if (left.empty() || right.empty()) return false;
        auto a = MotionPathFromUtf8(left).lexically_normal().make_preferred();
        auto b = MotionPathFromUtf8(right).lexically_normal().make_preferred();
#ifdef _WIN32
        auto aw = a.wstring(), bw = b.wstring();
        for (auto& c : aw) c = static_cast<wchar_t>(std::towlower(c));
        for (auto& c : bw) c = static_cast<wchar_t>(std::towlower(c));
        while (aw.size() > 3 && (aw.back() == L'/' || aw.back() == L'\\')) aw.pop_back();
        while (bw.size() > 3 && (bw.back() == L'/' || bw.back() == L'\\')) bw.pop_back();
        return aw == bw;
#else
        return a == b;
#endif
    }

    std::string ResolveOverride(const std::string& candidate, const std::string& fallback) const {
        if (candidate.empty()) return fallback;
        const auto p = MotionPathFromUtf8(candidate);
        const auto anchored = p.is_absolute() ? p : MotionPathFromUtf8(localRoot) / p;
        if (managed) {
            if (SamePath(MotionPathToUtf8(anchored), localRoot + "/GakuMotion/bone_overrides.json"))
                return configs + "/bone_overrides.json";
            // Existing relative references into the old motion library follow
            // the copied complete library; arbitrary external paths retain their
            // original anchor and are never scanned or copied by this resolver.
            if (!p.is_absolute()) {
                const auto normalized = p.lexically_normal();
                auto begin = normalized.begin();
                if (begin != normalized.end() && SamePath(MotionPathToUtf8(*begin), "motions")) {
                    auto tail = std::filesystem::path();
                    for (++begin; begin != normalized.end(); ++begin) tail /= *begin;
                    return MotionPathToUtf8(MotionPathFromUtf8(Motions()) / tail);
                }
            }
        }
        return MotionPathToUtf8(anchored.lexically_normal());
    }
};

inline bool SelectDataLayout(const std::string& localRoot, const DataLayout* offered,
                             DataLayout& selected, std::string& error) {
    error.clear();
    const auto receipt = MotionPathFromUtf8(localRoot + "/PluginData/gakumotion/layout-v1.json");
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(receipt, ec);
    if (ec == std::errc::no_such_file_or_directory || (!ec && !std::filesystem::exists(status))) {
        // The independent host root is a new install contract, not an old
        // Localify tree awaiting migration. Never mix it with legacy files.
        auto rootPath = MotionPathFromUtf8(localRoot).lexically_normal();
        if (rootPath.filename().empty()) rootPath = rootPath.parent_path();
        if (DataLayout::SamePath(MotionPathToUtf8(rootPath.filename()), "GakuLoader")) {
            if (!offered || !rootPath.is_absolute()) {
                error = "Independent GakuLoader root requires the paths service";
                return false;
            }
            for (const auto* path : {&offered->configs, &offered->userData,
                    &offered->pluginData, &offered->logs, &offered->temp}) {
                if (!MotionPathFromUtf8(*path).is_absolute()) {
                    error = "Independent GakuLoader category is not absolute";
                    return false;
                }
            }
            selected = *offered;
            selected.localRoot = localRoot;
            selected.managed = true;
            selected.libraryInPluginData = true;
        } else selected = DataLayout::Legacy(localRoot);
        return true;
    }
    auto fail = [&](const std::string& reason) {
        error = "Motion layout receipt rejected: " + reason +
            ". Run tools/migrate-layout.ps1 -Mode Plan for this installation; old data was not selected.";
        return false;
    };
    if (ec || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status))
        return fail("receipt is unreadable or is not a regular file");
    if (!offered) return fail("active layout requires gakuloader.paths v1");
    jsonlite::Value root;
    std::string parseError;
    if (!jsonlite::ParseFile(MotionPathToUtf8(receipt), root, parseError)) return fail(parseError);
    if (root.IntPath("schemaVersion", 0) != 1 || root.StringPath("pluginId", "") != "gakumotion" ||
        root.StringPath("layout", "") != "gakuloader-v1" || root.StringPath("state", "") != "active" ||
        !DataLayout::SamePath(root.StringPath("sourceRoot", ""), localRoot))
        return fail("schema/plugin/state/installation mismatch");
    const std::pair<const char*, const std::string*> fields[] = {
        {"paths.configs", &offered->configs}, {"paths.userData", &offered->userData},
        {"paths.pluginData", &offered->pluginData}, {"paths.logs", &offered->logs}};
    for (const auto& field : fields)
        if (!MotionPathFromUtf8(*field.second).is_absolute() ||
            !DataLayout::SamePath(root.StringPath(field.first, ""), *field.second))
            return fail(std::string("path mismatch: ") + field.first);
    if (!MotionPathFromUtf8(offered->temp).is_absolute()) return fail("temporary root is not absolute");
    selected = *offered;
    selected.localRoot = localRoot;
    selected.managed = true;
    return true;
}
}
