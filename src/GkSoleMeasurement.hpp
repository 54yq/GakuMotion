#pragma once

#include "GkMotionData.hpp"
#include "GkJsonLite.hpp"
#include "GkSoleProxyProfile.hpp"
#include <array>
#include <limits>

namespace GakuMotion
{
	// v2 is an observer. There is deliberately no solver, support state, or setter
	// callback in this module. Offsets are metres in the foot's rotation-only frame,
	// not Unity Transform local units: calibration must include the actual shoe size.
	namespace sole
	{
		inline bool Finite(const Vector3& p)
		{
			return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
		}

		inline float Dot(const Vector3& a, const Vector3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
		inline Vector3 Scale(const Vector3& p, float s) { return {p.x * s, p.y * s, p.z * s}; }
		inline bool Same(const Vector3& a, const Vector3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

		inline bool ValidRotation(const Quaternion& q)
		{
			const float norm = Quaternion::Dot(q, q);
			return std::isfinite(norm) && norm > 1e-8f && std::fabs(norm - 1.f) < .01f;
		}

		inline Vector3 BootProxyCenter(int side, int point)
		{
			const auto& p = sole_proxy_profile::Centers[side][point];
			return {p[0], p[1], p[2]};
		}

		inline Vector3 BootProxyCorner(int side, int patch, int corner)
		{
			const auto& p = sole_proxy_profile::Corners[side][patch][corner];
			return {p[0], p[1], p[2]};
		}
	}

	struct SoleMeasurementSettings
	{
		bool enabled{false}, planeCalibrated{false}, shoeCalibrated{false};
		// Evidence-derived approximate boot geometry. Selection is not calibration.
		// False keeps saved v2 drafts and their three manual points unchanged.
		bool useBootProxy{false};
		// Independent world plane: dot(unit normal, world position) = offsetM.
		Vector3 planeNormal{0, 1, 0};
		float planeOffsetM{0}, displayBandM{.002f};
		// Both feet need separate calibration; never mirror arbitrary rig axes.
		// Zero initial points intentionally mean "unconfigured", not a shoe shape.
		std::array<std::array<Vector3, 3>, 2> points{}; // heel, forefoot, toe
		static constexpr auto Profile = "saki-training-boots";

		bool IsValid() const
		{
			const float n = sole::Dot(planeNormal, planeNormal);
			if (!sole::Finite(planeNormal) || std::fabs(n - 1.f) > .001f || planeNormal.y < .1f ||
				!std::isfinite(planeOffsetM) || std::fabs(planeOffsetM) > 10000 ||
				!std::isfinite(displayBandM) || displayBandM < 0 || displayBandM > .05f)
				return false;
			for (const auto& foot : points)
			{
				for (const auto& p : foot)
					if (!sole::Finite(p) || sole::Dot(p, p) > .25f) return false;
				if (shoeCalibrated && !useBootProxy && (sole::Dot(foot[0] - foot[1], foot[0] - foot[1]) < 1e-6f ||
					sole::Dot(foot[1] - foot[2], foot[1] - foot[2]) < 1e-6f))
					return false;
			}
			if (useBootProxy)
				for (int side = 0; side < 2; ++side)
				{
					for (int i = 0; i < 3; ++i)
					{
						const auto p = sole::BootProxyCenter(side, i);
						if (!sole::Finite(p) || sole::Dot(p, p) > .25f) return false;
					}
					for (int patch = 0; patch < 2; ++patch)
						for (int i = 0; i < 4; ++i)
						{
							const auto p = sole::BootProxyCorner(side, patch, i);
							if (!sole::Finite(p) || sole::Dot(p, p) > .25f) return false;
						}
				}
			return true;
		}

		bool operator==(const SoleMeasurementSettings& s) const
		{
			if (enabled != s.enabled || planeCalibrated != s.planeCalibrated || shoeCalibrated != s.shoeCalibrated ||
				useBootProxy != s.useBootProxy ||
				!sole::Same(planeNormal, s.planeNormal) || planeOffsetM != s.planeOffsetM || displayBandM != s.
				displayBandM)
				return false;
			for (int side = 0; side < 2; ++side)
				for (int i = 0; i < 3; ++i)
					if (!sole::Same(points[side][i], s.points[side][i])) return false;
			return true;
		}
	};

	struct SoleFootPose
	{
		bool valid{false};
		Vector3 hip, knee, ankle, toe;
		Quaternion footRotation;
	};

	struct SolePointMeasurement
	{
		Vector3 world;
		float signedDistanceM{0}, penetrationM{0};
	};

	struct SolePatchMeasurement
	{
		std::array<SolePointMeasurement, 4> corners{};
		float minimumDistanceM{0}, maximumDistanceM{0}, maximumPenetrationM{0};
	};

	struct SoleFootMeasurement
	{
		bool valid{false};
		bool usingBootProxy{false};
		SoleFootPose pose;
		std::array<SolePointMeasurement, 3> points{};
		std::array<SolePatchMeasurement, 2> patches{}; // heel and forefoot, rotation-only metres
		float minimumDistanceM{0}, maximumPenetrationM{0};
	};

	struct SoleMeasurementSnapshot
	{
		bool available{false}, calibrated{false};
		void* actor{nullptr};
		float clipTime{0};
		std::array<SoleFootMeasurement, 2> feet;
	};

	inline SoleFootMeasurement MeasureSole(const SoleFootPose& pose, const SoleMeasurementSettings& settings, int side)
	{
		SoleFootMeasurement result;
		if (!settings.enabled || !settings.IsValid() || side < 0 || side > 1 || !pose.valid ||
			!sole::Finite(pose.hip) || !sole::Finite(pose.knee) || !sole::Finite(pose.ankle) ||
			!sole::Finite(pose.toe) || !sole::ValidRotation(pose.footRotation))
			return result;
		result.valid = true;
		result.pose = pose;
		result.usingBootProxy = settings.useBootProxy;
		result.minimumDistanceM = (std::numeric_limits<float>::max)();
		const auto rotation = pose.footRotation.Normalized();
		auto measure = [&](const Vector3& local, SolePointMeasurement& point)
		{
			point.world = pose.ankle + rotation.Rotate(local);
			point.signedDistanceM = sole::Dot(settings.planeNormal, point.world) - settings.planeOffsetM;
			if (!sole::Finite(point.world) || !std::isfinite(point.signedDistanceM)) return false;
			point.penetrationM = (std::max)(0.f, -point.signedDistanceM);
			result.minimumDistanceM = (std::min)(result.minimumDistanceM, point.signedDistanceM);
			result.maximumPenetrationM = (std::max)(result.maximumPenetrationM, point.penetrationM);
			return true;
		};
		for (int i = 0; i < 3; ++i)
		{
			const auto local = settings.useBootProxy ? sole::BootProxyCenter(side, i) : settings.points[side][i];
			if (!measure(local, result.points[i])) return {};
		}
		if (settings.useBootProxy)
			for (int patchIndex = 0; patchIndex < 2; ++patchIndex)
			{
				auto& patch = result.patches[patchIndex];
				patch.minimumDistanceM = (std::numeric_limits<float>::max)();
				patch.maximumDistanceM = -(std::numeric_limits<float>::max)();
				for (int i = 0; i < 4; ++i)
				{
					auto& corner = patch.corners[i];
					if (!measure(sole::BootProxyCorner(side, patchIndex, i), corner)) return {};
					patch.minimumDistanceM = (std::min)(patch.minimumDistanceM, corner.signedDistanceM);
					patch.maximumDistanceM = (std::max)(patch.maximumDistanceM, corner.signedDistanceM);
					patch.maximumPenetrationM = (std::max)(patch.maximumPenetrationM, corner.penetrationM);
				}
			}
		return result;
	}

	inline bool ParseSoleSettings(const jsonlite::Value& root, SoleMeasurementSettings& output, std::string& error)
	{
		auto reject = [&]()
		{
			error = "invalid sole_measurement v2 settings";
			return false;
		};
		const auto* s = root.Find("sole_measurement");
		if (!root.IsObject() || !s || !s->IsObject()) return reject();
		const auto* version = s->Find("version");
		const auto* profile = s->Find("profile");
		const auto* space = s->Find("point_space");
		if (!version || !version->IsNumber() || version->numberValue != 2 ||
			!profile || !profile->IsString() || profile->stringValue != SoleMeasurementSettings::Profile ||
			!space || !space->IsString() || space->stringValue != "foot_rotation_meters")
			return reject();
		SoleMeasurementSettings next;
		auto boolean = [&](const char* key, bool& value)
		{
			const auto* v = s->Find(key);
			if (!v || !v->IsBool()) return false;
			value = v->boolValue;
			return true;
		};
		auto number = [](const jsonlite::Value* v, float& value)
		{
			if (!v || !v->IsNumber() || !std::isfinite(v->numberValue)) return false;
			value = static_cast<float>(v->numberValue);
			return std::isfinite(value);
		};
		auto vector = [&](const jsonlite::Value* v, Vector3& value)
		{
			return v && v->IsArray() && v->Size() == 3 && number(v->At(0), value.x) &&
				number(v->At(1), value.y) && number(v->At(2), value.z);
		};
		// The optional extension preserves old version-2 documents byte meanings.
		if (const auto* v = s->Find("use_boot_proxy"))
		{
			if (!v->IsBool()) return reject();
			next.useBootProxy = v->boolValue;
		}
		if (!boolean("enabled", next.enabled) || !boolean("plane_calibrated", next.planeCalibrated) ||
			!boolean("shoe_calibrated", next.shoeCalibrated) || !vector(s->Find("plane_normal"), next.planeNormal) ||
			!number(s->Find("plane_offset_m"), next.planeOffsetM) || !number(
				s->Find("display_band_m"), next.displayBandM))
			return reject();
		for (int side = 0; side < 2; ++side)
		{
			const auto* foot = s->Find(side == 0 ? "left_points_m" : "right_points_m");
			if (!foot || !foot->IsArray() || foot->Size() != 3) return reject();
			for (int i = 0; i < 3; ++i) if (!vector(foot->At(i), next.points[side][i])) return reject();
		}
		if (!next.IsValid()) return reject();
		output = next;
		error.clear();
		return true;
	}

	inline std::string SerializeSoleSettings(const SoleMeasurementSettings& s)
	{
		if (!s.IsValid()) return {};
		std::ostringstream out;
		auto vector = [&](const Vector3& p)
		{
			out << '[' << jsonlite::FormatNumber(p.x, 9) << ',' << jsonlite::FormatNumber(p.y, 9)
				<< ',' << jsonlite::FormatNumber(p.z, 9) << ']';
		};
		out << "{\n  \"sole_measurement\": {\n    \"version\": 2,\n    \"profile\": \"" << s.Profile
			<< "\",\n    \"point_space\": \"foot_rotation_meters\",\n    \"enabled\": " << (
				s.enabled ? "true" : "false")
			<< ",\n    \"plane_calibrated\": " << (s.planeCalibrated ? "true" : "false")
			<< ",\n    \"shoe_calibrated\": " << (s.shoeCalibrated ? "true" : "false") << ",\n    \"plane_normal\": ";
		vector(s.planeNormal);
		out << ",\n    \"plane_offset_m\": " << jsonlite::FormatNumber(s.planeOffsetM, 9)
			<< ",\n    \"display_band_m\": " << jsonlite::FormatNumber(s.displayBandM, 9)
			<< ",\n    \"use_boot_proxy\": " << (s.useBootProxy ? "true" : "false");
		for (int side = 0; side < 2; ++side)
		{
			out << ",\n    \"" << (side == 0 ? "left_points_m" : "right_points_m") << "\": [";
			for (int i = 0; i < 3; ++i)
			{
				if (i) out << ',';
				vector(s.points[side][i]);
			}
			out << ']';
		}
		out << "\n  }\n}\n";
		return out.str();
	}
} // namespace GakuMotion
