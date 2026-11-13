"""Narrow host contract, lifecycle orchestration, and an allowlisted mock host."""
from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Protocol

from .core import ControlScope, DesiredState, Timeline, TimelineError, _number


class ControlError(RuntimeError):
    """Host rejected a control transaction; caller must report/retry explicitly."""


class StaleSceneError(ControlError):
    """Scene identity changed; never write an old snapshot into new objects."""


@dataclass(frozen=True)
class Baseline:
    scene_epoch: str
    state: DesiredState
    # Adapter-owned restoration data, e.g. camera clearFlags, leases, stable IDs.
    # This token must remain in-process; it is never loaded from timeline JSON.
    opaque: Any = None


class ControlAPI(Protocol):
    """A host implementation runs these operations on its Unity main thread.

    Each call validates scene identity and all targets before mutation. Apply
    is absolute, idempotent and atomic for the supplied scope. Capture must
    fail for unavailable/unsupported targets instead of inventing defaults.
    Restore also restores adapter-owned data and releases property ownership.
    Object IDs are capabilities registered by the host, never Unity paths.
    """

    def capture(self, scope: ControlScope, scene_epoch: str) -> Baseline: ...

    def apply(self, state: DesiredState, scene_epoch: str) -> None: ...

    def restore(self, baseline: Baseline) -> None: ...


@dataclass(frozen=True)
class TransportFrame:
    performance_time: float
    status: str
    scene_epoch: str
    loop: bool = False
    scene_ready: bool = True

    def __post_init__(self) -> None:
        _number(self.performance_time, "PerformanceTime")
        if self.status not in ("playing", "paused", "preview", "stopped"):
            raise TimelineError("transport status: expected playing, paused, preview or stopped")
        if not isinstance(self.scene_epoch, str) or not self.scene_epoch:
            raise TimelineError("scene_epoch: expected a nonempty opaque string")
        if type(self.loop) is not bool or type(self.scene_ready) is not bool:
            raise TimelineError("loop and scene_ready: expected boolean")


class Orchestrator:
    """No wall clock, timers, event cursor, or accumulated delta time.

    Paused and Preview frames are still sampled: a paused seek is a valid state
    change. The host supplies frozen PerformanceTime for an ordinary pause.
    Stop restores the captured baseline, not evaluate(0). A loading frame
    suspends ownership; readiness re-captures before applying the current time.
    """

    def __init__(self, timeline: Timeline, control: ControlAPI):
        self.timeline = timeline
        self.control = control
        self._baseline: Baseline | None = None

    def update(self, frame: TransportFrame) -> DesiredState | None:
        if frame.status == "stopped" or not frame.scene_ready:
            self.stop()
            return None
        # Pure evaluation precedes capture/mutation so invalid inputs never
        # acquire ownership. Every update describes the complete owned state.
        desired = self.timeline.evaluate(frame.performance_time, frame.loop)
        if self._baseline is not None and self._baseline.scene_epoch != frame.scene_epoch:
            self._baseline = None
        try:
            if self._baseline is None:
                baseline = self.control.capture(self.timeline.scope, frame.scene_epoch)
                if baseline.scene_epoch != frame.scene_epoch:
                    raise StaleSceneError("capture returned a different scene epoch")
                self._baseline = baseline
            self.control.apply(desired, frame.scene_epoch)
        except StaleSceneError:
            self._baseline = None
            raise
        return desired

    def stop(self) -> None:
        if self._baseline is None:
            return
        try:
            self.control.restore(self._baseline)
        except StaleSceneError:
            # Already unloaded: discard the old token, never target the new scene.
            pass
        # Other failures propagate and retain the token so restoration can retry.
        self._baseline = None

    def suspend(self) -> None:
        """Host calls before unloading a still-live scene or releasing control."""
        self.stop()


class MockControlAPI:
    """In-memory host whose initial IDs are its independently supplied allowlist.

    Visibility models activeSelf, not effective activeInHierarchy. Real adapters
    should register non-nested background roots. No object lookup by path exists.
    """

    def __init__(self, initial_state: DesiredState, scene_epoch: str = "scene-1",
                 *, clear_flags: str = "Skybox"):
        self._validate_epoch(scene_epoch)
        self.state = initial_state
        self.scene_epoch = scene_epoch
        self._seen_epochs = {scene_epoch}
        self.clear_flags = clear_flags
        self.history: list[dict] = []

    @staticmethod
    def _validate_epoch(epoch: str) -> None:
        if not isinstance(epoch, str) or not epoch:
            raise ControlError("scene epoch must be a nonempty string")

    def _check_epoch(self, epoch: str) -> None:
        if epoch != self.scene_epoch:
            raise StaleSceneError(f"scene changed: expected {epoch}, current {self.scene_epoch}")

    def _check_scope(self, scope: ControlScope) -> None:
        unknown = set(scope.objects) - set(self.state.visibility)
        if unknown:
            raise ControlError(f"object IDs are not registered: {', '.join(sorted(unknown))}")
        if scope.background and self.state.background is None:
            raise ControlError("background control is unsupported in this scene")
        if scope.camera_mode and self.state.camera_mode is None:
            raise ControlError("camera mode control is unsupported in this scene")

    def capture(self, scope: ControlScope, scene_epoch: str) -> Baseline:
        self._check_epoch(scene_epoch)
        self._check_scope(scope)
        owned = DesiredState(
            {target: self.state.visibility[target] for target in scope.objects},
            self.state.background if scope.background else None,
            self.state.camera_mode if scope.camera_mode else None,
        )
        self.history.append({"operation": "capture", "scene_epoch": scene_epoch,
                             "state": owned.to_dict()})
        return Baseline(scene_epoch, owned, {"clear_flags": self.clear_flags})

    def _set(self, state: DesiredState, scene_epoch: str) -> None:
        self._check_epoch(scene_epoch)
        self._check_scope(ControlScope(tuple(state.visibility), state.background is not None,
                                       state.camera_mode is not None))
        self.state = DesiredState(
            {**self.state.visibility, **state.visibility},
            state.background if state.background is not None else self.state.background,
            state.camera_mode if state.camera_mode is not None else self.state.camera_mode,
        )

    def apply(self, state: DesiredState, scene_epoch: str) -> None:
        self._set(state, scene_epoch)
        if state.background is not None:
            self.clear_flags = "SolidColor"
        self.history.append({"operation": "apply", "scene_epoch": scene_epoch,
                             "state": state.to_dict()})

    def restore(self, baseline: Baseline) -> None:
        self._check_epoch(baseline.scene_epoch)
        if baseline.state.background is not None and (
                not isinstance(baseline.opaque, dict)
                or not isinstance(baseline.opaque.get("clear_flags"), str)):
            raise ControlError("invalid mock background restoration token")
        self._set(baseline.state, baseline.scene_epoch)
        if baseline.state.background is not None:
            self.clear_flags = baseline.opaque["clear_flags"]
        self.history.append({"operation": "restore", "scene_epoch": baseline.scene_epoch,
                             "state": baseline.state.to_dict()})

    def rebind(self, state: DesiredState, scene_epoch: str, *, clear_flags: str = "Skybox") -> None:
        self._validate_epoch(scene_epoch)
        if scene_epoch in self._seen_epochs:
            raise ControlError("rebind must use a fresh scene epoch")
        self._seen_epochs.add(scene_epoch)
        self.state = state
        self.scene_epoch = scene_epoch
        self.clear_flags = clear_flags
        self.history.append({"operation": "rebind", "scene_epoch": scene_epoch,
                             "state": state.to_dict()})
