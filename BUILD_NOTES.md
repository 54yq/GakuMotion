# 构建与验证

GakuMotion 的完整运行包包含 C++ 插件 `GakuMotion.dll` 和 C# NativeAOT 导入器 `GakuMotion.VmdImporter.Native.dll`，发布时应配套提供。当前仓库缺少部分必需源码文件，暂不能直接构建完整插件；普通使用请按[安装指南](docs/INSTALLATION.md)取得配套 DLL。

项目结构见[架构](docs/ARCHITECTURE.md)。当前构建入口是根目录 `build.ps1`，输出必须位于源码目录之外，不会部署到游戏。

## 环境与源码前提

| 项目 | 要求 |
| --- | --- |
| 系统 | Windows x64、PowerShell 7（`pwsh`） |
| C++ | Visual Studio 的桌面 C++ 工作负载、MSVC x64、Windows SDK |
| 导入器 | .NET 8 SDK；NativeAOT 链接使用 MSVC 和 Windows SDK |
| 界面与动作核心 | 仓库已包含本构建所需的 Dear ImGui、MinHook 与 KineTexture 源文件，无需初始化子模块 |

游戏运行和面板导入无需 Python、.NET SDK 或 .NET 运行时。当前缺少的源码范围和维护记录见 [SOURCE_REVIEW.md](SOURCE_REVIEW.md)；安装工具链本身不会补齐这些文件。

## 当前构建入口的限制

现行 `build.ps1` 在编译前核对清单内每个文件的 SHA256；修改源码后也会被这一检查拦住。它另按缺项名单和完整源码状态检查是否允许产品构建。因此，当前入口还不是可修改源码后直接编译的常规开发流程，后续需要完善；本页列出的是现有接口，不表示完整产品已能构建。

## 检查源码与构建入口

从仓库根目录执行：

```powershell
pwsh -NoProfile -File build.ps1 -Target Preflight
```

当前命令会列出完整构建所需的缺项并退出，不启动编译。源码补齐及上述流程完善后，运行时和导入器分别使用：

```powershell
pwsh -NoProfile -File build.ps1 -Target Runtime -OutputRoot ../GakuMotion-build
pwsh -NoProfile -File build.ps1 -Target NativeAOT -OutputRoot ../GakuMotion-build
```

当前这两条命令同样会在检查处停止。`-Target All` 包含两个目标；`-WindowsSdkRoot` 可指定 Windows SDK 根目录。脚本不支持旧研发入口的 `-Configuration`、`-Preview`、`-SkipDeploy` 等参数。

## 合成契约检查

```powershell
pwsh -NoProfile -File build.ps1 -Target Contracts -OutputRoot ../GakuMotion-build
```

该目标编译并运行合成的舞台加载边界测试，不读取游戏、模型、动作或录制；仍须先通过清单文件检查。它不能替代完整插件与导入器构建。

## 插件接入与问题排查

宿主生命周期、主线程调度及独立插件示例见 [GakuLoader 插件指南](https://github.com/crested-ezios/GakuLoader/blob/main/docs/PLUGIN_DEVELOPMENT.md)，Motion 的 ABI 见 [GakuPlugin.h](include/GakuPlugin.h)。产品导入使用 NativeAOT：旧导入器、缺失 DLL 或 ABI 不匹配会明确报错，不会自动切换到 Python。

| 症状 | 检查方式 |
| --- | --- |
| 找不到 Visual Studio / MSVC | 安装桌面 C++ 工作负载和 x64 工具 |
| 找不到 Windows SDK | 检查 SDK 安装位置，必要时指定 `-WindowsSdkRoot` |
| 修改源码后提示文件哈希不匹配 | 当前构建入口尚有上述限制，不要把它当作编译错误 |
| 提示缺少源码文件 | 查看命令列出的路径与源码维护记录，当前仓库不能生成完整产品 |
| 导入器构建工具缺失 | 检查 .NET 8 SDK、MSVC 和 Windows SDK |
| DLL 加载或导入失败 | 按[故障排查](docs/TROUBLESHOOTING.md)核对配套版本与日志 |

构建成功、文件部署、进程加载与游戏效果分别记录。更换安装 DLL 后需要正常退出并重新启动游戏，不能用离线测试替代实际加载与效果判断。
