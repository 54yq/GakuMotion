#pragma once
#include <chrono>
#include <cmath>
#include <cstdint>

namespace GakuMotion {
// Samples the loader's once-per-frame update, independently of GUI rendering.
struct LiveFrameMonitor {
    using Clock = std::chrono::steady_clock;
    struct Snapshot {
        bool available{};
        float fps{}, frameMs{}, baselineMs{}, lastStallMs{};
        uint64_t stalls{};
    } display;
    Clock::time_point last{}, window{}, lastLog{};
    double baseline{};
    unsigned frames{};
    uintptr_t owner{};
    int scene{};

    bool Update(Clock::time_point now, bool live, uintptr_t currentOwner, int currentScene) {
        if (!live || !currentOwner) { *this = {}; return false; }
        if (!display.available || owner != currentOwner || scene != currentScene) {
            *this = {}; display.available = true; owner = currentOwner; scene = currentScene;
            last = window = now; return false;
        }
        const double ms = std::chrono::duration<double, std::milli>(now - last).count();
        last = now;
        if (!std::isfinite(ms) || ms <= 0 || ms > 10000) {
            baseline = 0; frames = 0; window = now; return false;
        }
        const bool stall = baseline > 0 && ms >= 50.0 && ms >= baseline * 2.0;
        if (!baseline) baseline = ms;
        else if (!stall) baseline = baseline * .95 + ms * .05;
        ++frames;
        const double seconds = std::chrono::duration<double>(now - window).count();
        if (seconds >= 1.0) {
            display.fps = static_cast<float>(frames / seconds); frames = 0; window = now;
        }
        if (seconds >= 1.0 || stall) {
            display.frameMs = static_cast<float>(ms);
            display.baselineMs = static_cast<float>(baseline);
        }
        if (!stall) return false;
        ++display.stalls; display.lastStallMs = static_cast<float>(ms);
        if (lastLog.time_since_epoch().count() && now - lastLog < std::chrono::seconds(2)) return false;
        lastLog = now; return true;
    }
};
}
