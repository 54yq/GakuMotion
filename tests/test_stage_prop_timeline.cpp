// Synthetic regression coverage for migrated and newly authored prop timelines.
#include "GkStagePropTimeline.hpp"
#include <cassert>
#include <cstdio>

using namespace GakuMotion::stage_prop_timeline;

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path path = std::filesystem::path(argv[1]) / "prop-timeline-fixture.txt";
    assert(meniDefaults.size() == 13);
    for (size_t i = 0; i < meniDefaults.size(); ++i)
    {
        assert(Valid(meniDefaults[i]));
        for (size_t j = i + 1; j < meniDefaults.size(); ++j)
            assert(!Overlap(meniDefaults[i], meniDefaults[j]));
    }
    const auto hold = std::find_if(meniDefaults.begin(), meniDefaults.end(), [](const Cue& cue) {
        return cue.prop == 11 && cue.first == 1484 && cue.last == 1690;
    });
    assert(hold != meniDefaults.end() && hold->anchor == Anchor::Camera && hold->movement == Movement::Hold);
    const auto exit = std::find_if(meniDefaults.begin(), meniDefaults.end(), [](const Cue& cue) {
        return cue.prop == 11 && cue.first == 1690 && cue.last == 1710;
    });
    assert(exit != meniDefaults.end() && exit->anchor == Anchor::Camera && exit->movement == Movement::Smooth);
    assert(exit->hideAfter && Progress(*exit, 1690) == 0 && Progress(*exit, 1700) == 0.5f);
    for (int frame = 0; frame <= lastFrame; ++frame)
        assert(FrameAtTime(static_cast<float>(frame) / 30.f) == frame);

    {
        std::ofstream legacy(path);
        legacy << "MeniPropTimeline1 2\n"
               << "11 1 0 1400 1690 0.2 0 0 0 2 0\n"
               << "11 4 1 1690 1710 0 0 0 0 0 0\n";
    }
    auto custom = Load(path);
    assert(custom.size() == 2 && custom[0].position.x == 0.2f && custom[0].rotation.y == 2.f);
    assert(custom[0].anchor == Anchor::Grip && custom[1].movement == Movement::Handoff);
    assert(Progress(custom[1], 1690) == 0 && Progress(custom[1], 1695) == 0.5f && Progress(custom[1], 1700) == 1);
    custom[1].movement = Movement::Throw;
    custom[1].anchor = Anchor::Stage;
    custom[1].endPosition = {1, 2, 3};
    custom[1].arcHeight = 1.5f;
    custom[1].hideAfter = true;
    assert(Valid(custom[1]));
    assert(ArcOffset(custom[1], 0) == 0 && ArcOffset(custom[1], 0.5f) == 1.5f && ArcOffset(custom[1], 1) == 0);
    assert(Save(path, custom) && Save(path, custom));
    const auto restored = Load(path);
    assert(restored.size() == 2 && restored[1].movement == Movement::Throw &&
        restored[1].anchor == Anchor::Stage && restored[1].endPosition.z == 3 &&
        restored[1].hideAfter && restored[1].arcHeight == 1.5f);
    std::puts("Prop timeline: legacy data, camera cues, exact frame boundaries, handoff, throw and save passed.");
}
