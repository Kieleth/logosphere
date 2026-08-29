#!/usr/bin/env python3
"""Voyager's own book, reflected: every chapter into its seed.

Reads the Voyager book corpus (corpora/voyager-book) and emits one
seed per chapter. Chapter One fixes the shape: the kinds of moments,
the ways a season is spent. Chapter Two fixes the numbers the shape
needs in play: what a season costs and the bounds a moment's chance
must respect. Both chapters may leave questions open on purpose, and
those load as records too.

The extractor carries NO list of kinds and NO list of season modes.
Both are derived from the chapters' own text, so the schema enum is
the only closed vocabulary. Write a sixth kind into the book and this
extractor will emit it; the ontology will refuse it at load until the
schema grows the value; the drift gate turns red in between. That
refusal chain is the design, not an accident.

The revision pinned in every seed is one content hash over ALL the
chapters, in filename order: the seeds of one book must declare one
revision, and a book's edition is the whole book, not the chapter
that happened to change. Not a git commit, because the book lives in
this repository and a seed pinned to repository history would drift
every time an unrelated commit landed.

Usage: extract_book.py --corpus <corpora/voyager-book> \
                       --out-dir <examples/voyager/seeds>
"""

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

CHAPTER_ONE = "01-the-shape-of-a-career.md"
CHAPTER_TWO = "02-seasons-and-moments-in-play.md"
CHAPTERS = [CHAPTER_ONE, CHAPTER_TWO]
LAYER = "voyager-book"

KINDS_SECTION = "The kinds of moments"
SEASONS_SECTION = "Seasons"
UNSETTLED_SECTION = "Unsettled"
SEASON_COST_SECTION = "What a season costs"
MOMENT_RISK_SECTION = "What a moment risks"

# The sentence that fixes the three ways a season is spent.
SEASON_SENTENCE_MARK = "A season is spent one of three ways"
# The sentence that fixes what a season costs.
SEASON_COST_MARK = "ages a character by 1 year"
# The sentence that fixes the bounds of a moment's chance.
CHANCE_BOUNDS_MARK = "keep it between 0.05 and 0.95"


def read_chapter(corpus_root: Path, chapter: str) -> str:
    path = corpus_root / chapter
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
    """The exact bytes of the sentence holding the mark. The end is
    searched from AFTER the mark, because a mark may itself contain a
    full stop: "0.05" must not end the sentence at its own dot."""
    at = text.find(mark)
    if at < 0:
        sys.exit(f"the chapter no longer says '{mark}'")
    start = text.rfind(". ", 0, at)
    start = 0 if start < 0 else start + 2
    tail = at + len(mark)
    ends = [e for e in (text.find(". ", tail), text.find(".\n", tail))
            if e >= 0]
    if not ends:
        sys.exit(f"the sentence at '{mark}' never ends")
    return text[start:min(ends) + 1]


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
    # \s+ because a corpus line may in principle wrap; ours do not,
    # but the match must not depend on it.
    modes = re.findall(r"\bin\s+([a-z]+),", sentence)
    if not modes:
        sys.exit("the season sentence names no ways to spend one")
    return sentence, modes


def unsettled_of(text: str):
    """Each bullet of the Unsettled section, exact bytes; empty list
    when the chapter has no such section, because a chapter with
    nothing open is allowed to say nothing."""
    if not re.search(r"^## " + re.escape(UNSETTLED_SECTION) + r"$",
                     text, re.M):
        return []
    body = section_of(text, UNSETTLED_SECTION)
    out = []
    for found in re.finditer(r"^- .*?(?=\n- |\n\n|\Z)", body, re.M | re.S):
        out.append(found.group(0).strip())
    return out


def envelope(chapter: str, revision: str, ops, counts):
    return {
        "source": {"file": chapter, "commit": revision},
        "layer": LAYER,
        "generated_by": "examples/voyager/tools/extract_book.py",
        "invariants": {
            "count_of_type": counts,
            "unique_name_per_type": sorted(counts.keys()),
        },
        "ops": ops,
    }


def create(ops, entity_type, alias, properties):
    ops.append({"op": "create_entity", "type": entity_type,
                "as": alias, "properties": properties})


def unsettled_ops(ops, text, chapter):
    stem = chapter.split("-")[0]
    questions = unsettled_of(text)
    for index, question in enumerate(questions, start=1):
        create(ops, "UnsettledQuestion", f"@unsettled_{stem}_{index}", {
            "name": f"unsettled {stem}-{index}",
            "question_text": question,
            "source_section": UNSETTLED_SECTION,
            "source_kind": "list_item",
            "source_quote": question,
        })
    return len(questions)


