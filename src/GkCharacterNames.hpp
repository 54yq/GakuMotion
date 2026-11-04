#pragma once
#include <cctype>
#include <string>

namespace GakuMotion
{
	struct IdolName { const char* code; const char* chinese; };
	inline constexpr IdolName kIdolNames[] = {
		{"hski", "花海咲季"}, {"ttmr", "月村手毬"}, {"krnh", "有栖川凛花"},
		{"fktn", "藤田琴音"}, {"atbm", "有村麻央"}, {"kcna", "仓本千奈"},
		{"shro", "篠泽广"}, {"ssmk", "紫云清夏"}, {"kllj", "葛城莉莉娅"},
		{"hume", "花海佑芽"}, {"hmsz", "姬崎莉波"}, {"jsna", "十王星南"},
		{"hrnm", "秦谷美铃"}, {"amao", "雨夜燕"}, {"nasr", "根绪亚纱里"}
	};

	inline std::string DetectIdolCharacterCode(std::string name)
	{
		for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		std::string result;
		for (const auto& idol : kIdolNames)
			for (size_t pos = name.find(idol.code); pos != std::string::npos; pos = name.find(idol.code, pos + 1))
			{
				const size_t end = pos + 4;
				if ((pos == 0 || !std::isalnum(static_cast<unsigned char>(name[pos - 1]))) &&
					(end == name.size() || !std::isalnum(static_cast<unsigned char>(name[end]))))
				{
					if (!result.empty() && result != idol.code) return {};
					result = idol.code;
				}
			}
		return result;
	}

	inline const char* LookupIdolChineseName(const char* code)
	{
		if (!code || !code[0]) return nullptr;
		std::string normalized = code;
		for (auto& c : normalized) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		for (const auto& idol : kIdolNames) if (normalized == idol.code) return idol.chinese;
		return nullptr;
	}

	inline std::string FormatIdolDisplayName(const char* code)
	{
		if (!code || !code[0] || std::string(code) == "auto") return "当前角色";
		if (const char* name = LookupIdolChineseName(code)) return std::string(name) + " (" + code + ")";
		return code;
	}
}
