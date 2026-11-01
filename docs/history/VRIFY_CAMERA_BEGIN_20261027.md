# VRify camera-begin coexistence (2026-10-27)

> 历史记录：保留 2026-10-27 当轮实现、部署与验收结论；其中 OPEN/待验收等状态只描述当时版本，不代表当前安装状态。提到的本地日志、恢复文件与原始研究材料未随源码仓附带。当前操作请看[使用指南](../USAGE.md)。

VRify owns RenderPipeline/Manager.BeginCameraRendering even without a device.
The previous Motion bridge tried exclusive leases on both and disabled camera
control on status -5. Motion now validates/subscribes to
`gaku.vrify.camera-begin` v1, keeps its post-original BeforeRender, and skips
those two leases only. The provider excludes VR eye cameras. Without VRify or
without installed VR begin hooks, the independent path remains available.

This is a coordinated VRify/Motion update; an old VRify has no provider.
The identical ABI header is `src/GakuVrifyCameraBegin.h` in Motion and
`src/host/GakuVrifyCameraBegin.h` in VRify. Existing Localify camera-write
subscriptions and capture/page policy are unchanged. Base: b227adf.

Validation: real provider/hook/consumer fixture in VRify
`tests/test-camera-begin.py`; Motion 1101 assertions and ImGui syntax pass;
Release and DiagnosticCapture builds pass. User manual acceptance and the
remaining VR-mode limitation are recorded below. No screenshot-safety acceptance
is inferred.

Pair recovery and hash manifest:
`build/camera_begin_coexist_20261027_213036`.
See VRify `docs/MOTION_CAMERA_COEXIST_20261027.md` for the failure evidence,
contract, candidate hashes, deployment status and real-game checklist.

Deployment follow-up: paired VRify Release and Motion DiagnosticCapture were
installed after the user exited normally; both disk hashes match the recovery
manifest. Host, Localify and recorded configuration hashes remain unchanged.
User manual verification on 2026-10-27:

- PASS without a VR device: Motion camera movement and rotation are restored.
- Known Issue with a VR device and VR mode active: Motion camera control becomes
  inactive. Camera scheduling is not yet aligned with VRify's interface for the
  official camera scheduler; this integration is deferred.

This acceptance covers the stated manual movement/rotation case only. Imported
camera tracks and simultaneous VR/Motion camera composition are not accepted.
No new log verification is claimed. At the user's request, this round is closed
with the deployed pair retained; no further code changes are made.