def build_chapter_one(text: str, revision: str):
    ops = []
    kinds = kinds_of(text)
    for kind in kinds:
        create(ops, "MomentKind", f"@kind_{kind['name'].lower()}", {
            "name": kind["name"],
            "moment_kind_key": kind["name"].lower(),
            "source_section": KINDS_SECTION,
            "source_kind": "paragraph",
            "source_quote": kind["quote"],
        })
    sentence, modes = season_modes_of(text)
    for mode in modes:
        create(ops, "SeasonMode", f"@season_{mode}", {
            "name": mode,
            "season_mode_key": mode,
            "source_section": SEASONS_SECTION,
            "source_kind": "sentence",
            "source_quote": sentence,
        })
    questions = unsettled_ops(ops, text, CHAPTER_ONE)
    return envelope(CHAPTER_ONE, revision, ops, {
        "MomentKind": len(kinds),
        "SeasonMode": len(modes),
        "UnsettledQuestion": questions,
    })


def build_chapter_two(text: str, revision: str):
    ops = []
    season_sentence = sentence_containing(
        section_of(text, SEASON_COST_SECTION), SEASON_COST_MARK)
    create(ops, "RuleConstant", "@season_standard_years", {
        "name": "season_standard_years",
        "constant_value": "1",
        "source_section": SEASON_COST_SECTION,
        "source_kind": "sentence",
        "source_quote": season_sentence,
    })
    bounds_sentence = sentence_containing(
        section_of(text, MOMENT_RISK_SECTION), CHANCE_BOUNDS_MARK)
    for name, value in (("moment_chance_floor", "0.05"),
                        ("moment_chance_ceiling", "0.95")):
        create(ops, "RuleConstant", f"@{name}", {
            "name": name,
            "constant_value": value,
            "source_section": MOMENT_RISK_SECTION,
            "source_kind": "sentence",
            "source_quote": bounds_sentence,
        })
    questions = unsettled_ops(ops, text, CHAPTER_TWO)
    return envelope(CHAPTER_TWO, revision, ops, {
        "RuleConstant": 3,
        "UnsettledQuestion": questions,
    })


def build_life_procedure(text: str, revision: str):
    """The play procedure: one season, broken by one moment.

    Step ORDER is a composition decision and lives here, in the
    game's own tooling, not in the book; each step still cites the
    sentence that makes it a rule. Emitted by the extractor rather
    than hand-authored because every seed of the book must carry the
    book's one revision, and a hand-written copy of a moving revision
    is a drift trap.
    """
    ops = []
    create(ops, "Procedure", "@voyager_life", {
        "name": "voyager_life",
        "source_section": SEASON_COST_SECTION,
        "source_kind": "heading",
        "source_quote": SEASON_COST_SECTION,
    })
    choosing = sentence_containing(
        section_of(text, SEASON_COST_SECTION),
        "Choosing how a season is spent")
    create(ops, "ProcedureStep", "@spend_season", {
        "name": "spend_season",
        "step_index": "0",
        "primitive_ref": "spend_season",
        "source_section": SEASON_COST_SECTION,
        "source_kind": "sentence",
        "source_quote": choosing,
    })
    fate = sentence_containing(
        section_of(text, "What stage is"),
        "A kind you have never faced arrives as fate")
    create(ops, "ProcedureStep", "@face_moment", {
        "name": "face_moment",
        "step_index": "1",
        "primitive_ref": "face_moment",
        "source_section": "What stage is",
        "source_kind": "sentence",
        "source_quote": fate,
    })
    for step in ("@spend_season", "@face_moment"):
        ops.append({"op": "set_relation", "from": "@voyager_life",
                    "relation": "HAS_PART", "to": step})
    return envelope(CHAPTER_TWO, revision, ops, {
        "Procedure": 1,
        "ProcedureStep": 2,
    })


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus", required=True,
                        help="root of the vendored book corpus (declared, "
                             "never derived from this file's location)")
    parser.add_argument("--out-dir", required=True,
                        help="directory the seeds are written into")
    args = parser.parse_args()

    corpus_root = Path(args.corpus).resolve()
    digest = hashlib.sha256()
    for chapter in CHAPTERS:
        path = corpus_root / chapter
        if not path.is_file():
            sys.exit(f"no chapter at {path}")
        digest.update(path.read_bytes())
    revision = "sha256:" + digest.hexdigest()

    seeds = {
        "voyager_book_rules.json":
            build_chapter_one(read_chapter(corpus_root, CHAPTER_ONE),
                              revision),
        "voyager_book_play_rules.json":
            build_chapter_two(read_chapter(corpus_root, CHAPTER_TWO),
                              revision),
        "voyager_life_procedure.json":
            build_life_procedure(read_chapter(corpus_root, CHAPTER_TWO),
                                 revision),
    }
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    for filename, seed in seeds.items():
        out = out_dir / filename
        out.write_text(json.dumps(seed, indent=2, ensure_ascii=False) + "\n",
                       encoding="utf-8")
        counts = seed["invariants"]["count_of_type"]
        print(f"{out}: {len(seed['ops'])} ops, " +
              ", ".join(f"{k} {v}" for k, v in sorted(counts.items())))


if __name__ == "__main__":
    main()
