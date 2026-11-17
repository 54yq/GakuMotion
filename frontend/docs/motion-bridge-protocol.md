# GakuMotion Web Panel Bridge 协议（v1，含第二阶段扩展 2026-11-15）

前端权威定义：`frontend/src/motion/protocol.ts`。本文档供 GakuMotion native
web-panel 服务（`gakumotion.web-panel/v1`）与 GakuLoader 转发层对齐。
传输核心在 `frontend/src/bridge/core.ts`。

## 1. Wire 形状（必须严格一致）

所有消息为 JSON 对象，`v` 必须等于 `1`，否则前端直接忽略。

```jsonc
// 请求（React → native）
{ "v": 1, "id": 1, "type": "request", "method": "player.play", "params": {} }

// 成功响应（native → React，id 与请求一致）
{ "v": 1, "type": "response", "id": 1, "ok": true, "result": {} }

// 失败响应
{ "v": 1, "type": "response", "id": 1, "ok": false,
  "error": { "code": "stale_revision", "message": "曲库已变化", "details": {} } }

// 事件（native → React）
{ "v": 1, "type": "event", "name": "player.stateChanged", "payload": {}, "seq": 12, "sid": 1 }
```

- `seq`：每个 session 内单调递增（用于诊断，前端目前不据此排序）。
- `sid`：native session id。前端收到更大的 `sid` 会 reject 所有 pending 请求
  （错误码 `reset`）；收到更小的 `sid` 的事件被丢弃。
- `bridge.hello` 的 result 必须含 `{ "protocol": 1, "sessionId": <number> }`。
- 服务关闭/失效时推送 `{ "name": "bridge.closed", "payload": { "reason": "..." } }`。
- 请求默认 15s 超时（前端 `timeout`），支持 AbortSignal（`cancelled`）。

## 2. 能力协商

启动顺序：`bridge.hello` → `motion.hello` → `state.get`。

`motion.hello` result：

```jsonc
{
  "protocol": 1,
  "sessionId": 1,
  "pluginVersion": "0.1.1",
  "supportedMethods": ["bridge.hello", "bridge.ping", "motion.hello", "state.get", "player.play", "..."],
  "supportedEvents": ["player.stateChanged", "library.changed", "..."]
}
```

**前端只调用 `supportedMethods` 中列出的方法。** 未列出的方法在 UI 上显示
“待迁移/不可用”，不会发出请求。native 对未实现方法仍应返回
`{ "code": "not_implemented" }`（防御未知调用）。

## 3. 错误码

| code | 语义 |
|---|---|
| `unknown_method` | allow-list 之外的方法 |
| `not_implemented` | 已注册但尚未实现（待迁移） |
| `invalid_params` | 参数缺失/越界/格式错误 |
| `stale_revision` | libraryRevision 不匹配（曲库已变化） |
| `stale_token` | actor token/epoch 过期 |
| `not_allowed` | injectionAllowed=false 或权限不足 |
| `not_ready` | 播放器/镜头/场景接口未就绪 |
| `protected` | 播放中编辑保护等（issue#5 语义） |
| `busy` | 导入/刷新/对话框进行中 |
| `failed` | 操作执行失败（message 携带用户可读原因） |
| `internal` | 未分类异常 |

前端本地错误码（不由 native 产生）：`timeout`、`cancelled`、`closed`、`reset`、
`transport_error`、`no_webview`。

## 4. 状态快照 `state.get`

result（完整形状见 protocol.ts `MotionStateSnapshot`）：

