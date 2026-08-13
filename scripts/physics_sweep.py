#!/usr/bin/env python3
# =============================================================================
# PHYSICS SWEEP — one click: run everything headless, report what broke,
# and say it in INVARIANTS, not just test names.
# =============================================================================
# The whack-a-mole detector. Joins three files:
#   tests/invariants/INVARIANTS.jsonl  — what must always be true (INV-x)
#   tests/invariants/TEST_AUDIT.jsonl  — one line per test: status, baseline
#                                        verdict (expect), INV links
#   the live run                       — every test, serial, headless
#
# A MOLE is precisely: a live verdict that contradicts the audited baseline.
#   expect pass + FAIL + no known_open  -> MOLE (new red)
#   expect fail + PASS                  -> MOLE (gone green: fix landed or
#                                          a red-by-design ladder was healed;
#                                          promote it in the audit + ledger)
#   expect pass + FAIL + known_open     -> KNOWN OPEN (loud, but not new)
# Tests found on disk but absent from the audit are UNAUDITED — also a mole:
# a test without invariant links keeps physics dishonest.
#
# Markers, not exit codes, are the truth (repo learning: wrappers misread
# exit codes): grep for "SWEEP_VERDICT:".
#
# Usage:
#   ./scripts/physics_sweep.py                # full sweep (~35 min)
#   ./scripts/physics_sweep.py --filter walk  # subset by substring
#   ./scripts/physics_sweep.py --deadline 120 # per-test seconds (default 300)
# NEVER opens a window: registry tests run --no-head, INTERACTIVE is never
# set, and the environment strips it defensively.
# =============================================================================

import argparse, json, os, re, signal, subprocess, sys, time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
INV_FILE = REPO / "tests/invariants/INVARIANTS.jsonl"
AUDIT_FILE = REPO / "tests/invariants/TEST_AUDIT.jsonl"
RUNNER = REPO / "build-release/logosphere-tests"
BIN_DIR = REPO / "build-release"
RESULTS = REPO / "build-release/sweep_results.json"


def load_jsonl(path):
    rows = []
    if path.exists():
        for line in path.read_text().splitlines():
            line = line.strip()
            if line:
                rows.append(json.loads(line))
    return rows


def discover_registry():
    # Skip commented-out registrations: the runner's registry block carries
    # 17 dead names as full-line comments (a TODO conversion block plus
    # "REMOVED:" tombstones). Matching them made the first sweep report 17
    # phantom UNAUDITED tests whose only behavior is the harness's
    # "Unknown test" branch (rc=1 in 0.1 s, no engine, no test body).
    src = (REPO / "src/unified_test_runner.cpp").read_text()
    live = "\n".join(l for l in src.splitlines() if not l.lstrip().startswith("//"))
    return sorted(set(re.findall(r'registry\["([a-z0-9_]+)"\]', live)))


def discover_binaries():
    names = []
    for p in sorted(BIN_DIR.iterdir()):
        if not (p.is_file() and os.access(p, os.X_OK)):
            continue
        n = p.name
        if n == "logosphere-tests":
            continue
        if re.match(r"^(test[_-]|at_|eval_|logosphere-physics-guards)", n):
            names.append(n)
    return names


