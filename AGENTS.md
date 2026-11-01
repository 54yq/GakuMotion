# GakuMotion

- Use `build.ps1` for local Runtime, NativeAOT and synthetic-contract builds.
- Ordinary source edits and builds do not require a snapshot approval status. When preparing a source snapshot, update `freeze-manifest.json` and optionally run `tools/verify-source.ps1`.
- Retain dependency licenses and attribution notices. Keep source and parameter provenance accurate.
- Do not deploy, control the game, access real user motions/models/captures, publish a Release or change remote repositories unless the current user request authorizes that action.
- Keep user data and unrelated working changes intact. Use synthetic inputs for isolated tests.
- Record build/test results separately from game loading and visual acceptance.
- Make local Git checkpoints after validating the requested changes.

This repository has no automatic game-deployment step.
