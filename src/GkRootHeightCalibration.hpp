#pragma once
#include "GkMotionData.hpp"
#include "GkJsonLite.hpp"
#include <array>
#include <algorithm>
#include <filesystem>

namespace GakuMotion
{
	// Optional runtime measurement, separate from the portable gkclip. A fixed
	// trim cannot repair contact/IK or a time-varying trajectory. Revalidate it
	// whenever the clip's root/rotation data or the target leg geometry changes.
	struct RootHeightCalibration
	{
		static constexpr std::array<int, 8> RigIds{1, 2, 3, 4, 5, 6, 19, 20};
		bool enabled{false};
		float offset{0};
		std::array<Vector3, 21> rigOffsets{};

		static std::string ClipSignature(const MotionClip& clip)
		{
			uint32_t hash = 2166136261u;
			auto byte = [&hash](uint8_t b) { hash = (hash ^ b) * 16777619u; };
			auto word = [&byte](uint32_t w)
			{
				for (int shift = 0; shift < 32; shift += 8) byte(uint8_t(w >> shift));
			};
			auto scalar = [&word](float f)
			{
				uint32_t bits;
				std::memcpy(&bits, &f, sizeof(bits));
				word(bits);
			};
			word(clip.total_frames);
			scalar(clip.fps);
			for (const auto& bone : clip.bones)
			{
				byte(uint8_t(bone.humanoid_id));
				byte(uint8_t(bone.humanoid_id >> 8));
			}
			for (const auto& p : clip.root_positions)
			{
				scalar(p.x);
				scalar(p.y);
				scalar(p.z);
			}
			for (const auto& q : clip.bone_rotations)
			{
				scalar(q.x);
				scalar(q.y);
				scalar(q.z);
				scalar(q.w);
			}
			char text[9];
			std::snprintf(text, sizeof(text), "%08x", hash);
			return text;
		}

		bool Load(const std::string& path, const MotionClip& clip, std::string& error)
		{
			*this = RootHeightCalibration{};
			error.clear();
			std::error_code ec;
			const auto nativePath = MotionPathFromUtf8(path);
			const bool exists = std::filesystem::exists(nativePath, ec);
			if (ec)
			{
				error = ec.message();
				return false;
			}
			if (!exists) return true;
			jsonlite::Value root;
			std::ifstream file(nativePath, std::ios::binary);
			std::ostringstream text;
			text << file.rdbuf();
			if (!file.is_open()) { error = "cannot open root height calibration: " + path; return false; }
			if (!jsonlite::ParseText(text.str(), root, error)) return false;
			auto reject = [&error]()
			{
				error = "invalid or mismatched root height calibration";
				return false;
			};
			const auto* active = root.Find("enabled");
			if (!root.IsObject() || root.IntPath("version", 0) != 1 || !active || !active->IsBool()) return reject();
			if (!active->boolValue) return true;
			const auto* value = root.Find("offset_m");
			const auto* rows = root.Find("rig_offsets");
			if (clip.format != MotionClip::Format::kGkClip || clip.version < 2 ||
				root.StringPath("clip_signature", "") != ClipSignature(clip) ||
				!value || !value->IsNumber() || !std::isfinite(value->numberValue) ||
				std::fabs(value->numberValue) > .1 ||
				!rows || !rows->IsArray() || rows->Size() != RigIds.size())
				return reject();
			std::array<bool, 21> seen{};
			std::array<Vector3, 21> parsed{};
			for (const auto& row : rows->arrayItems)
			{
				if (!row.IsArray() || row.Size() != 4) return reject();
				for (const auto& v : row.arrayItems)
				{
					if (!v.IsNumber() || !std::isfinite(v.numberValue)) return reject();
				}
				const double idValue = row.arrayItems[0].numberValue;
				if (idValue < 0 || idValue > 20 || std::floor(idValue) != idValue) return reject();
				const int hid = int(idValue);
				if (std::find(RigIds.begin(), RigIds.end(), hid) == RigIds.end() || seen[hid]) return reject();
				seen[hid] = true;
				for (size_t j = 1; j < 4; ++j) if (std::fabs(row.arrayItems[j].numberValue) > 2) return reject();
				parsed[hid] = Vector3(row.arrayItems[1].AsFloat(0), row.arrayItems[2].AsFloat(0),
				                      row.arrayItems[3].AsFloat(0));
			}
			rigOffsets = parsed;
			offset = value->AsFloat(0);
			enabled = true;
			return true;
		}

		float Match(const std::array<Vector3, 21>& offsets,
		            const std::array<bool, 21>& present) const
		{
			if (!enabled) return 0;
			for (int hid : RigIds)
			{
				const auto& a = offsets[hid];
				const auto& b = rigOffsets[hid];
				// Recorder positions have six decimal places (metres).
				if (!present[hid] || !std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(a.z) ||
					std::fabs(a.x - b.x) > 2e-6f || std::fabs(a.y - b.y) > 2e-6f || std::fabs(a.z - b.z) > 2e-6f)
					return 0;
			}
			return offset;
		}
	};
} // namespace GakuMotion
