# 摄影准备阶段的曲库导入修复

摄影准备页面允许打开添加窗口，但旧实现只在角色回调中消费导入命令。此时尚未创建角色，界面已显示“导入曲目中”，后台转换实际没有启动。修复把纯文件任务的启动和轮询接到宿主帧，使该页面能够完成导入。

固定来源为研发仓 `0870da032cdb25d02f2d3c7803fce4afc618c743`，staging 适配基线为 `e41c502cbcb078a055f5c0ff7bf8ddcc2fe6fba5`。

| 文件 | 选取与适配 |
| --- | --- |
| `src/GakuMotion.hpp` | 只加入来源提交的 `Core::ProcessPendingLibraryCommands`；保持 staging 原有 GUI 命令消费者 |
| `src/GakuMotionPlugin.cpp` | 只加入 `MotionTick` 的调用与对应说明 |
| `tests/test_actorless_library.hpp` | 采用来源合成测试，并增加自有两帧动作写入器，替代 `LibraryRefreshFixture` 依赖 |
| `tests/test_refresh_core.cpp` | 在 staging 合成核心入口加入新测试头和调用；不带入研发仓完整测试入口 |

新消费者只处理队首连续的导入、镜头导入、刷新命令，并持续轮询已有文件任务。先前的 Stop 等角色控制是顺序屏障，后面的文件任务不能抢先执行。Remove、Delete、Select 等可能触发播放器、相机或角色清理的命令继续走原路径。

测试覆盖无角色时的真实 GUI 回调入队、启动一次、进度更新、成功、启动失败、运行失败、镜头导入、曲库刷新，以及保留播放/定位命令和先前 Stop 屏障。全部输入为合成数据。

本轮检查使用现有 `build.ps1 -Target Runtime`、`tests/run_release_tests.ps1` 和 `tests/run_gui_ux_tests.ps1`，Runtime 使用不含空格的独立 `OutputRoot`。合成核心 861 项、舞台合同 80 项、GUI 149 项通过，ImGui 语法检查通过。本地日志与产物回执汇总在忽略目录 `build/staging-import-fix-20261105/`；它们不成为源码构建条件。

Runtime 构建结果、可选源码冻结核验和实际游戏加载分别记录。本轮未重建未变更的 NativeAOT 导入器，未部署或控制游戏，也未访问用户素材；不把合成测试当作 staging 实机验收。依赖、参数、Control API、Studio 和眼动研究未纳入本次变更。
