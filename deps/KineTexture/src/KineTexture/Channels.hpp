#pragma once
#include "Sampling.hpp"
#include <string>

namespace KineTexture
{
	enum class ChannelValueType { Scalar, Vector3, RotationXyzw };
	enum class ChannelRepresentation { Absolute, Delta };
	struct Timebase
	{
		float sample_rate_hz{0};
		float origin_seconds{0};
	};
	struct Provenance
	{
		std::string source_id;
		std::string source_digest; // Optional evidence, never inferred from a path.
		std::string source_channel;
		std::string operation{"source"};
	};
	struct ChannelSemantics
	{
		std::string name;
		ChannelValueType value_type{ChannelValueType::Scalar};
		ChannelRepresentation representation{ChannelRepresentation::Absolute};
		std::string unit;
		std::string space; // Caller-defined frame/semantic domain, no engine default.
		std::string reference_id; // Required for deltas; identity is still available data.
	};
	inline Status ValidateTimebase(const Timebase& clock) noexcept
	{
		if (!std::isfinite(clock.sample_rate_hz) || clock.sample_rate_hz <= 0)
			return {ErrorCode::InvalidRate};
		if (!std::isfinite(clock.origin_seconds)) return {ErrorCode::InvalidTime};
		return {};
	}
	inline bool HasText(const std::string& text) noexcept
	{
		return text.find_first_not_of(" \t\r\n") != std::string::npos;
	}
	inline Status ValidateChannelSemantics(const ChannelSemantics& channel) noexcept
	{
		if (!HasText(channel.name) || !HasText(channel.unit) || !HasText(channel.space) ||
			(channel.value_type != ChannelValueType::Scalar && channel.value_type != ChannelValueType::Vector3 &&
			 channel.value_type != ChannelValueType::RotationXyzw) ||
			(channel.representation != ChannelRepresentation::Absolute && channel.representation != ChannelRepresentation::Delta) ||
			(channel.representation == ChannelRepresentation::Delta && !HasText(channel.reference_id)))
			return {ErrorCode::InvalidSchema};
		return {};
	}
	inline Status ValidateProvenance(const Provenance& provenance) noexcept
	{
		if (!HasText(provenance.source_id) || !HasText(provenance.operation)) return {ErrorCode::InvalidSchema};
		return {};
	}
}
