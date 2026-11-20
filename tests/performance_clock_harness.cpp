// Standalone transport harness. Adapter tests use the real MotionPlayer with
// scripted audio callbacks; no game, audio device, or rendering is required.
#include "GkPerformanceClock.hpp"
#ifdef GKMOTION_CLOCK_ADAPTER_TEST
#include "GkPerformanceClockAdapter.hpp"
namespace GakuMotion::Log
{
    void Write(Level level, const char* format, ...)
    {
        (void)level;
        (void)format;
    }
}
#endif
#include <cstdio>
#include <limits>

using namespace GakuMotion::performance;
static int passed = 0, failed = 0;
static void Check(bool ok, const char* name)
{
    if (ok) ++passed;
    else { ++failed; std::printf("FAIL: %s\n", name); }
}
static bool Near(double a, double b, double eps = 1e-10) { return std::fabs(a - b) <= eps; }
static constexpr double inf = std::numeric_limits<double>::infinity();
static constexpr double invalidNumber = std::numeric_limits<double>::quiet_NaN();

static void TestManual()
{
    Manual manual;
    Clock& clock = manual;
    Check(clock.Kind() == ClockKind::Manual && clock.Paused() && clock.Time().seconds == 0, "manual starts paused at zero");
    manual.SetTime(PerformanceTime{2});
    manual.Advance(100);
    Check(clock.Time().seconds == 2, "pause freezes explicit elapsed advancement");
    clock.Resume();
    manual.Advance(0.25);
    Check(clock.Time().seconds == 2.25, "resume uses supplied delta only");
    for (double rate : {0.1, 0.2, 0.5, 0.8, 1.0, 1.2, 1.5, 2.0, 3.0})
    {
        clock.Seek(PerformanceTime{});
        clock.SetPlaybackRate(rate);
        manual.Advance(2);
        Check(Near(clock.Time().seconds, 2 * rate), "manual selector rate");
    }
    clock.Pause();
    Check(clock.Seek(PerformanceTime{7}) && clock.Paused() && clock.Time().seconds == 7, "paused seek does not resume");
    manual.SetTime(PerformanceTime{9});
    Check(clock.Paused() && clock.Time().seconds == 9, "paused SetTime is absolute and ignores rate");
    Check(!clock.Seek(PerformanceTime{invalidNumber}) && !clock.Seek(PerformanceTime{inf}) && clock.Time().seconds == 9,
        "nonfinite seek rejected atomically");
    Check(clock.Seek(PerformanceTime{-1}) && clock.Time().seconds == 0, "negative seek clamps to zero");
    Check(!clock.SetPlaybackRate(invalidNumber) && !clock.SetPlaybackRate(inf), "invalid rates rejected");
    clock.SetPlaybackRate(20);
    Check(clock.PlaybackRate() == 5, "legacy rate upper clamp");
    clock.SetPlaybackRate(-1);
    Check(clock.PlaybackRate() == 0.1, "legacy rate lower clamp");
    clock.Resume();
    Check(!manual.Advance(-1) && !manual.Advance(invalidNumber) && !manual.Advance(inf) && clock.Time().seconds == 0,
        "invalid deltas do not move the clock");
    clock.SetPlaybackRate(5);
    Check(!manual.Advance((std::numeric_limits<double>::max)()) && clock.Time().seconds == 0, "overflow rejected atomically");
}

