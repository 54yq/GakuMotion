#pragma once

// =============================================================================
// GuiMotionPage.hpp — 动作控制页面（场景驱动角色 / 初始位置 / 高级动作）
// =============================================================================

#include "GuiOverlayCommon.hpp"
#include "GkGuiIcons.hpp"
#include "../GkGuiOverlay.hpp"
#include "../GkBoneOverride.hpp"
#include "../GkIdolController.hpp"
#include "../GkLiveActorPropPolicy.hpp"

#ifdef GKMOTION_GUI_ACTIVE
namespace GakuMotion
{
	inline void DrawMotionPage(GuiOverlay& overlay, const GuiContext& ctx)
	{
		if (!ctx.player)
		{
			ImGui::TextDisabled("播放器尚未就绪");
			return;
		}

		MotionPlayer& player = *ctx.player;
		IdolController& controller = player.controller;
		controller.useRestRotations = true; // 静息姿态对齐自动保持启用

		ImGui::TextUnformatted("驱动角色");
		if (ImGui::GetContentRegionAvail().x >= ImGui::CalcTextSize("物理：原生待确认，当前内建").x + ImGui::GetStyle().ItemSpacing.x) ImGui::SameLine();
		const auto physicalState = ClassifyPhysicsMonitor(ctx.physicsMonitor, ctx.physicsMonitorFresh);
		const char* physicalLabel = physicalState == PhysicsMonitorState::Native ? "原生物理运行中" :
			physicalState == PhysicsMonitorState::Builtin ? "内建物理运行中" :
			physicalState == PhysicsMonitorState::Waiting ? (ctx.physicsMonitor.actual == 1 ? "原生待确认，当前内建" : "原生待确认") :
			physicalState == PhysicsMonitorState::Fault ? (ctx.physicsMonitor.actual == 1 ? "原生异常，当前内建" : "原生异常") :
			physicalState == PhysicsMonitorState::Off ? "已关闭" :
			physicalState == PhysicsMonitorState::Stale ? "暂无当前数据" : "等待播放";
		ImGui::TextDisabled("物理：%s", physicalLabel);
		const std::string targetLabel = ctx.sceneActors.empty() ? "当前场景没有可驱动角色" :
			(ctx.targetActorLabel.empty() ? "正在识别角色" : ctx.targetActorLabel);
		ImGui::SetNextItemWidth((std::min)(360.0f, ImGui::GetContentRegionAvail().x));
		ImGui::BeginDisabled(ctx.sceneActors.empty() || !ctx.fnSelectTargetActor);
		if (ImGui::BeginCombo("##drive_actor_combo", targetLabel.c_str()))
		{
			for (const auto& actor : ctx.sceneActors)
			{
				const std::string label = actor.label + "##actor_" + std::to_string(actor.token);
				if (ImGui::Selectable(label.c_str(), actor.selected) && ctx.fnSelectTargetActor)
					ctx.fnSelectTargetActor(ctx.self, actor.token, ctx.actorRosterEpoch);
				if (actor.selected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		ImGui::EndDisabled();
        if (ctx.fnSetActorModelHidden)
            for (const auto& actor : ctx.sceneActors) if (actor.selected) {
                bool hidden = actor.modelHidden;
                if (ImGui::Checkbox("隐藏角色模型##primary_model", &hidden))
                    ctx.fnSetActorModelHidden(ctx.self, actor.token, ctx.actorRosterEpoch, hidden);
            }
        if (ctx.ensembleNotice.rfind("模型显隐未完成", 0) == 0)
            ImGui::TextWrapped("%s", ctx.ensembleNotice.c_str());
		ImGui::TextWrapped("表情随驱动角色自动适配。");

		if (ImGui::CollapsingHeader("场景道具", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ctx.fnScanActorProps && ImGui::Button("重新扫描道具")) ctx.fnScanActorProps(ctx.self);
			if (ctx.actorProps.empty()) ImGui::TextDisabled("当前没有识别到可列出的角色道具。");
			if (!ctx.actorPropsEditable) ImGui::TextDisabled("可查看已识别的道具，当前场景暂不可调整显隐。");
			for (const auto& prop : ctx.actorProps)
			{
				const auto key = ActorPropKey(prop);
				const auto label = prop.actorLabel + " / " + live_actor_props::DisplayName(prop.propName) + "##" + key;
				bool hidden = prop.hidden;
				ImGui::BeginDisabled(!ctx.actorPropsEditable || !prop.controllable || !ctx.fnSetActorPropHidden);
				if (ImGui::Checkbox(label.c_str(), &hidden))
					ctx.fnSetActorPropHidden(ctx.self, prop.actorToken, ctx.actorRosterEpoch, key.c_str(), hidden);
				ImGui::EndDisabled();
				if (!prop.active && !prop.hidden)
				{
					ImGui::SameLine();
					ImGui::TextDisabled("未启用");
				}
			}
			if (ctx.actorPropsEditable) ImGui::TextDisabled("勾选可隐藏道具。");
			ImGui::Spacing();
		}

		// =========================================================================
		// 3. 初始角色位置偏移
		// =========================================================================
		ImGui::TextUnformatted("主角色初始站位");
		uint64_t selectedActorToken = 0;
		for (const auto& actor : ctx.sceneActors) if (actor.selected) selectedActorToken = actor.token;
		if (overlay.initialPlacementActorToken != selectedActorToken)
		{
			overlay.initialPlacementActorToken = selectedActorToken;
			overlay.initialPositionEditing = false;
			overlay.initialYawEditing = false;
		}

		if (!overlay.initialPositionEditing)
		{
			const auto offset = controller.initialPositionOffset;
			overlay.initialPositionEdit[0] = offset.x;
			overlay.initialPositionEdit[1] = offset.y;
			overlay.initialPositionEdit[2] = offset.z;
		}

		const float dragItemWidth = (std::max)(40.0f, (ImGui::GetContentRegionAvail().x - 16.0f) / 3.0f);
		bool posChanged = false;

		// 位置 X / Y / Z
		ImGui::TextUnformatted("世界坐标偏移 (米)");
		ImGui::SetNextItemWidth(dragItemWidth);
		if (ImGui::DragFloat("##pos_x", &overlay.initialPositionEdit[0], 0.01f, -256.0f, 256.0f, "X: %.2f", ImGuiSliderFlags_AlwaysClamp))
			posChanged = true;

		ImGui::SameLine();
		ImGui::SetNextItemWidth(dragItemWidth);
		if (ImGui::DragFloat("##pos_y", &overlay.initialPositionEdit[1], 0.01f, -256.0f, 256.0f, "Y: %.2f", ImGuiSliderFlags_AlwaysClamp))
			posChanged = true;

		ImGui::SameLine();
		ImGui::SetNextItemWidth(dragItemWidth);
		if (ImGui::DragFloat("##pos_z", &overlay.initialPositionEdit[2], 0.01f, -256.0f, 256.0f, "Z: %.2f", ImGuiSliderFlags_AlwaysClamp))
			posChanged = true;

		if (posChanged)
		{
			overlay.initialPositionEditing = true;
			if (ctx.fnSetInitialPosition)
			{
				ctx.fnSetInitialPosition(ctx.self, Vector3(overlay.initialPositionEdit[0],
				                                           overlay.initialPositionEdit[1],
				                                           overlay.initialPositionEdit[2]));
			}
			else
			{
				controller.SetInitialPositionOffset(Vector3(overlay.initialPositionEdit[0],
				                                            overlay.initialPositionEdit[1],
				                                            overlay.initialPositionEdit[2]));
			}
		}
		if (!overlay.initialYawEditing) overlay.initialYawEdit = controller.initialYawDegrees;
		ImGui::SetNextItemWidth((std::min)(220.0f, ImGui::GetContentRegionAvail().x));
		if (ImGui::DragFloat("##main_yaw", &overlay.initialYawEdit, .5f, -180.0f, 180.0f, "朝向: %.1f 度"))
		{
			overlay.initialYawEditing = true;
			if (ctx.fnSetInitialYaw) ctx.fnSetInitialYaw(ctx.self, overlay.initialYawEdit);
			else controller.SetInitialYawDegrees(overlay.initialYawEdit);
		}

		// 重置与保存按钮
		if (GuiIcons::Button("重置站位", GuiIcons::Icon::Reset, ImVec2(120, 24)))
		{
			overlay.initialPositionEdit[0] = 0.0f;
			overlay.initialPositionEdit[1] = 0.0f;
			overlay.initialPositionEdit[2] = 0.0f;
			overlay.initialPositionEditing = false;
			overlay.initialYawEdit = 0.0f;
			overlay.initialYawEditing = false;
			if (ctx.fnSetInitialPosition)
				ctx.fnSetInitialPosition(ctx.self, Vector3(0.0f, 0.0f, 0.0f));
			else
				controller.SetInitialPositionOffset(Vector3(0.0f, 0.0f, 0.0f));
			if (ctx.fnSetInitialYaw) ctx.fnSetInitialYaw(ctx.self, 0.0f);
			else controller.SetInitialYawDegrees(0.0f);
		}
		GuiIcons::SameLineIfRoom("保存初始位置为默认", ImVec2(150, 24));
		if (GuiIcons::Button("保存初始位置为默认", GuiIcons::Icon::Save, ImVec2(150, 24)))
		{
			if (ctx.fnSaveInitialPosition) ctx.fnSaveInitialPosition(ctx.self);
		}
		if (!ctx.initialPositionNotice.empty())
		{
			const bool failed = ctx.initialPositionNotice.find("失败") != std::string::npos ||
				ctx.initialPositionNotice.find("无效") != std::string::npos;
			if (failed) ImGui::PushStyleColor(ImGuiCol_Text, GuiTheme::ErrorColor());
			ImGui::TextWrapped("%s", ctx.initialPositionNotice.c_str());
			if (failed) ImGui::PopStyleColor();
		}
		ImGui::TextWrapped("位置与朝向即时生效；朝向会同时旋转动作位移。切换动作时保留；停止后恢复场景原位。保存后作为下次启动的默认站位。");

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();


		if (ImGui::CollapsingHeader("多人同步播放"))
		{
			ImGui::TextWrapped("上方角色为主角色。其他角色分别选择曲库中的动作，统一使用主角色的音乐、镜头和进度。停止播放后可修改绑定。");
			ImGui::TextWrapped("绑定后可分别调整角色站位；未绑定的槽位不会参与播放。");
			ImGui::BeginDisabled(!ctx.fnAlignEnsemble || std::none_of(ctx.ensembleFollowers.begin(), ctx.ensembleFollowers.end(), [](const auto& f) { return f.actorToken != 0; }));
			if (ImGui::Button("以主角色对齐原曲队形")) ctx.fnAlignEnsemble(ctx.self);
			ImGui::EndDisabled();
			ImGui::TextWrapped("先绑定并预览各角色；对齐会读取原始 VMD，恢复副角色的初始站位并统一朝向和走位，可再次微调。主角色站位不变。");
			ImGui::Separator();


            ImGui::BeginDisabled(!ctx.fnAutoAssignEnsemble || player.state == PlaybackState::Playing || player.state == PlaybackState::Paused);
            if (ImGui::Button("按名称自动分配")) ctx.fnAutoAssignEnsemble(ctx.self);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("停止播放后，按当前动作同目录、同前缀的文件名顺序分配给场上角色");
            ImGui::SameLine();
            if (ImGui::Button("新增角色")) {
                auto it = std::find(overlay.ensembleHiddenSlots.begin(), overlay.ensembleHiddenSlots.end(), true);
                if (it != overlay.ensembleHiddenSlots.end()) *it = false;
                else ++overlay.ensembleVisibleSlots;
            }
            for (size_t i=0;i<ctx.ensembleFollowers.size();++i)
                if(ctx.ensembleFollowers[i].actorToken) overlay.ensembleVisibleSlots=(std::max)(overlay.ensembleVisibleSlots,i+1);
            if (overlay.ensembleSeenAutoGeneration != ctx.ensembleAutoGeneration)
                overlay.ensembleVisibleSlots=(std::max)(overlay.ensembleVisibleSlots,ctx.ensembleAutoActors.size());
            const size_t slots = overlay.ensembleVisibleSlots;
            overlay.ensembleHiddenSlots.resize(slots, false);
            overlay.ensembleRemovePending.resize(slots, false);
            overlay.ensembleRemoveFrames.resize(slots, 0);
            overlay.ensembleDraftActor.resize(slots);
            overlay.ensembleDraftMotion.resize(slots);
            overlay.ensembleDraftOffset.resize(slots);
            overlay.ensemblePositionGeneration.resize(slots);
            overlay.ensemblePositionDraft.resize(slots);
            overlay.ensembleYawDraft.resize(slots);

			if (overlay.ensembleDraftEpoch != ctx.actorRosterEpoch)
			{
				overlay.ensembleDraftEpoch = ctx.actorRosterEpoch;
				for (size_t i = 0; i < (std::min)(slots, ctx.ensembleFollowers.size()); ++i)
				{
					overlay.ensembleDraftActor[i] = ctx.ensembleFollowers[i].actorToken;
					overlay.ensembleDraftMotion[i] = ctx.ensembleFollowers[i].motionKey;
					overlay.ensembleDraftOffset[i] = ctx.ensembleFollowers[i].offsetSeconds;
				}
			}
            if (overlay.ensembleSeenAutoGeneration != ctx.ensembleAutoGeneration) {
                overlay.ensembleSeenAutoGeneration=ctx.ensembleAutoGeneration;
                for(size_t i=0;i<(std::min)(slots,ctx.ensembleAutoActors.size());++i) {
                    overlay.ensembleHiddenSlots[i]=false; overlay.ensembleRemovePending[i]=false;
                    overlay.ensembleDraftActor[i]=ctx.ensembleAutoActors[i];
                    overlay.ensembleDraftMotion[i]=i<ctx.ensembleFollowers.size()?ctx.ensembleFollowers[i].motionKey:std::string{};
                    overlay.ensembleDraftOffset[i]=0.0f;
                }
            }
            const size_t displayedRoles = static_cast<size_t>(std::count(overlay.ensembleHiddenSlots.begin(), overlay.ensembleHiddenSlots.end(), false));
            const bool scrollRoles = displayedRoles > 3;
            if (scrollRoles) ImGui::BeginChild("##ensemble_roles", ImVec2(0, 340.0f), true);
			for (size_t i = 0; i < slots; ++i)
			{
                const GuiEnsembleFollower emptyFollower{};
                const auto& currentFollower = i < ctx.ensembleFollowers.size() ? ctx.ensembleFollowers[i] : emptyFollower;
                if (overlay.ensembleRemovePending[i] && !currentFollower.actorToken) {
                    overlay.ensembleRemovePending[i] = false;
                    overlay.ensembleRemoveFrames[i] = 0;
                    overlay.ensembleHiddenSlots[i] = true;
                    overlay.ensembleDraftActor[i] = 0;
                    overlay.ensembleDraftMotion[i].clear();
                    overlay.ensembleDraftOffset[i] = 0.0f;
                }
                if (overlay.ensembleRemovePending[i] && currentFollower.actorToken && ++overlay.ensembleRemoveFrames[i] > 180) {
                    overlay.ensembleRemovePending[i] = false;
                    overlay.ensembleRemoveFrames[i] = 0;
                }
                if (overlay.ensembleHiddenSlots[i]) continue;
				ImGui::PushID(static_cast<int>(i));
				ImGui::Text("角色 %d", static_cast<int>(i) + 2);
				const auto& physics = currentFollower;
				const char* physicsLabel = !physics.actorToken ? "未绑定动作" :
					physics.physicsStatus.empty() ? (player.state == PlaybackState::Stopped ? "等待播放" : "等待角色骨架") : physics.physicsStatus.c_str();
				ImGui::SameLine(); ImGui::TextDisabled("物理：%s", physicsLabel);
                ImGui::SameLine();
                ImGui::BeginDisabled(player.state == PlaybackState::Playing || player.state == PlaybackState::Paused || overlay.ensembleRemovePending[i]);
                if (ImGui::SmallButton("删除角色")) {
                    if (currentFollower.actorToken && ctx.fnClearEnsembleFollower) {
                        overlay.ensembleRemovePending[i] = true;
                        ctx.fnClearEnsembleFollower(ctx.self, static_cast<int>(i), ctx.actorRosterEpoch);
                    } else if (!currentFollower.actorToken) {
                        overlay.ensembleHiddenSlots[i] = true;
                        overlay.ensembleDraftActor[i] = 0;
                        overlay.ensembleDraftMotion[i].clear();
                        overlay.ensembleDraftOffset[i] = 0.0f;
                    }
                }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && (player.state == PlaybackState::Playing || player.state == PlaybackState::Paused))
                    ImGui::SetTooltip("请先停止播放，再删除角色槽位");
                if (ctx.fnSetActorModelHidden && currentFollower.actorToken)
                    for (const auto& candidate : ctx.sceneActors) if (candidate.token == currentFollower.actorToken) {
                        bool hidden = candidate.modelHidden;
                        if (ImGui::Checkbox("隐藏角色模型##follower_model", &hidden))
                            ctx.fnSetActorModelHidden(ctx.self, candidate.token, ctx.actorRosterEpoch, hidden);
                    }
				ImGui::BeginDisabled(player.state == PlaybackState::Playing || player.state == PlaybackState::Paused);
				std::string actorLabel = "请选择角色";
				for (const auto& candidate : ctx.sceneActors)
					if (candidate.token == overlay.ensembleDraftActor[i]) actorLabel = candidate.label;
				ImGui::SetNextItemWidth((std::max)(100.0f, (std::min)(260.0f, ImGui::GetContentRegionAvail().x * .42f)));
				if (ImGui::BeginCombo("##ensemble_actor", actorLabel.c_str()))
				{
					for (const auto& candidate : ctx.sceneActors)
					{
						if (candidate.selected) continue;
						const std::string label = candidate.label + "##ensemble_" + std::to_string(candidate.token);
						if (ImGui::Selectable(label.c_str(), candidate.token == overlay.ensembleDraftActor[i]))
							overlay.ensembleDraftActor[i] = candidate.token;
					}
					ImGui::EndCombo();
				}
                GuiIcons::SameLineIfRoom("动作", ImVec2(180, 0));
				std::string motionLabel = "请选择动作";
				if (ctx.motions) for (const auto& motion : *ctx.motions)
					if (motion.name == overlay.ensembleDraftMotion[i]) motionLabel = motion.DisplayLabel();
				ImGui::SetNextItemWidth((std::max)(100.0f, (std::min)(300.0f, ImGui::GetContentRegionAvail().x)));
				if (ImGui::BeginCombo("##ensemble_motion", motionLabel.c_str()))
				{
					if (ctx.motions) for (const auto& motion : *ctx.motions)
					{
						const std::string label = motion.DisplayLabel() + "##ensemble_motion_" + motion.name;
						if (ImGui::Selectable(label.c_str(), motion.name == overlay.ensembleDraftMotion[i]))
							overlay.ensembleDraftMotion[i] = motion.name;
					}
					ImGui::EndCombo();
				}
                GuiIcons::SameLineIfRoom("延迟", ImVec2(150, 0));
				ImGui::SetNextItemWidth(130.0f);
				ImGui::DragFloat("##动作延迟（秒）", &overlay.ensembleDraftOffset[i], 0.01f, -30.0f, 30.0f, "%.2f");
                GuiIcons::SameLineIfRoom("绑定动作", ImVec2(90, 0));
				ImGui::BeginDisabled(!ctx.fnSetEnsembleFollower || !overlay.ensembleDraftActor[i] || overlay.ensembleDraftMotion[i].empty());
				if (ImGui::Button("绑定动作"))
					ctx.fnSetEnsembleFollower(ctx.self, static_cast<int>(i), overlay.ensembleDraftActor[i], ctx.actorRosterEpoch,
					                          overlay.ensembleDraftMotion[i].c_str(), overlay.ensembleDraftOffset[i], ctx.libraryRevision);
				ImGui::EndDisabled();
				ImGui::SameLine();
				ImGui::BeginDisabled(!ctx.fnClearEnsembleFollower || !currentFollower.actorToken);
				if (ImGui::Button("解除绑定")) ctx.fnClearEnsembleFollower(ctx.self, static_cast<int>(i), ctx.actorRosterEpoch);
				ImGui::EndDisabled();
				ImGui::EndDisabled();
				if (!currentFollower.motionKey.empty())
				{
                    if (!ImGui::TreeNode("站位与朝向")) { ImGui::PopID(); continue; }
					const auto& follower = currentFollower;
					if (overlay.ensemblePositionGeneration[i] != follower.bindingGeneration)
					{
						overlay.ensemblePositionGeneration[i] = follower.bindingGeneration;
						overlay.ensemblePositionDraft[i] = follower.position;
						overlay.ensembleYawDraft[i] = follower.yawDegrees;
					}
					// Apply programmatic formation changes once no position/yaw widget is being dragged.
					if (!ImGui::IsAnyItemActive())
					{
						overlay.ensemblePositionDraft[i] = follower.position;
						overlay.ensembleYawDraft[i] = follower.yawDegrees;
					}
					auto& offset = overlay.ensemblePositionDraft[i];
					const float width = (std::max)(40.0f, (ImGui::GetContentRegionAvail().x - 16.0f) / 3.0f);
					ImGui::TextUnformatted("角色站位偏移（世界坐标，米）");
					ImGui::BeginDisabled(!ctx.fnSetEnsemblePosition || !follower.actorToken || !follower.bindingGeneration);
					ImGui::SetNextItemWidth(width);
					bool positionChanged = ImGui::DragFloat("##ensemble_pos_x", &offset.x, .01f, -256.0f, 256.0f, "X: %.2f", ImGuiSliderFlags_AlwaysClamp);
					ImGui::SameLine(); ImGui::SetNextItemWidth(width);
					positionChanged |= ImGui::DragFloat("##ensemble_pos_y", &offset.y, .01f, -256.0f, 256.0f, "Y: %.2f", ImGuiSliderFlags_AlwaysClamp);
					ImGui::SameLine(); ImGui::SetNextItemWidth(width);
					positionChanged |= ImGui::DragFloat("##ensemble_pos_z", &offset.z, .01f, -256.0f, 256.0f, "Z: %.2f", ImGuiSliderFlags_AlwaysClamp);
					ImGui::SetNextItemWidth((std::min)(220.0f, ImGui::GetContentRegionAvail().x));
					ImGui::BeginDisabled(!ctx.fnSetEnsembleYaw);
					bool yawChanged = ImGui::DragFloat("##ensemble_yaw", &overlay.ensembleYawDraft[i], .5f,
					                                   -180.0f, 180.0f, "朝向: %.1f 度");
					ImGui::EndDisabled();
					if (ImGui::Button("重置角色站位"))
					{ offset = {}; overlay.ensembleYawDraft[i] = 0.0f; positionChanged = yawChanged = true; }
					if (positionChanged && ctx.fnSetEnsemblePosition)
						ctx.fnSetEnsemblePosition(ctx.self, static_cast<int>(i), follower.actorToken, ctx.actorRosterEpoch,
						                          follower.bindingGeneration, offset);
					if (yawChanged && ctx.fnSetEnsembleYaw)
						ctx.fnSetEnsembleYaw(ctx.self, static_cast<int>(i), follower.actorToken, ctx.actorRosterEpoch,
						                     follower.bindingGeneration, overlay.ensembleYawDraft[i]);
					ImGui::EndDisabled();
                    ImGui::TreePop();
				}
				ImGui::PopID();
			}
            if (scrollRoles) ImGui::EndChild();
			if (!ctx.ensembleNotice.empty()) ImGui::TextWrapped("%s", ctx.ensembleNotice.c_str());
		}
		if (ImGui::CollapsingHeader("表情幅度"))
		{
			auto& weights = controller.facialWeights;
			ImGui::SliderFloat("整体##face", &weights.overall, 0.0f, 2.0f, "%.2f 倍");
			ImGui::SliderFloat("眉毛##face", &weights.brow, 0.0f, 2.0f, "%.2f 倍");
			ImGui::SliderFloat("眼部##face", &weights.eye, 0.0f, 2.0f, "%.2f 倍");
			ImGui::SliderFloat("嘴部##face", &weights.mouth, 0.0f, 2.0f, "%.2f 倍");
			ImGui::TextWrapped("分项倍率乘以整体倍率，眼骨视线保持独立。");
			if (ImGui::Button("重置表情幅度")) weights = FacialWeights{};
			ImGui::SameLine();
			ImGui::BeginDisabled(!ctx.fnSaveFacialWeights);
			if (ImGui::Button("保存表情幅度")) ctx.fnSaveFacialWeights(ctx.self);
			ImGui::EndDisabled();
			if (!ctx.facialWeightsNotice.empty()) ImGui::TextWrapped("%s", ctx.facialWeightsNotice.c_str());
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// =========================================================================
		// 4. 高级动作设置 (折叠)
		// =========================================================================
		if (ImGui::CollapsingHeader("高级动作设置"))
		{
			ImGui::Indent(10.0f);
			bool resetAfterPlacement = ctx.placementResetExperiment;
			if (ImGui::Checkbox("站位调整后重置动态骨（实验）", &resetAfterPlacement) && ctx.fnSetPlacementResetExperiment)
				ctx.fnSetPlacementResetExperiment(ctx.self, resetAfterPlacement);
			ImGui::TextWrapped("站位停止变化约 0.5 秒后执行一次；动作播放继续。仅本次运行有效。");
			ImGui::TextUnformatted("手臂辅助骨模式（对照实验）");
			const char* auxLabels[] = {"正常：拟合模型", "静默：锁定初始姿态", "归零：单位旋转",
			                           "回放：录制数据", "旧版：世界空间解算"};
			const int auxMode = controller.auxDriveMode;
			const char* currentAuxLabel = auxMode >= 0 && auxMode < kAuxModeCount ? auxLabels[auxMode] : "未知";
			ImGui::SetNextItemWidth((std::min)(360.0f, ImGui::GetContentRegionAvail().x));
			ImGui::BeginDisabled(!ctx.injectionAllowed || !ctx.fnSetAuxDriveMode);
			if (ImGui::BeginCombo("##aux_drive_mode", currentAuxLabel))
			{
				for (int mode = 0; mode < kAuxModeCount; ++mode)
				{
					if (ImGui::Selectable(auxLabels[mode], mode == auxMode)) ctx.fnSetAuxDriveMode(ctx.self, mode);
					if (mode == auxMode) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			ImGui::EndDisabled();
			ImGui::TextWrapped("与 F8 切换相同，仅影响主角色，本次运行有效。对照可能导致手臂破面；正常模式可恢复默认。回放缺少录制数据时会退回静默。");
			ImGui::BeginDisabled(!ctx.injectionAllowed || !ctx.fnCaptureWristDiagnostic || player.state == PlaybackState::Stopped);
			if (ImGui::Button("记录手腕诊断") && ctx.fnCaptureWristDiagnostic) ctx.fnCaptureWristDiagnostic(ctx.self);
			ImGui::EndDisabled();
			ImGui::TextWrapped("记录当前主角色的手臂和手腕骨骼状态到日志，用于分析变形。");
			if (controller.wristDiagnosticStatus == 1)
				ImGui::TextWrapped("诊断请求已发送，等待角色姿态。");
			else if (controller.wristDiagnosticStatus == 2)
				ImGui::TextWrapped("已记录到日志（第 %llu 次，动作时间 %.3f 秒）。",
				                   static_cast<unsigned long long>(controller.wristDiagnosticCount), controller.wristDiagnosticTime);
			else if (controller.wristDiagnosticStatus == 3)
				ImGui::TextWrapped("记录失败：日志服务不可用。");
			// Offer every current scene actor, including those not yet motion-bound.
			const GuiSceneActor* selectedActor = nullptr;
			for (const auto& actor : ctx.sceneActors)
				if (actor.token == overlay.armCalibrationToken) selectedActor = &actor;
			if (!selectedActor)
			{
				for (const auto& actor : ctx.sceneActors) if (actor.selected) { selectedActor = &actor; break; }
				if (!selectedActor && !ctx.sceneActors.empty()) selectedActor = &ctx.sceneActors.front();
				overlay.armCalibrationToken = selectedActor ? selectedActor->token : 0;
			}
			overlay.armCalibrationEpoch = ctx.actorRosterEpoch;
			const std::string selectedLabel = selectedActor ? selectedActor->label : "尚未检测到角色";
			ImGui::SetNextItemWidth((std::min)(360.0f, ImGui::GetContentRegionAvail().x));
			ImGui::BeginDisabled(ctx.sceneActors.empty());
			if (ImGui::BeginCombo("校正角色", selectedLabel.c_str()))
			{
				for (const auto& actor : ctx.sceneActors)
				{
					const std::string label = actor.label + (actor.selected ? "（主角色）" : "") + "##arm_" + std::to_string(actor.token);
					if (ImGui::Selectable(label.c_str(), actor.token == overlay.armCalibrationToken)) overlay.armCalibrationToken = actor.token;
				}
				ImGui::EndCombo();
			}
			ImGui::EndDisabled();
			const GuiArmCalibration* selectedArm = nullptr;
			for (const auto& arm : ctx.sceneArmCalibrations) if (arm.token == overlay.armCalibrationToken) selectedArm = &arm;
			const bool isPrimary = !selectedArm || selectedArm->primary;
			auto setArm = [&](int operation, float value) {
				if (selectedArm && ctx.fnSetSceneArm)
					ctx.fnSetSceneArm(ctx.self, selectedArm->token, ctx.actorRosterEpoch, operation, value);
				else if (!selectedArm)
				{
					if ((operation == 0 || operation == 2) && ctx.fnSetWristTwistShare) ctx.fnSetWristTwistShare(ctx.self, operation == 2 ? 0.0f : value);
					if ((operation == 1 || operation == 2) && ctx.fnSetForearmAxisAdjustment) ctx.fnSetForearmAxisAdjustment(ctx.self, operation == 2 ? 45.0f : value);
					if (operation == 3 && ctx.fnSetForearmAxisLock) ctx.fnSetForearmAxisLock(ctx.self, value != 0.0f);
				}
			};
			float wristShare = selectedArm ? selectedArm->share : controller.wristTwistShare;
			bool locked = selectedArm ? selectedArm->locked : ctx.forearmAxisLocked;
			ImGui::BeginDisabled(!ctx.injectionAllowed || (selectedArm ? !ctx.fnSetSceneArm : !ctx.fnSetWristTwistShare));
			if (ImGui::SliderFloat("前臂扭转分担 [实验]", &wristShare, -1.0f, 1.0f, "%.2f")) setArm(0, wristShare);
			ImGui::EndDisabled();
			float axisDegrees = selectedArm ? selectedArm->degrees : controller.forearmAxisAdjustmentDeg;
			ImGui::BeginDisabled(!ctx.injectionAllowed || locked || (selectedArm ? !ctx.fnSetSceneArm : !ctx.fnSetForearmAxisAdjustment));
			if (ImGui::SliderFloat("前臂轴向校正 [实验]", &axisDegrees, -90.0f, 90.0f, "%.1f°")) setArm(1, axisDegrees);
			ImGui::SetNextItemWidth(120.0f);
			if (ImGui::InputFloat("角度（度）", &axisDegrees, 0.0f, 0.0f, "%.1f") && std::isfinite(axisDegrees))
				setArm(1, (std::max)(-90.0f, (std::min)(90.0f, axisDegrees)));
			if (ImGui::Button("-0.1°")) setArm(1, (std::max)(-90.0f, std::round(axisDegrees * 10.0f - 1.0f) / 10.0f));
			ImGui::SameLine();
			if (ImGui::Button("+0.1°")) setArm(1, (std::min)(90.0f, std::round(axisDegrees * 10.0f + 1.0f) / 10.0f));
			ImGui::SameLine();
			if (ImGui::Button("设为 +45°")) setArm(1, 45.0f);
			ImGui::SameLine();
			if (ImGui::Button("重置前臂校正")) setArm(2, 0.0f);
			ImGui::EndDisabled();
			ImGui::BeginDisabled(!ctx.injectionAllowed || (selectedArm ? !ctx.fnSetSceneArm : !ctx.fnSetForearmAxisLock));
			if (ImGui::Checkbox(!isPrimary ? "锁定当前角色前臂校正" : "锁定并保存前臂校正", &locked)) setArm(3, locked ? 1.0f : 0.0f);
			ImGui::EndDisabled();
			ImGui::TextWrapped("对所选角色的 gkclip 生效。重置为轴向 +45°、分担 0；锁定时请先解锁再调整或重置。");
			ImGui::TextWrapped("主角色锁定会保存；其他角色默认 +45°，调整和锁定在当前场景内保留。");
			if (selectedArm && !selectedArm->bound) ImGui::TextWrapped("该角色尚未绑定动作；设置将在绑定后生效。");
			if (isPrimary && !ctx.forearmAxisNotice.empty()) ImGui::TextWrapped("%s", ctx.forearmAxisNotice.c_str());
			ImGui::Spacing();
			overlay.DrawBoneOverridesSection(ctx);
			ImGui::Unindent(10.0f);
		}
	}
} // namespace GakuMotion
#endif
