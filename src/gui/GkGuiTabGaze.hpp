#pragma once

#include "../GkGuiOverlay.hpp"
#include "GkGuiThemeColors.hpp"

#ifdef GKMOTION_GUI_ACTIVE
namespace GakuMotion
{
	inline void GuiOverlay::DrawGazeTab(const GuiContext& ctx)
	{
		if (!ctx.lookAt)
		{
			ImGui::TextDisabled("视线管理器尚未就绪");
			return;
		}
		LookAtManager& lookAt = *ctx.lookAt;

		if (lookAt.IsVRifyLiveGazeActive())
		{
			ImGui::TextColored(GuiTheme::SuccessColor(),
			                   "● VR 模式：由 VRify LiveGaze 统一接管视线");
		}
		else
		{
			ImGui::TextColored(GuiTheme::InfoColor(),
			                   "● 桌面模式：GakuMotion 独立对视");
		}
		ImGui::Separator();

		int mode = static_cast<int>(lookAt.gazeMode.load());
		bool changed = false;
		changed |= ImGui::RadioButton("Auto（VR 时自动避让 VRify）", &mode, 0);
		changed |= ImGui::RadioButton("ForceOff（保留纯 VMD 头动）", &mode, 1);
		changed |= ImGui::RadioButton("DesktopOn（强制桌面追焦）", &mode, 2);
		if (changed)
		{
			lookAt.gazeMode.store(static_cast<GazeMode>(mode));
			if (ctx.config) ctx.config->gazeMode = mode;
		}
		if (ctx.fnCycleGaze)
		{
			ImGui::SameLine();
			if (ImGui::Button("循环切换")) ctx.fnCycleGaze(ctx.self);
		}

		ImGui::Separator();
		bool desktop = lookAt.desktopLookAtEnabled.load();
		if (ImGui::Checkbox("开启桌面追焦 (LookAt)", &desktop))
		{
			lookAt.desktopLookAtEnabled.store(desktop);
			if (ctx.config) ctx.config->desktopLookAtEnabled = desktop ? 1 : 0;
		}

		float eye = lookAt.eyeWeight.load();
		ImGui::TextUnformatted("眼神权重 (Eyes)");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(65.0f);
		bool eyeInputChanged = ImGui::InputFloat("##eye_input", &eye, 0.0f, 0.0f, "%.2f");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(160.0f);
		bool eyeSliderChanged = ImGui::SliderFloat("##eye_slider", &eye, 0.0f, 1.0f, "%.2f");
		if (eyeInputChanged || eyeSliderChanged)
		{
			lookAt.eyeWeight.store(eye);
			if (ctx.config) ctx.config->eyeWeight = eye;
		}

		float head = lookAt.headWeight.load();
		ImGui::TextUnformatted("头部追焦 (Head)");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(65.0f);
		bool headInputChanged = ImGui::InputFloat("##head_input", &head, 0.0f, 0.0f, "%.2f");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(160.0f);
		bool headSliderChanged = ImGui::SliderFloat("##head_slider", &head, 0.0f, 1.0f, "%.2f");
		if (headInputChanged || headSliderChanged)
		{
			lookAt.headWeight.store(head);
			if (ctx.config) ctx.config->headWeight = head;
		}

		float body = lookAt.bodyWeight.load();
		ImGui::TextUnformatted("身体追焦 (Body)");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(65.0f);
		bool bodyInputChanged = ImGui::InputFloat("##body_input", &body, 0.0f, 0.0f, "%.2f");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(160.0f);
		bool bodySliderChanged = ImGui::SliderFloat("##body_slider", &body, 0.0f, 1.0f, "%.2f");
		if (bodyInputChanged || bodySliderChanged)
		{
			lookAt.bodyWeight.store(body);
			if (ctx.config) ctx.config->bodyWeight = body;
		}
	}
} // namespace GakuMotion
#endif