static void TestFixedStep()
{
    FixedStep fixed;
    Clock& clock = fixed;
    Check(clock.Kind() == ClockKind::FixedStep && fixed.Fps().numerator == 60, "fixed default is 60fps");
    fixed.AdvanceFrames(20);
    Check(fixed.FrameIndex() == 0 && clock.Time().seconds == 0, "paused fixed advance is inert");
    Check(fixed.SetFrameIndex(120) && clock.Time().seconds == 2 && clock.Paused(), "explicit index positioning while paused");
    clock.Resume();
    fixed.AdvanceFrames();
    Check(fixed.FrameIndex() == 121 && Near(clock.Time().seconds, 121.0 / 60), "one exact frame index step");
    clock.SetPlaybackRate(2);
    const double beforeRate = clock.Time().seconds;
    Check(Near(beforeRate, 121.0 / 60), "changing rate keeps current position");
    fixed.AdvanceFrames(30);
    Check(Near(clock.Time().seconds, beforeRate + 1), "new rate applies only after anchor");
    clock.Seek(PerformanceTime{10.125});
    fixed.AdvanceFrames(3);
    Check(Near(clock.Time().seconds, 10.225), "off-grid seek anchors subsequent steps");
    clock.Pause();
    clock.Seek(PerformanceTime{4});
    fixed.AdvanceFrames(3);
    Check(clock.Time().seconds == 4 && fixed.FrameIndex() == 154 && clock.Paused(), "paused seek keeps output index and pause");
    fixed.SetFrameRate({24, 1});
    clock.Resume();
    fixed.AdvanceFrames(12);
    Check(Near(clock.Time().seconds, 5), "FPS changes reanchor without discontinuity");
    Check(!fixed.SetFrameRate({0, 1}) && !fixed.SetFrameRate({24, 0}) && fixed.Fps().numerator == 24,
        "invalid FPS rejected atomically");
    Check(!fixed.SetFrameIndex(-1) && !fixed.AdvanceFrames(-1) && !clock.Seek(PerformanceTime{inf}) &&
        !clock.SetPlaybackRate(invalidNumber) && Near(clock.Time().seconds, 5), "invalid fixed inputs preserve state");

    for (FrameRate fps : {FrameRate{24, 1}, {30, 1}, {60, 1}, {120, 1}, {30000, 1001}})
    {
        FixedStep sequential, direct;
        sequential.SetFrameRate(fps); direct.SetFrameRate(fps);
        sequential.Resume(); direct.Resume();
        bool allSteps = true;
        for (int n = 0; n < 100000; ++n) allSteps = sequential.AdvanceFrames() && allSteps;
        direct.SetFrameIndex(100000);
        const double expected = 100000.0 * fps.denominator / fps.numerator;
        Check(allSteps && sequential.FrameIndex() == 100000 && sequential.Time().seconds == direct.Time().seconds &&
            Near(direct.Time().seconds, expected), "100000 steps equal direct indexed sample, including fractional FPS");
    }
    FixedStep fractional;
    fractional.SetFrameRate({30000, 1001});
    fractional.SetFrameIndex(30000);
    Check(fractional.Time().seconds == 1001, "30000/1001 fps reaches 1001 seconds at frame 30000");
    FixedStep reanchored, indexed;
    for (FixedStep* candidate : {&reanchored, &indexed})
    {
        candidate->SetFrameIndex(73);
        candidate->Seek(PerformanceTime{8.125});
        candidate->SetPlaybackRate(1.2);
        candidate->SetFrameRate({30000, 1001});
        candidate->Resume();
    }
    for (int i = 0; i < 10000; ++i)
    {
        reanchored.SetPlaybackRate(1.2);
        reanchored.SetFrameRate({30000, 1001});
        reanchored.AdvanceFrames();
    }
    indexed.SetFrameIndex(10073);
    Check(reanchored.Time().seconds == indexed.Time().seconds && reanchored.FrameIndex() == indexed.FrameIndex(),
        "nonzero anchor, fractional FPS and rate: repeated controls do not accumulate drift");
    FixedStep huge;
    huge.SetFrameIndex((std::numeric_limits<int64_t>::max)());
    huge.Resume();
    Check(!huge.AdvanceFrames() && huge.FrameIndex() == (std::numeric_limits<int64_t>::max)(), "frame counter overflow rejected");
    // Adjacent indices above double's exact integer range must be subtracted
    // as integers before conversion, otherwise the next step silently vanishes.
    huge.Seek(PerformanceTime{});
    huge.SetFrameIndex((std::numeric_limits<int64_t>::max)() - 1);
    Check(huge.Time().seconds == 0 && huge.FrameIndex() == (std::numeric_limits<int64_t>::max)(),
        "position before anchor cannot create negative performance time");
    FixedStep highAnchor;
    highAnchor.SetFrameIndex(9007199254740992LL);
    highAnchor.Seek(PerformanceTime{1});
    highAnchor.SetFrameIndex(9007199254740993LL);
    Check(Near(highAnchor.Time().seconds, 1 + 1.0 / 60), "large-index adjacent steps retain integer delta");
}

