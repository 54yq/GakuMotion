# GakuMotion

- Use `build.ps1` for local Runtime, NativeAOT and synthetic-contract builds.
- Ordinary source edits and builds do not require a snapshot approval status.
- Retain dependency licenses and attribution notices. Keep source and parameter provenance accurate.
- Do not deploy, control the game, access real user motions/models/captures, publish a Release or change remote repositories unless the current user request authorizes that action.
- Keep user data and unrelated working changes intact. Use synthetic inputs for isolated tests.
- Record build/test results separately from game loading and visual acceptance.
- Make local Git checkpoints after validating the requested changes.

## 运行时问题排查与实机复测

- 先用现有日志、资源和源码确定异常最早出现在哪一层。写清可区分的根因假设、能证实或排除它的观测，并沿配置或资源、运行时实际读取值、求解和写回、最终显示逐段核对。遇到原生作业时，查实际执行的分支、默认值、下限和回退值。
- 优先复用已有复现资料和正常样本。单次运行按角色与同一源帧对齐输入和输出，跨运行用动作时间对齐。做单变量对照并保留预期行为。记录临时缓解措施与根因修复各自的证据。
- 发布诊断版前先离线验证目标能匹配、字段能读出有效值、单项读取失败能记录原因，并核对成对 DLL 的版本、哈希和桥接导出。需要启动日志确认实际加载时，优先在正常启动或播放中采集。
- 只有现有资料无法区分假设、且预期数据只能从实机取得时，才请求用户复测。合并本轮所需采样，指明一次短复现要回答的问题。未采到数据时先修复并验证采样；再次请求测试前，写明上次缺了什么、这次如何取得。
- 如果连续复测仍未改变判断，或又需要采同类参数，暂停请求复测并重新评估排查方法。主动给出可执行的候选方案和推荐项，说明影响范围、要保留的行为、验证标准与回退方式。根因仍有不确定性时，可以先用可回退的输入隔离或最小改动验证效果。涉及明显功能取舍时，带着方案与用户一起决定。
- 在问题记录中分别标明已证实的原因、未决项、构建、安装、实际加载和视觉验证状态，并写清每项结论适用的场景。

## 分支版本维护

- 在 `src/GakuMotionPlugin.cpp` 的 `GakuPlugin_Query` 中维护插件版本，格式为 `<上游版本>+fork.<分支版本>`。上游版本字段用于标明源码基线，分支版本独立递增。
- 每次确定新的分支版本时，在同一次变更中更新插件描述符、README 的当前版本和 `docs/CHANGELOG.md`。在本地用 `fork-v<分支版本>` 标签标记经过验证的对应提交。
- 用该提交构建运行时 DLL，并核对 `GakuPlugin_Query` 实际返回的版本与 Git 标签一致。分支仓库发布时沿用此标签和版本号。记录构建、部署和实机验证状态。

This repository has no automatic game-deployment step.
