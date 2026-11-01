#pragma once
#include "GkMotionData.hpp"
#include "GkEyeTargetFrames.hpp"
#include <cctype>

namespace GakuMotion
{
    inline const EyeTargetFrame* FindEyeTargetFrame(std::string actorName)
    {
        for (char& c : actorName) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (const auto& frame : kEyeTargetFrames)
            if (actorName.find(frame.character) != std::string::npos) return &frame;
        return nullptr; // Unmeasured faces do not inherit another character's calibration.
    }

    inline Quaternion EyeFrameQuaternion(const float (&q)[4])
    {
        return {q[0], q[1], q[2], q[3]};
    }

    inline double EyeRotationErrorDegrees(const Quaternion& a, const Quaternion& b)
    {
        if (!KineTexture::IsUnitRotation(a) || !KineTexture::IsUnitRotation(b)) return 360.0;
        const double dot = double(a.x)*b.x + double(a.y)*b.y + double(a.z)*b.z + double(a.w)*b.w;
        const double na = double(a.x)*a.x + double(a.y)*a.y + double(a.z)*a.z + double(a.w)*a.w;
        const double nb = double(b.x)*b.x + double(b.y)*b.y + double(b.z)*b.z + double(b.w)*b.w;
        return 2.0 * std::acos(std::clamp(std::abs(dot) / std::sqrt(na*nb), 0.0, 1.0)) * 57.29577951308232;
    }

    // Geometry-relative EyePose scaling (#150 §8, accepted 2026-10-25).
    //
    // `gain` is the character's measured eye-region response ratio against the
    // jsna anchor, i.e. a geometric quantity, not a hand-tuned art knob:
    //     gain(ch) = 0.60 * R(jsna) / R(ch)
    // Slerp from identity is a rotation power, so this is a pure angle scaling
    // and commutes with the head->parent conjugation performed below. A gain of
    // 1.0 reproduces the raw 1:1 source rotation, which is the rejected baseline.
    inline Quaternion ScaleEyeRotation(const Quaternion& delta, float gain)
    {
        if (!(gain > 0.0f) || gain >= 1.0f) return delta;
        return Quaternion::Slerp(Quaternion::Identity(), delta, gain);
    }

    // D is a source rotation expressed in Head space after coordinate reflection.
    // Conjugate into the measured neutral eye-parent frame before composing the
    // prefab's eye local. An animated bind-time eye pose is never used as rest.
    inline bool MapHeadSpaceEyeDelta(const EyeTargetFrame& frame, int hid,
        const Quaternion& delta, const Quaternion& actualParentToHead, float gain, Quaternion& output)
    {
        if ((hid != 21 && hid != 22) || !KineTexture::IsUnitRotation(delta)) return false;
        const Quaternion parent = EyeFrameQuaternion(frame.headFaceLocal);
        const Quaternion neutral = hid == 21 ? EyeFrameQuaternion(frame.leftLocal) : EyeFrameQuaternion(frame.rightLocal);
        if (EyeRotationErrorDegrees(actualParentToHead, parent) > 0.25) return false;
        output = (parent.Inverse() * ScaleEyeRotation(delta, gain) * parent * neutral).Normalized();
        return true;
    }
}
