#!/usr/bin/env bash
# Regenerate every seed this game generates, from the corpus it reads.
#
# A seed carrying `generated_by` is the extractor's and is never
# hand-edited: teach the extractor and run this. CI runs it and diffs,
# so a hand edit shows up as drift rather than as a mystery.
#
# The corpus is DECLARED, not derived: it lives outside every game and
# is passed in by name, the same way logosphere_game_corpus() hands the
# C++ its root.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
game="$(dirname "$here")"
root="${LOGOSPHERE_ROOT:-$(cd "$game/../.." && pwd)}"
corpus="$root/corpora/cepheus-srd"
book="$root/corpora/voyager-book"

if [ ! -d "$corpus" ]; then
    echo "no corpus at $corpus (set LOGOSPHERE_ROOT to the repo root)" >&2
    exit 1
fi
if [ ! -d "$book" ]; then
    echo "no book at $book (set LOGOSPHERE_ROOT to the repo root)" >&2
    exit 1
fi

python3 "$here/extract_chargen.py" \
    --corpus "$corpus" \
    --out "$game/seeds/voyager_cepheus_rules.json"

# The game's own book, through the same reflection. Edit a chapter and
# commit without rerunning this, and CI shows the book and the graph
# disagreeing, which is the loop working.
python3 "$here/extract_book.py" \
    --corpus "$book" \
    --out "$game/seeds/voyager_book_rules.json"

echo "seeds/voyager_chargen_procedure.json is hand-authored (no"
echo "generated_by): the order of the steps is a composition decision,"
echo "not something read out of the book."
