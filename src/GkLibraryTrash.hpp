#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace GakuMotion::LibraryTrash
{
	inline constexpr const char* DirectoryName = ".gakumotion-trash";

	inline bool PlainDirectory(const std::filesystem::path& path, std::error_code& error)
	{
		const auto status = std::filesystem::symlink_status(path, error);
		if (error || !std::filesystem::is_directory(status) || std::filesystem::is_symlink(status)) return false;
		// MSVC also recognizes directory junctions through read_symlink. Do not
		// traverse a reparse target even when it happens to stay inside the library.
		std::error_code linkError;
		(void)std::filesystem::read_symlink(path, linkError);
		return static_cast<bool>(linkError);
	}

	inline bool ResolveRoot(const std::filesystem::path& input, std::filesystem::path& root, std::string& error)
	{
		std::error_code ec;
		if (input.empty()) { error = "目录路径为空"; return false; }
		// Reject traversal before normalization. All ancestor components must be
		// real directories, so lexical absolute paths cannot escape through links.
		for (const auto& part : input)
			if (part == ".." || part == ".") { error = "目录路径含相对跳转"; return false; }
		root = std::filesystem::absolute(input, ec).lexically_normal();
		if (ec) { error = ec.message(); return false; }
		if (!root.has_filename() && root != root.root_path()) root = root.parent_path();
		auto parent = root.root_path();
		if (!PlainDirectory(parent, ec)) { error = "目录根无效或为链接"; return false; }
		for (const auto& part : root.relative_path())
		{
			parent /= part;
			if (!PlainDirectory(parent, ec)) { error = "目录无效或包含链接: " + ec.message(); return false; }
		}
		return true;
	}

	inline bool ResolveChild(const std::filesystem::path& root, const std::filesystem::path& input,
		std::filesystem::path& child, std::string& error)
	{
		if (!ResolveRoot(input, child, error)) return false;
		if (child.parent_path() != root || child.filename() == DirectoryName)
		{
			error = "只允许操作曲库内的直接子目录";
			return false;
		}
		return true;
	}

	// Same-volume rename only: never copy, overwrite, change permissions or
	// recursively delete. A locked/invalid directory remains in its original place.
	inline bool Move(const std::filesystem::path& inputRoot, const std::filesystem::path& input,
		std::filesystem::path& stored, std::string& error)
	{
		std::filesystem::path root, source;
		if (!ResolveRoot(inputRoot, root, error) || !ResolveChild(root, input, source, error)) return false;
		const auto trash = root / DirectoryName;
		std::error_code ec;
		if (std::filesystem::exists(trash, ec))
		{
			// The trash name is reserved, so validate its topology separately.
			std::filesystem::path checked;
			if (!ResolveRoot(trash, checked, error) || checked.parent_path() != root)
			{ error = "恢复目录无效或为链接"; return false; }
		}
		else if (ec || !std::filesystem::create_directory(trash, ec))
		{ error = "无法建立恢复目录: " + ec.message(); return false; }

		static std::atomic<uint64_t> sequence{0};
		const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
		std::filesystem::path slot;
		bool created = false;
		for (unsigned retry = 0; retry < 32 && !created; ++retry)
		{
			slot = trash / (std::to_string(stamp) + "-" + std::to_string(++sequence));
			created = std::filesystem::create_directory(slot, ec);
			if (ec) { error = ec.message(); return false; }
		}
		if (!created) { error = "无法建立唯一恢复目录"; return false; }
		stored = slot / source.filename();
		std::filesystem::rename(source, stored, ec);
		if (ec)
		{
			error = ec.message();
			std::error_code cleanup;
			std::filesystem::remove(slot, cleanup); // This directory is still empty.
			stored.clear();
			return false;
		}
		return true;
	}

	inline bool Restore(const std::filesystem::path& inputRoot, const std::filesystem::path& stored,
		std::filesystem::path& restored, std::string& error)
	{
		std::filesystem::path root;
		if (!ResolveRoot(inputRoot, root, error)) return false;
		const auto trash = root / DirectoryName;
		std::error_code ec;
		std::filesystem::path source, checkedTrash;
		if (!ResolveRoot(trash, checkedTrash, error) || !ResolveRoot(stored, source, error)) return false;
		if (checkedTrash.parent_path() != root || source.parent_path().parent_path() != checkedTrash ||
			source.filename() == DirectoryName || source.filename().empty())
		{ error = "恢复路径超出曲库恢复目录"; return false; }
		restored = root / source.filename();
		if (std::filesystem::exists(std::filesystem::symlink_status(restored, ec)))
		{ error = "原位置已有同名曲目，未覆盖；请先重命名或移走该曲目"; return false; }
		if (ec && ec != std::errc::no_such_file_or_directory) { error = ec.message(); return false; }
		ec.clear();
		std::filesystem::rename(source, restored, ec);
		if (ec) { error = ec.message(); return false; }
		std::filesystem::remove(source.parent_path(), ec); // Empty receipt container only.
		return true;
	}

	inline bool Latest(const std::filesystem::path& inputRoot, std::filesystem::path& stored, std::string& error)
	{
		std::filesystem::path root;
		if (!ResolveRoot(inputRoot, root, error)) return false;
		const auto trash = root / DirectoryName;
		std::error_code ec;
		if (!std::filesystem::exists(trash, ec)) { error = "没有可恢复的已删除曲目"; return false; }
		std::filesystem::path checkedTrash;
		if (!ResolveRoot(trash, checkedTrash, error) || checkedTrash.parent_path() != root)
		{ error = "恢复目录无效或为链接"; return false; }
		std::vector<std::filesystem::path> slots;
		for (std::filesystem::directory_iterator it(trash, ec), end; it != end && !ec; it.increment(ec))
			if (PlainDirectory(it->path(), ec)) slots.push_back(it->path());
		if (ec) { error = ec.message(); return false; }
		std::sort(slots.rbegin(), slots.rend());
		for (const auto& slot : slots)
		{
			std::vector<std::filesystem::path> entries;
			for (std::filesystem::directory_iterator it(slot, ec), end; it != end && !ec; it.increment(ec))
				entries.push_back(it->path());
			if (ec) { error = ec.message(); return false; }
			if (entries.size() == 1 && PlainDirectory(entries[0], ec)) { stored = entries[0]; return true; }
		}
		error = "没有可恢复的已删除曲目";
		return false;
	}
}
