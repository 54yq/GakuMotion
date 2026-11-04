#pragma once

#include "../GkGuiOverlay.hpp"
#include "../GkCamera.hpp"
#include "GkGuiThemeColors.hpp"
#include "GuiCameraMode.hpp"

#ifdef GKMOTION_GUI_ACTIVE
namespace GakuMotion
{
	inline void GuiOverlay::DrawCameraTab(const GuiContext& ctx)
	{
		// Retain edits until the Unity-thread command is acknowledged. The
		// snapshot can lag behind this render frame, especially during input.
		if (!cameraDraftReady)
		{
			cameraDraft = cameraLastSnapshot = ctx.cameraSnapshot.settings;
			cameraDraftReady = true;
		}
		else if (cameraSettingsPending)
		{
			if (ctx.cameraSnapshot.settings == cameraExpected)
			{
				cameraSettingsPending = false;
				cameraLastSnapshot = ctx.cameraSnapshot.settings;
			}
		}
		else if (!(ctx.cameraSnapshot.settings == cameraLastSnapshot))
		{
			cameraDraft = cameraLastSnapshot = ctx.cameraSnapshot.settings;
		}
		auto& settings = cameraDraft;
		ImGui::Text("镜头接管: %s | 运镜轨道: %s", ctx.cameraSnapshot.owned ? "生效" : "未接管",
		            ctx.cameraSnapshot.hasTrack ? "已加载" : "无");
		if (!ctx.cameraSnapshot.available)
			ImGui::TextColored(GuiTheme::WarningColor(), "当前镜头接口未就绪，请检查插件日志。");
		if (ctx.cameraSnapshot.owned)
		{
			const auto p = ctx.cameraSnapshot.position;
			ImGui::Text("控制镜头坐标: [%.3f, %.3f, %.3f] | 视野: %.1f", p.x, p.y, p.z, ctx.cameraSnapshot.fov);
		}
		if (!ctx.cameraSource.empty()) ImGui::TextWrapped("轨道来源: %s", ctx.cameraSource.c_str());
		if (ctx.cameraFrames && ctx.cameraFps > 0)
			ImGui::Text("镜头帧数: %u | fps: %.0f | 时长: %.2fs", ctx.cameraFrames, ctx.cameraFps,
			            (ctx.cameraFrames - 1) / ctx.cameraFps);
		if (!ctx.cameraSnapshot.error.empty())
			ImGui::TextColored(GuiTheme::ErrorColor(), "轨道错误: %s", ctx.cameraSnapshot.error.c_str());
		if (!ctx.cameraSnapshot.followTargetError.empty())
			ImGui::TextColored(GuiTheme::ErrorColor(), "跟随目标错误: %s", ctx.cameraSnapshot.followTargetError.c_str());
		ImGui::Separator();
		int selection = CameraControlModeIndex(settings);
		bool changed = ImGui::Combo("镜头模式", &selection, kCameraControlModes, IM_ARRAYSIZE(kCameraControlModes));
		if (changed) SetCameraControlMode(settings, selection);
		ImGui::BeginDisabled(!settings.enabled);
		ImGui::BeginDisabled(settings.mode == CameraMode::Follow);
		changed |= ImGui::Checkbox("运镜随角色平移", &settings.followActor);
		ImGui::EndDisabled();
		changed |= ImGui::Checkbox("镜头限定在场景范围内", &settings.limitToScene);
		if (settings.mode == CameraMode::Follow)
		{
			int target = static_cast<int>(settings.followTarget);
			if (ImGui::Combo("跟随部位", &target, "髋部\0胸部\0头部\0"))
			{
				settings.followTarget = static_cast<CameraFollowTarget>(target);
				changed = true;
			}
		}

		ImGui::EndDisabled();
		ImGui::Spacing();
		static const char* const shadowModes[] = {
			"0: 原生默认 (Native Default)",
			"1: 阴影近机位锚定 (消除大Distance硬切)",
			"2: 完全绕过阴影 (Bypass Actor Shadow)"
		};
		int curExp = settings.actorShadowExperimentMode;
		if (ImGui::Combo("角色阴影实验##actor_shadow_exp", &curExp, shadowModes, 3))
		{
			settings.actorShadowExperimentMode = curExp;
			changed = true;
		}
		if (curExp == 1)
		{
			ImGui::TextColored(GuiTheme::SuccessColor(), "已启用机位锚定：大 Distance 下将阴影体贴近角色，避免近裁切面硬切。");
		}
		else if (curExp == 2)
		{
			ImGui::TextColored(GuiTheme::WarningColor(), "已绕过阴影 Pass：不渲染 ActorShadow，用于完全隔离硬切。");
		}
		ImGui::Spacing();
		ImGui::BeginDisabled(!settings.enabled);
		float lo[] = {settings.boundsMin.x, settings.boundsMin.y, settings.boundsMin.z};
		float hi[] = {settings.boundsMax.x, settings.boundsMax.y, settings.boundsMax.z};
		changed |= ImGui::InputFloat3("场景最小坐标（世界）", lo);
		changed |= ImGui::InputFloat3("场景最大坐标（世界）", hi);

		ImGui::Spacing();
		ImGui::TextUnformatted("【VMD 运镜初始位置调节】");
		ImGui::TextDisabled("用于匹配不同身高的偶像（如较矮偶像调低 Y 轴高度）；以角色本地坐标为基准。");
		ImGui::BeginDisabled(settings.mode != CameraMode::Track);
		float vmdOffset[] = {settings.vmdOffset.x, settings.vmdOffset.y, settings.vmdOffset.z};
		changed |= ImGui::InputFloat3("VMD 镜头初始偏移（角色坐标）", vmdOffset, "%.3f");
		ImGui::SameLine();
		if (ImGui::Button("归零##vmd_offset_zero"))
		{
			vmdOffset[0] = vmdOffset[1] = vmdOffset[2] = 0.0f;
			changed = true;
		}
		ImGui::SetNextItemWidth(260.0f);
		changed |= ImGui::SliderFloat("VMD Y 高度微调 (m)##vmd_y_slider", &vmdOffset[1], -1.0f, 1.0f, "%.3f m");
		ImGui::EndDisabled();

		ImGui::Spacing();
		ImGui::TextUnformatted("【跟随模式位置设置】");
		ImGui::BeginDisabled(settings.mode != CameraMode::Follow);
		float offset[] = {settings.followOffset.x, settings.followOffset.y, settings.followOffset.z};
		float target[] = {settings.targetOffset.x, settings.targetOffset.y, settings.targetOffset.z};
		changed |= ImGui::InputFloat3("跟随位置偏移（角色坐标）", offset);
		changed |= ImGui::InputFloat3("注视目标偏移（相对所选部位，角色坐标）", target);
		ImGui::EndDisabled();
		ImGui::TextUnformatted("追随速率 (Target Rate)");
		ImGui::TextWrapped("0 停止追随，0.1 平滑追随，1 即时追随；按接管时镜头本地 X/Y/Z 追近当前目标。");
		changed |= ImGui::SliderFloat3("追随速率 XYZ", &settings.followRate.x, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (ImGui::Button("重置追随速率 (0.1)"))
		{
			settings.followRate = {0.1f, 0.1f, 0.1f};
			changed = true;
		}
		ImGui::TextUnformatted("位移比例（旧版跟随幅度）");
		ImGui::TextWrapped("按接管时的镜头本地 X/Y/Z 缩放最终髋部位移：0 不跟随，0.5 跟一半，1 完整跟随；不表示追赶速度。");
		const char* factorLabels[] = {"位移比例 X（镜头本地）", "位移比例 Y（镜头本地）", "位移比例 Z（镜头本地）"};
		float* factors[] = {&settings.followFactor.x, &settings.followFactor.y, &settings.followFactor.z};
		for (int i = 0; i < 3; ++i)
		{
			ImGui::PushID(i);
			ImGui::Text("%-16s", factorLabels[i]);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(65.0f);
			changed |= ImGui::InputFloat("##input", factors[i], 0.0f, 0.0f, "%.2f");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(160.0f);
			changed |= ImGui::SliderFloat("##slider", factors[i], 0.0f, 1.0f, "%.2f");
			ImGui::SameLine();
			if (ImGui::Button("全幅 (1.0)"))
			{
				*factors[i] = 1.0f;
				changed = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("不跟随 (0.0)"))
			{
				*factors[i] = 0.0f;
				changed = true;
			}
			ImGui::PopID();
		}

		ImGui::TextUnformatted("跟随视野");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(65.0f);
		changed |= ImGui::InputFloat("##fov_input", &settings.followFov, 0.0f, 0.0f, "%.1f");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(160.0f);
		changed |= ImGui::SliderFloat("##fov_slider", &settings.followFov, 1.0f, 179.0f, "%.1f°");
		ImGui::EndDisabled();

		bool valid = CameraFinite(settings.followFactor) &&
			settings.followFactor.x >= 0.0f && settings.followFactor.x <= 1.0f &&
			settings.followFactor.y >= 0.0f && settings.followFactor.y <= 1.0f &&
			settings.followFactor.z >= 0.0f && settings.followFactor.z <= 1.0f &&
			CameraFinite(settings.followRate) &&
			settings.followRate.x >= 0.0f && settings.followRate.x <= 1.0f &&
			settings.followRate.y >= 0.0f && settings.followRate.y <= 1.0f &&
			settings.followRate.z >= 0.0f && settings.followRate.z <= 1.0f &&
			std::isfinite(settings.followFov) &&
			settings.followFov >= 1.0f && settings.followFov <= 179.0f;
		for (int i = 0; i < 3; ++i)
			valid &= std::isfinite(lo[i]) && std::isfinite(hi[i]) &&
				lo[i] <= hi[i] && std::isfinite(offset[i]) && std::isfinite(target[i]) &&
				std::isfinite(vmdOffset[i]);
		settings.boundsMin = {lo[0], lo[1], lo[2]};
		settings.boundsMax = {hi[0], hi[1], hi[2]};
		settings.followOffset = {offset[0], offset[1], offset[2]};
		settings.targetOffset = {target[0], target[1], target[2]};
		settings.vmdOffset = {vmdOffset[0], vmdOffset[1], vmdOffset[2]};
		auto apply = [&]()
		{
			if (!ctx.fnSetCameraSettings) return;
			ctx.fnSetCameraSettings(ctx.self, settings);
			cameraExpected = settings;
			cameraSettingsPending = true;
		};
		if (changed && valid) apply();
		if (!valid) ImGui::TextWrapped("范围最小值须小于等于最大值，所有坐标须为有限数值。");
		ImGui::BeginDisabled(!valid || !ctx.fnSaveCameraSettings);
		if (ImGui::Button("保存镜头设置"))
		{
			apply();
			ctx.fnSaveCameraSettings(ctx.self);
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("恢复默认镜头设置"))
		{
			settings = CameraSettings{};
			apply();
		}
		ImGui::TextDisabled("修改立即生效；保存后下次启动自动载入。选择游戏镜头即可恢复手动控制。");
		ImGui::Separator();
		ImGui::TextWrapped("播放和暂停期间拦截键鼠镜头移动；停止后恢复。跟随目标沿用角色选择，全部角色时跟随首个角色。场景范围需按当前场景设置，边界限制不检测墙体碰撞。");
		ImGui::Separator();
		ImGui::TextUnformatted("【导入当前曲目的镜头】");
		const int selected = ctx.selectedMotionIndex ? *ctx.selectedMotionIndex : -1;
		const bool hasSelected = ctx.motions && selected >= 0 && selected < static_cast<int>(ctx.motions->size());
		if (hasSelected) ImGui::TextWrapped("曲目: %s", (*ctx.motions)[selected].title.c_str());
		else ImGui::TextDisabled("请先在“曲目与播放”中选择曲目。");
		const bool active = ctx.player && (ctx.player->state == PlaybackState::Playing ||
			ctx.player->state == PlaybackState::Paused);
		ImGui::BeginDisabled(ctx.cameraImportBusy || ctx.importBusy);
		ImGui::InputText("镜头文件", importCameraSource, sizeof(importCameraSource));
		ImGui::SameLine();
		if (ImGui::Button("浏览##camera")) Browse(ctx, 2, importCameraSource, sizeof(importCameraSource));
		ImGui::Combo("VMD 采样率", &importCameraFpsIndex,
		             "30 fps\0 60 fps\0 90 fps\0 120 fps\0 150 fps\0 180 fps\0 210 fps\0 240 fps\0");
		ImGui::InputFloat("VMD 坐标缩放", &importCameraScale, 0.001f, 0.01f, "%.4f");
		ImGui::Checkbox("识别相邻关键帧的大幅变化为切镜", &importCameraDetectCuts);
		ImGui::TextWrapped(
			"支持镜头专用 VMD 和已烘焙的 .gkcamera。采样和缩放只用于 VMD。导入将替换当前曲目的 camera.gkcamera，转换失败会保留原文件。");
		std::string sourceLower = importCameraSource;
		for (char& c : sourceLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		const bool bakedSource = sourceLower.ends_with(".gkcamera");
		const bool importValid = importCameraSource[0] != '\0' && (bakedSource ||
			(std::isfinite(importCameraScale) && importCameraScale > 0));
		ImGui::BeginDisabled(!hasSelected || active || !importValid || !ctx.fnImportCamera);
		if (ImGui::Button("导入并替换当前曲目镜头"))
		{
			CameraImportRequest request;
			request.source = importCameraSource;
			request.motionKey = (*ctx.motions)[selected].name;
			request.fps = static_cast<float>((importCameraFpsIndex + 1) * 30);
			request.scale = importCameraScale;
			request.detectCuts = importCameraDetectCuts;
			ctx.fnImportCamera(ctx.self, request);
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!hasSelected || active || !ctx.fnReloadCamera);
		if (ImGui::Button("重新载入镜头")) ctx.fnReloadCamera(ctx.self);
		ImGui::EndDisabled();
		ImGui::EndDisabled();
		if (active) ImGui::TextDisabled("先停止 MMD 播放，再导入或重新载入镜头。");
		if (ctx.cameraImportBusy) ImGui::TextDisabled("正在后台转换 / 验证镜头，完成后自动载入…");
		if (!ctx.cameraNotice.empty()) ImGui::TextWrapped("%s", ctx.cameraNotice.c_str());
	}
} // namespace GakuMotion
#endif
