#pragma once

#include "../GkGuiOverlay.hpp"
#include "../GkExperimentalSolePitch.hpp"
#include "GkGuiThemeColors.hpp"
#include <algorithm>

#ifdef GKMOTION_GUI_ACTIVE
namespace GakuMotion
{
	inline void GuiOverlay::DrawExperimentalSolePitchTab(const GuiContext& ctx)
	{
		ImGui::TextColored(GuiTheme::WarningColor(), "Experimental penetration-correction Demo");
		ImGui::TextWrapped("仅用于咲季蓝色培育靴：会在原动作后旋转 Foot 的鞋侧轴 pitch。可能出现踮脚、动作突变或仍然穿地；不是生产修正。鞋底测量 v2 页保留 #078 原动作读数。");
		ImGui::TextWrapped("先在鞋底测量 v2 页启用培育靴代理、核对并应用水平地面，再播放 / 暂停 MMD 动作，打开下面的 Demo 开关。");
		auto experiment = ctx.experimentalSolePitchSettings;
		bool experimentChanged = ImGui::Checkbox("experimentalSolePitchCorrection [DEMO]",
		                                         &experiment.experimentalSolePitchCorrection);
		experimentChanged = ImGui::Checkbox("使用实测蓝靴鞋底包络", &experiment.useMeshEnvelope) || experimentChanged;
		ImGui::TextUnformatted("Demo 最大 pitch");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(65.0f);
		experimentChanged = ImGui::InputFloat("##demo_pitch_input", &experiment.maxPitchDegrees, 0.0f, 0.0f, "%.1f")
			|| experimentChanged;
		ImGui::SameLine();
		ImGui::SetNextItemWidth(160.0f);
		experimentChanged = ImGui::SliderFloat("##demo_pitch_slider", &experiment.maxPitchDegrees, 0.f, 35.f,
		                                       "%.1f deg") || experimentChanged;
		if (experimentChanged && ctx.fnSetExperimentalSolePitch) ctx.fnSetExperimentalSolePitch(
			ctx.self, experiment);
		ImGui::TextDisabled("默认关闭；每次启动、换角色或换场景后需重新开启。只旋转 Foot，无法靠旋转解决所有穿入。");
		if (!ctx.soleSettings.enabled || !ctx.soleSettings.useBootProxy)
			ImGui::TextWrapped("Demo 条件：先启用培育靴粗略代理。");
		if (!ctx.soleSettings.planeCalibrated)
			ImGui::TextWrapped("Demo 条件：应用已校准的水平地面。未校准地面不会触发修正。");
		ImGui::Text("当前地面 Y %.4f m | 原动作时间 %.3f s", ctx.soleSettings.planeOffsetM,
		            ctx.experimentalSolePitchSnapshot.clipTime);
		ImGui::Separator();
		if (ctx.experimentalSolePitchSettings.experimentalSolePitchCorrection)
			for (int side = 0; side < 2; ++side)
			{
				const auto& trace = ctx.experimentalSolePitchSnapshot.feet[side];
				ImGui::Text("%s Foot: %s", side == 0 ? "左" : "右", trace.status);
				if (trace.solution.valid)
				{
					ImGui::Text("pitch %+.2f deg | %s穿入 %.2f -> %.2f mm", trace.solution.pitchDegrees,
					            trace.solution.usedMeshEnvelope ? "实测包络" : "矩形代理",
					            trace.solution.beforePenetrationM * 1000,
					            (trace.measuredAfterValid
						             ? (std::max)(0.f, -trace.measuredAfterMinimumDistanceM)
						             : trace.solution.beforePenetrationM) * 1000);
					if (trace.solution.usedMeshEnvelope)
						ImGui::TextDisabled("包络来自固定鞋型采样；本帧实际 BakeMesh 对照由自动化单独检查。");
					if (trace.applied) ImGui::TextDisabled(
						trace.setterReadbackMatches ? "Foot setter 已读回确认" : "Foot setter 读回未确认");
				}
			}
	}
} // namespace GakuMotion
#endif
