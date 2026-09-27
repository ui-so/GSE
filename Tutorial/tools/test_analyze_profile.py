import json
import tempfile
import unittest
from pathlib import Path
from analyze_profile import analyze

class ProfileTests(unittest.TestCase):
    def test_warmup_delayed_gpu_and_zero_counters(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            records = [dict(type="session", gpu_timestamps_available=True, frame_budget_ms=16.6667)]
            for i in (1,21,22):
                records.append(dict(type="frame",frame_id=i,warmup=i==1,frame_ms=10,
                    frame_interval_ms=11,cpu_ms={"render":6,"child":4},counters={"draw_calls":0 if i==22 else 10}))
            for i in (22,21):
                records.append(dict(type="gpu_span",frame_id=i,name="render_total",gpu_ms=3))
            records.append(dict(type="session_end",dropped_records=0,pending_gpu_spans=0))
            for r in records: r["schema_version"]=1
            (path/"profile-0.jsonl").write_text("\n".join(json.dumps(r) for r in records))
            r=analyze(path)
            self.assertEqual(r["measured_frames"],2)
            self.assertEqual(r["counters"]["draw_calls"]["mean"],5)
            self.assertEqual(r["gpu_ms"]["render_total"]["samples"],2)
            self.assertEqual(r["cpu_ms"]["render"]["mean"],6)
            self.assertEqual(r["flags"],[])
    def test_corrupt_log_is_reported(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)
            (path/"profile-0.jsonl").write_text("not json")
            with self.assertRaises(ValueError): analyze(path)

if __name__=="__main__": unittest.main()
