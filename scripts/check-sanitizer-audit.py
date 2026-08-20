#!/usr/bin/env python3
"""Red means NEW: the sanitizer lane's failing set must equal the audited set.

WHY THIS EXISTS. sanitizers-linux lands red on every run with nine
failing tests. Nine is not a number anyone reads. A tenth appearing --
which is the entire point of the lane -- looks exactly like the nine,
because a red job is a red job. The lane was reporting a finding it
could no longer distinguish from its own background.

The repository already had the answer and had not applied it here.
tests/invariants/TEST_AUDIT.jsonl records an expected verdict per test,
298 of them, 20 marked `expect: fail`, and physics-linux enforces that
direction on its born-red ladders: a ladder that silently goes GREEN
fails CI as loudly as a new red, because it means either a mechanism
landed unrecorded or an assertion was weakened.

This is that discipline for the sanitizer lane. Both directions:

  a test failing that is NOT audited   -> a new memory error. FAIL.
  a test audited that is NOT failing   -> the finding is gone, or the
                                          test stopped running. FAIL,
                                          and update the audit.

WHY A SEPARATE FILE, and it is the smallest extension available.
TEST_AUDIT.jsonl is keyed on the test name, one row per test, and its
`expect` field is lane-agnostic. Six of these nine already have a row
there saying `expect: pass`, which is TRUE in headless-linux and false
here: they pass unsanitized and fail under ASan. A second row per test
with the opposite verdict would make that file self-contradictory
unless you knew to read a lane field it does not have.

And these entries record something TEST_AUDIT does not describe. A row
there says what a test proves. Eight of these say what an INSTRUMENT
reported, on a toolchain, about code that is behaving correctly -- and
seven of the eight would vanish against an ASan-instrumented libc++.
That is the distinction the `finding` field carries, and it is the only
field added to the nine that TEST_AUDIT already has.

WHY THE CTEST EXIT CODE IS AN INPUT. CTest writes
LastTestsFailed.log only when something failed. Measured: an all-pass
run leaves Testing/Temporary/ with LastTest.log and no LastTestsFailed.
So an absent log means one of two opposite things — every test passed,
or the step died before finishing — and this gate must not confuse them.
The step therefore records ctest's exit code beside the log, and this
reads it: 0 means the failing set is genuinely empty (all nine audited
findings went quiet, which is a change and fails), non-zero with no log
means the run did not finish (which is not a result at all).

Usage:
    check-sanitizer-audit.py --last-failed build-san/Testing/Temporary/LastTestsFailed.log \
                             --ctest-exit  build-san/Testing/Temporary/ctest.exit
    check-sanitizer-audit.py --last-failed <log> --audit <jsonl>

Exit 0 = the set matches the audit exactly. Exit 1 = it does not, with
the difference named. Exit 2 = the input is unreadable, which is not
the same as clean and must never be reported as clean.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

REQUIRED_KEYS = {"test", "kind", "status", "expect", "finding",
                 "proves", "touches", "what_it_proves", "gaps", "known_open"}


def load_audit(path: Path):
    """Return {test: row}. Every row must be an accepted red."""
    rows, problems = {}, []
    for line_no, line in enumerate(path.read_text().splitlines(), 1):
        if not line.strip():
            continue
        try:
            row = json.loads(line)
        except json.JSONDecodeError as exc:
            problems.append(f"{path.name}:{line_no}: not JSON ({exc})")
            continue
        missing = REQUIRED_KEYS - set(row)
        extra = set(row) - REQUIRED_KEYS
        if missing or extra:
            problems.append(
                f"{path.name}:{line_no}: keys differ from TEST_AUDIT's shape "
                f"(missing {sorted(missing)}, unexpected {sorted(extra)})")
            continue
        if row["expect"] != "fail":
            problems.append(
                f"{path.name}:{line_no}: expect={row['expect']!r}. This file "
                f"records accepted REDS only; a passing test belongs in "
                f"TEST_AUDIT.jsonl.")
            continue
        if row["test"] in rows:
            problems.append(f"{path.name}:{line_no}: {row['test']} listed twice")
            continue
        rows[row["test"]] = row
    return rows, problems


def load_failed(path: Path):
    """CTest writes `<index>:<name>` per failing test, in run order."""
    names = []
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line:
            continue
        names.append(line.split(":", 1)[1] if ":" in line else line)
    return names


def main() -> int:
    repo = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--last-failed", type=Path, required=True,
                        help="CTest's Testing/Temporary/LastTestsFailed.log")
    parser.add_argument("--ctest-exit", type=Path,
                        help="file holding ctest's exit code. Without it, an "
                             "absent log is treated as a run that did not "
                             "finish, because it cannot be told from a run "
                             "in which everything passed.")
    parser.add_argument("--audit", type=Path,
                        default=repo / "tests/invariants/SANITIZER_AUDIT.jsonl")
    args = parser.parse_args()

    if not args.audit.is_file():
        print(f"::error::no audit file at {args.audit}", file=sys.stderr)
        return 2

    audit, problems = load_audit(args.audit)
    if problems:
        print(f"::error::{args.audit} is malformed:")
        for problem in problems:
            print(f"  {problem}")
        return 2

    ctest_exit = None
    if args.ctest_exit and args.ctest_exit.is_file():
        text = args.ctest_exit.read_text().strip()
        if text.lstrip("-").isdigit():
            ctest_exit = int(text)

    if not args.last_failed.is_file():
        # CTest writes this file only when something failed, so an absent
        # log means one of two opposite things. Its exit code is what
        # tells them apart.
        if ctest_exit == 0:
            failed = []          # everything passed. A change, handled below.
        else:
            print(f"::error::{args.last_failed} is absent and ctest did not "
                  f"report success (exit {ctest_exit if ctest_exit is not None else 'unknown'}). "
                  f"The suite did not finish: a build failure, a timeout kill, "
                  f"or a wrong path. This is NOT a clean run.",
                  file=sys.stderr)
            return 2
    else:
        failed = load_failed(args.last_failed)
    failed_set = set(failed)
    audited_set = set(audit)

    unaudited = sorted(failed_set - audited_set)
    recovered = sorted(audited_set - failed_set)

    if not unaudited and not recovered:
        print(f"sanitizer audit: {len(failed)} failing, all {len(audit)} "
              f"audited, and no audited finding went quiet.")
        for name in sorted(audited_set):
            print(f"  {name}: {audit[name]['finding']}")
        real = [n for n, r in audit.items() if r["status"] == "real-defect"]
        print(f"\n{len(audit) - len(real)} instrument artifact(s), "
              f"{len(real)} real defect(s): {', '.join(sorted(real)) or 'none'}")
        return 0

    print("::error::the sanitizer lane's failing set is not the audited set.")
    if unaudited:
        print(f"\nNEW RED, {len(unaudited)} test(s) failing with no audited "
              f"entry. This is what the lane exists to tell you:")
        for name in unaudited:
            print(f"  {name}")
        print("\nRead the report in the step above, then add one row to "
              "tests/invariants/SANITIZER_AUDIT.jsonl naming the finding and "
              "whether it is a real defect or an artifact of the toolchain. "
              "Do not suppress it and do not widen ASAN_OPTIONS.")
    if recovered:
        print(f"\nWENT QUIET, {len(recovered)} audited finding(s) not reported "
              f"this run:")
        for name in recovered:
            print(f"  {name}: {audit[name]['finding']}")
        print("\nEither the finding is fixed -- in which case delete its row, "
              "in the same commit as the fix -- or the test stopped running, "
              "which is worse. Direction-locked on purpose: an audited red "
              "that silently goes green is as much a change as a new one.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
