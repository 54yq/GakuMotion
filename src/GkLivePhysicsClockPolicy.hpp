#pragma once
namespace GakuMotion::live_physics_clock
{
    inline bool ShouldRun(bool takeover, bool fault, int backend, bool playing, bool paused,
                          bool hasInput, bool ownsClock = false)
    {
        return takeover && !fault && backend == 2 && (playing || paused) && (hasInput || ownsClock);
    }

    inline bool CanPublish(bool playing, bool paused, bool hasInput, bool ownsClock)
    {
        return playing || hasInput || (paused && ownsClock);
    }
}
