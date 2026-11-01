# GakuMotion

在《学园偶像大师》PC DMM 版中回放 MMD 动作、表情、运镜与音频。

GakuMotion 通过 [GakuLoader](https://github.com/crested-ezios/GakuLoader) 加载，在偶像摄影及符合准入条件的更衣室中驱动角色，不改写游戏可执行文件或 AssetBundle。VMD 导入由配套 NativeAOT 导入器完成，日常导入和播放无需安装 Python 或 .NET 运行时。

[安装](docs/INSTALLATION.md) · [使用指南](docs/USAGE.md) · [常见问题](docs/TROUBLESHOOTING.md) · [源码构建](BUILD_NOTES.md) · [全部文档](docs/README.md)

## 功能概览

- **动作与表情**：导入 VMD 身体动作、表情和眼动，支持编辑已有曲目的资源。
- **曲库与播放**：浏览、搜索和刷新曲库；加载静止预览或播放；按时间、采样帧定位及逐帧查看。
- **镜头与音频**：导入 VMD 运镜，选择固定、跟随或注视模式，配合 PCM WAV 同步播放。
- **角色与场景**：调整角色初始位置、骨骼覆盖与表情适配，设置背景和舞台，选择次级物理后端。

表情、眼动和服装物理的表现受角色、源动作与适配数据影响；实验功能在面板中单独标识。

## 开始使用

### 安装插件

准备 GakuLoader，以及同一配套版本的 `GakuMotion.dll` 和 `GakuMotion.VmdImporter.Native.dll`。退出游戏后，将两个 DLL 放入 `gakumas-local/plugins/`，再启动游戏。

安装需要维护者提供的同版本配套 DLL。当前仓库尚无可直接下载的配套安装包，源码也暂不能构建完整插件。完整目录、配置与更新方法见 [安装指南](docs/INSTALLATION.md)；源码开发见 [构建指南](BUILD_NOTES.md)。

### 导入第一支动作

进入偶像摄影，按 **Ctrl+M** 唤出控制面板，在“曲库”中导入 VMD，可同时添加表情、镜头和音频。选择曲目后点击“加载预览”查看首帧，或点击“播放选中”开始表演。

**F9** 播放或暂停，**F10** 停止并释放接管。导入、曲目管理和镜头设置见 [使用指南](docs/USAGE.md)。

### 遇到问题

从 [常见问题](docs/TROUBLESHOOTING.md) 检查页面准入、配套 DLL、曲库文件和日志。开发者可继续查看 [架构](docs/ARCHITECTURE.md) 与 [文件格式](docs/formats/ACTION_FORMATS.md)。

## 使用范围

- 控制面板与接管限定在确认过的摄影和更衣室页面；主页、Live、培育及未知页面不开放。GM 接管期间，已识别的官方手动拍照入口会被阻断；自动与公共 capture 保持原始行为。规则见 [页面准入与拍照边界](docs/USAGE.md#打开面板)。
- 目标腿部几何重烘焙尚未移植到原生导入器；请求该操作会报错并保留已有资源。
- VR 模式下的 Motion 运镜仍有已知兼容问题，见 [VRify 共存记录](docs/USAGE.md#已知兼容性)。

## 开发接入

运行时与导入器的分工见 [架构](docs/ARCHITECTURE.md)，动作与镜头的读写契约见 [文件格式](docs/formats/ACTION_FORMATS.md)。宿主插件开发从 [GakuLoader 插件指南](https://github.com/crested-ezios/GakuLoader/blob/main/docs/PLUGIN_DEVELOPMENT.md) 开始，Motion 使用的 ABI 见 [GakuPlugin.h](include/GakuPlugin.h)。

## 相关项目

[GakuLoader](https://github.com/crested-ezios/GakuLoader) 提供插件宿主、主线程调度和控制窗口；[KineTexture](https://github.com/crested-ezios/KineTexture) 提供共享动作计算能力；[GakuBurstBridge](https://github.com/crested-ezios/GakuBurstBridge) 是实验性原生物理的可选伴生组件，基础播放不依赖它。GakuMotion 使用的 Dear ImGui、MinHook 等组件及其许可见 [第三方通知](THIRD_PARTY_NOTICES.md)。

## 许可

GakuMotion 源码采用 [MIT License](LICENSE)。第三方组件遵循各自许可；游戏名称、角色与资产权利归原权利人所有。
