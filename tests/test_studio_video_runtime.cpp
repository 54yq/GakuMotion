// Meaningful offline lifecycle/ownership checks. No Unity/game calls or rendered
// output are involved; the production adapter is built by the main DLL build.
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include <limits>
#include "../src/GkStageStudioSettings.hpp"

namespace StudioRuntime
{
    struct Context
    {
        int owner{}, activeScene{};
        std::string activeName;
        bool allowed{};
        bool SameOwner(const Context& other) const
        { return owner != 0 && owner == other.owner && activeScene == other.activeScene; }
    };
}
#define GK_STUDIO_VIDEO_FAKE_RUNTIME
#include "../src/GkStudioVideoRuntime.hpp"

namespace
{
    int checks{}, failures{};
    void Check(bool value, const char* message)
    { ++checks; if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; } }

    struct Fake
    {
        bool objects{}, createOk{true}, prepareOk{true}, pollOk{true}, updateOk{true}, pauseOk{true}, destroyOk{true};
        bool requiresPlayForFrame{};
        StudioVideo::Frame frame;
        double clock{};
        int creates{}, prepares{}, polls{}, plays{}, pauses{}, updates{}, destroys{};
        float lastWidth{};
        std::vector<std::string> calls;
        StudioVideo::Backend Api()
        {
            return {this,
                [](void* p, const auto&, const auto&, std::string& error) {
                    auto& f=*static_cast<Fake*>(p); ++f.creates; f.calls.push_back("create"); f.objects=true;
                    if (!f.createOk) error="create failed"; return f.createOk; },
                [](void* p, std::string& error) {
                    auto& f=*static_cast<Fake*>(p); ++f.prepares; f.calls.push_back("prepare");
                    if (!f.prepareOk) error="prepare failed"; return f.prepareOk; },
                [](void* p, auto& frame, std::string& error) {
                    auto& f=*static_cast<Fake*>(p); ++f.polls; frame=f.frame;
                    if (f.requiresPlayForFrame && frame.prepared && f.plays>0)
                    { frame.texture=true; frame.frame=0; frame.width=1280; frame.height=720; }
                    if (!f.pollOk) error="poll failed"; return f.pollOk; },
                [](void* p, bool paused, std::string& error) {
                    auto& f=*static_cast<Fake*>(p); if (paused) ++f.pauses; else ++f.plays;
                    if (!f.pauseOk) error="playback failed"; return f.pauseOk; },
                [](void* p, const auto& s, const auto&, std::string& error) {
                    auto& f=*static_cast<Fake*>(p); ++f.updates; f.lastWidth=s.width;
                    if (!f.updateOk) error="update failed"; return f.updateOk; },
                [](void* p, std::string& error) {
                    auto& f=*static_cast<Fake*>(p); ++f.destroys; f.calls.push_back("destroy");
                    if (f.destroyOk) f.objects=false; else error="retirement pending"; return f.destroyOk; },
                [](void* p) { return static_cast<Fake*>(p)->objects; },
                [](void* p) { return static_cast<Fake*>(p)->clock; }};
        }
    };
    GakuMotion::studio::Video Settings()
    { GakuMotion::studio::Video s; s.enabled=true; s.path="C:\\fixture-only.mp4"; return s; }
    StudioRuntime::Context Photo(int owner=10, int scene=20) { return {owner,scene,"Photography",true}; }

    void GateAndPreparation()
    {
        Fake f; auto api=f.Api(); StudioVideo::Runtime runtime; auto s=Settings(); std::string notice;
        auto title=Photo(); title.allowed=false; runtime.Tick(api,s,title,notice);
        Check(f.creates==0 && f.polls==0,"no backend initialization or managed sampling outside Photography");
        runtime.Tick(api,s,Photo(),notice);
        Check(f.creates==1 && f.prepares==1 && f.plays==0,"prepare once without treating Prepare return as playback");
        for(int i=0;i<5;++i) {f.clock+=1; runtime.Tick(api,s,Photo(),notice);}
        Check(f.creates==1 && f.prepares==1,"pending preparation does not create/Prepare every frame");
        Check(!runtime.DecodedFrameObserved(),"unprepared player is not a decoded frame");
        f.frame.prepared=true; runtime.Tick(api,s,Photo(),notice);
        Check(f.plays==1 && runtime.GetPhase()==StudioVideo::Phase::WaitingFrame,"start playback after preparation and wait for real frame");
        f.frame.texture=true; f.frame.frame=-1; runtime.Tick(api,s,Photo(),notice);
        Check(!runtime.DecodedFrameObserved(),"texture allocation with frame=-1 does not certify a decoded frame");
        f.frame.frame=0; f.frame.width=1280; f.frame.height=720; runtime.Tick(api,s,Photo(),notice);
        Check(runtime.DecodedFrameObserved() && runtime.GetPhase()==StudioVideo::Phase::Playing,"prepared nonnull texture and frame0 establish decoded-frame observation");
        Check(notice.find("视觉确认")!=std::string::npos,"frame status preserves pending visual acceptance");
        s.paused=true; runtime.Tick(api,s,Photo(),notice); runtime.Tick(api,s,Photo(),notice);
        Check(f.pauses==1 && f.plays==1 && runtime.GetPhase()==StudioVideo::Phase::Paused,"pause changes once and remains paused");
        s.paused=false; s.width=9; runtime.Tick(api,s,Photo(),notice);
        Check(f.plays==2 && f.creates==1 && f.lastWidth==9,"resume/layout edits reuse the existing owned session");
        s.enabled=false; runtime.Tick(api,s,Photo(),notice);
        Check(!f.objects && f.destroys==1,"disable retires owned resources");
    }
    void TimeoutsAndRetry()
    {
        Fake f; auto api=f.Api(); StudioVideo::Runtime runtime; auto s=Settings(); std::string notice;
        runtime.Tick(api,s,Photo(),notice); f.clock=20; runtime.Tick(api,s,Photo(),notice);
        Check(runtime.GetPhase()==StudioVideo::Phase::Faulted && f.destroys==1,"preparation deadline releases owned resources");
        for(int i=0;i<4;++i) runtime.Tick(api,s,Photo(),notice);
        Check(f.creates==1,"same faulting configuration does not retry continuously");
        runtime.Tick(api,s,Photo(11,21),notice);
        Check(f.creates==2 && runtime.GetPhase()==StudioVideo::Phase::Preparing,"new Photography owner allows a fresh bounded attempt");
        f.frame.prepared=true; runtime.Tick(api,s,Photo(11,21),notice); f.clock=30; runtime.Tick(api,s,Photo(11,21),notice);
        Check(runtime.GetPhase()==StudioVideo::Phase::Faulted,"prepared-but-no-frame has its own deadline");
        s.path="C:\\fixture-second.mp4"; runtime.Tick(api,s,Photo(11,21),notice);
        Check(f.creates==3,"changed source allows another attempt");
    }
    void InitiallyPausedStillDecodesFirstFrame()
    {
        Fake f; f.requiresPlayForFrame=true; f.frame.prepared=true;
        auto api=f.Api(); StudioVideo::Runtime runtime; auto s=Settings(); s.paused=true; std::string notice;
        runtime.Tick(api,s,Photo(),notice);
        Check(f.plays==1 && f.pauses==0 && runtime.GetPhase()==StudioVideo::Phase::WaitingFrame,
              "initially paused session first plays to make decoding possible");
        f.clock=1; runtime.Tick(api,s,Photo(),notice);
        Check(f.plays==1 && f.pauses==1 && runtime.DecodedFrameObserved() && runtime.GetPhase()==StudioVideo::Phase::Paused,
              "first decoded frame immediately applies requested pause exactly once");
        f.clock=30; runtime.Tick(api,s,Photo(),notice);
        Check(f.creates==1 && f.pauses==1 && runtime.GetPhase()==StudioVideo::Phase::Paused,
              "initial pause with decoded frame does not hit the waiting-frame deadline");
        s.paused=false; runtime.Tick(api,s,Photo(),notice);
        Check(f.plays==2 && f.pauses==1 && runtime.GetPhase()==StudioVideo::Phase::Playing,
              "resume after initial paused frame preserves the owned player");
    }
    void CleanupMustFinishBeforeReplacement()
    {
        Fake f; auto api=f.Api(); StudioVideo::Runtime runtime; auto s=Settings(); std::string notice;
        runtime.Tick(api,s,Photo(),notice); f.destroyOk=false; s.path="C:\\fixture-second.mp4";
        runtime.Tick(api,s,Photo(),notice);
        Check(f.objects && f.creates==1 && runtime.GetPhase()==StudioVideo::Phase::Retiring,"failed cleanup retains ownership and blocks source replacement");
        f.destroyOk=true; runtime.Tick(api,s,Photo(),notice);
        Check(f.creates==2 && f.calls[f.calls.size()-3]=="destroy" && f.calls[f.calls.size()-2]=="create", "retirement finishes before next create/prepare");
        f.destroyOk=false; auto outside=Photo(); outside.allowed=false; runtime.Tick(api,s,outside,notice);
        Check(f.objects && runtime.GetPhase()==StudioVideo::Phase::Retiring,"owner loss retains failed cleanup for retry outside Photography");
        f.destroyOk=true; runtime.Tick(api,s,outside,notice);
        Check(!f.objects && f.creates==2,"cleanup outside Photography never creates replacement objects");
    }
    void FailureAtEachStage()
    {
        for(int stage=0;stage<5;++stage)
        {
            Fake f; auto api=f.Api(); StudioVideo::Runtime runtime; auto s=Settings(); std::string notice;
            if(stage==0) f.createOk=false; if(stage==1) f.prepareOk=false; if(stage==2) f.pollOk=false;
            if(stage==3) {f.frame.prepared=true; f.pauseOk=false;} if(stage==4) f.updateOk=false;
            runtime.Tick(api,s,Photo(),notice);
            Check(runtime.GetPhase()==StudioVideo::Phase::Faulted && !f.objects && f.destroys==1,"failure retires partial resources at every creation/prepare/poll/play/layout stage");
            runtime.Tick(api,s,Photo(),notice);
            Check(f.creates==1,"failure at any stage is latched for the same owner/config");
        }
        Fake f; auto api=f.Api(); StudioVideo::Runtime runtime; auto s=Settings(); std::string notice;
        s.width=std::numeric_limits<float>::quiet_NaN(); runtime.Tick(api,s,Photo(),notice);
        Check(f.creates==0,"invalid numeric layout cannot create Unity objects");
        s=Settings(); s.path="https://fixture.invalid/video.mp4"; runtime.Tick(api,s,Photo(),notice);
        Check(f.creates==0,"network URLs are not accepted by the local-file MVP");
    }
    void OwnerAndClock()
    {
        Fake f; auto api=f.Api(); StudioVideo::Runtime runtime; auto s=Settings(); std::string notice;
        runtime.Tick(api,s,Photo(),notice); runtime.Tick(api,s,Photo(10,21),notice);
        Check(f.creates==2 && f.destroys==1,"active scene transition invalidates the old ownership scope");
        f.clock=-1; runtime.Tick(api,s,Photo(10,21),notice);
        Check(runtime.GetPhase()==StudioVideo::Phase::Faulted && !f.objects,"nonmonotonic timing cannot leave an indefinite preparation");
        s.enabled=false; runtime.Tick(api,s,Photo(10,21),notice); s.enabled=true; f.clock=1; runtime.Tick(api,s,Photo(10,21),notice);
        Check(f.creates==3,"explicit off/on clears a failure latch");
    }
    void ObservedLayerSelection()
    {
        using StudioVideo::SelectVisibleLayer;
        Check(SelectVisibleLayer({},0xffffffffu).layer<0,"no geometry cannot invent a default visible layer");
        Check(SelectVisibleLayer({7,7,7},uint32_t{1}<<7).layer==7,"repeated observations of one visible geometry layer select it");
        Check(SelectVisibleLayer({7,7},uint32_t{1}<<6).layer<0,"camera mask mismatch is rejected rather than expanding the camera mask");
        Check(SelectVisibleLayer({2,7,2},uint32_t{1}<<7).layer==7,"multiple observed layers select the sole camera-visible one");
        Check(SelectVisibleLayer({2,7},(uint32_t{1}<<2)|(uint32_t{1}<<7)).layer<0,"ambiguous visible geometry layers fail closed");
        Check(SelectVisibleLayer({7},0).layer<0,"zero camera culling mask rejects all geometry layers");
        Check(SelectVisibleLayer({31},uint32_t{1}<<31).layer==31,"highest valid layer uses unsigned mask arithmetic");
        Check(SelectVisibleLayer({-1},0xffffffffu).layer<0 && SelectVisibleLayer({32},0xffffffffu).layer<0,"invalid layer indices are rejected");
        Check(StudioVideo::Diagnostics().empty(),"fake diagnostics does not invoke any Unity getter");
    }
}
int main()
{
    GateAndPreparation(); TimeoutsAndRetry(); InitiallyPausedStillDecodesFirstFrame();
    CleanupMustFinishBeforeReplacement(); FailureAtEachStage(); OwnerAndClock(); ObservedLayerSelection();
    std::cout << checks-failures << '/' << checks << " studio video lifecycle checks passed; no runtime/visual certification\n";
    return failures ? 1 : 0;
}
