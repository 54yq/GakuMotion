#pragma once
// A requested, Unity-thread-only observation. It does not bind or move bones.
namespace LiveArmSnapshot
{
    inline void RecordGarment(void* root, void* actor, uint64_t sequence, const char* phase,
                              unsigned slot, int actual, int dispatchFrame, const GbbStatus* status)
    {
        if (!root || !g_getChildCountNative || !g_getChildNative) return;
        // Breadth-first samples favor the attachment end of the cloth chains.
        constexpr size_t maxTransforms = 1024;
        constexpr size_t maxSamples = 32;
        std::vector<void*> queue{root};
        size_t recorded = 0;
        for (size_t i = 0; i < queue.size() && recorded < maxSamples; ++i)
        {
            void* node = queue[i];
            const auto name = HelperGetTransformName(node);
            if (GakuMotion::FallbackPhysics::IsGarmentBone(name))
            {
                void* parent = g_getParentNative(node);
                if (parent)
                {
                    float anchor[3]{}, local[4]{}, parentWorld[4]{};
                    HelperGetWorldPos(node, anchor);
                    HelperGetLocalRot(node, local);
                    HelperGetWorldRot(parent, parentWorld);
                    const auto parentName = HelperGetTransformName(parent);
                    g_hostContext.logInfo(
                        "[GakuMotion][LiveGarmentBone] seq=%llu phase=%s actor=%p transform=%p bone=%s parent=%s anchor=%.6f,%.6f,%.6f local=%.6f,%.6f,%.6f,%.6f parent_world=%.6f,%.6f,%.6f,%.6f",
                        static_cast<unsigned long long>(sequence), phase, actor, node, name.c_str(), parentName.c_str(),
                        anchor[0], anchor[1], anchor[2], local[0], local[1], local[2], local[3],
                        parentWorld[0], parentWorld[1], parentWorld[2], parentWorld[3]);
                    ++recorded;
                }
            }
            const int count = HelperGetChildCount(node);
            if (count < 0 || count > 256) continue;
            for (int j = 0; j < count && queue.size() < maxTransforms; ++j)
                if (void* child = HelperGetChild(node, j)) queue.push_back(child);
        }
        const bool native = actual == 2 && status && status->actor == actor && status->injected;
        g_hostContext.logInfo("[GakuMotion][LiveGarmentSample] seq=%llu phase=%s actor=%p slot=%u actual=%d dispatch_frame=%d native_source_frame=%d bones=%zu limited=%d",
            static_cast<unsigned long long>(sequence), phase, actor, slot, actual, dispatchFrame,
            native ? status->source_frame : -1, recorded,
            int(recorded == maxSamples || queue.size() == maxTransforms));
    }

