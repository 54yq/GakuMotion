#pragma once

#include "../GkGuiOverlay.hpp"
#include "../GkSoleMeasurement.hpp"
#include "GkGuiThemeColors.hpp"
#include <cmath>
#include <algorithm>

#ifdef GKMOTION_GUI_ACTIVE
namespace GakuMotion
{
	inline void GuiOverlay::DrawSolePlot(const GuiContext& ctx, bool sideView, float width)
	{
		const bool isLight = GuiTheme::IsLight();
		const ImVec2 start = ImGui::GetCursorScreenPos(), size(width, 280);
		ImGui::InvisibleButton(sideView ? "sole_side" : "sole_front", size);
		auto* draw = ImGui::GetWindowDrawList();

		const ImU32 canvasBg = isLight ? IM_COL32(245, 245, 242, 255) : IM_COL32(25, 28, 32, 255);
		const ImU32 canvasBorder = isLight ? IM_COL32(195, 195, 190, 255) : IM_COL32(50, 55, 65, 255);
		const ImU32 titleColor = isLight ? IM_COL32(40, 40, 40, 255) : IM_COL32(230, 230, 230, 255);
		const ImU32 floorColor = isLight ? IM_COL32(110, 115, 120, 255) : IM_COL32(180, 180, 180, 255);

		draw->AddRectFilled(start, ImVec2(start.x + size.x, start.y + size.y), canvasBg);
		draw->AddRect(start, ImVec2(start.x + size.x, start.y + size.y), canvasBorder);
		draw->PushClipRect(start, ImVec2(start.x + size.x, start.y + size.y), true);
		const auto& settings = ctx.soleSettings;
		const Vector3 normal = settings.planeNormal;
		Vector3 tangent = Vector3(1, 0, 0) - sole::Scale(normal, normal.x);
		const float length = std::sqrt(sole::Dot(tangent, tangent));
		if (!settings.IsValid() || length < 1e-5f)
		{
			draw->PopClipRect();
			return;
		}
		tangent = sole::Scale(tangent, 1.f / length);
		if (sideView)
			tangent = {
				normal.y * tangent.z - normal.z * tangent.y,
				normal.z * tangent.x - normal.x * tangent.z,
				normal.x * tangent.y - normal.y * tangent.x
			};
		Vector3 center;
		if (ctx.soleSnapshot.available)
			center = sole::Scale(ctx.soleSnapshot.feet[0].pose.ankle + ctx.soleSnapshot.feet[1].pose.ankle, .5f);
		const float pixels = (std::min)(size.x - 20, size.y - 30) / solePlotSpanM;
		const float floorY = start.y + size.y - 35;
		auto project = [&](const Vector3& p)
		{
			return ImVec2(start.x + size.x * .5f + sole::Dot(p - center, tangent) * pixels,
			              floorY - (sole::Dot(p, normal) - settings.planeOffsetM) * pixels);
		};
		draw->AddText(ImVec2(start.x + 8, start.y + 6), titleColor, sideView ? "平面侧视" : "平面正视");
		draw->AddLine(ImVec2(start.x, floorY), ImVec2(start.x + size.x, floorY), floorColor, 2);
		draw->AddText(ImVec2(start.x + 5, floorY + 5), floorColor,
		              settings.planeCalibrated ? "地面参考" : "地面参考（待校准）");
		for (int side = 0; side < 2; ++side)
		{
			const auto& foot = ctx.soleSnapshot.feet[side];
			if (!foot.valid) continue;
			const auto skeleton = side == 0
				? (isLight ? IM_COL32(30, 120, 220, 255) : IM_COL32(90, 180, 255, 255))
				: (isLight ? IM_COL32(210, 110, 30, 255) : IM_COL32(240, 170, 90, 255));
			draw->AddLine(project(foot.pose.hip), project(foot.pose.knee), skeleton, 1);
			draw->AddLine(project(foot.pose.knee), project(foot.pose.ankle), skeleton, 1);
			draw->AddLine(project(foot.pose.ankle), project(foot.pose.toe), skeleton, 1);
			draw->AddCircleFilled(project(foot.pose.ankle), 3, skeleton);
			const Vector3 axes[] = {{.06f, 0, 0}, {0, .06f, 0}, {0, 0, .06f}};
			const ImU32 colors[] = {
				isLight ? IM_COL32(210, 30, 30, 255) : IM_COL32(255, 90, 90, 255),
				isLight ? IM_COL32(20, 160, 40, 255) : IM_COL32(90, 255, 90, 255),
				isLight ? IM_COL32(30, 80, 210, 255) : IM_COL32(90, 120, 255, 255)
			};
			for (int i = 0; i < 3; ++i)
				draw->AddLine(project(foot.pose.ankle),
				              project(foot.pose.ankle + foot.pose.footRotation.Normalized().Rotate(axes[i])),
				              colors[i], 2);
			if (foot.usingBootProxy)
			{
				for (int patchIndex = 0; patchIndex < 2; ++patchIndex)
				{
					const auto& patch = foot.patches[patchIndex];
					ImU32 color = skeleton;
					if (settings.planeCalibrated)
						color = patch.minimumDistanceM < -settings.displayBandM
							        ? (isLight ? IM_COL32(210, 30, 30, 255) : IM_COL32(255, 80, 80, 255))
							        : patch.minimumDistanceM <= settings.displayBandM
							        ? (isLight ? IM_COL32(190, 130, 0, 255) : IM_COL32(255, 210, 70, 255))
							        : skeleton;
					ImVec2 corners[4];
					for (int i = 0; i < 4; ++i) corners[i] = project(patch.corners[i].world);
					float twiceArea = 0;
					for (int i = 0; i < 4; ++i) twiceArea += corners[i].x * corners[(i + 1) % 4].y - corners[(i + 1)
						% 4].x * corners[i].y;
					if (twiceArea < 0) std::swap(corners[1], corners[3]);
					// ImGui fill expects clockwise screen winding.
					if (std::fabs(twiceArea) > 1.f)
						draw->AddConvexPolyFilled(corners, 4, (color & 0x00ffffffU) | (isLight ? 0x24000000U : 0x30000000U));
					for (int i = 0; i < 4; ++i) draw->AddLine(corners[i], corners[(i + 1) % 4], color, 2);
				}
			}
			for (int i = 0; i < 3; ++i)
			{
				const auto& point = foot.points[i];
				ImU32 color = isLight ? IM_COL32(110, 110, 110, 255) : IM_COL32(150, 150, 150, 255);
				if (ctx.soleSnapshot.calibrated)
					color = point.signedDistanceM < -settings.displayBandM
						        ? (isLight ? IM_COL32(210, 30, 30, 255) : IM_COL32(255, 80, 80, 255))
						        : point.signedDistanceM <= settings.displayBandM
						        ? (isLight ? IM_COL32(190, 130, 0, 255) : IM_COL32(255, 210, 70, 255))
						        : (isLight ? IM_COL32(30, 150, 80, 255) : IM_COL32(90, 220, 150, 255));
				const ImVec2 p = project(point.world);
				draw->AddCircleFilled(p, 4, color);
				char label[8];
				std::snprintf(label, sizeof(label), "%c%d", side == 0 ? 'L' : 'R', i + 1);
				draw->AddText(ImVec2(p.x + 5, p.y - 8), color, label);
			}
		}
		draw->PopClipRect();
	}

