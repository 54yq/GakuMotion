#pragma once

#include "GkSoleMeasurement.hpp"
#include "GkSolePitchEnvelope.hpp"

namespace GakuMotion::sole_pitch
{
	// An opt-in, single-axis experiment. This pure solver does not write a pose or
	// alter the read-only profile/measurement contract. The caller applies at most
	// one returned delta after writing the original motion pose for that frame.
	struct Settings
	{
		bool experimentalSolePitchCorrection{false};
		float maxPitchDegrees{20.f};
		bool useMeshEnvelope{false};

		bool IsValid() const
		{
			return std::isfinite(maxPitchDegrees) && maxPitchDegrees >= 0.f && maxPitchDegrees <= 45.f;
		}
	};

	struct Result
	{
		bool valid{false}, actionable{false}, fullyCleared{false};
		bool usedMeshEnvelope{false};
		Vector3 localAxis{};
		float pitchDegrees{0.f};
		Quaternion localDelta{};
		Quaternion correctedWorldRotation{};
		float beforeMinimumDistanceM{0.f}, afterMinimumDistanceM{0.f};
		float beforePenetrationM{0.f}, afterPenetrationM{0.f};
	};

	// Separate experiment telemetry: #078's original-pose measurement is untouched.
	struct FootSnapshot
	{
		bool freshPose{false}, applied{false}, setterReadbackMatches{false};
		float measuredAfterMinimumDistanceM{0.f};
		bool measuredAfterValid{false};
		Result solution;
		SoleFootMeasurement after;
		const char* status{"off"};
	};

	struct Snapshot
	{
		void* actor{nullptr};
		float clipTime{0};
		uint64_t poseSerial{0};
		std::array<FootSnapshot, 2> feet;
	};

	inline Vector3 Cross(const Vector3& a, const Vector3& b)
	{
		return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
	}

	inline bool Normalize(Vector3& v)
	{
		const float n = sole::Dot(v, v);
		if (!sole::Finite(v) || !std::isfinite(n) || n < 1e-10f) return false;
		v = sole::Scale(v, 1.f / std::sqrt(n));
		return sole::Finite(v);
	}

	// Rig axes differ between feet. Derive the sagittal pitch axis from each
	// unchanged profile's heel -> forefoot direction and its rectangle winding,
	// rather than assuming Transform X or mirroring the other foot. The plane
	// normal points from the sole toward the ankle; the resulting axis is lateral.
	inline bool LocalPitchAxis(int side, Vector3& axis)
	{
		if (side < 0 || side > 1) return false;
		const auto heel = sole::BootProxyCenter(side, 0);
		const auto forefoot = sole::BootProxyCenter(side, 1);
		Vector3 forward = forefoot - heel;
		Vector3 normal{};
		for (int patch = 0; patch < 2; ++patch)
		{
			const auto c = sole::BootProxyCorner(side, patch, 0);
			Vector3 patchNormal = Cross(sole::BootProxyCorner(side, patch, 1) - c,
			                            sole::BootProxyCorner(side, patch, 3) - c);
			if (!Normalize(patchNormal)) return false;
			normal = normal + patchNormal;
		}
		if (!Normalize(forward) || !Normalize(normal)) return false;
		if (sole::Dot(normal, sole::Scale(heel + forefoot, -.5f)) < 0.f)
			normal = sole::Scale(normal, -1.f);
		axis = Cross(forward, normal);
		return Normalize(axis);
	}

	inline Quaternion Delta(const Vector3& unitAxis, float pitchDegrees)
	{
		constexpr float radiansPerDegree = 3.14159265358979323846f / 180.f;
		const float half = pitchDegrees * radiansPerDegree * .5f;
		const float s = std::sin(half);
		return {unitAxis.x * s, unitAxis.y * s, unitAxis.z * s, std::cos(half)};
	}

	inline constexpr float InvalidMinimumDistance = -(std::numeric_limits<float>::max)();

