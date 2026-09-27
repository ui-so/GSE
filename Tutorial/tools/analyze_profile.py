"""Summarize one GSE JSONL session without third-party packages."""
import argparse
import collections
import json
import math
from pathlib import Path
import statistics


def distribution(values):
    values = sorted(values)
    if not values:
        return None
    def percentile(p):
        x = (len(values) - 1) * p
        low, high = math.floor(x), math.ceil(x)
        return values[low] + (values[high] - values[low]) * (x - low)
    return {"samples": len(values), "mean": statistics.mean(values),
            "p50": percentile(.5), "p95": percentile(.95), "p99": percentile(.99), "max": values[-1]}


def analyze(directory):
    records = []
    for path in sorted(directory.glob("profile-*.jsonl")):
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            try:
                record = json.loads(line)
                if record.get("schema_version") != 1:
                    raise ValueError("unsupported schema_version")
                records.append(record)
            except (ValueError, TypeError) as error:
                raise ValueError(f"{path}:{number}: {error}") from error
    if not records:
        raise ValueError("No profile records in session directory")
    sessions = [r for r in records if r["type"] == "session"]
    session_file = directory / "session.json"
    session = sessions[0] if sessions else json.loads(session_file.read_text(encoding="utf-8")) if session_file.exists() else {}
    all_frames = {r["frame_id"]: r for r in records if r["type"] == "frame"}
    frames = {i:r for i,r in all_frames.items() if not r["warmup"]}
    cpu, counters, gpu = (collections.defaultdict(list) for _ in range(3))
    for frame in frames.values():
        for name, value in frame["cpu_ms"].items():
            cpu[name].append(value)
        for name, value in frame["counters"].items():
            counters[name].append(value)
    gpu_by_frame = collections.defaultdict(float)
    for record in records:
        if record["type"] == "gpu_span" and record["frame_id"] in frames:
            gpu_by_frame[(record["frame_id"], record["name"])] += record["gpu_ms"]
    for (_, name), value in gpu_by_frame.items():
        gpu[name].append(value)
    events = [r for r in records if r["type"] == "event"]
    errors = [r for r in events if r["name"] in ("opengl_error", "renderer_initialization_failed")]
    end = next((r for r in reversed(records) if r["type"] == "session_end"), {})
    dropped = max([end.get("dropped_records", 0)] + [r["counters"].get("log_records_dropped_total", 0) for r in all_frames.values()])
    intervals = distribution([f["frame_interval_ms"] for f in frames.values() if f.get("frame_interval_ms", 0) > 0])
    totals = distribution([f["frame_ms"] for f in frames.values()])
    flags = []
    if not frames:
        flags.append("insufficient_steady_state_frames")
    if dropped:
        flags.append("telemetry_records_dropped")
    if errors:
        flags.append("render_validation_errors")
    if not session.get("gpu_timestamps_available"):
        flags.append("gpu_timing_unavailable")
    if totals and totals["p95"] > session.get("frame_budget_ms", 16.6667):
        flags.append("frame_p95_over_budget")
    if frames and len(gpu.get("render_total", [])) < len(frames):
        flags.append("gpu_results_incomplete")
    return {"schema_version": 1, "session": session, "measured_frames": len(frames),
            "frame_ms": totals, "frame_interval_ms": intervals, "cpu_ms": {k:distribution(v) for k,v in cpu.items()},
            "gpu_ms": {k:distribution(v) for k,v in gpu.items()},
            "counters": {k:distribution(v) for k,v in counters.items()},
            "flags": flags, "validation_errors": errors, "dropped_records": dropped,
            "pending_gpu_spans": end.get("pending_gpu_spans"),
            "notes": ["CPU scopes and GPU scopes are inclusive and may overlap; do not sum them.",
                      "GPU spans join on original frame_id and can arrive several frames later.",
                      "frame_ms includes present_wait but excludes end-of-frame serialization; frame_interval_ms includes the gap between frame starts.",
                      "benchmark_gpu_wait exists only in benchmark mode; captured sub-renders share one frame_id.",
                      "Absent optional metrics mean unavailable or not sampled; they do not mean zero."]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("session", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--assert-clean", action="store_true")
    parser.add_argument("--max-frame-p95-ms", type=float)
    parser.add_argument("--max-draw-calls", type=float)
    args = parser.parse_args()
    report = analyze(args.session)
    payload = json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False)
    if args.output:
        args.output.write_text(payload + "\n", encoding="utf-8")
    else:
        print(payload)
    if args.max_frame_p95_ms is not None and (not report["frame_ms"] or report["frame_ms"]["p95"] > args.max_frame_p95_ms):
        raise SystemExit(3)
    draw = report["counters"].get("draw_calls")
    if args.max_draw_calls is not None and (not draw or draw["max"] > args.max_draw_calls):
        raise SystemExit(3)
    if args.assert_clean and (report["validation_errors"] or report["dropped_records"]):
        raise SystemExit(2)


if __name__ == "__main__":
    main()
