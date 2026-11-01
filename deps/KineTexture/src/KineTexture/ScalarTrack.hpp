#pragma once
#include "Channels.hpp"
#include <vector>

namespace KineTexture
{
	enum class ScalarKeyEvaluation { Exact, LinearExpression };
	// Numeric frame-major view. The adapter binds its column order to channel
	// semantics; the view does not infer names, weight ranges or source FPS.
	struct DenseScalarTracksView
	{
		float fps{0};
		std::size_t frame_count{0}, channel_count{0};
		std::span<const float> values;
		std::span<const std::uint8_t> valid; // Empty = all present. Otherwise per sample.
		float origin_seconds{0};
	};
	inline Status ValidateDenseScalarTracksShape(const DenseScalarTracksView& track) noexcept
	{
		if (const auto s = ValidateTimebase({track.fps, track.origin_seconds}); !s) return s;
		if (!track.frame_count || !track.channel_count) return {ErrorCode::EmptyInput};
		if (track.frame_count > std::numeric_limits<std::size_t>::max() / track.channel_count)
			return {ErrorCode::InvalidSize};
		const auto n = track.frame_count * track.channel_count;
		if (track.values.size() != n || (!track.valid.empty() && track.valid.size() != n))
			return {ErrorCode::InvalidSize};
		return {};
	}
	inline Status ValidateDenseScalarTracks(const DenseScalarTracksView& track) noexcept
	{
		if (const auto s = ValidateDenseScalarTracksShape(track); !s) return s;
		for (std::size_t i = 0; i < track.values.size(); ++i)
		{
			if (!track.valid.empty() && track.valid[i] > 1) return {ErrorCode::InvalidMask, i};
			if ((track.valid.empty() || track.valid[i]) && !std::isfinite(track.values[i]))
				return {ErrorCode::NonFinite, i};
		}
		return {};
	}
	// Endpoints hold. At an exact key only that key is required; interpolation
	// never bridges an unavailable sample. Missing values yield valid=0 and a
	// zero sentinel; an authored zero yields valid=1. No clamping of value range.
	// Buffers must not overlap input, and ingestion must validate the full view.
	inline Status SampleDenseScalarTracks(const DenseScalarTracksView& track, float seconds,
		std::span<float> output, std::span<std::uint8_t> outputValid = {},
		ScalarKeyEvaluation keyEvaluation = ScalarKeyEvaluation::Exact) noexcept
	{
		if (const auto s = ValidateDenseScalarTracksShape(track); !s) return s;
		if (!std::isfinite(seconds)) return {ErrorCode::InvalidTime};
		if (keyEvaluation != ScalarKeyEvaluation::Exact && keyEvaluation != ScalarKeyEvaluation::LinearExpression)
			return {ErrorCode::InvalidSchema};
		if (output.size() != track.channel_count ||
			(!outputValid.empty() && outputValid.size() != track.channel_count) ||
			(!track.valid.empty() && outputValid.empty())) return {ErrorCode::InvalidSize};
		const float coordinate = std::clamp((seconds - track.origin_seconds) * track.fps,
			0.0f, static_cast<float>(track.frame_count - 1));
		std::size_t a;
		float t;
		if (coordinate >= static_cast<float>(track.frame_count - 1))
		{
			a = track.frame_count - 1;
			t = coordinate - static_cast<float>(a);
		}
		else
		{
			a = static_cast<std::size_t>(coordinate);
			t = coordinate - static_cast<float>(a);
		}
		const auto b = std::min(a + 1, track.frame_count - 1);
		// Preflight all reads before changing either caller buffer.
		for (std::size_t c = 0; c < track.channel_count; ++c)
		{
			const auto ia = a * track.channel_count + c, ib = b * track.channel_count + c;
			if (!track.valid.empty() && (track.valid[ia] > 1 || track.valid[ib] > 1)) return {ErrorCode::InvalidMask, c};
			if ((track.valid.empty() || track.valid[ia]) && !std::isfinite(track.values[ia])) return {ErrorCode::NonFinite, c};
			if (t != 0 && (track.valid.empty() || track.valid[ib]) && !std::isfinite(track.values[ib])) return {ErrorCode::NonFinite, c};
			const bool present = track.valid.empty() || (track.valid[ia] && (t == 0 || track.valid[ib]));
			if (present)
			{
				const float value = track.values[ia];
				const float next = t == 0 && (keyEvaluation == ScalarKeyEvaluation::Exact || !track.valid.empty())
					? value : value + (track.values[ib] - value) * t;
				if (!std::isfinite(next)) return {ErrorCode::NonFinite, c};
			}
		}
		for (std::size_t c = 0; c < track.channel_count; ++c)
		{
			const auto ia = a * track.channel_count + c, ib = b * track.channel_count + c;
			const bool present = track.valid.empty() || (track.valid[ia] && (t == 0 || track.valid[ib]));
			if (!outputValid.empty()) outputValid[c] = present ? 1 : 0;
			if (!present) { output[c] = 0; continue; }
			// Retain Gaku's original float expression, including the terminal hold.
			const float value = track.values[ia];
			output[c] = t == 0 && (keyEvaluation == ScalarKeyEvaluation::Exact || !track.valid.empty())
				? value : value + (track.values[ib] - value) * t;
		}
		return {};
	}
	struct ScalarTrack
	{
		ChannelSemantics semantics;
		Timebase timebase;
		Provenance provenance;
		std::vector<float> values;
		std::vector<std::uint8_t> valid;
		DenseScalarTracksView View() const noexcept
		{
			return {timebase.sample_rate_hz, values.size(), 1, values, valid, timebase.origin_seconds};
		}
		Status Validate() const noexcept
		{
			if (const auto s = ValidateChannelSemantics(semantics); !s) return s;
			if (semantics.value_type != ChannelValueType::Scalar) return {ErrorCode::InvalidSchema};
			if (const auto s = ValidateProvenance(provenance); !s) return s;
			if (const auto s = ValidateTimebase(timebase); !s) return s;
			if (values.empty()) return valid.empty() ? Status{} : Status{ErrorCode::InvalidSize};
			return ValidateDenseScalarTracks(View());
		}
		Status Sample(float seconds, float& value, std::uint8_t& present) const noexcept
		{
			if (const auto s = Validate(); !s) return s;
			if (!std::isfinite(seconds)) return {ErrorCode::InvalidTime};
			if (values.empty()) { value = 0; present = 0; return {}; }
			return SampleDenseScalarTracks(View(), seconds, {&value, 1}, {&present, 1});
		}
	};
}
