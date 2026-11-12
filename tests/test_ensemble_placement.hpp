#pragma once
#include "GkIdolController.hpp"

namespace ensemblePlacementFixture
{
    using namespace GakuMotion;
    struct Node { Node* parent{}; Vector3 position; Quaternion rotation; };
    inline std::array<Node, 6> nodes;
    inline std::vector<Node*> rotationWrites;
    inline Quaternion WorldRotation(Node* n)
    { return n->parent ? (WorldRotation(n->parent) * n->rotation).Normalized() : n->rotation; }
    inline Vector3 WorldPosition(Node* n)
    { return n->parent ? WorldPosition(n->parent) + WorldRotation(n->parent).Rotate(n->position) : n->position; }
    inline void WorldP(void* v, float* p)
    { const auto x = WorldPosition(static_cast<Node*>(v)); p[0]=x.x; p[1]=x.y; p[2]=x.z; }
    inline void WorldQ(void* v, float* p)
    { const auto q = WorldRotation(static_cast<Node*>(v)); p[0]=q.x; p[1]=q.y; p[2]=q.z; p[3]=q.w; }
    inline void LocalQ(void* v, float* p)
    { const auto q = static_cast<Node*>(v)->rotation; p[0]=q.x; p[1]=q.y; p[2]=q.z; p[3]=q.w; }
    inline void SetWorldP(void* v,float x,float y,float z)
    {
        auto* n=static_cast<Node*>(v); const Vector3 p{x,y,z};
        n->position=n->parent?WorldRotation(n->parent).Inverse().Rotate(p-WorldPosition(n->parent)):p;
    }
    inline void SetLocalQ(void* v,float x,float y,float z,float w)
    { auto* n=static_cast<Node*>(v); n->rotation={x,y,z,w}; rotationWrites.push_back(n); }
    inline void* Parent(void* v) { return static_cast<Node*>(v)->parent; }
    inline bool NearP(Vector3 a,Vector3 b)
    { const auto d=a-b; return d.x*d.x+d.y*d.y+d.z*d.z<1e-8f; }
    inline bool NearQ(Quaternion a,Quaternion b)
    { a=a.Normalized(); b=b.Normalized(); return std::fabs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w)>.999999f; }
    inline MotionClip Clip(bool hips=true)
    {
        MotionClip clip; clip.format=MotionClip::Format::kGkClip; clip.version=4;
        clip.total_frames=2; clip.fps=30; clip.duration_seconds=1.f/30;
        if(hips)clip.bones.push_back({0,"Hips",false});
        clip.bones.push_back({7,"Spine",false}); clip.bones.push_back({8,"Chest",false});
        clip.bone_count=uint32_t(clip.bones.size());
        clip.bone_parents=BuildBoneParentIndex(clip.bones); clip.bone_parent_hids=BuildBoneParentHids(clip.bones);
        clip.root_positions={{.3f,.2f,.7f},{-.2f,.6f,1.3f}};
        for(int f=0;f<2;++f)
        {
            if(hips)clip.bone_rotations.push_back(Quaternion::FromEulerDegreesXYZ(5,23+f*13.f,-7));
            clip.bone_rotations.push_back(Quaternion::FromEulerDegreesXYZ(17,-12,9));
            clip.bone_rotations.push_back(Quaternion::FromEulerDegreesXYZ(-8,15,11));
        }
        return clip;
    }
    inline void Wire(IdolController& c,const MotionClip& clip)
    {
        nodes={}; rotationWrites.clear();
        nodes[0].position={4,.3f,-3}; nodes[0].rotation=Quaternion::FromEulerDegreesXYZ(0,57,0);
        nodes[1].parent=&nodes[0]; nodes[1].position={.2f,.1f,-.3f};
        nodes[1].rotation=Quaternion::FromEulerDegreesXYZ(12,-29,8);
        nodes[2].parent=&nodes[1]; nodes[2].position={.05f,.9f,.07f};
        nodes[2].rotation=Quaternion::FromEulerDegreesXYZ(3,19,4);
        nodes[3].parent=&nodes[2]; nodes[3].position={0,.3f,0};
        nodes[4].parent=&nodes[3]; nodes[4].position={0,.25f,0};
        nodes[5].parent=&nodes[4]; nodes[5].position={.3f,.12f,.08f};
        c.fnGetWorldPosition=WorldP; c.fnGetWorldRotation=WorldQ; c.fnGetLocalRotation=LocalQ;
        c.fnSetWorldPosition=SetWorldP; c.fnSetLocalRotation=SetLocalQ; c.fnGetTransformParent=Parent;
        c.probeFirstFrameDone=true; c.SetExperimentalWaistSupportEnabled(false); c.auxDriveMode=kAuxSilence;
        IdolBoneBinding b; b.actor=&nodes[0]; b.actorTransform=&nodes[0]; b.rootTransform=&nodes[2];
        b.bindPoseCaptured=true; b.defensivelyMuted=true;
        b.restActorPos=nodes[0].position; b.restActorRot=nodes[0].rotation;
        b.restRootPos=b.restActorRot.Inverse().Rotate(WorldPosition(&nodes[2])-b.restActorPos);
        b.placementRestRootLocal=nodes[2].rotation; b.placementRestRootCaptured=true;
        for(const auto& bone:clip.bones)
        {
            const int i=bone.humanoid_id==0?2:bone.humanoid_id==7?3:4;
            b.boneTransforms.push_back(&nodes[size_t(i)]); b.boneHids.push_back(bone.humanoid_id);
            b.restLocals.push_back(nodes[size_t(i)].rotation);
            b.restRotations.push_back((b.restActorRot.Inverse()*WorldRotation(&nodes[size_t(i)])).Normalized());
        }
        c.currentBinding=b;
    }
}

