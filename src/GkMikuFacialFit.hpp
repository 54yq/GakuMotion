#pragma once
#include "GkMikuFacialFitModel.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace GakuMotion
{
    inline constexpr const char* kMikuRawNames[] = {
        "miku.raw.v1.a", "miku.raw.v1.i", "miku.raw.v1.u", "miku.raw.v1.e", "miku.raw.v1.o",
        "miku.raw.v1.present", "miku.raw.v1.unsupported", "miku.raw.v1.native_authored"
    };
    enum class MikuFitDecision { Disabled, MissingRawSource, InvalidRawSource, UnsupportedSource,
                                 NativeAuthored, UnknownFace, CharacterMismatch, MissingOutput,
                                 OutsideDomain, InvalidModel, Applied };
    inline const char* MikuFitDecisionName(MikuFitDecision value)
    {
        switch (value) {
        case MikuFitDecision::Disabled: return "disabled";
        case MikuFitDecision::MissingRawSource: return "missing_raw_source";
        case MikuFitDecision::InvalidRawSource: return "invalid_raw_source";
        case MikuFitDecision::UnsupportedSource: return "unsupported_source_mouth";
        case MikuFitDecision::NativeAuthored: return "native_authored_mouth";
        case MikuFitDecision::UnknownFace: return "unverified_face_identity";
        case MikuFitDecision::CharacterMismatch: return "character_override_mismatch";
        case MikuFitDecision::MissingOutput: return "missing_native_output";
        case MikuFitDecision::OutsideDomain: return "outside_fitted_domain";
        case MikuFitDecision::InvalidModel: return "invalid_fitted_model";
        case MikuFitDecision::Applied: return "applied";
        }
        return "unknown";
    }
    inline bool HasMikuRawSource(const std::vector<std::string>& names)
    {
        for (const char* name : kMikuRawNames)
            if (std::count(names.begin(), names.end(), name) != 1) return false;
        return true;
    }
    inline const MikuMouthFitProfile* FindMikuMouthProfile(const std::string& code)
    {
        for (const auto& profile : kMikuMouthFits)
            if (code == profile.character) return &profile;
        return nullptr;
    }
    inline const MikuMouthFitProfile* FindMikuMouthFace(std::string name)
    {
        const std::string clone = "(Clone)";
        if (name.size() >= clone.size() && name.compare(name.size()-clone.size(), clone.size(), clone) == 0)
        {
            name.resize(name.size()-clone.size());
            if (!name.empty() && name.back() == ' ') name.pop_back();
        }
        for (const auto& profile : kMikuMouthFits)
            if (name == profile.faceName) return &profile;
        return nullptr;
    }
    inline bool EvaluateMikuMouthProfile(const MikuMouthFitProfile& profile,
                                        const std::array<double, 5>& source,
                                        std::array<double, 9>& output,
                                        double inputTolerance = 1e-10)
    {
        constexpr double tolerance = 1e-10;
        if (!std::isfinite(inputTolerance) || inputTolerance < tolerance || inputTolerance > 1e-7) return false;
        double total = 0;
        for (double value : source)
        {
            if (!std::isfinite(value) || value < 0 || value > 1) return false;
            total += value;
        }
        if (total > 1+inputTolerance) return false; // Do not normalize an authored out-of-domain mixture.
        const bool floatBoundaryRoundoff = total > 1 && inputTolerance > tolerance;
        output.fill(0);
        if (total == 0) return true; // Predictor zero, never a nonzero neutral oracle/default.
        const std::array<double, 6> augmented{source[0], source[1], source[2], source[3], source[4], 1.};
        for (size_t cell = 0; cell < profile.cells.size(); ++cell)
        {
            std::array<double, 6> bary{};
            bool inside = true;
            double sum = 0;
            for (size_t row = 0; row < 6; ++row)
            {
                double rowScale = 0;
                for (size_t column = 0; column < 6; ++column)
                {
                    bary[row] += profile.inverses[cell][row][column]*augmented[column];
                    if (column < 5) rowScale += std::fabs(profile.inverses[cell][row][column]);
                }
                // A float32 sum excess is expressed in input-coordinate units,
                // not local barycentric units (small cells amplify it). This
                // exception is confined to the outer sum=1 boundary; interior
                // cell selection keeps the exact portable 1e-10 rule.
                const double baryTolerance = floatBoundaryRoundoff
                    ? inputTolerance*(std::max)(1., rowScale) : tolerance;
                if (!std::isfinite(bary[row]) || bary[row] < -baryTolerance) { inside = false; break; }
                bary[row] = (std::max)(0., bary[row]);
                sum += bary[row];
            }
            if (!inside || !std::isfinite(sum) || sum <= 0) continue;
            std::array<double, 5> reconstructed{};
            output.fill(0);
            for (size_t vertex = 0; vertex < 6; ++vertex)
            {
                const unsigned index = profile.cells[cell][vertex];
                if (index >= profile.outputs.size()) return false;
                const double weight = bary[vertex]/sum;
                for (size_t axis = 0; axis < 5; ++axis) reconstructed[axis] += weight*profile.inputs[index][axis];
                for (size_t channel = 0; channel < 9; ++channel) output[channel] += weight*profile.outputs[index][channel];
            }
            for (size_t axis = 0; axis < 5; ++axis)
                if (std::fabs(reconstructed[axis]-source[axis]) > (std::max)(1e-9, inputTolerance)) return false;
            for (double value : output)
                if (!std::isfinite(value) || value < -tolerance || value > 1+tolerance) return false;
            if (output[7]+output[8] > 1+tolerance) return false;
            return true; // Exact frozen lexicographic first matching cell.
        }
        return false;
    }
    inline MikuFitDecision TryApplyMikuMouthFit(
        const std::vector<std::string>& names, const std::vector<float>& input,
        const std::vector<std::string>& available, const std::string& actualFaceName,
        const std::string& characterOverride, std::vector<float>& adapted)
    {
        if (!HasMikuRawSource(names)) return MikuFitDecision::MissingRawSource;
        std::array<double, 8> raw{};
        for (size_t i = 0; i < raw.size(); ++i)
        {
            const size_t slot = static_cast<size_t>(std::find(names.begin(), names.end(), kMikuRawNames[i])-names.begin());
            if (slot >= input.size() || !std::isfinite(input[slot])) return MikuFitDecision::InvalidRawSource;
            raw[i] = input[slot];
        }
        if (std::fabs(raw[5]-1.) > 1e-10 || raw[6] < 0 || raw[7] < 0) return MikuFitDecision::InvalidRawSource;
        if (raw[7] != 0) return MikuFitDecision::NativeAuthored;
        if (raw[6] != 0) return MikuFitDecision::UnsupportedSource;
        const auto* profile = FindMikuMouthFace(actualFaceName);
        if (!profile) return MikuFitDecision::UnknownFace;
        if (!characterOverride.empty() && characterOverride != "auto" && characterOverride != profile->character)
            return MikuFitDecision::CharacterMismatch;
        std::array<size_t, 9> slots{};
        for (size_t i = 0; i < slots.size(); ++i)
        {
            if (std::count(available.begin(), available.end(), profile->shapes[i]) != 1) return MikuFitDecision::MissingOutput;
            slots[i] = static_cast<size_t>(std::find(available.begin(), available.end(), profile->shapes[i])-available.begin());
        }
        std::array<double, 9> output{};
        // Only this float32 clip bridge permits 1e-7 boundary roundoff. Raw
        // values are unchanged; the portable double core keeps its 1e-10 default.
        if (!EvaluateMikuMouthProfile(*profile, {raw[0], raw[1], raw[2], raw[3], raw[4]}, output, 1e-7))
            return MikuFitDecision::OutsideDomain;
        if (adapted.size() != available.size()) return MikuFitDecision::InvalidModel;
        for (size_t i = 0; i < adapted.size(); ++i)
            if (available[i].rfind("b_mouth.mouth_", 0) == 0) adapted[i] = 0;
        for (size_t i = 0; i < output.size(); ++i)
            adapted[slots[i]] = static_cast<float>(std::clamp(output[i], 0., 1.));
        return MikuFitDecision::Applied;
    }
}
