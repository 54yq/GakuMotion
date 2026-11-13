# Performance Clock v0

这是独立、进程内的 C++20 transport 原型。核心为 [GkPerformanceClock.hpp](../../../src/GkPerformanceClock.hpp)，Windows 默认绑定为 [GkPerformanceClockAdapter.hpp](../../../src/GkPerformanceClockAdapter.hpp)。现有生产播放器未切换到新接口；其时间状态、MCI 同步及 Tick 调用仍由原管线拥有。

## 时间坐标

`performance::PerformanceTime` 是 double 演出秒 `P`，不代表墙钟、音频设备位置或源动作帧。读取时间没有推进副作用。有效位置有限且非负；有限负数 Seek 夹到零，NaN/Infinity 被拒绝。Manual/FixedStep 不知道素材长度，没有时长上限；Windows Adapter 保留播放器的 `[0, TimelineDuration()]` 上限，返回的实际时间可通过 `Time()` 获取。

沿用 [现行时序](PLAYBACK_TIMING.md)，不改 `info.json` 或 `.gkclip`：

| 量 | 关系 |
| --- | --- |
| 全局播放速度 `R` | 本地推进 `P_next = P + elapsedSeconds * R`；不改变当前位置 |
| 素材速度 `v` | 每轨独立；相对真实经过时间的有效速度为 `R * v` |
| 已有 `offset_seconds = o` | 演出轴上的秒延时，正值后移、负值提前 |
| 素材采样时间 `S` | `max(0, (P - o) * v)`；`v=0` 保持首帧 |
| 若将 source offset 定义为素材轴加数 `b` | `S=max(0, P*v+b)`，等价 `b=-o*v`；v0 不新增这个持久化字段 |
| MCI 原速时音频反算 | `P = audioSourceSeconds / audioSpeed + audioOffset + syncCorrection` |

例如 `R=2, v=0.5, o=1`，演出从 `P=3` 开始时素材位置为 `1` 秒；真实经过一秒后 `P=5, S=2`。全局倍速只乘一次。素材采样继续由旧 `TrackSourceTime` 执行，Adapter 的 `LegacySourceTime` 显式保留 float 边界。既有源动作帧导航与 FixedStep 的输出采样序号是两种坐标。

## 接口与驱动

`performance::Clock` 统一提供 `Kind / Time / Seek / Pause / Resume / Paused / PlaybackRate / SetPlaybackRate`，不是冻结的插件 ABI。所有时钟使用现有全局速度范围 `[0.1, 5]`，有限输入按此夹取；非有限值返回 false。素材自己的零速规则不变。`Pause/Resume` 是命令，宿主可能因场景、音频打开门禁等拒绝；须读回逻辑状态，不能据此推断设备确实发声或静音。

| 实现 | 时间所有者与推进入口 | 暂停与显式定位 |
| --- | --- | --- |
| `Manual` | 自己存演出时间；`SetTime` 是绝对赋值，`Advance(elapsedSeconds)` 显式按全局速度推进，没有内部墙钟 | 默认暂停；Pause 冻结 Advance，Seek/SetTime 仍可定位且不恢复播放 |
| `FixedStep` | 自己存整数输出帧序号；`SetFrameRate({numerator, denominator})` 设置有理 FPS，`AdvanceFrames(count)` 前进，`SetFrameIndex(n)` 显式定位 | 默认暂停；Pause 冻结 AdvanceFrames，SetFrameIndex/Seek 仍可定位 |
| `AudioDriven` | 非 owning Backend 视图；Windows 默认工厂 `MakeMotionPlayerAudioDrivenClock(player)` 直接读写现有 MotionPlayer | 沿用现有 Play/Pause/Resume/Seek；没有第二个 Advance/Tick，生产宿主照常推进 |

Clock 故意没有共同的 Tick：Manual 的 elapsed、FixedStep 的帧数和 AudioDriven 的宿主更新不能混作同一种单位。所有访问在所属线程串行执行；Clock 不提供锁或线程切换。Backend 所指对象必须比 AudioDriven 活得更久；读回调不得抛异常，不得推进，必须返回有效时间和有限速度。默认绑定的 MotionPlayer 不可在视图存活期间被移动或销毁。

### FixedStep 的精确范围

默认 `60/1 fps`、`R=1`、零 anchor 时，`P(n)=n/60`。一般形式为：

`P(n) = anchorP + (n - anchorFrame) * fpsDenominator / fpsNumerator * R`