static void TestEnsemblePlacement()
{
    using namespace ensemblePlacementFixture;
    BeginTest("ensemble placement rotates completed hierarchical pose around scene pivot");
    const auto clip=Clip(); IdolController c; Wire(c,clip);
    const Vector3 translation{.4f,.2f,-.8f}; c.SetInitialPositionOffset(translation);
    c.ApplyMotion(clip,1.f/30);
    const auto baseWrites=rotationWrites.size();
    std::array<Vector3,4> beforeP; std::array<Quaternion,4> beforeQ;
    for(size_t i=0;i<4;++i){beforeP[i]=WorldPosition(&nodes[i+2]);beforeQ[i]=WorldRotation(&nodes[i+2]);}
    Check(c.SetPlacementYawDegrees(90),"finite world placement yaw accepted");
    rotationWrites.clear(); c.ApplyMotion(clip,1.f/30);
    const auto yaw=Quaternion::FromEulerDegreesXYZ(0,90,0); const auto pivot=nodes[0].position+translation;
    bool coherent=true;
    for(size_t i=0;i<4;++i)coherent &= NearP(WorldPosition(&nodes[i+2]),pivot+yaw.Rotate(beforeP[i]-pivot)) &&
        NearQ(WorldRotation(&nodes[i+2]),yaw*beforeQ[i]);
    Check(coherent,"rotated intermediate parent, descendants and an unkeyed child share one rigid world yaw");
    Check(rotationWrites.size()==baseWrites+1 && rotationWrites.back()==&nodes[2],
        "only one placement root rotation is appended after all ordinary bone writes");
    const auto placed=WorldPosition(&nodes[5]); const auto placedQ=WorldRotation(&nodes[5]);
    for(int i=0;i<25;++i)c.ApplyMotion(clip,1.f/30);
    Check(NearP(WorldPosition(&nodes[5]),placed)&&NearQ(WorldRotation(&nodes[5]),placedQ),
        "repeated sampling at the same time cannot accumulate root rotation or displacement");
    c.SetPlacementYawDegrees(0); rotationWrites.clear(); c.ApplyMotion(clip,1.f/30);
    Check(NearP(WorldPosition(&nodes[5]),beforeP[3])&&NearQ(WorldRotation(&nodes[5]),beforeQ[3])&&
        rotationWrites.size()==baseWrites,"zero yaw preserves the existing output and write count");
    c.SetPlacementYawDegrees(45);
    Check(!c.SetPlacementYawDegrees(std::numeric_limits<float>::quiet_NaN())&&c.placementYawDegrees==45,
        "nonfinite yaw cannot replace a valid placement");
    Check(c.SetPlacementYawDegrees(450)&&c.placementYawDegrees==90,"yaw wraps without changing its geometric value");

    BeginTest("partial clips cannot accumulate placement on an unkeyed Hips root");
    const auto partial=Clip(false); IdolController p; Wire(p,partial);
    p.ApplyMotion(partial,0); const auto original=WorldRotation(&nodes[2]);
    const auto originalChild=WorldRotation(&nodes[5]); p.SetPlacementYawDegrees(-63); p.ApplyMotion(partial,0);
    const auto expected=WorldRotation(&nodes[5]); const auto expectedP=WorldPosition(&nodes[5]);
    for(int i=0;i<30;++i)p.ApplyMotion(partial,0);
    Check(NearQ(WorldRotation(&nodes[5]),expected)&&NearP(WorldPosition(&nodes[5]),expectedP),
        "a clip without any Hips rotation track starts from its captured root baseline each frame");
    p.SetPlacementYawDegrees(0); p.ApplyMotion(partial,0);
    Check(NearQ(WorldRotation(&nodes[2]),original)&&NearQ(WorldRotation(&nodes[5]),originalChild),
        "resetting yaw removes the prior placement even when the clip never writes Hips");
    p.SetPlacementYawDegrees(77);p.ApplyMotion(partial,0);p.ResetToRestPose(p.currentBinding);
    Check(NearQ(nodes[2].rotation,p.currentBinding.placementRestRootLocal),
        "native restoration also removes yaw from an unkeyed Hips root");

    BeginTest("ensemble runtime configuration copy preserves per-clip and actor state");
    IdolController source,target; source.fnGetWorldPosition=WorldP; source.fnGetTransformParent=Parent;
    source.armOffsetPitch=9;source.eyeMotionExperimentMode=2;source.facialMikuMouthFit=false;
    source.rootHeightCalibration.offset=.1f;target.rootHeightCalibration.offset=.7f;
    source.eyeMotionSourceStatus=EyeMotionExperimentStatus::Legacy;
    target.eyeMotionSourceStatus=EyeMotionExperimentStatus::UnverifiedSource;
    target.placementYawDegrees=23;target.targetActor=&nodes[5];target.initialPositionOffset={1,2,3};
    target.CopyRuntimeConfigurationFrom(source);
    Check(target.fnGetWorldPosition==WorldP&&target.fnGetTransformParent==Parent&&target.armOffsetPitch==9&&
        target.eyeMotionExperimentMode==2&&!target.facialMikuMouthFit,"runtime adapters and common user policy propagate");
    Check(target.rootHeightCalibration.offset==.7f&&target.eyeMotionSourceStatus==EyeMotionExperimentStatus::UnverifiedSource&&
        target.targetActor==&nodes[5]&&target.placementYawDegrees==23&&NearP(target.initialPositionOffset,{1,2,3}),
        "configuration copy does not import another clip calibration, source evidence, actor or placement");
}