```jsonc
{
  "permission": { "injectionAllowed": true, "notice": "" },
  "player": {
    "state": "stopped",            // stopped | playing | paused | preview
    "title": "未加载曲目",
    "time": 0,                      // 秒，连续时间
    "duration": 0,                  // 秒，TimelineDuration
    "totalFrames": 0,               // 采样帧数；末帧 = totalFrames - 1
    "fps": 60,
    "speed": 1,                     // 0.1..3
    "volume": 1,                    // 0..1（UI 显示 0-100%）
    "playMode": 1                   // 见 §6
  },
  "library": {
    "revision": 7,                  // libraryRevision，单调递增
    "entries": [ { "id": "folder-key", "title": "...", "directory": "gakumas-local/motions/...",
                   "durationSeconds": 200, "totalFrames": 12000,
                   "hasMotion": true, "hasFacial": false, "hasCamera": true, "hasAudio": true } ],
    "refreshing": false,
    "notice": ""
  },
  "loaded": {
    "motionKey": "",                // 当前加载曲目 key
    "generation": 0,                // loadedMotionGeneration
    "injectionSession": 0,          // 权限会话；变化时前端丢弃 scrub
    "sourceMissing": false,
    "sourceChanged": false
  },
  "playlist": { "mode": 1, "notice": "" },
  "actors": {
    "epoch": 3,                     // actorRosterEpoch
    "actors": [ { "token": 123, "label": "咲季", "selected": true } ],
    "targetLabel": "咲季",
    "boundLabel": "咲季"
  },
  "camera": {
    "settings": { /* CameraSettings，见 §7 */ },
    "available": true, "owned": false, "hasTrack": true,
    "error": "", "followTargetError": "", "notice": ""
  },
  "stage": { "settings": { /* StageSettings，见 §8 */ }, "sceneName": "", "statusNotice": "" },
  "facial": { "weights": { "overall": 1, "brow": 1, "eye": 1, "mouth": 1 }, "notice": "" },
  "position": { "offset": { "x": 0, "y": 0, "z": 0 }, "notice": "" },
  "ensemble": {
    "settings": { "enabled": false, "masterSlot": 0, "cameraSlot": 0,
                  "slots": [ { "enabled": false, "actorToken": 0, "actorHint": "", "motionKey": "",
                               "offset": { "x": 0, "y": 0, "z": 0 }, "yawDegrees": 0 } ] },
    "revision": 0, "actorEpoch": 0,
    "available": false, "verified": false, "probeEnabled": false, "active": false,
    "notice": "", "physics": ["", "", ""]
  },
  "physicsBackend": 2,              // 0 关闭 / 1 内建 / 2 原生（实验）
  "musicVolumeSupport": 0,          // <0 表示后端不支持独立音量
  "musicVolumeNotice": "",
  "importProgress": null,           // 或 { active, progress(0..1), statusText, error }
  "motionsRoot": "gakumas-local/motions"
}
```

## 5. 事件

| name | payload | 频率/时机 |
|---|---|---|
| `player.stateChanged` | PlayerState（同上） | 播放中 ~10Hz（onUiTick 节流）；命令后立即 |
| `permission.changed` | `{ injectionAllowed, notice }` | 权限翻转；false 时前端丢弃进行中的 scrub |
| `library.changed` | `{ revision, entries, refreshing, notice }` | 刷新/导入/删除后 |
| `loaded.changed` | `{ motionKey, generation, injectionSession, sourceMissing, sourceChanged }` | 加载/重载/源文件变化 |
| `playlist.changed` | `{ mode, notice }` | 模式/通知变化 |
| `actors.changed` | `{ epoch, actors, targetLabel, boundLabel }` | roster/选择变化 |
| `camera.changed` | `{ settings, available, owned, hasTrack, error, followTargetError, notice }` | 设置被 native 确认/外部变化（pending ack 依据） |
| `stage.changed` | `{ settings, sceneName, statusNotice }` | 舞台设置确认 |
| `facial.changed` | `{ weights, notice }` | 表情幅度变化/保存结果 |
| `position.changed` | `{ offset, notice }` | 初始位置变化/保存结果 |
| `ensemble.changed` | EnsembleSnapshot（同上） | ensemble 状态变化 |
| `settings.changed` | `{ physicsBackend }` | 物理后端变化 |
| `import.progress` | `{ active, finished, succeeded, progress, stage, statusText, errorDetail }` | 导入进度（见 §10） |
| `appearance.changed` | `{ theme, uiScale, dpiScale, scaleLimited }` | 宿主外观（可选转发） |
| `bridge.closed` | `{ reason }` | 服务关闭 |

## 6. 播放语义（与 ImGui 一致）

- `playMode`：`0=播放一次, 1=单曲循环, 2=列表循环, 3=随机播放`；
  ensemble 激活时 UI 只显示 `0=演出一次, 1=整场循环`（数值复用 0/1）。
  非法值按 1 处理。默认 1。
- `speed` 档位：`0.1, 0.2, 0.5, 0.8, 1.0, 1.2, 1.5, 2.0, 3.0`。
- `volume`：0..1 浮点。`player.setVolume` 拖动中调用；**松开时前端追加
  `player.saveVolume`**（commit 才保存）。
