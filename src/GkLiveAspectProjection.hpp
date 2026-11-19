#pragma once
#include <array>
#include <cmath>

namespace GakuMotion::live_aspect
{
    using Projection = std::array<float, 16>;

    // Preserve horizontal framing. On the taller target each pixel then has
    // the same scale in both axes, with extra scene content above and below.
    inline bool PreserveHorizontalFraming(Projection& projection, float originalAspect, float targetAspect)
    {
        if (!std::isfinite(originalAspect) || !std::isfinite(targetAspect) ||
            targetAspect <= 0 || originalAspect <= targetAspect) return false;
        for (float value : projection)
            if (!std::isfinite(value)) return false;
        if (projection[0] <= 0 || projection[5] <= 0 ||
            std::abs(projection[5] / projection[0] - targetAspect) > 0.005f) return false;

        const float scale = targetAspect / originalAspect;
        // Unity stores the matrix by column. Keep horizontal and vertical
        // offsets in their original pixel positions, including lens shift.
        projection[0] *= scale;
        for (int column = 0; column < 4; ++column)
            projection[column * 4 + 1] *= scale;
        return true;
    }
}
