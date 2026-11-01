# 构建与验证

GakuMotion 的完整运行包包含 C++ 插件 `GakuMotion.dll` 和 C# NativeAOT 导入器 `GakuMotion.VmdImporter.Native.dll`。两个文件应来自同一源码版本并配套提供。普通使用不需要开发工具，见[安装指南](docs/INSTALLATION.md)。

项目结构见[架构](docs/ARCHITECTURE.md)。构建入口是根目录 `build.ps1`；默认输出到已被 Git 忽略的 `build/`，也可指定仓库外目录。脚本不会部署到游戏。

## 环境与依赖

| 项目 | 要求 |
| --- | --- |
| 系统 | Windows x64、PowerShell 5.1 或以上；以下示例使用 `pwsh` |
| C++ | Visual Studio 桌面 C++ 工作负载、MSVC x64、Windows SDK |
| 导入器 | .NET 8 SDK；根 `global.json` 固定从稳定 8.0.405 feature band 选择，不使用预览 SDK |
| 界面与动作核心 | 所需 Dear ImGui、MinHook 和 KineTexture 源文件已包含，无需初始化子模块 |

运行游戏插件和界面导入不需要 Python、.NET SDK 或 .NET 运行时。NativeAOT 发布需要 .NET SDK 能恢复相应包，以及 MSVC 和 Windows SDK；第一次恢复可能访问正常配置的 NuGet 源。

## 构建

从仓库根目录执行：

```powershell
pwsh -NoProfile -File build.ps1 -Target All
```

成功后得到：

```text
build/native/GakuMotion.dll
build/nativeaot/GakuMotion.VmdImporter.Native.dll
```

分别构建或选择输出位置：

```powershell
pwsh -NoProfile -File build.ps1 -Target Runtime -OutputRoot ../GakuMotion-build
pwsh -NoProfile -File build.ps1 -Target NativeAOT -OutputRoot ../GakuMotion-build
```

`-Target Preflight` 只检查真实源文件依赖、MSVC 和 Windows SDK，不编译。`-WindowsSdkRoot` 可指定 SDK 根目录。NativeAOT 发布会切换到仓库根目录选择 SDK，因此从其他工作目录使用 `-File` 调用时也采用根 `global.json`。

## 合成契约检查

```powershell
pwsh -NoProfile -File build.ps1 -Target Contracts
```

该目标编译并运行合成的舞台加载边界测试，不读取游戏、模型、动作或录制。它不代替完整插件构建、真实导入链检查或游戏效果验证。

## 可选的源码快照校验

维护者准备固定源码快照时可执行：

```powershell
pwsh -NoProfile -File tools/verify-source.ps1
```

该工具比对 `freeze-manifest.json` 中的文件大小和 SHA256。普通构建不调用它、不读取审核状态；开发者修改源码后可以直接编译。准备新的固定快照时，再更新清单中的文件身份。

## 插件接入与问题排查

宿主生命周期、主线程调度及独立插件示例见 [GakuLoader 插件指南](https://github.com/crested-ezios/GakuLoader/blob/main/docs/PLUGIN_DEVELOPMENT.md)，Motion 的 ABI 见 [GakuPlugin.h](include/GakuPlugin.h)。产品导入使用 NativeAOT，不回退到 Python。

| 症状 | 检查方式 |
| --- | --- |
| 找不到 Visual Studio / MSVC | 安装桌面 C++ 工作负载和 x64 工具 |
| 找不到 Windows SDK | 核对 SDK 安装位置，必要时指定 `-WindowsSdkRoot` |
| 缺少源文件 | 重新取得完整源码；查看具体报错路径，不用空文件替代 |
| .NET SDK 不匹配 | 按根 `global.json` 安装稳定 .NET 8 SDK，检查 `dotnet --version` |
| NuGet 恢复失败 | 检查恢复日志和已配置包源，不把恢复失败当作 C# 编译错误 |
| DLL 加载或导入失败 | 按[故障排查](docs/TROUBLESHOOTING.md)核对配套 DLL、ABI 和日志 |

构建成功、文件部署、进程加载与游戏效果分别记录。更换安装 DLL 后需正常退出并重新启动游戏。
