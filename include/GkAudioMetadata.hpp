// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace GakuAudioMeta
{
	struct Metadata
	{
		bool valid{false};
		uint32_t sampleRate{0};
		uint32_t channels{0};
		uint32_t bitsPerSample{0};
		double durationSeconds{0.0};
		std::string codec;
		std::string title;
		std::string artist;
		std::string album;
		std::string coverMime;
		std::vector<uint8_t> coverBytes;
	};

	inline std::string LowerAscii(std::string_view text)
	{
		std::string out(text);
		for (char& ch : out)
			ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		return out;
	}

	inline bool IsNetworkPath(std::string_view utf8Path)
	{
		if (utf8Path.find("://") != std::string_view::npos) return true;
		if (utf8Path.starts_with("\\\\") || utf8Path.starts_with("//")) return true;
		return false;
	}

	inline bool IsSupportedAudioExtension(std::string_view extOrPath)
	{
		std::string lower = LowerAscii(extOrPath);
		const size_t dot = lower.find_last_of('.');
		const std::string ext = (dot == std::string::npos) ? lower : lower.substr(dot);
		return ext == ".wav" || ext == ".mp3" || ext == ".flac" ||
		       ext == ".ogg" || ext == ".oga" || ext == ".m4a" ||
		       ext == ".mp4" || ext == ".aac" || ext == ".opus" || ext == ".wma";
	}

	inline std::filesystem::path PathFromUtf8(std::string_view utf8)
	{
#ifdef _WIN32
		if (utf8.empty() || utf8.find('\0') != std::string_view::npos) return {};
		const int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
			utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
		if (needed <= 0) return {};
		std::wstring wide(static_cast<size_t>(needed), L'\0');
		if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
			utf8.data(), static_cast<int>(utf8.size()), wide.data(), needed) != needed) return {};
		return std::filesystem::path(wide);
#else
		return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
#endif
	}

	namespace detail
	{
		inline uint16_t ReadLE16(const uint8_t* p) { return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8); }
		inline uint32_t ReadLE32(const uint8_t* p)
		{
			return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
			       (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
		}
		inline uint16_t ReadBE16(const uint8_t* p) { return (static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]); }
		inline uint32_t ReadBE32(const uint8_t* p)
		{
			return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
			       (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
		}
		inline uint64_t ReadBE64(const uint8_t* p)
		{
			return (static_cast<uint64_t>(ReadBE32(p)) << 32) | static_cast<uint64_t>(ReadBE32(p + 4));
		}
		inline uint32_t ReadSyncsafe(const uint8_t* p)
		{
			return ((static_cast<uint32_t>(p[0]) & 0x7Fu) << 21) |
			       ((static_cast<uint32_t>(p[1]) & 0x7Fu) << 14) |
			       ((static_cast<uint32_t>(p[2]) & 0x7Fu) << 7) |
			       (static_cast<uint32_t>(p[3]) & 0x7Fu);
		}

		inline std::string TrimNullAndSpace(std::string s)
		{
			while (!s.empty() && (s.back() == '\0' || s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
				s.pop_back();
			size_t start = 0;
			while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\0'))
				++start;
			return start > 0 ? s.substr(start) : s;
		}

		inline bool IsValidUtf8(const uint8_t* data, size_t len)
		{
			size_t i = 0;
			while (i < len)
			{
				const uint8_t c = data[i];
				if (c <= 0x7F) { ++i; continue; }
				size_t extra = 0;
				if ((c & 0xE0) == 0xC0 && c >= 0xC2) extra = 1;
				else if ((c & 0xF0) == 0xE0) extra = 2;
				else if ((c & 0xF8) == 0xF0 && c <= 0xF4) extra = 3;
				else return false;
				if (i + extra >= len) return false;
				for (size_t j = 1; j <= extra; ++j)
					if ((data[i + j] & 0xC0) != 0x80) return false;
				i += extra + 1;
			}
			return true;
		}

		inline std::string CodepageOrLatin1ToUtf8(const uint8_t* data, size_t len)
		{
			while (len > 0 && data[len - 1] == 0) --len;
			if (len == 0) return {};
			if (IsValidUtf8(data, len))
				return TrimNullAndSpace(std::string(reinterpret_cast<const char*>(data), len));
#ifdef _WIN32
			const int wlen = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS,
				reinterpret_cast<const char*>(data), static_cast<int>(len), nullptr, 0);
			if (wlen > 0)
			{
				std::wstring w(static_cast<size_t>(wlen), L'\0');
				if (MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS,
					reinterpret_cast<const char*>(data), static_cast<int>(len), w.data(), wlen) == wlen)
				{
					const int u8len = WideCharToMultiByte(CP_UTF8, 0, w.data(), wlen, nullptr, 0, nullptr, nullptr);
					if (u8len > 0)
					{
						std::string out(static_cast<size_t>(u8len), '\0');
						WideCharToMultiByte(CP_UTF8, 0, w.data(), wlen, out.data(), u8len, nullptr, nullptr);
						return TrimNullAndSpace(std::move(out));
					}
				}
			}
#endif
			std::string out;
			out.reserve(len * 2);
			for (size_t i = 0; i < len; ++i)
			{
				const uint8_t ch = data[i];
				if (ch == 0) break;
				if (ch < 0x80) out.push_back(static_cast<char>(ch));
				else
				{
					out.push_back(static_cast<char>(0xC0 | (ch >> 6)));
					out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
				}
			}
			return TrimNullAndSpace(std::move(out));
		}

		inline std::string Utf16ToUtf8(const uint8_t* data, size_t len, bool bigEndian)
		{
			if (len < 2) return {};
			size_t offset = 0;
			if (data[0] == 0xFF && data[1] == 0xFE) { bigEndian = false; offset = 2; }
			else if (data[0] == 0xFE && data[1] == 0xFF) { bigEndian = true; offset = 2; }
			std::u16string u16;
			u16.reserve((len - offset) / 2);
			for (size_t i = offset; i + 1 < len; i += 2)
			{
				const char16_t ch = bigEndian ? static_cast<char16_t>((data[i] << 8) | data[i + 1])
				                              : static_cast<char16_t>(data[i] | (data[i + 1] << 8));
				if (ch == 0) break;
				u16.push_back(ch);
			}
			if (u16.empty()) return {};
#ifdef _WIN32
			const int u8len = WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<const wchar_t*>(u16.data()),
				static_cast<int>(u16.size()), nullptr, 0, nullptr, nullptr);
			if (u8len > 0)
			{
				std::string out(static_cast<size_t>(u8len), '\0');
				WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<const wchar_t*>(u16.data()),
					static_cast<int>(u16.size()), out.data(), u8len, nullptr, nullptr);
				return TrimNullAndSpace(std::move(out));
			}
