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
		struct Group {
			std::string key, name;
			std::vector<const GuiActorProp*> parts;
		};
		std::vector<Group> groups;
		std::unordered_map<std::string, size_t> groupIndices;
		for (const auto& prop : ctx.actorProps) {
			const bool penlight = prop.propName.find("Penlight") != std::string::npos ||
				prop.propName.find("penlight") != std::string::npos;
			const auto key = penlight ? std::string("penlight") : prop.modelId
				? "model/" + std::to_string(prop.modelId) : ActorPropKey(prop);
			auto [entry, inserted] = groupIndices.try_emplace(key, groups.size());
			if (!inserted) {
				groups[entry->second].parts.push_back(&prop);
				continue;
			}
			auto name = prop.modelId ? prop.modelName : live_actor_props::DisplayName(prop.propName);
			if (name.find("_trolley") != std::string::npos) name = "移动箱";
			else if (name.find("_crane") != std::string::npos) name = "摇臂";
			else if (name.find("_dolly") != std::string::npos) name = "轨道摄影车";
			groups.push_back({key, penlight ? "荧光棒" : name + " · " + prop.actorLabel, {&prop}});
		}
		for (const auto& group : groups) {
			const auto hiddenCount = std::count_if(group.parts.begin(), group.parts.end(),
				[](const auto* part) { return part->hidden; });
			bool hidden = static_cast<size_t>(hiddenCount) == group.parts.size();
			const bool editable = ctx.actorPropsEditable && ctx.fnSetActorPropHidden &&
				std::all_of(group.parts.begin(), group.parts.end(), [](const auto* part) { return part->controllable; });
			ImGui::PushID(group.key.c_str());
			ImGui::BeginDisabled(!editable);
			if (ImGui::Checkbox("##hide_group", &hidden)) {
				for (const auto* part : group.parts)
					if (part->hidden != hidden)
						ctx.fnSetActorPropHidden(ctx.self, part->actorToken, ctx.actorRosterEpoch,
							ActorPropKey(*part).c_str(), hidden);
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("隐藏或恢复整组");
			ImGui::EndDisabled();
			ImGui::SameLine();
			const auto heading = numbered(group.name) + (hiddenCount && !hidden ? "（部分隐藏）" : "");
			const bool expanded = ImGui::TreeNodeEx("model", ImGuiTreeNodeFlags_OpenOnArrow, "%s", heading.c_str());
			if (expanded) for (const auto* part : group.parts)
				draw(*part, part->modelId ? part->propName : live_actor_props::DisplayName(part->propName) + " · " + part->actorLabel);
			if (expanded) ImGui::TreePop();
			ImGui::PopID();
		}
		if (ctx.actorProps.empty()) ImGui::TextDisabled("当前没有识别到道具。");
		else if (ctx.actorPropsEditable) ImGui::TextDisabled("勾选可隐藏道具。");
		ImGui::Spacing();
	}
}
