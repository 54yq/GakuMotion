#pragma once

#include "Sampling.hpp"
#include <new>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace KineTexture
{
	using BoneId = std::uint64_t; // External identity; never an array index.
	enum class Handedness { Right, Left };
	enum class Axis { PositiveX, NegativeX, PositiveY, NegativeY, PositiveZ, NegativeZ };
	enum class LengthUnit { Meters };
	enum class RootFrame { Model, World };
	enum class RotationRepresentation { LocalAbsolute, RestRelative, WorldAbsolute };

	struct CoordinateBasis
	{
		Handedness handedness{Handedness::Right};
		Axis up{Axis::PositiveY};
		Axis forward{Axis::PositiveZ};
		bool operator==(const CoordinateBasis&) const = default;
	};

	// Coordinates are declarations, not conversion requests. All supplied vectors
	// and Hamilton-active quaternions must already use this same numeric basis.
	struct JointDescriptor
	{
		BoneId id{0};
		std::string name; // Optional; nonempty names must be unique.
		std::int32_t parent{-1};
		Vector3 rest_offset;
		Quaternion rest_rotation;
		Vector3 primary_axis{1, 0, 0};
		Vector3 secondary_axis{0, 1, 0};
	};

	struct SkeletonDescriptor
	{
		std::string id;
		std::uint32_t revision{1};
		CoordinateBasis basis;
		LengthUnit length_unit{LengthUnit::Meters};
		bool allow_multiple_roots{false};
		std::vector<JointDescriptor> joints;
	};

	struct PoseSchema
	{
		std::string skeleton_id;
		std::uint32_t skeleton_revision{1};
		std::uint32_t revision{1};
		CoordinateBasis basis;
		LengthUnit length_unit{LengthUnit::Meters};
		RootFrame root_frame{RootFrame::Model};
		RotationRepresentation rotations{RotationRepresentation::LocalAbsolute};
		bool operator==(const PoseSchema&) const = default;
	};

	struct PoseFrame
	{
		PoseSchema schema;
		double time_seconds{0};
		std::uint64_t sequence{0};
		Vector3 root_translation;
		std::vector<Quaternion> joint_rotations;
		// Empty means descriptor rest offsets; otherwise these REPLACE rest offsets.
		// RotationRepresentation changes rotation semantics only, never translation.
		std::vector<Vector3> local_translations;
		std::vector<std::uint8_t> valid; // Empty = all valid; otherwise exactly 0/1.
		// Optional scalar tracks carry caller-owned semantics (no target constants).
		std::vector<std::string> channel_names;
		std::vector<float> channel_values;
	};

	struct FkOutputView
	{
		std::span<Vector3> positions;
		std::span<Quaternion> rotations;
		std::span<std::uint8_t> valid;
	};

	class FkPlan;
	class FkContext
	{
		friend class FkPlan;
		const FkPlan* owner_{nullptr};
		std::uint64_t generation_{0};
		std::vector<Vector3> positions_;
		std::vector<Quaternion> rotations_;
		std::vector<std::uint8_t> valid_;
	public:
		std::size_t JointCount() const noexcept { return positions_.size(); }
	};

	inline bool ValidBasis(const CoordinateBasis& b) noexcept
	{
		const int u = static_cast<int>(b.up), f = static_cast<int>(b.forward);
		return (b.handedness == Handedness::Right || b.handedness == Handedness::Left) &&
			u >= 0 && u <= 5 && f >= 0 && f <= 5 && u / 2 != f / 2;
	}

	inline bool ValidAxes(const Vector3& a, const Vector3& b) noexcept
	{
		if (!IsFinite(a) || !IsFinite(b)) return false;
		const double aa = double(a.x) * a.x + double(a.y) * a.y + double(a.z) * a.z;
		const double bb = double(b.x) * b.x + double(b.y) * b.y + double(b.z) * b.z;
		const double ab = double(a.x) * b.x + double(a.y) * b.y + double(a.z) * b.z;
		return aa > 1e-12 && bb > 1e-12 && (aa * bb - ab * ab) > 1e-10 * aa * bb;
	}

	// Prepare owns descriptor/schema copies and a parents-first order. It does no
	// string lookup during Evaluate, and failed Prepare preserves the previous plan.
	// A prepared plan may be shared read-only; each consumer needs its own context.
	class FkPlan
	{
		SkeletonDescriptor skeleton_;
		PoseSchema schema_;
		std::vector<std::size_t> order_;
		std::uint64_t generation_{0};

	public:
		std::size_t JointCount() const noexcept { return skeleton_.joints.size(); }
		std::span<const std::size_t> EvaluationOrder() const noexcept { return order_; }
		const PoseSchema& Schema() const noexcept { return schema_; }

		Status Prepare(const SkeletonDescriptor& skeleton, const PoseSchema& schema,
			std::size_t maxJoints = 65536)
		{
			const std::size_t n = skeleton.joints.size();
			if (!n) return {ErrorCode::EmptyInput};
			if (n > maxJoints || n > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
				return {ErrorCode::BudgetExceeded};
			if (skeleton.id.empty() || !skeleton.revision || schema.revision != 1 ||
				skeleton.length_unit != LengthUnit::Meters || schema.length_unit != LengthUnit::Meters ||
				!ValidBasis(skeleton.basis) || !ValidBasis(schema.basis) ||
				(schema.root_frame != RootFrame::Model && schema.root_frame != RootFrame::World) ||
				(schema.rotations != RotationRepresentation::LocalAbsolute &&
				 schema.rotations != RotationRepresentation::RestRelative &&
				 schema.rotations != RotationRepresentation::WorldAbsolute))
				return {ErrorCode::InvalidSchema};
			if (schema.skeleton_id != skeleton.id || schema.skeleton_revision != skeleton.revision ||
				!(schema.basis == skeleton.basis)) return {ErrorCode::SchemaMismatch};
			try
			{
				std::unordered_set<BoneId> ids;
				std::unordered_set<std::string> names;
				std::size_t roots = 0;
				for (std::size_t i = 0; i < n; ++i)
				{
					const auto& j = skeleton.joints[i];
					if (j.parent < -1 || j.parent >= static_cast<std::int32_t>(n) || j.parent == static_cast<std::int32_t>(i))
						return {ErrorCode::InvalidParent, i};
					if (j.parent == -1) ++roots;
					if (!ids.insert(j.id).second) return {ErrorCode::DuplicateId, i};
					if (!j.name.empty() && !names.insert(j.name).second) return {ErrorCode::DuplicateName, i};
					if (!IsFinite(j.rest_offset)) return {ErrorCode::NonFinite, i};
					if (!IsUnitRotation(j.rest_rotation)) return {ErrorCode::InvalidRotation, i};
					if (!ValidAxes(j.primary_axis, j.secondary_axis)) return {ErrorCode::InvalidAxes, i};
				}
				if (roots > 1 && !skeleton.allow_multiple_roots) return {ErrorCode::InvalidParent};
				std::vector<std::uint8_t> state(n, 0);
				std::vector<std::size_t> stack, order;
				stack.reserve(n);
				order.reserve(n);
				for (std::size_t start = 0; start < n; ++start)
				{
					std::int32_t at = static_cast<std::int32_t>(start);
					while (at >= 0 && state[at] == 0)
					{
						state[at] = 1;
						stack.push_back(static_cast<std::size_t>(at));
						at = skeleton.joints[at].parent;
					}
					if (at >= 0 && state[at] == 1) return {ErrorCode::Cycle, static_cast<std::size_t>(at)};
					while (!stack.empty())
					{
						const std::size_t i = stack.back();
						stack.pop_back();
						state[i] = 2;
						order.push_back(i);
					}
				}
				SkeletonDescriptor nextSkeleton = skeleton;
				PoseSchema nextSchema = schema;
				skeleton_ = std::move(nextSkeleton);
				schema_ = std::move(nextSchema);
				order_ = std::move(order);
				++generation_;
				return {};
			}
			catch (const std::bad_alloc&) { return {ErrorCode::AllocationFailed}; }
		}

		Status PrepareContext(FkContext& context) const
		{
			if (!generation_) return {ErrorCode::NotPrepared};
			try
			{
				FkContext next;
				next.positions_.resize(JointCount());
				next.rotations_.resize(JointCount());
				next.valid_.resize(JointCount());
				next.owner_ = this;
				next.generation_ = generation_;
				context = std::move(next);
				return {};
			}
			catch (const std::bad_alloc&) { return {ErrorCode::AllocationFailed}; }
		}

		// Output is in schema.root_frame. Root translation is added once to each
		// root; no undeclared host/actor rotation is injected. Missing children remain
		// invalid, and a valid child with a missing parent fails. Invalid output
		// slots are zero position/identity rotation with valid=0; always use the mask.
		// Any failure leaves caller output untouched. No allocation after prepare.
		Status Evaluate(const PoseFrame& frame, FkContext& context, FkOutputView output) const noexcept
		{
			if (!generation_) return {ErrorCode::NotPrepared};
			const std::size_t n = JointCount();
			if (context.owner_ != this || context.generation_ != generation_ || context.JointCount() != n)
				return {ErrorCode::ContextMismatch};
			if (!(frame.schema == schema_)) return {ErrorCode::SchemaMismatch};
			if (!std::isfinite(frame.time_seconds)) return {ErrorCode::InvalidTime};
			if (!IsFinite(frame.root_translation)) return {ErrorCode::NonFinite};
			if (frame.joint_rotations.size() != n || (!frame.local_translations.empty() && frame.local_translations.size() != n) ||
				(!frame.valid.empty() && frame.valid.size() != n) || output.positions.size() != n || output.rotations.size() != n ||
				output.valid.size() != n || frame.channel_names.size() != frame.channel_values.size()) return {ErrorCode::InvalidSize};
			for (std::size_t i = 0; i < frame.channel_values.size(); ++i)
				if (!std::isfinite(frame.channel_values[i])) return {ErrorCode::NonFinite, i};
			for (std::size_t i = 0; i < n; ++i)
			{
				if (!frame.valid.empty() && frame.valid[i] > 1) return {ErrorCode::InvalidMask, i};
				const bool valid = frame.valid.empty() || frame.valid[i] == 1;
				if (!valid) continue;
				if (!IsUnitRotation(frame.joint_rotations[i])) return {ErrorCode::InvalidRotation, i};
				if (!frame.local_translations.empty() && !IsFinite(frame.local_translations[i])) return {ErrorCode::NonFinite, i};
				const auto p = skeleton_.joints[i].parent;
				if (p >= 0 && !frame.valid.empty() && frame.valid[p] != 1) return {ErrorCode::MissingParent, i};
			}
			for (const std::size_t i : order_)
			{
				const auto& joint = skeleton_.joints[i];
				context.valid_[i] = frame.valid.empty() ? 1 : frame.valid[i];
				if (!context.valid_[i])
				{
					context.positions_[i] = {};
					context.rotations_[i] = Quaternion::Identity();
					continue;
				}
				const Vector3 offset = frame.local_translations.empty() ? joint.rest_offset : frame.local_translations[i];
				const Quaternion parent = joint.parent < 0 ? Quaternion::Identity() : context.rotations_[joint.parent];
				const Vector3 parentPosition = joint.parent < 0 ? frame.root_translation : context.positions_[joint.parent];
				context.positions_[i] = parentPosition + parent.Rotate(offset);
				const Quaternion value = frame.joint_rotations[i];
				if (schema_.rotations == RotationRepresentation::WorldAbsolute) context.rotations_[i] = value;
				else
				{
					const Quaternion local = schema_.rotations == RotationRepresentation::RestRelative ? joint.rest_rotation * value : value;
					context.rotations_[i] = parent * local;
				}
				if (!IsFinite(context.positions_[i]) || !IsFinite(context.rotations_[i])) return {ErrorCode::NonFinite, i};
			}
			std::copy(context.positions_.begin(), context.positions_.end(), output.positions.begin());
			std::copy(context.rotations_.begin(), context.rotations_.end(), output.rotations.begin());
			std::copy(context.valid_.begin(), context.valid_.end(), output.valid.begin());
			return {};
		}
	};
}