	inline bool ValidObjectivePose(const SoleFootPose& pose, const SoleMeasurementSettings& settings, int side)
	{
		return side >= 0 && side <= 1 && settings.enabled && settings.useBootProxy && settings.planeCalibrated &&
			settings.IsValid() && pose.valid && sole::Finite(pose.hip) && sole::Finite(pose.knee) &&
			sole::Finite(pose.ankle) && sole::Finite(pose.toe) && sole::ValidRotation(pose.footRotation) &&
			std::fabs(settings.planeNormal.x) <= 1e-5f && std::fabs(settings.planeNormal.z) <= 1e-5f &&
			std::fabs(settings.planeNormal.y - 1.f) <= 1e-5f;
	}

	inline float MinimumDistanceObjective(const SoleFootPose& pose, const SoleMeasurementSettings& settings,
	                                      int side, bool useMeshEnvelope)
	{
		if (!ValidObjectivePose(pose, settings, side)) return InvalidMinimumDistance;
		const Vector3* meshPoints = nullptr;
		size_t count = 8;
		if (useMeshEnvelope)
		{
			meshPoints = side == 0 ? sole_pitch_envelope::LeftPoints : sole_pitch_envelope::RightPoints;
			count = side == 0 ? sole_pitch_envelope::LeftPointCount : sole_pitch_envelope::RightPointCount;
			if (!meshPoints || count < 4 || count > 128) return InvalidMinimumDistance;
		}
		const auto rotation = pose.footRotation.Normalized();
		float minimum = (std::numeric_limits<float>::max)();
		for (size_t index = 0; index < count; ++index)
		{
			const auto point = useMeshEnvelope
				                   ? meshPoints[index]
				                   : sole::BootProxyCorner(side, int(index / 4), int(index % 4));
			if (!sole::Finite(point)) return InvalidMinimumDistance;
			const auto world = pose.ankle + rotation.Rotate(point);
			const float distance = sole::Dot(settings.planeNormal, world) - settings.planeOffsetM;
			if (!sole::Finite(world) || !std::isfinite(distance)) return InvalidMinimumDistance;
			minimum = (std::min)(minimum, distance);
		}
		return minimum;
	}

	// Readback measurement does not require the correction switch to be on. The
	// finite negative sentinel lets callers fail closed rather than show a guessed
	// clearance; compare with InvalidMinimumDistance before publishing telemetry.
	inline float MinimumDistance(const SoleFootPose& pose, const SoleMeasurementSettings& settings,
	                             int side, const Settings& experiment)
	{
		if (!experiment.IsValid()) return InvalidMinimumDistance;
		return MinimumDistanceObjective(pose, settings, side, experiment.useMeshEnvelope);
	}

