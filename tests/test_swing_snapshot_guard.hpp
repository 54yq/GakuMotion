#pragma once
#include "GkSwingSnapshotGuard.hpp"
#include "GkSwingCheckpointReport.hpp"

namespace SwingSnapshotGuardHarness
{
    struct Mock { int reads{}, commits{}; int failAt{-1}; bool commitSucceeds{true}; };
    inline bool Read(void* ctx, GakuMotion::swing_snapshot::Kind kind, const GakuMotion::swing_snapshot::Region&, uint8_t* out, size_t size) {
        auto& m = *static_cast<Mock*>(ctx); ++m.reads; const int i = static_cast<int>(kind);
        if (m.failAt == i) return false;
        for (size_t j = 0; j < size; ++j) out[j] = static_cast<uint8_t>(i + 1);
        return true;
    }
    inline bool Commit(void* ctx, const GakuMotion::swing_snapshot::Evidence&, const GakuMotion::swing_snapshot::Blocks& data) {
        auto& m = *static_cast<Mock*>(ctx); ++m.commits;
        for(size_t i=0;i<data.size();++i) for(uint8_t value:data[i]) if(value!=i+1) return false;
        return m.commitSucceeds && data[0].size() == 64 && data[0][0] == 1 &&
            data[static_cast<size_t>(GakuMotion::swing_snapshot::Kind::Static)][0] == 2 &&
            data[static_cast<size_t>(GakuMotion::swing_snapshot::Kind::Clock)][0] == 5;
    }
    inline void Test()
    {
        using namespace GakuMotion::swing_snapshot;
        auto check = [](bool ok, const char* s) { StageRuntimeHarness::Check(ok, s); };
        GakuMotion::jsonlite::Value reportJson; std::string error;
        const std::string sample=R"({"profile":"gakumas-burst-20261008-windows-x64","actor_swing_lists":{"jobs_complete":false,"lists":[{"name":"dynamicBones","layout_verified":true,"length":2,"capacity":4,"stride":956,"samples":64,"failures":0,"changes":0,"container":"0x1000","payload":"0x2000"}]},"observed":{"swing_fields_unboxed":{"_time":556}}})";
        check(GakuMotion::jsonlite::ParseText(sample,reportJson,error),"synthetic checkpoint report parses");
        const auto report=GakuMotion::ParseSwingCheckpointReport(reportJson);
        check(report.parsed && report.lists[0].stable && report.lists[0].count==2 && report.clockLayout && !report.lists[1].seen,
            "report displays verified headers but leaves missing regions missing");
        Evidence e{1, 2, 3, 4, 5, true, true, true, true};
        for (size_t i = 0; i < e.regions.size(); ++i) e.regions[i] = {0x1000 + 0x100 * i, 64, i + 1, true};
        check(Ready(e) && MayRestore(e, e), "verified synthetic snapshot can be considered for restoration");
        auto changed = e; changed.jobsComplete = false;
        check(!Ready(changed) && !MayRestore(e, changed), "in-flight jobs block snapshot and restoration");
        changed = e; changed.allStateCovered = false;
        check(!Ready(changed), "unknown physics state coverage cannot be assumed safe");
        changed = e; changed.regions[static_cast<size_t>(Kind::Static)].verified = false;
        check(!Ready(changed), "static collision state must be verified before restoration");
        changed = e; changed.regions[0].data += 16;
        check(!MayRestore(e, changed), "reallocated region cannot receive stale snapshot");
        changed = e; changed.regions[0].identity++;
        check(!MayRestore(e, changed), "changed native allocation identity rejects restoration");
        changed = e; changed.regions[1].data = changed.regions[0].data + 1;
        check(!Ready(changed), "overlapping state regions are rejected");
        changed = e; changed.regions[0].data = (std::numeric_limits<uintptr_t>::max)() - 8;
        check(!Ready(changed), "region arithmetic overflow is rejected");
        changed = e; changed.actor++;
        check(!MayRestore(e, changed), "another actor cannot receive the saved state");
        changed = e; changed.paused = false;
        check(!MayRestore(e, changed), "resumed Live cannot receive a stale physics snapshot");
        Checkpoint checkpoint; Mock mock{};
        check(checkpoint.Capture(e, &mock, Read) && checkpoint.Valid() && mock.reads == 5, "capture deep-copies all verified regions including static collisions");
        changed = e; changed.regions[1].identity++;
        check(!checkpoint.Restore(changed, &mock, Commit) && mock.commits == 0 && checkpoint.Valid(), "identity change prevents every restore write");
        mock.commitSucceeds = false;
        check(!checkpoint.Restore(e, &mock, Commit) && checkpoint.Valid(), "failed atomic adapter preserves checkpoint");
        mock.commitSucceeds = true;
        check(checkpoint.Restore(e, &mock, Commit) && !checkpoint.Valid(), "successful atomic restore consumes the checkpoint");
        mock.failAt = 1; mock.reads = 0;
        check(!checkpoint.Capture(e, &mock, Read) && !checkpoint.Valid(), "partial capture never becomes a restorable checkpoint");
    }
}
