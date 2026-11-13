# Timeline Orchestrator v0

独立、基于状态的 GakuMotion Trailer 时间线原型，使用 Python 标准库运行。可见性、背景颜色及可选镜头模式均由 **PerformanceTime 的绝对值直接求出**；跳到任意 `t` 不需要先播放零秒或重放历史事件。

本目录没有接入游戏 DLL、当前 GUI、Unity 对象查找、Windows 音频时钟或截图/分享保护。Mock 的运行记录和 HTML 只证明原型行为，不代表游戏画面验收。

## 运行

在仓库根目录执行；当前环境已使用 Python 3.12.4 验证，无第三方依赖：

```powershell
python -m tools.timeline_orchestrator --log build/timeline-orchestrator/execution.jsonl --preview build/timeline-orchestrator/preview.html
python -m tools.timeline_orchestrator --help
python -m unittest discover -s tools/timeline_orchestrator/tests -v
```

省略 `--log` 时 JSONL 写到 stdout。`--timeline PATH` 可选择另一份编排；CLI 的 Mock 仅允许下面列出的六个背景逻辑 ID，未知 ID 会报错，输入时间线不会自动扩大宿主权限。

时间线输入、JSONL 输出与 HTML 输出必须使用不同路径；冲突在写文件前报错。

`execution.jsonl` 每行包含输入 transport 快照、完整目标状态、实际 Mock 状态、相机 clear flags 及本步 Control API 记录。默认 19 步脚本覆盖播放、大幅前跳、暂停、暂停中倒退 Seek、恢复播放、宿主场景切换、Loop 边界和 Stop 恢复。脚本使用固定时间输入，不睡眠、不读取系统时钟；换用其他时长时按比例缩放演示时间。

`preview.html` 是可直接在浏览器打开的自包含离线预览。滑块选择引擎预先独立求值的 257 个采样点；Loop 开关可比较末尾状态与回到零秒的状态。卡片表示逻辑可见性，背景只示意 RGBA 数值，不模拟 Unity 层级或色彩管线。

## 最小 JSON 格式

完整可运行示例见 [trailer_scene_transition.json](examples/trailer_scene_transition.json)。最小分组编排为：

```json
{
  "version": 1,
  "duration": 16,
  "groups": {"set": ["prop.banner", "prop.panel"]},
  "tracks": [
    {
      "kind": "visibility",
      "group": "set",
      "keys": [
        {"time": 0, "value": true},
        {"time": 2, "value": false, "stagger": 0.5},
        {"time": 10, "value": true, "stagger": 0.5, "order": "reverse"}
      ]
    }
  ]
}
```

| 字段 | 含义 |
| --- | --- |
| `version` | 固定整数 `1`。 |
| `duration` | 正有限秒数，定义 Clamp/Loop 的时间区间。 |
| `groups` | 可省略；分组名到有序、不重复逻辑 ID 列表的映射。 |
| `tracks` | 非空状态轨道数组。每个轨道都必须在 `time: 0` 明确初态。 |
| `kind: visibility` | `object` 或 `group` 二选一；`value` 为布尔值，按阶跃保持。 |
| `stagger` | 可见性键内可选的非负间隔秒数，默认 `0`；首键不能错开。 |
| `order` | 可见性键内 `forward`（默认）或 `reverse`，依分组顺序逐个生效。 |
| `kind: background` | 全局背景色；值为 `[r,g,b,a]`，各分量在 `[0,1]`。 |
| `interpolation` | 仅背景色可选：`step`（默认）或 RGBA 分量 `linear`。 |
| `kind: camera_mode` | 可选，全局阶跃状态；仅 `Track`、`Hold`、`Follow`。 |

同一物件只能属于一个可见性轨道；分组可重复引用物件，但被控制轨道实际展开后不能重叠。同类全局轨道只能有一条。不声明的属性不由编排接管：`DesiredState.background/camera_mode` 为 `None` 表示未接管，不是重设默认值。

键时间必须严格递增，均在 `[0,duration]` 内；逐个展开后的时间也必须在时长内，且每个物件的键不能交叉或重合。到达键时间时新状态立即生效。非法字段、类型、非有限数、重复 JSON 字段与未支持的轨道会被拒绝，不会静默忽略。

对象和分组只使用 ASCII 字母、数字、点、下划线、连字符构成的逻辑 ID。JSON 不能包含 Unity 路径、对象指针、任意方法调用或场景加载命令。

## 示例编排

