#pragma once

#include "GkInjectionPolicy.hpp"
#include <string>
#include <string_view>

namespace GakuMotion::stage_load
{
    inline bool IsTarget(std::string_view key) noexcept
    {
        return key == "env_3d_live_all001-00-noon" ||
            key == "env_3d_prod_hifstage-00-00-noon" ||
            key == "env_3d_live_courtyard-00-00-noon";
    }

    // A rewritten resource-name request is not proof of a completed load.
    // Admission additionally requires the exact live owner and Active Scene.
    // No managed object or asynchronous operation is retained here.
    struct Receipt
    {
        injection::OwnerSnapshot owner;
        std::string target;

        void Clear() { owner = {}; target.clear(); }
        bool Record(const injection::OwnerSnapshot& current, std::string_view key)
        {
            if (current.AllowedContext() != injection::Context::Photography || !IsTarget(key)) return false;
            owner = current;
            target = key;
            return true;
        }
        void Observe(const injection::OwnerSnapshot& current, bool ownerUnloaded = false)
        {
            if (ownerUnloaded || (current.Valid() && !owner.SameOwner(current))) Clear();
        }
        injection::Context Context(std::string_view activeScene, std::string_view presenterScene,
            const injection::OwnerSnapshot& current) const noexcept
        {
            const auto native = injection::PreviewContext(activeScene, presenterScene, current);
            if (native != injection::Context::Unknown) return native;
            return !target.empty() && IsTarget(target) && activeScene == target &&
                presenterScene == "Photography" && owner.SameOwner(current) &&
                current.role == injection::OwnerRole::Photography
                ? injection::Context::Photography : injection::Context::Unknown;
        }
    };

    // Unity-thread only; GUI selection uses StageManager's mutex separately.
    inline Receipt receipt;
}
