#pragma once
#include "../GkLiveActorPropPolicy.hpp"

namespace GakuMotion
{
	inline void DrawTimelineProps(const GuiContext& ctx)
	{
		if (!ImGui::CollapsingHeader("时间轴道具", ImGuiTreeNodeFlags_DefaultOpen)) return;
		if (ctx.fnScanActorProps && ImGui::Button("重新扫描道具")) ctx.fnScanActorProps(ctx.self);
		if (ctx.ensembleNotice.starts_with("该时间轴道具")) ImGui::TextWrapped("%s", ctx.ensembleNotice.c_str());
		if (!ctx.actorPropsEditable) ImGui::TextDisabled("接管后可以调整道具显隐。");
		for (const auto& prop : ctx.actorProps)
		{
			const auto key = ActorPropKey(prop);
			const auto name = prop.timeline && prop.propName.find("_trolley") != std::string::npos
				? "移动箱（" + prop.propName + "）" : live_actor_props::DisplayName(prop.propName);
			const auto label = prop.actorLabel + " / " + name + "##" + key;
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
		if (ctx.actorProps.empty()) ImGui::TextDisabled("当前没有识别到道具。");
		else if (ctx.actorPropsEditable) ImGui::TextDisabled("勾选可隐藏道具。");
		ImGui::Spacing();
	}
}
