#pragma once

#include "../include/GkAudioMetadata.hpp"

#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace GakuMotion::audio
{
	inline bool IsSafeSingleFileName(std::string_view name) noexcept
	{
		if (name.empty() || name == "." || name == "..") return false;
		for (const char ch : name)
		{
			if (ch == '/' || ch == '\\' || ch == ':' || ch == '\0') return false;
		}
		return true;
	}

	inline std::string ResolveTrackAudioFileName(const std::filesystem::path& dir, std::string_view hint = "")
	{
		if (dir.empty()) return {};
		std::error_code ec;
		if (IsSafeSingleFileName(hint) && GakuAudioMeta::IsSupportedAudioExtension(hint))
		{
			if (std::filesystem::is_regular_file(dir / std::u8string(hint.begin(), hint.end()), ec))
				return std::string(hint);
		}
		static constexpr const char* kCandidates[] = {
			"song.wav", "song.flac", "song.mp3", "song.ogg", "song.oga",
			"song.m4a", "song.aac", "song.opus", "song.wma"
		};
		for (const char* candidate : kCandidates)
		{
			if (std::filesystem::is_regular_file(dir / candidate, ec))
				return candidate;
		}
		return {};
	}

	inline std::string ResolveTrackCoverFileName(const std::filesystem::path& dir, std::string_view hint = "")
	{
		if (dir.empty()) return {};
		std::error_code ec;
		auto isImgExt = [](std::string_view n) {
			const auto dot = n.find_last_of('.');
			if (dot == std::string_view::npos) return false;
			std::string ext = GakuAudioMeta::LowerAscii(n.substr(dot));
			return ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".webp" || ext == ".bmp";
		};
		if (IsSafeSingleFileName(hint) && isImgExt(hint))
		{
			if (std::filesystem::is_regular_file(dir / std::u8string(hint.begin(), hint.end()), ec))
				return std::string(hint);
		}
		static constexpr const char* kCandidates[] = { "cover.jpg", "cover.jpeg", "cover.png", "cover.webp" };
		for (const char* candidate : kCandidates)
		{
			if (std::filesystem::is_regular_file(dir / candidate, ec))
				return candidate;
		}
		return {};
	}

	// Read only RIFF chunk headers. Supports PCM (1), IEEE float (3), and WAVEFORMATEXTENSIBLE (0xFFFE).
	inline float PcmWavDuration(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary | std::ios::ate);
		if (!input) return 0.0f;
		const auto length = input.tellg();
		if (length < 12) return 0.0f;
		input.seekg(0);
		char header[12]{};
		input.read(header, sizeof(header));
		if (std::memcmp(header, "RIFF", 4) || std::memcmp(header + 8, "WAVE", 4)) return 0.0f;
		uint32_t byteRate = 0, dataBytes = 0;
		for (unsigned chunk = 0; chunk < 4096 && input.tellg() <= length - std::streamoff{8}; ++chunk)
		{
			char kind[4]{};
			uint32_t size = 0;
			input.read(kind, 4);
			input.read(reinterpret_cast<char*>(&size), 4);
			const auto start = input.tellg();
			if (!input || size > length - start) return 0.0f;
			if (!std::memcmp(kind, "fmt ", 4) && size >= 16)
			{
				uint16_t format = 0, channels = 0, align = 0, bits = 0;
				uint32_t rate = 0;
				input.read(reinterpret_cast<char*>(&format), 2);
				input.read(reinterpret_cast<char*>(&channels), 2);
				input.read(reinterpret_cast<char*>(&rate), 4);
				input.read(reinterpret_cast<char*>(&byteRate), 4);
				input.read(reinterpret_cast<char*>(&align), 2);
				input.read(reinterpret_cast<char*>(&bits), 2);
				const bool validFmt = (format == 1 || format == 3 || format == 0xFFFE);
				if (!input || !validFmt || !channels || !rate || !align ||
					(bits != 8 && bits != 16 && bits != 24 && bits != 32) ||
					static_cast<uint32_t>(channels) * (bits / 8) != align ||
					static_cast<uint64_t>(rate) * align != byteRate) return 0.0f;
			}
			if (!std::memcmp(kind, "data", 4)) dataBytes = size;
			if (byteRate && dataBytes) return static_cast<float>(static_cast<double>(dataBytes) / byteRate);
			input.seekg(start + static_cast<std::streamoff>(static_cast<uint64_t>(size) + (size & 1)));
		}
		return 0.0f;
	}

	inline float AudioFileDuration(const std::filesystem::path& path)
	{
		if (path.empty()) return 0.0f;
		const std::string ext = GakuAudioMeta::LowerAscii(path.extension().string());
		if (ext == ".wav")
		{
			const float wav = PcmWavDuration(path);
			if (wav > 0.0f) return wav;
		}
		const auto meta = GakuAudioMeta::ProbeFilePath(path);
		if (meta.valid && meta.durationSeconds > 0.0)
			return static_cast<float>(meta.durationSeconds);
		return 0.0f;
	}

	// Backend observations, not requests or the existence of a play callback.
	// The host refreshes this after Play/Resume/Seek and before BGM policy sync.
	enum class PlaybackState : uint8_t { Stopped, Starting, Playing, Paused, Failed };

	struct Observation
	{
		PlaybackState state{PlaybackState::Stopped};
		bool sourceOpen{false};
		bool confirmed{false};
		uint64_t sourceEpoch{0}; // Nonzero identity of an actual opened audio source.
		uint32_t errorCode{0};

		bool EffectivePlaying() const noexcept
		{
			return state == PlaybackState::Playing && sourceOpen && confirmed && sourceEpoch != 0 && errorCode == 0;
		}
	};

	inline const char* StateName(PlaybackState state) noexcept
	{
		switch (state)
		{
		case PlaybackState::Stopped: return "stopped";
		case PlaybackState::Starting: return "starting";
		case PlaybackState::Playing: return "playing";
		case PlaybackState::Paused: return "paused";
		case PlaybackState::Failed: return "failed";
		}
		return "unknown";
	}
}
