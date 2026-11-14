"""Compare bounded, read-only QPC samples; this does not establish a job fence."""
import argparse
import json
import re
from pathlib import Path

HEADER = re.compile(r"\[LiveEvalTiming\] pid=(\d+) count=(\d+) epoch=(\d+) qpc_frequency=(\d+)")
ROW = re.compile(r"\[LiveEvalTiming\] index=(\d+) start_qpc=(\d+) end_qpc=(\d+) thread=(\d+)")


def compare(report, log_text, pid):
    timing = report.get("actor_swing_timing", {})
    frequency = timing.get("qpc_frequency", 0)
    groups = []
    for line in log_text.splitlines():
        header = HEADER.search(line)
        if header:
            group_pid, count, epoch, hz = map(int, header.groups())
            groups.append({"pid": group_pid, "count": count, "epoch": epoch, "frequency": hz, "rows": []})
            continue
        row = ROW.search(line)
        if row and groups:
            index, start, end, thread = map(int, row.groups())
            groups[-1]["rows"].append({"index": index, "start": start, "end": end, "thread": thread})
    candidates = [g for g in groups if g["pid"] == pid and g["frequency"] == frequency and frequency > 0]
    if not candidates:
        raise ValueError("No matching process and QPC frequency in evaluation log")
    group = candidates[-1]
    evaluations = [r for r in group["rows"] if r["start"] and r["end"] >= r["start"]]
    swings = timing.get("samples", [])
    completed = [s for s in swings if s.get("start_qpc", 0) and s.get("end_qpc", 0) >= s["start_qpc"]]
    overlapping, after_eval = 0, 0
    for swing in completed:
        overlaps = [e for e in evaluations if e["start"] <= swing["end_qpc"] and swing["start_qpc"] <= e["end"]]
        if overlaps:
            overlapping += 1
            if all(swing["end_qpc"] > e["end"] for e in overlaps):
                after_eval += 1
    lists = report.get("actor_swing_lists", {}).get("lists", [])
    stable = [r for r in lists if r.get("layout_verified") and r.get("samples", 0) > 0 and
              r.get("failures", 0) == 0 and r.get("changes", 0) == 0 and
              0 <= r.get("length", -1) <= r.get("capacity", -1) and r.get("stride", 0) > 0]
    return {"pid": pid, "epoch": group["epoch"], "qpc_frequency": frequency,
            "evaluation_logged": len(group["rows"]), "evaluation_complete": len(evaluations),
            "swing_logged": len(swings), "swing_complete": len(completed),
            "swing_overlapping_evaluation": overlapping, "overlap_ending_after_evaluation": after_eval,
            "stable_list_instances": len(stable),
            "stable_list_live_bytes": sum(r["length"] * r["stride"] for r in stable),
            "restoration_ready": False,
            "interpretation": "Bounded overlapping samples only; zero overlap or completion does not prove job synchronization."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("log", type=Path)
    parser.add_argument("--pid", required=True, type=int)
    args = parser.parse_args()
    try:
        result = compare(json.loads(args.report.read_text(encoding="utf-8")),
                         args.log.read_text(encoding="utf-8", errors="replace"), args.pid)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.error(str(error))
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
