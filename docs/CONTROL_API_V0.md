# Control API v0 原型

GakuMotion 提供进程内 C++ 服务 `gakumotion.control/v0`，供 Desktop GUI、Trailer 编排器、未来 VR UI 和 Director 适配器使用同一控制合同。现有 Desktop GUI 仍使用原有回调；本轮复用其主线程队列与播放器方法，未迁移全部 GUI。API 不改 GakuLoader ABI、播放器或音频时钟。

本候选已与 Clock 模块、Timeline tools 离线合入 `dev`；仍未提供两者的执行 Adapter 或外部 Director IPC。[整合说明](INTEGRATION_V0.md) 记录时间坐标、循环、异步应用与恢复缺口；合并不扩展本页列出的能力。

公共依赖只有 [GakuMotionControl.h](../include/GakuMotionControl.h)。合同使用固定宽度字段、值副本、固定容量 UTF-8 字符数组和函数表，不传递 Unity 指针、STL 所有权、消费者回调或可执行载荷。首版面向 x64、默认结构对齐；不可自行打包结构。

## 既有入口与 v0 范围

GUI 回调定义与绑定见 [GkGuiOverlay.hpp](../src/GkGuiOverlay.hpp)、[GakuMotion.hpp](../src/GakuMotion.hpp)；API 执行适配见 [GkControlCore.inl](../src/GkControlCore.inl)。

| 控制 | 既有入口与复用实现 | v0 命令语义 |
| --- | --- | --- |
| Load | `fnSelectMotion` → `QueueSelectMotion` → `SelectMotionByIndex` | 通过当前曲库精确 key 加载 Preview，不自动播放，不接受任意文件路径 |
| Play | `fnPlay` / `fnResume` → `QueuePlay` / `QueueResume` → `MotionPlayer::Play/Resume` | Paused 时续播；Playing 时保持；其余状态从已加载动作启动 |
| Pause | `fnPause` → `QueuePause` → `MotionPlayer::Pause` | Playing 转 Paused；已暂停时保持；其它状态返回 `NotReady` |
| Stop | `fnStop` → `QueueStop` → `Core::StopPerformance` | 沿原有停止/恢复链释放播放器、相机和舞台；推进 Session；已知相机/场景恢复未确认时返回 `Failed` |
| Seek | `fnSeek` → `QueueSeek` → `MotionPlayer::Seek`；另有帧定位和 scrub 回调 | 绝对演出秒数，范围 `[0, State.duration]`；复用现有暂停与音频同步行为 |
| Speed | `fnSetSpeed` → GUI `Speed` → `MotionPlayer::SetPlaybackSpeed` | 全局演出倍速 `[0.1, 5]`，保留现有音频降级规则 |
| Offset | `fnSetInitialPosition` → `QueueInitialPosition` → `SetInitialPositionOffset` | 角色初始位置 XYZ，单位米；不保存配置 |
| Camera | `fnSetCameraSettings` → `QueueCameraSettings` → `ApplyCameraSettings` | 部分、临时配置：启用、Track/Hold/Follow、LookAt/FollowMove、髋/胸/头与 XYZ 追随速率；其它设置保留，API 不自动保存 |
| Scene | `fnSetStagePreset` / `fnSetStageSettings` 等 → Stage GUI 队列 → `StageManager` | 只允许现有 `ApplyPreset` 的 Native、绿、蓝、黑、白、灰六项；其它舞台/节点/灯光入口不开放 |

每轨道的速度与秒偏移已经存在于导入/管理流程和 `info.json/track_timing`，见 [播放时序](../DevAgents/systems/runtime/PLAYBACK_TIMING.md)。v0 的 `Offset` 专指空间位置，未提供运行时改写每轨道时间偏移的命令。帧步进、scrub、导入、重载、静音等既有入口也不自动等于 v0 能力。

## 获取服务与查询

通过现有 `GakuHostApi::queryService` 查询 `ServiceName`、`Version`、`sizeof(Api)`。**产品原型名为 v0，首个 wire `Version` 是 1**：当前 Loader registry 不接受版本字段为 0；无需为此修改 Loader。服务表头和每个输入命令都检查版本/大小；v0 要求精确大小，不接受任意结构截断或附加字段。

```cpp
#include "GakuPlugin.h"          // 仅服务发现需要宿主合同
#include "GakuMotionControl.h"  // 领域消费者仅依赖此头文件

namespace ctl = GakuMotion::control::v0;

GakuStatus BindControl(const GakuHostApi& host, ctl::Api& api)
{
    if (!host.queryService) return GAKU_E_UNAVAILABLE;
    const auto status = host.queryService(host.hostContext, ctl::ServiceName,
        ctl::Version, sizeof(api), &api);
    if (status != GAKU_OK) return status;
    if (api.apiVersion != ctl::Version) return GAKU_E_VERSION;
    if (api.structSize != sizeof(api)) return GAKU_E_SIZE;
    return GAKU_OK;
}
```

所有函数可从任意线程调用。查询仅复制受锁保护的原生状态，不调用 Unity；它们可能短暂等待状态锁，不适合硬实时线程。`getState` 在停用后仍可返回历史快照，此时 `available=0`；`getCapabilities` 区分已实现的 `supportedMask` 与当前可提交的 `availableMask`。能力检查只是提示，执行前仍会复核。

