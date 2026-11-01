#pragma once

// =============================================================================
// GuiMotionPage.hpp — 动作控制页面（驱动对象 / 表情适配 / 视线 / 初始位置 / 高级动作）
// =============================================================================

#include "GuiOverlayCommon.hpp"
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

		// =========================================================================
		// 1. 实际驱动对象与表情适配角色分别设置。
		// =========================================================================
		ImGui::TextUnformatted("驱动对象");
		ImGui::TextWrapped("目标: %s", ctx.targetActorLabel.empty() ? "未确认" : ctx.targetActorLabel.c_str());
		ImGui::TextWrapped("当前绑定: %s", ctx.boundActorLabel.empty() ? "尚未绑定角色" : ctx.boundActorLabel.c_str());
		ImGui::BeginDisabled(!controller.currentBinding.actor || !ctx.fnPickFocusedActor);
		if (ImGui::Button("只驱动当前绑定角色")) ctx.fnPickFocusedActor(ctx.self);
		ImGui::EndDisabled();
		ImGui::BeginDisabled(ctx.knownActorCount == 0 || !ctx.fnNextTargetActor);
		if (ImGui::Button("下一个场景角色")) ctx.fnNextTargetActor(ctx.self);
		ImGui::EndDisabled();
		if (ImGui::GetContentRegionAvail().x > 300.0f) ImGui::SameLine();
		ImGui::BeginDisabled(!ctx.fnClearTargetActor);
		if (ImGui::Button("驱动全部场景角色")) ctx.fnClearTargetActor(ctx.self);
		ImGui::EndDisabled();
		ImGui::TextWrapped("角色选择来自当前场景；“下一个”轮换一圈后回到全部角色。");

		ImGui::Spacing();
		ImGui::TextUnformatted("表情适配角色");
		static const char* kFacialCodes[] = {
			"auto", "hski", "ttmr", "fktn", "atbm", "kcna", "shro", "ssmk",
			"kllj", "hume", "hmsz", "jsna", "hrnm", "amao", "nasr", "krnh"
		};
		const std::string facialCode = ctx.config ? ctx.config->facialCharacterOverride : "auto";
		const std::string facialLabel = facialCode.empty() || facialCode == "auto"
			? "自动识别" : FormatIdolDisplayName(facialCode.c_str());
		ImGui::SetNextItemWidth((std::min)(300.0f, ImGui::GetContentRegionAvail().x));
		ImGui::BeginDisabled(!ctx.config);
		if (ImGui::BeginCombo("##facial_actor_combo", facialLabel.c_str()))
		{
			for (const char* code : kFacialCodes)
			{
				const bool isSelected = facialCode == code || (facialCode.empty() && std::strcmp(code, "auto") == 0);
				const std::string label = std::strcmp(code, "auto") == 0 ? "自动识别" : FormatIdolDisplayName(code);
				if (ImGui::Selectable(label.c_str(), isSelected) && ctx.config)
					ctx.config->facialCharacterOverride = code;
				if (isSelected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		ImGui::EndDisabled();
		ImGui::TextWrapped("仅覆盖表情适配身份，在本次运行中生效；驱动对象由上方单独选择。");

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// =========================================================================
		// 2. 视线 (Gaze)
		// =========================================================================
		ImGui::TextUnformatted("视线");

		bool isVR = (ctx.lookAt && ctx.lookAt->IsVRifyLiveGazeActive());

		if (isVR)
		{
			ImGui::BeginDisabled(true);
			int dummyMode = 0;
			ImGui::RadioButton("自动", &dummyMode, 0); ImGui::SameLine();
			ImGui::RadioButton("使用动作原始视线", &dummyMode, 1); ImGui::SameLine();
			ImGui::RadioButton("注视镜头", &dummyMode, 2);
			ImGui::EndDisabled();
			ImGui::TextColored(GuiTheme::WarningColor(), "视线由 GakuVRify 接管");
		}
		else
		{
			// 映射：
			// 0 = 自动 (Auto, desktopLookAtEnabled=true)
			// 1 = 使用动作原始视线 (ForceOff / Force VMD)
			// 2 = 注视镜头 (DesktopOn)
			int gazeChoice = 0;
			if (ctx.lookAt)
			{
				const auto curMode = ctx.lookAt->gazeMode.load();
				if (curMode == GazeMode::ForceOff) gazeChoice = 1;
				else if (curMode == GazeMode::DesktopOn) gazeChoice = 2;
				else gazeChoice = 0;
			}

			bool changed = false;
			if (ImGui::RadioButton("自动", &gazeChoice, 0)) changed = true;
			ImGui::SameLine(0.0f, 20.0f);
			if (ImGui::RadioButton("使用动作原始视线", &gazeChoice, 1)) changed = true;
			ImGui::SameLine(0.0f, 20.0f);
			if (ImGui::RadioButton("注视镜头", &gazeChoice, 2)) changed = true;

			if (changed && ctx.lookAt)
			{
				if (gazeChoice == 0)
				{
					ctx.lookAt->gazeMode.store(GazeMode::Auto);
					ctx.lookAt->desktopLookAtEnabled.store(true);
				}
				else if (gazeChoice == 1)
				{
					ctx.lookAt->gazeMode.store(GazeMode::ForceOff);
					ctx.lookAt->desktopLookAtEnabled.store(false);
				}
				else if (gazeChoice == 2)
				{
					ctx.lookAt->gazeMode.store(GazeMode::DesktopOn);
					ctx.lookAt->desktopLookAtEnabled.store(true);
				}
			}
		}

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
		if (ImGui::Button("重置位置偏移", ImVec2(120, 24)))
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
		if (ImGui::GetContentRegionAvail().x > 290.0f) ImGui::SameLine();
		if (ImGui::Button("保存初始位置为默认", ImVec2(150, 24)))
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
