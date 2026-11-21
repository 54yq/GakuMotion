#pragma once

// =============================================================================
// GuiOverlayShell.hpp — 全局外壳布局（顶部状态栏 + 左侧 Sidebar + 中间 Content + 底部播放条）
// =============================================================================

#include "GuiOverlayCommon.hpp"
#include "GkGuiBranding.hpp"
#include "GkGuiIcons.hpp"
#include "GuiTransportBar.hpp"
#include "GuiLibraryPage.hpp"
#include "GuiMotionPage.hpp"
#include "GuiCameraPage.hpp"
#include "GuiStagePage.hpp"
#include "GuiSettingsPage.hpp"
#include "GuiDeveloperPage.hpp"

#ifdef GKMOTION_GUI_ACTIVE
namespace GakuMotion
{
	inline void DrawTopStatusBar(GuiOverlay& overlay, const GuiContext& ctx)
	{
		// 仅显示四项明确的普通状态：
		// 1. 当前角色
		// 2. 当前曲目
		// 3. 当前播放状态
		// 4. 就绪 / 错误指示
		const std::string actorName = ctx.boundActorLabel.empty() ? "尚未绑定" : ctx.boundActorLabel;
		const std::string targetName = ctx.targetActorLabel.empty() ? "未确认" : ctx.targetActorLabel;

		std::string trackName = "未加载曲目";
		if (ctx.player && !ctx.player->loadedMotionFile.empty())
		{
			std::filesystem::path p(ctx.player->loadedMotionFile);
			trackName = p.parent_path().filename().string();
			if (trackName.empty()) trackName = p.stem().string();
		}
		else if (!overlay.libraryEntries.empty() && overlay.selectedLibraryIndex >= 0 &&
		         overlay.selectedLibraryIndex < static_cast<int>(overlay.libraryEntries.size()))
		{
			trackName = overlay.libraryEntries[overlay.selectedLibraryIndex].title;
		}

		const char* stateStr = "已停止";
		ImVec4 stateColor = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
		if (ctx.player)
		{
			switch (ctx.player->state)
			{
			case PlaybackState::Playing:
				stateStr = "播放中";
				stateColor = GuiTheme::SuccessColor();
				break;
			case PlaybackState::Paused:
				stateStr = "已暂停";
				stateColor = GuiTheme::WarningColor();
				break;
			case PlaybackState::Preview:
				stateStr = "预览";
				stateColor = GuiTheme::InfoColor();
				break;
			case PlaybackState::Stopped:
			default:
				stateStr = "已停止";
				stateColor = ImVec4(0.55f, 0.58f, 0.65f, 1.0f);
				break;
			}
		}

		// 绘制状态条
		ImGui::TextWrapped("驱动: %s | 当前绑定: %s", targetName.c_str(), actorName.c_str());
		ImGui::TextWrapped("曲目: %s", trackName.c_str());
		ImGui::TextColored(stateColor, "状态: %s", stateStr);

		// 右对齐就绪 / 错误指示
		const char* readyText = ctx.injectionAllowed ? "[允许控制]" : "[已禁用]";
		const ImVec4 readyColor = ctx.injectionAllowed ? GuiTheme::SuccessColor() : GuiTheme::ErrorColor();
		ImGui::SameLine(0.0f, 12.0f);
		ImGui::TextColored(readyColor, "%s", readyText);
		if (ctx.liveTakeoverActive)
		{
			if (ImGui::Button("Live 接管页面"))
			{
				overlay.CommitScrub(ctx);
				overlay.livePageOpen = true;
			}
			if (!ctx.liveControlNotice.empty()) ImGui::TextWrapped("%s", ctx.liveControlNotice.c_str());
		}

		ImGui::Spacing();
		ImGui::Separator();
	}

