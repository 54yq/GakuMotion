#pragma once

#include "../GkGuiOverlay.hpp"
#include "../GkStageManager.hpp"
#include "GkGuiThemeColors.hpp"

#ifdef GKMOTION_GUI_ACTIVE
namespace GakuMotion
{
	inline void GuiOverlay::DrawStageTab(const GuiContext& ctx)
	{
		ImGui::Text("当前场景: %s", ctx.stageSceneName.empty() ? "(未进入3D场景/OutGame)" : ctx.stageSceneName.c_str());
		if (!ctx.stageStatusNotice.empty())
		{
			ImGui::TextColored(GuiTheme::InfoColor(), "● 状态: %s", ctx.stageStatusNotice.c_str());
		}
		if (!ctx.stageMountedKey.empty())
		{
			ImGui::TextColored(GuiTheme::SuccessColor(), "● 已挂载舞台: %s", ctx.stageMountedKey.c_str());
		}
		ImGui::Separator();

		// ---- 1. 预设快速切换 ----
		ImGui::TextUnformatted("【舞台与幕布预设】");
		int currentPreset = ctx.stageSettings.selectedPresetIndex;
		const char* previewText = (currentPreset >= 0 && currentPreset < kStagePresetCount)
			? kDefaultStagePresets[currentPreset].displayName : "选择预设...";

		if (ImGui::BeginCombo("预设方案##stage_preset", previewText))
		{
			for (int i = 0; i < kStagePresetCount; ++i)
			{
				const bool isSelected = (currentPreset == i);
				if (ImGui::Selectable(kDefaultStagePresets[i].displayName, isSelected))
				{
					if (ctx.fnSetStagePreset) ctx.fnSetStagePreset(ctx.self, i);
				}
				if (isSelected) ImGui::SetItemDefaultFocus();
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", kDefaultStagePresets[i].description);
				}
			}
			ImGui::EndCombo();
		}

		if (currentPreset >= 0 && currentPreset < kStagePresetCount)
		{
			ImGui::TextDisabled("说明: %s", kDefaultStagePresets[currentPreset].description);
		}

		ImGui::Spacing();
		ImGui::Separator();

		// ---- 2. 背景隐藏与节点探测 ----
		ImGui::TextUnformatted("【背景显示与节点管理】");
		StageSettings settings = ctx.stageSettings;
		bool settingsChanged = false;

		if (ImGui::Checkbox("隐藏原生场景背景##hide_bg", &settings.hideBackground))
		{
			settingsChanged = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("扫描/刷新场景节点##scan_nodes"))
		{
			if (ctx.fnScanStageCandidates) ctx.fnScanStageCandidates(ctx.self);
		}

		if (!ctx.stageCandidates.empty())
		{
			char treeLabel[64];
			snprintf(treeLabel, sizeof(treeLabel), "已探测场景节点 (%zu 个)###stage_nodes", ctx.stageCandidates.size());
			if (ImGui::CollapsingHeader(treeLabel))
			{
				ImGui::Indent(10.0f);
				for (size_t i = 0; i < ctx.stageCandidates.size(); ++i)
				{
					const auto& c = ctx.stageCandidates[i];
					bool active = c.currentlyActive;
					char itemLabel[256];
					snprintf(itemLabel, sizeof(itemLabel), "%s %s##cand_%zu",
						c.isColliderLocked ? "[碰撞 (锁定)]" : (c.isIdentifiedBackground ? "[背景]" : "[节点]"),
						c.name.c_str(), i);

					if (c.isColliderLocked)
					{
						ImGui::BeginDisabled(true);
						ImGui::Checkbox(itemLabel, &active);
						ImGui::EndDisabled();
						if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
						{
							ImGui::SetTooltip("地面与摄像机活动范围碰撞体，系统锁定保持启用以防止摄像机掉出世界");
						}
					}
					else
					{
						if (ImGui::Checkbox(itemLabel, &active))
						{
							if (ctx.fnToggleStageCandidate)
								ctx.fnToggleStageCandidate(ctx.self, static_cast<int>(i), active);
						}
					}
				}
				ImGui::Unindent(10.0f);
			}
		}
		else
		{
			ImGui::TextDisabled("暂未捕获场景节点（需在 3D 角色加载后自动识别）");
		}

		ImGui::Spacing();
		ImGui::Separator();

