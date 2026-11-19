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
		std::unordered_map<std::string, unsigned> labelCounts;
		const auto numbered = [&](const std::string& display) {
			const auto ordinal = ++labelCounts[display];
			return display + (ordinal > 1 ? " #" + std::to_string(ordinal) : "");
		};
		const auto draw = [&](const GuiActorProp& prop, const std::string& name) {
			const auto key = ActorPropKey(prop);
			const auto label = name + "##" + key;
			bool hidden = prop.hidden;
			ImGui::BeginDisabled(!ctx.actorPropsEditable || !prop.controllable || !ctx.fnSetActorPropHidden);
			if (ImGui::Checkbox(label.c_str(), &hidden))
				ctx.fnSetActorPropHidden(ctx.self, prop.actorToken, ctx.actorRosterEpoch, key.c_str(), hidden);
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("%s", prop.propName.c_str());
			ImGui::EndDisabled();
			if (!prop.active && !prop.hidden) {
				ImGui::SameLine();
				ImGui::TextDisabled("未启用");
			}
		};
		for (size_t i = 0; i < ctx.actorProps.size();) {
			const auto& prop = ctx.actorProps[i];
			if (!prop.modelId) {
				draw(prop, numbered(live_actor_props::DisplayName(prop.propName) + " · " + prop.actorLabel));
				++i;
				continue;
			}
			auto name = prop.modelName;
			if (name.find("_trolley") != std::string::npos) name = "移动箱";
			else if (name.find("_crane") != std::string::npos) name = "摇臂";
			else if (name.find("_dolly") != std::string::npos) name = "轨道摄影车";
			const auto heading = numbered(name + " · " + prop.actorLabel);
			ImGui::PushID(prop.modelId);
			const bool expanded = ImGui::TreeNodeEx("model", ImGuiTreeNodeFlags_OpenOnArrow, "%s", heading.c_str());
			do {
				const auto& part = ctx.actorProps[i++];
				if (expanded) draw(part, part.propName);
			} while (i < ctx.actorProps.size() && ctx.actorProps[i].modelId == prop.modelId);
			if (expanded) ImGui::TreePop();
			ImGui::PopID();
		}
		if (ctx.actorProps.empty()) ImGui::TextDisabled("当前没有识别到道具。");
		else if (ctx.actorPropsEditable) ImGui::TextDisabled("勾选可隐藏道具。");
		ImGui::Spacing();
	}
}
