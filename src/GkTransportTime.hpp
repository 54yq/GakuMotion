#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <string_view>

namespace GakuMotion
{
	struct TransportNavigationEditState
	{
		char time[64]{};
		char frame[32]{};
		std::string motionFile;
		uint64_t generation{0};
		uint64_t injectionSession{0};
		bool bound{false};
		bool editingTime{false}, editingFrame{false};
		std::string error;
	};

	inline bool ValidTransportTimeline(uint32_t count, float fps) noexcept
	{
		return count > 0 && std::isfinite(fps) && fps > 0.0f &&
			static_cast<double>(count - 1) / fps <= (std::numeric_limits<float>::max)();
	}

	inline double TransportLastFrameTime(uint32_t count, float fps) noexcept
	{
		return ValidTransportTimeline(count, fps) ? static_cast<double>(count - 1) / fps : 0.0;
	}

	inline bool TransportDurationMismatch(uint32_t count, float fps, float duration) noexcept
	{
		if (!ValidTransportTimeline(count, fps) || !std::isfinite(duration) || duration < 0) return true;
		const double expected = static_cast<double>(count) / fps;
		const double tolerance = (std::max)(1e-4, expected * std::numeric_limits<float>::epsilon() * 2.0);
		return std::fabs(expected - duration) > tolerance;
	}

	// Undo only float time-storage noise near a sample. Arbitrary between-sample
	// times stay continuous; frame stepping never adds 1/fps repeatedly to a float.
	inline double TransportFramePosition(float seconds, float fps, uint32_t count) noexcept
	{
		if (!ValidTransportTimeline(count, fps) || !std::isfinite(seconds)) return 0.0;
		double position = std::clamp(static_cast<double>(seconds) * fps, 0.0, static_cast<double>(count - 1));
		const double nearest = std::round(position);
		const double tolerance = (std::min)(0.125, (std::max)(1e-6,
			std::fabs(position) * std::numeric_limits<float>::epsilon() * 2.0));
		if (std::fabs(position - nearest) <= tolerance) position = nearest;
		return position;
	}

	inline int64_t TransportSteppedFrame(float seconds, float fps, uint32_t count, int delta) noexcept
	{
		double position = TransportFramePosition(seconds, fps, count);
		// A time in the terminal hold is after the last sample, so one backward
		// step first reaches that sample rather than skipping to the penultimate.
		if (ValidTransportTimeline(count, fps) && std::isfinite(seconds))
		{
			const double raw = static_cast<double>(seconds) * fps;
			const double tolerance = (std::min)(0.125, (std::max)(1e-6,
				std::fabs(raw) * std::numeric_limits<float>::epsilon() * 2.0));
			if (raw > static_cast<double>(count - 1) + tolerance) position = raw;
		}
		const double base = delta > 0 ? std::floor(position) : std::ceil(position);
		return static_cast<int64_t>(std::clamp(base + static_cast<double>(delta), 0.0,
			count > 0 ? static_cast<double>(count - 1) : 0.0));
	}

	inline void FormatTransportTime(double seconds, char* out, size_t size) noexcept
	{
		if (!out || size == 0) return;
		if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
		// A human-readable display does not alter the continuously stored time.
		if (seconds > 1e12)
		{
			std::snprintf(out, size, "%.3f", seconds);
			return;
		}
		const uint64_t milliseconds = static_cast<uint64_t>(std::round(seconds * 1000.0));
		std::snprintf(out, size, "%02llu:%02llu.%03llu",
			static_cast<unsigned long long>(milliseconds / 60000),
			static_cast<unsigned long long>((milliseconds / 1000) % 60),
			static_cast<unsigned long long>(milliseconds % 1000));
	}

	inline std::string_view TrimTransportInput(std::string_view input) noexcept
	{
		const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
		while (!input.empty() && space(input.front())) input.remove_prefix(1);
		while (!input.empty() && space(input.back())) input.remove_suffix(1);
		return input;
	}

	// Deliberately locale independent: decimal seconds or minutes:seconds, never
	// NaN/Inf, signs, exponents, a partial parse, or an overflowing callback value.
	inline bool ParseTransportTime(std::string_view input, float& out) noexcept
	{
		input = TrimTransportInput(input);
		if (input.empty()) return false;
		const auto decimal = [](std::string_view part, bool fraction, double& value) {
			if (part.empty()) return false;
			value = 0.0;
			bool dot = false, digit = false;
			double scale = 0.1;
			for (const char c : part)
			{
				if (c == '.' && fraction && !dot) { dot = true; continue; }
				if (c < '0' || c > '9') return false;
				digit = true;
				if (dot) { value += (c - '0') * scale; scale *= 0.1; }
				else value = value * 10.0 + (c - '0');
				if (!std::isfinite(value) || value > (std::numeric_limits<float>::max)()) return false;
			}
			return digit;
		};
		double seconds = 0.0;
		const size_t colon = input.find(':');
		if (colon == std::string_view::npos)
		{
			if (!decimal(input, true, seconds)) return false;
		}
		else
		{
			double minutes = 0.0, remainder = 0.0;
			if (!decimal(input.substr(0, colon), false, minutes) ||
				!decimal(input.substr(colon + 1), true, remainder) || remainder >= 60.0) return false;
			seconds = minutes * 60.0 + remainder;
		}
		if (!std::isfinite(seconds) || seconds > (std::numeric_limits<float>::max)()) return false;
		out = static_cast<float>(seconds);
		return true;
	}

	inline bool ParseTransportFrame(std::string_view input, int64_t& out) noexcept
	{
		input = TrimTransportInput(input);
		if (input.empty()) return false;
		int64_t value = 0;
		for (const char c : input)
		{
			if (c < '0' || c > '9') return false;
			if (value > ((std::numeric_limits<int64_t>::max)() - (c - '0')) / 10) return false;
			value = value * 10 + (c - '0');
		}
		out = value;
		return true;
	}
}
