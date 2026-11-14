#pragma once

// =============================================================================
// GuiSettingsPage.hpp — 设置页面（物理引擎 / 热键一览 / 开发者模式开关）
// =============================================================================

#include "GuiOverlayCommon.hpp"
#include "../GkGuiOverlay.hpp"

#ifdef GKMOTION_GUI_ACTIVE
namespace GakuMotion
{
	inline void DrawPhysicsDiagnostics(const GuiContext& ctx)
	{
		ImGui::TextUnformatted("物理运行监控");
		const auto monitorState = ClassifyPhysicsMonitor(ctx.physicsMonitor, ctx.physicsMonitorFresh);
		const auto& monitor = ctx.physicsMonitor;
		if (monitor.endLoopAttempts) {
			ImGui::Text("结束循环尝试：%llu 次", static_cast<unsigned long long>(monitor.endLoopAttempts));
			ImGui::TextWrapped("%s", live_end_loop::StatusText(monitor.endLoopStatus));
		}
		if (ctx.liveViewingConfirmed)
		{
			ImGui::TextUnformatted("Live 调度监控（本进程累计）");
			ImGui::Text("动画推进探针：%s / %s；结束检查 / 回调：%s / %s",
				monitor.scheduleEvalDeltaHook ? "步长已接入" : "步长未接入", monitor.scheduleEvalPlainHook ? "无参已接入" : "无参未接入",
				monitor.scheduleEndCheckHook ? "已接入" : "未接入", monitor.scheduleEndHook ? "已接入" : "未接入");
			ImGui::TextWrapped("顺序：正常播放 / 暂停 / 接管。监控统计当前识别对象；结束前循环使用提前跳转，不拦截结束回调。计数为零不代表没有其他推进路径。");
			ImGui::Text("带步长 Evaluate：%llu / %llu / %llu", static_cast<unsigned long long>(monitor.scheduleEvalDelta[0]), static_cast<unsigned long long>(monitor.scheduleEvalDelta[1]), static_cast<unsigned long long>(monitor.scheduleEvalDelta[2]));
			ImGui::Text("无参 Evaluate：%llu / %llu / %llu", static_cast<unsigned long long>(monitor.scheduleEvalPlain[0]), static_cast<unsigned long long>(monitor.scheduleEvalPlain[1]), static_cast<unsigned long long>(monitor.scheduleEvalPlain[2]));
			ImGui::Text("结束检查：%llu / %llu / %llu；回调：%llu / %llu / %llu",
				static_cast<unsigned long long>(monitor.scheduleChecks[0]), static_cast<unsigned long long>(monitor.scheduleChecks[1]), static_cast<unsigned long long>(monitor.scheduleChecks[2]),
				static_cast<unsigned long long>(monitor.scheduleEnds[0]), static_cast<unsigned long long>(monitor.scheduleEnds[1]), static_cast<unsigned long long>(monitor.scheduleEnds[2]));
			ImGui::Text("最近步长 %.5f 秒；结束检查时间 %.3f 秒", monitor.scheduleLastDelta, monitor.scheduleLastEndTime);
			ImGui::Text("推进时序样本：%u / %u 已返回（最多 64）", monitor.scheduleTimingCompleted, monitor.scheduleTimingSamples);
			ImGui::Spacing();
			ImGui::TextUnformatted("原生 Live：只读采样");
			if (!ctx.physicsLiveObservationCurrent || monitor.liveObserveState == 0)
				ImGui::TextWrapped("等待原生 Live 播放，尚未采样。");
			else if (monitor.liveObserveState < 0)
				ImGui::TextWrapped("只读采样不可用；无法判断原生 Live 物理状态。");
			else
			{
				ImGui::Text("%s：%.1f 秒", monitor.liveObserveState == 1 ? "采样中" : (monitor.liveObserveState == 3 ? "采样提前结束" : "采样已结束"), monitor.liveObserveSeconds);
				if (monitor.liveDeltaKnown) ImGui::Text("动画时间调用：%llu 次", static_cast<unsigned long long>(monitor.liveDeltaCalls));
				else ImGui::TextWrapped("动画时间调用次数不可用。");
				ImGui::Text("Live 进度：%.2f → %.2f 秒", monitor.liveTimeStart, monitor.liveTimeEnd);
				if (monitor.liveActorProbeResult == 0)
				{
					ImGui::Text("角色标识：%p", reinterpret_cast<void*>(monitor.liveActorToken));
					ImGui::Text("角色 Swing 匹配：%llu，其他：%llu，标识不可读：%llu",
						static_cast<unsigned long long>(monitor.liveActorMatches), static_cast<unsigned long long>(monitor.liveActorOther),
						static_cast<unsigned long long>(monitor.liveActorBad));
					ImGui::TextWrapped("匹配基于该角色 Rig 的两项任务标识；计数为零不能证明角色没有物理。");
				}
				else ImGui::Text("角色 Swing 归属尚未确认（状态 %d）。", monitor.liveActorProbeResult);
				if (monitor.liveObserveState >= 2 && monitor.liveSwingKnown)
				{
					ImGui::Text("Swing 特征调用：%llu 次", static_cast<unsigned long long>(monitor.liveSwingCalls));
					ImGui::Text("动画步长范围：%.5f ~ %.5f 秒", monitor.liveSwingMinDt, monitor.liveSwingMaxDt);
				}
				else if (monitor.liveObserveState >= 2) ImGui::TextWrapped("未能从采样记录确认 Swing 调用或步长。");
				ImGui::TextWrapped("正常播放后采样最多 5 秒；暂停、明显跳转或接管会提前结束。Swing 记录为进程累计数据，不能确认所属角色或证明物理效果正常。");
			}
		}
		if (monitor.photoGraphSampled)
		{
			ImGui::TextUnformatted("拍摄现场角色动画图（本次进程最近一次采样）：");
			if (!monitor.photoGraphPlayerFound) ImGui::TextWrapped("未找到角色动画播放器。");
			else if (!monitor.photoGraphGetterFound || monitor.photoGraphValid < 0) ImGui::TextWrapped("播放器已找到，图状态读取失败。");
			else if (!monitor.photoGraphValid) ImGui::TextWrapped("图无效。");
			else
			{
				const char* mode = monitor.photoGraphUpdateMode == 0 ? "音频时钟" : monitor.photoGraphUpdateMode == 1 ? "游戏时间" :
					monitor.photoGraphUpdateMode == 2 ? "不受缩放时间" : monitor.photoGraphUpdateMode == 3 ? "手动" : "未确认";
				ImGui::Text("图有效，自动播放：%s；更新时间：%s", monitor.photoGraphPlaying < 0 ? "未确认" :
					monitor.photoGraphPlaying ? "运行" : "未运行", mode);
			}
		}
		ImGui::TextUnformatted("GakuMotion 动作：");
		switch (monitorState)
		{
		case PhysicsMonitorState::NoSample: ImGui::TextWrapped("等待动作播放，尚无监控数据。"); break;
		case PhysicsMonitorState::Stale: ImGui::TextWrapped("当前没有新的播放数据；下方为上次记录。"); break;
		case PhysicsMonitorState::Off: ImGui::TextWrapped("物理已关闭。"); break;
		case PhysicsMonitorState::Builtin: ImGui::TextWrapped("当前使用内建物理。"); break;
		case PhysicsMonitorState::Native: ImGui::TextWrapped("原生物理正在处理当前角色。"); break;
		case PhysicsMonitorState::Waiting: ImGui::TextWrapped("原生物理尚未确认生效，当前回退到内建物理。"); break;
		case PhysicsMonitorState::Fault: ImGui::TextWrapped("原生物理接口发生错误，当前回退到内建物理。"); break;
		}
		if (monitor.observed)
		{
			ImGui::Text("记录场景：%s", monitor.live ? "Live 接管" : "其他场景");
			ImGui::Text("Swing 调用：%llu", static_cast<unsigned long long>(monitor.swingCalls));
			ImGui::Text("匹配并处理：%llu", static_cast<unsigned long long>(monitor.matched));
			ImGui::Text("角色不匹配：%llu    数据过期：%llu", static_cast<unsigned long long>(monitor.identitySkips),
				static_cast<unsigned long long>(monitor.expired));
			ImGui::Text("处理失败：%llu    上次写入骨骼：%llu", static_cast<unsigned long long>(monitor.failed),
				static_cast<unsigned long long>(monitor.writes));
			ImGui::TextWrapped("调用、匹配与失败次数为本次游戏进程的累计值，可能包含之前的角色或场景；0 次不代表角色匹配已通过。");
		}
		if (ctx.liveViewingConfirmed)
		{
			if (!ctx.physicsGraphCurrent) ImGui::TextWrapped("角色动画图：等待当前阶段的检测。");
			else if (!monitor.graphPlayerFound) ImGui::TextWrapped("角色动画图：尚未找到角色动画播放器。");
			else if (!monitor.graphGetterFound || monitor.graphValid < 0) ImGui::TextWrapped("角色动画图：播放器已找到，图状态读取失败。");
			else if (!monitor.graphValid) ImGui::TextWrapped("角色动画图：读取成功，但图无效。");
			else if (monitor.graphPlaying < 0) ImGui::TextWrapped("角色动画图：有效，运行状态读取失败。");
			else ImGui::Text("角色动画图：有效，自动播放%s", monitor.graphPlaying ? "中" : "未运行");
			if (ctx.physicsGraphCurrent && monitor.graphValid == 1)
			{
				const char* mode = monitor.graphUpdateMode == 0 ? "音频时钟" : monitor.graphUpdateMode == 1 ? "游戏时间" :
					monitor.graphUpdateMode == 2 ? "不受缩放时间" : monitor.graphUpdateMode == 3 ? "手动" : "未确认";
				ImGui::Text("图的更新时间模式：%s", mode);
				if (monitor.graphOutputs < 0) ImGui::TextWrapped("图的第一个输出读取失败，角色归属未确认。");
				else if (monitor.graphActorTarget < 0) ImGui::TextWrapped("图的第一个输出已读取，目标角色未确认。");
				else ImGui::Text("图的第一个输出%s", monitor.graphActorTarget ? "指向当前角色" : "不指向当前角色");
				ImGui::TextWrapped("仅检查第一个输出，不能证明整张图只影响此角色。");
				ImGui::TextWrapped("自动播放未运行不代表没有手动求值。");
			}
		}

	}