def run_one(name, kind, deadline):
    if kind == "registry":
        cmd = [str(RUNNER), "--test", name, "--no-head"]
    else:
        cmd = [str(BIN_DIR / name)]
    env = {k: v for k, v in os.environ.items() if k != "INTERACTIVE"}
    t0 = time.time()
    proc = subprocess.Popen(cmd, cwd=REPO, env=env, start_new_session=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        out, _ = proc.communicate(timeout=deadline)
        rc = proc.returncode
    except subprocess.TimeoutExpired:
        os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
        out, _ = proc.communicate()
        return "HUNG", None, time.time() - t0, out[-400:].decode(errors="replace")
    dt = time.time() - t0
    text = out.decode(errors="replace")
    if kind == "registry":
        ok = rc == 0 and "All tests passed" in text
    else:
        ok = rc == 0
    if "SKIPPED (headless)" in text or "SKIPPED(headless)" in text:
        return "SKIP", rc, dt, ""
    return ("PASS" if ok else "FAIL"), rc, dt, ("" if ok else text[-400:])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--filter", default="")
    ap.add_argument("--deadline", type=int, default=300)
    args = ap.parse_args()

    invariants = {r["id"]: r for r in load_jsonl(INV_FILE)}
    audit = {r["test"]: r for r in load_jsonl(AUDIT_FILE)}
    if not invariants:
        print("SWEEP_VERDICT: BROKEN-SETUP (no INVARIANTS.jsonl)"); sys.exit(2)
    if not RUNNER.exists():
        print("SWEEP_VERDICT: BROKEN-SETUP (build logosphere-tests first)"); sys.exit(2)
    # The invariants machine truth is validated before anything trusts it:
    # id pattern + enums (from schema/physics.yaml), dangling/cyclic
    # derives_from, audit links, mechanism symbols still in the tree.
    val = subprocess.run([sys.executable,
                          str(REPO / "scripts/validate_invariants.py")])
    if val.returncode != 0:
        print("SWEEP_VERDICT: BROKEN-SETUP (validate_invariants.py failed)")
        sys.exit(2)

    tests = [(n, "registry") for n in discover_registry()]
    tests += [(n, "binary") for n in discover_binaries()]
    if args.filter:
        tests = [(n, k) for n, k in tests if args.filter in n]

    moles_red, moles_green, unaudited, known_open, results = [], [], [], [], []
    counts = {"PASS": 0, "FAIL": 0, "HUNG": 0, "SKIP": 0}
    t_start = time.time()

    for i, (name, kind) in enumerate(tests, 1):
        verdict, rc, dt, tail = run_one(name, kind, args.deadline)
        counts[verdict] = counts.get(verdict, 0) + 1
        entry = audit.get(name)
        expect = entry["expect"] if entry else None
        cls = "OK"
        if entry is None:
            cls = "UNAUDITED"; unaudited.append(name)
        elif verdict == "HUNG":
            # A hang is a failure for baseline purposes: only a mole when the
            # audit expected this test to pass (a known-red or legacy test
            # that hangs instead of failing is the same known red).
            if expect == "pass" and not entry.get("known_open"):
                cls = "MOLE"; moles_red.append((name, entry, "HUNG"))
            elif expect == "pass":
                cls = "KNOWN-OPEN"; known_open.append((name, entry))
            else:
                cls = "OK"
        elif expect == "pass" and verdict == "FAIL":
            if entry.get("known_open"):
                cls = "KNOWN-OPEN"; known_open.append((name, entry))
            else:
                cls = "MOLE"; moles_red.append((name, entry, tail))
        elif expect == "fail" and verdict == "PASS":
            cls = "MOLE-GREEN"; moles_green.append((name, entry))
        elif expect == "skip" and verdict not in ("SKIP",):
            cls = "AUDIT-DRIFT"
        results.append({"test": name, "kind": kind, "verdict": verdict,
                        "class": cls, "rc": rc, "secs": round(dt, 1)})
        print(f"[{i:3d}/{len(tests)}] {verdict:5s} {cls:11s} {name} ({dt:.0f}s)",
              flush=True)

    # ---- the report ---------------------------------------------------------
    print("\n" + "=" * 70)
    print(f"PHYSICS SWEEP — {len(tests)} tests in {(time.time()-t_start)/60:.1f} min")
    print(f"  PASS {counts['PASS']}  FAIL {counts['FAIL']}  "
          f"HUNG {counts['HUNG']}  SKIP {counts['SKIP']}")

    if known_open:
        print(f"\nKNOWN OPEN ({len(known_open)}) — established reds, still loud:")
        for name, e in known_open:
            print(f"  {name}  [{e.get('known_open')}]")

    broken_inv = {}
    for name, e, _ in moles_red:
        for inv in (e.get("proves") or []) if e else []:
            broken_inv.setdefault(inv, []).append(name)
    # known-open reds break invariants too — an invariant does not care
    # whether its violation is freshly discovered.
    for name, e in known_open:
        for inv in e.get("proves") or []:
            broken_inv.setdefault(inv, []).append(name)

    if broken_inv:
        print(f"\nINVARIANTS BROKEN ({len(broken_inv)}):")
        for inv in sorted(broken_inv):
            slug = invariants.get(inv, {}).get("slug", "?")
            print(f"  {inv} ({slug}): {', '.join(sorted(set(broken_inv[inv])))}")
    else:
        print("\nINVARIANTS BROKEN: none")

    proving = {iid: 0 for iid in invariants}
    for e in audit.values():
        for inv in e.get("proves") or []:
            if inv in proving:
                proving[inv] += 1
    holes = [iid for iid, n in proving.items() if n == 0]
    if holes:
        print(f"COVERAGE HOLES (no proving test): {', '.join(sorted(holes))}")

    if moles_green:
        print(f"\nGONE GREEN ({len(moles_green)}) — expected fail, now passing; "
              f"promote in the audit + ledger:")
        for name, _ in moles_green:
            print(f"  {name}")
    if unaudited:
        print(f"\nUNAUDITED ({len(unaudited)}) — link to invariants or classify:")
        for name in unaudited:
            print(f"  {name}")

    RESULTS.write_text(json.dumps(results, indent=1))
    print(f"\nresults: {RESULTS}")

    n_moles = len(moles_red) + len(moles_green) + len(unaudited)
    if n_moles == 0:
        print("SWEEP_VERDICT: ALL QUIET")
        sys.exit(0)
    print(f"SWEEP_VERDICT: MOLES {n_moles} "
          f"(new-red {len(moles_red)}, gone-green {len(moles_green)}, "
          f"unaudited {len(unaudited)})")
    sys.exit(1)


if __name__ == "__main__":
    main()
