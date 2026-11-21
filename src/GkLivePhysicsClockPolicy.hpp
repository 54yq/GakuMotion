#pragma once
#include <algorithm>
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

    // Keep transport/audio authoritative. A hitch can leave the visual sample
    // briefly behind, with a bounded deficit when the frame rate stays low.
    struct PoseClock
    {
        static constexpr float Step = 1.f / 30.f;
        static constexpr float MaxLag = 2.f * Step;
        static constexpr float InitialInterval = .024f;
        const void* actor{};
        int seek{};
        float transport{}, pose{}, interval{InitialInterval};

        float Sample(float time,const void* target,int seekCount,bool playing)
        {
            if (!playing || target!=actor || seekCount!=seek || time+0.01f<transport) {
                actor=playing ? target : nullptr;
                seek=seekCount;
                transport=pose=time;
                interval=InitialInterval;
            } else if (time>transport) {
                const float elapsed = time-transport;
                // Leave modest headroom above recent cadence so low FPS does
                // not fill the entire lag allowance before the next stall.
                const float budget = std::clamp(interval+.006f,Step,.055f);
                pose=(std::min)(time,(std::max)(pose+budget,time-MaxLag));
                transport=time;
                interval += .1f * ((std::min)(elapsed,.072f)-interval);
            }
            return pose;
        }
    };

    inline bool IsTransportPaused(bool takeover, bool paused)
    {
        return takeover && paused;
    }

    inline bool ShouldRun(bool takeover, bool fault, int backend, bool playing, bool paused,
                          bool hasInput, bool ownsClock = false, bool preview = false)
    {
        return takeover && !fault && backend == 2 &&
            !IsTransportPaused(takeover, paused) && (playing || (preview && ownsClock)) && (hasInput || ownsClock);
    }

    inline bool CanPublish(bool playing, bool paused, bool hasInput, bool ownsClock, bool preview = false)
    {
        if (preview) return ownsClock;
        return playing || hasInput || (paused && ownsClock);
    }
}
