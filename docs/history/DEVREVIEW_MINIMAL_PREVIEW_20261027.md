# Developer Preview: minimal page admission and manual shutter guard

> 历史记录：保留 2026-10-27 当轮实现、部署与验收结论；其中 OPEN/待验收等状态只描述当时版本，不代表当前安装状态。提到的本地日志、恢复文件与原始研究材料未随源码仓附带。当前操作请看[使用指南](../USAGE.md)。

DevReview Blocker remains OPEN. This scope supersedes the earlier automatic
capture restore / await-lease candidate. The user explicitly reduced the task to
Photography / Costume takeover and official manual shutter blocking while GM is
active. No network behavior or Loader service is changed.

## Rules derived after the human-operated samples

The `DEVREVIEW_PAGE_OBSERVATION_20261027.md` (original research record, not bundled) records all six page
categories and the GM inactive Live.AutoPhoto continuation cancelled by the old
guard. Background names are shared between Photography and Produce; Costume's
old background also lingers after its TopScreen changes. Therefore these rules
combine exact Active Scene names with the one necessary exact page condition:

- Photography: Active Scene must be `Photography` or one of the ten distinct
  loaded Photography background names recorded in phase 1. Require one exact
  `Campus.Photography.PhotographyScenePresenter`, belonging to a loaded
  `Photography` Scene. A confirmed common current presenter takes precedence;
  a Produce or OutGame root cannot borrow an old Photo component.
- Costume: Active Scene must be exactly
  `env_3d_dress_dressroom-00-00-noon`. Require the current OutGame root's exact
  `Campus.OutGame.CostumeSelectScreenPresenter` TopScreen and no unfinished
  OutGame page transition. Home, Costume idol list and other OutGame pages are denied.
- Everything else is denied. There is no prefix match, time-of-day guessing,
  environment discovery, inherited role grant, or general page framework.
  Background names not captured in phase 1 stay unsupported in this Preview.

Scene ownership values remain only to bind the existing fresh actor and clean
up the previous session. A read failure does not mean the old Scene was destroyed:
restoration evidence is retained unless replacement or unloading is confirmed.
An existing live Photo root can be read on a denied background for native cleanup;
that read itself grants no permission.

## Capture behavior

- GM session inactive manual request: immediately call original, including all
  original awaits. Do not check layout/owner/thread, restore state, acquire a
  lease, consult scene policy, cancel, or create a diagnostic transaction first.
- Automatic / public capture: original behavior. Their hooks are not installed.
- GM active manual request: reject Click, pressed rapid-shutter entry and the
  direct Photography manual async task. Key release reaches the native handler.
- GM active means playback (including pause/preview), owned camera or enabled
  stage application. Saved settings, capture suspension and old restore flags
  are not session activation conditions. The guard does not create a suspension.
- A manual cancellation failure immediately disables takeover through the
  existing pending revocation path; it does not start a capture or network action.

Six existing hooks remain: Photography manual MoveNext, PhotoButton Click/Press,
and OutGame WillChange/DidChange/Destroy for Costume's page latch. Five prior
hooks are removed: Live.AutoPhoto, Photography.CaptureTexture,
Photography.StockCapture, Live.ManualPhoto and Shared.CanTakePhoto. Their metadata
and asynchronous restore callbacks are no longer takeover prerequisites.

## Validation and remaining acceptance

Offline tests check inactive direct calls and awaits with zero begin/continue/
end/fault callbacks, unknown owner / invalid layout, automatic/public passthrough,
active manual refusal, Stop followed by native behavior, and the exact positive /
negative scene and presenter combinations from the sampled pages.

Offline tests do not certify game rendering, native cancellation UX, or repair
of the reported black screen. After replacing the candidate with the game closed,
human acceptance is still required:

1. GM OFF: official Photography still takes a normal photo; ordinary Live and
   Produce Live can finish/skip and return normally.
2. Supported Photography / Costume: controls can start GM; Home, Produce and both
   Live categories cannot start it. Unlisted backgrounds stay disabled.
3. Photography GM ON: click, held/rapid and direct manual entry produce no photo.
4. Stop: normal official manual photography works again.

Use a clear local GM pose/camera sentinel for takeover checks. Do not upload a GM
photo or probe official servers. The existing diagnostic page log remains for
correlating scene, session and manual refusal; no automated game operation is used.

