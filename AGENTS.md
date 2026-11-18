# GakuMotion

- Use `build.ps1` for local Runtime, NativeAOT and synthetic-contract builds.
- Ordinary source edits and builds do not require a snapshot approval status.
- Retain dependency licenses and attribution notices. Keep source and parameter provenance accurate.
- Do not deploy, control the game, access real user motions/models/captures, publish a Release or change remote repositories unless the current user request authorizes that action.
- Keep user data and unrelated working changes intact. Use synthetic inputs for isolated tests.
- Record build/test results separately from game loading and visual acceptance.
- Make local Git checkpoints after validating the requested changes.

## 分支版本维护

- 在 `src/GakuMotionPlugin.cpp` 的 `GakuPlugin_Query` 中维护插件版本，格式为 `<上游版本>+fork.<分支版本>`。上游版本字段用于标明源码基线，分支版本独立递增。
- 每次确定新的分支版本时，在同一次变更中更新插件描述符、README 的当前版本和 `docs/CHANGELOG.md`。在本地用 `fork-v<分支版本>` 标签标记经过验证的对应提交。
- 用该提交构建运行时 DLL，并核对 `GakuPlugin_Query` 实际返回的版本与 Git 标签一致。分支仓库发布时沿用此标签和版本号。记录构建、部署和实机验证状态。

This repository has no automatic game-deployment step.
