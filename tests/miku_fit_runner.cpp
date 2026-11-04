#include "GkMikuFacialFit.hpp"
#include "GkJsonLite.hpp"
#include <iostream>
#include <iomanip>

int main()
{
    std::string line;
    while (std::getline(std::cin, line))
    {
        GakuMotion::jsonlite::Value root;
        std::string error;
        std::array<double, 5> source{};
        std::array<double, 9> output{};
        const GakuMotion::MikuMouthFitProfile* profile = nullptr;
        bool applied = false;
        const char* decision = "not_applied";
        if (line.size() <= 4096 && GakuMotion::jsonlite::ParseText(line, root, error))
        {
            const auto* code = root.Find("character");
            const auto* known = root.Find("source_known");
            const auto* vowels = root.Find("source_vowels");
            const auto* mode = root.Find("mode");
            const bool bridge = mode && mode->IsString() && mode->stringValue == "bridge";
            bool valid = code && code->IsString() && known && known->IsBool() && known->boolValue &&
                         vowels && vowels->IsArray() && vowels->Size() == 5;
            if (code && code->IsString()) profile = GakuMotion::FindMikuMouthProfile(code->stringValue);
            if (bridge)
            {
                const auto* raw = root.Find("raw8");
                const auto* model = root.Find("model_name");
                const auto* overrideCode = root.Find("character_override");
                std::vector<std::string> names, available;
                std::vector<float> input, adapted;
                if (known && known->IsBool() && known->boolValue && raw && raw->IsArray() && raw->Size() == 8)
                {
                    bool numbers = true;
                    for (size_t i = 0; i < 8; ++i)
                    {
                        const auto* item = raw->At(i);
                        numbers = numbers && item && item->IsNumber();
                        input.push_back(item && item->IsNumber() ? static_cast<float>(item->numberValue) : 0);
                    }
                    if (numbers) names.assign(std::begin(GakuMotion::kMikuRawNames), std::end(GakuMotion::kMikuRawNames));
                }
                if (profile) available.assign(profile->shapes.begin(), profile->shapes.end());
                adapted.assign(available.size(), 0);
                const std::string actualName = model && model->IsString() ? model->stringValue : profile ? profile->faceName : "";
                const std::string characterOverride = overrideCode && overrideCode->IsString() ? overrideCode->stringValue : "auto";
                const auto result = GakuMotion::TryApplyMikuMouthFit(names, input, available, actualName, characterOverride, adapted);
                applied = result == GakuMotion::MikuFitDecision::Applied;
                decision = GakuMotion::MikuFitDecisionName(result);
                if (applied && adapted.size() == 9)
                    for (size_t i = 0; i < 9; ++i) output[i] = adapted[i];
            }
            else
            {
                if (valid) for (size_t i = 0; i < 5; ++i)
                {
                    const auto* item = vowels->At(i);
                    valid = valid && item && item->IsNumber();
                    if (item && item->IsNumber()) source[i] = item->numberValue;
                }
                if (valid && profile) applied = GakuMotion::EvaluateMikuMouthProfile(*profile, source, output);
                if (applied) decision = "applied";
            }
        }
        if (!applied) output.fill(0);
        std::cout << "{\"applied\":" << (applied ? "true" : "false") << ",\"weights\":[";
        for (size_t i = 0; i < output.size(); ++i)
            std::cout << (i ? "," : "") << std::setprecision(17) << output[i];
        std::cout << "],\"character\":\"" << (profile ? profile->character : "")
                  << "\",\"mapping_sha256\":\"" << (profile ? profile->mappingSha256 : "")
                  << "\",\"decision\":\"" << decision << "\"}\n";
    }
    return 0;
}
