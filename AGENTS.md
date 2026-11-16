# GakuMotion

- Use `build.ps1` for local Runtime, NativeAOT and synthetic-contract builds.
- Ordinary source edits and builds do not require a snapshot approval status.
- Retain dependency licenses and attribution notices. Keep source and parameter provenance accurate.
- Do not deploy, control the game, access real user motions/models/captures, publish a Release or change remote repositories unless the current user request authorizes that action.
- Keep user data and unrelated working changes intact. Use synthetic inputs for isolated tests.
- Record build/test results separately from game loading and visual acceptance.
- Make local Git checkpoints after validating the requested changes.
- Whenever features change, update `README.md` alongside the implementation and review it before uploading or publishing. Preserve the agreed structure: 基础功能 (including 已合入上游的改进) → 分支扩展 → 功能演示 → 上游 PR 说明 → 文档与反馈 → 相关项目. Integrate changes into the relevant section, consolidate repeated descriptions, and keep each demo video in the demo section. Distinguish upstream capabilities, fork additions, integrated upstream contributions and experimental limitations; retain branding and attribution, and link to upstream installation and general usage guides.

- Refer to fork maintainer 54yq as 野生的空白 in reader-facing text. Describe upstream PR contributions briefly and factually; link the relevant feature descriptions directly to their PRs with verified target branches and statuses. Distinguish merged, pending and closed-unmerged PRs, refresh the dated PR list, and avoid repeated ownership claims or promotional wording.

- Present the demo videos as highlighting major capabilities while also including basic fixes. Keep the README feature demos linked to the relevant capabilities: BV1otWa62EgK for ensemble playback, BV1G6s76KE3C for character import and stage takeover, and BV14Ds76bE4e for takeover with more than three actors. Link character-import functionality to https://github.com/54yq/GakuActors and distinguish its role from Motion playback/takeover.

This repository has no automatic game-deployment step.
