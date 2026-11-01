#pragma once

#include "../GkGuiOverlay.hpp"

#ifdef GKMOTION_GUI_ACTIVE
namespace GakuMotion
{
	inline void GuiOverlay::DrawImport(const GuiContext& ctx)
	{
		ImGui::BeginDisabled(ctx.importBusy);
		ImGui::InputText("VMD 文件", importVmd, sizeof(importVmd));
		ImGui::SameLine();
		if (ImGui::Button("浏览##vmd")) Browse(ctx, 0, importVmd, sizeof(importVmd));
		ImGui::InputText("WAV 音频（可选）", importAudio, sizeof(importAudio));
		ImGui::SameLine();
		if (ImGui::Button("浏览##wav")) Browse(ctx, 1, importAudio, sizeof(importAudio));
		ImGui::InputText("曲名（可选）", importTitle, sizeof(importTitle));
		ImGui::Combo("动作帧率##motion", &importMotionFpsIndex, "自动检测 (推荐)\0 30 fps\0 60 fps\0");
		ImGui::TextWrapped("音频留空会寻找同名 WAV 或 song.wav；未找到则导入为无声动作。");
		ImGui::BeginDisabled(importVmd[0] == '\0');
		if (ImGui::Button("转换并加入列表") && ctx.fnImportMotion)
		{
			MotionImportRequest request;
			request.vmd = importVmd;
			request.audio = importAudio;
			request.title = importTitle;
			if (importMotionFpsIndex == 1) request.fps = 30.0f;
			else if (importMotionFpsIndex == 2) request.fps = 60.0f;
			else request.fps = 0.0f;
			ctx.fnImportMotion(ctx.self, request);
		}
		ImGui::EndDisabled();
		ImGui::EndDisabled();
	}

