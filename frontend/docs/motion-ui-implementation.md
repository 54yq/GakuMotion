# GakuMotion React 前端实现清单

生产入口：`frontend/src/App.tsx` → `frontend/src/motion/components/MotionApp.tsx`
（在 GakuMotion 仓库独立构建、测试与打包，由 GakuMotion 原生插件提供给 GakuLoader 插件内容区）。

## 结构

| 路径 | 内容 |
|---|---|
| `src/bridge/core.ts` | 通用 WebView2 传输（v/id/type/method/params、ok/result/error、事件 name/seq/sid、超时/AbortSignal/sid 失效） |
| `src/motion/protocol.ts` | Motion 领域协议类型 + 常量/默认值（与 native 对齐的权威前端定义） |
| `src/motion/bridge.ts` | MotionWebView2Client + getMotionBridge/setMotionBridge；生产构建在 WebView2 外无桥时显示不可用，不出假数据 |
| `src/motion/mock.ts` | **仅 dev / 预览** 的有状态 mock（Vite dev / preview / vitest），UI 顶部有醒目 Mock 横幅；capability 列表仅包含 mock 已实现方法 |
| `src/motion/store.ts` | MotionStore：快照/事件订阅、capability 门控、scrub 三段式、camera 草稿-expected-pending、libraryRevision/actorEpoch 守卫、音量/表情节流+commit |
| `src/motion/time.ts` | GkTransportTime.hpp 纯函数移植（0-based 帧、float 噪声吸附、mm:ss.mmm 解析/格式化） |
| `src/motion/cameraMode.ts` | GuiCameraMode.hpp 移植 + legacy transform 升级 |
| `src/motion/components/` | MotionApp / MotionSidebar / MotionStatusBar / PermissionGate / TransportBar / NumberField / Unsupported / EnsembleSection / BoneSection / DropOverlay / ImportProgressModal / MotionErrorBoundary |
| `src/motion/pages/` | LibraryPage（+ImportDialog 导入/编辑弹窗）/ MotionPage（含 Ensemble 与骨骼折叠区）/ CameraPage / StagePage（含自定义场景节点树）/ SettingsPage / DeveloperPage（动作实验与视线、遥测、鞋底测量 v2、Pitch Demo、摄影舞台替换、布光与视频 6 子页） |
| `docs/motion-bridge-protocol.md` | 与 GakuMotion native web-panel 服务的完整协议契约 |

## 已实现（语义对齐 ImGui）

- Shell：Sidebar（曲库/动作/镜头/舞台/设置 + 开发者模式追加 [开发]）、顶部状态条
  （驱动/绑定/曲目/播放态/允许控制）、权限降级页（禁用说明 + 开发者选项，
  权限丢失丢弃 scrub）、底部常驻 TransportBar、Mock 横幅。
- TransportBar：scrub 三段式（begin/update/end，拖动冻结显示值，Radix 键盘
  commit 时序已处理）；播放/暂停/继续/停止/上下曲（ensemble 激活禁用上下曲）；
  播放模式（普通 4 态 / ensemble 2 态）；倍速 9 档；音量拖动节流、松开 commit
  才 saveVolume；镜头快捷模式（pending→ack）；上一帧/下一帧；时间/动作帧输入
  定位（Enter 提交/Esc 取消，0-based，末帧 total-1，超界报错文案与 ImGui 一致）；
  告警行（帧位置/时长不一致/源文件变更/音量不支持/导航错误/命令错误）。
- Library：表格（状态[播][暂][预][载]、内容标签、时长）、搜索/清空、刷新、
  打开文件夹、重新加载当前曲目、单击选中/双击播放、加载预览/播放选中、
  删除确认模态（libraryRevision 守卫：revision/key/directory 变化即禁用并提示）、
  大列表窗口化渲染（ListClipper 等价）。
- Motion：驱动角色（token+epoch）、表情幅度 4 滑条（0–2，节流+commit）、
  重置/保存、初始位置 XYZ（±10m，即时生效/重置/保存默认）。
- Camera：模式/状态行/VMD 运镜范围 0.3–1.0/分模式 Transform（legacy 自动升级）/
  跟随部位/速率/位移比例/注视偏移/FOV/场景范围限制/保存 camera.json；
  草稿-expected-pending 确认（深度相等 ack）。
- Stage：光影开关（native 保留 camera.actorShadowExperimentMode 联动）、
  场景背景开关（hideBackground/greenScreen/overrideCameraClear）、背景颜色 +
  5 预置、自定义节点开关。
- Settings：物理后端 0/1/2 + 提示文案、静态快捷键表（7 行）、开发者模式（会话态）。
- 热键抑制：editable focus 经 shell.setTextInputActive 上报（capability 门控）。
- 主题/缩放：appearance.get/changed 可选转发，缺省 dark。

## 边界与验证说明

- `frontend/` 已完整迁入 `GakuMotion` 仓库，不再包含 `GakuLoader` Manager 原型或其测试。
- Phase 2 能力（导入模态、拖放状态、Ensemble、骨骼微调、场景节点、Developer 六子页）已按 `motion.hello.supportedMethods` 能力协商实现，详见 `frontend/src/test/motion-phase2-contracts.test.tsx` 与 [docs/motion-bridge-protocol.md](motion-bridge-protocol.md)。
- 本地前端验证入口：`tests/run_frontend_tests.ps1`（`tsc -b --noEmit`、`vitest run`、`preview:build`、`frontend/build.ps1` 离线资产门禁）；`dist/` 仅包含 `index.html` 与相对路径 js/css，CSP 保留，WebView2 之外不启用 Mock。
- 独立浏览器预览模式（`?preview=1` / `dist-preview/`）默认支持 `GakuLoader` 的全部 5 个主题（`Dark`、`Sapphire`、`Paper & Ink`、`Apple Light`、`Apple Dark`）。
- 实机游戏内验收（`gakumas.exe` 运行时、VMD 导入与舞台画面表现）仍需在游戏环境中人工验证。
