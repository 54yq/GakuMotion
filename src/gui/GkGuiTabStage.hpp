#pragma once

#include "../GkGuiOverlay.hpp"

#ifdef GKMOTION_GUI_ACTIVE
namespace GakuMotion
{
	// Legacy callers use the same controls as the current Stage page. The old
	// immediate mount/unmount and unsupported transform controls are retired.
	inline void GuiOverlay::DrawStageTab(const GuiContext& ctx)
	{
		DrawStagePage(*this, ctx);
	}
}
#endif
