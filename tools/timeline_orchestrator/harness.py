"""Standard-library CLI and mock preview; never calls Unity or an audio clock."""

from __future__ import annotations

import argparse
from collections.abc import Mapping
from dataclasses import fields, is_dataclass
import html
import json
from pathlib import Path
from typing import Any

from .control import MockControlAPI, Orchestrator, TransportFrame
from .core import DesiredState, Timeline


# This registry is trusted harness configuration, independent of timeline input.
# A production adapter must populate its own allowlist from approved background
# bindings, validating scene generation and object identity on every write.
MOCK_OBJECT_IDS = (
    "prop.banner", "prop.panel", "prop.desk", "prop.chair", "prop.plant", "prop.shelf"
)
DEFAULT_TIMELINE = Path(__file__).with_name("examples") / "trailer_scene_transition.json"


def _plain(value: Any) -> Any:
    """Serialize immutable state mappings without dataclasses.asdict deepcopy."""
    if is_dataclass(value) and not isinstance(value, type):
        return {field.name: _plain(getattr(value, field.name)) for field in fields(value)}
    if isinstance(value, Mapping):
        return {str(key): _plain(item) for key, item in value.items()}
    if isinstance(value, (tuple, list)):
        return [_plain(item) for item in value]
    return value


def _baseline(second_scene: bool = False) -> DesiredState:
    visibility = {object_id: True for object_id in MOCK_OBJECT_IDS}
    # Restoration must preserve a natively hidden object, not force everything on.
    visibility["prop.chair"] = False
    if second_scene:
        visibility["prop.banner"] = False
    return DesiredState(
        visibility=visibility,
        background=(0.24, 0.16, 0.09, 1.0) if second_scene else (0.12, 0.20, 0.28, 1.0),
        camera_mode="Follow" if second_scene else "Hold",
    )


def _validate_mock_scope(timeline: Timeline) -> None:
    unknown = set(timeline.evaluate(0).visibility) - set(MOCK_OBJECT_IDS)
    if unknown:
        raise ValueError(
            "Mock allowlist rejects object IDs: " + ", ".join(sorted(unknown))
            + "; allowed IDs: " + ", ".join(MOCK_OBJECT_IDS)
        )


def run_demo(timeline: Timeline) -> list[dict[str, Any]]:
    """Replay discontinuous transport snapshots, not timeline events or wall time."""
    _validate_mock_scope(timeline)
    control = MockControlAPI(_baseline(), scene_epoch="scene-1")
    orchestrator = Orchestrator(timeline, control)
    rows: list[dict[str, Any]] = []
    cursor = 0
    epoch = "scene-1"
    duration = timeline.duration

    def record(action: str, frame: TransportFrame | None, desired: DesiredState | None) -> None:
        nonlocal cursor
        rows.append({
            "step": len(rows),
            "action": action,
            "transport": _plain(frame),
            "desired": _plain(desired),
            "applied": _plain(control.state),
            "camera_clear_flags": control.clear_flags,
            "control_calls": _plain(control.history[cursor:]),
        })
        cursor = len(control.history)

    def update(action: str, ratio: float, status: str = "playing", *,
               loop: bool = False, ready: bool = True) -> None:
        frame = TransportFrame(
            performance_time=duration * ratio,
            status=status,
            scene_epoch=epoch,
            loop=loop,
            scene_ready=ready,
        )
        record(action, frame, orchestrator.update(frame))

    update("play", 0)
    update("forward_skip_into_hide_sequence", 2.75 / 16)
    update("forward_skip_past_all_hidden", 7.5 / 16)
    update("pause_same_time", 7.5 / 16, "paused")
    update("backward_seek_while_paused", 3.25 / 16, "paused")
    update("resume", 6.5 / 16)
    update("before_host_scene_change", 0.5)
    orchestrator.suspend()
    record("host_scene_will_change_restore_old_scope", None, None)
    update("scene_pending_old_epoch", 0.5, "paused", ready=False)

    # Scene loading belongs to the host. Mock rebind simulates fresh approved
    # bindings; timeline JSON never executes a load or resolves arbitrary objects.
    epoch = "scene-2"
    control.rebind(_baseline(second_scene=True), scene_epoch=epoch, clear_flags="SolidColor")
    record("host_rebind_new_scene", None, None)
    update("scene_pending_new_epoch", 0.5, "paused", ready=False)
    update("scene_ready_recompute_current_time", 0.5, "paused")
    update("paused_seek_into_restore_sequence", 12 / 16, "paused")
    update("resume_before_loop_boundary", 1 - 1e-6, loop=True)
    update("exact_loop_boundary", 1, loop=True)
    update("loop_with_overshoot", 1 + 2.75 / 16, loop=True)
    update("backward_seek_after_loop", 0, "paused", loop=True)
    update("stop_restore_new_scene_baseline", 0, "stopped", loop=True)
    update("repeated_stop", 0, "stopped", loop=True)
    return rows


