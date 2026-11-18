#pragma once
#include "GkAudioPlayback.hpp"
#include "GkRuntimeConfig.hpp"
#include <array>
#include <map>

namespace GakuMotion
{
	// A library refresh observes files; it never changes the loaded performance.
	struct MotionFileVersion
	{
		std::array<FileSignature, 12> files{};
		bool operator==(const MotionFileVersion&) const = default;
	};

	inline MotionFileVersion ReadMotionFileVersion(const std::string& directory)
	{
		MotionFileVersion result;
		const std::filesystem::path root(std::u8string(directory.begin(), directory.end()));
		std::string audioHint;
		std::string coverHint;
		{
			std::error_code ec;
			if (std::filesystem::is_regular_file(root / "info.json", ec))
			{
				std::ifstream infoStream(root / "info.json", std::ios::binary);
				if (infoStream)
				{
					std::string raw((std::istreambuf_iterator<char>(infoStream)), std::istreambuf_iterator<char>());
					jsonlite::Value rootJson;
					std::string parseErr;
					if (jsonlite::ParseText(raw, rootJson, parseErr) && rootJson.type == jsonlite::Type::Object)
					{
						audioHint = rootJson.StringPath("audio_file", "");
						coverHint = rootJson.StringPath("cover_file", "");
					}
				}
			}
		}

		std::string resolvedAudio = audio::ResolveTrackAudioFileName(root, audioHint);
		if (resolvedAudio.empty()) resolvedAudio = "song.wav";
		std::string resolvedCover = audio::ResolveTrackCoverFileName(root, coverHint);
		if (resolvedCover.empty()) resolvedCover = "cover.jpg";

		const std::array<std::string, 12> names{
			"motion.gkclip", "motion.gk_world_dump", "motion.gkmotion",
			"camera.gkcamera", resolvedAudio, "info.json",
			"bone_overrides.json", "root_height_calibration.json",
			"aux_replay.json", "source.vmd", "source_morph.vmd", resolvedCover};

		for (size_t i = 0; i < result.files.size(); ++i)
		{
			std::error_code ec;
			const auto path = root / std::u8string(names[i].begin(), names[i].end());
			auto& signature = result.files[i];
			signature.exists = std::filesystem::is_regular_file(path, ec);
			if (!signature.exists || ec) continue;
			auto time = std::filesystem::last_write_time(path, ec);
			if (!ec) signature.writeTime = static_cast<long long>(time.time_since_epoch().count());
			auto length = std::filesystem::file_size(path, ec);
			if (!ec) signature.size = static_cast<long long>(length);
		}
		return result;
	}

	struct LibraryScanSnapshot
	{
		MotionLibraryScanResult result;
		std::map<std::string, MotionFileVersion> versions;
	};

	inline LibraryScanSnapshot ReadLibrarySnapshot(const std::string& root)
	{
		LibraryScanSnapshot snapshot;
		snapshot.result = ScanMotionLibraryDetailed(root);
		for (const auto& entry : snapshot.result.motions)
			snapshot.versions.emplace(entry.name, ReadMotionFileVersion(entry.dir));
		return snapshot;
	}
}
