"""Exercise real NativeAOT exports and the production C++ bridge with synthetic data."""
from __future__ import annotations
import argparse
import ctypes
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import subprocess

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native-dll", type=Path, required=True)
    parser.add_argument("--runtime-dll", type=Path, required=True)
    parser.add_argument("--bridge-exe", type=Path, required=True)
    parser.add_argument("--old-abi-dir", type=Path, required=True)
    parser.add_argument("--bad-status-dir", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    args = parser.parse_args()
    output = args.output_root.resolve()
    output.mkdir(parents=True, exist_ok=True)
    repo = Path(__file__).resolve().parents[1]
    source = (repo / "src/GkCsharpImport.hpp").read_text("utf-8")
    assert re.search(r"abi\(\)\s*<\s*3", source), "Runtime bridge must require body ABI 3"
    module = ctypes.CDLL(str(args.native_dll.resolve()))
    versions = {}
    for name in ("GkVmdImporter_AbiVersion", "GkCameraImporter_AbiVersion"):
        fn = getattr(module, name)
        fn.restype = ctypes.c_int32
        fn.argtypes = []
        versions[name] = fn()
    assert list(versions.values()) == [3, 1], versions
    cases = [
        ("native-bridge-e2e", [str(args.bridge_exe.resolve()), str(args.native_dll.resolve().parent),
                               str(args.old_abi_dir.resolve()), str(output / "bridge-work")]),
        ("native-bridge-error-gate", [str(args.bridge_exe.resolve()), "--bad-status",
                                     str(args.bad_status_dir.resolve())]),
    ]
    for name, command in cases:
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (output / (name + ".log")).write_bytes(result.stdout)
        assert result.returncode == 0, name + " failed; inspect its log"
    clips = []
    for path in (output / "bridge-work").rglob("motion.gkclip"):
        data = path.read_bytes()
        assert data[:6] == b"GKCLIP" and len(data) > 32
        version, fps, frames, duration, bones, root_scale = struct.unpack_from("<IfIfIf", data, 8)
        assert 1 <= version <= 5 and 0 < frames <= 1000000 and 0 < bones <= 256
        assert math.isfinite(fps) and fps > 0 and math.isfinite(duration) and duration >= 0
        metadata = json.loads((path.parent / "info.json").read_text("utf-8-sig"))
        assert "track_timing" in metadata
        clips.append({"path": str(path), "version": version, "fps": fps, "frames": frames,
                      "bones": bones, "track_timing": metadata["track_timing"], "sha256": digest(path)})
    assert clips, "No imported synthetic clip was produced"
    head = subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"], text=True).strip()
    result = {"status": "PASS", "source_commit": head, "runtime_required_body_abi": 3,
              "exports": versions, "runtime_sha256": digest(args.runtime_dll),
              "nativeaot_sha256": digest(args.native_dll), "synthetic_clips": clips,
              "game_started": False, "user_assets_accessed": False}
    (output / "synthetic-import-e2e.json").write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps({"status": "PASS", "body_abi": 3, "camera_abi": 1,
                      "synthetic_clips": len(clips), "source_commit": head}))

if __name__ == "__main__":
    main()
