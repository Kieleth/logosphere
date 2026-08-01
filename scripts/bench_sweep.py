#!/usr/bin/env python3
"""
Parametric performance sweep.

Design + rationale: the performance research notes
Methodology:        docs/PERFORMANCE_RESEARCH.md

Runs a matrix of configurations and records one JSONL metrics file per run
plus a manifest with full provenance. Two modes:

  fast   single trial per config. Minutes. Results are INDICATIVE.
  study  repeated trials in INTERLEAVED order with cooldowns. Slower, and
         the only mode whose numbers should settle an argument.

Why interleaved: sequential blocks load thermal drift onto whichever config
runs last. The same build has measured 16.0 ms and 26.0 ms in one session on
this machine, which is larger than most effects we chase. Rotating config
order per repeat spreads drift across the matrix instead of concentrating it.

Usage:
  scripts/bench_sweep.py --mode fast  --scene mlp
  scripts/bench_sweep.py --mode study --scene mlp --repeats 3
  scripts/bench_sweep.py --mode study --scene eden --repeats 3
"""

import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
EDEN = REPO / "build-release" / "eden" / "eden"
TESTS = REPO / "build-release" / "logosphere-tests"
METALLIB = REPO / "build-release" / "default.metallib"


def sh(cmd, **kw):
    return subprocess.run(cmd, shell=True, capture_output=True, text=True,
                          cwd=REPO, **kw).stdout.strip()


def provenance(mode, args):
    dirty = bool(sh("git status --porcelain"))

    # Hash the BINARIES, not just the shaders. A sweep once produced zero GPU
    # data because logosphere-tests was built before the instrumentation was
    # wired — a stale-binary trap that a metallib hash alone cannot catch.
    def md5(p):
        return hashlib.md5(p.read_bytes()).hexdigest() if p.exists() else ""

    mlmd5 = md5(METALLIB)
    return {
        "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "git_sha": sh("git rev-parse --short HEAD"),
        "git_branch": sh("git rev-parse --abbrev-ref HEAD"),
        "git_dirty": dirty,
        "metallib_md5": mlmd5,
        "eden_md5": md5(EDEN),
        "tests_md5": md5(TESTS),
        "eden_mtime": EDEN.stat().st_mtime if EDEN.exists() else 0,
        "tests_mtime": TESTS.stat().st_mtime if TESTS.exists() else 0,
        "build": "release",
        "host": platform.node(),
        "machine": platform.machine(),
        "os": platform.platform(),
        "mode": mode,
        "args": vars(args),
        # Stated plainly so a reader never has to guess how much to trust it.
        "trust": ("INDICATIVE — single trial per config, thermal drift not "
                  "controlled" if mode == "fast" else
                  "STUDY — interleaved repeats with cooldowns"),
    }


def build_matrix(scene, args):
    """Config list. Each entry is a dict the runner knows how to execute."""
    cfgs = []
    if scene == "mlp":
        for p in args.particles:
            for l in args.lights:
                cfgs.append({"scene": "mlp", "particles": p, "lights": l,
                             "width": args.width, "height": args.height})
    else:
        for (w, h) in args.resolutions:
            cfgs.append({"scene": "eden", "particles": None, "lights": None,
                         "width": w, "height": h})
    return cfgs