- 帧基准：**0-based**。`player.seekFrame { frame }` 末帧 = `totalFrames - 1`；
  越界返回 `invalid_params`。`player.stepFrame { delta: -1 | 1 }` 暂停并帧步。
- `player.seek { seconds }`：秒，可位于两采样帧之间；`> duration` 由前端拦截，
  native 也应拒绝（`invalid_params`）。
- scrub 三段式：`player.scrubBegin { seconds }`（冻结主时钟）→
  `player.scrubUpdate { seconds }`（拖动中）→ `player.scrubEnd { seconds }`（提交）。
  权限丢失/曲目切换时前端**丢弃** scrub（不再发 scrubEnd），native 侧也应在
  权限翻转时自行结束 scrub 状态。
- `player.stop`：归零并恢复游戏动作。`player.cycle { delta }`：ensemble 激活时
  前端禁用；native 返回 `protected`。
- `player.reload`：重新加载当前曲目，保留时间并暂停。

## 7. CameraSettings（与 GkCamera.hpp 数值一致）

```jsonc
{
  "enabled": true,
  "mode": 0,                        // CameraMode: Track=0, Hold=1(legacy), Follow=2
  "followBehavior": 1,              // LookAt=0, FollowMove=1
  "followTarget": 0,                // Hips=0, Chest=1, Head=2
  "followActor": false,
  "limitToScene": false,
  "boundsMin": { "x": -5, "y": 0.05, "z": -5 },
  "boundsMax": { "x": 5, "y": 5, "z": 5 },
  "followOffset": { "x": 0, "y": 0.6, "z": 3 },
  "targetOffset": { "x": 0, "y": 0.4, "z": 0 },
  "vmdOffset": { "x": 0, "y": 0, "z": 0 },
  "vmdRangeScale": 1,               // 0.3..1.0
  "followFactor": { "x": 1, "y": 1, "z": 1 },   // 0..1
  "followRate": { "x": 0.1, "y": 0.1, "z": 0.1 }, // 0..1
  "followFov": 45,                  // 15..90
  "srpFinalOverwriteFallback": false,
  "actorShadowExperimentMode": 0,   // 0 默认 / 1 锚定角色 / 2 旁路
  "transformTrack":      { "position": { "x": 0, "y": 0, "z": 0 },   "rotation": { "x": 0, "y": 0, "z": 0 } },
  "transformFollowMove": { "position": { "x": 0, "y": 0.6, "z": 3 }, "rotation": { "x": 0, "y": 0, "z": 0 } },
  "transformLookAt":     { "position": { "x": 0, "y": 0.6, "z": 3 }, "rotation": { "x": 0, "y": 0, "z": 0 } },
  "usePerModeTransforms": false
}
```

- UI 快捷模式映射：`游戏 → enabled=false`；`VMD → enabled=true, mode=Track`；
  `跟随 → mode=Follow, followBehavior=FollowMove, followActor=true`；
  `注视 → mode=Follow, followBehavior=LookAt, followActor=true`。
- `camera.set { settings }`：立即应用；native 确认后推 `camera.changed`，
  前端以“快照 settings 与 expected 深度相等”作为 pending ack。
- `camera.save`：写 camera.json。
- legacy `usePerModeTransforms=false` 时前端自动把 vmdOffset/followOffset
  同步进对应 transform 并升级（与 ImGui 相同）。

## 8. StageSettings（与 GkStageManager.hpp 一致）

```jsonc
{
  "hideBackground": false,
  "greenScreenActive": false,
  "greenScreenColor": [0, 1, 0, 1],   // RGBA 0..1
  "screenDistance": 4, "screenScale": 35, "screenHeightOffset": 1,
  "overrideCameraClear": false,
  "selectedPresetIndex": 0,
  "customStageKey": "", "loadReplacementKey": "",
  "stageFloorYOffset": 0, "stageYawOffset": 0, "stagePosXOffset": 0, "stagePosZOffset": 0,
  "lightingEffectsEnabled": true,
  "customNodeControl": false
}
```

**跨域联动必须由 native 在 `stage.set` 内保留**：
- `lightingEffectsEnabled` 变化 → 同步 `cameraSettings.actorShadowExperimentMode = enabled ? 1 : 2`
  并推 `camera.changed`（ImGui 原来在 UI 层做，迁移后下沉到 native，前端只发 stage.set）。