	// The objective is the lowest signed distance among either all eight original
	// proxy corners or the opt-in actual low-shoe hull. The latter preserves every
	// directional minimum of its frozen source points, avoiding rectangular corner
	// overestimation. Ankle position, leg pose, and the horizontal floor stay
	// fixed. Zero is always a candidate: if rotation cannot improve penetration,
	// the result is the original pose, even when all allowed angles still collide.
	inline Result Solve(const SoleFootPose& pose, const SoleMeasurementSettings& settings,
	                    int side, float maxPitchDegrees, bool useMeshEnvelope = false)
	{
		Result result;
		const Settings bounds{false, maxPitchDegrees, useMeshEnvelope};
		if (!bounds.IsValid() || !ValidObjectivePose(pose, settings, side) ||
			!LocalPitchAxis(side, result.localAxis))
			return result;
		const auto original = pose.footRotation.Normalized();
		auto minimumDistance = [&](float angle)
		{
			auto candidate = pose;
			candidate.footRotation = (original * Delta(result.localAxis, angle)).Normalized();
			return MinimumDistanceObjective(candidate, settings, side, useMeshEnvelope);
		};
		result.beforeMinimumDistanceM = minimumDistance(0.f);
		if (result.beforeMinimumDistanceM == InvalidMinimumDistance) return result;
		result.valid = true;
		result.usedMeshEnvelope = useMeshEnvelope;
		result.correctedWorldRotation = original;
		result.afterMinimumDistanceM = result.beforeMinimumDistanceM;
		result.beforePenetrationM = (std::max)(0.f, -result.beforeMinimumDistanceM);
		result.afterPenetrationM = result.beforePenetrationM;
		result.fullyCleared = result.beforeMinimumDistanceM >= 0.f;
		if (result.fullyCleared || maxPitchDegrees == 0.f) return result;

		// Bounded coarse search in both pitch directions. If a clearing angle is
		// found, bracket its first zero crossing and choose the smallest |pitch|.
		// Otherwise refine the best sampled interval to reduce residual collision.
		constexpr int steps = 80;
		const float step = maxPitchDegrees / steps;
		float bestAngle = 0.f, bestDistance = result.beforeMinimumDistanceM;
		float clearingAngle = 0.f;
		bool clearingFound = false;
		auto consider = [&](float angle)
		{
			const float distance = minimumDistance(angle);
			if (distance > bestDistance + 1e-7f ||
				(std::fabs(distance - bestDistance) <= 1e-7f && std::fabs(angle) < std::fabs(bestAngle)))
			{
				bestAngle = angle;
				bestDistance = distance;
			}
			return distance;
		};
		for (int direction : {-1, 1})
		{
			float previous = 0.f;
			bool directionCleared = false;
			for (int i = 1; i <= steps; ++i)
			{
				const float angle = direction * (std::min)(maxPitchDegrees, step * i);
				const float distance = consider(angle);
				if (!directionCleared && distance >= 0.f)
				{
					float lo = previous, hi = angle;
					for (int iteration = 0; iteration < 24; ++iteration)
					{
						const float mid = .5f * (lo + hi);
						if (minimumDistance(mid) >= 0.f) hi = mid;
						else lo = mid;
					}
					if (!clearingFound || std::fabs(hi) < std::fabs(clearingAngle))
					{
						clearingFound = true;
						clearingAngle = hi;
					}
					directionCleared = true;
				}
				previous = angle;
			}
		}
		if (clearingFound)
		{
			bestAngle = clearingAngle;
			bestDistance = minimumDistance(bestAngle);
		}
		else
		{
			float lo = (std::max)(-maxPitchDegrees, bestAngle - step);
			float hi = (std::min)(maxPitchDegrees, bestAngle + step);
			for (int iteration = 0; iteration < 24; ++iteration)
			{
				const float third = (hi - lo) / 3.f;
				const float a = lo + third, b = hi - third;
				if (consider(a) < consider(b)) lo = a;
				else hi = b;
			}
			consider(.5f * (lo + hi));
		}
		// Ignore sub-micrometre numerical differences; never worsen a collision or
		// issue a meaningless zero delta. This is not support detection or locking.
		if (bestDistance < result.beforeMinimumDistanceM + 1e-6f || std::fabs(bestAngle) < 1e-5f) return result;
		result.actionable = true;
		result.pitchDegrees = bestAngle;
		result.localDelta = Delta(result.localAxis, bestAngle);
		result.correctedWorldRotation = (original * result.localDelta).Normalized();
		result.afterMinimumDistanceM = bestDistance;
		result.afterPenetrationM = (std::max)(0.f, -bestDistance);
		result.fullyCleared = bestDistance >= 0.f;
		return result;
	}

	inline Result Solve(const SoleFootPose& pose, const SoleMeasurementSettings& soleSettings,
	                    int side, const Settings& experiment)
	{
		if (!experiment.experimentalSolePitchCorrection || !experiment.IsValid()) return {};
		return Solve(pose, soleSettings, side, experiment.maxPitchDegrees, experiment.useMeshEnvelope);
	}
}