	inline void GuiOverlay::DrawSoleTab(const GuiContext& ctx)
	{
		if (!soleDraftReady)
		{
			soleDraft = ctx.soleSettings;
			soleDraftReady = true;
		}
		if (soleDraftGeneration != ctx.soleCalibrationGeneration)
		{
			soleDraftGeneration = ctx.soleCalibrationGeneration;
			soleDraft.planeCalibrated = false;
			soleDraft.shoeCalibrated = false;
		}
		ImGui::TextWrapped("v2 仅测量和可视化：不改姿态，在培育 / MMD 场景使用。培育靴粗略代理来自咲季蓝色靴子的网格证据；换装后尺寸仍按这份档案计算，可用于观察适用边界。");
		if (ImGui::Button("启用培育靴粗略代理") && ctx.fnSetSoleSettings)
		{
			soleDraft.enabled = true;
			soleDraft.useBootProxy = true;
			soleDraft.shoeCalibrated = false;
			ctx.fnSetSoleSettings(ctx.self, soleDraft);
		}
		ImGui::SameLine();
		if (ImGui::Button("切回手工点 fallback") && ctx.fnSetSoleSettings)
		{
			soleDraft.useBootProxy = false;
			soleDraft.shoeCalibrated = false;
			ctx.fnSetSoleSettings(ctx.self, soleDraft);
		}
		ImGui::Checkbox("开启鞋底测量（只读）", &soleDraft.enabled);
		if (soleDraft.useBootProxy)
			ImGui::TextWrapped("粗略模型：后跟 / 前掌两个面片随 Foot 整体旋转。矩形边角可能高估穿入；趾部弯曲、换装尺寸差异需人工判读。距离是代理估计，不能作为真实鞋底认证。");
		else
			ImGui::TextWrapped("手工 fallback：鞋跟 / 前掌 / 脚尖三点。保留已有局部坐标，不作为主要校准流程。");
		ImGui::DragFloat3("平面法线", &soleDraft.planeNormal.x, .001f, -1, 1, "%.4f");
		if (ImGui::Button("归一化法线"))
		{
			const float n = sole::Dot(soleDraft.planeNormal, soleDraft.planeNormal);
			if (std::isfinite(n) && n > 1e-8f) soleDraft.planeNormal = sole::Scale(
				soleDraft.planeNormal, 1.f / std::sqrt(n));
		}
		ImGui::SameLine();
		if (ImGui::Button("水平世界平面")) soleDraft.planeNormal = {0, 1, 0};
		ImGui::DragFloat("平面距离（水平时为世界 Y）", &soleDraft.planeOffsetM, .001f, -10000, 10000, "%.4f m");
		ImGui::Checkbox("已校准本场景地面", &soleDraft.planeCalibrated);
		const char* names[] = {"鞋跟", "前掌", "脚尖"};
		for (int side = 0; side < 2; ++side)
		{
			if (!soleDraft.useBootProxy && ImGui::CollapsingHeader(side == 0 ? "左靴局部代理点（米）" : "右靴局部代理点（米）"))
			{
				ImGui::PushID(side);
				for (int i = 0; i < 3; ++i) ImGui::DragFloat3(names[i], &soleDraft.points[side][i].x, .001f, -.5f,
				                                              .5f, "%.4f");
				ImGui::PopID();
			}
		}
		ImGui::Checkbox(soleDraft.useBootProxy ? "已核对当前鞋型与粗略代理范围" : "已核对本角色、服装的六个鞋底点", &soleDraft.shoeCalibrated);
		ImGui::DragFloat("显示容差（不修改原始距离）", &soleDraft.displayBandM, .0001f, 0, .05f, "%.4f m");
		if (ImGui::Button("应用测量设置") && ctx.fnSetSoleSettings) ctx.fnSetSoleSettings(ctx.self, soleDraft);
		ImGui::SameLine();
		if (ImGui::Button("读取已应用值")) soleDraft = ctx.soleSettings;
		ImGui::SameLine();
		if (ImGui::Button("保存") && ctx.fnSaveSoleSettings) ctx.fnSaveSoleSettings(ctx.self);
		ImGui::SameLine();
		if (ImGui::Button("载入") && ctx.fnLoadSoleSettings) ctx.fnLoadSoleSettings(ctx.self);
		if (!(soleDraft == ctx.soleSettings)) ImGui::TextDisabled("草案与已应用值不同；先应用，再保存。绘图使用已应用值。");
		if (!ctx.soleNotice.empty()) ImGui::TextWrapped("%s", ctx.soleNotice.c_str());
		ImGui::Separator();
		if (!ctx.soleSettings.enabled) ImGui::TextDisabled("测量关闭");
		else if (!ctx.soleSnapshot.available) ImGui::TextDisabled("等待目标角色的有效双脚骨架（不沿用上一帧数据）");
		else
		{
			ImGui::Text("Actor=%p | 动作时间 %.3f s | %s", ctx.soleSnapshot.actor, ctx.soleSnapshot.clipTime,
			            ctx.soleSnapshot.calibrated ? "已校准代理测量" : "草案距离，不能认定实际穿地");
			for (int side = 0; side < 2; ++side)
			{
				const auto& foot = ctx.soleSnapshot.feet[side];
				ImGui::Text("%s: 最低距离 %+.2f mm", side == 0 ? "左靴" : "右靴", foot.minimumDistanceM * 1000);
				if (ctx.soleSnapshot.calibrated)
				{
					ImGui::SameLine();
					ImGui::Text(foot.usingBootProxy ? "估计代理穿入 %.2f mm" : "最大代理穿入 %.2f mm",
					            foot.maximumPenetrationM * 1000);
				}
				if (foot.usingBootProxy)
				{
					for (int i = 0; i < 2; ++i)
					{
						const auto& patch = foot.patches[i];
						ImGui::Text("%s面片: 最低 %+.2f mm / 最高 %+.2f mm", i == 0 ? "后跟" : "前掌",
						            patch.minimumDistanceM * 1000, patch.maximumDistanceM * 1000);
						if (ctx.soleSettings.planeCalibrated)
						{
							ImGui::SameLine();
							ImGui::Text("估计穿入 %.2f mm", patch.maximumPenetrationM * 1000);
						}
					}
				}
				for (int i = 0; i < 3; ++i)
				{
					if (i) ImGui::SameLine();
					ImGui::Text("%s %+.2f mm", names[i], foot.points[i].signedDistanceM * 1000);
				}
			}
		}
		ImGui::TextUnformatted("视图跨度");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(65.0f);
		ImGui::InputFloat("##sole_span_input", &solePlotSpanM, 0.0f, 0.0f, "%.2f");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(160.0f);
		ImGui::SliderFloat("##sole_span_slider", &solePlotSpanM, 0.25f, 2.0f, "%.2f m");
		const float width = (std::max)(100.f, (ImGui::GetContentRegionAvail().x - 8) * .5f);
		DrawSolePlot(ctx, false, width);
		ImGui::SameLine();
		DrawSolePlot(ctx, true, width);
		ImGui::TextDisabled("坐标轴：X 红 / Y 绿 / Z 蓝。面片：后跟 / 前掌；中心点：1 鞋跟、2 前掌、3 脚尖。黄色仅表示近地，不表示支撑。");
	}
} // namespace GakuMotion
#endif