- `hideBackground=true` → `greenScreenActive=true, overrideCameraClear=true`；
  `hideBackground=false` → 两者清零并恢复 skybox（RestoreCameraSkybox）。
  前端会同时提交这些字段，native 以提交值为准并执行 skybox 恢复副作用。
- 视觉舞台设置（`stage.set`）仅作用于当前运行态，不写入 `stage.json`；`stage.json` 仅由 `stage.setLoadReplacement` 持久化 `loadReplacementKey`。

## 9. Library 守卫与删除确认

- `library.select { key, autoplay, revision }`：revision 必须等于当前
  libraryRevision，否则 `stale_revision`。autoplay=true 双击播放/播放选中；
  false 加载预览。
- `library.delete { key, revision }`：
  1. 前端先弹确认模态，显示 key + directory + “永久删除整个曲目目录…无法撤销”。
  2. 模态打开期间若 `library.changed` 使 revision 变化 / key 消失 / directory
     变化 → 显示“曲库已变化，请取消后重新选择曲目。”并禁用确认按钮。
  3. 确认时携带**打开模态时记录的 revision**；native 不匹配返回 `stale_revision`。
  4. 导入/刷新进行中 native 返回 `busy`；播放中保护按 Core 语义返回 `protected`。
- `library.refresh`：重新扫描；完成后推 `library.changed`。
- `library.openFolder`：native ShellExecute 打开 motionsRoot。

## 10. 其余方法

| method | params | result | 备注 |
|---|---|---|---|
| `actors.selectTarget` | `{ token, epoch }` | `{}` | epoch 过期 → `stale_token` |
| `facial.setWeights` | `{ overall, brow, eye, mouth }` | `{}` | 各 0..2；直写当前驱动角色 |
| `facial.save` | `{}` | `{}` | 保存默认 |
| `position.set` | `{ x, y, z }` | `{}` | 各 -10..10 米；即时生效 |
| `position.save` | `{}` | `{}` | 保存为下次启动默认 |
| `settings.setPhysics` | `{ backend }` | `{}` | 0/1/2；本次运行生效 |
| `shell.setTextInputActive` | `{ active }` | `{}` | 宿主转发：editable focus 热键抑制 |
| `appearance.get` | `{}` | `{ theme, uiScale, dpiScale, scaleLimited }` | 可选宿主转发 |

### 第二阶段方法（capability 门控；未 advertise 前前端显示“待迁移/不可用”）

权威类型见 protocol.ts。以下为本轮定稿的形状（替换了旧占位）：

**文件对话框 / 导入 / 编辑**

| method | params | result |
|---|---|---|
| `dialog.pickFile` | `{ kind, multiple }` | `{ dialogId }` |
| `dialog.poll` | `{ dialogId }` | `{ state: pending/accepted/cancelled/failed, paths, notice }` |
| `library.probeFiles` | `{ paths }` | `{ classification: ImportClassification, probes: VmdProbe[] }` |
| `library.import` | `{ request: ImportRequest }` | `{ started }` |
| `library.startEdit` | `{ key, revision }` | `EditEntryInfo { key,title,hasFacial,hasCamera,hasAudio,timing }` |

- `kind`：0=全部演出文件(*.vmd;*.gkcamera;*.wav) 1=动作VMD 2=表情VMD 3=镜头(*.vmd;*.gkcamera)
  4=WAV 5=本地视频（studio 用）。过滤串与 GuiOverlayCommon.hpp 一致。
- `dialog.poll` 由前端以 ~200ms 轮询直到非 pending；同一时刻只允许一个对话框（busy）。
- `library.probeFiles`：classification 为 ClassifyImportFiles 语义（建议标题/四槽推荐/
  非 WAV 音频计数）；probes 为每个 .vmd/.gkcamera 的 VmdProbe（tendency:
  invalid/motionOnly/motionAndFacial/facialOnly/cameraOnly/empty；isGkCamera→[烘焙镜头]）。
- `ImportRequest`：`{ title, editKey(''=新增), motionFile, facialFile, cameraFile, audioFile,
  disableMorph, clearCamera, clearAudio, fps(0=auto), timing: PerformanceTiming }`。
  三态映射：新增 facial 自动=不传 morphVmd 也不 disableMorph；（不导入）=disableMorph；
  编辑（留空/清除）=disableMorph，[保持当前已导入轨道]=两者都不传。audio 同理用 clearAudio；
  camera 编辑清除=clearCamera。timing 每槽 `{ speed>=0, offsetSeconds }`（秒，正数后移）。
