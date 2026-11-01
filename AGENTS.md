# GakuMotion frozen source staging

Work locally in this independent repository. Keep the selected upstream source
bytes and their provenance records together. Changes to a frozen implementation
require a new explicit review and manifest update.

- Build and test only through the local build entry, using an external output directory.
- Do not deploy, start or control the game, access real motions/models/captures,
  upload files, push, or change remote repositories from this task.
- Required data-bearing inputs excluded by the source review must remain listed
  in `freeze-manifest.json`. Do not create substitutes, empty resources or stubs
  to bypass the incomplete-source preflight.
- Retain every applicable third-party license and attribution notice.
- Record actual build/test results separately from source review and game acceptance.
- Make local Git checkpoints after validated changes. Commit only this repository's files.

This repository deliberately has its own instructions; it does not inherit the
research repository's automatic game-deployment workflow.
