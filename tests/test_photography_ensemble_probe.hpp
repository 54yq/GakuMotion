#pragma once
#include "GkPhotographyEnsembleProbe.hpp"

static void TestPhotographyEnsembleProbe()
{
    using namespace GakuMotion::photography_ensemble_probe;
    BeginTest("bounded read-only Photography ensemble evidence");
    Session session; Frame f; f.number=1;f.owner=10;f.scene=20;f.layout=30;f.capacity=3;f.available=true;
    for(unsigned i=0;i<3;++i)
    {
        auto& s=f.slots[i];s.actor=100+i;s.actorRoot=200+i;s.hips=300+i;s.instanceId=400+int(i);
        s.identityAvailable=s.built=s.rosterMatched=true;s.callbacks=1;
        s.actorPose.available=s.beforePose.available=s.appliedPose.available=true;
        s.actorPose.position={float(i),0,0};s.beforePose.position={float(i),1,0};s.appliedPose=s.beforePose;
    }
    auto e=session.Observe(f);
    Check(e.snapshotAvailable&&e.populatedSlots==3&&!e.crossFrameAvailable&&!e.visualVerified,
        "one three-slot observation supplies identities but cannot verify cross-frame or visuals");
    ++f.number;e=session.Observe(f);
    Check(e.crossFrameAvailable&&e.matchingSlots==3&&e.stableActorRoots==3&&e.appliedComparisonAvailable&&
        !e.changedHipsAfterApply&&e.readyForInjection&&!e.visualVerified,
        "successive matching slots and callbacks permit injection evidence without certifying rendering");
    ++f.number;f.slots[1].beforePose.position.x+=.1f;e=session.Observe(f);
    Check(e.changedHipsAfterApply==1&&e.stableActorRoots==3&&e.readyForInjection,
        "ordinary early-frame Hips animation is evidence, separate from the stable actor-layout injection gate");
    ++f.number;f.slots[1].instanceId+=1000;e=session.Observe(f);
    Check(e.matchingSlots==2,"reused native address with a new Unity instance cannot inherit old-slot evidence");
    ++f.number;f.layout+=1;e=session.Observe(f);
    Check(!e.crossFrameAvailable&&!e.appliedComparisonAvailable,"changing layout invalidates cross-frame comparisons");
    ++f.number;f.slots[0].hipsParent=999;e=session.Observe(f);
    Check(!e.readyForInjection&&e.matchingSlots==2,"reparenting Hips invalidates readiness before another pose write");
    ++f.number;f.slots[0].rosterMatched=false;e=session.Observe(f);
    Check(!e.readyForInjection,"native slots absent from the fresh actor registry cannot authorize injection");
    ++f.number;f.slots[0].rosterMatched=true;f.slots[2]={};e=session.Observe(f);
    ++f.number;e=session.Observe(f);
    Check(e.readyForInjection&&e.populatedSlots==2,"two built slots can be ready while the third capacity slot is empty");
    ++f.number;f.slots[1]={};e=session.Observe(f);
    ++f.number;e=session.Observe(f);
    Check(e.readyForInjection&&e.populatedSlots==1,
        "a single built actor can establish the same gate for gradual one-to-two-to-three testing");
    ReadinessLatch latch;
    Check(latch.Observe(f,e),"successful initial cross-frame evidence latches readiness");
    ++f.number;f.slots[0].callbacks=0;Evidence duringCapture;
    Check(latch.Observe(f,duringCapture),"capture suspension may suppress actor callbacks without discarding a stable certified identity");
    bool stayedReady=true;
    for(int i=0;i<10;++i){++f.number;f.slots[0].actorPose.position.x+=.00003f;stayedReady&=latch.Observe(f,duringCapture,false);}
    Check(!stayedReady&&!latch.Validate(f),"the certified root pivot does not drift with sub-threshold changes from successive frames");
    ++f.number;f.capacity=4;e=session.Observe(f);
    Check(!e.snapshotAvailable,"capacity beyond the explicit supported three-slot bound is unavailable");
    f.capacity=3;
    for(unsigned i=0;i<FrameBudget+20;++i){++f.number;e=session.Observe(f);}
    Check(session.Exhausted()&&e.frames==FrameBudget,"a probe session has a hard physical-frame budget");
    session.Reset();++f.number;e=session.Observe(f);
    Check(e.frames==1&&!e.crossFrameAvailable,"explicit restart clears all previous evidence");
}