	inline void DrawShell(GuiOverlay& overlay, const GuiContext& ctx)
	{
		const auto& style = ImGui::GetStyle();
		const float availableWidth = ImGui::GetContentRegionAvail().x;
		const float sidebarWidth = (std::max)(115.0f, ImGui::GetFontSize() * 6.4f);
		const bool compact = availableWidth < sidebarWidth + style.ItemSpacing.x + ImGui::GetFontSize() * 18.0f;

		auto drawNavigation = [&](bool horizontal)
		{
			const float width = ImGui::GetContentRegionAvail().x;
			const float minWidth = GuiIcons::ButtonSize("设置").x;
			const int columns = width >= 3.0f * minWidth + 2.0f * style.ItemSpacing.x ? 3 : 2;
			const float itemWidth = horizontal
				? (std::max)(1.0f, std::floor((width - (columns - 1) * style.ItemSpacing.x) / columns))
				: width;
			const float height = (std::max)(30.0f, ImGui::GetFrameHeight());
			int itemIndex = 0;
			auto navItem = [&](const char* label, GuiPage page, GuiIcons::Icon icon)
			{
				if (horizontal && itemIndex % columns != 0) ImGui::SameLine();
				++itemIndex;
				ImGui::PushStyleColor(ImGuiCol_Button, overlay.activePage == page
					? ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive) : ImVec4(0, 0, 0, 0));
				const ImVec2 size(itemWidth, height);
				const bool pressed = horizontal ? ImGui::Button(label, size) : GuiIcons::Button(label, icon, size);
				if (pressed) overlay.activePage = page;
				ImGui::PopStyleColor();
			};
			navItem("曲库", GuiPage::Library, GuiIcons::Icon::Library);
			navItem("动作", GuiPage::Motion, GuiIcons::Icon::Motion);
			navItem("镜头", GuiPage::Camera, GuiIcons::Icon::Camera);
			navItem("舞台", GuiPage::Stage, GuiIcons::Icon::Stage);
			navItem("外部舞台", GuiPage::ExternalStage, GuiIcons::Icon::Stage);
			navItem("设置", GuiPage::Settings, GuiIcons::Icon::Settings);
			if (overlay.developerMode) navItem("  [开发]", GuiPage::Developer, GuiIcons::Icon::Developer);
		};
		if (compact)
		{
			ImGui::PushID("##gkmotion_compact_navigation");
			drawNavigation(true);
			ImGui::PopID();
			ImGui::Separator();
		}

		// Reserve the full transport and both vertical spacing gaps.
		const float transportHeight = TransportBarHeight(availableWidth, &overlay) + 2.0f * style.ItemSpacing.y + 1.0f;
		const float contentAreaHeight = (std::max)(1.0f, std::floor(ImGui::GetContentRegionAvail().y - transportHeight));
		const float mainContentWidth = compact ? availableWidth :
			(std::max)(1.0f, availableWidth - sidebarWidth - style.ItemSpacing.x);

		if (!compact)
		{
			ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
			if (ImGui::BeginChild("##gkmotion_sidebar", ImVec2(sidebarWidth, contentAreaHeight), false))
			{
				GuiBranding::DrawSidebarIcon(ImGui::GetContentRegionAvail().x - 30.0f);
				ImGui::Separator();
				drawNavigation(false);
			}
			ImGui::EndChild();
			ImGui::PopStyleColor();
			ImGui::PopStyleVar();
			ImGui::SameLine();
		}


		// 2. 中间 Content
		if (ImGui::BeginChild("##gkmotion_main_content", ImVec2(mainContentWidth, contentAreaHeight), false))
		{
			DrawTopStatusBar(overlay, ctx);

			switch (overlay.activePage)
			{
			case GuiPage::Library:
				DrawLibraryPage(overlay, ctx);
				break;
			case GuiPage::Motion:
				DrawMotionPage(overlay, ctx);
				break;
			case GuiPage::Camera:
				DrawCameraPage(overlay, ctx);
				break;
			case GuiPage::Stage:
				DrawStagePage(overlay, ctx);
				break;
			case GuiPage::ExternalStage:
				DrawExternalStageControl();
				break;
			case GuiPage::Settings:
				DrawSettingsPage(overlay, ctx);
				break;
			case GuiPage::Developer:
				if (overlay.developerMode)
					DrawDeveloperPage(overlay, ctx);
				else
					DrawLibraryPage(overlay, ctx);
				break;
			}
		}
		ImGui::EndChild();

		// ---- 下半部：底部全局常驻播放控制器（横跨 Sidebar + Content） ----
		DrawTransportBar(overlay, ctx);
	}
} // namespace GakuMotion
#endif
