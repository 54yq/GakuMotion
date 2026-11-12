#pragma once
// Client side of the rinha.facial-fit service: one optional query at connect
// time, then process-lifetime read-only access to the provider's face table.
// A host without RinhaReplacement (or with a disabled/missing table) simply
// leaves `available` false and every caller falls back to the compiled tables.
#include "GakuPlugin.h"
#include "RinhaFacialFit.h"
#include <string>

namespace GakuMotion::facial_fit
{
    inline RinhaFacialFitService service{};
    inline bool available{false};

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

    inline void Connect(const GakuHostApi& api)
    {
        service = {};
        available = false;
        if (!api.queryService) return;
        RinhaFacialFitService candidate{};
        if (api.queryService(api.hostContext, RINHA_FACIAL_FIT_SERVICE, GAKU_SERVICE_VERSION,
            sizeof(candidate), &candidate) != GAKU_OK) return;
        if (candidate.abiVersion != GAKU_SERVICE_VERSION || candidate.structSize < sizeof(candidate)) return;
        if (!candidate.faceCount || !candidate.faceAt || !candidate.findFace ||
            !candidate.evaluateMouth || !candidate.evaluateEyes) return;
        if (candidate.faceCount(candidate.context) == 0) return;
        service = candidate;
        available = true;
    }

    // Exact identity lookup; returns nullptr when this face has no fitted table.
    inline const RinhaFacialFitFace* Find(const std::string& faceName)
    {
        if (!available || !service.findFace) return nullptr;
        return service.findFace(service.context, StripCloneSuffix(faceName).c_str());
    }
}