每次从整数序号计算，不累计 `1/fps`。例如 `30000/1001 fps` 的第 30000 帧为 1001 秒。Seek、实际速率/FPS 变化将当前 `P` 与输出序号重设为 anchor，当前位置不跳变；重复设置相同值不改变 anchor。`SetFrameIndex` 按最近 anchor 投影，不保存过去的速度区间，不能用来恢复历史变速片段。若反向投影得到负演出时间，则拒绝并保持原状态。负帧号、负推进数、帧号加法溢出及零 FPS 分子/分母也被拒绝。

这里的“精确”指整数帧坐标及无增量累计误差；double 秒仍有舍入，极大时间不保证可分辨相邻帧。Seek 可以保留非帧网格上的秒位置。它不改变 gkclip FPS/首末帧规则，不等同于 Unity 帧、物理步长或确定性物理。

### 循环

Manual/FixedStep 调用方可显式调用 `RestartLoop(clock, duration)`：到尾直接 Seek(0)，丢弃超出余量，保留暂停、速率和输出帧计数。FixedStep 在零点重设 anchor，下一帧继续推进；禁用循环时不调用此工具。非正/非有限 duration 被拒绝。

AudioDriven 返回 `HostOwned`，不会执行 Seek。现有 MotionPlayer::Tick 继续在尾部停止并重新打开音频；非循环继续 Stop 并设置 finished。这样不会用普通 Seek 偷换原来的循环音频生命周期。

## Windows 默认实现与接入方案

现有 MCI `waveaudio` 返回的是毫秒量化的素材位置；只有 `abs(R * audioSpeed - 1) <= 1e-6` 时才支持音频。其余速度、静音、延迟起点前、音频结束、无回调或未确认播放状态继续本地降级。起播/跳转/续播后三帧 settle、最后一帧最多三次采样均值、syncCorrection 和回退容差均由原 MotionPlayer 处理。现有容差实际允许至多约 10ms 小倒退，较大倒退保持；v0 没有将它改成严格单调时钟。

实际接入分三步，均尚未执行：

1. 在拥有 MotionPlayer 的主线程创建默认 AudioDriven 视图，先让新的只读消费者取 `Clock::Time()`。原 Core/宿主 Tick 保持唯一时间驱动者；不得额外调用 AdvanceClock。
2. 新消费者的控制操作通过同一视图转发，回读最终位置/状态。现有 scrub、源动作帧定位、finished、播放列表和音频循环仍走原播放器；不在两处缓存或写回演出时间。
3. 只有另行验证播放器、相机、表情消费者的完整时序及音频生命周期后，才考虑扩展采样入口以接收 Manual/FixedStep。v0 不提供“将其时间每帧写入 player.currentTime”的捷径，那会绕过 seek/settle、source offset 和循环状态。

源码核对位置：`src/GkMotionPlayer.hpp` 的 AdvanceClock/SeekPosition/Tick，`src/GkTrackTiming.hpp` 的 TrackSourceTime，`src/GakuMotionPlugin.cpp` 的 HelperAudioGetTime 与现有宿主 realDeltaTime。Adapter 不调用 MCI，不修改回调接线、音频输出、Unity 物理或渲染调度，也不含编辑器 UI。

## 独立 Harness

以下命令已在本轮 worktree 实跑。默认模式不依赖 KineTexture、ImGui、游戏或音频设备：

```powershell
powershell -NoProfile -File tests/run_performance_clock_harness.ps1
```

加 `-WithMotionPlayerAdapter` 会再编译真实 MotionPlayer Adapter 测试；需要 KineTexture 头文件。脚本支持 `-KineTextureInclude`、`-VisualStudioPath`、`-WindowsSdkRoot` 显式指定已有依赖/工具链。初始候选 worktree 未初始化子模块，当时通过 include 参数只读复用已有 checkout 的依赖；合入 inventory 主 checkout 后使用默认依赖路径复核通过。

Harness 使用 MSVC C++20 `/utf-8 /MT`，纯模块额外 `/W4 /WX`。结果为纯模块 59/0，含 Adapter 共 86/0（新增 27 项真实播放器断言）。覆盖 Pause、Seek/SetTime、循环边界和余量、九档倍速、半程变速/FPS/Seek 后连续性、10 万帧与直接求值一致、30000/1001 FPS、非法输入和计数溢出，以及旧播放器 settle、音频降级、暂停 seek、拖动冻结、原速循环 stop/reopen 和 finished。音频为脚本回调，未执行游戏内视听验收。

模块已与 Control / Timeline 离线合入 `dev`，生产时钟仍保持原路径；未部署、push 或 release。初始候选证据见 [阶段交接](../../handoffs/handoff_036_performance_clock_v0_20261104.md)，合并后的时间 owner、Loop 差异与验证范围见 [整合说明](../../../docs/INTEGRATION_V0.md)。