def write_preview(timeline: Timeline, path: Path, title: str) -> None:
    """Embed engine-evaluated samples in a dependency-free inspection artifact."""
    count = 256
    samples = []
    for index in range(count + 1):
        time = timeline.duration * index / count
        samples.append({
            "time": time,
            "clamp": _plain(timeline.evaluate(time)),
            "loop": _plain(timeline.evaluate(time, loop=True)),
        })
    data = json.dumps(samples, ensure_ascii=False, allow_nan=False).replace("<", "\\u003c")
    document = _PREVIEW_HTML.replace("__TITLE__", html.escape(title)).replace("__SAMPLES__", data)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(document, encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Timeline Orchestrator v0: deterministic mock harness")
    parser.add_argument("--timeline", type=Path, default=DEFAULT_TIMELINE)
    parser.add_argument("--log", type=Path, help="write JSONL execution log; otherwise use stdout")
    parser.add_argument("--preview", type=Path, help="write self-contained HTML state preview")
    args = parser.parse_args(argv)
    try:
        resolved_paths: dict[Path, str] = {}
        for label, path in (("timeline", args.timeline), ("log", args.log), ("preview", args.preview)):
            if path is None:
                continue
            resolved = path.resolve()
            if resolved in resolved_paths:
                raise ValueError(f"{label} and {resolved_paths[resolved]} must use different file paths")
            resolved_paths[resolved] = label
        timeline = Timeline.load(args.timeline)
        rows = run_demo(timeline)
        output = "".join(json.dumps(row, ensure_ascii=False, allow_nan=False) + "\n" for row in rows)
        if args.log:
            args.log.parent.mkdir(parents=True, exist_ok=True)
            args.log.write_text(output, encoding="utf-8")
        else:
            print(output, end="")
        if args.preview:
            write_preview(timeline, args.preview, args.timeline.name)
    except (ValueError, TypeError, RuntimeError, OSError) as error:
        parser.error(str(error))
    return 0


_PREVIEW_HTML = r"""<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Timeline Orchestrator 模拟预览 · __TITLE__</title>
<style>
:root{color-scheme:dark;font:15px/1.6 system-ui,sans-serif;background:#101319;color:#e8edf4}
body{max-width:1000px;margin:32px auto;padding:0 22px}h1{font-size:25px;margin-bottom:8px}
p{color:#adb8c9}section{background:#1a202a;border:1px solid #303b4c;border-radius:12px;padding:20px;margin:18px 0}
.tag{font-size:12px;border:1px solid #748293;border-radius:5px;padding:3px 7px;color:#c5d0e0}
.controls{display:flex;gap:14px;align-items:center;flex-wrap:wrap}input[type=range]{flex:1;min-width:220px;accent-color:#9abaff}
button{background:#28364a;color:inherit;border:1px solid #536682;border-radius:6px;padding:5px 12px;cursor:pointer}
output{font-variant-numeric:tabular-nums;min-width:95px}#stage{min-height:190px;transition:none}
#objects{display:grid;grid-template-columns:repeat(auto-fit,minmax(135px,1fr));gap:12px;padding:20px 0}
.object{border-radius:8px;padding:13px 9px;text-align:center;background:#e2eafa;color:#16243a;border:2px solid #f0f5ff}
.object.hidden{background:#26303b;color:#a8b1c0;border:2px dashed #778495;opacity:.55}
#camera{display:inline-block;background:#18202bd9;padding:5px 10px;border-radius:6px;color:#f0f4fa}
pre{white-space:pre-wrap;font:13px/1.5 ui-monospace,monospace;color:#bfd5ef;max-height:330px;overflow:auto}
.note{font-size:13px}a{color:#a8c6ff}
</style></head><body>
<span class="tag">独立 Mock · 离线模拟</span><h1>Timeline Orchestrator v0</h1>
<p>__TITLE__ · 拖动滑块直接查看目标状态。每一帧均由 Python 引擎独立 evaluate(t) 产生，跳到任意帧无需重放事件。</p>
<section><div class="controls"><button id="start">0 秒</button><input id="time" aria-label="PerformanceTime 采样帧" type="range" min="0" max="256" step="1" value="0"><button id="end">末尾</button><output id="clock"></output><label><input id="loop" type="checkbox">Loop</label></div>
<p class="note">采样分辨率为时长 / 256；滑块跳转不表示播放。Loop 下末尾样本直接映射为 0 秒。</p></section>
<section id="stage"><span id="camera"></span><div id="objects"></div></section>
<section><strong>当前完整目标状态</strong><pre id="state"></pre></section>
<p class="note">物件卡片表示逻辑可见性，背景为归一化 RGBA 数值预览。这是模拟示意，不是游戏截图、Unity 层级、颜色空间或最终视觉验收。场景加载由宿主负责；CLI JSONL 另含暂停、倒退 Seek、场景 pending/ready 和 Stop 恢复记录。</p>
<script id="samples" type="application/json">__SAMPLES__</script><script>
const samples=JSON.parse(document.getElementById('samples').textContent);
const slider=document.getElementById('time'), loop=document.getElementById('loop');
function render(){
 const sample=samples[Number(slider.value)], state=loop.checked?sample.loop:sample.clamp;
 document.getElementById('clock').textContent=sample.time.toFixed(3)+' s';
 document.getElementById('camera').textContent='镜头模式：'+(state.camera_mode??'未接管');
 const bg=state.background;
 document.getElementById('stage').style.backgroundColor=bg?'rgba('+bg.slice(0,3).map(v=>Math.round(v*255)).concat(bg[3]).join(',')+')':'#1a202a';
 const objects=document.getElementById('objects');objects.replaceChildren();
 for(const [id,visible] of Object.entries(state.visibility)){
  const card=document.createElement('div');card.className='object'+(visible?'':' hidden');
  const label=document.createElement('div');label.textContent=id;
  const flag=document.createElement('small');flag.textContent=visible?'显示':'隐藏';
  card.append(label,flag);objects.append(card);
 }
 document.getElementById('state').textContent=JSON.stringify(state,null,2);
}
slider.addEventListener('input',render);loop.addEventListener('change',render);
document.getElementById('start').onclick=()=>{slider.value=0;render()};
document.getElementById('end').onclick=()=>{slider.value=256;render()};render();
</script></body></html>"""