		// ---- 3. 纯色绿幕控制 ----
		ImGui::TextUnformatted("【纯色绿幕 (Chroma Key & Backdrop)】");
		if (ImGui::Checkbox("启用纯色绿幕##enable_chroma", &settings.greenScreenActive))
		{
			settingsChanged = true;
			if (settings.greenScreenActive)
			{
				settings.hideBackground = true;
				settings.overrideCameraClear = true;
			}
			else
			{
				settings.overrideCameraClear = false;
			}
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(!settings.greenScreenActive);
		if (ImGui::Checkbox("相机底色清除##cam_clear", &settings.overrideCameraClear))
		{
			settingsChanged = true;
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		if (ImGui::Button("恢复原生相机底色##restore_sky"))
		{
			if (ctx.fnRestoreCameraSkybox) ctx.fnRestoreCameraSkybox(ctx.self);
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("恢复 GM 修改前保存的相机清除模式与背景色；没有原生快照时不写入默认值");
		}

		ImGui::BeginDisabled(!settings.greenScreenActive && !settings.overrideCameraClear);
		if (ImGui::ColorEdit4("幕布底色##chroma_color", settings.greenScreenColor, ImGuiColorEditFlags_Float))
		{
			settingsChanged = true;
		}

		// 快速色彩预设
		ImGui::TextUnformatted("色板预设:");
		ImGui::SameLine();
		if (ImGui::Button("绿幕##btn_green"))
		{
			settings.greenScreenColor[0] = 0.0f; settings.greenScreenColor[1] = 1.0f;
			settings.greenScreenColor[2] = 0.0f; settings.greenScreenColor[3] = 1.0f;
			settingsChanged = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("蓝幕##btn_blue"))
		{
			settings.greenScreenColor[0] = 0.0f; settings.greenScreenColor[1] = 0.0f;
			settings.greenScreenColor[2] = 1.0f; settings.greenScreenColor[3] = 1.0f;
			settingsChanged = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("纯黑##btn_black"))
		{
			settings.greenScreenColor[0] = 0.0f; settings.greenScreenColor[1] = 0.0f;
			settings.greenScreenColor[2] = 0.0f; settings.greenScreenColor[3] = 1.0f;
			settingsChanged = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("纯白##btn_white"))
		{
			settings.greenScreenColor[0] = 1.0f; settings.greenScreenColor[1] = 1.0f;
			settings.greenScreenColor[2] = 1.0f; settings.greenScreenColor[3] = 1.0f;
			settingsChanged = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("摄影灰##btn_gray"))
		{
			settings.greenScreenColor[0] = 0.18f; settings.greenScreenColor[1] = 0.18f;
			settings.greenScreenColor[2] = 0.18f; settings.greenScreenColor[3] = 1.0f;
			settingsChanged = true;
		}

		if (ImGui::SliderFloat("幕布距离 (m)##screen_dist", &settings.screenDistance, 0.5f, 20.0f, "%.1f m"))
		{
			settingsChanged = true;
		}
		if (ImGui::SliderFloat("幕布尺寸 (m)##screen_scale", &settings.screenScale, 5.0f, 100.0f, "%.0f m"))
		{
			settingsChanged = true;
		}
		if (ImGui::SliderFloat("垂直高度 (m)##screen_h", &settings.screenHeightOffset, -5.0f, 5.0f, "%.2f m"))
		{
			settingsChanged = true;
		}
		ImGui::EndDisabled();

		ImGui::Spacing();
		ImGui::Separator();

		// ---- 4. 官方舞台挂载与地基微调 ----
		ImGui::TextUnformatted("【官方舞台挂载与地基对齐】");
		static char customKeyBuf[128] = "env_3d_live_all001-00-noon";
		ImGui::SetNextItemWidth(260.0f);
		ImGui::InputText("舞台资产Key##custom_stage_key", customKeyBuf, sizeof(customKeyBuf));
		ImGui::SameLine();
		if (ImGui::Button("挂载舞台##btn_mount_stage"))
		{
			if (ctx.fnMountCustomStage) ctx.fnMountCustomStage(ctx.self, customKeyBuf);
		}
		ImGui::SameLine();
		if (ImGui::Button("卸载恢复原生##btn_unload_stage"))
		{
			if (ctx.fnUnloadCustomStage) ctx.fnUnloadCustomStage(ctx.self);
		}

		bool hasMountedStage = !ctx.stageMountedKey.empty() || settings.selectedPresetIndex >= 6;
		if (hasMountedStage)
		{
			ImGui::TextDisabled("舞台地基与朝向微调 (实时对齐偶像脚底基准):");
			if (ImGui::SliderFloat("地基垂直高度 (m)##stage_y", &settings.stageFloorYOffset, -2.0f, 2.0f, "%.2f m"))
			{
				settingsChanged = true;
			}
			if (ImGui::SliderFloat("舞台水平朝向 (°)##stage_yaw", &settings.stageYawOffset, -180.0f, 180.0f, "%.1f deg"))
			{
				settingsChanged = true;
			}
			if (ImGui::SliderFloat("舞台水平X偏移 (m)##stage_x", &settings.stagePosXOffset, -10.0f, 10.0f, "%.2f m"))
			{
				settingsChanged = true;
			}
			if (ImGui::SliderFloat("舞台水平Z偏移 (m)##stage_z", &settings.stagePosZOffset, -10.0f, 10.0f, "%.2f m"))
			{
				settingsChanged = true;
			}
			if (ImGui::Button("重置舞台位姿##reset_stage_pose"))
			{
				settings.stageFloorYOffset = 0.0f;
				settings.stageYawOffset = 0.0f;
				settings.stagePosXOffset = 0.0f;
				settings.stagePosZOffset = 0.0f;
				settingsChanged = true;
			}
		}

		if (settingsChanged && ctx.fnSetStageSettings)
		{
			ctx.fnSetStageSettings(ctx.self, settings);
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::TextDisabled("说明：官方 Live 舞台及场景直接通过游戏内部管线解密挂载，环境光感与模型贴图完整保留。");
	}
} // namespace GakuMotion
#endif
