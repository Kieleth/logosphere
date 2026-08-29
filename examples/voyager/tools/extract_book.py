#!/usr/bin/env python3
"""Voyager's own book, reflected: chapter one into a seed.

Reads the Voyager book corpus (corpora/voyager-book) and emits the
rules Chapter One fixes: the kinds of moments, the ways a season is
spent, and the questions the book leaves open on purpose.

The extractor carries NO list of kinds and NO list of season modes.
Both are derived from the chapter's own text, so the schema enum is
the only closed vocabulary. Write a sixth kind into the book and this
extractor will emit it; the ontology will refuse it at load until the
schema grows the value; the drift gate turns red in between. That
refusal chain is the design, not an accident.

The revision pinned in the seed is the content hash of the chapter
itself, not a git commit: the book lives in this repository, and a
seed that pinned the repository's own history would drift every time
an unrelated commit landed.

Usage: extract_book.py --corpus <corpora/voyager-book> \
                       --out <examples/voyager/seeds/...json>
"""

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

CHAPTER = "01-the-shape-of-a-career.md"
LAYER = "voyager-book"

KINDS_SECTION = "The kinds of moments"
SEASONS_SECTION = "Seasons"
UNSETTLED_SECTION = "Unsettled"

# The sentence that fixes the three ways a season is spent.
SEASON_SENTENCE_MARK = "A season is spent one of three ways"


def read_chapter(corpus_root: Path) -> str:
    path = corpus_root / CHAPTER
    if not path.is_file():
        sys.exit(f"no chapter at {path}: is --corpus the corpus root?")
    return path.read_text(encoding="utf-8")


def section_of(text: str, heading: str) -> str:
    """The exact bytes of one ## section, heading line excluded."""
    pattern = re.compile(
        r"^## " + re.escape(heading) + r"\n(.*?)(?=^## |\Z)",
        re.M | re.S)
    found = pattern.search(text)
    if not found:
        sys.exit(f"the chapter has no section '## {heading}'")
    return found.group(1)


def sentence_containing(text: str, mark: str) -> str:
    """The exact bytes of the sentence holding the mark, newlines kept."""
    at = text.find(mark)
    if at < 0:
        sys.exit(f"the chapter no longer says '{mark}'")
    start = text.rfind(". ", 0, at)
    start = 0 if start < 0 else start + 2
    end = text.find(".", at)
    if end < 0:
        sys.exit(f"the sentence at '{mark}' never ends")
    return text[start:end + 1]


def kinds_of(text: str):
    """Every bold-led definition paragraph in the kinds section."""
    body = section_of(text, KINDS_SECTION)
    out = []
    for found in re.finditer(
            r"^\*\*([A-Z][a-z]+)\.\*\*.*?(?=\n\n|\Z)", body, re.M | re.S):
        out.append({"name": found.group(1),
                    "quote": found.group(0).strip()})
    if not out:
        sys.exit("the kinds section defines no kinds")
    return out


def season_modes_of(text: str):
    """The three ways, read out of the one sentence that fixes them."""
    sentence = sentence_containing(section_of(text, SEASONS_SECTION),
                                   SEASON_SENTENCE_MARK)
    # \s+ because the corpus is hard-wrapped and "in" may end a line.
    modes = re.findall(r"\bin\s+([a-z]+),", sentence)
    if not modes:
        sys.exit("the season sentence names no ways to spend one")
    return sentence, modes


def unsettled_of(text: str):
    """Each bullet of the Unsettled section, exact bytes."""
    body = section_of(text, UNSETTLED_SECTION)
    out = []
    for found in re.finditer(r"^- .*?(?=\n- |\n\n|\Z)", body, re.M | re.S):
        out.append(found.group(0).strip())
    if not out:
        sys.exit("the Unsettled section is empty; delete it or fill it")
    return out


def build(text: str, revision: str):
    ops = []

    def create(entity_type, alias, properties):
        ops.append({"op": "create_entity", "type": entity_type,
                    "as": alias, "properties": properties})

    kinds = kinds_of(text)
    for kind in kinds:
        create("MomentKind", f"@kind_{kind['name'].lower()}", {
            "name": kind["name"],
            "moment_kind_key": kind["name"].lower(),
            "source_section": KINDS_SECTION,
            "source_kind": "paragraph",
            "source_quote": kind["quote"],
        })

    sentence, modes = season_modes_of(text)
    for mode in modes:
        create("SeasonMode", f"@season_{mode}", {
            "name": mode,
            "season_mode_key": mode,
            "source_section": SEASONS_SECTION,
            "source_kind": "sentence",
            "source_quote": sentence,
        })

    questions = unsettled_of(text)
    for index, question in enumerate(questions, start=1):
        create("UnsettledQuestion", f"@unsettled_{index}", {
            "name": f"unsettled {index}",
            "question_text": question,
            "source_section": UNSETTLED_SECTION,
            "source_kind": "list_item",
            "source_quote": question,
        })

    invariants = {
        "count_of_type": {
            "MomentKind": len(kinds),
            "SeasonMode": len(modes),
            "UnsettledQuestion": len(questions),
        },
        "unique_name_per_type": [
            "MomentKind", "SeasonMode", "UnsettledQuestion",
        ],
    }
    return {
        "source": {"file": CHAPTER, "commit": revision},
        "layer": LAYER,
        "generated_by": "examples/voyager/tools/extract_book.py",
        "invariants": invariants,
        "ops": ops,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus", required=True,
                        help="root of the vendored book corpus (declared, "
                             "never derived from this file's location)")
    parser.add_argument("--out", required=True, help="seed file to write")
    args = parser.parse_args()

    corpus_root = Path(args.corpus).resolve()
    raw = (corpus_root / CHAPTER).read_bytes() \
        if (corpus_root / CHAPTER).is_file() else None
    if raw is None:
        sys.exit(f"no chapter at {corpus_root / CHAPTER}")
    revision = "sha256:" + hashlib.sha256(raw).hexdigest()
    text = read_chapter(corpus_root)

    seed = build(text, revision)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(seed, indent=2, ensure_ascii=False) + "\n",
                   encoding="utf-8")
    counts = seed["invariants"]["count_of_type"]
    print(f"{out}: {len(seed['ops'])} ops, " +
          ", ".join(f"{k} {v}" for k, v in sorted(counts.items())))


if __name__ == "__main__":
    main()
