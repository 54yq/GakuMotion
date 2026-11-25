// Synthetic regression coverage for migrated and newly authored prop timelines.
#include "GkStagePropTimeline.hpp"
#include <cassert>
#include <cstdio>

using namespace GakuMotion::stage_prop_timeline;

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path path = std::filesystem::path(argv[1]) / "prop-timeline-fixture.txt";
    assert(meniDefaults.size() == 14);
    for (const auto angles : {GakuMotion::Vector3{25, 40, -50}, GakuMotion::Vector3{25, 90, -50},
                             GakuMotion::Vector3{25, -90, -50}})
    {
        using GakuMotion::Quaternion;
        const auto q = Quaternion::FromEulerDegreesXYZ(angles.x, angles.y, angles.z);
        const auto e = StageEuler(q);
        const auto restored = Quaternion::FromEulerDegreesXYZ(e.x, e.y, e.z);
        assert(std::abs(q.x * restored.x + q.y * restored.y + q.z * restored.z + q.w * restored.w) > 0.99999f);
    }
    for (size_t i = 0; i < meniDefaults.size(); ++i)
    {
        assert(Valid(meniDefaults[i]));
        for (size_t j = i + 1; j < meniDefaults.size(); ++j)
            assert(!Overlap(meniDefaults[i], meniDefaults[j]));
    }
    const auto hold = std::find_if(meniDefaults.begin(), meniDefaults.end(), [](const Cue& cue) {
        return cue.prop == 11 && cue.first == 1484 && cue.last == 1665;
    });
    assert(hold != meniDefaults.end() && hold->anchor == Anchor::Camera && hold->movement == Movement::Hold);
    const auto exit = std::find_if(meniDefaults.begin(), meniDefaults.end(), [](const Cue& cue) {
        return cue.prop == 11 && cue.first == 1665 && cue.last == 1741;
    });
    assert(exit != meniDefaults.end() && exit->anchor == Anchor::CameraToStage && exit->movement == Movement::Hold);
    assert(exit->hideAfter && exit->position.x == hold->position.x && exit->rotation.x == hold->rotation.x);
    assert(Save(path, meniDefaults));
    const auto savedDefault = Load(path);
    assert(savedDefault.size() == meniDefaults.size() &&
        savedDefault[exit - meniDefaults.begin()].anchor == Anchor::CameraToStage);
    const auto& dogLaunch = meniDefaults.back();
    assert(dogLaunch.prop == 1 && dogLaunch.movement == Movement::GripThrow && dogLaunch.hideAfter);
    assert(meniDefaults.front().last == dogLaunch.first && dogLaunch.first == 1305 && dogLaunch.last == 1353);
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
    assert(LoadSourceTrial(path).empty());
    const auto trialPath = path.parent_path() / "source-prop-trial-fixture.txt";
    auto first = Cue{1, 1, false, 1293, 1353};
    first.movement = Movement::SourceAttachment;
    first.hideAfter = true;
    auto second = first;
    second.prop = 11;
    second.first = 1484;
    second.last = 1600;
    assert(Save(trialPath, {first, second}));
    const auto trial = LoadSourceTrial(trialPath);
    assert(trial.size() == 2 && trial[1].place == 1 && trial[1].movement == Movement::SourceAttachment);
    for (const auto& cue : trial)
    {
        assert(!ContainsFrame(cue, FrameAtTime(static_cast<float>(cue.last) / 30.f)));
        assert(ContainsFrame(cue, FrameAtTime(static_cast<float>(cue.first) / 30.f)));
        assert(!ContainsFrame(cue, FrameAtTime(static_cast<float>(cue.first - 1) / 30.f)));
    }
    {
        std::ofstream broken(trialPath, std::ios::app);
        broken << "unexpected data\n";
    }
    assert(LoadSourceTrial(trialPath).empty());
    assert(LoadSourceTrial(path.parent_path() / "missing-source-prop-trial.txt").empty());
    std::puts("Prop timeline: legacy data, camera cues, source-trial boundaries, handoff, throw and save passed.");
}
