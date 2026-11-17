# GakuMotion web-panel native service (`gakumotion.web-panel/v1`)

Provider-owned optional service for the GakuLoader WebView2 plugin content panel.
In default `Release` builds the React WebView2 panel is the sole primary UI;
legacy ImGui (`RenderGmlMotion` under `GKMOTION_ENABLE_IMGUI` / `-LegacyImGui`)
is retired from default builds and kept only as an explicit compile-time fallback.

## Layout

- `include/GakuWebPanel.h` — ABI contract shared with GakuLoader (`GAKU_WEB_PANEL_VERSION = 1`, ABI frozen).
- `frontend/` — GakuMotion React + Vite + Tailwind + Vitest frontend (`src/motion/`),
  independent preview mode (`?preview=1` / `dist-preview/`), offline asset build (`frontend/build.ps1`),
  and visual check (`frontend/scripts/visual-check.mjs`).
- `src/GkWebPanelBridge.hpp` — wire protocol, method allow-list, revision/token
  guards, event sequencing, async polling. Windows-free and headless-testable.
- `src/GakuMotionPlugin.cpp` — service publication (`PublishWebPanelApi`),
  host-service hooks (plugin-directory `GakuMotion.UI`, ShellExecute folder opener, host
  file-dialog/file-drop/file-drag/appearance), lifecycle wiring.
- `tests/test_web_panel_bridge.cpp` + `tests/run_web_panel_tests.ps1` —
  synthetic headless contract tests (no game, no real user assets).

## Wire contract

Authoritative frontend definitions: [frontend/src/motion/protocol.ts](../frontend/src/motion/protocol.ts)
and [frontend/docs/motion-bridge-protocol.md](../frontend/docs/motion-bridge-protocol.md).

- request `{v:1,id,type:"request",method,params}`
- response `{v:1,type:"response",id,ok,result|error:{code,message,details?}}`
- event `{v:1,type:"event",name,payload,seq,sid}` (full envelope emitted by
  Motion; the Loader passes sink JSON through unchanged)
- `handleMessage` buffer rule: on short/NULL buffer returns `GAKU_E_SIZE` and
  sets `requiredBytes` = UTF-8 bytes + 1 (NUL included), output untouched;
  success also reports `requiredBytes` including the NUL.
- `getAssets`: resolves solely from GakuMotion's own plugin directory
  (`<plugin DLL dir>\GakuMotion.UI`, e.g. `GakuLoader\plugins\GakuMotion.UI`).
  Never reads `<gameRoot>\GakuMotion.UI`. If `index.html` is absent in the plugin
  directory, returns `GAKU_E_UNAVAILABLE`. Entry `index.html`, virtual host
  `gakumotion.invalid`, absolute `assetsDir`.

## Threading / lifecycle

- All service callbacks run on the Loader desktop UI thread; `onUiTick` shares
  the `onDesktopUi` callback gate, so host file-drop polling is legal there.
- Core reads/writes use `Core::stateMutex`; mutations are queued through
  `Core::QueueGui` (`GuiAction` + `PositionCommand` generation/session guards,
  `libraryRevision`, actor `token+epoch`) and drain on the Unity main thread,
  exactly like the ImGui overlay. Long work (refresh/import/delete) is queued.
- The event sink is only invoked from UI-thread callbacks. `MotionShutdown`
  (Unity thread) silently invalidates the bridge and releases the dialog
  ticket; `bridge.closed` is emitted from the UI-thread `onShutdown`. If the
  Loader retires the owner first, it destroys the WebView and the frontend
  detects the disconnect via sid/destroy.
- Each activation creates a fresh bridge (new `sessionId`); retired instances
  stay resident because copied service tables may still reference them.

## Methods