def run_one(cfg, out_path, frames):
    env = dict(os.environ)
    env["LOGOSPHERE_METRICS"] = str(out_path)
    env["LOGOSPHERE_METRICS_EVERY"] = "1"

    if cfg["scene"] == "mlp":
        env["PARTICLES"] = str(cfg["particles"])
        env["LIGHTS"] = str(cfg["lights"])
        # One light count per process. The vehicle otherwise walks phases
        # 1..N inside a single process, which correlates light count with
        # elapsed time — thermal drift then masquerades as light cost, and
        # interleaving cannot fix it from out here.
        env["MLP_SINGLE_PHASE"] = "1"
        env["MLP_DUMP_DIR"] = "/tmp/bench_sweep_dumps"
        os.makedirs("/tmp/bench_sweep_dumps", exist_ok=True)
        cmd = [str(TESTS), "--test", "test_multi_light_progressive", "--no-head"]
    else:
        cmd = [str(EDEN), "--bench", str(frames), "--headless",
               "--size", str(cfg["width"]), str(cfg["height"])]

    t0 = time.time()
    proc = subprocess.run(cmd, env=env, capture_output=True, text=True, cwd=REPO)
    return {"wall_s": round(time.time() - t0, 2), "returncode": proc.returncode}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", choices=["fast", "study"], default="fast")
    ap.add_argument("--scene", choices=["mlp", "eden"], default="mlp")
    ap.add_argument("--repeats", type=int, default=3, help="study mode only")
    ap.add_argument("--cooldown", type=float, default=20.0,
                    help="seconds between configs in study mode")
    ap.add_argument("--frames", type=int, default=400, help="eden frames")
    ap.add_argument("--particles", type=int, nargs="+",
                    default=[144, 576, 1296, 2304])
    ap.add_argument("--lights", type=int, nargs="+", default=[1, 2, 4, 6])
    ap.add_argument("--width", type=int, default=1600)
    ap.add_argument("--height", type=int, default=1051)
    ap.add_argument("--resolutions", type=lambda s: tuple(map(int, s.split("x"))),
                    nargs="+", default=[(1600, 1051), (3200, 2102)])
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    if not TESTS.exists() or not EDEN.exists():
        print("ERROR: build-release binaries missing. Build first:", file=sys.stderr)
        print("  cmake --build build-release --target eden logosphere-tests -j 8",
              file=sys.stderr)
        return 2

    prov = provenance(args.mode, args)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    run_dir = Path(args.out) if args.out else (
        REPO / "benchmarks" / "runs" / f"{stamp}-{prov['git_sha']}")
    run_dir.mkdir(parents=True, exist_ok=True)

    cfgs = build_matrix(args.scene, args)
    repeats = args.repeats if args.mode == "study" else 1

    print(f"[SWEEP] mode={args.mode} scene={args.scene} "
          f"configs={len(cfgs)} repeats={repeats}")
    print(f"[SWEEP] out={run_dir}")
    if prov["git_dirty"]:
        print("[SWEEP] WARNING: working tree is dirty — provenance records this")

    # Discard a warmup run before anything measured. The first run of a cold
    # sequence pays for shader compiles, first-touch allocations and a GPU
    # still ramping its clocks, and it is not small: 35.3 ms against 26.7 for
    # runs 2 and 3 of the same config (kit method note, 2026-07-31). Without
    # this, whichever config goes first carries that cost.
    #
    # NOT YET VERIFIED to reduce spread. The run intended to confirm it landed
    # on a contended machine (another engine build plus a browser at >100% CPU,
    # load average 3.7) and came back noisier than the run without it, which
    # measures the contention and not the warmup. Re-confirm on a quiet
    # machine before trusting any spread figure quoted for this harness.
    warm_path = run_dir / "warmup_discarded.jsonl"
    print("  [warmup] discarded run ... ", end="", flush=True)
    wres = run_one(cfgs[0], warm_path, args.frames)
    print(f"{wres['wall_s']}s (not recorded)")
    warm_path.unlink(missing_ok=True)
    if args.mode == "study":
        time.sleep(args.cooldown)

    index = []
    for rep in range(repeats):
        # Rotate config order every repeat so thermal drift does not land on
        # the same config each time.
        order = cfgs[rep % len(cfgs):] + cfgs[:rep % len(cfgs)]
        for ci, cfg in enumerate(order):
            tag = (f"{cfg['scene']}_p{cfg['particles']}_l{cfg['lights']}"
                   f"_{cfg['width']}x{cfg['height']}_rep{rep}")
            out_path = run_dir / f"{tag}.jsonl"
            print(f"  [{rep+1}/{repeats}] {tag} ... ", end="", flush=True)
            res = run_one(cfg, out_path, args.frames)
            nrec = sum(1 for _ in open(out_path)) if out_path.exists() else 0
            print(f"{res['wall_s']}s, {nrec} records"
                  f"{' RC=' + str(res['returncode']) if res['returncode'] else ''}")
            index.append({**cfg, "repeat": rep, "file": out_path.name,
                          "records": nrec, **res})

            if args.mode == "study" and not (rep == repeats - 1 and ci == len(order) - 1):
                time.sleep(args.cooldown)

    (run_dir / "manifest.json").write_text(
        json.dumps({"provenance": prov, "runs": index}, indent=2))
    print(f"[SWEEP] done -> {run_dir}/manifest.json")
    print(f"[SWEEP] report: scripts/bench_report.py {run_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
