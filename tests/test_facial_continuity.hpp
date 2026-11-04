#pragma once

// Guard the real v2 adapter and setter against the observed binary default jump.
// Closed-form midpoint and continuity limits are independent of implementation.
static void TestFacialDefaultContinuity()
{
    g_current = "v2 facial default subthreshold continuity";
    const std::vector<std::string> mouth{"b_mouth.mouth_002", "b_mouth.mouth_024", "b_mouth.mouth_026"};
    const auto* saki = FindCharacterBaseline("hski");
    auto sample = [&](float activity)
    {
        return EvaluateFacialAdapter({"mmd2.a"}, {activity}, mouth, saki);
    };
    const auto zero = sample(0), half = sample(.0025f), clear = sample(.005f);
    Check(Near(zero[0],1) && Near(half[0],.5f) && Near(clear[0],0),
          "zero retains full default; half threshold yields half; clear intent yields all");
    Check(Near(half[1],.0025f) && Near(clear[1],.005f),
          "continuous yielding does not rescale authored phonemes");
    const auto below = sample(.005f - .000001f), above = sample(.005f + .000001f);
    Check(std::abs(below[0]-above[0])*100.f < .0001f,
          "threshold +/-1e-6 no longer changes default by 100 percentage points");

    float previous = zero[0], largestStep = 0;
    bool monotone = true, exactPhoneme = true;
    for (int i=1; i<=10000; ++i)
    {
        const float input = i*.000001f;
        const auto out = sample(input);
        largestStep = (std::max)(largestStep,std::abs(out[0]-previous)*100.f);
        monotone = monotone && out[0] <= previous+1e-6f;
        exactPhoneme = exactPhoneme && out[1] == input;
        previous = out[0];
    }
    Check(monotone && largestStep < .031f && exactPhoneme,
          "dense fade-in is monotone with bounded steps and unchanged phoneme weights");
    bool reversible = true;
    for (int i=0;i<=100;++i)
    {
        const float input = i*.0001f;
        const auto forward = sample(input);
        (void)sample(.01f-input); // reverse seek must not inherit earlier samples
        reversible = reversible && forward == sample(input);
    }
    Check(reversible, "fade-out and backward/repeated seeks use the same stateless curve");

    const std::vector<std::string> brow{"b_brow.brow_001","b_brow.brow_003","b_brow.brow_014"};
    auto midBrow = EvaluateFacialAdapter({"b_brow.brow_001"},{.0025f},brow,FindCharacterBaseline("atbm"));
    Check(Near(midBrow[0],.0025f) && Near(midBrow[1],.5f) && Near(midBrow[2],.35f),
          "brow defaults yield together continuously without changing explicit brow weight");
    const auto nearBrow = EvaluateFacialAdapter({"b_brow.brow_001"},{.005f-.000001f},brow,FindCharacterBaseline("atbm"));
    const auto clearBrow = EvaluateFacialAdapter({"b_brow.brow_001"},{.005f+.000001f},brow,FindCharacterBaseline("atbm"));
    Check(std::abs(nearBrow[1]-clearBrow[1])*100.f < .0001f &&
          std::abs(nearBrow[2]-clearBrow[2])*100.f < .0001f,
          "both 100 and 70 percent brow priors are continuous across the old gate");
    auto tooth = EvaluateFacialAdapter({"b_mouth.mouth_026"},{.0025f},mouth,saki);
    auto clearTooth = EvaluateFacialAdapter({"b_mouth.mouth_026"},{.01f},mouth,saki);
    Check(Near(tooth[0],.5f) && Near(tooth[2],.0025f) && Near(clearTooth[0],0) && Near(clearTooth[2],.01f),
          "tooth uses the same existing mouth ownership and continuous small-activity yielding");
    auto zeroTracks=EvaluateFacialAdapter({"b_mouth.mouth_002","mmd2.a"},{0,0},mouth,saki);
    auto disabled=EvaluateFacialAdapter({"mmd2.a"},{.0025f},mouth,saki,false);
    auto scaled=EvaluateFacialAdapter({"mmd2.a"},{.0025f},mouth,saki,true,.8f);
    Check(Near(zeroTracks[0],1) && Near(disabled[0],0) && Near(scaled[0],.4f),
          "zero-only tracks, baseline disable and multiplier preserve their contracts");
    auto strong=sample(.6f);
    auto native=EvaluateFacialAdapter({"mmd2.a","b_mouth.mouth_024"},{.6f,.2f},mouth,saki);
    Check(Near(strong[0],0) && Near(strong[1],.6f) && Near(native[0],0) && Near(native[1],.2f),
          "clear phonemes fully replace defaults and explicit native variants retain precedence");

    // Use actual clip sampling, Binding and percentage setter for the audit's
    // A=0->.01 case, not just a reimplementation of the scalar fade formula.
    struct State { std::vector<float> weights{31,0,0,9,4}; bool blink=true,disabled=false; };
    static State state;state=State{};
    FaceApi api;
    api.getModel=[](void*)->void* {return &state;};
    api.getCount=[](void*) {return 5;};
    api.getName=[](void*,int i) {
        static const char* names[]{"b_mouth.mouth_002","b_mouth.mouth_024","b_mouth.mouth_026","b_eye.eye_001","b_eye.eye_003"};
        return std::string(names[i]);
    };
    api.getWeight=[](void*,int i) {return state.weights[i];};
    api.setWeight=[](void*,int i,float value) {state.weights[i]=value;};
    api.getActorName=[](void*) {return std::string("chr_hski");};
    api.getFacial=[](void*)->void* {return &state;};
    api.readBlink=[](void*,bool& b,bool& d) {b=state.blink;d=state.disabled;return true;};
    api.writeBlink=[](void*,bool b,bool d) {state.blink=b;state.disabled=d;return true;};
    MotionClip clip;clip.fps=1;clip.total_frames=3;
    clip.blendshape_names={"mmd2.a"};clip.blendshape_weights={0,.01f,0};
    auto actor=reinterpret_cast<void*>(0xfc02);BlendshapeBinding binding;
    binding.Apply(actor,clip,.25f,api);const float midpoint=state.weights[0];
    binding.Apply(actor,clip,.5f,api);const float atThreshold=state.weights[0];
    binding.Apply(actor,clip,.501f,api);const float afterThreshold=state.weights[0];
    Check(Near(midpoint,50) && Near(atThreshold,0) && Near(afterThreshold,0),
          "real consumer removes the audit's .500s/.501s 100-to-zero discontinuity");
    binding.Apply(actor,clip,1.75f,api);
    Check(Near(state.weights[0],midpoint) && Near(state.weights[1],.25f),
          "real consumer fade-out restores the same default and phoneme as fade-in");
    binding.Apply(actor,clip,2.f,api);
    Check(Near(state.weights[0],100) && state.blink && !state.disabled,
          "all-zero mouth restores full default while mouth-only still leaves blink native");
    binding.Restore(actor,api);
    Check(Near(state.weights[0],31) && state.blink && !state.disabled,
          "Stop restores original native state after continuous default yielding");
    clip.total_frames=1;clip.blendshape_names={"mmd2.smile_eyes"};clip.blendshape_weights={0};
    binding.Apply(actor,clip,0,api);
    Check(!state.blink && state.disabled && Near(state.weights[3],0) && Near(state.weights[4],0) && Near(state.weights[0],31),
          "zero-only eye track retains existing whole-eye ownership and does not seize mouth");
    binding.Restore(actor,api);
    Check(state.blink && !state.disabled && Near(state.weights[3],9) && Near(state.weights[4],4),
          "zero-only eye track still restores saved blink and eye values on Stop");
}