Final offline runner: **1096 passed, 0 failed**, ImGui syntax passed. Release and
DiagnosticCapture builds passed. PE inspection passed: page/capture diagnostics
are absent from normal Release, the obsolete restore-failure injection is absent
from both artifacts, and neither artifact imports Winsock or consumes the former
process-lifetime network service. Loader source and installed hash are unchanged.
Implementation: `14c63f90939d9c18cee19082b142104b1427bf74`. The user exited the
black-screen process, and the agent confirmed no game process remained before
installing DiagnosticCapture at 2026-10-27 20:47:12 +08:00. Installed SHA256:
`C6CF75E376E91295BC6A4B254C4A1D9D3869800700E652CFA9219C86B27EE234`.
The old observation DLL is retained as the recovery artifact. Loader hash still
matches `B1FC95D5452445CD8349819F4294FA554FBE78FFCE60572DF5AAD9644F291E78`.
Artifacts, build/test/PE reports, pre-deployment log and manifest are saved in
`build/preview_manual_only_20261027_204354`.
The first human acceptance of this installed candidate is recorded below.

## First game acceptance of 14c63f9

The user supplied these results from the installed DiagnosticCapture candidate.
The running process was 65344, started at 2026-10-27 20:48:31 +08:00; on-disk
hash remained the installed C6CF75E3 candidate. The scope must remain as tested:

| Behavior | Human acceptance / local evidence |
| --- | --- |
| Photography admission | Accepted in several randomly selected Photography scenes; do not generalize to every scene/time variant. |
| GM stopped / idle official Photography | Accepted: normal photography works. |
| GM motion playing manual shutter | Accepted: button press is blocked. Log contains 26 Press and 26 Click rejections, all GM active and lease depth 0. This does not independently exercise every possible direct native entry. |
| Desired settings between Photography scenes | Accepted: settings carry over. |
| Settings applied in Live | Accepted: GM settings do not apply in Live. Stored desired configuration is retained. |
| Costume | Still failed; exact page facts below. |
| Live clicked skip | User confirmed clicking skip now exits normally. A few seconds of black display remain. Natural playback ending is not yet accepted by this report. |

The new session has no GM inactive cancellation, lease, restore or continuation
gate events. Existing sampled Live-to-next-page log gaps are not black-screen
durations: scene polling is throttled and unchanged states emit ten-second
heartbeats, with no skip/fade markers. These records cannot establish whether
the remaining delay is native or influenced by GM. No Live hook/profiling work
is added for this unproven boundary.

Evidence files under the candidate folder: `first-acceptance-gakuloader.log`,
`first-acceptance-guard-events.log`, `first-acceptance-page-observations.log`,
and `first-acceptance-transitions.json`.

## Bounded Costume follow-up

In the failed Costume sample, the Active Scene is the exact dressing-room
background, handle `-544926`, loaded=1; current root is exact
`Campus.OutGame.OutGameScenePresenter` in loaded OutGame, handle `-481194`.
The fresh runtime-invoked TopScreen is exact `CostumeSelectScreenPresenter`,
while admission still reports `costume_page=0`, hooks ready, depth 0. Thus the
additional once-per-DidChange Costume identity latch vetoes a valid fresh page.
The log does not distinguish callback timing from the old direct getter path;
neither is asserted as a separately proven cause.

The minimal follow-up removes that cached identity. The existing latch now only
means page stable: WillChange=false, DidChange=true, Destroy=false. Page identity
continues to use the fresh exact runtime-invoked TopScreen. This permits child
navigation from idol list to Costume after the outer DidChange, without adding
a hook or widening the exact scene/presenter allowlist. A stale Costume top
cannot reopen admission while an outer transition is pending.

Offline regression additionally covers late child selection, old top during a
transition, a stable idol list, null top and OutGame destruction. The updated full
runner passed **1101 assertions, 0 failures**, plus ImGui syntax. Both builds and
the PE feature/network boundary check passed. No Photography
capture or Live capture behavior is changed by this follow-up. Costume still
requires new human acceptance before it is marked passed. Blocker remains OPEN.

Costume follow-up implementation: `b7d189341953ecadd8d323631a46dbbc908d6d09`.
The user exited the game, and no game process remained before deployment at
2026-10-27 21:18:54 +08:00. Installed DiagnosticCapture SHA256:
`0F8FC1B692AA8573EB76188726AC56499BFC9D2FCF97425D657542FFFBBE7ED2`.
The previously accepted C6CF75E3 candidate is backed up. Loader hash is unchanged.
Recovery files, builds, tests, PE report, pre-deployment log and manifest:
`build/costume_page_stability_20261027_211612`.
This confirms disk deployment only; Costume game acceptance is still pending.
