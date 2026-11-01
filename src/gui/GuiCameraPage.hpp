#pragma once

// =============================================================================
// GuiCameraPage.hpp — 镜头控制页面（模式选择 / 基础 Transform / 跟随参数 / 高级）
// =============================================================================

#include "GuiOverlayCommon.hpp"
#include "../GkGuiOverlay.hpp"
#include "../GkCamera.hpp"

#ifdef GKMOTION_GUI_ACTIVE
namespace GakuMotion
{
	inline void DrawCameraPage(GuiOverlay& overlay, const GuiContext& ctx)
	{
		// 同步草稿状态（保持原有 Unity 线程确认防回弹逻辑）
		if (!overlay.cameraDraftReady)
		{
			overlay.cameraDraft = overlay.cameraLastSnapshot = ctx.cameraSnapshot.settings;
			overlay.cameraDraftReady = true;
		}
		else if (overlay.cameraSettingsPending)
		{
			if (ctx.cameraSnapshot.settings == overlay.cameraExpected)
			{
				overlay.cameraSettingsPending = false;
				overlay.cameraLastSnapshot = ctx.cameraSnapshot.settings;
			}
		}
		else if (!(ctx.cameraSnapshot.settings == overlay.cameraLastSnapshot))
		{
			overlay.cameraDraft = overlay.cameraLastSnapshot = ctx.cameraSnapshot.settings;
		}

		auto& settings = overlay.cameraDraft;
		bool changed = false;

		// 确保 settings.usePerModeTransforms 初始同步（未编辑时展示对应 legacy 偏移）
		if (!settings.usePerModeTransforms)
		{
			settings.transformTrack.position = settings.vmdOffset;
			settings.transformFollowMove.position = settings.followOffset;
			settings.transformLookAt.position = settings.followOffset;
		}

		auto upgradePerMode = [&]()
		{
			if (!settings.usePerModeTransforms)
			{
				settings.transformTrack.position = settings.vmdOffset;
				settings.transformFollowMove.position = settings.followOffset;
				settings.transformLookAt.position = settings.followOffset;
				settings.usePerModeTransforms = true;
			}
		};

		// =========================================================================
		// 1. 镜头模式选择 (Camera Mode)
		// =========================================================================
		if (ImGui::Checkbox("启用 GakuMotion 镜头接管", &settings.enabled)) changed = true;
		ImGui::TextWrapped("镜头接管: %s | 运镜轨道: %s",
			ctx.cameraSnapshot.owned ? "已生效" : "未接管",
			ctx.cameraSnapshot.hasTrack ? "已加载" : "无");
		if (!ctx.cameraSnapshot.available)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, GuiTheme::WarningColor());
			ImGui::TextWrapped("当前镜头接口未就绪。");
			ImGui::PopStyleColor();
		}
		if (!ctx.cameraSnapshot.error.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, GuiTheme::ErrorColor());
			ImGui::TextWrapped("镜头轨道错误: %s", ctx.cameraSnapshot.error.c_str());
			ImGui::PopStyleColor();
		}
		if (overlay.cameraSettingsPending) ImGui::TextDisabled("正在应用镜头设置…");
		ImGui::TextWrapped("停用接管可恢复游戏手动镜头；镜头模式和参数会保留。");
		ImGui::Spacing();
		ImGui::TextUnformatted("镜头模式");

		// 对应用户感知的模式：
		// 0: VMD 镜头 (VMD 运镜轨道 Track)
		// 1: 跟随角色 (FollowMove: 镜头与注视点协同移动)
		// 2: 注视角色 (LookAt: 镜头固定位置，视线追随角色)
		// 3: 固定接管时镜头 (Hold)
		int userMode = 0;
		if (settings.mode == CameraMode::Follow)
		{
			if (settings.followBehavior == FollowBehavior::LookAt)
				userMode = 2;
			else
				userMode = 1;
		}
		else if (settings.mode == CameraMode::Hold)
		{
			userMode = 3;
		}

		static const char* modeOptions[] = {"VMD 镜头", "跟随角色", "注视角色", "固定接管时镜头"};
		ImGui::SetNextItemWidth((std::min)(260.0f, ImGui::GetContentRegionAvail().x));
		if (ImGui::Combo("##camera_mode", &userMode, modeOptions, IM_ARRAYSIZE(modeOptions)))
		{
			upgradePerMode();
			settings.mode = userMode == 0 ? CameraMode::Track : userMode == 3 ? CameraMode::Hold : CameraMode::Follow;
			settings.followActor = userMode == 1 || userMode == 2;
			if (userMode == 1) settings.followBehavior = FollowBehavior::FollowMove;
			if (userMode == 2) settings.followBehavior = FollowBehavior::LookAt;
			changed = true;
		}
		if (userMode == 0 && !ctx.cameraSnapshot.hasTrack)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, GuiTheme::WarningColor());
			ImGui::TextWrapped("当前曲目没有可用运镜轨道，VMD 模式将保持固定机位。");
			ImGui::PopStyleColor();
		}
		if (userMode == 3) ImGui::TextWrapped("保持接管时捕获的机位；高级设置可启用随角色平移。需要重新取景时先停用接管，再操作游戏镜头。");

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// =========================================================================
		// 2. 基础 Transform (位置 / 旋转)
		// =========================================================================
		const float dragItemWidth = (std::max)(40.0f, (ImGui::GetContentRegionAvail().x - 36.0f) / 3.0f);
		if (userMode != 3)
		{
			ImGui::TextUnformatted("镜头位置与旋转偏移");

			CameraModeTransform* activeTf = nullptr;
			if (userMode == 0)
				activeTf = &settings.transformTrack;
			else if (userMode == 1)
				activeTf = &settings.transformFollowMove;
			else
				activeTf = &settings.transformLookAt;

			// 位置 X / Y / Z
			ImGui::TextUnformatted("位置 (米)");
			ImGui::SetNextItemWidth(dragItemWidth);
			if (ImGui::DragFloat("##cam_pos_x", &activeTf->position.x, 0.01f, -50.0f, 50.0f, "X: %.2f"))
			{
				upgradePerMode();
				changed = true;
			}

			ImGui::SameLine();
			ImGui::SetNextItemWidth(dragItemWidth);
			if (ImGui::DragFloat("##cam_pos_y", &activeTf->position.y, 0.01f, -50.0f, 50.0f, "Y: %.2f"))
			{
				upgradePerMode();
				changed = true;
			}

			ImGui::SameLine();
			ImGui::SetNextItemWidth(dragItemWidth);
			if (ImGui::DragFloat("##cam_pos_z", &activeTf->position.z, 0.01f, -50.0f, 50.0f, "Z: %.2f"))
			{
				upgradePerMode();
				changed = true;
			}

			// 旋转 X / Y / Z
			ImGui::TextUnformatted("旋转 (度)");
			ImGui::SetNextItemWidth(dragItemWidth);
			if (ImGui::DragFloat("##cam_rot_x", &activeTf->rotation.x, 0.5f, -180.0f, 180.0f, "X: %.1f°"))
			{
				upgradePerMode();
				changed = true;
			}

			ImGui::SameLine();
			ImGui::SetNextItemWidth(dragItemWidth);
			if (ImGui::DragFloat("##cam_rot_y", &activeTf->rotation.y, 0.5f, -180.0f, 180.0f, "Y: %.1f°"))
			{
				upgradePerMode();
				changed = true;
			}

			ImGui::SameLine();
			ImGui::SetNextItemWidth(dragItemWidth);
			if (ImGui::DragFloat("##cam_rot_z", &activeTf->rotation.z, 0.5f, -180.0f, 180.0f, "Z: %.1f°"))
			{
				upgradePerMode();
				changed = true;
			}

			if (ImGui::Button("重置位置与旋转偏移", ImVec2(160, 24)))
			{
				upgradePerMode();
				if (userMode == 0)
				{
					settings.transformTrack = CameraSettings::DefaultTrackTransform();
					settings.vmdOffset = settings.transformTrack.position;
				}
				else if (userMode == 1)
				{
					settings.transformFollowMove = CameraSettings::DefaultFollowMoveTransform();
					settings.followOffset = settings.transformFollowMove.position;
				}
				else
				{
					settings.transformLookAt = CameraSettings::DefaultLookAtTransform();
					settings.followOffset = settings.transformLookAt.position;
				}
				changed = true;
			}

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
		}

		// =========================================================================
		// 3. 跟随模式专属设置 (Follow Factor)
		// =========================================================================
		if (userMode == 1 || userMode == 2)
		{
			ImGui::TextUnformatted("跟随幅度（各轴向跟随位移比例）");

			ImGui::TextUnformatted("比例");
			ImGui::SetNextItemWidth(dragItemWidth);
			if (ImGui::DragFloat("##ff_x", &settings.followFactor.x, 0.01f, 0.0f, 1.0f, "X: %.2f"))
				changed = true;

			ImGui::SameLine();
			ImGui::SetNextItemWidth(dragItemWidth);
			if (ImGui::DragFloat("##ff_y", &settings.followFactor.y, 0.01f, 0.0f, 1.0f, "Y: %.2f"))
				changed = true;

			ImGui::SameLine();
			ImGui::SetNextItemWidth(dragItemWidth);
			if (ImGui::DragFloat("##ff_z", &settings.followFactor.z, 0.01f, 0.0f, 1.0f, "Z: %.2f"))
				changed = true;

			if (ImGui::Button("重置跟随幅度", ImVec2(120, 24)))
			{
				settings.followFactor = Vector3(1.0f, 1.0f, 1.0f);
				changed = true;
			}

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
		}

		// =========================================================================
		// 4. 高级镜头设置 (折叠)
		// =========================================================================
		if (ImGui::CollapsingHeader("高级镜头设置"))
		{
			ImGui::Indent(10.0f);

			// VMD 模式下专属选项
			if (userMode == 0 || userMode == 3)
			{
				if (ImGui::Checkbox("镜头随角色平移", &settings.followActor))
					changed = true;
			}

			if (userMode == 1 || userMode == 2)
			{
				// 注视目标偏移
				ImGui::TextUnformatted("注视目标偏移 (Target Offset)");
				ImGui::SetNextItemWidth(dragItemWidth);
				if (ImGui::DragFloat("##to_x", &settings.targetOffset.x, 0.01f, -5.0f, 5.0f, "X: %.2f")) changed = true;
				ImGui::SameLine();
				ImGui::SetNextItemWidth(dragItemWidth);
				if (ImGui::DragFloat("##to_y", &settings.targetOffset.y, 0.01f, -5.0f, 5.0f, "Y: %.2f")) changed = true;
				ImGui::SameLine();
				ImGui::SetNextItemWidth(dragItemWidth);
				if (ImGui::DragFloat("##to_z", &settings.targetOffset.z, 0.01f, -5.0f, 5.0f, "Z: %.2f")) changed = true;

				// 跟随视野
				ImGui::SetNextItemWidth(120.0f);
				if (ImGui::SliderFloat("镜头 FOV", &settings.followFov, 15.0f, 90.0f, "%.1f°")) changed = true;
			}

			// 场景范围限制
			if (ImGui::Checkbox("限制镜头在场景范围内", &settings.limitToScene)) changed = true;
			if (settings.limitToScene)
			{
				ImGui::TextUnformatted("最小边界 (Bounds Min)");
				ImGui::SetNextItemWidth(dragItemWidth);
				if (ImGui::DragFloat("##bmin_x", &settings.boundsMin.x, 0.1f, -50.0f, 50.0f, "X: %.1f")) changed = true;
				ImGui::SameLine();
				ImGui::SetNextItemWidth(dragItemWidth);
				if (ImGui::DragFloat("##bmin_y", &settings.boundsMin.y, 0.1f, -50.0f, 50.0f, "Y: %.1f")) changed = true;
				ImGui::SameLine();
				ImGui::SetNextItemWidth(dragItemWidth);
				if (ImGui::DragFloat("##bmin_z", &settings.boundsMin.z, 0.1f, -50.0f, 50.0f, "Z: %.1f")) changed = true;

				ImGui::TextUnformatted("最大边界 (Bounds Max)");
				ImGui::SetNextItemWidth(dragItemWidth);
				if (ImGui::DragFloat("##bmax_x", &settings.boundsMax.x, 0.1f, -50.0f, 50.0f, "X: %.1f")) changed = true;
				ImGui::SameLine();
				ImGui::SetNextItemWidth(dragItemWidth);
				if (ImGui::DragFloat("##bmax_y", &settings.boundsMax.y, 0.1f, -50.0f, 50.0f, "Y: %.1f")) changed = true;
				ImGui::SameLine();
				ImGui::SetNextItemWidth(dragItemWidth);
				if (ImGui::DragFloat("##bmax_z", &settings.boundsMax.z, 0.1f, -50.0f, 50.0f, "Z: %.1f")) changed = true;
			}

			ImGui::Unindent(10.0f);
		}

		// 提交镜头修改
		if (changed)
		{
			if (userMode == 0)
				settings.vmdOffset = settings.transformTrack.position;
			else if (userMode == 1)
				settings.followOffset = settings.transformFollowMove.position;
			else if (userMode == 2)
				settings.followOffset = settings.transformLookAt.position;

			overlay.cameraExpected = settings;
			overlay.cameraSettingsPending = true;
			if (ctx.fnSetCameraSettings) ctx.fnSetCameraSettings(ctx.self, settings);
		}

		ImGui::Spacing();
		ImGui::BeginDisabled(!settings.IsValid() || !ctx.fnSaveCameraSettings);
		if (ImGui::Button("保存镜头设置", ImVec2(120, 24))) ctx.fnSaveCameraSettings(ctx.self);
		ImGui::EndDisabled();
		ImGui::TextWrapped("修改立即生效；保存后下次启动自动载入，作用于全部曲目。");
		if (!ctx.cameraNotice.empty())
		{
			const bool failed = ctx.cameraNotice.find("失败") != std::string::npos ||
				ctx.cameraNotice.find("无效") != std::string::npos;
			if (failed) ImGui::PushStyleColor(ImGuiCol_Text, GuiTheme::ErrorColor());
			ImGui::TextWrapped("%s", ctx.cameraNotice.c_str());
			if (failed) ImGui::PopStyleColor();
		}
	}
} // namespace GakuMotion
#endif
