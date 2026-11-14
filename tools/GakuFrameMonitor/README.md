# Live 帧数监测

GakuFrameMonitor 是可选的独立 GakuLoader 插件。它在宿主的插件面板显示 Unity 帧更新频率，不在游戏画面上叠加文字。需要同时安装包含 `GakuMotion_GetFrameContext` 的配套 GakuMotion 版本。

在已确认的 Live 场景中，当两次宿主帧回调间隔至少 50 ms 且达到近期基准两倍时，插件会限频记录卡顿时间、Live 与动作进度、已绑定副角色数量和主角色最近的物理统计。日志中的时间和状态可帮助定位问题；它们不能单独证明卡顿原因。这里测量的是 Unity 更新回调间隔，不是显卡帧时间或显示器实际输出帧率。

## 构建

在 Windows x64 上使用 MSVC C++20 和 Windows SDK：

```powershell
./tools/GakuFrameMonitor/build.ps1 -PortableVisualStudio '<Visual Studio 根目录>' -WindowsSdkRoot '<Windows SDK 根目录>'
```

构建脚本先运行合成检测与插件 ABI 测试，再输出 `tools/GakuFrameMonitor/build/GakuFrameMonitor.dll`。退出游戏后将该文件手动放入 GakuLoader 的 `plugins` 目录。移除该文件可卸载监测器；用户配置与素材不会被修改。

本插件使用仓库内的 `include/GakuPlugin.h`、`include/GakuFrameContext.h` 和 `deps/imgui`；相关许可沿用仓库内的许可证与第三方声明。
