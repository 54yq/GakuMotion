#pragma once
#include "GkJsonLite.hpp"
#include <array>
#include <cmath>
#include <charconv>
#include <string>
namespace GakuMotion {
struct SwingCheckpointRow {
    bool seen{}, layout{}, stable{};
    uint64_t count{}, capacity{}, stride{}, samples{}, failures{}, changes{};
    std::string container, payload;
};
struct SwingCheckpointReport {
    bool parsed{}, clockLayout{};
    std::array<SwingCheckpointRow,4> lists{};
    std::string source;
};
inline SwingCheckpointReport ParseSwingCheckpointReport(const jsonlite::Value& root) {
    SwingCheckpointReport out;
    const auto* profile=root.Find("profile"); const auto* section=root.Find("actor_swing_lists");
    const auto* lists=section?section->Find("lists"):nullptr;
    if (!profile || profile->AsString("")!="gakumas-burst-20261008-windows-x64" || !lists || !lists->IsArray()) return out;
    const char* names[]{"dynamicBones","staticBones","breastBones","chainBones"};
    auto number=[](const jsonlite::Value& row,const char* key,uint64_t& dst) {
        const auto* v=row.Find(key); if(!v || !v->IsNumber()) return false;
        double n=v->AsDouble(-1); if(!std::isfinite(n)||n<0||n>1000000000||std::floor(n)!=n) return false;
        dst=static_cast<uint64_t>(n); return true;
    };
    for(const auto& row:lists->arrayItems) {
        const auto* name=row.Find("name"); if(!name) continue;
        for(unsigned i=0;i<4;++i) if(name->AsString("")==names[i]) {
            if(out.lists[i].seen) return {};
            auto& r=out.lists[i]; r.seen=true;
            const auto* layout=row.Find("layout_verified"); r.layout=layout && layout->IsBool() && layout->boolValue;
            if(!number(row,"length",r.count)||!number(row,"capacity",r.capacity)||!number(row,"stride",r.stride)||
                !number(row,"samples",r.samples)||!number(row,"failures",r.failures)||!number(row,"changes",r.changes)||
                r.count>r.capacity || r.capacity>1000000 || r.stride>65536) return {};
            const auto* container=row.Find("container"); const auto* payload=row.Find("payload");
            r.container=container?container->AsString(""):""; r.payload=payload?payload->AsString(""):"";
            auto address=[](const std::string& s) { uint64_t n{}; if(!s.starts_with("0x") || s.size()<3) return uint64_t{0};
                auto result=std::from_chars(s.data()+2,s.data()+s.size(),n,16); return result.ec==std::errc{} && result.ptr==s.data()+s.size()?n:uint64_t{0}; };
            const bool addresses=address(r.container)!=0 && (!r.count || address(r.payload)!=0);
            r.stable=r.layout && r.stride && r.samples && !r.failures && !r.changes && addresses;
        }
    }
    const auto* clock=root.FindPath("observed.swing_fields_unboxed._time");
    out.clockLayout=clock && clock->IsNumber() && clock->AsInt(-1)==556;
    out.parsed=true; return out;
}
}