static void TestLoops()
{
    Manual manual;
    FixedStep fixed;
    for (Clock* clock : {static_cast<Clock*>(&manual), static_cast<Clock*>(&fixed)})
    {
        clock->SetPlaybackRate(2);
        clock->Seek(PerformanceTime{0.99});
        Check(RestartLoop(*clock, PerformanceTime{1}) == LoopResult::BeforeEnd, "loop boundary is inclusive only at end");
        clock->Seek(PerformanceTime{1});
        Check(RestartLoop(*clock, PerformanceTime{1}) == LoopResult::Restarted && clock->Time().seconds == 0 &&
            clock->Paused() && clock->PlaybackRate() == 2, "exact end resets without resuming or changing rate");
        clock->Seek(PerformanceTime{2.4});
        Check(RestartLoop(*clock, PerformanceTime{1}) == LoopResult::Restarted && clock->Time().seconds == 0,
            "loop discards overshoot like legacy player");
        Check(RestartLoop(*clock, PerformanceTime{0}) == LoopResult::InvalidDuration &&
            RestartLoop(*clock, PerformanceTime{inf}) == LoopResult::InvalidDuration, "invalid loop durations rejected");
    }
    fixed.Resume(); fixed.AdvanceFrames(30);
    Check(fixed.Time().seconds == 1 && RestartLoop(fixed, PerformanceTime{1}) == LoopResult::Restarted, "fixed loop at rate 2");
    fixed.AdvanceFrames();
    Check(fixed.FrameIndex() == 31 && Near(fixed.Time().seconds, 1.0 / 30), "loop reanchors without resetting output counter");
}

static void TestAudioView()
{
    bool threw = false;
    try { AudioDriven invalid{AudioClockBackend{}}; }
    catch (const std::invalid_argument&) { threw = true; }
    Check(threw, "partial audio binding rejected");
    Manual authority;
    AudioDriven audio{{&authority,
        [](void* p) { return static_cast<Manual*>(p)->Time(); },
        [](void* p, PerformanceTime t) { return static_cast<Manual*>(p)->Seek(t); },
        [](void* p) { static_cast<Manual*>(p)->Pause(); },
        [](void* p) { static_cast<Manual*>(p)->Resume(); },
        [](void* p) { return static_cast<Manual*>(p)->Paused(); },
        [](void* p) { return static_cast<Manual*>(p)->PlaybackRate(); },
        [](void* p, double r) { return static_cast<Manual*>(p)->SetPlaybackRate(r); }}};
    Check(audio.Kind() == ClockKind::AudioDriven && audio.Paused(), "audio view reports kind and authority state");
    audio.Resume(); authority.Advance(2);
    Check(audio.Time().seconds == 2 && audio.Time().seconds == 2, "reads neither cache nor advance authority");
    audio.Pause(); audio.Seek(PerformanceTime{5}); authority.Advance(1);
    Check(audio.Paused() && audio.Time().seconds == 5, "audio transport forwards pause and seek");
    audio.SetPlaybackRate(2); audio.Resume(); authority.Advance(1);
    Check(audio.Time().seconds == 7 && audio.PlaybackRate() == 2, "audio forwards rates and observes host advancement");
    Check(!audio.Seek(PerformanceTime{invalidNumber}) && !audio.SetPlaybackRate(inf), "audio rejects invalid input before backend");
    Check(RestartLoop(audio, PerformanceTime{1}) == LoopResult::HostOwned && audio.Time().seconds == 7,
        "local loop helper cannot bypass host audio loop");
}

#ifdef GKMOTION_CLOCK_ADAPTER_TEST
using namespace GakuMotion;
namespace Script
{
    static float position = 0, lastSeek = -1;
    static int reads = 0, plays = 0, stops = 0, seeks = 0, pausedSeeks = 0, resumes = 0;
    static bool effective = true;
    static void Prepare(MotionPlayer& player)
    {
        position = 0; lastSeek = -1;
        reads = plays = stops = seeks = pausedSeeks = resumes = 0;
        effective = true;
        player.clip.total_frames = 600;
        player.clip.fps = 30;
        player.clip.duration_seconds = 20;
        // Tick has a valid actor but BindActor exits without transform callbacks.
        player.fnPlayAudio = [](const std::string&) { ++plays; position = 0; };
        player.fnStopAudio = [] { ++stops; position = -1; };
        player.fnPauseAudio = [] {};
        player.fnResumeAudio = [](float t) { ++resumes; position = t; };
        player.fnSeekAudio = [](float t) { ++seeks; lastSeek = t; position = t; };
        player.fnSeekPausedAudio = [](float t) { ++pausedSeeks; lastSeek = t; position = t; };
        player.fnGetAudioTime = [] { ++reads; return position; };
        player.fnObserveAudio = [] {
            audio::Observation observation;
            observation.sourceOpen = true; observation.confirmed = effective; observation.sourceEpoch = 1;
            observation.state = audio::PlaybackState::Playing;
            return observation;
        };
    }
}