Advertised in `motion.hello.supportedMethods` (`kSupportedMethods` in
`src/GkWebPanelBridge.hpp`): `bridge.hello`, `bridge.ping`, `motion.hello`,
`state.get`, `player.play/pause/resume/stop/cycle/seek/seekFrame/stepFrame/
scrubBegin/scrubUpdate/scrubEnd/setSpeed/setVolume/saveVolume/reload`,
`playlist.setMode`, `library.refresh/select/delete/openFolder/import/probeFiles/
startEdit`, `actors.selectTarget`, `facial.setWeights/save`, `position.set/save`,
`camera.set/save`, `stage.set/scan/toggleNode/setLoadReplacement`,
`settings.setPhysics`, `shell.setTextInputActive`, `appearance.get`,
`dialog.pickFile/poll`, `drop.setTarget/poll/peek`, `ensemble.set/presetSave/
presetLoad/probe`, `bone.get/set/reset/resetAll/setMaster/reload/saveToMotion/
saveToGlobal`, `gaze.get/set/cycle`, `sole.get/set/save/load`,
`solePitch.get/set`, `studio.get/set/reset`, `experiments.get/set`,
`telemetry.get`.

`Dispatch` also accepts compatibility aliases (`ensemble.preset`,
`ensemble.preset.save/load`, `bone.tree`, `bone.toggle`,
`gaze.setMode/setDesktop/setWeights`, `sole.read/apply`,
`studio.setLighting/setVideo`, `experiments.setMiku/setWaistSupport/setBeforeQuartz`).
Host-dependent methods (`library.openFolder`, `dialog.pickFile`, `drop.*`) return
structured `not_implemented` when the corresponding host service hook is absent;
anything outside `Dispatch` returns `unknown_method`.

Events (`kSupportedEvents`): `player.stateChanged` (~10 Hz while playing,
immediate after commands), `permission.changed`, `library.changed`,
`loaded.changed`, `playlist.changed`, `actors.changed`, `camera.changed`,
`stage.changed`, `stage.nodesChanged`, `facial.changed`, `position.changed`,
`ensemble.changed`, `settings.changed`, `import.progress`,
`appearance.changed`, `bridge.closed`, `gaze.changed`, `bone.changed`,
`sole.changed`, `solePitch.changed`, `studio.changed`, `experiments.changed`.
Permission loss / injection-session change finishes an in-flight native scrub
through the queue (the frontend drops its scrub without sending `scrubEnd`).

## Known boundaries

- `stage.set` keeps the ImGui cross-domain side effects (lighting →
  `camera.actorShadowExperimentMode`, `hideBackground` → green screen /
  camera clear / skybox restore). `stage.json` persistence follows existing
  Core semantics: only `loadReplacementKey` is persisted, by the dedicated
  `stage.setLoadReplacement` control; visual stage settings are not persisted
  by Core today.
- File dialogs, drag/drop, hotkeys and ShellExecute stay on host services /
  Win32; no Web API substitutes. WebView-child-HWND OLE drop swallowing is a
  live-acceptance risk flagged by the Loader host.
- `facial.setWeights` and `gaze.*` write directly under `core_.stateMutex`
  (matching the ImGui overlay; `gaze.*` requires `GazeRuntimeAvailable()`,
  returning `not_ready` otherwise).

## Verification

- `tests/run_web_panel_tests.ps1` — 23 groups / 304 assertions (synthetic).
- `tests/run_frontend_tests.ps1` — TypeScript typecheck, Vitest unit tests,
  `preview:build`, and `frontend/build.ps1` offline asset verification.
- `build.ps1 -Target Runtime` — native `GakuMotion.dll` (default `Release`
  without ImGui; pass `-LegacyImGui` for `Release ImGui`).
- `build.ps1 -Target Frontend` — builds `frontend/dist/` and stages
  `build/native/GakuMotion.UI/` (`build.ps1 -Target All` builds native,
  NativeAOT importer, and frontend together).
- `build.ps1 -Target Contracts` — existing stage-load contracts (regression).

Game loading, WebView rendering and drag/drop acceptance are manual steps and
are not covered by these offline checks.