	inline void DrawSettingsPage(GuiOverlay& overlay, const GuiContext& ctx)
	{
		// =========================================================================
		// 1. 物理引擎选择
		// =========================================================================
		ImGui::TextUnformatted("物理引擎");

		// 与 RuntimeConfig / Core 使用相同的后端编号。
		static const char* kPhysicsOptions[] = {
			"关闭",
			"内建物理",
			"原生物理（默认，实验性）"
		};

		int currentChoice = (std::clamp)(ctx.physicsBackend, 0, 2);

		ImGui::SetNextItemWidth((std::min)(240.0f, ImGui::GetContentRegionAvail().x));
		if (ImGui::Combo("##physics_engine_select", &currentChoice, kPhysicsOptions, IM_ARRAYSIZE(kPhysicsOptions)))
		{
			if (ctx.fnSetPhysicsBackend) ctx.fnSetPhysicsBackend(ctx.self, currentChoice);
		}
		ImGui::TextWrapped("选择在本次运行中生效；重启或控制文件变更后读取 control.json 的设置。");
		if (ctx.physicsBackend == 2)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, GuiTheme::WarningColor());
			ImGui::TextWrapped("已选择原生物理实验；原生接口不可用时回退到内建物理。选择后端不代表原生物理已实际运行。");
			ImGui::PopStyleColor();
		}
		else if (ctx.physicsBackend == 1)
			ImGui::TextWrapped("内建弹簧骨为保底方案，不包含官方碰撞模型。");

		ImGui::Spacing();

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// =========================================================================
		// 2. 快捷键一览
		// =========================================================================
		ImGui::TextUnformatted("常用快捷键");

		const ImGuiTableFlags hotkeyTableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
		                                         ImGuiTableFlags_SizingStretchProp;
		if (ImGui::BeginTable("##hotkeys_table", 3, hotkeyTableFlags))
		{
			ImGui::TableSetupColumn("快捷键", ImGuiTableColumnFlags_WidthFixed, 110.0f);
			ImGui::TableSetupColumn("功能", ImGuiTableColumnFlags_WidthFixed, 150.0f);
			ImGui::TableSetupColumn("说明", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			auto AddRow = [](const char* key, const char* func, const char* desc)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", key);
				ImGui::TableSetColumnIndex(1);
				ImGui::TextWrapped("%s", func);
				ImGui::TableSetColumnIndex(2);
				ImGui::TextWrapped("%s", desc);
			};

			AddRow("Ctrl + M", "唤出控制面板", "打开或唤回 GakuMotion 控制窗口");
			AddRow("F9", "播放 / 暂停 / 继续", "控制当前曲目；暂停后从当前位置继续");
			AddRow("F10", "停止并恢复游戏状态", "停止表演，复位进度并恢复原生角色控制");
			AddRow(", / .", "上一曲 / 下一曲", "切换播放列表中的曲目");
			AddRow("- / =", "后退 / 前进 5 秒", "相对当前位置跳转");
			AddRow("Shift + F11", "重新加载骨骼微调", "从磁盘重新载入骨骼覆盖设置");
			AddRow("Ctrl + F11", "切换骨骼微调启用", "开启或暂时旁路骨骼覆盖");

			ImGui::EndTable();
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// =========================================================================
		// 3. 开发者模式开关
		// =========================================================================
		ImGui::TextUnformatted("开发者选项");
		if (ImGui::Checkbox("开发者模式", &overlay.developerMode))
		{
			// 开启后侧边栏将显示 [开发] 选项
		}
		ImGui::TextWrapped("开启后在侧边栏显示 [开发] 页，包含诊断、鞋底测量、探针与实验性开关。");
	}
} // namespace GakuMotion
#endif
