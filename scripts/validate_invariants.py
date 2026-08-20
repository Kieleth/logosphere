#!/usr/bin/env python3
"""Validate the invariants machine truth before anyone trusts it.

Checks INVARIANTS.jsonl and TEST_AUDIT.jsonl for the failure modes a
JSON-schema pass cannot see, taking id pattern and enum vocabularies
from schema/physics.yaml (single source, no second declaration):

  1. id unique + matches the schema's pattern
  2. kind / verification / status values exist in the schema enums
  3. derives_from references resolve (no danglers)
  4. derives_from is acyclic
  5. every TEST_AUDIT proves/touches id exists in INVARIANTS.jsonl
  6. every mechanism-named symbol (underscore identifiers, minus
     single-letter math stems like I_light) still exists in the tree:
     content via git grep, or a path name via git ls-files.
     tests/invariants/ is excluded so the ledger cannot satisfy itself.

Run by scripts/physics_sweep.py at startup; failure is BROKEN-SETUP.
"""
import json, re, subprocess, sys
from pathlib import Path

import yaml

REPO = Path(__file__).resolve().parent.parent
GREP_PATHS = ["src", "include", "tests", "scripts", "schema", "docs",
              "examples", "CMakeLists.txt", ":(exclude)tests/invariants"]


def main() -> int:
    errors = []
    sch = yaml.safe_load((REPO / "schema/physics.yaml").read_text())
    enum = lambda name: set(sch["enums"][name]["permissible_values"])
    vocab = {"kind": enum("InvariantKind"), "verification": enum("VerificationMode"),
             "status": enum("InvariantStatus")}
    id_pat = re.compile(sch["classes"]["Invariant"]["attributes"]["id"]["pattern"])

    rows = [json.loads(l) for l in
            (REPO / "tests/invariants/INVARIANTS.jsonl").read_text().splitlines() if l.strip()]
    ids = [r["id"] for r in rows]
    if len(ids) != len(set(ids)):
        errors.append(f"duplicate invariant ids: {sorted(i for i in ids if ids.count(i) > 1)}")
    edges = {}
    for r in rows:
        if not id_pat.fullmatch(r["id"]):
            errors.append(f'{r["id"]}: id does not match {id_pat.pattern}')
        for field, allowed in vocab.items():
            if r.get(field) not in allowed:
                errors.append(f'{r["id"]}: {field}={r.get(field)!r} not in {sorted(allowed)}')
        edges[r["id"]] = r.get("derives_from") or []
        for d in edges[r["id"]]:
            if d not in ids:
                errors.append(f'{r["id"]}: derives_from dangling reference {d}')

    def cyclic(node, stack):
        if node in stack:
            return stack[stack.index(node):] + [node]
        for d in edges.get(node, []):
            hit = cyclic(d, stack + [node])
            if hit:
                return hit
        return None
    for i in ids:
        hit = cyclic(i, [])
        if hit:
            errors.append(f'derives_from cycle: {" -> ".join(hit)}')
            break

    # SANITIZER_AUDIT.jsonl carries TEST_AUDIT's row shape plus a
    # `finding` field, for reds the sanitizer lane accepts. Same link
    # rule: a ledger that can name an invariant that does not exist is
    # not a ledger.
    for audit in ("TEST_AUDIT.jsonl", "SANITIZER_AUDIT.jsonl"):
        path = REPO / "tests/invariants" / audit
        if not path.exists():
            continue
        for a in path.read_text().splitlines():
            if not a.strip():
                continue
            row = json.loads(a)
            for link in (row.get("proves") or []) + (row.get("touches") or []):
                if link not in ids:
                    errors.append(
                        f'{audit} {row["test"]}: links unknown invariant {link}')

    tree_paths = subprocess.run(["git", "ls-files"], cwd=REPO, capture_output=True,
                                text=True).stdout
    sym_re = re.compile(r"\b[A-Za-z][A-Za-z0-9]+(?:_[A-Za-z0-9]+)+\b")  # 2+ char stem
    for r in rows:
        for tok in sorted(set(sym_re.findall(r["mechanism"]))):
            in_content = subprocess.run(
                ["git", "grep", "-l", "--fixed-strings", tok, "--"] + GREP_PATHS,
                cwd=REPO, capture_output=True).returncode == 0
            if not in_content and tok not in tree_paths:
                errors.append(f'{r["id"]}: mechanism names {tok}, not found in tree')

    for e in errors:
        print(f"INVARIANTS-INVALID: {e}")
    if not errors:
        print(f"invariants OK: {len(rows)} rows, links resolve, mechanisms grounded")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
