#pragma once

#include "../GkGuiOverlay.hpp"

#ifdef GKMOTION_GUI_ACTIVE
namespace GakuMotion
{
	// Developer-only selection for a future native Photography load. It may
	// be edited with developer mode enabled while general injection is denied.
	inline void DrawStageLoadReplacementControl(const GuiContext& ctx)
	{
		ImGui::PushID("stage_load_replacement");
		ImGui::TextUnformatted("摄影舞台替换（实验，载入时生效）");
		ImGui::TextWrapped("中庭舞台已获用户实测正常反馈；通用 Live 与初星舞台的完整灯光效果尚未完成。");
		static constexpr const char* keys[] = {
			"", "env_3d_live_all001-00-noon", "env_3d_prod_hifstage-00-00-noon",
			"env_3d_live_courtyard-00-00-noon"
		};
		static constexpr const char* names[] = {
			"原生摄影背景", "通用 Live 舞台（灯光未完成）", "初星舞台（灯光未完成）", "中庭舞台（日中）"
		};
		int selectedIndex = 0;
		for (int i = 1; i < 4; ++i)
			if (ctx.stageSettings.loadReplacementKey == keys[i]) selectedIndex = i;
		ImGui::BeginDisabled(!ctx.fnSetStageLoadReplacement);
		if (ImGui::BeginCombo("下次摄影背景", names[selectedIndex]))
		{
			for (int i = 0; i < 4; ++i)
			{
				const bool selected = selectedIndex == i;
				if (ImGui::Selectable(names[i], selected) && ctx.fnSetStageLoadReplacement)
					ctx.fnSetStageLoadReplacement(ctx.self, keys[i]);
				if (selected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		ImGui::EndDisabled();
		ImGui::TextWrapped("选择会自动保存，离开当前页面后保留，下次进入偶像摄影时生效。已在摄影中时，可使用游戏原生功能切换背景触发载入。");
		ImGui::TextWrapped("恢复原生：选择“原生摄影背景”，然后重新进入摄影或原生切换背景。当前场景不会立即改变。");
		if (!ctx.stageLoadNotice.empty())
			ImGui::TextWrapped("%s", ctx.stageLoadNotice.c_str());
		ImGui::PopID();
	}
}
#endif
