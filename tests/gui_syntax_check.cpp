// =============================================================================
// tests/gui_syntax_check.cpp — ImGui 面板骨架的「编译期」验证
// =============================================================================
// 目的：GkGuiOverlay.hpp 默认不参与构建（GKMOTION_ENABLE_IMGUI 未定义），
//       若不单独验证，面板代码会长期处于「无人编译」的腐烂状态。
//       本文件在启用 ImGui 的编译配置下包含面板头文件，
//       由 tests/run_tests.ps1 以 -fsyntax-only 方式检查语法与语义。
//
// 注意（阶段二前置条件）：能编译 ≠ 能绘制。宿主 loader 未导出任何 ImGui 符号，
//       插件侧仍缺少共享的 ImGuiContext，见 DevAgents/ARCHITECTURE.md §6。
// =============================================================================

#include "GkGuiOverlay.hpp"

namespace
{
	// 轻量触碰：确保 GuiContext 的字段与回调签名在编译期被实例化检查
	void TouchGuiContext(const GakuMotion::GuiContext& ctx)
	{
		(void)ctx.player;
		(void)ctx.lookAt;
		(void)ctx.config;
		(void)ctx.motions;
		(void)ctx.overrides;
		(void)ctx.masterOverridesEnabled;
		(void)ctx.selectedMotionIndex;
		(void)ctx.statusLine;
		(void)ctx.fnSelectMotion;
		(void)ctx.fnCycleMotion;
		(void)ctx.fnReloadOverrides;
		(void)ctx.fnToggleOverrides;
		(void)ctx.fnCycleGaze;
		(void)ctx.fnRefreshMotions;
	}

	void TouchGuiOverlay()
	{
		GakuMotion::GuiOverlay overlay;
		overlay.motionsRoot = "gakumas-local/motions";
		overlay.RefreshMotions();
		GakuMotion::GuiContext ctx;
		TouchGuiContext(ctx);
		if (overlay.showWindow)
		{
			overlay.Draw(ctx);
		}
	}
} // namespace
