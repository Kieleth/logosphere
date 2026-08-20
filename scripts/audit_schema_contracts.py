#!/usr/bin/env python3
"""Audit for the defect classes found 2026-08-16.

Reads every schema YAML directly and merges them into one class map.
No linkml import resolution: this repo's packs import by bare name and
SchemaView cannot resolve that from the pack directory, which made an
earlier version of this script crash on every pack while a grep
swallowed the traceback and printed a clean sheet. Silence and "my
tool exploded" look identical, so the tool does not get to be silent.

Checks:

  C  open discriminator. A concrete subclass of a typed primitive whose
     type slot is unconstrained in itself AND in every ancestor.
     Inheritance matters: the engine's WorldEvent narrows event_type
     for ten concrete events, and reading only the leaf reports ten
     defects that are not there.

  B  uninhabitable range. A slot whose range names a class no instance
     can belong to: a mixin, or an abstract class with no concrete
     descendant.

  A  vocabulary that reaches no registry. An enum declared in a schema
     whose members appear in no generated registry file, so code may
     write values nothing ever heard of.
"""
import glob
import json
import re
import sys
from pathlib import Path

import yaml

ROOT = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
DISCRIMINATORS = {"Relation": "relation_type", "Event": "event_type",
                  "Signal": "signal_type"}

SCHEMA_GLOBS = ["schema/*.yaml", "schema/packs/*.yaml",
                "examples/*/schema/*.yaml", "examples/*/schema/*/*.yaml"]

classes, enums, owner = {}, {}, {}
for pattern in SCHEMA_GLOBS:
    for path in sorted(glob.glob(str(ROOT / pattern))):
        try:
            doc = yaml.safe_load(open(path)) or {}
        except Exception as exc:                       # noqa: BLE001
            print(f"UNREADABLE {path}: {exc}")
            continue
        rel = str(Path(path).relative_to(ROOT))
        for name, body in (doc.get("classes") or {}).items():
            classes[name] = body or {}
            owner[name] = rel
        for name, body in (doc.get("enums") or {}).items():
            enums[name] = body or {}
            owner[name] = rel

# The malleus root's own primitives, so ancestry terminates.
for prim in ("Entity", "Event", "Relation", "Signal", "Identifiable"):
    classes.setdefault(prim, {})


def ancestors(name, seen=None):
    seen = seen or set()
    if name in seen or name not in classes:
        return []
    seen.add(name)
    out, body = [], classes[name]
    for parent in filter(None, [body.get("is_a")] + (body.get("mixins") or [])):
        out += [parent] + ancestors(parent, seen)
    return out


def is_concrete(name):
    b = classes.get(name, {})
    return not b.get("abstract") and not b.get("mixin")


findings = []

# --- C ---------------------------------------------------------------
for name in sorted(classes):
    if not is_concrete(name):
        continue
    anc = set(ancestors(name))
    for prim, slot in DISCRIMINATORS.items():
        if prim not in anc:
            continue
        rng = eq = None
        for c in [name] + ancestors(name):
            u = ((classes.get(c, {}).get("slot_usage") or {}).get(slot) or {})
            rng = rng or u.get("range")
            eq = eq or u.get("equals_string")
        if eq or (rng and rng in enums):
            continue
        findings.append(
            f"[C open-discriminator] {owner.get(name)}: {name}.{slot} "
            f"unconstrained here and in every ancestor "
            f"(is_a={classes[name].get('is_a')})")

# --- B ---------------------------------------------------------------
concrete_desc = {}
for name in classes:
    for a in ancestors(name):
        if is_concrete(name):
            concrete_desc.setdefault(a, []).append(name)

for name in sorted(classes):
    for slot, u in (classes[name].get("slot_usage") or {}).items():
        rng = (u or {}).get("range")
        if not rng or rng not in classes:
            continue
        body = classes[rng]
        if body.get("mixin"):
            findings.append(
                f"[B uninhabitable-range] {owner.get(name)}: "
                f"{name}.{slot} -> {rng}, a MIXIN; no instance belongs to it")
        elif body.get("abstract") and not concrete_desc.get(rng):
            findings.append(
                f"[B uninhabitable-range] {owner.get(name)}: "
                f"{name}.{slot} -> {rng}, abstract with no concrete subtype")

# --- A ---------------------------------------------------------------
generated = ""
generated_files = 0
for pattern in ("src/generated/*.cpp", "examples/*/src/generated/*.cpp"):
    for path in glob.glob(str(ROOT / pattern)):
        generated += Path(path).read_text()
        generated_files += 1

# With no generated output at all, "this enum is in no registry" and
# "nothing has been generated yet" are the same observation, and check A
# cannot tell them apart. An unanswerable question says so; it does not
# answer no. Found by the vacuity test, which planted a clean tree and
# watched this check fire on it.
if generated_files == 0:
    # Deliberately not shaped like a finding: findings start with "[".
    # This is the audit declining to answer, which is a different thing
    # from answering clean, and the reader has to be able to tell.
    print("note: check A skipped, no generated registry files under this "
          "root, so registration cannot be judged")

# Only schemas that declare KG types get asked. schema/physics.yaml is
# standalone by design and says so at the top: nothing in it is a KG
# entity type, and it generates a constants header rather than a
# registry. Asking "why is this enum not in a registry" of a file that
# deliberately has none produces six confident falsehoods.
kg_files = {
    owner[c] for c in classes
    if {"Entity", "Event", "Relation", "Signal"} & set(ancestors(c))
}

for name, body in sorted(enums.items()):
    if generated_files == 0 or owner.get(name) not in kg_files:
        continue
    members = list((body.get("permissible_values") or {}).keys())
    if not members:
        continue
    seen = [m for m in members if json.dumps(m) in generated]
    if not seen:
        findings.append(
            f"[A unregistered-vocabulary] {owner.get(name)}: enum {name} "
            f"({len(members)} members) appears in NO generated registry")
    elif len(seen) < len(members):
        missing = sorted(set(members) - set(seen))
        findings.append(
            f"[A partial-vocabulary] {owner.get(name)}: {len(missing)} of "
            f"{len(members)} members unregistered: {', '.join(missing[:6])}")

print(f"schemas read: {len(set(owner.values()))}, "
      f"classes: {len(classes)}, enums: {len(enums)}")
for f in findings:
    print(f)
print(f"findings: {len(findings)}")
