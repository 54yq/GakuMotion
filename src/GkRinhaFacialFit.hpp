#pragma once
// Client side of the rinha.facial-fit service: process-lifetime read-only access
// to the provider's face table. A host without RinhaReplacement (or with a
// disabled/missing table) leaves `available` false and every caller falls back
// to the compiled tables.
//
// Why the lookup is retried instead of done once at activation:
//   GakuLoader activates plugins in sorted filename order (see
//   PluginHost.cpp ActivatePlugins), and "GakuMotion.dll" sorts before
//   "RinhaReplacement.dll". A single query during activation therefore *always*
//   runs before the provider has registered, and the fit would silently never
//   apply on a real install. The host handle stays valid for process lifetime,
//   so re-asking later is safe; `Ensure()` throttles the retry and bumps a
//   generation counter that forces a binding rebuild once the provider appears.
#include "GakuPlugin.h"
#include "RinhaFacialFit.h"
#include <chrono>
#include <string>

namespace GakuMotion::facial_fit
{
    inline RinhaFacialFitService service{};
    inline bool available{false};
    // Bumped on every successful lookup; bindings compare their bound value so
    // a provider that appears mid-session rebinds without a clip change.
    inline unsigned generation{0};

    inline GakuHostApi host{};
    inline std::chrono::steady_clock::time_point nextAttempt{};
    inline constexpr std::chrono::milliseconds kRetryInterval{500};

    // Face prefab identity may carry Unity's clone suffix; the provider only
    // matches the authored name.
    inline std::string StripCloneSuffix(std::string name)
    {
        const std::string clone = "(Clone)";
        if (name.size() >= clone.size() && name.compare(name.size() - clone.size(), clone.size(), clone) == 0)
        {
            name.resize(name.size() - clone.size());
            if (!name.empty() && name.back() == ' ') name.pop_back();
        }
        return name;
    }

    inline bool Lookup()
    {
        service = {};
        available = false;
        if (!host.queryService) return false;
        RinhaFacialFitService candidate{};
        if (host.queryService(host.hostContext, RINHA_FACIAL_FIT_SERVICE, GAKU_SERVICE_VERSION,
            sizeof(candidate), &candidate) != GAKU_OK) return false;
        if (candidate.abiVersion != GAKU_SERVICE_VERSION || candidate.structSize < sizeof(candidate)) return false;
        if (!candidate.faceCount || !candidate.faceAt || !candidate.findFace ||
            !candidate.evaluateMouth || !candidate.evaluateEyes) return false;
        if (candidate.faceCount(candidate.context) == 0) return false;
        service = candidate;
        available = true;
        ++generation;
        return true;
    }

    inline void Connect(const GakuHostApi& api)
    {
        host = api;
        nextAttempt = {};
        Lookup();
    }

    // One branch in the steady state; a throttled re-query until it succeeds.
    inline void Ensure()
    {
        if (available || !host.queryService) return;
        const auto now = std::chrono::steady_clock::now();
        if (now < nextAttempt) return;
        nextAttempt = now + kRetryInterval;
        Lookup();
    }

    // Exact identity lookup; returns nullptr when this face has no fitted table.
    inline const RinhaFacialFitFace* Find(const std::string& faceName)
    {
        Ensure();
        if (!available || !service.findFace) return nullptr;
        return service.findFace(service.context, StripCloneSuffix(faceName).c_str());
    }
}