static void TestMotionPlayerAdapter()
{
    MotionPlayer player;
    Script::Prepare(player);
    auto clock = MakeMotionPlayerAudioDrivenClock(player);
    const auto actor = reinterpret_cast<void*>(1);
    const auto tick = [&](float dt) { player.Tick(dt, actor); };
    Check(clock.Paused() && !player.audioStarted, "factory does not play or rewire host");
    clock.Resume();
    Check(!clock.Paused() && Script::plays == 1, "stopped resume delegates Play");
    for (int i = 0; i < 3; ++i) tick(0.01f);
    Check(Script::reads == 3 && Near(clock.Time().seconds, 0.03, 1e-6) &&
        Near(player.audioSeekOffset, 0.03, 1e-6), "legacy three-frame settle and three queries retained");
    Script::position = 0.5f; tick(0.01f);
    Check(Near(clock.Time().seconds, 0.53, 1e-6), "audio remains authoritative after settle");
    const int beforeRead = Script::reads;
    clock.Time(); clock.Time();
    Check(Script::reads == beforeRead, "clock reads never poll audio or double-advance");
    Script::position = 0.495f; tick(0.01f);
    Check(Near(clock.Time().seconds, 0.525, 1e-6), "legacy tolerance allows small 5ms backstep");
    Script::position = 0.3f; tick(0.01f);
    Check(Near(clock.Time().seconds, 0.525, 1e-6), "legacy large backstep is held");
    Script::position = -1; tick(0.01f);
    Check(Near(clock.Time().seconds, 0.535, 1e-6), "failed audio query falls back locally");
    Script::effective = false; tick(0.01f);
    Check(Near(clock.Time().seconds, 0.545, 1e-6), "unconfirmed audio falls back locally");
    Script::effective = true;
    clock.Pause();
    const double frozen = clock.Time().seconds;
    tick(1);
    Check(clock.Paused() && clock.Time().seconds == frozen, "real host Tick respects Pause");
    clock.Seek(PerformanceTime{4});
    Check(clock.Paused() && clock.Time().seconds == 4 && Script::pausedSeeks == 1 && Script::lastSeek == 4,
        "paused seek positions backend without playing");
    clock.Resume();
    Check(Script::resumes == 1 && Script::position == 4, "paused resume uses mapped position");
    clock.Seek(PerformanceTime{8});
    Check(Script::seeks == 1 && Script::lastSeek == 8 && player.audioSyncSettleFrames == 3, "playing seek retains settle reset");
    clock.SetPlaybackRate(2);
    tick(0.5f);
    Check(clock.Time().seconds == 9 && !player.audioStarted, "unsupported audio rate stops music and advances locally");
    clock.SetPlaybackRate(1);
    Check(player.audioStarted && Script::position == 9 && clock.Time().seconds == 9, "rate restore reopens at current source position");
    player.BeginScrub(player.currentTime);
    const double scrubTime = clock.Time().seconds;
    tick(1);
    Check(clock.Time().seconds == scrubTime, "adapter adds no second drive during scrub");
    player.EndScrub(player.currentTime);
    player.timing.audio = {0.5f, 1.0f};
    clock.SetPlaybackRate(2);
    clock.Seek(PerformanceTime{3});
    Check(Script::lastSeek == 1 && Near(LegacySourceTime(clock.Time(), player.timing.audio), 1), "source offset remains performance-axis delay");
    for (int i = 0; i < 3; ++i) tick(0.01f);
    Script::position += 0.5f; tick(0.01f);
    Check(Near(clock.Time().seconds, 4.06, 1e-5), "audio source time inverted through speed and offset once");
    player.timing.audio = {};
    clock.SetPlaybackRate(2);
    clock.Seek(PerformanceTime{19.9});
    const int stopsBefore = Script::stops;
    tick(0.2f);
    Check(clock.Time().seconds == 0 && !clock.Paused() && Script::stops > stopsBefore,
        "host loop resets zero, discards overshoot, and stops audio");
    clock.SetPlaybackRate(1);
    clock.Seek(PerformanceTime{19.99});
    const int playsBefore = Script::plays, stopsBeforeAudioLoop = Script::stops;
    tick(0.02f);
    Check(clock.Time().seconds == 0 && Script::plays == playsBefore + 1 && Script::stops == stopsBeforeAudioLoop + 1,
        "native-rate loop retains stop/reopen transport");
    player.loop = false;
    clock.SetPlaybackRate(2); clock.Seek(PerformanceTime{19.9}); tick(0.2f);
    Check(clock.Paused() && clock.Time().seconds == 0 && player.finished && player.state == PlaybackState::Stopped,
        "non-loop finish retains stop/finished behavior");
    clock.Seek(PerformanceTime{100});
    Check(clock.Time().seconds == 20 && player.IsPreviewing(), "adapter preserves legacy seek upper bound and preview");
    clock.Seek(PerformanceTime{-2});
    Check(clock.Time().seconds == 0 && !clock.Seek(PerformanceTime{inf}), "adapter validates seeks before narrowing");
    player.timing.motion = {2, 1};
    Check(LegacySourceTime(PerformanceTime{3}, player.timing.motion) == 4 &&
        LegacySourceTime(PerformanceTime{0}, player.timing.motion) == 0, "legacy mapping is reused unchanged");
    player.timing.motion = {0.5f, -2};
    Check(LegacySourceTime(PerformanceTime{}, player.timing.motion) == 1, "negative offset skips source intro");
    player.timing.motion.speed = 0;
    Check(LegacySourceTime(PerformanceTime{100}, player.timing.motion) == 0, "zero source speed freezes first frame");
    MotionPlayer empty;
    auto emptyClock = MakeMotionPlayerAudioDrivenClock(empty);
    Check(!emptyClock.Seek(PerformanceTime{2}), "unloaded adapter seek reports failure");

    // Combined audio observe + position callback queries once per tick on absolute backend.
    {
        MotionPlayer combined;
        Script::Prepare(combined);
        combined.audioPositionIsAbsolute = true;
        static int combinedCalls = 0;
        static bool combinedPlaying = true;
        static float combinedTime = 0.25f;
        combinedCalls = 0;
        combinedPlaying = true;
        combinedTime = 0.25f;
        combined.fnObserveAudioWithTime = [](audio::Observation* outObs, float* outTimeSeconds) -> bool {
            ++combinedCalls;
            if (outObs)
            {
                outObs->sourceOpen = true;
                outObs->confirmed = true;
                outObs->sourceEpoch = 1;
                outObs->state = combinedPlaying ? audio::PlaybackState::Playing : audio::PlaybackState::Paused;
            }
            if (outTimeSeconds) *outTimeSeconds = combinedPlaying ? combinedTime : -1.0f;
            return true;
        };
        combined.Play();
        Script::reads = 0;
        combined.Tick(0.016f, actor);
        Check(combinedCalls == 1 && Script::reads == 0 && Near(combined.currentTime, 0.25, 1e-6) &&
            std::string_view(combined.lastClockSource) == "audio",
            "combined audio observe+time uses a single query per tick");
        combinedTime = 0.50f;
        combined.Tick(0.016f, actor);
        Check(combinedCalls == 2 && Script::reads == 0 && Near(combined.currentTime, 0.50, 1e-6),
            "combined callback tracks monotonic absolute audio progress");
        combinedPlaying = false;
        combined.Tick(0.020f, actor);
        Check(combinedCalls == 3 && Script::reads == 0 && Near(combined.currentTime, 0.52, 1e-6) &&
            std::string_view(combined.lastClockSource) == "local(no-audio)",
            "combined callback falls back to local clock when audio stops playing");
    }

    // Smooth PLL audio clock under 20ms quantized mixer steps at 60fps and 120fps.
    {
        static double trueAudioSeconds = 0.0;
        static double quantStepSeconds = 0.020;
        auto wireQuantizedAudio = [](MotionPlayer& p) {
            Script::Prepare(p);
            p.audioPositionIsAbsolute = true;
            p.fnSupportsAudioPlaybackRate = [] { return true; };
            p.fnSetAudioPlaybackRate = [](float) {};
            p.fnObserveAudioWithTime = [](audio::Observation* outObs, float* outTimeSeconds) -> bool {
                if (outObs)
                {
                    outObs->sourceOpen = true;
                    outObs->confirmed = true;
                    outObs->sourceEpoch = 1;
                    outObs->state = audio::PlaybackState::Playing;
                }
                if (outTimeSeconds)
                {
                    const double q = std::floor(trueAudioSeconds / quantStepSeconds) * quantStepSeconds;
                    *outTimeSeconds = static_cast<float>(q);
                }
                return true;
            };
        };

        // A. 60fps (16.67ms) with 20ms quantized audio cursor updates.
        {
            MotionPlayer smooth60;
            wireQuantizedAudio(smooth60);
            trueAudioSeconds = 0.0;
            quantStepSeconds = 0.020;
            smooth60.Play();
            constexpr float dt60 = 1.0f / 60.0f;
            bool allPositive = true;
            bool allBounded = true;
            bool steadyLowVariance = true;
            double minDelta = 1e9, maxDelta = 0.0;
            for (int f = 0; f < 180; ++f)
            {
                trueAudioSeconds += static_cast<double>(dt60);
                smooth60.Tick(dt60, actor);
                const double d = smooth60.lastStepDeltaSeconds;
                if (d <= 0.0) allPositive = false;
                if (d < dt60 * 0.90 || d > dt60 * 1.10) allBounded = false;
                if (f >= 90)
                {
                    if (d < dt60 * 0.96 || d > dt60 * 1.04) steadyLowVariance = false;
                    minDelta = (std::min)(minDelta, d);
                    maxDelta = (std::max)(maxDelta, d);
                }
            }
            const double expectedMeanAudio = trueAudioSeconds - 0.5 * quantStepSeconds;
            Check(allPositive && allBounded && steadyLowVariance && (maxDelta - minDelta) < 0.001,
                "60fps with 20ms quantized audio advances smoothly every frame with low variance");
            Check(std::fabs(smooth60.preciseTimeSeconds - expectedMeanAudio) < 0.012,
                "60fps smooth audio clock stays phase-locked to quantized audio time");
        }

        // B. 120fps (8.33ms) with 20ms quantized audio cursor updates (>50% unchanged cursor frames).
        {
            MotionPlayer smooth120;
            wireQuantizedAudio(smooth120);
            trueAudioSeconds = 0.0;
            quantStepSeconds = 0.020;
            smooth120.Play();
            constexpr float dt120 = 1.0f / 120.0f;
            bool allPositive = true;
            bool allBounded = true;
            for (int f = 0; f < 240; ++f)
            {
                trueAudioSeconds += static_cast<double>(dt120);
                smooth120.Tick(dt120, actor);
                const double d = smooth120.lastStepDeltaSeconds;
                if (d <= 0.0) allPositive = false;
                if (d < dt120 * 0.90 || d > dt120 * 1.10) allBounded = false;
            }
            const double expectedMeanAudio = trueAudioSeconds - 0.5 * quantStepSeconds;
            Check(allPositive && allBounded,
                "120fps with 20ms quantized audio has zero frozen frames and bounded steps");
            Check(std::fabs(smooth120.preciseTimeSeconds - expectedMeanAudio) < 0.012,
                "120fps smooth audio clock remains phase-locked to quantized audio");
        }

        // C. 1.5x playback speed + 0.5% audio clock drift + Seek/Pause/Resume re-anchor.
        {
            MotionPlayer drifted;
            wireQuantizedAudio(drifted);
            drifted.SetPlaybackSpeed(1.5f);
            trueAudioSeconds = 0.0;
            quantStepSeconds = 0.020;
            drifted.Play();
            constexpr float dt60 = 1.0f / 60.0f;
            bool allSmooth = true;
            for (int f = 0; f < 240; ++f)
            {
                trueAudioSeconds += static_cast<double>(dt60) * 1.5 * 1.005;
                drifted.Tick(dt60, actor);
                const double d = drifted.lastStepDeltaSeconds;
                if (d < dt60 * 1.5 * 0.90 || d > dt60 * 1.5 * 1.10) allSmooth = false;
            }
            const double expectedMeanAudio = trueAudioSeconds - 0.5 * quantStepSeconds;
            Check(allSmooth && std::fabs(drifted.preciseTimeSeconds - expectedMeanAudio) < 0.015,
                "1.5x rate with 20ms quantization and 0.5% drift tracks smoothly without lag accumulation");

            drifted.Seek(8.0f);
            trueAudioSeconds = 8.0;
            drifted.Pause();
            drifted.Tick(dt60, actor);
            Check(Near(drifted.currentTime, 8.0, 1e-6), "paused seek holds exact position");
            drifted.Resume();
            trueAudioSeconds += static_cast<double>(dt60) * 1.5;
            drifted.Tick(dt60, actor);
            Check(drifted.lastStepDeltaSeconds > dt60 * 1.5 * 0.90 &&
                drifted.lastStepDeltaSeconds < dt60 * 1.5 * 1.10,
                "resume after seek continues smoothly without PLL spike");
        }
    }
}
#endif

int main()
{
    TestManual(); TestFixedStep(); TestLoops(); TestAudioView();
#ifdef GKMOTION_CLOCK_ADAPTER_TEST
    TestMotionPlayerAdapter();
#endif
    std::printf("Performance Clock harness: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
