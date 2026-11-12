"""Strict JSON compiler and history-independent absolute-time state evaluator."""
from __future__ import annotations

from bisect import bisect_right
from dataclasses import dataclass
import json
import math
from pathlib import Path
from types import MappingProxyType
from typing import Any, Mapping


class TimelineError(ValueError):
    """An invalid document or time; nothing may be applied to the host."""


CAMERA_MODES = frozenset({"Track", "Hold", "Follow"})
Color = tuple[float, float, float, float]


def _number(value: Any, where: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise TimelineError(f"{where}: expected a finite number")
    try:
        result = float(value)
    except (ValueError, OverflowError) as exc:
        raise TimelineError(f"{where}: expected a finite number") from exc
    if not math.isfinite(result):
        raise TimelineError(f"{where}: expected a finite number")
    return result


def _id(value: Any, where: str) -> str:
    # Logical identifiers only: no Unity paths, pointers, or reflection selectors.
    if (not isinstance(value, str) or not value or len(value) > 128
            or any(not (c.isascii() and (c.isalnum() or c in "._-")) for c in value)):
        raise TimelineError(f"{where}: expected a logical ID (ASCII letters, digits, ._-)")
    return value


def _fields(value: Any, allowed: set[str], required: set[str], where: str) -> dict:
    if not isinstance(value, dict):
        raise TimelineError(f"{where}: expected an object")
    if set(value) - allowed or required - set(value):
        raise TimelineError(f"{where}: unknown or missing fields")
    return value


def _color(value: Any, where: str) -> Color:
    if not isinstance(value, (list, tuple)) or len(value) != 4:
        raise TimelineError(f"{where}: expected RGBA array with four components")
    result = tuple(_number(c, where) for c in value)
    if any(c < 0 or c > 1 for c in result):
        raise TimelineError(f"{where}: RGBA components must be in [0, 1]")
    return result  # type: ignore[return-value]


@dataclass(frozen=True)
class DesiredState:
    """Complete values for owned properties; None means property is unowned."""

    visibility: Mapping[str, bool]
    background: Color | None = None
    camera_mode: str | None = None

    def __post_init__(self) -> None:
        copied = dict(self.visibility)
        for object_id, visible in copied.items():
            _id(object_id, "state.visibility")
            if type(visible) is not bool:
                raise TimelineError("state.visibility: values must be boolean")
        object.__setattr__(self, "visibility", MappingProxyType(copied))
        if self.background is not None:
            object.__setattr__(self, "background", _color(self.background, "state.background"))
        if self.camera_mode is not None and (not isinstance(self.camera_mode, str)
                                             or self.camera_mode not in CAMERA_MODES):
            raise TimelineError("state.camera_mode: expected Track, Hold or Follow")

    def to_dict(self) -> dict:
        return {
            "visibility": dict(self.visibility),
            "background": list(self.background) if self.background is not None else None,
            "camera_mode": self.camera_mode,
        }


@dataclass(frozen=True)
class ControlScope:
    objects: tuple[str, ...]
    background: bool = False
    camera_mode: bool = False


@dataclass(frozen=True)
class _Track:
    times: tuple[float, ...]
    values: tuple[Any, ...]
    linear: bool = False

    def sample(self, time: float) -> Any:
        index = bisect_right(self.times, time) - 1
        value = self.values[index]
        if not self.linear or index + 1 == len(self.times):
            return value
        end = self.values[index + 1]
        weight = (time - self.times[index]) / (self.times[index + 1] - self.times[index])
        return tuple(a + (b - a) * weight for a, b in zip(value, end))


def _unique_pairs(pairs: list[tuple[str, Any]]) -> dict:
    result: dict = {}
    for key, value in pairs:
        if key in result:
            raise TimelineError(f"duplicate JSON member: {key}")
        result[key] = value
    return result


@dataclass(frozen=True)
class Timeline:
    duration: float
    scope: ControlScope
    _visibility: Mapping[str, _Track]
    _background: _Track | None
    _camera: _Track | None

    @classmethod
    def load(cls, path: str | Path) -> Timeline:
        try:
            document = json.loads(Path(path).read_text(encoding="utf-8-sig"),
                                  object_pairs_hook=_unique_pairs)
        except (json.JSONDecodeError, UnicodeError) as exc:
            raise TimelineError(f"invalid timeline JSON: {exc}") from exc
        return cls.from_dict(document)

    @classmethod
    def from_dict(cls, document: dict) -> Timeline:
        doc = _fields(document, {"version", "duration", "groups", "tracks"},
                      {"version", "duration", "tracks"}, "timeline")
        if type(doc["version"]) is not int or doc["version"] != 1:
            raise TimelineError("version: expected integer 1")
        duration = _number(doc["duration"], "duration")
        if duration <= 0:
            raise TimelineError("duration: must be positive")
        raw_groups = doc.get("groups", {})
        if not isinstance(raw_groups, dict):
            raise TimelineError("groups: expected an object")
        groups: dict[str, tuple[str, ...]] = {}
        for name, members in raw_groups.items():
            _id(name, "groups")
            if not isinstance(members, list) or not members:
                raise TimelineError(f"groups.{name}: expected a nonempty array")
            ids = tuple(_id(member, f"groups.{name}") for member in members)
            if len(set(ids)) != len(ids):
                raise TimelineError(f"groups.{name}: duplicate object ID")
            groups[name] = ids
        if not isinstance(doc["tracks"], list) or not doc["tracks"]:
            raise TimelineError("tracks: expected a nonempty array")

        visibility: dict[str, _Track] = {}
        background = camera = None
        for track_index, raw_track in enumerate(doc["tracks"]):
            where = f"tracks[{track_index}]"
            track = _fields(raw_track, {"kind", "object", "group", "keys", "interpolation"},
                            {"kind", "keys"}, where)
            kind = track["kind"]
            if kind not in ("visibility", "background", "camera_mode"):
                raise TimelineError(f"{where}.kind: unsupported state track")
            targets: tuple[str, ...] = ()
            if kind == "visibility":
                if ("object" in track) == ("group" in track) or "interpolation" in track:
                    raise TimelineError(f"{where}: visibility needs exactly one object or group")
                if "group" in track:
                    group_id = _id(track["group"], f"{where}.group")
                    if group_id not in groups:
                        raise TimelineError(f"{where}: unknown group {group_id}")
                    targets = groups[group_id]
                else:
                    targets = (_id(track["object"], f"{where}.object"),)
                if any(target in visibility for target in targets):
                    raise TimelineError(f"{where}: overlapping visibility tracks")
            else:
                if "object" in track or "group" in track:
                    raise TimelineError(f"{where}: global state must not have a target")
                if (kind == "background" and background is not None
                        or kind == "camera_mode" and camera is not None):
                    raise TimelineError(f"{where}: duplicate global state track")
                if kind == "camera_mode" and "interpolation" in track:
                    raise TimelineError(f"{where}: camera mode is step state")
            interpolation = track.get("interpolation", "step")
            if interpolation not in ("step", "linear"):
                raise TimelineError(f"{where}: interpolation must be step or linear")
            keys = track["keys"]
            if not isinstance(keys, list) or not keys:
                raise TimelineError(f"{where}.keys: expected a nonempty array")
            times: list[float] = []
            values: list[Any] = []
            object_keys: dict[str, list[tuple[float, bool]]] = {target: [] for target in targets}
            previous = -1.0
            for key_index, raw_key in enumerate(keys):
                key_where = f"{where}.keys[{key_index}]"
                allowed = {"time", "value", "stagger", "order"} if kind == "visibility" else {"time", "value"}
                key = _fields(raw_key, allowed, {"time", "value"}, key_where)
                time = _number(key["time"], f"{key_where}.time")
                if time < 0 or time > duration or time <= previous:
                    raise TimelineError(f"{key_where}: times must increase strictly in [0, duration]")
                if key_index == 0 and time != 0:
                    raise TimelineError(f"{where}: first key must be at time 0")
                previous = time
                value = key["value"]
                if kind == "visibility":
                    if type(value) is not bool:
                        raise TimelineError(f"{key_where}: visibility must be boolean")
                    stagger = _number(key.get("stagger", 0), f"{key_where}.stagger")
                    order = key.get("order", "forward")
                    if stagger < 0 or order not in ("forward", "reverse"):
                        raise TimelineError(f"{key_where}: invalid stagger or order")
                    if key_index == 0 and stagger != 0:
                        raise TimelineError(f"{key_where}: initial state cannot be staggered")
                    ordered = targets if order == "forward" else tuple(reversed(targets))
                    for offset, target in enumerate(ordered):
                        expanded_time = time + offset * stagger
                        if not math.isfinite(expanded_time) or expanded_time > duration:
                            raise TimelineError(f"{key_where}: stagger extends beyond duration")
                        object_keys[target].append((expanded_time, value))
                elif kind == "background":
                    value = _color(value, key_where)
                elif not isinstance(value, str) or value not in CAMERA_MODES:
                    raise TimelineError(f"{key_where}: expected Track, Hold or Follow")
                times.append(time)
                values.append(value)
            if kind == "visibility":
                for target, expanded in object_keys.items():
                    # Reject crossing/colliding stagger instructions instead of
                    # silently letting an earlier instruction override a later one.
                    if any(a[0] >= b[0] for a, b in zip(expanded, expanded[1:])):
                        raise TimelineError(f"{where}: unordered expanded keys for {target}")
                    visibility[target] = _Track(tuple(item[0] for item in expanded),
                                                tuple(item[1] for item in expanded))
            else:
                compiled = _Track(tuple(times), tuple(values), interpolation == "linear")
                if kind == "background":
                    background = compiled
                else:
                    camera = compiled
        return cls(duration, ControlScope(tuple(visibility), background is not None, camera is not None),
                   MappingProxyType(visibility), background, camera)

    def evaluate(self, performance_time: float, loop: bool = False) -> DesiredState:
        time = max(0.0, _number(performance_time, "PerformanceTime"))
        if type(loop) is not bool:
            raise TimelineError("loop: expected boolean")
        time = time % self.duration if loop else min(time, self.duration)
        return DesiredState(
            {target: track.sample(time) for target, track in self._visibility.items()},
            self._background.sample(time) if self._background else None,
            self._camera.sample(time) if self._camera else None,
        )