	inline void GuiOverlay::DrawMotionTab(const GuiContext& ctx)
	{
		if (!ctx.player || !ctx.motions)
		{
			ImGui::TextDisabled("运行时尚未就绪");
			return;
		}
		MotionPlayer& player = *ctx.player;
		const std::vector<MotionEntry>& library = *ctx.motions;

		ImGui::Text("【动作库】%s", motionsRoot.c_str());
		if (ImGui::Button("刷新列表") && ctx.fnRefreshMotions)
		{
			ctx.fnRefreshMotions(ctx.self);
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(共 %zu 个曲目)", library.size());
		if (ImGui::Button("导入外部 VMD")) importOpen = !importOpen;
		ImGui::SameLine();
		if (ImGui::Button("恢复已移除曲目")) PlaylistAction(ctx, GuiPlaylistAction::Restore);
		ImGui::SameLine();
		if (ImGui::Button("恢复最近删除")) PlaylistAction(ctx, GuiPlaylistAction::RestoreDeleted);
		if (importOpen) DrawImport(ctx);
		if (!ctx.playlistNotice.empty()) ImGui::TextWrapped("%s", ctx.playlistNotice.c_str());
		if (ctx.importBusy) ImGui::TextDisabled("正在后台导入…");
		motionFilter.Draw("搜索曲目", -1);

		if (ImGui::Button("上一曲") && ctx.fnCycleMotion) ctx.fnCycleMotion(ctx.self, -1);
		ImGui::SameLine();
		if (ImGui::Button("下一曲") && ctx.fnCycleMotion) ctx.fnCycleMotion(ctx.self, +1);

		// ---- 曲目列表 ----
		ImGui::BeginChild("##motion_list", ImVec2(0, 180), true);
		for (size_t i = 0; i < library.size(); ++i)
		{
			if (!motionFilter.PassFilter(library[i].title.c_str()) &&
				!motionFilter.PassFilter(library[i].name.c_str()))
				continue;
			const bool selected = (ctx.selectedMotionIndex && *ctx.selectedMotionIndex == static_cast<int>(i));
			char label[320]{};
			std::snprintf(label, sizeof(label), "%s##%zu", library[i].DisplayLabel().c_str(), i);
			if (ImGui::Selectable(label, selected))
			{
				if (ctx.fnSelectMotion) ctx.fnSelectMotion(ctx.self, static_cast<int>(i), false);
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("目录: %s\n音频: %s\n覆盖表: %s",
				                  library[i].name.c_str(),
				                  library[i].hasAudio ? "有" : "无",
				                  library[i].hasOverrides ? "有" : "无");
			}
		}
		ImGui::EndChild();

		const int selected = ctx.selectedMotionIndex ? *ctx.selectedMotionIndex : -1;
		const bool hasSelected = selected >= 0 && selected < static_cast<int>(library.size());
		ImGui::BeginDisabled(!hasSelected);
		const char* key = hasSelected ? library[selected].name.c_str() : "";
		if (ImGui::Button("上移")) PlaylistAction(ctx, 1, key, -1);
		ImGui::SameLine();
		if (ImGui::Button("下移")) PlaylistAction(ctx, 1, key, 1);
		ImGui::SameLine();
		if (ImGui::Button("从列表移除")) PlaylistAction(ctx, GuiPlaylistAction::Remove, key);
		ImGui::SameLine();
		if (ImGui::Button("删除本地文件")) RequestMotionDelete(ctx, key);
		ImGui::EndDisabled();
		DrawMotionDeleteConfirmation(ctx);
		ImGui::TextDisabled("列表移除保留文件；本地文件删除需确认，可从恢复区找回。顺序自动保存。");
		int mode = ctx.playlistMode;
		if (ImGui::Combo("播放模式", &mode, "播放一次\0单曲循环\0列表循环\0随机轮播\0"))
			PlaylistAction(ctx, 5, "", mode);
		if (ImGui::CollapsingHeader("保存 / 载入播放列表"))
		{
			ImGui::InputText("列表 JSON 路径（空为默认）", playlistFile, sizeof(playlistFile));
			if (ImGui::Button("保存列表")) PlaylistAction(ctx, 3, playlistFile);
			ImGui::SameLine();
			if (ImGui::Button("载入列表")) PlaylistAction(ctx, 4, playlistFile);
		}

		ImGui::Separator();

		// ---- 进度与播放控制 ----
		if (player.clip.total_frames > 0)
		{
			ProbeDrawReady(ctx);
			ImGui::Text("时长: %.2f 秒 | 总帧: %u | 骨骼: %u | fps: %.1f",
			            player.clip.duration_seconds,
			            player.clip.total_frames,
			            player.clip.bone_count,
			            player.clip.fps);

			// ---- 进度条 ----
			if (scrubMotion != player.loadedMotionFile)
			{
				scrub.active = false;
				scrubMotion = player.loadedMotionFile;
			}
			if (!scrub.active)
			{
				s_scrubValue = player.currentTime;
			}
			else
			{
				s_scrubValue = scrub.value;
			}

			const float sliderMax = player.clip.duration_seconds;
			const bool sliderChanged =
				ImGui::SliderFloat("进度##scrub", &s_scrubValue, 0.0f, sliderMax, "%.2f s");
#ifdef GKMOTION_GUI_TEST
			scrubRectMin = ImGui::GetItemRectMin(); scrubRectMax = ImGui::GetItemRectMax();
#endif

			if (ImGui::IsItemActivated())
			{
				scrub.active = true;
				scrub.value = s_scrubValue;
				++probeScrubBegin;
				ProbeScrub(ctx, "BEGIN", s_scrubValue,
				           ImGui::IsMouseDragging(ImGuiMouseButton_Left) ? 1 : 0);
				if (ctx.fnScrubBegin) ctx.fnScrubBegin(ctx.self, s_scrubValue);
				else ProbeScrub(ctx, "BEGIN_NO_CALLBACK", s_scrubValue);
			}
			if (scrub.active)
			{
				scrub.value = s_scrubValue;
				if (sliderChanged)
				{
					++probeScrubUpdate;
					const bool notable = (probeScrubUpdate <= 3) ||
						((probeScrubUpdate % 15) == 0) ||
						(std::fabs(s_scrubValue - probeLastLoggedScrub) > 0.5f);
					if (notable)
					{
						probeLastLoggedScrub = s_scrubValue;
						ProbeScrub(ctx, "UPDATE", s_scrubValue,
						           ImGui::IsMouseDragging(ImGuiMouseButton_Left) ? 1 : 0);
					}
					if (ctx.fnScrubUpdate) ctx.fnScrubUpdate(ctx.self, s_scrubValue);
				}
				if (ImGui::IsItemDeactivated() || !ImGui::IsItemActive())
				{
					scrub.active = false;
					++probeScrubEnd;
					ProbeScrub(ctx, "END", scrub.value,
					           ImGui::IsMouseDragging(ImGuiMouseButton_Left) ? 1 : 0);
					if (ctx.fnScrubEnd) ctx.fnScrubEnd(ctx.self, scrub.value);
					else if (ctx.fnSeek) ctx.fnSeek(ctx.self, scrub.value);
					else ProbeScrub(ctx, "END_NO_CALLBACK", scrub.value);
				}
			}

			if (ImGui::Button("-5s"))
			{
				if (ctx.fnSeek) ctx.fnSeek(ctx.self, player.currentTime - 5.0f);
				else player.SeekRelative(-5.0f);
			}
			ImGui::SameLine();
			if (ImGui::Button("+5s"))
			{
				if (ctx.fnSeek) ctx.fnSeek(ctx.self, player.currentTime + 5.0f);
				else player.SeekRelative(+5.0f);
			}
			ImGui::SameLine();
			if (ImGui::Button("重置到起点"))
			{
				if (ctx.fnSeek) ctx.fnSeek(ctx.self, 0.0f);
				else player.Seek(0.0f);
				scrub.active = false;
				s_scrubValue = 0.0f;
			}

			if (player.state == PlaybackState::Playing)
			{
				if (ImGui::Button("暂停"))
				{
					if (ctx.fnPause) ctx.fnPause(ctx.self);
					else player.Pause();
				}
			}
			else if (player.state == PlaybackState::Paused)
			{
				if (ImGui::Button("继续"))
				{
					if (ctx.fnResume) ctx.fnResume(ctx.self);
					else player.Resume();
				}
			}
			else
			{
				if (ImGui::Button("播放"))
				{
					if (ctx.fnPlay) ctx.fnPlay(ctx.self);
					else player.Play();
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("停止"))
			{
				if (ctx.fnStop) ctx.fnStop(ctx.self);
				else player.Stop();
			}

			if (ImGui::Button("上一帧") && ctx.fnSeek) ctx.fnSeek(
				ctx.self, player.currentTime - 1.0f / player.clip.fps);
			ImGui::SameLine();
			if (ImGui::Button("下一帧") && ctx.fnSeek) ctx.fnSeek(
				ctx.self, player.currentTime + 1.0f / player.clip.fps);
			ImGui::InputFloat("跳转到（秒）", &seekInput, 1.0f, 5.0f, "%.3f");
			ImGui::SameLine();
			if (ImGui::Button("跳转") && ctx.fnSeek) ctx.fnSeek(ctx.self, seekInput);
			bool muted = player.muted;
			if (ImGui::Checkbox("静音预览", &muted) && ctx.fnSetMuted) ctx.fnSetMuted(ctx.self, muted);
			const bool audioSynced = !player.muted && player.fnGetAudioTime &&
			(ctx.selectedMotionIndex && *ctx.selectedMotionIndex >= 0 &&
				*ctx.selectedMotionIndex < static_cast<int>(library.size()) && library[*ctx.selectedMotionIndex].
				hasAudio);
			ImGui::BeginDisabled(audioSynced);
			float speed = player.playbackSpeedMultiplier;
			ImGui::TextUnformatted("预览速度");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(65.0f);
			bool speedInputChanged = ImGui::InputFloat("##speed_input", &speed, 0.0f, 0.0f, "%.2f");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(180.0f);
			bool speedSliderChanged = ImGui::SliderFloat("##speed_slider", &speed, 0.1f, 5.0f, "%.2fx");
			if ((speedInputChanged || speedSliderChanged) && ctx.fnSetSpeed)
				ctx.fnSetSpeed(ctx.self, speed);
			ImGui::EndDisabled();
			if (audioSynced) ImGui::TextDisabled("有声播放与音频同步；静音后可调整动作速度。");

			ImGui::TextDisabled("状态: %s | 音频校正: %.3fs | 累计跳转: %d 次",
			                    Core_PlaybackStateName(player.state),
			                    player.AudioSyncOffsetSeconds(),
			                    player.SeekCount());
		}
		else
		{
			ImGui::TextDisabled("请先在列表中选择并载入一个动作包");
		}
	}
} // namespace GakuMotion
#endif
