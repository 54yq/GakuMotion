# GakuMotion

- Use `build.ps1` for local Runtime, NativeAOT and synthetic-contract builds.
- Ordinary source edits and builds do not require a snapshot approval status.
- Retain dependency licenses and attribution notices. Keep source and parameter provenance accurate.
- Do not deploy, control the game, access real user motions/models/captures, publish a Release or change remote repositories unless the current user request authorizes that action.
- Keep user data and unrelated working changes intact. Use synthetic inputs for isolated tests.
- Record build/test results separately from game loading and visual acceptance.
- Make local Git checkpoints after validating the requested changes.
- Before uploading or publishing feature iterations of this fork, review and update `README.md` to reflect the current functionality. Distinguish inherited upstream capabilities, fork additions, upstream contributions integrated into the fork, and experimental limitations; retain branding and attribution, and link to upstream installation and general usage guides.

- When describing upstream capabilities, explicitly credit features or fixes contributed by fork maintainer 54yq through upstream PRs, including the PR link and verified target branch/status. Keep merged contributions distinct from pending or closed-unmerged PRs, and refresh the dated upstream-contribution list when updating the README.

This repository has no automatic game-deployment step.
