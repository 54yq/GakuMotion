#pragma once

// =============================================================================
// GuiMotionPage.hpp — 动作控制页面（场景驱动角色 / 初始位置 / 高级动作）
// =============================================================================

#include "GuiOverlayCommon.hpp"
#include "GkGuiIcons.hpp"
#include "../GkGuiOverlay.hpp"
#include "../GkBoneOverride.hpp"
#include "../GkIdolController.hpp"

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
		ImGui::TextWrapped("表情随驱动角色自动适配。");
		if (ImGui::CollapsingHeader("多人同步播放（最多三人）"))
		{
			ImGui::TextWrapped("上方角色为主角色。其他角色分别选择曲库中的动作，统一使用主角色的音乐、镜头和进度。停止播放后可修改绑定。");
			ImGui::TextWrapped("绑定后可分别调整角色站位；额外角色的次级物理暂不由插件驱动。");
			if (overlay.ensembleDraftEpoch != ctx.actorRosterEpoch)
			{
				overlay.ensembleDraftEpoch = ctx.actorRosterEpoch;
				for (size_t i = 0; i < ctx.ensembleFollowers.size(); ++i)
				{
					overlay.ensembleDraftActor[i] = ctx.ensembleFollowers[i].actorToken;
					overlay.ensembleDraftMotion[i] = ctx.ensembleFollowers[i].motionKey;
					overlay.ensembleDraftOffset[i] = ctx.ensembleFollowers[i].offsetSeconds;
				}
			}
			for (size_t i = 0; i < ctx.ensembleFollowers.size(); ++i)
			{
				ImGui::PushID(static_cast<int>(i));
				ImGui::Text("角色 %d", static_cast<int>(i) + 2);
				ImGui::BeginDisabled(player.state == PlaybackState::Playing || player.state == PlaybackState::Paused);
				std::string actorLabel = "请选择角色";
				for (const auto& candidate : ctx.sceneActors)
					if (candidate.token == overlay.ensembleDraftActor[i]) actorLabel = candidate.label;
				ImGui::SetNextItemWidth((std::min)(360.0f, ImGui::GetContentRegionAvail().x));
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
				std::string motionLabel = "请选择动作";
				if (ctx.motions) for (const auto& motion : *ctx.motions)
					if (motion.name == overlay.ensembleDraftMotion[i]) motionLabel = motion.DisplayLabel();
				ImGui::SetNextItemWidth((std::min)(360.0f, ImGui::GetContentRegionAvail().x));
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
				ImGui::SetNextItemWidth(180.0f);
				ImGui::DragFloat("动作延迟（秒）", &overlay.ensembleDraftOffset[i], 0.01f, -30.0f, 30.0f, "%.2f");
				ImGui::BeginDisabled(!ctx.fnSetEnsembleFollower || !overlay.ensembleDraftActor[i] || overlay.ensembleDraftMotion[i].empty());
				if (ImGui::Button("绑定动作"))
					ctx.fnSetEnsembleFollower(ctx.self, static_cast<int>(i), overlay.ensembleDraftActor[i], ctx.actorRosterEpoch,
					                          overlay.ensembleDraftMotion[i].c_str(), overlay.ensembleDraftOffset[i], ctx.libraryRevision);
				ImGui::EndDisabled();
				ImGui::SameLine();
				ImGui::BeginDisabled(!ctx.fnClearEnsembleFollower || !ctx.ensembleFollowers[i].actorToken);
				if (ImGui::Button("解除绑定")) ctx.fnClearEnsembleFollower(ctx.self, static_cast<int>(i), ctx.actorRosterEpoch);
				ImGui::EndDisabled();
				ImGui::EndDisabled();
				if (!ctx.ensembleFollowers[i].motionKey.empty())
				{
					ImGui::TextWrapped("当前动作：%s", ctx.ensembleFollowers[i].motionKey.c_str());
					ImGui::TextWrapped("状态：%s", ctx.ensembleFollowers[i].active ? "角色已驱动" :
					                   player.state == PlaybackState::Stopped ? "等待播放" : "等待可恢复的角色骨架");
					const auto& follower = ctx.ensembleFollowers[i];
					if (overlay.ensemblePositionGeneration[i] != follower.bindingGeneration)
					{
						overlay.ensemblePositionGeneration[i] = follower.bindingGeneration;
						overlay.ensemblePositionDraft[i] = follower.position;
					}
					auto& offset = overlay.ensemblePositionDraft[i];
					const float width = (std::max)(40.0f, (std::min)(110.0f, (ImGui::GetContentRegionAvail().x - 16.0f) / 3.0f));
					ImGui::TextUnformatted("角色站位偏移（世界坐标，米）");
					ImGui::BeginDisabled(!ctx.fnSetEnsemblePosition || !follower.actorToken || !follower.bindingGeneration);
					ImGui::SetNextItemWidth(width);
					bool positionChanged = ImGui::DragFloat("##ensemble_pos_x", &offset.x, .01f, -10.0f, 10.0f, "X: %.2f");
					ImGui::SameLine(); ImGui::SetNextItemWidth(width);
					positionChanged |= ImGui::DragFloat("##ensemble_pos_y", &offset.y, .01f, -10.0f, 10.0f, "Y: %.2f");
					ImGui::SameLine(); ImGui::SetNextItemWidth(width);
					positionChanged |= ImGui::DragFloat("##ensemble_pos_z", &offset.z, .01f, -10.0f, 10.0f, "Z: %.2f");
					if (ImGui::Button("重置角色站位")) { offset = {}; positionChanged = true; }
					if (positionChanged && ctx.fnSetEnsemblePosition)
						ctx.fnSetEnsemblePosition(ctx.self, static_cast<int>(i), follower.actorToken, ctx.actorRosterEpoch,
						                          follower.bindingGeneration, offset);
					ImGui::EndDisabled();
				}
				ImGui::PopID();
			}
			if (!ctx.ensembleNotice.empty()) ImGui::TextWrapped("%s", ctx.ensembleNotice.c_str());
		}
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::TextUnformatted("表情幅度");
		auto& face = controller.facialWeights;
		ImGui::SliderFloat("整体##face", &face.overall, 0.0f, 2.0f, "%.2f 倍");
		ImGui::SliderFloat("眉毛##face", &face.brow, 0.0f, 2.0f, "%.2f 倍");
		ImGui::SliderFloat("眼部##face", &face.eye, 0.0f, 2.0f, "%.2f 倍");
		ImGui::SliderFloat("嘴部##face", &face.mouth, 0.0f, 2.0f, "%.2f 倍");
		ImGui::TextWrapped("分项与整体倍率相乘；通常限制在 0–100%%。眼骨视线不受影响。");
		if (ImGui::Button("重置表情幅度")) face = FacialWeights{};
		ImGui::SameLine();
		if (ImGui::Button("保存表情幅度") && ctx.fnSaveFacialWeights) ctx.fnSaveFacialWeights(ctx.self);
		if (!ctx.facialWeightsNotice.empty()) ImGui::TextWrapped("%s", ctx.facialWeightsNotice.c_str());

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// =========================================================================
		// 3. 初始角色位置偏移
		// =========================================================================
		ImGui::TextUnformatted("初始角色位置偏移");

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
		if (ImGui::DragFloat("##pos_x", &overlay.initialPositionEdit[0], 0.01f, -10.0f, 10.0f, "X: %.2f"))
			posChanged = true;

		ImGui::SameLine();
		ImGui::SetNextItemWidth(dragItemWidth);
		if (ImGui::DragFloat("##pos_y", &overlay.initialPositionEdit[1], 0.01f, -10.0f, 10.0f, "Y: %.2f"))
			posChanged = true;

		ImGui::SameLine();
		ImGui::SetNextItemWidth(dragItemWidth);
		if (ImGui::DragFloat("##pos_z", &overlay.initialPositionEdit[2], 0.01f, -10.0f, 10.0f, "Z: %.2f"))
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

		// 重置与保存按钮
		if (GuiIcons::Button("重置位置偏移", GuiIcons::Icon::Reset, ImVec2(120, 24)))
		{
			overlay.initialPositionEdit[0] = 0.0f;
			overlay.initialPositionEdit[1] = 0.0f;
			overlay.initialPositionEdit[2] = 0.0f;
			overlay.initialPositionEditing = false;
			if (ctx.fnSetInitialPosition)
				ctx.fnSetInitialPosition(ctx.self, Vector3(0.0f, 0.0f, 0.0f));
			else
				controller.SetInitialPositionOffset(Vector3(0.0f, 0.0f, 0.0f));
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
		ImGui::TextWrapped("修改立即作用于当前驱动对象，切歌保留，停止后恢复场景原位；保存后作为下次启动的默认偏移。");

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// =========================================================================
		// 4. 高级动作设置 (折叠) — 仅保留骨骼微调选单
		// =========================================================================
		if (ImGui::CollapsingHeader("高级动作设置"))
		{
			ImGui::Indent(10.0f);
			overlay.DrawBoneOverridesSection(ctx);
			ImGui::Unindent(10.0f);
		}
	}
} // namespace GakuMotion
#endif
