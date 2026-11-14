#pragma once
#include <algorithm>
#include <cmath>
struct FrameDetector {
    double baselineMs{0};
    bool sample(double ms) {
        if (!std::isfinite(ms) || ms <= 0 || ms > 10000) { baselineMs=0; return false; }
        const bool anomaly = baselineMs > 0 && ms >= 50.0 && ms >= baselineMs * 2.0;
        if (!baselineMs) baselineMs = ms;
        else if (!anomaly) baselineMs = baselineMs * .95 + ms * .05;
        return anomaly;
    }
};