- `library.startEdit`：revision 不匹配→`stale_revision`；key 不存在→`invalid_params`。
- 播放中编辑保护（fix-issue-5）：编辑已加载且 Playing/Paused 的曲目时 native 拒绝导入，
  推 `import.progress { active:false, finished:true, succeeded:false,
  errorDetail:"当前曲目正在播放，无法替换其资源文件。请停止播放后重试。你的修改尚未丢失。" }`；
  前端同时在模态内提供“停止播放并保存”（先 player.stop，停止确认后自动重新提交）。
- `import.progress` 事件与 `state.get.importProgress` 形状改为
  `{ active, finished, succeeded, progress(0..1), stage, statusText, errorDetail }`。
  前端全局进度模态：最短显示 550ms；提交后 250ms 内到达的 finished+succeeded 视为
  stale-success 忽略（awaitingImportStart 守卫）；失败切换到独立错误模态显示 errorDetail。

**整窗拖放**

| method | params | result |
|---|---|---|
| `drop.setTarget` | `{ enabled }` | `{}` |
| `drop.poll` | `{}` | `{ files: string[] }`（消费已投放文件，空=无） |
| `drop.peek` | `{}` | `DropPeek { visual: none/armed/hover/invalid, fileCount, accepted, hostElevated, hostBlocked }` |

- 前端在曲库页且无模态/导入/刷新/对话框时注册目标；~200ms 轮询 peek+poll。
- accepted=批次内受支持文件数（.vmd/.gkcamera/.wav/.mp3/.ogg/.flac/.m4a）；
  accepted=0 且有文件→invalid。hostElevated/hostBlocked 为宿主能力位，非拖拽态。

**Ensemble**

| method | params | result |
|---|---|---|
| `ensemble.set` | `{ settings, revision, actorEpoch }` | `{}` |
| `ensemble.presetSave` | `{ name, settings }` | `{}` |
| `ensemble.presetLoad` | `{ name }` | `{ settings }`（actorToken 一律 0，hint 仅展示） |
| `ensemble.probe` | `{ enabled }` | `{}` |

- revision/actorEpoch 不匹配→`stale_revision`/`stale_token`；校验失败→`invalid_params`
  （message 用 ensemble::Validate 的英文原文）。
- 前端草稿规则：snapshot.revision 变化→重置草稿；actorEpoch 变化→清空各槽 actorToken 并标脏。

**骨骼微调 bone.***

| method | params | result |
|---|---|---|
| `bone.get` | `{}` | `BoneState { masterEnabled, overrides: BoneOverrideEntry[], notice }` |
| `bone.set` | `{ humanoidId, boneName, pitch, yaw, roll, spaceWorld, enabled, symmetrical }` | `{}` |
| `bone.reset` | `{ humanoidId, boneName, symmetrical }` | `{}` |
| `bone.resetAll` | `{}` | `{}` |
| `bone.setMaster` | `{ enabled }` | `{}` |
| `bone.reload` | `{}` | `{}` |
| `bone.saveToMotion` / `bone.saveToGlobal` | `{}` | `{}` |

- `BoneOverrideEntry { humanoidId, boneName, enabled, spaceWorld, eulerDeg:{x,y,z}, isDynamic }`。
- 每次变更推 `bone.changed`（BoneState）；保存成功/失败文案走 notice
  （“已成功写入当前曲目 bone_overrides.json”/“保存失败：无法写入当前曲目目录”等）。
- symmetrical=true 时 native 同时写镜像骨（GetSymmetricalHumanoidId 映射）。

**视线 gaze.***

| method | params | result |
|---|---|---|
| `gaze.get` | `{}` | `GazeState { settings, runtimeAvailable, vrifyQueryAvailable, vrifyLiveGazeActive, notice }` |
| `gaze.set` | `{ settings: GazeSettings }` | `{}` |

- `GazeSettings { mode(0 Auto/1 ForceOff/2 DesktopOn), desktopLookAtEnabled, eyeWeight, headWeight, bodyWeight }`
  权重 0..1；持久化到 control.json；变更推 `gaze.changed`。“循环切换”由前端 (mode+1)%3 后 set。

