#pragma once

#include "GkMotionData.hpp"
#include <cmath>

namespace GakuMotion::waist_support
{
	// Opt-in runtime callers must verify the exact Waist_O -> Waist_H hierarchy,
	// obtain Thigh_O from the same actor, and gate the effective scale separately.
	// This pure solver performs no binding, writing, or feature enablement.
	// Evidence: build/joint_axis_hips/hip_geometry_truth.json (2026-10-14).
	// Three reference recordings, 7852 valid rig frames, establish the midpoint and
	// down-to-thigh laws (two jsna takes are GakuMotion-inactive; Saki provenance is
	// documented in handoff #045). This is not rendered mesh/visual acceptance.
	struct Result
	{
		Vector3 localPosition;
		Quaternion localRotation;
	};

	inline bool Finite(const Vector3& value) noexcept
	{
		return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
	}

	// Check effective world scales at Waist_O and its parent chain, not only an
	// isolated localScale: rotation-only inverse position is valid at unit scale.
	inline bool IsUnitScale(const Vector3& scale) noexcept
	{
		constexpr float tolerance = 1e-4f;
		return Finite(scale) && std::fabs(scale.x - 1.f) <= tolerance &&
			std::fabs(scale.y - 1.f) <= tolerance && std::fabs(scale.z - 1.f) <= tolerance;
	}

	// Returns the absolute local pose to write on Waist_H, whose direct parent must
	// be Waist_O. In particular, this result is not a delta to multiply or accumulate.
	// The caller owns the unit-scale gate; no scale argument can be inferred here.
	inline bool Solve(const Vector3& waistWorldPosition,
	                  const Quaternion& waistWorldRotation,
	                  const Vector3& thighWorldPosition,
	                  Result& out) noexcept
	{
		out = Result{};
		if (!Finite(waistWorldPosition) || !Finite(thighWorldPosition) ||
			!std::isfinite(waistWorldRotation.x) || !std::isfinite(waistWorldRotation.y) ||
			!std::isfinite(waistWorldRotation.z) || !std::isfinite(waistWorldRotation.w))
			return false;

		// Unity rotations are unit quaternions. Allow capture/float rounding, but
		// reject invalid state rather than silently replacing it with identity.
		double qx = waistWorldRotation.x, qy = waistWorldRotation.y;
		double qz = waistWorldRotation.z, qw = waistWorldRotation.w;
		const double normSquared = qx * qx + qy * qy + qz * qz + qw * qw;
		if (!std::isfinite(normSquared) || std::fabs(normSquared - 1.) > 1e-3)
			return false;
		const double inverseNorm = 1. / std::sqrt(normSquared);
		qx *= -inverseNorm;
		qy *= -inverseNorm;
		qz *= -inverseNorm;
		qw *= inverseNorm;

		// Work in double until the result is validated, avoiding overflow in the
		// subtraction and preserving a usable fail-closed contract for bad inputs.
		const double dx = double(thighWorldPosition.x) - waistWorldPosition.x;
		const double dy = double(thighWorldPosition.y) - waistWorldPosition.y;
		const double dz = double(thighWorldPosition.z) - waistWorldPosition.z;
		const double tx = 2. * (qy * dz - qz * dy);
		const double ty = 2. * (qz * dx - qx * dz);
		const double tz = 2. * (qx * dy - qy * dx);
		const double vx = dx + qw * tx + qy * tz - qz * ty;
		const double vy = dy + qw * ty + qz * tx - qx * tz;
		const double vz = dz + qw * tz + qx * ty - qy * tx;
		const double distanceSquared = vx * vx + vy * vy + vz * vz;
		// Below ten micrometres the reference pair does not define a stable axis.
		if (!std::isfinite(distanceSquared) || distanceSquared < 1e-10)
			return false;
		const double inverseDistance = 1. / std::sqrt(distanceSquared);
		const double nx = vx * inverseDistance, ny = vy * inverseDistance, nz = vz * inverseDistance;

		// FromToRotation(down, direction): cross=(-nz,0,nx), w=1-ny.
		// Near the antiparallel singularity no unique bend plane is established by
		// these two points. Decline the pose rather than selecting an arbitrary axis.
		const double scalar = 1. - ny;
		if (scalar <= 1e-6)
			return false;
		const double inverseRotationNorm = 1. / std::sqrt(nx * nx + nz * nz + scalar * scalar);

		Result candidate;
		candidate.localPosition = {float(.5 * vx), float(.5 * vy), float(.5 * vz)};
		candidate.localRotation = {
			float(-nz * inverseRotationNorm), 0.f,
			float(nx * inverseRotationNorm), float(scalar * inverseRotationNorm)
		};
		if (!Finite(candidate.localPosition) || !std::isfinite(candidate.localRotation.x) ||
			!std::isfinite(candidate.localRotation.z) || !std::isfinite(candidate.localRotation.w))
			return false;
		out = candidate;
		return true;
	}
}
