# 构建与验证

GakuMotion 由 C++ 插件 `GakuMotion.dll`、C# NativeAOT 导入器 `GakuMotion.VmdImporter.Native.dll` 以及 WebView2 React 前端 `GakuMotion.UI/` 组成。日常使用预编译文件不需要开发工具；安装位置见[安装指南](docs/INSTALLATION.md)。

## 环境与依赖

- Windows x64、PowerShell，以及安装了桌面 C++ 工作负载和 Windows SDK 的 Visual Studio 2022 或更新版本。
- NativeAOT 导入器构建需要 .NET 8 SDK、MSVC 和 Windows SDK；首次恢复依赖可能需要访问配置的 NuGet 源。运行预编译导入器不需要安装 .NET 运行时。
- WebView2 前端（`frontend/`）构建与预览需要 Node.js（CI 使用 Node 24）与 npm；依赖锁文件为 `frontend/package-lock.json`。
- 构建前确认仓库所需的 Dear ImGui、MinHook、KineTexture 与 Candidate B 来源文件均已就位。
- 仓库根目录的 `global.json` 选择稳定的 .NET 8.0.427 feature band，允许该 band 中的更新补丁。

## 构建

在 GakuMotion 仓库根目录执行相应命令：

| 目标 | 命令 | 输出 |
| --- | --- | --- |
| 全量构建（插件 + 导入器 + 前端） | `powershell -NoProfile -File build.ps1 -Target All` | `build/native/GakuMotion.dll`、`build/native/GakuMotion.UI/`、`build/nativeaot/GakuMotion.VmdImporter.Native.dll` |
| 仅 C++ 运行时插件 | `powershell -NoProfile -File build.ps1 -Target Runtime` | `build/native/GakuMotion.dll` |
| 仅 NativeAOT 导入器 | `powershell -NoProfile -File build.ps1 -Target NativeAOT` | `build/nativeaot/GakuMotion.VmdImporter.Native.dll` |
| 仅 React 前端与资产暂存 | `powershell -NoProfile -File build.ps1 -Target Frontend` | `frontend/dist/` 与 `build/native/GakuMotion.UI/` |
| 前端离线生产构建脚本 | `powershell -NoProfile -File frontend\build.ps1 -Dependencies Ci` | `frontend/dist/`（含相对路径与无 source-map 校验） |
| 离线发布暂存打包 | `powershell -NoProfile -File tools\package-release.ps1` | `build/release/GakuMotion-WebView2-.../`（含插件 DLL、`GakuMotion.UI/` 与许可清单） |

`build.ps1` 还支持 `-Target Preflight`、`-Target Contracts`、`-LegacyImGui`（显式启用已退役的旧 ImGui 回退界面）、`-FrontendDependencies Auto|Ci|Install|Skip`、`-OutputRoot` 与 `-DotNet`。不会自动部署到游戏目录。

将配套的 `GakuMotion.dll`、`GakuMotion.VmdImporter.Native.dll` 与 `GakuMotion.UI/` 目录一同放入 `GakuLoader/plugins/`，并正常退出、重新启动游戏才能加载新版。

## 独立前端预览与 WebStorm 配置

可在不启动游戏和 `GakuLoader` 的情况下在浏览器中预览 GakuMotion React 界面（由 `frontend/src/motion/mock.ts` 驱动）：

```powershell
Push-Location frontend
npm ci --ignore-scripts
npm run dev:preview          # 启动 Vite 并打开 /?preview=1
npm run preview:build        # 构建独立预览产物到 frontend/dist-preview/（不污染生产 dist/）
npm run preview:serve        # 本地托管 dist-preview/ 并打开 /?preview=1
Pop-Location
```

预览模式工具条支持切换 GakuLoader 的全部主题（`Dark`、`Sapphire`、`Paper & Ink`、`Apple Light`、`Apple Dark`）、UI 缩放（`75%`–`150%`）、模拟控制权限开关与重置会话；生产构建（`dist/`）在 WebView2 之外不启用 Mock。

WebStorm / JetBrains IDE 原生运行配置已存放在 `.idea/runConfigurations/`（打开仓库根目录时使用）及 `frontend/.idea/runConfigurations/`（直接打开 `frontend/` 时使用）。

## 离线验证

| 检查项 | 命令 | 检查范围 |
| --- | --- | --- |
| 原生核心与语法 | `powershell -NoProfile -File tests\run_release_tests.ps1` | 合成核心、舞台加载边界与 ImGui 骨架语法 |
| Web Panel 协议契约 | `powershell -NoProfile -File tests\run_web_panel_tests.ps1` | `gakumotion.web-panel/v1` 请求/响应/事件/资产解析 |
| 前端检查全集 | `powershell -NoProfile -File tests\run_frontend_tests.ps1` | TypeScript 类型检查、Vitest 单元测试、`preview:build` 与生产离线资产门禁 |
| 舞台加载契约 | `powershell -NoProfile -File build.ps1 -Target Contracts` | 舞台加载 detour 转发与元数据 |

此外仓库还提供 `tests/run_layout_tests.ps1`、`tests/test_layout_migration.ps1` 与 `tests/run_unicode_path_tests.ps1`。

## 常见问题

| 现象 | 检查 |
| --- | --- |
| 找不到 Visual Studio、MSVC 或 SDK | 检查桌面 C++ 工作负载、x64 工具链与 Windows SDK |
| 找不到依赖源码 | 按仓库子模块与 `deps/` 目录补全，不以空文件替代 |
| .NET SDK 不匹配或 NuGet 恢复失败 | 核对 SDK 版本、`global.json` 与恢复日志 |
| 找不到 `npm.cmd` 或前端依赖失败 | 安装 Node.js 24+，在 `frontend/` 下运行 `npm ci --ignore-scripts` |
| 导入器加载或 ABI 失败 | 检查 DLL 与 `GakuMotion.UI/` 是否来自同一配套构建，重启游戏并查看[故障排查](docs/TROUBLESHOOTING.md) |

架构与格式分别见[架构说明](docs/ARCHITECTURE.md)、[Web Panel 桥接说明](docs/WEB_PANEL_BRIDGE.md)和[动作与镜头格式](docs/formats/ACTION_FORMATS.md)。