**场景节点 / 舞台替换**

| method | params | result |
|---|---|---|
| `stage.scan` | `{}` | `{ nodes: StageNode[] }` |
| `stage.toggleNode` | `{ index, active }` | `{}` |
| `stage.setLoadReplacement` | `{ key }` | `{ notice }` |

- `StageNode { index, name, displayName, parentIndex, depth, hasChildren, currentlyActive,
  isColliderLocked, isIdentifiedBackground, isLightingRoot, isLightingNode }`。
- toggleNode 后推 `stage.nodesChanged { nodes }`；碰撞锁定节点前端禁用勾选。
- setLoadReplacement 允许在 injectionAllowed=false 时调用（开发者模式）；key 为空=恢复原生。

**实验 / 遥测**

| method | params | result |
|---|---|---|
| `experiments.get` | `{}` | `ExperimentsState { settings, mouthFitStatus, eyeFitStatus, fittedFaceLabel, notice }` |
| `experiments.set` | `{ patch: Partial<ExperimentSettings> }` | `{}` |
| `telemetry.get` | `{}` | `TelemetrySnapshot { statusLine, player{...}, actor{...} }` |

- `ExperimentSettings { mikuMouthFit, mikuEyeFit, mikuFitAllowUnverified, waistSupport, beforeQuartz }`。
- mouthFitStatus/eyeFitStatus 为 native 计算好的显示文案（MikuFitStatusText；
  关闭→“已关闭”，Stopped→“尚未接管”）；变更推 `experiments.changed`。
- telemetry 为按钮触发快照；scrub 探针计数由前端本地统计。

**鞋底测量 sole.* / Pitch Demo solePitch.***

| method | params | result |
|---|---|---|
| `sole.get` | `{}` | `SoleState { settings, snapshot, generation, notice }` |
| `sole.set` | `{ settings }` | `{}` |
| `sole.save` / `sole.load` | `{}` | `{}` |
| `solePitch.get` | `{}` | `SolePitchState { settings, clipTime, feet[2], notice }` |
| `solePitch.set` | `{ settings }` | `{}` |

- SoleSettings/SoleSnapshot 形状见 protocol.ts（与 GkSoleMeasurement.hpp 对齐；
  points[2][3]，patches[2] 各 4 角；距离单位米）。
- generation=soleCalibrationGeneration；变化时前端把草稿 planeCalibrated/shoeCalibrated 复位。
- 前端在对应 tab 可见且测量开启时以 ~500ms 轮询 sole.get / solePitch.get；
  变更 also 推 `sole.changed` / `solePitch.changed`。
- solePitch.settings `{ enabled(=experimentalSolePitchCorrection), maxPitchDegrees(0..35 UI), useMeshEnvelope }`。

**布光与视频 studio.***

| method | params | result |
|---|---|---|
| `studio.get` | `{}` | `StudioState { settings, revision, lightingNotice, videoNotice }` |
| `studio.set` | `{ settings }` | `{}` |
| `studio.reset` | `{}` | `{}` |

- StudioSettings 与 GkStageStudioSettings.hpp 字段一致；native 用 studio::Validate
  校验（invalid_params），持久化并推 `studio.changed`。
- 视频路径必须本地绝对路径；enabled 且 path 空→invalid_params。浏览用 dialog.pickFile kind=5。
- injectionAllowed=false 时仍可编辑/保存预设（应用延后到有效摄影场景）。

**新增事件**：`bone.changed`、`gaze.changed`、`stage.nodesChanged`、
`experiments.changed`、`sole.changed`、`solePitch.changed`、`studio.changed`。

以上第二阶段方法与事件已在 `src/GkWebPanelBridge.hpp` 实现并纳入 `motion.hello.supportedMethods` / `supportedEvents`，前端据此自动启用对应控件。

## 11. 前端行为约定（native 可依赖）

- 前端不会在 `supportedMethods` 之外发请求；native allow-list 仍应独立校验。
- 播放中高频状态只走 `player.stateChanged` 事件，前端不轮询 `state.get`。
- 前端在 `bridge.closed` / sid 升级后进入断连页，不会继续发请求。
- 音量/表情/位置滑条：拖动中节流发送 set，commit 时发送最终值（音量另发 save）。
