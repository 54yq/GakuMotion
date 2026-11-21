#pragma once
#include <KineTexture/Math.hpp>
#include <algorithm>
#include <cmath>

namespace GakuMotion::native_swing {
    using KineTexture::Quaternion;

    // Learn the render route on the same camera before borrowing transforms.
    struct CameraPair {
        void* pending{};
        void* confirmed{};
        int beginFrame{-1};
        int endFrame{-1};

        bool Begin(void* camera, int frame)
        {
            if (!camera || pending || frame <= endFrame) return false;
            pending = camera;
            beginFrame = frame;
            return confirmed == camera && endFrame == frame - 1;
        }

        bool End(void* camera, int frame)
        {
            if (!pending || pending != camera || frame != beginFrame) return false;
            confirmed = camera;
            endFrame = frame;
            pending = nullptr;
            return true;
        }
    };
    inline float Angle(const Quaternion& a, const Quaternion& b)
    {
        return 114.59156f * std::acos((std::min)(1.f, std::abs(Quaternion::Dot(a,b))));
    }

    // Return the middle sample unless it reverses a substantial part of its
    // excursion by the next sample. Time is the clip clock, not a fixed FPS.
    inline Quaternion FilterMiddle(Quaternion a, Quaternion b, Quaternion c, float fraction)
    {
        const auto expected = Quaternion::Slerp(a,c,fraction);
        const float incoming = Angle(a,b);
        const float outgoing = Angle(b,c);
        const float travel = Angle(a,c);
        if ((std::min)(incoming,outgoing) < 0.35f || travel >= (std::min)(incoming,outgoing) ||
            Angle(b,expected) < 0.45f)
            return b;
        return expected;
    }
}
