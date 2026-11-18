#pragma once

static void TestMikuMouthFit()
{
    g_current = "Miku raw-source character mouth fit";
    Check(kMikuMouthFits.size() == 4, "four independent fitted profiles are generated");
    if (kMikuMouthFits.empty()) return;
    for (const auto& profile : kMikuMouthFits)
    {
        Check(FindMikuMouthFace(profile.faceName) == &profile &&
              FindMikuMouthFace(std::string(profile.faceName)+"(Clone)") == &profile,
              "exact face prefab identity permits its Unity clone suffix");
        for (size_t node = 0; node < profile.inputs.size(); ++node)
        {
            std::array<double, 9> output{};
            bool exact = EvaluateMikuMouthProfile(profile, profile.inputs[node], output);
            for (size_t channel = 0; channel < 9; ++channel)
                exact = exact && std::fabs(output[channel]-profile.outputs[node][channel]) < 1e-8;
            Check(exact, "pure C++ evaluator reproduces the frozen training node");
        }
    }
    Check(!FindMikuMouthFace("prefix_mdl_chr_atbm-base-0000_face") &&
          !FindMikuMouthFace("mdl_chr_atbm-base-0001_face") && !FindMikuMouthProfile("ttmr"),
          "unfitted actor/face variant and substring identities are refused");
    const auto& profile = kMikuMouthFits[0];
    std::vector<std::string> names(std::begin(kMikuRawNames), std::end(kMikuRawNames));
    std::vector<float> raw{.25f,0,0,0,0,1,0,0};
    std::vector<std::string> available(profile.shapes.begin(), profile.shapes.end());
    available.push_back("b_mouth.mouth_031");
    available.push_back("b_brow.brow_003");
    auto attempt = [&](const std::vector<std::string>& inputNames, const std::vector<float>& input,
                       const std::string& faceName, const std::string& overrideCode,
                       const std::vector<std::string>& outputs)
    {
        std::vector<float> adapted(outputs.size(), .4f);
        const auto before = adapted;
        const auto decision = TryApplyMikuMouthFit(inputNames, input, outputs, faceName, overrideCode, adapted);
        if (decision != MikuFitDecision::Applied) Check(adapted == before, "every refusal preserves complete old-adapter output");
        return std::make_pair(decision, adapted);
    };
    auto fitted = attempt(names, raw, profile.faceName, "auto", available);
    Check(fitted.first == MikuFitDecision::Applied && fitted.second[9] == 0 && fitted.second[10] == .4f,
          "fit replaces whole mouth, including prior smile, while preserving brows");
    auto zero = raw; zero[0] = 0;
    auto neutral = attempt(names, zero, profile.faceName, "auto", available);
    Check(neutral.first == MikuFitDecision::Applied &&
          std::all_of(neutral.second.begin(), neutral.second.begin()+10, [](float value) { return value == 0; }),
          "zero source produces zero mouth without readding character baseline");
    auto missing = names; missing.erase(missing.begin()+3);
    Check(attempt(missing, raw, profile.faceName, "auto", available).first == MikuFitDecision::MissingRawSource,
          "E cannot be inferred from baked native027 or remaining source tracks");
    Check(attempt({"mmd2.a","b_mouth.mouth_027"}, {.25f,.3f}, profile.faceName, "auto", available).first == MikuFitDecision::MissingRawSource,
          "old semantic/native clips never masquerade as raw source vowels");
    auto authored = raw; authored[7] = 1;
    Check(attempt(names, authored, profile.faceName, "auto", available).first == MikuFitDecision::NativeAuthored,
          "authored native mouth, including a zero source track, retains precedence");
    auto unsupported = raw; unsupported[6] = .001f;
    Check(attempt(names, unsupported, profile.faceName, "auto", available).first == MikuFitDecision::UnsupportedSource,
          "unsupported source mouth does not hide behind an activity threshold");
    auto outside = raw; outside[1] = 1;
    Check(attempt(names, outside, profile.faceName, "auto", available).first == MikuFitDecision::OutsideDomain,
          "out-of-domain mixtures fall back without input normalization");
    auto roundoff = raw; roundoff[0] = .6f; roundoff[1] = .4f;
    const auto unchangedRaw = roundoff;
    Check(attempt(names, roundoff, profile.faceName, "auto", available).first == MikuFitDecision::Applied && roundoff == unchangedRaw,
          "float32 simplex-boundary roundoff is tolerated without rewriting or scaling source input");
    roundoff[0] = .6001f;
    Check(attempt(names, roundoff, profile.faceName, "auto", available).first == MikuFitDecision::OutsideDomain,
          "roundoff tolerance does not admit a genuinely oversummed mixture");
    auto invalid = raw; invalid[0] = std::numeric_limits<float>::quiet_NaN();
    Check(attempt(names, invalid, profile.faceName, "auto", available).first == MikuFitDecision::InvalidRawSource,
          "nonfinite raw input is not clamped into the fitted domain");
    invalid = raw; invalid[5] = 0;
    Check(attempt(names, invalid, profile.faceName, "auto", available).first == MikuFitDecision::InvalidRawSource,
          "present marker must establish known source provenance");
    Check(attempt(names, raw, "mdl_chr_hski-base-0000_face", profile.character, available).first == MikuFitDecision::UnknownFace,
          "manual role override cannot invent actual face identity");
    Check(attempt(names, raw, profile.faceName, "ttmr", available).first == MikuFitDecision::CharacterMismatch,
          "a conflicting manual character override falls back");
    auto incomplete = available; incomplete.erase(incomplete.begin()+2);
    Check(attempt(names, raw, profile.faceName, "auto", incomplete).first == MikuFitDecision::MissingOutput,
          "all nine exact native outputs are required before replacement");

    struct FaceState { std::vector<std::string> names; std::vector<float> weights; std::string faceName; };
    static FaceState state;
    state.names = available; state.weights.assign(available.size(), 17.f); state.faceName = profile.faceName;
    FaceApi api;
    api.getModel = [](void*)->void* { return &state; };
    api.getCount = [](void*) { return static_cast<int>(state.names.size()); };
    api.getName = [](void*, int slot) { return state.names[slot]; };
    api.getWeight = [](void*, int slot) { return state.weights[slot]; };
    api.setWeight = [](void*, int slot, float value) { state.weights[slot] = value; };
    api.getActorName = [](void*) { return std::string("chr_ttmr"); }; // Actual face controls identity.
    api.getModelName = [](void*) { return state.faceName; };
    MotionClip clip; clip.fps = 1; clip.total_frames = 2;
    clip.blendshape_names = names;
    clip.blendshape_names.push_back("mmd2.a");
    clip.blendshape_names.push_back("b_brow.brow_003");
    clip.blendshape_weights = {.25f,0,0,0,0,1,0,0,.6f,.2f, 0,0,0,0,0,1,0,0,0,.2f};
    BlendshapeBinding binding; binding.enableMikuMouthFit = true;
    void* actor = reinterpret_cast<void*>(0x4d494b55);
    binding.Apply(actor, clip, 0, api);
    std::array<double, 9> expected{};
    EvaluateMikuMouthProfile(profile, {.25,0,0,0,0}, expected);
    bool correct = binding.mikuFitDecision == MikuFitDecision::Applied;
    for (size_t i = 0; i < 9; ++i) correct = correct && std::fabs(state.weights[i]-expected[i]*100.) < 1e-4;
    Check(correct && state.weights[9] == 0 && Near(state.weights[10],20),
          "actual clip sampler/binding/setter uses raw vowels and percentage units with unchanged brows");
    binding.Apply(actor, clip, 1, api);
    Check(std::all_of(state.weights.begin(), state.weights.begin()+10, [](float value) { return value == 0; }),
          "real consumer zero frame does not restore or add mouth baseline");
    binding.Apply(actor, clip, 0, api);
    const auto first = state.weights;
    binding.Apply(actor, clip, 0, api);
    Check(first == state.weights, "paused and backward seek evaluation has no hidden history");
    binding.enableMikuMouthFit = false;
    binding.Apply(actor, clip, 0, api);
    Check(binding.mikuFitDecision == MikuFitDecision::Disabled && !binding.boundMikuMouthFit,
          "changing the experimental mode releases and rebuilds the old face lease");
    binding.Restore(actor, api);
    Check(std::all_of(state.weights.begin(), state.weights.end(), [](float value) { return value == 17.f; }),
          "Stop after switching modes restores the original native face instead of fitted weights");
}
