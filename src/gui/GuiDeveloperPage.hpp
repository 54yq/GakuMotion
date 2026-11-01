#pragma once

// =============================================================================
// GuiDeveloperPage.hpp — 开发者页面（仅开发者模式显示：遥测/诊断/鞋底测量/Pitch/资产挂载）
// =============================================================================

#include "GuiOverlayCommon.hpp"
#include "../GkGuiOverlay.hpp"

#ifdef GKMOTION_GUI_ACTIVE
namespace GakuMotion
{
	inline void DrawDeveloperPage(GuiOverlay& overlay, const GuiContext& ctx)
	{
		ImGui::TextColored(GuiTheme::WarningColor(), "【开发者模式已激活】当前包含低频维护工具、实验性功能与内部遥测");
		ImGui::Separator();

		if (ImGui::BeginTabBar("##developer_sub_tabs"))
		{
			// ---- 子页面 1：运行时诊断与遥测 ----
			if (ImGui::BeginTabItem("遥测与诊断"))
			{
				ImGui::Spacing();
				ImGui::TextUnformatted("内部状态诊断");
				if (!ctx.statusLine.empty())
				{
					ImGui::TextColored(GuiTheme::SuccessColor(), "%s", ctx.statusLine.c_str());
				}

				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();

				// 播放器内部遥测
				if (ctx.player)
				{
					const auto& player = *ctx.player;
					ImGui::Text("播放器状态:");
					ImGui::BulletText("当前状态: %d (%s) | 当前时间: %.3fs",
					                  static_cast<int>(player.state),
					                  GuiOverlay::Core_PlaybackStateName(player.state),
					                  player.currentTime);
					ImGui::BulletText("剪辑信息: 时长=%.3fs | 总帧数=%u | FPS=%.1f | 骨骼数=%u",
					                  player.clip.duration_seconds,
					                  player.clip.total_frames,
					                  player.clip.fps,
					                  player.clip.bone_count);
					ImGui::BulletText("已载入文件: %s", player.loadedMotionFile.c_str());
					ImGui::BulletText("驱动 Actor: %p | 绑定骨骼: %zu",
					                  player.controller.currentBinding.actor,
					                  player.controller.currentBinding.boundBoneCount);
				}

				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();

				// Scrub 探针遥测
				ImGui::Text("Scrub 拖动链路探针 (PROBE S1):");
				ImGui::BulletText("Begin 触发: %d 次 | Update 触发: %d 次 | End 触发: %d 次",
				                  overlay.probeScrubBegin, overlay.probeScrubUpdate, overlay.probeScrubEnd);
				ImGui::BulletText("最后拖动值: %.3f | active: %d",
				                  overlay.probeLastScrubValue, overlay.scrub.active ? 1 : 0);

				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();

				// 实验性底层物理开关
				ImGui::Text("实验性底层特性:");
				bool bq = ctx.beforeQuartzExperiment;
				if (ImGui::Checkbox("BeforeQuartz 实验 (优先于 Quartz 阶段估算)", &bq))
				{
					if (ctx.fnSetBeforeQuartzExperiment) ctx.fnSetBeforeQuartzExperiment(ctx.self, bq);
				}

				ImGui::EndTabItem();
			}

			// ---- 子页面 2：鞋底测量 v2 ----
			if (ImGui::BeginTabItem("鞋底测量 v2"))
			{
				overlay.DrawSoleTab(ctx);
				ImGui::EndTabItem();
			}

			// ---- 子页面 3：Pitch Demo ----
			if (ImGui::BeginTabItem("Pitch Demo [Experimental]"))
			{
				overlay.DrawExperimentalSolePitchTab(ctx);
				ImGui::EndTabItem();
			}

			// ---- 子页面 4：自定义舞台资产挂载 ----
			if (ImGui::BeginTabItem("舞台资产挂载"))
			{
				ImGui::Spacing();
				ImGui::TextUnformatted("官方 3D 舞台资产挂载");
				if (!ctx.stageMountedKey.empty())
				{
					ImGui::TextColored(GuiTheme::SuccessColor(), "当前已挂载: %s", ctx.stageMountedKey.c_str());
				}
				else
				{
					ImGui::TextDisabled("当前未挂载自定义舞台资产");
				}

				static char s_mountKeyBuf[128] = "env_3d_live_all001-00-noon";
				ImGui::SetNextItemWidth(260.0f);
				ImGui::InputText("舞台 Asset Key##mount_key", s_mountKeyBuf, sizeof(s_mountKeyBuf));
				ImGui::SameLine();
				if (ImGui::Button("挂载舞台##btn_mount"))
				{
					if (ctx.fnMountCustomStage && s_mountKeyBuf[0])
					{
						ctx.fnMountCustomStage(ctx.self, s_mountKeyBuf);
					}
				}
				ImGui::SameLine();
				if (ImGui::Button("卸载舞台##btn_unmount"))
				{
					if (ctx.fnUnloadCustomStage)
					{
						ctx.fnUnloadCustomStage(ctx.self);
					}
				}

				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}
	}
} // namespace GakuMotion
#endif