示例中的背景组顺序为 `prop.banner / prop.panel / prop.desk / prop.chair / prop.plant / prop.shelf`，也是 Harness 明确注册的 Mock allowlist。

| PerformanceTime | 目标状态 |
| --- | --- |
| 0 秒 | 六个背景物件显示，镜头 `Track`；深色背景开始线性变黑。 |
| 2～4.5 秒 | 按列表顺序，每隔 0.5 秒隐藏一个物件。 |
| 6 秒 | 已全部隐藏，背景为黑色，镜头 `Hold`。 |
| 8 秒 | Harness 宿主演示切换场景；JSON 不执行场景加载。 |
| 9～14 秒 | 背景从黑色线性恢复到暖深色。 |
| 10～12.5 秒 | 按反向顺序，每隔 0.5 秒显示一个物件。 |
| 14 秒 | 镜头回到 `Track`；末尾继续保持全部状态。 |

可见性是逐个切换布尔状态，没有材质透明度淡入淡出。物件名称只是 Mock 逻辑 ID，不声称对应某个游戏场景中的真实节点。

## 求值与生命周期

[core.py](core.py) 在加载时把分组错开编译为每个物件的独立状态轨道；`Timeline.evaluate(t, loop=False)` 用二分定位该时刻的键。求值不读取先前状态，不保存事件游标，也不累计 `deltaTime`。输入文档与输出状态相互隔离，求值结果不可修改编译后的轨道。

- **任意 Seek**：直接求值 `t`；首次调用就位于中间、倒退、多圈跨越和连续前进使用同一路径。
- **负时间 / 末尾**：负数先夹到零；不 Loop 时超过时长保持末尾，Loop 时用 `t % duration`，精确终点等于零秒。
- **Pause / Preview**：仍按宿主给出的时间求值；普通暂停由宿主冻结 PerformanceTime，暂停中 Seek 则应用新时刻。编排不自行推进时钟。
- **Stop**：恢复首次接管前捕获的真实值并释放所有权，绝不以 JSON 的零秒初态代替恢复。Harness 初始就有隐藏物件，并使用与时间线不同的背景色和镜头模式。
- **重复 Stop**：没有持有 baseline 时不写状态。
- **再次开始**：重新捕获当时的宿主值，允许用户在两次编排之间改变原生状态。
- **场景切换**：宿主在旧场景仍有效时先 `suspend()` 恢复并释放；pending 帧不写入时间线状态。宿主更换绑定和 `scene_epoch`，ready 后重新 capture，再直接应用当前时间状态。
- **突然换代 / 对象已卸载**：旧 token 不写入新场景。若未提前 suspend，无法声称旧对象已成功恢复；宿主需安排正常卸载顺序。其他恢复失败会保留 token 并抛出错误，允许重试。

此处的 `scene_epoch` 是宿主提供的不透明身份，不能由时间线指定。宿主加载耗时与 PerformanceTime 如何冻结或恢复，由未来适配器负责；Harness 在切换前后均保持 8 秒，展示无需从零重放即可正确绑定新场景。

本独立原型的 `loop=True` 按 JSON `duration` 取模并保留越界余量；[Clock 原型](../../DevAgents/systems/runtime/PERFORMANCE_CLOCK_V0.md) 的 `RestartLoop` 则到尾归零并丢弃余量。接入时由单一宿主 transport 负责回绕，向 Timeline 传入回绕后的 PerformanceTime 并使用 `loop=False`；时间线只求值，不自行推进或再次回绕。JSON 时长仍限制状态采样范围，宿主需显式决定它与表演周期的关系。

## Mock 宿主契约与未来适配缺口

[control.py](control.py) 定义 `ControlAPI` Protocol、`TransportFrame`、`Baseline` 和生命周期层 `Orchestrator`。此 Python Protocol 不是 [GakuMotionControl.h](../../include/GakuMotionControl.h) 的绑定，也不是正式 IPC 协议：

```text
TransportFrame(performance_time, status, scene_epoch, loop=False, scene_ready=True)
  status = playing | paused | preview | stopped

capture(scope, scene_epoch) -> Baseline(scene_epoch, state, opaque)
apply(complete_desired_state, scene_epoch) -> None
restore(baseline) -> None

Orchestrator(timeline, control).update(frame) -> DesiredState | None
Orchestrator.stop() / suspend()
```

