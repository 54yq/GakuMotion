#pragma once
#include <array>
#include <cmath>

namespace GakuMotion::live_aspect
{
    using Projection = std::array<float, 16>;

    inline bool ProjectionMatchesAspect(const Projection& projection, float aspect)
    {
        for (float value : projection)
            if (!std::isfinite(value)) return false;
        return projection[0] > 0 && projection[5] > 0 &&
            std::abs(projection[5] / projection[0] - aspect) < 0.005f;
    }

    // A native aspect switch can change the lens as well as the render target.
    // Measure the change once, then apply the ratio to the live lens each frame.
    inline float HorizontalScaleAfterSwitch(const Projection& before, const Projection& after,
        float originalAspect, float targetAspect)
    {
        const float fallback = targetAspect / originalAspect;
        if (!ProjectionMatchesAspect(before, originalAspect) ||
            !ProjectionMatchesAspect(after, targetAspect)) return fallback;
        const float scale = before[0] / after[0];
        return std::isfinite(scale) && scale >= 0.25f && scale <= 4.f ? scale : fallback;
    }

    inline bool PreserveHorizontalFraming(Projection& projection, float originalAspect,
        float targetAspect, float measuredHorizontalScale = 0.f)
    {
        if (!std::isfinite(originalAspect) || !std::isfinite(targetAspect) ||
            targetAspect <= 0 || originalAspect <= targetAspect) return false;

        const bool stillWide = ProjectionMatchesAspect(projection, originalAspect);
        if (!stillWide && !ProjectionMatchesAspect(projection, targetAspect)) return false;
        const float aspectScale = targetAspect / originalAspect;
        const float horizontalScale = stillWide ? 1.f :
            measuredHorizontalScale > 0.f ? measuredHorizontalScale : aspectScale;
        if (!std::isfinite(horizontalScale)) return false;
        // Unity stores matrices by column. Preserve the horizontal center
        // and scale the vertical lens shift by the viewport height change.
        projection[0] *= horizontalScale;
        projection[5] *= stillWide ? aspectScale : horizontalScale;
        projection[9] *= aspectScale;
        projection[13] *= aspectScale;
        return true;
    }
}
