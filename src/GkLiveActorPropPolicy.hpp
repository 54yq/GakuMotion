#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>
namespace GakuMotion::live_actor_props
{
    inline bool IsSupportedPair(const std::string& root, const std::string& prop) {
        if(root.size()<=5 || root.size()>120 || root.compare(0,5,"Root_")!=0 || prop!="Prop_"+root.substr(5))return false;
        std::string suffix=root.substr(5);
        std::transform(suffix.begin(),suffix.end(),suffix.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        const char* protectedNames[]{"body","face","hair","head","bone","skeleton","hand","foot","costume"};
        for(const char* protectedName:protectedNames) if(suffix.find(protectedName)!=std::string::npos)return false;
        return true;
    }

    inline std::string DisplayName(const std::string& prop) {
        if (prop == "Prop_Microphone") return "麦克风";
        if (prop == "Prop_Hknesuitcase") return "行李箱";
        if (!prop.starts_with("Prop_")) return prop;
        const auto suffix = prop.substr(5);
        std::string normalized;
        for (unsigned char c : suffix)
            if (std::isalnum(c)) normalized += static_cast<char>(std::tolower(c));
        const auto contains = [&](const char* word) { return normalized.find(word) != std::string::npos; };
        if (contains("penlight") || contains("lightstick") || contains("glowstick"))
            return "荧光棒（" + suffix + "）";
        if (contains("watergun") || contains("waterpistol") || contains("squirtgun"))
            return "水枪（" + suffix + "）";
        if (contains("candy") || contains("lollipop"))
            return "糖果（" + suffix + "）";
        return prop;
    }

    inline constexpr int MaxPropDepth = 32;

    struct PropMatch {
        void* root{};
        void* object{};
        std::string rootName, propName;
    };

    // Match the same bounded actor subtree and exact names in GakuActors and GakuMotion.
    template<class Children, class Name, class IsOtherActor>
    std::vector<PropMatch> ScanActorProps(void* actor, Children children, Name name, IsOtherActor isOtherActor) {
        constexpr size_t maxNodes = 4096;
        std::vector<PropMatch> matches;
        if (!actor) return matches;
        std::vector<std::pair<void*, int>> pending{{actor, 0}};
        size_t visited = 0;
        for (size_t i = 0; i < pending.size() && visited < maxNodes; ++i) {
            const auto [parent, depth] = pending[i];
            const auto nodes = children(parent);
            visited += nodes.size();
            std::vector<std::string> names;
            names.reserve(nodes.size());
            for (void* node : nodes) names.push_back(name(node));
            for (size_t j = 0; j < nodes.size(); ++j) {
                void* root = nodes[j];
                if (isOtherActor(root)) continue;
                const auto& rootName = names[j];
                if (rootName.starts_with("Root_")) {
                    auto propName = "Prop_" + rootName.substr(5);
                    if (IsSupportedPair(rootName, propName) && std::count(names.begin(), names.end(), rootName) == 1) {
                        void* found = nullptr;
                        int count = 0;
                        for (void* prop : children(root)) {
                            if (name(prop) == propName) { found = prop; ++count; }
                        }
                        if (count == 1 && !isOtherActor(found))
                            matches.push_back({root, found, rootName, std::move(propName)});
                    }
                }
                if (depth + 1 < MaxPropDepth && pending.size() < maxNodes)
                    pending.emplace_back(root, depth + 1);
            }
        }
        return matches;
    }
}
