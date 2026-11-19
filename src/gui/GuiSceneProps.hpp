#pragma once
#include "../GkLiveActorPropPolicy.hpp"

namespace GakuMotion
{
	inline void DrawSceneProps(const GuiContext& ctx, bool timelineOnly = false)
	{
		if (!ImGui::CollapsingHeader(timelineOnly ? "时间轴道具" : "场景道具", ImGuiTreeNodeFlags_DefaultOpen)) return;
		if (ctx.fnScanActorProps && ImGui::Button("重新扫描道具")) ctx.fnScanActorProps(ctx.self);
		if (ctx.ensembleNotice.starts_with("该时间轴道具")) ImGui::TextWrapped("%s", ctx.ensembleNotice.c_str());
		if (!ctx.actorPropsEditable) ImGui::TextDisabled("接管后可以调整道具显隐。");
		bool found = false;
		for (const auto& prop : ctx.actorProps)
		{
			if (timelineOnly && !prop.timeline) continue;
			found = true;
			const auto key = ActorPropKey(prop);
			const auto label = prop.actorLabel + " / " + live_actor_props::DisplayName(prop.propName) + "##" + key;
			bool hidden = prop.hidden;
			ImGui::BeginDisabled(!ctx.actorPropsEditable || !prop.controllable || !ctx.fnSetActorPropHidden);
			if (ImGui::Checkbox(label.c_str(), &hidden))
				ctx.fnSetActorPropHidden(ctx.self, prop.actorToken, ctx.actorRosterEpoch, key.c_str(), hidden);
			ImGui::EndDisabled();
			if (!prop.active && !prop.hidden) {
				ImGui::SameLine();
				ImGui::TextDisabled("未启用");
			}
		}
		if (!found) ImGui::TextDisabled("当前没有识别到道具。");
		else if (ctx.actorPropsEditable) ImGui::TextDisabled("勾选可隐藏道具。");
		ImGui::Spacing();
	}
}
