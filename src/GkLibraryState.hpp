#pragma once
#include "GkRuntimeConfig.hpp"
#include <array>
#include <map>

namespace GakuMotion
{
	// A library refresh observes files; it never changes the loaded performance.
	struct MotionFileVersion
	{
		std::array<FileSignature, 11> files{};
		bool operator==(const MotionFileVersion&) const = default;
	};

	inline MotionFileVersion ReadMotionFileVersion(const std::string& directory)
	{
		static constexpr const char* names[] = {"motion.gkclip", "motion.gk_world_dump", "motion.gkmotion",
			"camera.gkcamera", "song.wav", "info.json", "bone_overrides.json", "root_height_calibration.json",
			"aux_replay.json", "source.vmd", "source_morph.vmd"};
		MotionFileVersion result;
		const std::filesystem::path root(std::u8string(directory.begin(), directory.end()));
		for (size_t i = 0; i < result.files.size(); ++i)
		{
			std::error_code ec;
			const auto path = root / names[i];
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
