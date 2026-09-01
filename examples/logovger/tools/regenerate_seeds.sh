#!/usr/bin/env bash
# Rebuild every seed that a tool owns, from the vendored SRD.
#
# THE POINT: a seed with "generated_by" in its envelope is the output of
# the script it names. Editing such a file by hand looks like it works
# and is undone by the next regeneration. It happened: two PRs added a
# mishap's choice modelling straight into cepheus_book1_shared_tables
# .json, the extractor never learned it, and regenerating would have
# dropped fifteen ops without a word.
#
# CI runs this and then `git diff --exit-code`, so a hand-edit to a
# generated seed fails the build instead of surviving to be lost.
# Run it locally after changing an extractor.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
srd="$repo/corpora/cepheus-srd"
seeds="$repo/examples/logovger/seeds"

python3 "$here/extract_skills.py" "$srd" \
    "$seeds/cepheus_book1_skill_vocabulary.json"

# Careers reads the vocabulary too: every skill a service table grants
# is resolved against it, and one that does not resolve fails the run.
python3 "$here/extract_careers.py" "$srd" \
    "$seeds/cepheus_book1_skill_vocabulary.json" \
    "$seeds/cepheus_careers.json"

# Careers reads the skill vocabulary, so it runs second: every skill a
# career table grants is resolved against it, and an unresolved one
# fails the run rather than being guessed at.
python3 "$here/extract_career_tables.py" "$srd" \
    "$seeds/cepheus_book1_skill_vocabulary.json" \
    "$seeds/cepheus_book1_career_tables.json"

python3 "$here/extract_shared_tables.py" "$srd" \
    "$seeds/cepheus_book1_shared_tables.json"

# Every seed this wrote must say who wrote it, and no other seed may
# claim to be generated. That is what makes the diff check meaningful:
# an unmarked generated file is one nothing regenerates.
python3 - "$seeds" <<'PY'
import json, os, sys

seeds = sys.argv[1]
generated = {
    "cepheus_book1_skill_vocabulary.json":
        "examples/logovger/tools/extract_skills.py",
    "cepheus_careers.json":
        "examples/logovger/tools/extract_careers.py",
    "cepheus_book1_career_tables.json":
        "examples/logovger/tools/extract_career_tables.py",
    "cepheus_book1_shared_tables.json":
        "examples/logovger/tools/extract_shared_tables.py",
}

problems = []
for name in sorted(os.listdir(seeds)):
    if not name.endswith(".json"):
        continue
    with open(os.path.join(seeds, name), encoding="utf-8") as handle:
        marker = json.load(handle).get("generated_by")
    expected = generated.get(name)
    if marker != expected:
        problems.append(
            "%s: generated_by is %r, expected %r" % (name, marker, expected))

if problems:
    print("seed ownership markers are wrong:")
    for line in problems:
        print("  " + line)
    sys.exit(1)
print("seed ownership: %d generated, the rest hand-authored" % len(generated))
PY
