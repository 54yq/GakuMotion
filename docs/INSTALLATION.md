# 安装与更新

GakuMotion 面向《学园偶像大师》PC DMM 版，在 Windows x64 上由 GakuLoader 加载。当前界面标记为 Developer Preview；使用范围见[使用指南](USAGE.md)。

## 准备文件

需要 [GakuLoader](https://github.com/crested-ezios/GakuLoader) 和同一构建配套的两个文件：

- `GakuMotion.dll`：游戏内回放与控制面板。
- `GakuMotion.VmdImporter.Native.dll`：VMD 身体、表情、眼动和运镜导入。

配套导入器是 NativeAOT 动态库。游戏运行和面板导入无需安装 Python、.NET 运行时或 .NET SDK。自行编译时需要的工具链见[构建说明](../BUILD_NOTES.md)。

维护者提供的配套包应附带组件版本与逐文件 SHA256 清单。先核对整个配套包，避免分别取用不同日期的 DLL。普通用户无需构建工具；源码目录中的 `build/` 文件也不自动构成已验证的发行包。

[GakuBurstBridge](https://github.com/crested-ezios/GakuBurstBridge) 仅供实验性原生物理使用。选择安装时，将 `GakuBurstBridge.dll` 放在 `gakumas-local/GakuMotion/`，不要放进 `plugins/`。不安装 Bridge 仍可使用基础动作、导入和内建物理；原生物理不可用时回退至内建方案。

## 安装

1. 正常退出游戏。
2. 按 GakuLoader 的说明安装加载器，使 `version.dll` 位于游戏根目录。
3. 将两个 GakuMotion DLL 放进 `gakumas-local/plugins/`。
4. 启动游戏，进入偶像摄影，按 `Ctrl+M` 唤出 GakuMotion 面板。
5. 在“曲库”添加 VMD，或放入已有动作文件夹后点击“刷新曲库”。

典型目录如下。`GakuMotion/` 保存配置与导入日志，首次运行会生成所需的基础配置；动作文件夹由导入器创建，也可以从已有动作库复制。

```text
<游戏根目录>/
├── version.dll                         GakuLoader
└── gakumas-local/
    ├── gakuloader.log                   宿主与插件日志
    ├── plugins/
    │   ├── GakuMotion.dll
    │   └── GakuMotion.VmdImporter.Native.dll
    ├── GakuMotion/                      配置与导入日志
    │   └── GakuBurstBridge.dll          可选，实验性原生物理伴生库
    └── motions/
        └── MyDance/
            ├── motion.gkclip           身体动作，可含表情和眼动
            ├── camera.gkcamera         运镜，可选
            ├── song.wav                PCM WAV 音频，可选
            └── info.json               曲名等元数据，可选
```

曲库也识别已有的 `motion.gkmotion` 和 `motion.gk_world_dump`。一个曲目对应 `motions/` 下的一个直接子目录。导入时请保留生成的附属文件；迁移动作库时复制整个曲目目录。

## 更新

1. 正常退出游戏，备份现有的两个 DLL。
2. 用同一构建的新版本同时替换两个 DLL。
3. 保留 `GakuMotion/` 配置目录和 `motions/` 动作库，重新启动游戏。

DLL 不支持通过覆盖文件在已运行的游戏中换版；文件已替换与进程已加载新版是两件事。若需要回退，退出游戏后恢复配套的两个旧 DLL。

## 配置与卸载

普通使用优先通过面板调整。配置集中在 `gakumas-local/GakuMotion/`：

| 文件 | 用途 |
| --- | --- |
| `control.json` | 运行时声明式配置与一次性控制命令，支持热读取 |
| `playlist.json` | 曲库顺序与播放模式 |
| `initial_position.json` | 保存的默认位置偏移 |
| `camera.json` | 保存的镜头参数 |
| `bone_overrides.json` | 全局骨骼微调 |
| `sole_measurement.json` | 开发者鞋底测量配置 |

停止使用 GakuMotion 时，退出游戏后移走两个 GakuMotion DLL 即可；动作和配置可以保留。GakuLoader 的卸载方式以其自身说明为准。

下一步：[导入并播放第一支动作](USAGE.md) · [排查安装与导入问题](TROUBLESHOOTING.md) · [文档索引](README.md)
