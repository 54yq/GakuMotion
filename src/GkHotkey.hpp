#pragma once

// =============================================================================
// GkHotkey.hpp — 可靠的热键「按下沿」检测
// =============================================================================
// 背景（实机故障的根因）：
//   原实现在每个新物理帧里用 `GetAsyncKeyState(vk) & 1` 判按键。低位 bit 的
//   语义是「自上次调用以来该键是否被按下过」，它有三个致命性质：
//     1. 它是**粘滞**的：按键状态变化只写 bit，不在你读的时候清。游戏窗口失焦、
//        掉帧、切场景导致若干帧没有 OnLateUpdate 时，用户在别的程序里敲的
//        `.` / `,` 会把 bit 置上；等帧循环恢复，第一次读取就立刻触发一次
//        「幽灵切换」。实机日志中「暂停一段时间后无人操作却出现
//        switch prev + switch next」正是这个成因。
//     2. 它**无视前台窗口**：GetAsyncKeyState 报的是物理键态，不是「发给本窗口
//        的键」。玩家在浏览器/编辑器里打逗号句号同样会切歌。
//     3. 它把**按住时的系统自动重复**当成一串新按下，长按 = 连环切歌。
//
// 契约：
//   * 只认**上升沿**（物理按下瞬间）—— 自动重复天然被排除；
//   * 焦点状态变化时清空沿状态基线 —— 失焦期间的按键永不补发；
//   * 焦点不在本进程时所有按键返回 false（可选择记录，供探针使用）。
//
// 纯逻辑，不直接调用 Win32：按键状态由调用方注入（生产用
// ::GetAsyncKeyState，测试用手写序列），因此可在离线单测里穷举验证。
// =============================================================================

#include <array>
#include <cstddef>

namespace GakuMotion
{
	class HotkeyGate
	{
	public:
		static constexpr int kMaxKeys = 256;

		// 焦点状态变化时调用。切换瞬间标记「需要重新采样基线」。
		void SetForegroundWindowFocused(bool focused) noexcept
		{
			if (focused == m_focused) return;
			m_focused = focused;
			m_focusDirty = true;
		}

		bool ForegroundFocused() const noexcept { return m_focused; }

		// 复位沿状态基线（例如刚装载插件）。
		void Reset() noexcept
		{
			m_down.fill(false);
			m_focusDirty = true;
		}

		// 每个采样帧开头调用一次。焦点切换后的第一帧是「基线重建帧」：
		// 该帧内所有按键只更新基线、不产生触发（即使按键此刻正好被按住）。
		// 没有这一步，玩家在 Alt-Tab 回来的瞬间正好压着 `,` 就会被误判成新按下。
		void NewFrame() noexcept
		{
			if (m_focusDirty)
			{
				m_down.fill(false);
				m_priming = true;
				m_focusDirty = false;
			}
			else
			{
				m_priming = false;
			}
		}

		// 注入某键当前是否物理按下，返回「本次是否构成一次新的按下沿」。
		// 注意：即使本帧不允许触发（失焦/基线重建），也必须照常更新内部基线，
		// 否则焦点切回时会补发一串历史按键。
		bool Pressed(int vk, bool down) noexcept
		{
			if (vk < 0 || vk >= kMaxKeys) return false;
			const bool prev = m_down[static_cast<std::size_t>(vk)];
			m_down[static_cast<std::size_t>(vk)] = down;
			if (!m_focused || m_priming) return false;
			return down && !prev;
		}

		// 只读查询当前基线（诊断用）。
		bool BaselineDown(int vk) const noexcept
		{
			if (vk < 0 || vk >= kMaxKeys) return false;
			return m_down[static_cast<std::size_t>(vk)];
		}

	private:
		std::array<bool, kMaxKeys> m_down{};
		bool m_focused{true};
		bool m_focusDirty{false};
		bool m_priming{false};
	};
} // namespace GakuMotion
