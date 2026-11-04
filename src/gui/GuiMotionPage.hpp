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
