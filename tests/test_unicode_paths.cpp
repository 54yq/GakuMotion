// No game/Unity or external assets: self-authored files and a silent WAV.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "GkWindowsPaths.hpp"
#include "GkBoneOverride.hpp"
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cstdint>
#include <cstdlib>

namespace fs = std::filesystem;
namespace paths = GakuMotion::windows_path;
static int failures = 0, checks = 0;
static void Check(bool ok, const char* name)
{
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL: %s\n", name); }
    else std::printf("PASS: %s\n", name);
}
template<class T> static void Put(std::ofstream& out, T value)
{ out.write(reinterpret_cast<const char*>(&value), sizeof(value)); }

int wmain(int argc, wchar_t** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("Unicode paths: start\n");
    std::wstring wide = L"\u521d\u661f space \U0001F60A\u30C6\u30B9\u30C8";
    std::string utf8;
    Check(paths::ToUtf8(wide, utf8), "UTF-16 to UTF-8");
    std::wstring roundtrip;
    Check(paths::FromUtf8(utf8, roundtrip) && roundtrip == wide, "lossless Unicode roundtrip");
    const std::string before = utf8;
    Check(!paths::ToUtf8(std::wstring(1, wchar_t{0xD800}), utf8) && utf8 == before,
        "reject unpaired surrogate without altering output");
    const auto previousWide = roundtrip;
    Check(!paths::FromUtf8("\xFF", roundtrip) && roundtrip == previousWide,
        "reject malformed UTF-8 without altering output");
    Check(!paths::FromUtf8(std::string("a\0b", 3), roundtrip), "reject embedded path NUL");

    if (argc != 2) { std::printf("Expected a writable synthetic parent directory\n"); return 2; }
    const auto temporaryParent = fs::absolute(fs::path(argv[1])).lexically_normal();
    const auto root = temporaryParent / (L"gakumotion-unicode-" + std::to_wstring(GetCurrentProcessId()) +
        L"-" + std::to_wstring(GetTickCount64()));
    Check(fs::create_directory(root), "create private synthetic root");
    const auto directory = root / wide;
    fs::create_directory(directory);
    const auto config = directory / L"\u8A2D\u5B9A.json";
    GakuMotion::BoneOverrideSet original;
    original.enabled = true;
    Check(original.SaveToFile(GakuMotion::MotionPathToUtf8(config)), "save override under Unicode root");
    GakuMotion::BoneOverrideSet loaded;
    Check(loaded.LoadFromFile(GakuMotion::MotionPathToUtf8(config)) && loaded.enabled,
        "read override under Unicode root");

    const auto wav = directory / L"\u97F3\u6E90 \U0001F60A.wav";
    {
        std::ofstream out(wav, std::ios::binary);
        out.write("RIFF", 4); Put<std::uint32_t>(out, 36 + 1600); out.write("WAVEfmt ", 8);
        Put<std::uint32_t>(out, 16); Put<std::uint16_t>(out, 1); Put<std::uint16_t>(out, 1);
        Put<std::uint32_t>(out, 8000); Put<std::uint32_t>(out, 16000);
        Put<std::uint16_t>(out, 2); Put<std::uint16_t>(out, 16);
        out.write("data", 4); Put<std::uint32_t>(out, 1600);
        const std::string silence(1600, '\0'); out.write(silence.data(), silence.size());
    }
    const std::string open = "open \"" + GakuMotion::MotionPathToUtf8(wav) +
        "\" type waveaudio alias gkmotion_unicode_test";
    const auto openError = paths::SendMciUtf8(open.c_str(), nullptr, 0, nullptr);
    if (openError) std::printf("MCI open OS error: %lu\n", static_cast<unsigned long>(openError));
    Check(openError == 0, "MCI opens Unicode WAV using wide API");
    char reply[64]{};
    Check(paths::SendMciUtf8("set gkmotion_unicode_test time format milliseconds", nullptr, 0, nullptr) == 0,
        "MCI selects milliseconds");
    Check(paths::SendMciUtf8("status gkmotion_unicode_test length", reply, sizeof(reply), nullptr) == 0 &&
        std::atoi(reply) == 100, "MCI bounded UTF-8 status return");
    Check(paths::SendMciUtf8("close gkmotion_unicode_test", nullptr, 0, nullptr) == 0,
        "MCI closes synthetic file without playback");
    Check(paths::SendMciUtf8("\xFF", nullptr, 0, nullptr) == MCIERR_INVALID_FILE,
        "MCI rejects malformed UTF-8");

    // Only this process's freshly-created synthetic tree is eligible for cleanup.
    Check(fs::absolute(root.parent_path()).lexically_normal() == temporaryParent &&
        root.filename().wstring().find(L"gakumotion-unicode-") == 0, "validate private cleanup boundary");
    fs::remove_all(root);
    Check(!fs::exists(root), "remove synthetic tree");
    std::printf("Unicode paths: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
