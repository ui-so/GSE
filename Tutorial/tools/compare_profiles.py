"""Compare two same-scenario profile summaries; report rather than hide regressions."""
import argparse
import json
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("before", type=Path)
p.add_argument("after", type=Path)
a = p.parse_args()
before, after = (json.loads(path.read_text(encoding="utf-8")) for path in (a.before, a.after))
for key in ("scenario", "seed", "width", "height", "renderer"):
    if before["session"].get(key) != after["session"].get(key):
        p.error(f"incomparable session field: {key}")
result = {"schema_version": 1, "type": "comparison", "changes": {},
          "flags_before": before["flags"], "flags_after": after["flags"],
          "note": "Also verify launch arguments, camera, profiling mode, and workload in raw logs."}
for label, first, second in (
    ("frame_ms", before.get("frame_ms"), after.get("frame_ms")),
    ("frame_interval_ms", before.get("frame_interval_ms"), after.get("frame_interval_ms")),
    ("draw_calls", before["counters"].get("draw_calls"), after["counters"].get("draw_calls")),
    ("render_gpu_ms", before["gpu_ms"].get("render_total"), after["gpu_ms"].get("render_total"))):
    if first and second:
        result["changes"][label] = {k: {"before": first[k], "after": second[k],
            "change_percent": (second[k] / first[k] - 1) * 100 if first[k] else None}
            for k in ("mean", "p95", "p99")}
print(json.dumps(result, indent=2, allow_nan=False))
