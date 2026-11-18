#pragma once

#include "../GkGuiOverlay.hpp"
#include "GkGuiThemeColors.hpp"

#ifdef GKMOTION_GUI_ACTIVE
namespace GakuMotion
{
	inline void GuiOverlay::DrawStatusBar(const GuiContext& ctx)
	{
		ImGui::TextColored(GuiTheme::SuccessColor(), "%s", ctx.statusLine.c_str());
		ImGui::Separator();
	}
} // namespace GakuMotion
#endif
