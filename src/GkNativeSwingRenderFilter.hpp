#pragma once
#include <KineTexture/Math.hpp>
#include <algorithm>
#include <array>
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

    // A symmetric five-tap low pass on a fixed time grid. Resampling avoids
    // making the cutoff depend on Unity's uneven render intervals.
    class RotationSmoother {
        struct Sample { float time{}; Quaternion rotation{}; };
        std::array<Sample,64> history_{};
        size_t first_{}, count_{};

        const Sample& At(size_t i) const { return history_[(first_+i)%history_.size()]; }

        Quaternion AtTime(float time) const
        {
            size_t upper = count_-1;
            while (upper > 0 && At(upper-1).time >= time) --upper;
            if (upper == 0) return At(0).rotation;
            const auto& a = At(upper-1);
            const auto& b = At(upper);
            return Quaternion::Slerp(a.rotation,b.rotation,(time-a.time)/(b.time-a.time));
        }

    public:
        static constexpr float kTapSpacing = .025f;
        static constexpr float kDelay = 2.f*kTapSpacing;

        void Reset() { first_ = count_ = 0; }

        bool Push(float time, Quaternion rotation, Quaternion& output, float& center)
        {
            if (count_ && time <= At(count_-1).time) Reset();
            if (count_ == history_.size()) { first_ = (first_+1)%history_.size(); --count_; }
            history_[(first_+count_)%history_.size()] = {time,rotation};
            ++count_;
            if (count_ < 2) return false;
            // Shorten the window near a restart rather than holding an old pose.
            const float step = (std::min)(kTapSpacing,(time-At(0).time)*.25f);
            center = time-2.f*step;
            const auto reference = AtTime(center);
            constexpr std::array weights{1.f,2.f,2.f,2.f,1.f};
            Quaternion sum{0.f,0.f,0.f,0.f};
            for (size_t i=0; i<weights.size(); ++i) {
                const auto q = AtTime((std::max)(At(0).time,time-float(4-i)*step));
                const float weight = Quaternion::Dot(q,reference) < 0.f ? -weights[i] : weights[i];
                sum.x += weight*q.x; sum.y += weight*q.y; sum.z += weight*q.z; sum.w += weight*q.w;
            }
            output = sum.Normalized();
            return true;
        }
    };
}