宿主以完整快照驱动 `update()`，不向编排暴露音频后端。`scope` 由编译结果描述需要接管的属性，**不代表授权**；适配器必须用独立 allowlist 检查它，再捕获全部属性后才写入。`opaque` 为适配器保留恢复用的数据，例如相机 clear flags、对象身份和租约；不能来自 JSON。

Mock 也模拟相机 clear flags：capture 将其保存到 `Baseline.opaque`，应用背景色时切换为 `SolidColor`，restore 还原捕获值。JSONL 中的 `camera_clear_flags` 使这部分恢复过程可检查；它仍不是对真实 Unity 相机的验证。

真实适配器应在 Unity 主线程执行 capture/apply/restore，写入前验证所有目标及场景身份；对完整 scope 应用绝对、幂等的状态，并明确处理批次失败，避免部分成功被当成完整应用。Capture 失败不能填造默认值。可见性对应 `activeSelf`；建议注册互不嵌套的背景根，避免隐藏父节点令子节点 `true` 仍不可见。角色、碰撞/边界、UI、系统和受保护对象不进入 allowlist。

Control ticket 的 `Pending` 仅表示入队，不能映射为同步 `apply` 已完成；未来 Adapter 需要执行回执、过期帧失效和结果释放。`scene_epoch` 与因 Stop、目标或截图保护变化而失效的 Control session 必须分别管理。普通 Control Stop 也不能替代本协议要求的编排基线恢复。

当前源码接入依据如下；本目录仅提供 Python 契约和 Mock，没有改写这些入口：

| 拥有源码 | 已存在的能力 / 未来适配方式 |
| --- | --- |
| [GkMotionPlayer.hpp](../../src/GkMotionPlayer.hpp) | `MotionPlayer::currentTime` 是已注释的 performance clock；`PlaybackState` 含 Stopped/Playing/Paused/Preview。发布此时间及状态，不采用各文件的 `MotionSourceTime()` / `CameraSourceTime()`。 |
| [GkTrackTiming.hpp](../../src/GkTrackTiming.hpp) | `PerformanceTiming` 和 `TrackSourceTime()` 定义各文件相对 performance clock 的速度/偏移；编排属于统一表演时间，不套用单文件映射。 |
| [GkStageManager.hpp](../../src/GkStageManager.hpp) | `StageCandidate` 已有 `instanceId/generation/originalActive`；`StageUnityApi` 有 `fnGeneration/fnMatches`。将经批准候选封装为逻辑 ID，不开放任意查找，也不用会随重扫变化的列表 index 作为外部身份。 |
| [GkStageRuntime.hpp](../../src/GkStageRuntime.hpp) | `StageLoader::Matches()` 验证 generation 和新鲜枚举中的对象；真实绑定应遵守既有身份与主线程边界。 |
| [GkStageManager.hpp](../../src/GkStageManager.hpp) | `RestoreForSuspension()` 精确恢复相机 clear flags 与背景色并读回验证；未来 `Baseline.opaque` 应保留两者，不强制写 Skybox 默认值。 |
| [GkCamera.hpp](../../src/GkCamera.hpp) | 现有 `CameraMode` 为 Track/Hold/Follow，原型沿用名称；生产模式应用仍需宿主提供有限入口。 |
| [GakuMotion.hpp](../../src/GakuMotion.hpp) | `Core::AbandonScene()` 会停止播放器并丢弃旧场景绑定。未来持续跨场景编排需独立保留编排会话，并以宿主 ready 信号重建绑定，不能假定播放器自动跨场景延续。 |

仓库已包含独立 [Performance Clock 模块](../../DevAgents/systems/runtime/PERFORMANCE_CLOCK_V0.md) 与进程内 [Control API v0](../../docs/CONTROL_API_V0.md)；本目录仍无互联 Adapter。现有 Control v0 不提供对象可见性、连续 RGBA、完整 scope 原子应用及基线租约，不能直接满足本原型。本目录不新增 Hook、网络服务或 GUI。

## 验证边界

测试覆盖直接首次 Seek、倒退与随机时间、正反序 stagger 的精确边界、颜色插值、Loop 取模、暂停中 Seek、真实 baseline 恢复、重复 Stop、再次开始、场景未就绪与身份过期、未知对象和格式拒绝。Harness 额外输出跨场景执行记录；HTML 复用相同求值器，不另写 JavaScript 时间线引擎。

未来仍需实现和验证游戏主线程 adapter、真实 allowlist 映射、场景加载协同、写入/恢复回执及游戏录制效果。完整 Timeline Editor、任意 Unity 访问和截图/分享保护修改不在 v0 范围内。
