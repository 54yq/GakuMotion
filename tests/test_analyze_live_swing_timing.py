import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("timing", Path(__file__).parents[1] / "tools" / "analyze_live_swing_timing.py")
timing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(timing)


class TimingTests(unittest.TestCase):
    def test_overlap_and_incomplete(self):
        report = {"actor_swing_timing": {"qpc_frequency": 1000, "samples": [
            {"start_qpc": 110, "end_qpc": 120}, {"start_qpc": 190, "end_qpc": 220},
            {"start_qpc": 500, "end_qpc": 0}]}}
        log = "[LiveEvalTiming] pid=7 count=1 epoch=2 qpc_frequency=1000\n[LiveEvalTiming] index=0 start_qpc=100 end_qpc=200 thread=3"
        result = timing.compare(report, log, 7)
        self.assertEqual((result["swing_complete"], result["swing_overlapping_evaluation"], result["overlap_ending_after_evaluation"]), (2, 2, 1))

    def test_rejects_wrong_process_or_clock(self):
        report = {"actor_swing_timing": {"qpc_frequency": 1000, "samples": []}}
        log = "[LiveEvalTiming] pid=7 count=0 epoch=1 qpc_frequency=999"
        with self.assertRaises(ValueError):
            timing.compare(report, log, 7)
        with self.assertRaises(ValueError):
            timing.compare(report, log, 8)


if __name__ == "__main__":
    unittest.main()
