#pragma once
#include "GkFacialAdapterModel.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace GakuMotion
{
    inline unsigned FacialFamily(const std::string& name)
    {
        if (name.rfind("b_brow.brow_", 0) == 0) return 1;
        if (name.rfind("b_eye.eye_", 0) == 0) return 2;
        if (name.rfind("b_mouth.mouth_", 0) == 0) return 4;
        return 0;
    }

    struct FacialWeights
    {
        float overall{1.0f}, brow{1.0f}, eye{1.0f}, mouth{1.0f};

        static float SafeMultiplier(float value)
        {
            return std::isfinite(value) ? std::clamp(value, 0.0f, 2.0f) : 1.0f;
        }

        float Apply(const std::string& name, float weight) const
        {
            const unsigned family = FacialFamily(name);
            // Do not alter unrelated native or corrective channels.
            if (!family) return weight;
            const float part = family == 1 ? brow : family == 2 ? eye : mouth;
            if (!std::isfinite(weight)) return 0.0f;
            const float multiplier = SafeMultiplier(overall) * SafeMultiplier(part);
            // Preserve an intentionally over-range pre-existing baseline at
            // the default while never amplifying beyond that existing value.
            return std::clamp(weight * multiplier, 0.0f, (std::max)(1.0f, weight));
        }
    };

    inline const CharacterBaselineProfile* FindCharacterBaseline(const std::string& codeOrName)
    {
        if (codeOrName.empty()) return nullptr;
        std::string lower = codeOrName;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (const auto& profile : kCharacterFaceDefaults)
            if (lower.find(profile.code) != std::string::npos) return &profile;
        return nullptr; // No fabricated default for an unknown/uncached character.
    }

    inline const FacialVariantPrior* FacialSemantic(const std::string& name)
    {
        for (const auto& prior : kFacialVariantPriors)
            if (name == prior.semantic) return &prior;
        return nullptr;
    }

    // Default yielding is continuous only within the existing small activity
    // threshold. Clear intent still owns its family completely; no weight sum
    // or time/history state changes the authored phoneme or native variant.
    inline float FacialDefaultPresence(float activity)
    {
        if (activity <= 0.0f) return 1.0f;
        if (!std::isfinite(activity) || activity >= kFacialActivityThreshold) return 0.0f;
        const float u = activity / kFacialActivityThreshold;
        const float remaining = 1.0f - u;
        // 1 - smoothstep(u), written without cancellation near u == 1.
        return remaining * remaining * (1.0f + 2.0f * u);
    }

    // Pure evaluation at clip time: pause/repeat/seek and actor switching do not
    // inherit hidden wall-clock state. Explicit native variants take precedence.
    inline std::vector<float> EvaluateFacialAdapter(
        const std::vector<std::string>& inputNames, const std::vector<float>& input,
        const std::vector<std::string>& available, const CharacterBaselineProfile* profile,
        bool enableDefault = true, float defaultMultiplier = 1.0f)
    {
        auto value = [&](const std::string& name)
        {
            const auto it = std::find(inputNames.begin(), inputNames.end(), name);
            const size_t i = static_cast<size_t>(it - inputNames.begin());
            return it != inputNames.end() && i < input.size() && std::isfinite(input[i])
                ? std::clamp(input[i], 0.0f, 1.0f) : 0.0f;
        };
        std::vector<float> out;
        out.reserve(available.size());
        for (const auto& name : available) out.push_back(value(name));
        for (const auto& prior : kFacialVariantPriors)
        {
            const float semantic = value(prior.semantic);
            bool authoredVariant = false;
            for (const char* name : prior.candidates)
                if (name && value(name) > kFacialActivityThreshold) authoredVariant = true;
            if (authoredVariant) continue;
            // Learned ordered prior; target models missing a variant use the
            // next observed member of the same semantic group.
            for (const char* name : prior.candidates)
            {
                if (!name) continue;
                const auto it = std::find(available.begin(), available.end(), name);
                if (it == available.end()) continue;
                const size_t i = static_cast<size_t>(it - available.begin());
                out[i] = (std::max)(out[i], semantic);
                break;
            }
        }
        float mouthActivity = 0.0f, browActivity = 0.0f, eyeActivity = 0.0f;
        for (size_t i = 0; i < out.size(); ++i)
        {
            const unsigned family = FacialFamily(available[i]);
            if (family == 4) mouthActivity = (std::max)(mouthActivity, out[i]);
            if (family == 1) browActivity = (std::max)(browActivity, out[i]);
            if (family == 2) eyeActivity = (std::max)(eyeActivity, out[i]);
        }
        // Character defaults are a prior for absent intent, not an additive
        // smile. Clear activity yields the entire corresponding default family;
        // the subthreshold onset/release fades continuously to/from that state.
        if (enableDefault && profile)
        {
            const float multiplier = std::isfinite(defaultMultiplier) ? std::clamp(defaultMultiplier, 0.0f, 2.0f) : 1.0f;
            for (size_t j = 0; j < profile->channelCount; ++j)
            {
                const auto& baseline = profile->channels[j];
                const auto it = std::find(available.begin(), available.end(), baseline.name);
                if (it == available.end()) continue;
                const unsigned family = FacialFamily(baseline.name);
                const float activity = family == 4 ? mouthActivity : family == 1 ? browActivity : eyeActivity;
                const float presence = FacialDefaultPresence(activity);
                if (presence <= 0.0f) continue;
                const size_t i = static_cast<size_t>(it - available.begin());
                out[i] = (std::max)(out[i], baseline.weight * multiplier * presence);
            }
        }
        return out;
    }
}
