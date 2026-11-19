#pragma once
namespace GakuMotion::live_physics_clock
{
    // An owned graph borrowing the rig has to stop before its graph can be
    // destroyed and before the game's original graph may resume.
    template<typename Playing, typename Stop, typename Destroy>
    bool StopBeforeDestroy(Playing playing, Stop stop, Destroy destroy)
    {
        if (playing() != 0 && (!stop() || playing() != 0)) return false;
        return destroy();
    }

    inline bool ShouldRun(bool takeover, bool fault, int backend, bool playing, bool paused,
                          bool hasInput, bool ownsClock = false, bool preview = false)
    {
        return takeover && !fault && backend == 2 &&
            (playing || paused || (preview && ownsClock)) && (hasInput || ownsClock);
    }

    inline bool CanPublish(bool playing, bool paused, bool hasInput, bool ownsClock, bool preview = false)
    {
        if (preview) return ownsClock;
        return playing || hasInput || (paused && ownsClock);
    }
}
