#pragma once

// Product path strings are UTF-8. Keep Windows calls on their UTF-16 entry
// points; converting through the active ANSI code page loses valid file names.
#include <windows.h>
#include <mmsystem.h>
#include <limits>
#include <string>
#include <string_view>
#include <vector>
#include <cstring>
#include <utility>

namespace GakuMotion::windows_path
{
    inline bool FromUtf8(std::string_view text, std::wstring& output)
    {
        if (text.find('\0') != std::string_view::npos ||
            text.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) return false;
        if (text.empty()) { output.clear(); return true; }
        const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (!size) return false;
        std::wstring wide(static_cast<size_t>(size), L'\0');
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
            static_cast<int>(text.size()), wide.data(), size) != size) return false;
        output = std::move(wide);
        return true;
    }

    inline bool ToUtf8(std::wstring_view text, std::string& output)
    {
        if (text.find(L'\0') != std::wstring_view::npos ||
            text.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) return false;
        if (text.empty()) { output.clear(); return true; }
        const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (!size) return false;
        std::string utf8(static_cast<size_t>(size), '\0');
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
            static_cast<int>(text.size()), utf8.data(), size, nullptr, nullptr) != size) return false;
        output = std::move(utf8);
        return true;
    }

    // Preserve the existing transport's injectable char-command signature while
    // sending UTF-8 file names through MCI's wide entry point. All current status
    // replies are ASCII; conversion is bounded and never truncates a UTF-8 codepoint.
    inline MCIERROR WINAPI SendMciUtf8(LPCSTR command, LPSTR output, UINT capacity, HWND callback)
    {
        if (!command) return MCIERR_MISSING_COMMAND_STRING;
        std::wstring wideCommand;
        if (!FromUtf8(command, wideCommand)) return MCIERR_INVALID_FILE;
        std::vector<wchar_t> wideOutput(output && capacity ? capacity : 0, L'\0');
        auto error = mciSendStringW(wideCommand.c_str(),
            wideOutput.empty() ? nullptr : wideOutput.data(),
            static_cast<UINT>(wideOutput.size()), callback);
        // Some waveaudio drivers still reject supplementary characters even
        // through the W API. An existing DOS alias can name that same file
        // without copying it or changing the user's path. Volumes without such
        // an alias retain the original explicit MCI failure.
        if ((error == MCIERR_FILENAME_REQUIRED || error == MCIERR_FILE_NOT_FOUND || error == MCIERR_INVALID_FILE) &&
            wideCommand.compare(0, 6, L"open \"") == 0)
        {
            const auto end = wideCommand.find(L'"', 6);
            if (end != std::wstring::npos)
            {
                const auto path = wideCommand.substr(6, end - 6);
                const DWORD count = GetShortPathNameW(path.c_str(), nullptr, 0);
                if (count)
                {
                    std::wstring shortPath(count, L'\0');
                    const DWORD written = GetShortPathNameW(path.c_str(), shortPath.data(), count);
                    if (written && written < count)
                    {
                        shortPath.resize(written);
                        if (shortPath != path)
                        {
                            const auto aliasCommand = wideCommand.substr(0, 6) + shortPath + wideCommand.substr(end);
                            error = mciSendStringW(aliasCommand.c_str(),
                                wideOutput.empty() ? nullptr : wideOutput.data(),
                                static_cast<UINT>(wideOutput.size()), callback);
                        }
                    }
                }
            }
        }
        if (error || wideOutput.empty()) return error;
        // A misbehaving device must not make a status query read out of bounds.
        size_t length = 0;
        while (length < wideOutput.size() && wideOutput[length]) ++length;
        if (length == wideOutput.size()) return MCIERR_PARAM_OVERFLOW;
        std::string utf8;
        if (!ToUtf8(std::wstring_view(wideOutput.data(), length), utf8) || utf8.size() >= capacity)
            return MCIERR_PARAM_OVERFLOW;
        std::memcpy(output, utf8.c_str(), utf8.size() + 1);
        return 0;
    }
}
