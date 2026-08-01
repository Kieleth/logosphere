#!/usr/bin/env python3
"""
Analysis for bench_sweep runs.

Design: the performance research notes
Methodology: docs/PERFORMANCE_RESEARCH.md

Reports, per config:
  - frame time (median, IQR, and a drift figure)
  - CPU phase breakdown
  - GPU stage breakdown
  - GPU BUSY = union of every command buffer's execution interval. This is
    real GPU work per frame. Do NOT sum per-frame busy across frames: windows
    overlap because the GPU runs a frame behind, and the sum exceeds wall
    clock (it read 122% before this was fixed). See telemetry.h.
  - GPU OCCUPANCY = busy / frame. Near 100% means GPU-bound; near the CPU
    figure means balanced, and then a one-sided win is capped at the gap.
  - STAGE OVERLAP = sum(gpu stages) / busy. Above 1.0 the named stages overlap
    each other, so their sum overstates GPU work and a stage win is worth less
    than it reads. This is how two "wins" got misread in the 2026-07 campaign.
  - THERMAL SUSPECT flag when frame time drifts monotonically within a run.

Usage:
  scripts/bench_report.py benchmarks/runs/<dir>
  scripts/bench_report.py <dir> --baseline benchmarks/baseline.json
  scripts/bench_report.py <dir> --save-baseline benchmarks/baseline.json
"""

import argparse
import json
import statistics as st
import sys
from pathlib import Path


def load_run(path):
    recs = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line:
                try:
                    recs.append(json.loads(line))
                except json.JSONDecodeError:
                    pass  # partial last line if a run was killed
    return recs


def warm(recs, skip_frac=0.25):
    """Drop the warmup head: shader compiles and first-touch allocations."""
    n = len(recs)
    return recs[int(n * skip_frac):] if n >= 8 else recs


def drift(values):
    """Median of last third minus median of first third. Positive = slowing."""
    if len(values) < 9:
        return 0.0
    k = len(values) // 3
    return st.median(values[-k:]) - st.median(values[:k])


def split_by_lights(recs):
    """One MLP run cycles through 1..N lights in phases. Group by the light
    count each frame actually recorded rather than by the run's env var,
    which only names the maximum."""
    groups = {}
    for r in recs:
        groups.setdefault(r.get("lights", 0), []).append(r)
    return groups


def gpu_busy_per_frame(recs):
    """Real GPU work per frame: the UNION of command-buffer intervals, divided
    by the number of frames. Summing per-frame busy_ms instead double counts,
    because consecutive frames' windows overlap (the GPU runs about a frame
    behind the CPU). That sum reported 122% of wall clock on Eden at retina."""
    iv = []
    for r in recs:
        w = r.get("gpu_window") or {}
        s, e = w.get("start_s"), w.get("end_s")
        if s is not None and e is not None and e > s:
            iv.append((s, e))
    if not iv:
        return 0.0
    iv.sort()
    merged = [list(iv[0])]
    for s, e in iv[1:]:
        if s <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], e)
        else:
            merged.append([s, e])
    return sum(e - s for s, e in merged) * 1000.0 / len(recs)


def summarize(recs):
    recs = warm(recs)
    if not recs:
        return None
    frame = [r["phase_ms"].get("frame", 0.0) for r in recs]
    frame = [f for f in frame if f > 0]
    if not frame:
        return None

    phases = {}
    for key in recs[0].get("phase_ms", {}):
        vals = [r["phase_ms"].get(key, 0.0) for r in recs]
        m = st.median(vals)
        if m > 0.005:
            phases[key] = m

    gpu = {}
    for r in recs:
        for k, v in r.get("gpu_ms", {}).items():
            gpu.setdefault(k, []).append(v["ms"])
    gpu_med = {k: st.median(v) for k, v in gpu.items() if st.median(v) > 0.0}

    counters = {}
    for key in recs[0].get("counters", {}):
        vals = [r["counters"].get(key, 0) for r in recs]
        m = st.median(vals)
        if m:
            counters[key] = m

    fmed = st.median(frame)
    q = sorted(frame)
    iqr = q[int(len(q) * 0.75)] - q[int(len(q) * 0.25)]
    gpu_sum = sum(gpu_med.values())
    gpu_busy = gpu_busy_per_frame(recs)

    return {
        "frames": len(frame),
        "frame_median_ms": fmed,
        "frame_iqr_ms": iqr,
        "frame_drift_ms": drift(frame),
        "phases": phases,
        "gpu": gpu_med,
        "gpu_sum_ms": gpu_sum,
        # True GPU work per frame, and the share of the frame it occupies.
        "gpu_busy_ms": gpu_busy,
        "gpu_occupancy": (gpu_busy / fmed) if fmed else 0.0,
        # How much the NAMED stages overlap EACH OTHER. Above 1.0 their sum
        # overstates GPU work, so a stage win is worth less than it reads.
        "stage_overlap": (gpu_sum / gpu_busy) if gpu_busy else 0.0,
        "counters": counters,
        "particles": recs[-1].get("particles", 0),
        "lights": recs[-1].get("lights", 0),
    }


