#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace GakuMotion::swing_snapshot
{
    // Validation contract only. It deliberately performs no memory access and
    // does not assert that the game's unknown NativeList layout is understood.
    // Static-bone entries also carry collision state. Their stable allocation
    // does not make their contents immutable; include them in every checkpoint.
    enum class Kind : uint8_t { Dynamic, Static, Breast, Chain, Clock, Count };
    struct Region { uintptr_t data{}; size_t bytes{}; uint64_t identity{}; bool verified{}; };
    struct Evidence {
        uint64_t owner{}, actor{}, rig{}, graph{}, session{};
        bool paused{}, jobsComplete{}, layoutVerified{}, allStateCovered{};
        std::array<Region, static_cast<size_t>(Kind::Count)> regions{};
    };
    inline bool Ready(const Evidence& e)
    {
        if (!e.owner || !e.actor || !e.rig || !e.graph || !e.session || !e.paused ||
            !e.jobsComplete || !e.layoutVerified || !e.allStateCovered) return false;
        for (size_t i = 0; i < e.regions.size(); ++i) {
            const auto& r = e.regions[i];
            if (!r.verified || !r.data || !r.bytes || !r.identity ||
                r.bytes > (std::numeric_limits<uintptr_t>::max)() - r.data) return false;
            for (size_t j = 0; j < i; ++j) {
                const auto& p = e.regions[j];
                if (r.data < p.data + p.bytes && p.data < r.data + r.bytes) return false;
            }
        }
        return true;
    }
    inline bool MayRestore(const Evidence& saved, const Evidence& now)
    {
        if (!Ready(saved) || !Ready(now) || saved.owner != now.owner || saved.actor != now.actor ||
            saved.rig != now.rig || saved.graph != now.graph || saved.session != now.session) return false;
        for (size_t i = 0; i < saved.regions.size(); ++i) {
            const auto& a = saved.regions[i]; const auto& b = now.regions[i];
            if (a.data != b.data || a.bytes != b.bytes || a.identity != b.identity) return false;
        }
        return true;
    }
    // The runtime adapter must establish a completed job fence and a complete
    // state inventory before using this. Commit is an all-or-nothing operation
    // supplied by that adapter; no raw game memory is accessed by this class.
    using Blocks = std::array<std::vector<uint8_t>, static_cast<size_t>(Kind::Count)>;
    using Read = bool (*)(void*, Kind, const Region&, uint8_t*, size_t);
    using Commit = bool (*)(void*, const Evidence&, const Blocks&);
    class Checkpoint
    {
        static constexpr size_t MaxBytes = 16u * 1024u * 1024u;
        Evidence origin_{};
        Blocks blocks_{};
        bool valid_{};
    public:
        bool Valid() const { return valid_; }
        void Clear() { valid_ = false; origin_ = {}; for (auto& b : blocks_) b.clear(); }
        bool Capture(const Evidence& e, void* context, Read read)
        {
            if (valid_ || !Ready(e) || !read) return false;
            size_t total = 0;
            for (const auto& r : e.regions) {
                if (r.bytes > MaxBytes - total) return false;
                total += r.bytes;
            }
            Blocks candidate;
            try {
                for (size_t i = 0; i < candidate.size(); ++i) {
                    candidate[i].resize(e.regions[i].bytes);
                    if (!read(context, static_cast<Kind>(i), e.regions[i], candidate[i].data(), candidate[i].size())) return false;
                }
            } catch (...) { return false; }
            blocks_ = std::move(candidate); origin_ = e; valid_ = true; return true;
        }
        bool Restore(const Evidence& current, void* context, Commit commit)
        {
            if (!valid_ || !commit || !MayRestore(origin_, current)) return false;
            try { if (!commit(context, current, blocks_)) return false; }
            catch (...) { return false; }
            Clear(); return true;
        }
    };
}
