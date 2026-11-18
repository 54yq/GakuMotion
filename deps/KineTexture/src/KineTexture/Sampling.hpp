#pragma once

#include "Math.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace KineTexture
{
	enum class ErrorCode
	{
		None, EmptyInput, InvalidSize, InvalidRate, InvalidTime, NonFinite,
		InvalidRotation, InvalidParent, Cycle, DuplicateId, DuplicateName,
		InvalidAxes, InvalidSchema, SchemaMismatch, InvalidMask, MissingParent,
		NotPrepared, ContextMismatch, AllocationFailed, BudgetExceeded
	};

	struct Status
	{
		ErrorCode code{ErrorCode::None};
		std::size_t index{0};
		explicit constexpr operator bool() const noexcept { return code == ErrorCode::None; }
	};

	inline bool IsFinite(const Vector3& v) noexcept
	{
		return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
	}

	inline bool IsFinite(const Quaternion& q) noexcept
	{
		return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
	}

	inline bool IsUnitRotation(const Quaternion& q, double normTolerance = 1e-3) noexcept
	{
		if (!IsFinite(q)) return false;
		const double n = double(q.x) * q.x + double(q.y) * q.y + double(q.z) * q.z + double(q.w) * q.w;
		return n > 1e-12 && std::abs(std::sqrt(n) - 1.0) <= normTolerance;
	}

	struct DenseClipView
	{
		float fps{30.0f};
		std::size_t frame_count{0};
		std::size_t joint_count{0};
		std::span<const Vector3> root_positions;
		std::span<const Quaternion> joint_rotations; // frame-major, xyzw
	};

	inline Status ValidateDenseClipShape(const DenseClipView& clip) noexcept
	{
		if (!std::isfinite(clip.fps) || clip.fps <= 0) return {ErrorCode::InvalidRate};
		if (!clip.frame_count || !clip.joint_count) return {ErrorCode::EmptyInput};
		if (clip.frame_count > std::numeric_limits<std::size_t>::max() / clip.joint_count)
			return {ErrorCode::InvalidSize};
		if (clip.root_positions.size() != clip.frame_count ||
			clip.joint_rotations.size() != clip.frame_count * clip.joint_count)
			return {ErrorCode::InvalidSize};
		return {};
	}

	// Ingestion-time scan. SampleDensePose intentionally does not rescan a clip.
	inline Status ValidateDenseClip(const DenseClipView& clip) noexcept
	{
		if (const Status s = ValidateDenseClipShape(clip); !s) return s;
		for (std::size_t i = 0; i < clip.root_positions.size(); ++i)
			if (!IsFinite(clip.root_positions[i])) return {ErrorCode::NonFinite, i};
		for (std::size_t i = 0; i < clip.joint_rotations.size(); ++i)
			if (!IsUnitRotation(clip.joint_rotations[i])) return {ErrorCode::InvalidRotation, i};
		return {};
	}

	// Time is in seconds. Negative time holds frame zero; past-end holds the last
	// frame. NaN/Inf, malformed shapes and short outputs fail without modification.
	// Input must already have passed ValidateDenseClip; output must not alias input.
	inline Status SampleDensePose(const DenseClipView& clip, float timeSeconds,
		Vector3& outRootPos, std::span<Quaternion> outRotations) noexcept
	{
		if (const Status s = ValidateDenseClipShape(clip); !s) return s;
		if (!std::isfinite(timeSeconds)) return {ErrorCode::InvalidTime};
		if (outRotations.size() != clip.joint_count) return {ErrorCode::InvalidSize};

		// Arithmetic and evaluation order match the original dense scalar sampler.
		float frame_float = std::max(0.0f, timeSeconds * clip.fps);
		std::size_t f0;
		float t;
		// Clamp BEFORE integer conversion: huge finite times (and multiplication
		// overflow to infinity) must not cause an out-of-range float-to-int cast.
		if (frame_float >= static_cast<float>(clip.frame_count - 1))
		{
			f0 = clip.frame_count - 1;
			t = 0.0f;
		}
		else
		{
			f0 = static_cast<std::size_t>(frame_float);
			t = frame_float - static_cast<float>(f0);
		}
		std::size_t f1 = std::min(f0 + 1, clip.frame_count - 1);
		outRootPos = Vector3::Lerp(clip.root_positions[f0], clip.root_positions[f1], t);
		for (std::size_t b = 0; b < clip.joint_count; ++b)
		{
			const auto& q0 = clip.joint_rotations[f0 * clip.joint_count + b];
			const auto& q1 = clip.joint_rotations[f1 * clip.joint_count + b];
			outRotations[b] = Quaternion::Slerp(q0, q1, t);
		}
		return {};
	}
}
