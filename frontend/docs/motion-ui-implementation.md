# GakuMotion React 前端实现清单

生产入口：`frontend/src/App.tsx` → `frontend/src/motion/components/MotionApp.tsx`
（在 GakuMotion 仓库独立构建、测试与打包，由 GakuMotion 原生插件提供给 GakuLoader WebView2 插件内容区）。

## 目录与模块结构

| 路径 | 内容 |
|---|---|
| `src/bridge/core.ts` | 通用 WebView2 传输（`v/id/type/method/params`、`ok/result/error`、事件 `name/seq/sid`、超时/AbortSignal/`sid` 失效） |
| `src/motion/protocol.ts` | Motion 领域协议类型与常量默认值（与 native `GkWebPanelBridge.hpp` 对齐的权威前端定义） |
| `src/motion/bridge.ts` | `MotionWebView2Client` 与 `getMotionBridge`/`setMotionBridge`；生产构建在 WebView2 外无桥时显示不可用，不出假数据 |
| `src/motion/mock.ts` | **仅 dev / 预览 / 测试** 的有状态 mock（支持 `'full'` 全能力与 `'phase1'` 降级双配置，含导入/拖放/骨骼/多人舞台/场景树/摄影棚模拟） |
| `src/motion/store.ts` | `MotionStore`：快照/事件订阅、capability 门控、scrub 三段式、`seekRelative`、camera 草稿-expected-pending、`libraryRevision`/`actorEpoch` 守卫、单选/范围/批量多选与顺序批量删除、音量/表情节流+commit |
| `src/motion/hooks.ts` | 外观主题与 `uiScale` 同步（`useMotionAppearanceEffect`）、文本焦点上报与失焦重置（`useMotionTextInputBridge`）、面板内无冲突快捷键（`useMotionKeyboardShortcuts`） |
| `src/motion/time.ts` | `GkTransportTime.hpp` 纯函数移植（0-based 帧、float 噪声吸附、`mm:ss.mmm` 解析/格式化） |
| `src/motion/cameraMode.ts` | `GuiCameraMode.hpp` 移植与 legacy transform 自动升级 |
| `src/components/ui/popover.tsx` | 自研轻量锚点定位 `Popover`（跟随 CSS `zoom` 缩放，支持 `pinned` 固定模式，兼容 Radix `Select` Portal 外部点击判定） |
| `src/components/ui/slider.tsx` | 支持横向与 `orientation="vertical"` 竖向拖拽的统一 `Slider` 原语 |
| `src/motion/components/` | `MotionApp`、`MotionSidebar`、`MotionStatusBar`、`PermissionGate`、`TransportBar`（全局三区 Player Bar）、`PrecisionControlPanel`（可固定精细控制面板）、`PropertyGroup`（`PropertySection`/`CollapsiblePropertySection`/`PropertyRow`）、`ContextMenu`、`NumberField`、`Unsupported`、`EnsembleSection`、`BoneSection`、`DropOverlay`、`ImportProgressModal`、`MotionErrorBoundary` |
| `src/motion/pages/` | `LibraryPage`（纯列表曲库与批量操作 + `ImportDialog`）、`MotionPage`、`CameraPage`、`StagePage`、`SettingsPage`、`DeveloperPage` |
| `docs/motion-bridge-protocol.md` | 与 GakuMotion native web-panel 服务（`gakumotion.web-panel/v1`）的完整协议契约 |

## 现代化 UI/UX 与业务实现清单

- **设计规范与主题**：参照 Apple Design 桌面控件体系与网易云音乐桌面列表秩序，保留 GakuLoader 全部 5 套主题（`Dark`、`Sapphire`、`Paper & Ink`、`Apple Light`、`Apple Dark`）与 `uiScale`（`75%–150%`）。采用中性分层表面、1px 细边框、`6px`/`8px` 克制圆角与克制单主色，移除无实际封面的伪渐变占位与重阴影仪表盘卡片；未引入额外第三方运行时 UI 组件库。
- **Shell 与全局布局**：
  - `MotionSidebar`：紧凑单色几何品牌标识 + 分组导航入口（`曲库`、`动作`、`镜头`、`舞台` + 系统区 `设置` 及开发者模式 `[开发]`），带选中细指示条与高对比度文本层级。
  - `MotionStatusBar`：轻量单行状态顶栏，展示驱动/绑定角色、多人舞台标记、当前场景名、曲目、源文件告警与 `[允许控制]` / `[已禁用]` 状态。
  - `PermissionGate`：权限禁用时保留状态栏与折叠式开发者选项（摄影舞台替换、布光与视频），同时丢弃未完成 scrub 并关闭精细控制面板。