    inline void Record(void* actor, uint64_t sequence, const char* phase, const GakuMotion::Core& core)
    {
        if (!actor || !sequence || !phase || !g_hostContext.logInfo || !g_getPosNative || !g_getRotNative ||
            !g_getWorldPosNative || !g_getWorldRotNative || !g_getParentNative || !g_getNameNative) return;
        const auto* entry=core.sceneActorRegistry.Find(actor);
        const GakuMotion::MotionPlayer* observedPlayer=nullptr;
        unsigned role = 0;
        int actual = -1, dispatchFrame = -1;
        const GbbStatus* physicsStatus = nullptr;
        void* mainTarget=core.player.controller.targetActor ? core.player.controller.targetActor : core.player.controller.currentBinding.actor;
        if (mainTarget==actor)
        {
            observedPlayer=&core.player;
            role = 1;
            if (g_physicsActor == actor)
            {
                actual = g_physicsActual;
                dispatchFrame = g_physicsFrame;
                physicsStatus = &g_burstBridge.status;
            }
        }
        const auto& followers = core.ensemble.Followers();
        for (size_t i = 0; i < followers.size(); ++i)
        {
            const auto& follower = followers[i];
            if (follower.player && follower.player->controller.targetActor==actor &&
                core.sceneActorRegistry.Resolve(follower.actorToken,core.sceneActorRegistry.Epoch())==actor)
            {
                observedPlayer=follower.player.get();
                role = static_cast<unsigned>(i + 2);
                if (i < g_followerPhysics.size() && g_followerPhysics[i].actor == actor &&
                    g_followerPhysics[i].bindingGeneration == follower.bindingGeneration)
                {
                    actual = g_followerPhysics[i].actual;
                    dispatchFrame = g_followerPhysics[i].frame;
                    if (actual == 2) physicsStatus = &g_followerBridge.State(i).status;
                }
                break;
            }
        }
        g_hostContext.logInfo("[GakuMotion][LiveArmSnapshot] seq=%llu phase=%s actor=%p actor_token=%llu character=%s scene=%d live_time=%.4f motion_time=%.4f state=%d takeover=%d",
            static_cast<unsigned long long>(sequence),phase,actor,static_cast<unsigned long long>(entry?entry->token:0),
            entry?entry->character.c_str():"unknown",core.liveOwnerScene,core.liveObservation.time,observedPlayer?observedPlayer->currentTime:-1.0,
            observedPlayer?int(observedPlayer->state):-1,int(core.liveTakeoverActive));
        if (observedPlayer) {
            const auto& ctl=observedPlayer->controller;
            const auto binding=ctl.bindings.find(actor);
            const GakuMotion::IdolBoneBinding* captured=binding!=ctl.bindings.end()?&binding->second:
                (ctl.currentBinding.actor==actor?&ctl.currentBinding:nullptr);
            g_hostContext.logInfo("[GakuMotion][LiveArmInputs] seq=%llu phase=%s actor=%p paused=%d aux_mode=%d twist_share=%.4f axis_deg=%.3f overrides=%d rebind_policy=%d preserve_animator=%d bound=%d clip_version=%d local_delta=%d source_time=%.4f",
                static_cast<unsigned long long>(sequence),phase,actor,int(core.liveObservation.paused),ctl.auxDriveMode,
                ctl.wristTwistShare,ctl.forearmAxisAdjustmentDeg,ctl.lastAppliedOverrideCount,int(ctl.useAnimatorRebind),
                int(ctl.preserveAnimatorForSecondaryPhysics),int(captured && captured->bindPoseCaptured),
                int(observedPlayer->clip.version),int(observedPlayer->clip.IsLocalDeltaStream()),observedPlayer->MotionSourceTime());
            if (captured && captured->bindPoseCaptured) {
                auto logq=[&](const char* name,const GakuMotion::Quaternion& q) {
                    g_hostContext.logInfo("[GakuMotion][LiveArmBind] seq=%llu phase=%s actor=%p name=%s rot=%.6f,%.6f,%.6f,%.6f",
                        static_cast<unsigned long long>(sequence),phase,actor,name,q.x,q.y,q.z,q.w);
                };
                const auto& l=captured->leftArmAux; const auto& r=captured->rightArmAux;
                logq("LeftUpperArmRestRel",l.upperArmRestRel); logq("RightUpperArmRestRel",r.upperArmRestRel);
                logq("LeftForeArmRestRel",l.foreArmRestRel); logq("RightForeArmRestRel",r.foreArmRestRel);
                logq("LeftForeArmHRestLocal",l.foreArmHRestLocal); logq("RightForeArmHRestLocal",r.foreArmHRestLocal);
                logq("LeftRollHRestLocal",l.foreArmRollHRestLocal); logq("RightRollHRestLocal",r.foreArmRollHRestLocal);
                logq("LeftHandHRestLocal",l.handHRestLocal); logq("RightHandHRestLocal",r.handHRestLocal);
                for (size_t i=0;i<captured->boneHids.size() && i<captured->restLocals.size();++i)
                    if (captured->boneHids[i]>=11 && captured->boneHids[i]<=18) {
                        const auto& q=captured->restLocals[i];
                        g_hostContext.logInfo("[GakuMotion][LiveArmBindBone] seq=%llu phase=%s actor=%p hid=%u local_rot=%.6f,%.6f,%.6f,%.6f",
                            static_cast<unsigned long long>(sequence),phase,actor,unsigned(captured->boneHids[i]),q.x,q.y,q.z,q.w);
                    }
                if (core.liveTakeoverActive && core.config.secondaryPhysicsBackend == 2)
                    RecordGarment(captured->actorTransform, actor, sequence, phase, role, actual, dispatchFrame, physicsStatus);
            }
            if (std::strcmp(phase,"after_mmd_pose")==0 && observedPlayer->clip.total_frames) {
                GakuMotion::Vector3 root; std::vector<GakuMotion::Quaternion> sampled;
                observedPlayer->clip.Sample(observedPlayer->MotionSourceTime(),root,sampled);
                for (size_t i=0;i<sampled.size() && i<observedPlayer->clip.bones.size();++i) {
                    const int hid=observedPlayer->clip.bones[i].humanoid_id;
                    if (hid<11 || hid>18) continue;
                    const auto& q=sampled[i];
                    g_hostContext.logInfo("[GakuMotion][LiveArmClip] seq=%llu actor=%p hid=%d source_time=%.4f raw_rot=%.6f,%.6f,%.6f,%.6f",
                        static_cast<unsigned long long>(sequence),actor,hid,observedPlayer->MotionSourceTime(),q.x,q.y,q.z,q.w);
                }
            }
        }
        auto print=[&](const char* label,void* tr) {
            if(!tr) { g_hostContext.logInfo("[GakuMotion][LiveArmBone] seq=%llu phase=%s bone=%s missing=1",static_cast<unsigned long long>(sequence),phase,label); return; }
            float lp[3]{},wp[3]{},lr[4]{},wr[4]{};
            HelperGetLocalPos(tr,lp);HelperGetWorldPos(tr,wp);HelperGetLocalRot(tr,lr);HelperGetWorldRot(tr,wr);
            void* parent=g_getParentNative(tr);
            const auto parentName=parent?HelperGetTransformName(parent):std::string();
            g_hostContext.logInfo("[GakuMotion][LiveArmBone] seq=%llu phase=%s bone=%s parent=%s local_pos=%.6f,%.6f,%.6f local_rot=%.6f,%.6f,%.6f,%.6f world_pos=%.6f,%.6f,%.6f world_rot=%.6f,%.6f,%.6f,%.6f",
                static_cast<unsigned long long>(sequence),phase,label,parentName.c_str(),lp[0],lp[1],lp[2],lr[0],lr[1],lr[2],lr[3],wp[0],wp[1],wp[2],wr[0],wr[1],wr[2],wr[3]);
        };
        struct Bone { int id; const char* name; };
        const Bone bones[]={{0,"Hips"},{7,"Spine"},{11,"LeftShoulder"},{12,"RightShoulder"},
            {13,"LeftUpperArm"},{14,"RightUpperArm"},{15,"LeftForeArm"},{16,"RightForeArm"},{17,"LeftHand"},{18,"RightHand"}};
        for(const auto& bone:bones) print(bone.name,HelperGetBoneTransform(actor,bone.id));
        for(int side=0;side<2;++side) {
            void* fore=HelperGetBoneTransform(actor,15+side);
            const char* names[2][3]={{"LeftForeArm_Roll_H","LeftForeArm_H","LeftHand_H"},
                                     {"RightForeArm_Roll_H","RightForeArm_H","RightHand_H"}};
            for(const char* name:names[side]) print(name,LiveActorProps::UniqueChild(fore,name));
        }
    }
}
