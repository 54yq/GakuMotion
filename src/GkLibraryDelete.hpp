#pragma once

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace GakuMotion::LibraryDelete
{
	// Old recovery storage stays reserved, but no operation creates or restores it.
	inline constexpr const char* LegacyTrashDirectoryName = ".gakumotion-trash";

	inline bool IsLink(const std::filesystem::path& path, std::error_code& error)
	{
		const auto status = std::filesystem::symlink_status(path, error);
		if (error) return true;
#ifdef _WIN32
		const auto attributes = GetFileAttributesW(path.c_str());
		if (attributes == INVALID_FILE_ATTRIBUTES)
		{ error = std::error_code(GetLastError(), std::system_category()); return true; }
		if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) return true;
#endif
		return std::filesystem::is_symlink(status);
	}

	inline bool PlainDirectory(const std::filesystem::path& path, std::error_code& error)
	{
		return !IsLink(path, error) && std::filesystem::is_directory(path, error) && !error;
	}

	inline bool ResolveRoot(const std::filesystem::path& input, std::filesystem::path& root, std::string& error)
	{
		std::error_code ec;
		if (input.empty()) { error = "目录路径为空"; return false; }
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
			if (!PlainDirectory(parent, ec)) { error = "目录无效或包含链接"; return false; }
		}
		return true;
	}

	inline bool ResolveChild(const std::filesystem::path& root, const std::filesystem::path& input,
		std::filesystem::path& child, std::string& error)
	{
		if (!ResolveRoot(input, child, error)) return false;
		if (child.parent_path() != root || child.filename() == LegacyTrashDirectoryName)
		{ error = "只允许删除曲库内的直接子目录"; return false; }
		return true;
	}

	// Validate the entire tree before removing anything. Never follow a junction
	// or symbolic link, including links nested within an imported track.
	inline bool PermanentlyDelete(const std::filesystem::path& inputRoot, const std::filesystem::path& input,
		bool& changed, std::string& error)
	{
		changed = false;
		std::filesystem::path root, source;
		if (!ResolveRoot(inputRoot, root, error) || !ResolveChild(root, input, source, error)) return false;
		std::error_code ec;
		std::vector<std::filesystem::path> paths{source};
		for (std::filesystem::recursive_directory_iterator it(source, ec), end; it != end && !ec; it.increment(ec))
		{
			if (IsLink(it->path(), ec)) { error = "曲目目录包含链接，未删除任何文件"; return false; }
			paths.push_back(it->path());
		}
		if (ec) { error = ec.message(); return false; }
#ifdef _WIN32
		struct DeleteHandles
		{
			std::vector<HANDLE> values;
			~DeleteHandles() { for (auto handle : values) CloseHandle(handle); }
		} handles;
		// Detect ordinary sharing/permission failures before any irreversible work.
		// Close the preflight handles before removing directories so Windows can
		// complete child deletion before each parent is removed.
		for (const auto& path : paths)
		{
			const auto attributes = GetFileAttributesW(path.c_str());
			if (attributes == INVALID_FILE_ATTRIBUTES ||
				((attributes & FILE_ATTRIBUTE_READONLY) && !(attributes & FILE_ATTRIBUTE_DIRECTORY)))
			{ error = "曲目包含无法删除或只读的文件"; return false; }
			const auto handle = CreateFileW(path.c_str(), DELETE | FILE_READ_ATTRIBUTES,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
				FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
			if (handle == INVALID_HANDLE_VALUE)
			{ error = std::error_code(GetLastError(), std::system_category()).message(); return false; }
			handles.values.push_back(handle);
		}
		for (auto handle : handles.values) CloseHandle(handle);
		handles.values.clear();
#endif
		const auto count = std::filesystem::remove_all(source, ec);
		// remove_all returns uintmax_t(-1) on error; conservatively report a
		// possible partial deletion once destructive work has begun.
		changed = ec || count != 0;
		if (ec) { error = ec.message(); return false; }
		return true;
	}
}
