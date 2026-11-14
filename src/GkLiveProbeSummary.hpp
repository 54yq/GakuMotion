#pragma once
#include "GkJsonLite.hpp"
#include <cstdint>
#include <cmath>
#include <algorithm>
namespace GakuMotion
{
    struct LiveProbeSummary { bool valid{}; uint64_t swingCalls{}; float minDt{}, maxDt{}; };
    inline LiveProbeSummary ParseLiveProbeSummary(const jsonlite::Value& root)
    {
        LiveProbeSummary out;
        const auto* profile = root.Find("profile");
        const auto* traces = root.Find("traces");
        if (!profile || profile->AsString("") != "gakumas-burst-20261008-windows-x64" || !traces || !traces->IsArray()) return out;
        for (const auto& trace : traces->arrayItems)
        {
            const auto* caller = trace.Find("caller_rva");
            if (!caller) continue;
            const auto rva = caller->AsString("");
            if (rva != "0xcc9c8" && rva != "0x281d4") continue;
            const auto* calls = trace.Find("calls"); const auto* lo = trace.Find("delta_min"); const auto* hi = trace.Find("delta_max");
            if (!calls || !lo || !hi || !calls->IsNumber() || !lo->IsNumber() || !hi->IsNumber()) return {};
            const double n = calls->AsDouble(-1), a = lo->AsDouble(NAN), b = hi->AsDouble(NAN);
            if (!std::isfinite(n) || n < 0 || n > 1000000000 || std::floor(n) != n ||
                !std::isfinite(a) || !std::isfinite(b) || a > b) return {};
            if (!out.valid) { out.valid = true; out.minDt = static_cast<float>(a); out.maxDt = static_cast<float>(b); }
            else { out.minDt = (std::min)(out.minDt, static_cast<float>(a)); out.maxDt = (std::max)(out.maxDt, static_cast<float>(b)); }
            out.swingCalls += static_cast<uint64_t>(n);
        }
        return out;
    }
}
