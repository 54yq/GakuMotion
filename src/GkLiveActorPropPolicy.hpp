#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>
namespace GakuMotion::live_actor_props
{
    inline bool IsSupportedPair(const std::string& root, const std::string& prop) {
        if(root.size()<=5 || root.size()>120 || !root.starts_with("Root_") || !prop.starts_with("Prop_"))return false;
        std::string suffix=root.substr(5);
        if (suffix.ends_with("(Clone)")) suffix.resize(suffix.size() - 7);
        if (suffix.empty() || (prop != "Prop_" + suffix && prop != "Prop_" + suffix + "(Clone)")) return false;
        std::transform(suffix.begin(),suffix.end(),suffix.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        const char* protectedNames[]{"body","face","hair","head","bone","skeleton","hand","foot","costume"};
        for(const char* protectedName:protectedNames) if(suffix.find(protectedName)!=std::string::npos)return false;
        return true;
    }

    inline const char* KnownPropLabel(const std::string& suffix) {
        std::string normalized;
        for (unsigned char c : suffix)
            if (std::isalnum(c)) normalized += static_cast<char>(std::tolower(c));
        const auto contains = [&](const char* word) { return normalized.find(word) != std::string::npos; };
        if (normalized == "microphone") return "麦克风";
        if (normalized == "hknesuitcase") return "行李箱";
        if (contains("penlight") || contains("lightstick") || contains("glowstick"))
            return "荧光棒";
        if (contains("watergun") || contains("waterpistol") || contains("squirtgun"))
            return "水枪";
        if (contains("candy") || contains("lollipop"))
            return "糖果";
        return nullptr;
    }

    inline std::string PropSuffix(const std::string& name) {
        if (!name.starts_with("Prop_") || name.size() > 120) return {};
        auto suffix = name.substr(5);
        if (suffix.ends_with("(Clone)")) suffix.resize(suffix.size() - 7);
        return suffix;
    }

    inline bool IsRecognizedProp(const std::string& prop) {
        const auto suffix = PropSuffix(prop);
        return IsSupportedPair("Root_" + suffix, "Prop_" + suffix) && KnownPropLabel(suffix);
    }

    inline std::string DisplayName(const std::string& prop) {
        if (prop == "Prop_Microphone") return "麦克风";
        if (prop == "Prop_Hknesuitcase") return "行李箱";
        const auto suffix = PropSuffix(prop);
        if (const auto label = KnownPropLabel(suffix))
            return std::string(label) + "（" + suffix + "）";
        return prop;
    }

    inline constexpr int MaxPropDepth = 32;

    struct PropMatch {
        void* root{};
        void* object{};
        std::string rootName, propName;
    };

    // Exact pairs can identify new prop types. Known props can also be mounted
    // directly below a bone or a model wrapper.
    template<class Children, class Name, class IsOtherActor>
    std::vector<PropMatch> ScanActorProps(void* actor, Children children, Name name, IsOtherActor isOtherActor) {
        constexpr size_t maxNodes = 4096;
        std::vector<PropMatch> matches;
        if (!actor) return matches;
        struct Node { void* object; int depth; std::string rootName; bool uniqueRoot; };
        std::vector<Node> pending{{actor, 0, {}, false}};
        size_t discovered = 1;
        for (size_t i = 0; i < pending.size() && discovered < maxNodes; ++i) {
            const auto [parent, depth, parentName, uniqueRoot] = pending[i];
            const auto nodes = children(parent);
            if (nodes.size() > maxNodes - discovered) break;
            discovered += nodes.size();
            std::vector<std::string> names;
            names.reserve(nodes.size());
            for (void* node : nodes) names.push_back(name(node));
            for (size_t j = 0; j < nodes.size(); ++j) {
                void* object = nodes[j];
                if (isOtherActor(object)) continue;
                auto& objectName = names[j];
                if (objectName.starts_with("Prop_") && std::count(names.begin(), names.end(), objectName) == 1) {
                    const bool paired = IsSupportedPair(parentName, objectName);
                    if (uniqueRoot && paired)
                        matches.push_back({parent, object, parentName, objectName});
                    else if (!paired && IsRecognizedProp(objectName))
                        matches.push_back({parent, object, {}, objectName});
                }
                if (depth + 1 < MaxPropDepth) {
                    const bool root = objectName.starts_with("Root_");
                    pending.push_back({object, depth + 1, root ? objectName : std::string{},
                        root && std::count(names.begin(), names.end(), objectName) == 1});
                }
            }
        }
        return matches;
    }
}
