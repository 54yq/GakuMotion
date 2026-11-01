#pragma once

// =============================================================================
// GkImportToolsCheck.hpp — 部署一致性 preflight
// =============================================================================
// 起因：插件（DLL）与 <localRoot>/GakuMotion/tools 是两套独立发布的产物。
// 2026-10-29 的实机事故就是 DLL 已经按新版参数（--motion-vmd/--camera-vmd/
// --edit-dir）拼命令行，而部署目录里还是 2026-10-23 的 import_motion.py，
// argparse 直接把 "unrecognized arguments" 原文抛给用户。
//
// 这里只做一件事：在启动 Python 之前确认脚本仍然认识本次要传的开关。
// 不做版本协议、不做 package manager，也不改动被调用的脚本。
// =============================================================================

#include <windows.h>

#include <string>

namespace GakuMotion::tools_check
{
	namespace detail
	{
		inline std::wstring Wide(const std::string& utf8)
		{
			if (utf8.empty()) return {};
			const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
			if (size <= 0) return {};
			std::wstring out(static_cast<size_t>(size), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), out.data(), size);
			return out;
		}

		/// <summary>Reads a whole file as UTF-8 bytes. Returns false when unreadable.</summary>
		inline bool ReadAll(const std::wstring& path, std::string& text)
		{
			text.clear();
			HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
			                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (handle == INVALID_HANDLE_VALUE) return false;
			LARGE_INTEGER size{};
			if (!GetFileSizeEx(handle, &size) || size.QuadPart <= 0 || size.QuadPart > (8 << 20))
			{
				CloseHandle(handle);
				return false;
			}
			text.resize(static_cast<size_t>(size.QuadPart));
			DWORD read = 0;
			const BOOL ok = ReadFile(handle, text.data(), static_cast<DWORD>(text.size()), &read, nullptr);
			CloseHandle(handle);
			if (!ok) return false;
			text.resize(read);
			return true;
		}

		inline std::string Describe(const char* flag)
		{
			return std::string("缺少 ") + flag;
		}
	}

	/// <summary>
	/// Verifies that a deployed import_motion.py understands every flag this
	/// build may pass. `needCamera` adds the camera switch to the check.
	/// Returns true when the file is missing (the caller reports that case).
	/// </summary>
	inline bool VerifyMotionTool(const std::string& scriptUtf8, bool needCamera, std::string& error)
	{
		const std::wstring path = detail::Wide(scriptUtf8);
		if (path.empty()) return true;
		if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return true;

		std::string text;
		if (!detail::ReadAll(path, text)) return true;

		std::string missing;
		auto require = [&](const char* flag)
		{
			if (text.find(flag) != std::string::npos) return;
			if (!missing.empty()) missing += "、";
			missing += flag;
		};
		require("--motion-vmd");
		require("--motions-root");
		if (needCamera) require("--camera-vmd");

		if (missing.empty()) return true;
		error = "导入工具版本过旧：" + scriptUtf8 + " 不认识 " + missing +
		        "。DLL 与 tools 不同步，请重新运行 GakuMotion 的部署脚本（tools/deploy-importer.ps1）覆盖 GakuMotion/tools 后再导入。";
		return false;
	}

	/// <summary>Same contract for the standalone camera importer.</summary>
	inline bool VerifyCameraTool(const std::string& scriptUtf8, std::string& error)
	{
		const std::wstring path = detail::Wide(scriptUtf8);
		if (path.empty()) return true;
		if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return true;

		std::string text;
		if (!detail::ReadAll(path, text)) return true;

		std::string missing;
		auto require = [&](const char* flag)
		{
			if (text.find(flag) != std::string::npos) return;
			if (!missing.empty()) missing += "、";
			missing += flag;
		};
		require("--motion-dir");

		if (missing.empty()) return true;
		error = "镜头导入工具版本过旧：" + scriptUtf8 + " 不认识 " + missing +
		        "。请重新运行 GakuMotion 的部署脚本（tools/deploy-importer.ps1）后再导入镜头。";
		return false;
	}
}
