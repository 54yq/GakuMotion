#pragma once
#include "QuartzProfile.hpp"

namespace gbb::profile {
inline constexpr unsigned SwingStepCount = 5;
struct SwingStepVariant {
    uint32_t swing, dynamic, end, callReturn;
    uint64_t fingerprint;
    uint32_t sites[SwingStepCount];
    unsigned forcePointerOffset;
    bool avx;
};
inline constexpr SwingStepVariant SwingStepVariants[]{
    {0x280b0,0x2f360,0x3334f,0x28de5,0x8ef2b155005126e4ull,
     {0x30196,0x30a6c,0x31342,0x31efe,0x320a2},0x4b0,false},
    {0xcc8e0,0xd3450,0xd6fda,0xcd38b,0x1d5343175421cd10ull,
     {0xd41b5,0xd49f9,0xd5244,0xd5cb7,0xd6060},0x4d8,true}
};
inline uint64_t SwingCodeFingerprint(const unsigned char* code,size_t size) {
    uint64_t hash = 14695981039346656037ull;
    for (size_t i=0;i<size;++i) hash = (hash^code[i])*1099511628211ull;
    return hash;
}
inline const SwingStepVariant* VerifySwingSteps(HMODULE module,uintptr_t swing) {
    __try {
        const auto base = reinterpret_cast<uintptr_t>(module);
        const auto size = ImageSize(module);
        for (const auto& v : SwingStepVariants) {
            if (swing != base+v.swing) continue;
            const auto* begin = reinterpret_cast<const unsigned char*>(base+v.dynamic);
            if (v.end>size || v.callReturn>size || !Executable(begin) ||
                !ExactFunction(base,v.dynamic,v.end) || !RelativeCall(base,v.callReturn,v.dynamic) ||
                SwingCodeFingerprint(begin,v.end-v.dynamic)!=v.fingerprint) return nullptr;
            return &v;
        }
        return nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
} // namespace gbb::profile
