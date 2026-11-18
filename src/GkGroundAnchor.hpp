#pragma once
#include "GkMotionData.hpp"
#include <array>
#include <limits>

namespace GakuMotion
{
	// A single clip-wide translation, never a per-frame foot clamp: jumps survive.
	// Offsets are target Transform.localPosition values keyed by humanoid id.
	// The animated/bind hips translation and rotations are deliberately not inputs.
	inline bool ComputeClipGroundLift(const MotionClip& clip,
	                                  const std::array<Vector3, 21>& offsets,
	                                  const std::array<bool, 21>& present,
	                                  float& lift)
	{
		lift = 0.0f;
		if (clip.version < 2 ||
			clip.total_frames == 0 || clip.root_positions.size() != clip.total_frames ||
			clip.bone_count != clip.bones.size() ||
			clip.bone_rotations.size() != size_t(clip.total_frames) * clip.bone_count)
			return false;
		std::array<int, 21> index;
		index.fill(-1);
		for (size_t i = 0; i < clip.bones.size(); ++i)
		{
			const int hid = clip.bones[i].humanoid_id;
			if (hid < 21) index[hid] = static_cast<int>(i);
		}
		constexpr int chain[] = {0, 1, 2, 3, 4, 5, 6, 19, 20};
		for (int hid : chain)
		{
			if (index[hid] < 0 || !present[hid]) return false;
			const auto& p = offsets[hid];
			if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
			if (hid != 0 && p.x * p.x + p.y * p.y + p.z * p.z < 1e-10f) return false;
		}
		float floor = std::numeric_limits<float>::infinity();
		for (size_t f = 0; f < clip.total_frames; ++f)
		{
			std::array<Quaternion, 21> world;
			std::array<Vector3, 21> pos;
			for (int hid : chain)
			{
				Quaternion local = clip.bone_rotations[f * clip.bone_count + index[hid]];
				float norm = Quaternion::Dot(local, local);
				if (!std::isfinite(norm) || norm < 1e-8f) return false;
				float inv = 1.0f / std::sqrt(norm);
				local = Quaternion(local.x * inv, local.y * inv, local.z * inv, local.w * inv);
				const int parent = GakumasParentHid(hid);
				world[hid] = (clip.format != MotionClip::Format::kGkClip || parent < 0)
					             ? local
					             : world[parent] * local;
				pos[hid] = parent < 0
					           ? clip.root_positions[f]
					           : pos[parent] + world[parent].Rotate(offsets[hid]);
				if (!std::isfinite(pos[hid].y)) return false;
				if (hid == 5 || hid == 6 || hid == 19 || hid == 20)
					floor = std::min(floor, pos[hid].y);
			}
		}
		lift = -floor;
		return std::isfinite(lift);
	}
} // namespace GakuMotion
