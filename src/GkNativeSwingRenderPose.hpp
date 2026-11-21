#pragma once
#include "GakuBurstBridge.h"
#include "GkIdolController.hpp"

namespace GakuMotion {
    // LateUpdate prepares the next Swing input after the current job has run.
    // Display the humanoid pose the current job consumed, while leaving the
    // current motion sample queued for the next job.
    inline bool AlignNativeSwingRenderPose(const GbbFrame& previous, const GbbFrame& current,
                                          const GbbStatus& status, const IdolController& controller)
    {
        if (current.unity_frame <= 0 || status.source_frame != current.unity_frame - 1 ||
            previous.unity_frame != status.source_frame || previous.actor != current.actor ||
            previous.animator != current.animator || previous.flags != current.flags ||
            !previous.bone_count || previous.bone_count > GBB_MAX_BONES ||
            previous.bone_count != current.bone_count ||
            status.writes != previous.bone_count ||
            !controller.fnSetLocalRotation || !controller.fnSetLocalPosition)
            return false;
        for (uint32_t i = 0; i < previous.bone_count; ++i) {
            if (!previous.bones[i].transform || previous.bones[i].transform != current.bones[i].transform ||
                previous.bones[i].humanoid_id != current.bones[i].humanoid_id)
                return false;
        }
        for (uint32_t i = 0; i < previous.bone_count; ++i) {
            const auto& bone = previous.bones[i];
            const auto& q = bone.local_rotation;
            controller.fnSetLocalRotation(bone.transform, q.x, q.y, q.z, q.w);
            if (bone.humanoid_id == 0) {
                const auto& p = bone.local_position;
                controller.fnSetLocalPosition(bone.transform, p.x, p.y, p.z);
            }
        }
        return true;
    }
}
