# GakuMotion web-panel native service (`gakumotion.web-panel/v1`)

Provider-owned optional service for the GakuLoader WebView2 manager panel.
The ImGui fallback (`RenderGmlMotion`) is untouched; a registration miss only
logs a warning and never fails activation.

## Layout

- `include/GakuWebPanel.h` — ABI contract, copied verbatim from GakuLoader
  `worktrees/webview2-ui/include/GakuWebPanel.h` (keep in sync; ABI frozen).
- `src/GkWebPanelBridge.hpp` — wire protocol, method allow-list, revision/token
  guards, event sequencing, async polling. Windows-free and headless-testable.
- `src/GakuMotionPlugin.cpp` — service publication (`PublishWebPanelApi`),
  host-service hooks (module assets dir, ShellExecute folder opener, host
  file-dialog/file-drop/file-drag/appearance), lifecycle wiring.
- `tests/test_web_panel_bridge.cpp` + `tests/run_web_panel_tests.ps1` —
  synthetic headless contract tests (no game, no real user assets).

## Wire contract

Authoritative frontend definitions: GakuLoader
`frontend/src/motion/protocol.ts` and `frontend/docs/motion-bridge-protocol.md`.

- request `{v:1,id,type:"request",method,params}`
- response `{v:1,type:"response",id,ok,result|error:{code,message,details?}}`
- event `{v:1,type:"event",name,payload,seq,sid}` (full envelope emitted by
  Motion; the Loader passes sink JSON through unchanged)
- `handleMessage` buffer rule: on short/NULL buffer returns `GAKU_E_SIZE` and
  sets `requiredBytes` = UTF-8 bytes + 1 (NUL included), output untouched;
  success also reports `requiredBytes` including the NUL.
- `getAssets`: ordered candidates — host `runtime.gameRoot\GakuMotion.UI`
  first (the Loader's offline package stages the UI next to `version.dll`),
  then `<plugin DLL dir>\GakuMotion.UI` (the plugin usually lives in
  `GakuLoader/plugins`). The first candidate that actually contains
  `index.html` wins; both missing → `GAKU_E_UNAVAILABLE` so the Loader can
  fall back to ImGui. Entry `index.html`, virtual host `gakumotion.invalid`,
  absolute `assetsDir`.

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

Advertised in `motion.hello.supportedMethods`: `bridge.hello`, `bridge.ping`,
`motion.hello`, `state.get`, `player.play/pause/resume/stop/cycle/seek/
seekFrame/stepFrame/scrubBegin/scrubUpdate/scrubEnd/setSpeed/setVolume/
saveVolume/reload`, `playlist.setMode`, `library.refresh/select/delete/
openFolder`, `actors.selectTarget`, `facial.setWeights/save`, `position.set/
save`, `camera.set/save`, `stage.set`, `settings.setPhysics`,
`shell.setTextInputActive`, `appearance.get`.

Implemented but capability-gated (not advertised; enable by adding to the
allow-list): `dialog.pickFile`, `dialog.poll`, `drop.setTarget`, `drop.poll`.

Reserved → structured `not_implemented`: `library.import`, `library.probeFiles`,
`ensemble.*`, `stage.scan/toggleNode/setLoadReplacement`, and the
`gaze./bone./sole./solePitch./studio./experiments./telemetry.` domains.
Anything else → `unknown_method`.

Events: `player.stateChanged` (~10 Hz while playing, immediate after commands),
`permission.changed`, `library.changed`, `loaded.changed`, `playlist.changed`,
`actors.changed`, `camera.changed`, `stage.changed`, `facial.changed`,
`position.changed`, `ensemble.changed`, `settings.changed`, `import.progress`,
`appearance.changed`, `bridge.closed`. Permission loss / injection-session
change finishes an in-flight native scrub through the queue (the frontend
drops its scrub without sending `scrubEnd`).

## Known boundaries

- `stage.set` keeps the ImGui cross-domain side effects (lighting →
  `camera.actorShadowExperimentMode`, `hideBackground` → green screen /
  camera clear / skybox restore). `stage.json` persistence follows existing
  Core semantics: only `loadReplacementKey` is persisted, by the dedicated
  (still gated) `stage.setLoadReplacement` control; visual stage settings are
  not persisted by Core today.
- File dialogs, drag/drop, hotkeys and ShellExecute stay on host services /
  Win32; no Web API substitutes. WebView-child-HWND OLE drop swallowing is a
  live-acceptance risk flagged by the Loader host.
- Direct `gaze` writes and the reserved domains above are intentionally
  unimplemented; `facial.setWeights` uses the same under-lock direct write as
  the ImGui overlay.

## Verification

- `tests/run_web_panel_tests.ps1` — 14 groups / 173 assertions (synthetic).
- `build.ps1 -Target Runtime` — native `GakuMotion.dll` (Release ImGui).
- `build.ps1 -Target Contracts` — existing stage-load contracts (regression).

Game loading, WebView rendering and drag/drop acceptance are manual steps and
are not covered by these offline checks.