`State` 包含播放状态、演出时间/时长、倍速、位置、相机配置、曲库数量以及 Session/动作/曲库 revision。`cameraAvailable` 表示现有相机桥快照可用；`cameraOwned` 表示接管状态，`cameraError` 包含桥或跟随目标错误；`cameraRestorePending`、`sceneRestorePending` 表示已知恢复尚未确认，不代表对画面的验收。Camera 能力位描述启用就绪情况：启用需要可用相机桥，未就绪时仍可提交禁用相机请求。

先读取 `State.motionCount` 和 `State.libraryRevision`，再用 `getMotion(revision, index, size, output)` 枚举曲目 key；曲库变动返回 `StaleMotion`，重新查询。key 超过固定容量时返回 `SizeMismatch`，不会截断成另一个可执行标识。

## 异步命令与 Session

命令用 `Command{}` 初始化；从新快照复制 `session`、`generation` 到 `motionGeneration`，并复制 `libraryRevision`。所有命令严格匹配 Session 与动作 generation，Load 另匹配曲库 revision；0 是有效值，不是通配符。Load/Stop 等改变状态后，应等待其结果并重新查询，再提交下一条命令；本版没有依赖批次或原子编排。

```cpp
std::uint64_t ticket = 0;
ctl::State state{};
if (api.getState(sizeof(state), &state) == ctl::Error::Ok && state.available) {
    ctl::Command command{};
    command.kind = ctl::CommandKind::Seek;
    command.session = state.session;
    command.motionGeneration = state.generation;
    command.libraryRevision = state.libraryRevision;
    command.value = 12.5f; // 需在 state.duration 内
    const auto accepted = api.submit(&command, &ticket);
    // accepted == Pending 且 ticket != 0：保存 ticket，在后续 UI/event tick 轮询。
}

// 后续 tick，ticket 是上一次成功接受的请求：
ctl::Result result{};
if (api.poll(ticket, sizeof(result), &result) == ctl::Error::Ok &&
    result.status != ctl::Error::Pending) {
    // 使用 result.status 处理成功/失败，message 仅供显示。
    api.release(ticket);
}
```

完整可编译消费者见 [public-header Mock 示例](../examples/control_api_mock.cpp)。它只实现并声明 Seek，演示 Pending、主线程 Tick、成功、Session 变更导致拒绝与结果释放；它不是另一个播放器。

`submit` 返回 `Pending` 才表示复制并接受命令，不表示执行成功。同步拒绝不分配 ticket，输出为 0。`poll` 返回 `Ok` 只表示结果成功复制，具体完成状态在 `Result.status`。终态结果必须 `release`；Pending 不能释放，返回 `Busy`。最多保留 **128 条**未释放记录，包含 Pending 与已完成结果；满额返回 `Busy`，不会静默驱逐旧结果或复用 ticket。

命令进入现有 `QueueGui` FIFO，仅在已准入的 Unity actor 主线程 drain 中执行；没有合格 actor tick 时不会执行有效的 Unity 变更。`onTick` 可提前拒绝过期/不再准入的请求。提交与执行分别复核 Session、动作、页面/场景准入和截图/分享保护。旧请求在切场景、换目标、停止或捕获边界后不能落到新 Session。开始捕获时单独推进 Control Session，保留原 `injectionSession` 供既有捕获恢复链使用；捕获恢复后也不会重放捕获前的命令。

停用插件后，排队请求变为终态 `Unavailable`，既有结果仍可查询/释放；服务函数在模块驻留期间有效，不支持热卸载后继续使用。处理 `StaleSession` / `StaleMotion` 时重新读取快照并重新判断用户意图，不自动盲重试。

## 完成状态的边界

常见错误包括参数/版本/大小错误、`Unsupported`、`NotFound`、`NotReady`、`Protected`、`StaleSession`、`StaleMotion`、`Busy` 和底层执行 `Failed`。错误码用于程序逻辑，`Result.message` 用于诊断。API 不放宽拍照/分享拦截、恢复确认或页面准入。

Camera/Scene 的 `Ok` 表示设置已接受，原生应用与渲染继续走正常 tick，不表示画面已经完成、恢复全部确认或用户验收。Native 预设只恢复其既有背景/天空盒行为，不重置 GUI 另设的灯光或节点状态。`State.scene` 可能读取到 GUI 选中的其它原生舞台索引；v0 提交仍仅接受 0–5。

源码验证由现有测试 runner 执行，包含 Control 单元测试与 public-header Mock 编译/运行；独立 worktree 可通过 runner 的 `ImGuiInclude` / `KineTextureInclude` 参数指定已有依赖。离线测试不能替代真实宿主加载、游戏内画面或 VR 兼容验收。

## 尚未支持

- 跨进程 IPC、HTTP/socket、网络鉴权与远程控制。外部进程 Director 尚需未来单独设计的窄桥；当前服务只能在同一进程消费。
- Timeline Editor、批量/原子命令、定时/按帧调度、命令取消、完成回调与同步等待。
- 运行时每轨道时间偏移/速度修改、导入与任意路径加载、配置保存。
- 角色目标选择、任意 Unity 指针、内存写入、任意代码执行。
- 任意场景节点、背景资源/舞台挂载、灯光编排和拍照/分享保护控制。
- 完整迁移 Desktop GUI 或提供成品 Trailer/VR/Director UI。它们可在进程内共享服务表，跨进程消费者需后续桥接。