- **全局 Player Bar (`TransportBar`)**：
  - 切换 Sidebar 页面时不卸载，播放状态与精细控制面板状态全程保留。
  - **顶部全宽进度线**：横跨 Player Bar 顶边（`2px` 悬停加粗至 `4px`），承担三段式 scrub Seek 交互，带清洁的时间与动作帧提示气泡。
  - **左侧演出信息**：中性曲目状态图标（无虚假渐变封面）、播放状态与标题、等宽 `mm:ss.mmm / mm:ss.mmm` 时间、驱动角色/多人舞台标记。
  - **中间核心播放控制**：以 MV 定位为核心的 **快退 / 播放-暂停 / 快进 / 停止**；上下曲收至精细控制面板。
  - **右侧常用参数**：镜头快捷模式下拉、`播放速度` Popover、**竖向滑动条 `音量控制` Popover**（底部显示百分比与静音按钮）、`精细控制` 浮层入口。
- **精细控制面板 (`PrecisionControlPanel`)**：
  - 支持固定（`Pin`）跨页常驻、外部点击/`Escape`/关闭按钮收起，展开收起不中断正在进行的播放。
  - 完整保留：实时帧位置与 FPS 读数、源时长不一致告警、首帧/上一帧/下一帧/末帧步进、快进快退步长选择（`1s`/`5s`/`10s`/`15s`）、精确时间与 0-based 动作帧定位输入、0.10x–3.00x 连续倍速与 `±0.05x` 微调、次级上下曲与重载、播放模式选择。
- **键盘快捷键与防冲突策略**：
  - 输入框焦点通过 `shell.setTextInputActive` 通知原生端抑制 Win32 热键，并在窗口 `blur` 时兜底复位。
  - 面板内快捷键仅使用不与 `GakuMotionPlugin.cpp` 原生热键冲突的按键（`Space`、`←/→`、`Shift+←/→`、`Ctrl+K` / `/`）；原生热键（`F9`、`F10`、`,`、`.`、`-`、`=` 等）仅在独立 Mock 预览模式下由前端模拟。
- **业务页面架构统一**：
  - `LibraryPage`：纯高密度表格列表布局（`勾选列 / 状态 / 演出曲目 / 包含内容 / 时长`）；支持单选、`Ctrl/Cmd` 多选、`Shift` 范围选、表头全选、列头点击及下拉排序、轨道筛选条、悬停/聚焦内联快捷操作、右键桌面 `ContextMenu`、恒定高度单选/批量操作栏（避免双击抖动）、大列表虚拟窗口化、单曲与顺序批量删除的 `libraryRevision` 守卫弹窗、拖放导入及四轨 `ImportDialog`。
  - `MotionPage`：轻量当前曲目条、分组属性面板（驱动角色与站位偏移、4 路表情幅度分项条）及渐进折叠的多人舞台（`EnsembleSection`）与 51 骨微调器（`BoneSection`）。
  - `CameraPage`：紧凑 4 段式模式切换条、位置与旋转偏移、跟随目标与追随速率、渐进折叠高级设置及 `camera.json` 保存。
  - `StagePage`：当前舞台环境与角色阵容属性行、光影与场景背景开关行、纯色幕布色板预设及节点搜索树（`stage-node-tree`）。
  - `SettingsPage` & `DeveloperPage`：统一使用 `PropertySection`/`CollapsiblePropertySection`/`PropertyRow` 呈现物理后端、快捷键对照、曲库目录、开发者开关及 6 个实验子页。

## 验证与独立预览说明

- 独立浏览器预览模式（`?preview=1` / `dist-preview/`）：默认提供 `'full'` 完整能力 Mock，支持 5 套主题、缩放（`75%–150%`）、能力集切换、模拟文件拖入、模拟禁用控制与重置会话；可通过 `frontend/scripts/visual-check.mjs` 自动捕获并审查多主题及交互态截图。
- 前端全套自动化验证：`powershell -NoProfile -File tests\run_frontend_tests.ps1`（`tsc -b --noEmit`、`vitest run` 8 个文件 67 项测试、`preview:build`、`frontend/build.ps1` 离线资产门禁）。
- 原生桥接协议测试：`powershell -NoProfile -File tests\run_web_panel_tests.ps1`（C++ 合成契约 23 组 304 项断言）。
- 真实 WebView2 宿主联调：`webview2_live_verify` 加载生产 `build/native/GakuMotion.UI`（5 主题与缩放切换、文本焦点热键抑制、精细控制、播放/快进与镜头模式触发共 27 项通过）。
- 实机游戏进程内验收（`gakumas.exe` 运行时、真实游戏 VMD/GKCamera/WAV 播放、Explorer OLE 文件拖入 WebView2 及实际画质帧率同步）仍需在游戏环境中人工确认。
