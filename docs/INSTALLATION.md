# 安装与更新

GakuMotion 在 Windows x64 的 PC DMM 版中由 GakuLoader 加载。

## 安装布局

```text
<游戏根目录>/
  gakumas.exe
  version.dll
  GakuLoader/
    plugins/
      GakuMotion.dll
      GakuMotion.VmdImporter.Native.dll
      GakuBurstBridge.dll               # 可选伴生库
    Configs/gakumotion/
      control.json
      bone_overrides.json
      initial_position.json
      camera.json
      sole_measurement.json
      stage.json
    PluginData/gakumotion/
      motions/<曲目目录>/
      playlist.json
    Logs/gakumotion/
    Temp/gakumotion/transactions/
    tools/
```

配置按需生成；空曲库会在首次初始化时创建，不需要先添加曲目才能刷新。曲目目录可包含身体 `motion.gkclip`、表情、运镜、音频、元数据和用户附属文件；移动或备份时保留整个曲目目录。用户动作文件归 `PluginData/gakumotion/motions/`。

两个 Motion DLL 应来自同一配套包。

## 首次使用

1. 正常退出游戏后将配套包解压到游戏根目录。
2. 启动游戏，进入偶像摄影，按 `Ctrl+M` 打开 Motion 面板。
3. 添加或拖入 VMD，可同时附加表情、镜头和 PCM WAV。
4. 选择曲目加载预览或播放；`F9` 暂停/继续，`F10` 停止并释放接管。

## 更新与恢复

更新前退出游戏，备份当前程序、配置和完整曲库，再同时替换配套 DLL。

[使用指南](USAGE.md) · [排错指南](TROUBLESHOOTING.md)
