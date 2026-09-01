#!/usr/bin/env python3
"""Harvest the red cases across the physics battery, quickly.

Owner order 2026-09-01: "have a way to gather/harvest red cases
quickly." Runs the campaign probes (and the core battery) headless in
a chosen lever world, collects every [FAIL] with its case header and
the [measure] lines around it, and prints ONE compact digest. The
file is the archive; the digest is the delivery.

Usage:
  python3 scripts/harvest_reds.py                 # single-law world (default)
  python3 scripts/harvest_reds.py --world trio    # trio only
  python3 scripts/harvest_reds.py --world default # no levers
  python3 scripts/harvest_reds.py --full          # + strike/torsion/stack/mixed

Exit code = number of red asserts found.
"""
import argparse
import os
import re
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BIN = REPO / "build"

LIMITS = ["test_limits_ice", "test_limits_anvil", "test_limits_size",
          "test_limits_footprint", "test_limits_slipjoint",
          "test_limits_floor"]
BATTERY = ["test_torsion_transmission", "test_stack_stands",
           "test_mixed_mass_stands", "test_square_strike",
           "test_refused_momentum_ledger", "test_cube_drop_ladder",
           "test_ramp_race"]

# INV-36 (the flip, 2026-09-01): the single law is the default; the
# other worlds are kill-switch A/B configurations.
WORLDS = {
    "default": {},
    "trio": {"SINGLE_LAW": "0"},
    "legacy": {"FRICTION_TWIST": "0", "WARM_LEARN": "0",
               "MANIFOLD_SPAN": "0", "SINGLE_LAW": "0"},
}


def rebuild(tests):
    """THE STALE-BUILD GUARDRAIL (2026-09-01, measured burn: a lever-world
    'jury' ran pre-lever executables for hours - static libs relink
    NOTHING until the target is rebuilt). The harvester refuses to
    measure a binary it has not just built."""
    cmd = ["cmake", "--build", str(BIN), "--target"] + tests
    proc = subprocess.run(cmd, cwd=REPO, capture_output=True, text=True)
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout[-2000:] + proc.stderr[-2000:])
        sys.exit(f"REBUILD FAILED - refusing to measure stale binaries")


def run_one(name, env_extra):
    exe = BIN / name
    if not exe.exists():
        return None, f"(missing binary: {exe})"
    env = {k: v for k, v in os.environ.items() if k != "INTERACTIVE"}
    env.update(env_extra)
    t0 = time.time()
    try:
        proc = subprocess.run([str(exe)], cwd=REPO, env=env,
                              capture_output=True, text=True, timeout=600)
    except subprocess.TimeoutExpired:
        return None, "(HUNG at 600 s)"
    return proc.stdout, f"{time.time() - t0:.0f}s rc={proc.returncode}"


def harvest(text):
    """Yield (case, context_measures, fail_line) triples."""
    case = "(no case header)"
    measures = []
    for line in text.splitlines():
        m = re.match(r"^-- (.+) --$", line.strip())
        if m:
            case = m.group(1)
            measures = []
            continue
        if "[measure]" in line:
            measures.append(line.strip())
            continue
        if "[FAIL]" in line:
            yield case, list(measures), line.strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--world", choices=sorted(WORLDS), default="default")
    ap.add_argument("--full", action="store_true",
                    help="also run the strike/torsion/stack battery")
    args = ap.parse_args()

    tests = LIMITS + (BATTERY if args.full else [])
    env_extra = WORLDS[args.world]
    print("rebuilding targets (stale-build guardrail)...", flush=True)
    rebuild(tests)
    total = 0
    print(f"=== RED HARVEST — world: {args.world} ===")
    for name in tests:
        out, status = run_one(name, env_extra)
        if out is None:
            print(f"\n## {name}  {status}")
            total += 1
            continue
        reds = list(harvest(out))
        if not reds:
            print(f"   {name}: green  ({status})")
            continue
        print(f"\n## {name}  ({len(reds)} red, {status})")
        last_case = None
        for case, measures, fail in reds:
            if case != last_case:
                print(f"  -- {case}")
                for m in measures:
                    print(f"     {m}")
                last_case = case
            print(f"     {fail}")
        total += len(reds)
    print(f"\nHARVEST_VERDICT: {total} red asserts "
          f"({args.world} world{', full battery' if args.full else ''})")
    return min(total, 200)


if __name__ == "__main__":
    sys.exit(main())