#endif
			std::string out;
			for (size_t i = 0; i < u16.size(); ++i)
			{
				uint32_t cp = u16[i];
				if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < u16.size() &&
				    u16[i + 1] >= 0xDC00 && u16[i + 1] <= 0xDFFF)
				{
					cp = 0x10000 + (((cp - 0xD800) << 10) | (u16[++i] - 0xDC00));
				}
				if (cp < 0x80) out.push_back(static_cast<char>(cp));
				else if (cp < 0x800)
				{
					out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
					out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
				}
				else if (cp < 0x10000)
				{
					out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
					out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
					out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
				}
				else
				{
					out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
					out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
					out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
					out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
				}
			}
			return TrimNullAndSpace(std::move(out));
		}

		inline std::string DecodeId3Text(uint8_t encoding, const uint8_t* payload, size_t len)
		{
			if (len == 0) return {};
			switch (encoding)
			{
			case 0: return CodepageOrLatin1ToUtf8(payload, len);
			case 1: return Utf16ToUtf8(payload, len, false);
			case 2: return Utf16ToUtf8(payload, len, true);
			case 3: return TrimNullAndSpace(std::string(reinterpret_cast<const char*>(payload), len));
			default: return {};
			}
		}

		inline std::string DetectImageMime(const uint8_t* data, size_t len, std::string_view fallback)
		{
			if (len >= 8 && data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47)
				return "image/png";
			if (len >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF)
				return "image/jpeg";
			if (len >= 6 && std::memcmp(data, "GIF8", 4) == 0)
				return "image/gif";
			if (len >= 12 && std::memcmp(data, "RIFF", 4) == 0 && std::memcmp(data + 8, "WEBP", 4) == 0)
				return "image/webp";
			std::string f = LowerAscii(fallback);
			if (f == "png" || f == "image/png") return "image/png";
			if (f == "jpg" || f == "jpeg" || f == "image/jpeg" || f == "image/jpg") return "image/jpeg";
			return {};
		}

		inline std::vector<uint8_t> DecodeUnsync(const uint8_t* data, size_t len)
		{
			std::vector<uint8_t> out;
			out.reserve(len);
			for (size_t i = 0; i < len; ++i)
			{
				out.push_back(data[i]);
				if (data[i] == 0xFF && i + 1 < len && data[i + 1] == 0x00)
					++i;
			}
			return out;
		}

		inline size_t ParseId3v2At(const std::vector<uint8_t>& buf, size_t offset, Metadata& meta, int& bestPicScore)
		{
			if (offset + 10 > buf.size()) return offset;
			if (buf[offset] != 'I' || buf[offset + 1] != 'D' || buf[offset + 2] != '3') return offset;
			const uint8_t verMajor = buf[offset + 3];
			if (verMajor < 2 || verMajor > 4) return offset;
			const uint8_t flags = buf[offset + 5];
			const uint32_t tagSize = ReadSyncsafe(buf.data() + offset + 6);
			const size_t totalEnd = (std::min)(buf.size(), offset + 10 + static_cast<size_t>(tagSize));

			std::vector<uint8_t> unpacked;
			const uint8_t* cursorBase = buf.data() + offset + 10;
			size_t bodyLen = totalEnd - (offset + 10);
			if ((flags & 0x80) != 0 && verMajor <= 3)
			{
				unpacked = DecodeUnsync(cursorBase, bodyLen);
				cursorBase = unpacked.data();
				bodyLen = unpacked.size();
			}

			size_t pos = 0;
			if ((flags & 0x40) != 0 && verMajor >= 3 && pos + 4 <= bodyLen)
			{
				const uint32_t extSize = verMajor == 4 ? ReadSyncsafe(cursorBase + pos) : (ReadBE32(cursorBase + pos) + 4);
				pos += (std::min)(static_cast<size_t>(extSize), bodyLen - pos);
			}

			while (pos < bodyLen)
			{
				if (cursorBase[pos] == 0) break;
				std::string frameId;
				uint32_t frameSize = 0;
				uint16_t frameFlags = 0;
				size_t hdrLen = 0;
				if (verMajor == 2)
				{
					if (pos + 6 > bodyLen) break;
					frameId.assign(reinterpret_cast<const char*>(cursorBase + pos), 3);
					frameSize = (static_cast<uint32_t>(cursorBase[pos + 3]) << 16) |
					            (static_cast<uint32_t>(cursorBase[pos + 4]) << 8) |
					            static_cast<uint32_t>(cursorBase[pos + 5]);
					hdrLen = 6;
				}
				else
				{
					if (pos + 10 > bodyLen) break;
					frameId.assign(reinterpret_cast<const char*>(cursorBase + pos), 4);
					frameSize = verMajor == 4 ? ReadSyncsafe(cursorBase + pos + 4) : ReadBE32(cursorBase + pos + 4);
					frameFlags = ReadBE16(cursorBase + pos + 8);
					hdrLen = 10;
				}
				pos += hdrLen;
				if (frameSize == 0 || pos + frameSize > bodyLen) break;

				const uint8_t* fdata = cursorBase + pos;
				size_t flen = frameSize;
				std::vector<uint8_t> frameUnpacked;
				if (verMajor == 4 && (frameFlags & 0x0002) != 0)
				{
					frameUnpacked = DecodeUnsync(fdata, flen);
					fdata = frameUnpacked.data();
					flen = frameUnpacked.size();
				}
				if (verMajor == 4 && (frameFlags & 0x0001) != 0 && flen >= 4)
				{
					fdata += 4;
					flen -= 4;
				}

				if ((frameId == "TIT2" || frameId == "TT2") && flen >= 1 && meta.title.empty())
					meta.title = DecodeId3Text(fdata[0], fdata + 1, flen - 1);
				else if ((frameId == "TPE1" || frameId == "TP1") && flen >= 1 && meta.artist.empty())
					meta.artist = DecodeId3Text(fdata[0], fdata + 1, flen - 1);
				else if ((frameId == "TPE2" || frameId == "TP2") && flen >= 1 && meta.artist.empty())
					meta.artist = DecodeId3Text(fdata[0], fdata + 1, flen - 1);
				else if ((frameId == "TALB" || frameId == "TAL") && flen >= 1 && meta.album.empty())
					meta.album = DecodeId3Text(fdata[0], fdata + 1, flen - 1);
				else if ((frameId == "APIC" || frameId == "PIC") && flen >= 4)
				{
					const uint8_t enc = fdata[0];
					size_t p = 1;
					std::string mimeHint;
					if (frameId == "PIC")
					{
						if (p + 3 <= flen) mimeHint.assign(reinterpret_cast<const char*>(fdata + p), 3);
						p += 3;
					}
					else
					{
						const size_t start = p;
						while (p < flen && fdata[p] != 0) ++p;
						mimeHint.assign(reinterpret_cast<const char*>(fdata + start), p - start);
						if (p < flen) ++p;
					}
					if (p < flen)
					{
						const uint8_t picType = fdata[p++];
						if (enc == 1 || enc == 2)
						{
							while (p + 1 < flen && !(fdata[p] == 0 && fdata[p + 1] == 0)) p += 2;
							if (p + 1 < flen) p += 2;
						}
						else
						{
							while (p < flen && fdata[p] != 0) ++p;
							if (p < flen) ++p;
						}
						if (p < flen)
						{
							const std::string mime = DetectImageMime(fdata + p, flen - p, mimeHint);
							const int score = (picType == 3) ? 3 : (picType == 0 ? 2 : 1);
							if (!mime.empty() && score > bestPicScore)
							{
								bestPicScore = score;
								meta.coverMime = mime;
								meta.coverBytes.assign(fdata + p, fdata + flen);
							}
						}
					}
				}
				pos += frameSize;
			}
			return totalEnd;
		}

		inline void ParseFlacPictureBlock(const uint8_t* data, size_t len, Metadata& meta, int& bestPicScore)
		{
			if (len < 32) return;
			size_t pos = 0;
			const uint32_t picType = ReadBE32(data + pos); pos += 4;
			const uint32_t mimeLen = ReadBE32(data + pos); pos += 4;
			if (pos + mimeLen + 4 > len) return;
			const std::string mimeHint(reinterpret_cast<const char*>(data + pos), mimeLen);
			pos += mimeLen;
			const uint32_t descLen = ReadBE32(data + pos); pos += 4;
			if (pos + descLen + 20 > len) return;
			pos += descLen + 16; // skip desc, w, h, bpp, colors
			const uint32_t imgLen = ReadBE32(data + pos); pos += 4;
			if (pos + imgLen > len || imgLen == 0) return;
			const std::string mime = DetectImageMime(data + pos, imgLen, mimeHint);
			const int score = (picType == 3) ? 3 : (picType == 0 ? 2 : 1);
			if (!mime.empty() && score > bestPicScore)
			{
				bestPicScore = score;
				meta.coverMime = mime;
				meta.coverBytes.assign(data + pos, data + pos + imgLen);
			}
		}

		inline std::vector<uint8_t> DecodeBase64(std::string_view input)
		{
			static const int8_t kLut[256] = {
				-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
				-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
				-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
				-1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
				-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
				-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
				-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
				-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
			};
			std::vector<uint8_t> out;
			out.reserve((input.size() * 3) / 4);
			uint32_t val = 0;
			int valb = -8;
			for (const unsigned char c : input)
			{
				const int8_t d = kLut[c];
				if (d == -1) continue;
				val = (val << 6) + static_cast<uint32_t>(d);
				valb += 6;
				if (valb >= 0)
				{
					out.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
					valb -= 8;
				}
			}
			return out;
		}

		inline void ParseVorbisComments(const uint8_t* data, size_t len, Metadata& meta, int& bestPicScore)
		{
			if (len < 8) return;
			size_t pos = 0;
			const uint32_t vendorLen = ReadLE32(data + pos); pos += 4;
			if (pos + vendorLen + 4 > len) return;
			pos += vendorLen;
			const uint32_t count = ReadLE32(data + pos); pos += 4;
			for (uint32_t i = 0; i < count && pos + 4 <= len; ++i)
			{
				const uint32_t entryLen = ReadLE32(data + pos); pos += 4;
				if (pos + entryLen > len) break;
				std::string_view item(reinterpret_cast<const char*>(data + pos), entryLen);
				pos += entryLen;
				const size_t eq = item.find('=');
				if (eq == std::string_view::npos) continue;
				const std::string key = LowerAscii(item.substr(0, eq));
				const std::string val(item.substr(eq + 1));
				if (key == "title" && meta.title.empty()) meta.title = TrimNullAndSpace(val);
				else if ((key == "artist" || key == "albumartist") && meta.artist.empty()) meta.artist = TrimNullAndSpace(val);
				else if (key == "album" && meta.album.empty()) meta.album = TrimNullAndSpace(val);
				else if (key == "metadata_block_picture")
				{
					const auto raw = DecodeBase64(val);
					if (!raw.empty()) ParseFlacPictureBlock(raw.data(), raw.size(), meta, bestPicScore);
				}
			}
		}

		inline bool ParseWav(const std::vector<uint8_t>& buf, Metadata& meta)
		{
			if (buf.size() < 44 || std::memcmp(buf.data(), "RIFF", 4) != 0 || std::memcmp(buf.data() + 8, "WAVE", 4) != 0)
				return false;
			bool hasFmt = false, hasData = false, supportedFormat = false;
			uint32_t byteRate = 0, blockAlign = 0;
			uint64_t dataBytes = 0;
			int bestPicScore = 0;
			size_t pos = 12;
			int chunks = 0;
			while (pos + 8 <= buf.size() && ++chunks <= 4096)
			{
				const uint8_t* ch = buf.data() + pos;
				const uint32_t size = ReadLE32(ch + 4);
				const size_t payloadPos = pos + 8;
				const size_t avail = (std::min)(static_cast<size_t>(size), buf.size() - payloadPos);
				if (std::memcmp(ch, "fmt ", 4) == 0 && avail >= 16)
				{
					const uint16_t format = ReadLE16(buf.data() + payloadPos);
					meta.channels = ReadLE16(buf.data() + payloadPos + 2);
					meta.sampleRate = ReadLE32(buf.data() + payloadPos + 4);
					byteRate = ReadLE32(buf.data() + payloadPos + 8);
					blockAlign = ReadLE16(buf.data() + payloadPos + 12);
					meta.bitsPerSample = ReadLE16(buf.data() + payloadPos + 14);
					if (format == 1 || format == 3)
					{
						supportedFormat = true;
					}
					else if (format == 0xFFFE && avail >= 40)
					{
						const uint16_t subTag = ReadLE16(buf.data() + payloadPos + 24);
						supportedFormat = (subTag == 1 || subTag == 3);
					}
					hasFmt = true;
				}
				else if (std::memcmp(ch, "data", 4) == 0)
				{
					dataBytes = avail;
					hasData = true;
				}
				else if (std::memcmp(ch, "LIST", 4) == 0 && avail >= 4 && std::memcmp(buf.data() + payloadPos, "INFO", 4) == 0)
				{
					size_t p = payloadPos + 4;
					const size_t end = payloadPos + avail;
					while (p + 8 <= end)
					{
						const uint32_t subLen = ReadLE32(buf.data() + p + 4);
						const size_t subAvail = (std::min)(static_cast<size_t>(subLen), end - (p + 8));
						if (std::memcmp(buf.data() + p, "INAM", 4) == 0 && meta.title.empty())
							meta.title = CodepageOrLatin1ToUtf8(buf.data() + p + 8, subAvail);
						else if (std::memcmp(buf.data() + p, "IART", 4) == 0 && meta.artist.empty())
							meta.artist = CodepageOrLatin1ToUtf8(buf.data() + p + 8, subAvail);
						else if (std::memcmp(buf.data() + p, "IPRD", 4) == 0 && meta.album.empty())
							meta.album = CodepageOrLatin1ToUtf8(buf.data() + p + 8, subAvail);
						const size_t step = 8 + static_cast<size_t>(subLen) + (subLen & 1u);
						if (step <= 8 || p + step > end) break;
						p += step;
					}
				}
				else if ((std::memcmp(ch, "id3 ", 4) == 0 || std::memcmp(ch, "ID3 ", 4) == 0) && avail >= 10)
				{
					ParseId3v2At(buf, payloadPos, meta, bestPicScore);
				}
				const size_t step = 8 + static_cast<size_t>(size) + (size & 1u);
				if (step <= 8 || pos + step > buf.size()) break;
				pos += step;
			}
			if (!hasFmt || !hasData || !supportedFormat || meta.sampleRate == 0 || meta.channels == 0) return false;
			if (byteRate > 0) meta.durationSeconds = static_cast<double>(dataBytes) / byteRate;
			else if (blockAlign > 0) meta.durationSeconds = static_cast<double>(dataBytes) / (static_cast<double>(blockAlign) * meta.sampleRate);
			else return false;
			meta.codec = "wav";
			meta.valid = meta.durationSeconds > 0.0;
			return meta.valid;
		}

		inline bool ParseFlac(const std::vector<uint8_t>& buf, Metadata& meta)
		{
			size_t pos = 0;
			int bestPicScore = 0;
			if (buf.size() >= 10 && std::memcmp(buf.data(), "ID3", 3) == 0)
				pos = ParseId3v2At(buf, 0, meta, bestPicScore);
			if (pos + 4 > buf.size() || std::memcmp(buf.data() + pos, "fLaC", 4) != 0) return false;
			pos += 4;
			bool last = false;
			bool gotStreamInfo = false;
			while (!last && pos + 4 <= buf.size())
			{
				const uint8_t hdr = buf[pos];
				last = (hdr & 0x80) != 0;
				const uint8_t btype = hdr & 0x7F;
				const uint32_t blen = (static_cast<uint32_t>(buf[pos + 1]) << 16) |
				                      (static_cast<uint32_t>(buf[pos + 2]) << 8) |
				                      static_cast<uint32_t>(buf[pos + 3]);
				pos += 4;
				if (pos + blen > buf.size()) break;
				const uint8_t* blk = buf.data() + pos;
				if (btype == 0 && blen >= 34)
				{
					meta.sampleRate = (static_cast<uint32_t>(blk[10]) << 12) |
					                  (static_cast<uint32_t>(blk[11]) << 4) |
					                  (static_cast<uint32_t>(blk[12]) >> 4);
					meta.channels = ((blk[12] >> 1) & 0x07u) + 1u;
					meta.bitsPerSample = (((blk[12] & 0x01u) << 4) | (blk[13] >> 4)) + 1u;
					const uint64_t totalSamples = (static_cast<uint64_t>(blk[13] & 0x0Fu) << 32) |
					                              (static_cast<uint64_t>(blk[14]) << 24) |
					                              (static_cast<uint64_t>(blk[15]) << 16) |
					                              (static_cast<uint64_t>(blk[16]) << 8) |
					                              static_cast<uint64_t>(blk[17]);
					if (meta.sampleRate > 0)
						meta.durationSeconds = static_cast<double>(totalSamples) / meta.sampleRate;
					gotStreamInfo = meta.sampleRate > 0 && meta.channels > 0;
				}
				else if (btype == 4)
				{
					ParseVorbisComments(blk, blen, meta, bestPicScore);
				}
				else if (btype == 6)
				{
					ParseFlacPictureBlock(blk, blen, meta, bestPicScore);
				}
				pos += blen;
			}
			if (!gotStreamInfo) return false;
			meta.codec = "flac";
			meta.valid = meta.durationSeconds > 0.0;
			return meta.valid;
		}

		struct MpegFrameHeader
		{
			bool valid{false};
			uint32_t sampleRate{0};
			uint32_t channels{0};
			uint32_t bitrateBps{0};
			uint32_t samplesPerFrame{0};
			uint32_t frameSize{0};
			uint32_t sideInfoBytes{0};
		};

		inline MpegFrameHeader DecodeMpegFrame(const uint8_t* p, size_t avail)
		{
			MpegFrameHeader h;
			if (avail < 4) return h;
			if (p[0] != 0xFF || (p[1] & 0xE0) != 0xE0) return h;
			const int version = (p[1] >> 3) & 0x03; // 0=2.5, 1=reserved, 2=2, 3=1
			const int layer = (p[1] >> 1) & 0x03;   // 1=L3, 2=L2, 3=L1
			const int brIdx = (p[2] >> 4) & 0x0F;
			const int srIdx = (p[2] >> 2) & 0x03;
			const int pad = (p[2] >> 1) & 0x01;
			const int chMode = (p[3] >> 6) & 0x03;
			if (version == 1 || layer == 0 || brIdx == 0 || brIdx == 15 || srIdx == 3) return h;

			static constexpr uint32_t kSrV1[3] = {44100, 48000, 32000};
			static constexpr uint32_t kBrV1L1[16] = {0,32,64,96,128,160,192,224,256,288,320,352,384,416,448,0};
			static constexpr uint32_t kBrV1L2[16] = {0,32,48,56,64,80,96,112,128,160,192,224,256,320,384,0};
			static constexpr uint32_t kBrV1L3[16] = {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0};
			static constexpr uint32_t kBrV2L1[16] = {0,32,48,56,64,80,96,112,128,144,160,176,192,224,256,0};
			static constexpr uint32_t kBrV2L23[16] = {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0};

			uint32_t sr = kSrV1[srIdx];
			if (version == 2) sr /= 2;
			else if (version == 0) sr /= 4;

			uint32_t kbps = 0;
			if (version == 3)
				kbps = (layer == 3) ? kBrV1L1[brIdx] : (layer == 2) ? kBrV1L2[brIdx] : kBrV1L3[brIdx];
			else
				kbps = (layer == 3) ? kBrV2L1[brIdx] : kBrV2L23[brIdx];
			if (kbps == 0 || sr == 0) return h;

			h.sampleRate = sr;
			h.channels = (chMode == 3) ? 1u : 2u;
			h.bitrateBps = kbps * 1000u;
			if (layer == 3) // Layer I
			{
				h.samplesPerFrame = 384;
				h.frameSize = ((12u * h.bitrateBps) / sr + static_cast<uint32_t>(pad)) * 4u;
			}
			else if (layer == 2) // Layer II
			{
				h.samplesPerFrame = 1152;
				h.frameSize = (144u * h.bitrateBps) / sr + static_cast<uint32_t>(pad);
			}
			else // Layer III
			{
				h.samplesPerFrame = (version == 3) ? 1152u : 576u;
				h.frameSize = ((version == 3 ? 144u : 72u) * h.bitrateBps) / sr + static_cast<uint32_t>(pad);
				h.sideInfoBytes = (version == 3) ? (h.channels == 1 ? 17u : 32u) : (h.channels == 1 ? 9u : 17u);
			}
			h.valid = h.frameSize >= 4;
			return h;
		}

		inline bool ParseMp3(const std::vector<uint8_t>& buf, Metadata& meta)
		{
			size_t audioStart = 0;
			size_t audioEnd = buf.size();
			int bestPicScore = 0;
			if (buf.size() >= 10 && std::memcmp(buf.data(), "ID3", 3) == 0)
				audioStart = ParseId3v2At(buf, 0, meta, bestPicScore);
			if (audioEnd >= audioStart + 128 && std::memcmp(buf.data() + audioEnd - 128, "TAG", 3) == 0)
			{
				const uint8_t* id3v1 = buf.data() + audioEnd - 128;
				if (meta.title.empty()) meta.title = CodepageOrLatin1ToUtf8(id3v1 + 3, 30);
				if (meta.artist.empty()) meta.artist = CodepageOrLatin1ToUtf8(id3v1 + 33, 30);
				if (meta.album.empty()) meta.album = CodepageOrLatin1ToUtf8(id3v1 + 63, 30);
				audioEnd -= 128;
			}

			MpegFrameHeader first;
			size_t firstPos = audioStart;
			const size_t scanLimit = (std::min)(audioEnd, audioStart + 65536);
			for (size_t i = audioStart; i + 4 <= scanLimit; ++i)
			{
				const auto cand = DecodeMpegFrame(buf.data() + i, audioEnd - i);
				if (!cand.valid) continue;
				if (i + cand.frameSize + 4 <= audioEnd)
				{
					const auto next = DecodeMpegFrame(buf.data() + i + cand.frameSize, audioEnd - (i + cand.frameSize));
					if (!next.valid || next.sampleRate != cand.sampleRate) continue;
				}
				first = cand;
				firstPos = i;
				break;
			}
			if (!first.valid) return false;
			meta.sampleRate = first.sampleRate;
			meta.channels = first.channels;
			meta.codec = "mp3";

			// Check Xing / Info / VBRI header inside first frame
			uint32_t totalFrames = 0;
			const size_t xingPos = firstPos + 4 + first.sideInfoBytes;
			if (xingPos + 12 <= audioEnd &&
			    (std::memcmp(buf.data() + xingPos, "Xing", 4) == 0 || std::memcmp(buf.data() + xingPos, "Info", 4) == 0))
			{
				const uint32_t xflags = ReadBE32(buf.data() + xingPos + 4);
				if ((xflags & 0x01u) != 0 && xingPos + 12 <= audioEnd)
					totalFrames = ReadBE32(buf.data() + xingPos + 8);
			}
			else if (firstPos + 36 + 18 <= audioEnd && std::memcmp(buf.data() + firstPos + 36, "VBRI", 4) == 0)
			{
				totalFrames = ReadBE32(buf.data() + firstPos + 36 + 14);
			}

			if (totalFrames > 0)
			{
				meta.durationSeconds = static_cast<double>(totalFrames) * first.samplesPerFrame / first.sampleRate;
			}
			else if (audioEnd > firstPos)
			{
				const uint64_t bytes = audioEnd - firstPos;
				meta.durationSeconds = (static_cast<double>(bytes) * 8.0) / first.bitrateBps;
			}
			meta.valid = meta.durationSeconds > 0.0;
			return meta.valid;
		}

		inline bool ParseOgg(const std::vector<uint8_t>& buf, Metadata& meta)
		{
			if (buf.size() < 28 || std::memcmp(buf.data(), "OggS", 4) != 0) return false;
			uint32_t targetSerial = 0;
			bool hasSerial = false;
			uint64_t maxGranule = 0;
			std::vector<std::vector<uint8_t>> packets;
			std::vector<uint8_t> curPacket;
			size_t pos = 0;
			while (pos + 27 <= buf.size())
			{
				if (std::memcmp(buf.data() + pos, "OggS", 4) != 0) break;
				const uint32_t serial = ReadLE32(buf.data() + pos + 14);
				if (!hasSerial) { targetSerial = serial; hasSerial = true; }
				const uint8_t nSegs = buf[pos + 26];
				if (pos + 27 + nSegs > buf.size()) break;
				uint64_t granule = static_cast<uint64_t>(ReadLE32(buf.data() + pos + 6)) |
				                   (static_cast<uint64_t>(ReadLE32(buf.data() + pos + 10)) << 32);
				size_t bodySize = 0;
				for (uint8_t i = 0; i < nSegs; ++i) bodySize += buf[pos + 27 + i];
				const size_t bodyStart = pos + 27 + nSegs;
				if (bodyStart + bodySize > buf.size()) break;
				if (serial == targetSerial)
				{
					if (granule != 0xFFFFFFFFFFFFFFFFULL && granule > maxGranule) maxGranule = granule;
					if (packets.size() < 3)
					{
						size_t cursor = bodyStart;
						for (uint8_t i = 0; i < nSegs && packets.size() < 3; ++i)
						{
							const uint8_t segLen = buf[pos + 27 + i];
							curPacket.insert(curPacket.end(), buf.data() + cursor, buf.data() + cursor + segLen);
							cursor += segLen;
							if (segLen < 255)
							{
								packets.push_back(std::move(curPacket));
								curPacket.clear();
							}
						}
					}
				}
				pos = bodyStart + bodySize;
			}
			if (packets.empty()) return false;
			int bestPicScore = 0;
			const auto& p0 = packets[0];
			if (p0.size() >= 28 && std::memcmp(p0.data(), "\x01vorbis", 7) == 0)
			{
				meta.channels = p0[11];
				meta.sampleRate = ReadLE32(p0.data() + 12);
				meta.codec = "ogg";
				if (packets.size() >= 2 && packets[1].size() >= 15 && std::memcmp(packets[1].data(), "\x03vorbis", 7) == 0)
					ParseVorbisComments(packets[1].data() + 7, packets[1].size() - 7, meta, bestPicScore);
				if (meta.sampleRate > 0 && maxGranule > 0)
					meta.durationSeconds = static_cast<double>(maxGranule) / meta.sampleRate;
			}
			else if (p0.size() >= 19 && std::memcmp(p0.data(), "OpusHead", 8) == 0)
			{
				meta.channels = p0[9];
				const uint16_t preSkip = ReadLE16(p0.data() + 10);
				meta.sampleRate = 48000;
				meta.codec = "opus";
				if (packets.size() >= 2 && packets[1].size() >= 16 && std::memcmp(packets[1].data(), "OpusTags", 8) == 0)
					ParseVorbisComments(packets[1].data() + 8, packets[1].size() - 8, meta, bestPicScore);
				if (maxGranule > preSkip)
					meta.durationSeconds = static_cast<double>(maxGranule - preSkip) / 48000.0;
			}
			else return false;

			meta.valid = meta.sampleRate > 0 && meta.channels > 0 && meta.durationSeconds > 0.0;
			return meta.valid;
		}

		inline void WalkMp4Boxes(const uint8_t* data, size_t len, int depth, Metadata& meta)
		{
			if (depth > 12) return;
			size_t pos = 0;
			while (pos + 8 <= len)
			{
				uint64_t boxSize = ReadBE32(data + pos);
				std::string_view type(reinterpret_cast<const char*>(data + pos + 4), 4);
				size_t hdr = 8;
				if (boxSize == 1 && pos + 16 <= len)
				{
					boxSize = ReadBE64(data + pos + 8);
					hdr = 16;
				}
				else if (boxSize == 0)
				{
					boxSize = len - pos;
				}
				if (boxSize < hdr || pos + boxSize > len) break;
				const uint8_t* payload = data + pos + hdr;
				const size_t plen = static_cast<size_t>(boxSize - hdr);

				if (type == "moov" || type == "udta" || type == "trak" || type == "mdia" ||
				    type == "minf" || type == "stbl" || type == "ilst")
				{
					WalkMp4Boxes(payload, plen, depth + 1, meta);
				}
				else if (type == "meta" && plen >= 4)
				{
					WalkMp4Boxes(payload + 4, plen - 4, depth + 1, meta);
				}
				else if (type == "mvhd" && plen >= 20)
				{
					const uint8_t ver = payload[0];
					if (ver == 0 && plen >= 20)
					{
						const uint32_t ts = ReadBE32(payload + 12);
						const uint32_t dur = ReadBE32(payload + 16);
						if (ts > 0) meta.durationSeconds = static_cast<double>(dur) / ts;
					}
					else if (ver == 1 && plen >= 32)
					{
						const uint32_t ts = ReadBE32(payload + 20);
						const uint64_t dur = ReadBE64(payload + 24);
						if (ts > 0) meta.durationSeconds = static_cast<double>(dur) / ts;
					}
				}
				else if (type == "stsd" && plen >= 44)
				{
					// Sample description entries start at offset 8
					const uint8_t* entry = payload + 8;
					const size_t elen = plen - 8;
					if (elen >= 36)
					{
						const std::string_view fmt(reinterpret_cast<const char*>(entry + 4), 4);
						if (fmt == "mp4a" || fmt == "alac" || fmt == "Opus")
						{
							if (meta.channels == 0) meta.channels = ReadBE16(entry + 24);
							if (meta.sampleRate == 0) meta.sampleRate = ReadBE16(entry + 32);
						}
					}
				}
				else if (type == "\xA9" "nam" || type == "\xA9" "ART" || type == "aART" || type == "\xA9" "alb" || type == "covr")
				{
					// Inside ilst item: search for 'data' sub-box
					size_t p2 = 0;
					while (p2 + 16 <= plen)
					{
						const uint32_t s = ReadBE32(payload + p2);
						if (s < 8 || p2 + s > plen) break;
						if (std::memcmp(payload + p2 + 4, "data", 4) == 0 && s >= 16)
						{
							const uint32_t dataType = ReadBE32(payload + p2 + 8);
							const uint8_t* vptr = payload + p2 + 16;
							const size_t vlen = s - 16;
							if (type == "\xA9" "nam" && meta.title.empty())
								meta.title = TrimNullAndSpace(std::string(reinterpret_cast<const char*>(vptr), vlen));
							else if ((type == "\xA9" "ART" || type == "aART") && meta.artist.empty())
								meta.artist = TrimNullAndSpace(std::string(reinterpret_cast<const char*>(vptr), vlen));
							else if (type == "\xA9" "alb" && meta.album.empty())
								meta.album = TrimNullAndSpace(std::string(reinterpret_cast<const char*>(vptr), vlen));
							else if (type == "covr" && meta.coverBytes.empty() && vlen > 0)
							{
								const std::string hint = (dataType == 14) ? "image/png" : "image/jpeg";
								const std::string mime = DetectImageMime(vptr, vlen, hint);
								if (!mime.empty())
								{
									meta.coverMime = mime;
									meta.coverBytes.assign(vptr, vptr + vlen);
								}
							}
						}
						p2 += s;
					}
				}
				pos += static_cast<size_t>(boxSize);
			}
		}

		inline bool ParseMp4(const std::vector<uint8_t>& buf, Metadata& meta)
		{
			if (buf.size() < 12 || std::memcmp(buf.data() + 4, "ftyp", 4) != 0) return false;
			WalkMp4Boxes(buf.data(), buf.size(), 0, meta);
			if (meta.sampleRate == 0) meta.sampleRate = 48000;
			if (meta.channels == 0) meta.channels = 2;
			meta.codec = "m4a";
			meta.valid = meta.durationSeconds > 0.0;
			return meta.valid;
		}

		inline bool ParseAdtsAac(const std::vector<uint8_t>& buf, Metadata& meta)
		{
			size_t pos = 0;
			int bestPicScore = 0;
			if (buf.size() >= 10 && std::memcmp(buf.data(), "ID3", 3) == 0)
				pos = ParseId3v2At(buf, 0, meta, bestPicScore);
			if (pos + 7 > buf.size()) return false;
			static constexpr uint32_t kAdtsSr[16] = {
				96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
				16000, 12000, 11025, 8000, 7350, 0, 0, 0
			};
			uint64_t totalFrames = 0;
			uint32_t firstSr = 0, firstCh = 0;
			while (pos + 7 <= buf.size())
			{
				const uint8_t* p = buf.data() + pos;
				if (p[0] != 0xFF || (p[1] & 0xF6) != 0xF0) break;
				const int srIdx = (p[2] >> 2) & 0x0F;
				const uint32_t ch = ((p[2] & 0x01u) << 2) | (p[3] >> 6);
				const uint32_t frameLen = ((static_cast<uint32_t>(p[3] & 0x03u) << 11) |
				                           (static_cast<uint32_t>(p[4]) << 3) |
				                           (static_cast<uint32_t>(p[5]) >> 5));
				const uint32_t sr = kAdtsSr[srIdx];
				if (sr == 0 || frameLen < 7 || pos + frameLen > buf.size()) break;
				if (firstSr == 0) { firstSr = sr; firstCh = ch > 0 ? ch : 2; }
				++totalFrames;
				pos += frameLen;
			}
			if (totalFrames == 0 || firstSr == 0) return false;
			meta.sampleRate = firstSr;
			meta.channels = firstCh;
			meta.durationSeconds = (static_cast<double>(totalFrames) * 1024.0) / firstSr;
			meta.codec = "aac";
			meta.valid = meta.durationSeconds > 0.0;
			return meta.valid;
		}
	} // namespace detail

	inline Metadata ProbeBytes(const std::vector<uint8_t>& buf, std::string_view extensionHint = "")
	{
		Metadata meta;
		if (buf.size() < 12) return meta;
		const std::string ext = LowerAscii(extensionHint);

		if (std::memcmp(buf.data(), "RIFF", 4) == 0 && std::memcmp(buf.data() + 8, "WAVE", 4) == 0)
			if (detail::ParseWav(buf, meta)) return meta;
		if (std::memcmp(buf.data(), "fLaC", 4) == 0 ||
		    (ext == ".flac" && std::memcmp(buf.data(), "ID3", 3) == 0))
			if (detail::ParseFlac(buf, meta)) return meta;
		if (std::memcmp(buf.data(), "OggS", 4) == 0)
			if (detail::ParseOgg(buf, meta)) return meta;
		if (std::memcmp(buf.data() + 4, "ftyp", 4) == 0)
			if (detail::ParseMp4(buf, meta)) return meta;
		if (ext == ".aac")
		{
			if (detail::ParseAdtsAac(buf, meta)) return meta;
		}
		if (detail::ParseMp3(buf, meta)) return meta;
		if (detail::ParseAdtsAac(buf, meta)) return meta;
		return meta;
	}

	inline Metadata ProbeFilePath(const std::filesystem::path& path)
	{
		if (path.empty()) return {};
		std::error_code ec;
		if (!std::filesystem::is_regular_file(path, ec)) return {};
		const auto fileSize = std::filesystem::file_size(path, ec);
		if (ec || fileSize < 12 || fileSize > 1024ULL * 1024ULL * 1024ULL) return {};
		std::ifstream in(path, std::ios::binary);
		if (!in) return {};
		std::vector<uint8_t> buf(static_cast<size_t>(fileSize));
		if (!in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()))) return {};
		return ProbeBytes(buf, path.extension().string());
	}

	inline Metadata ProbeFileUtf8(std::string_view utf8Path)
	{
		if (utf8Path.empty() || IsNetworkPath(utf8Path)) return {};
		const auto p = PathFromUtf8(utf8Path);
		if (p.empty() || !IsSupportedAudioExtension(p.extension().string())) return {};
		return ProbeFilePath(p);
	}
} // namespace GakuAudioMeta