def config_key(run):
    return (run["scene"], run["particles"], run["lights"],
            run["width"], run["height"])


def ramp_report(path, bins=12):
    """Read a RAMP run (steady spawn, climbing particle count) as a
    continuous cost curve. One run, many load points — the alternative to
    a matrix of discrete configs."""
    recs = [r for r in load_run(path) if r["phase_ms"].get("frame", 0) > 0]
    if not recs:
        print("no usable records")
        return 1
    recs = warm(recs, 0.05)
    lo = min(r["particles"] for r in recs)
    hi = max(r["particles"] for r in recs)
    print(f"RAMP {Path(path).name}")
    print(f"  particles {lo} -> {hi}   frames {len(recs)}   "
          f"lights {recs[-1].get('lights', 0)}")
    if hi <= lo:
        print("  (particle count never grew — was MLP_SPAWN_RATE set?)")
        return 1

    width = (hi - lo) / bins
    print(f"\n  {'particles':>12}  {'frame ms':>9}  {'gpu sum':>8}  "
          f"{'cpu render':>10}  {'physics':>8}  n")
    prev = None
    for b in range(bins):
        a, z = lo + b * width, lo + (b + 1) * width
        sub = [r for r in recs if a <= r["particles"] < z or
               (b == bins - 1 and r["particles"] == hi)]
        if len(sub) < 3:
            continue
        f = st.median([r["phase_ms"]["frame"] for r in sub])
        g = st.median([sum(v["ms"] for v in r["gpu_ms"].values()) or 0
                       for r in sub]) if sub[0].get("gpu_ms") else 0.0
        cr = st.median([r["phase_ms"].get("render", 0) for r in sub])
        ph = st.median([r["phase_ms"].get("physics", 0) for r in sub])
        mid = int((a + z) / 2)
        mark = ""
        if prev is not None and prev > 0:
            growth = (f - prev) / prev
            if growth > 0.15:
                mark = "  <- knee"
        print(f"  {mid:12d}  {f:9.2f}  {g:8.2f}  {cr:10.2f}  {ph:8.2f}  {len(sub)}")
        prev = f
    print("\n  Cost per 1000 particles (median across the ramp): "
          f"{((st.median([r['phase_ms']['frame'] for r in recs if r['particles'] > (lo+hi)/2]) - st.median([r['phase_ms']['frame'] for r in recs if r['particles'] <= (lo+hi)/2])) / max(1,(hi-lo)) * 1000):.3f} ms")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("run_dir")
    ap.add_argument("--baseline")
    ap.add_argument("--save-baseline")
    ap.add_argument("--tolerance", type=float, default=0.05,
                    help="regression threshold, fraction of frame time")
    ap.add_argument("--ramp", action="store_true",
                    help="treat the path as a single RAMP jsonl file")
    args = ap.parse_args()

    if args.ramp:
        return ramp_report(args.run_dir)

    run_dir = Path(args.run_dir)
    manifest = json.loads((run_dir / "manifest.json").read_text())
    prov = manifest["provenance"]

    print("=" * 78)
    print(f"RUN {run_dir.name}")
    print(f"  {prov['git_sha']} ({prov['git_branch']})"
          f"{'  DIRTY' if prov['git_dirty'] else ''}"
          f"  metallib={prov['metallib_md5'][:8]}")
    print(f"  {prov['timestamp_utc']}  {prov['machine']}")
    print(f"  TRUST: {prov['trust']}")
    print("=" * 78)

    # Group repeats of the same config.
    grouped = {}
    for run in manifest["runs"]:
        recs = load_run(run_dir / run["file"])
        if not recs:
            continue
        # Group by the light count the frames actually recorded. With
        # MLP_SINGLE_PHASE each run holds exactly one, so this collapses to
        # the intended config; older multi-phase runs still split correctly.
        for lights, sub in split_by_lights(recs).items():
            s = summarize(sub)
            if s and lights > 0:
                key = (run["scene"], run["particles"], lights,
                       run["width"], run["height"])
                grouped.setdefault(key, []).append(s)

    results = {}
    for key, summaries in sorted(grouped.items(), key=lambda kv: str(kv[0])):
        scene, p, l, w, h = key
        label = f"{scene} p={p} l={l} {w}x{h}"
        med = st.median([s["frame_median_ms"] for s in summaries])
        spread = (max(s["frame_median_ms"] for s in summaries) -
                  min(s["frame_median_ms"] for s in summaries))
        best = min(summaries, key=lambda s: s["frame_median_ms"])

        print(f"\n{label}")
        print(f"  frame  median {med:6.2f} ms   ({1000/med:5.1f} FPS)"
              f"   trials={len(summaries)}  spread={spread:.2f} ms")

        # A spread between repeats larger than the effects we chase means the
        # number cannot settle anything.
        if len(summaries) > 1 and spread > 0.5:
            print(f"         ^ SPREAD EXCEEDS 0.5 ms — treat differences "
                  f"below {spread:.2f} ms as noise")
        d = st.median([s["frame_drift_ms"] for s in summaries])
        if abs(d) > 0.5:
            print(f"         ^ THERMAL SUSPECT: drift {d:+.2f} ms within run")

        if best["gpu"]:
            print(f"  gpu stages (median, best trial):")
            for k, v in sorted(best["gpu"].items(), key=lambda kv: -kv[1]):
                print(f"      {k:24s} {v:6.2f} ms")
            print(f"      {'SUM':24s} {best['gpu_sum_ms']:6.2f} ms")
            print(f"      {'BUSY (union)':24s} {best['gpu_busy_ms']:6.2f} ms"
                  f"   occupancy {best['gpu_occupancy']:.0%}"
                  f"   stage overlap {best['stage_overlap']:.2f}x")
            if best["stage_overlap"] > 1.05:
                print(f"      ^ the named stages overlap each other, so their "
                      f"sum overstates GPU work by "
                      f"{best['gpu_sum_ms'] - best['gpu_busy_ms']:.2f} ms.")
            cpu_ms = med - 0.0
            headroom = cpu_ms - best["gpu_busy_ms"]
            if best["gpu_occupancy"] > 0.85:
                print(f"      ^ GPU is {best['gpu_occupancy']:.0%} occupied and the frame "
                      f"leads GPU work by only {headroom:.2f} ms. Near balance a "
                      f"one-sided win is CAPPED at that gap, whichever side it is on.")

        cpu = {k: v for k, v in best["phases"].items() if k != "frame"}
        if cpu:
            top = sorted(cpu.items(), key=lambda kv: -kv[1])[:6]
            print("  cpu phases: " +
                  "  ".join(f"{k}={v:.2f}" for k, v in top))

        if best["counters"]:
            print("  counters:   " +
                  "  ".join(f"{k}={int(v)}" for k, v in
                            sorted(best["counters"].items())))

        results[str(key)] = {"frame_median_ms": med, "label": label}

    # Regression comparison
    if args.baseline and Path(args.baseline).exists():
        base = json.loads(Path(args.baseline).read_text())
        print("\n" + "=" * 78)
        print(f"VS BASELINE {args.baseline}")
        regressed = False
        for key, cur in results.items():
            b = base.get("results", {}).get(key)
            if not b:
                print(f"  {cur['label']:32s} (no baseline)")
                continue
            delta = cur["frame_median_ms"] - b["frame_median_ms"]
            pct = delta / b["frame_median_ms"] if b["frame_median_ms"] else 0
            flag = ""
            if pct > args.tolerance:
                flag = "  REGRESSION"
                regressed = True
            elif pct < -args.tolerance:
                flag = "  improvement"
            print(f"  {cur['label']:32s} {b['frame_median_ms']:6.2f} -> "
                  f"{cur['frame_median_ms']:6.2f} ms ({pct:+.1%}){flag}")
        if regressed:
            print("\n  RESULT: regression beyond tolerance")
            return 1

    if args.save_baseline:
        Path(args.save_baseline).parent.mkdir(parents=True, exist_ok=True)
        Path(args.save_baseline).write_text(json.dumps(
            {"provenance": prov, "results": results}, indent=2))
        print(f"\n[BASELINE] saved -> {args.save_baseline}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
