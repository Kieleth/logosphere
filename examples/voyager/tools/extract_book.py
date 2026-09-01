#!/usr/bin/env python3
"""Voyager's own book, reflected: every chapter into its seed.

Reads the Voyager book corpus (corpora/voyager-book) and emits one
seed per chapter plus the life procedure. Chapter One fixes the shape:
the kinds of moments, the ways a season is spent. Chapter Two fixes
what a season costs and the bounds a moment's chance must respect.
Chapter Three fixes what a life carries and what a moment may do to
it. Chapter Four is the director's playbook: how events arrive, the
doors, and examples per kind. Every chapter may leave questions open
on purpose, and those load as records too.

The extractor carries NO list of kinds, modes, standings, effects,
turns, weights or doors. Every vocabulary is derived from the
chapters' own sentences, so the schema enum is the only closed list.
Write a sixth kind into the book and this extractor will emit it; the
ontology will refuse it at load until the schema grows the value; the
drift gate turns red in between. That refusal chain is the design.

Keys are derived from the book's phrases by one rule: take the phrase,
drop articles, join with underscores. "move a characteristic" is
move_characteristic; "the career ends" is career_ends. The book
therefore names its own keys, and a phrase the schema does not know
is refused rather than mapped.

The revision pinned in every seed is one content hash over ALL the
chapters, in filename order: the seeds of one book must declare one
revision, and a book's edition is the whole book.

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
CHAPTER_THREE = "03-what-a-life-carries.md"
CHAPTER_FOUR = "04-the-directors-playbook.md"
CHAPTERS = [CHAPTER_ONE, CHAPTER_TWO, CHAPTER_THREE, CHAPTER_FOUR]
LAYER = "voyager-book"

ARTICLES = {"a", "an", "the"}


def key_of(phrase: str) -> str:
    words = [w for w in re.findall(r"[a-z]+", phrase.lower())
             if w not in ARTICLES]
    return "_".join(words)


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
    line_start = text.rfind("\n", 0, at)
    if line_start >= 0 and line_start + 1 > start:
        start = line_start + 1
    tail = at + len(mark)
    ends = [e for e in (text.find(". ", tail), text.find(".\n", tail))
            if e >= 0]
    if not ends:
        sys.exit(f"the sentence at '{mark}' never ends")
    return text[start:min(ends) + 1]


def listed_after(sentence: str, colon_mark: str):
    """The comma-and-or list a sentence gives after a colon."""
    at = sentence.find(colon_mark)
    if at < 0:
        sys.exit(f"no list after '{colon_mark}'")
    rest = sentence[at + len(colon_mark):].rstrip(".")
    rest = rest.split(" to the character")[0]
    parts = re.split(r",\s*|\s+or\s+", rest)
    # "obligation, or acquaintance" splits at the comma and leaves the
    # "or" on the last item; the Oxford comma is the book's, not a key.
    return [re.sub(r"^or\s+", "", p.strip()) for p in parts if p.strip()]


def unsettled_of(text: str):
    if not re.search(r"^## Unsettled$", text, re.M):
        return []
    body = section_of(text, "Unsettled")
    return [f.group(0).strip()
            for f in re.finditer(r"^- .*?(?=\n- |\n\n|\Z)", body, re.M | re.S)]


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
            "source_section": "Unsettled",
            "source_kind": "list_item",
            "source_quote": question,
        })
    return len(questions)


def constant(ops, name, value, section, sentence):
    create(ops, "RuleConstant", f"@{name}", {
        "name": name,
        "constant_value": value,
        "source_section": section,
        "source_kind": "sentence",
        "source_quote": sentence,
    })


# ------------------------------------------------------ chapter one


def build_chapter_one(text: str, revision: str):
    ops = []
    body = section_of(text, "The kinds of moments")
    kinds = [(f.group(1), f.group(0).strip()) for f in re.finditer(
        r"^\*\*([A-Z][a-z]+)\.\*\*.*?(?=\n\n|\Z)", body, re.M | re.S)]
    if not kinds:
        sys.exit("the kinds section defines no kinds")
    for name, quote in kinds:
        create(ops, "MomentKind", f"@kind_{name.lower()}", {
            "name": name,
            "moment_kind_key": name.lower(),
            "source_section": "The kinds of moments",
            "source_kind": "paragraph",
            "source_quote": quote,
        })
    sentence = sentence_containing(section_of(text, "Seasons"),
                                   "A season is spent one of three ways")
    modes = re.findall(r"\bin\s+([a-z]+),", sentence)
    if not modes:
        sys.exit("the season sentence names no ways to spend one")
    for mode in modes:
        create(ops, "SeasonMode", f"@season_{mode}", {
            "name": mode,
            "season_mode_key": mode,
            "source_section": "Seasons",
            "source_kind": "sentence",
            "source_quote": sentence,
        })
    questions = unsettled_ops(ops, text, CHAPTER_ONE)
    return envelope(CHAPTER_ONE, revision, ops, {
        "MomentKind": len(kinds),
        "SeasonMode": len(modes),
        "UnsettledQuestion": questions,
    })


# ------------------------------------------------------ chapter two


def build_chapter_two(text: str, revision: str):
    ops = []
    constant(ops, "season_standard_years", "1", "What a season costs",
             sentence_containing(section_of(text, "What a season costs"),
                                 "ages a character by 1 year"))
    bounds = sentence_containing(section_of(text, "What a moment risks"),
                                 "keep it between 0.05 and 0.95")
    constant(ops, "moment_chance_floor", "0.05", "What a moment risks",
             bounds)
    constant(ops, "moment_chance_ceiling", "0.95", "What a moment risks",
             bounds)
    questions = unsettled_ops(ops, text, CHAPTER_TWO)
    return envelope(CHAPTER_TWO, revision, ops, {
        "RuleConstant": 3,
        "UnsettledQuestion": questions,
    })


# ---------------------------------------------------- chapter three


def build_chapter_three(text: str, revision: str):
    ops = []
    standings_sentence = sentence_containing(
        section_of(text, "Standings"), "A standing is what someone")
    standings = listed_after(standings_sentence, "character: ")
    for standing in standings:
        create(ops, "StandingKind", f"@standing_{key_of(standing)}", {
            "name": standing,
            "standing_key": key_of(standing),
            "source_section": "Standings",
            "source_kind": "sentence",
            "source_quote": standings_sentence,
        })

    may = section_of(text, "What a moment may do")
    effects = []
    for found in re.finditer(r"^It may ([^,:]+?)(?:[,:]| by ).*$", may, re.M):
        effects.append((key_of(found.group(1)), found.group(0).strip()))
    if not effects:
        sys.exit("the chapter allows a moment to do nothing at all")
    for key, quote in effects:
        create(ops, "EffectKind", f"@effect_{key}", {
            "name": key.replace("_", " "),
            "effect_key": key,
            # What the rules must know about each kind, read off its
            # own sentence so the C++ spells no effect.
            "effect_turns_life": "true" if "turn the life" in quote
            else "false",
            "effect_moves_characteristic": "true"
            if "move a characteristic" in quote else "false",
            "source_section": "What a moment may do",
            "source_kind": "sentence",
            "source_quote": quote,
        })
    turn_sentence = sentence_containing(may, "a turn is one of these")
    turns = listed_after(turn_sentence, "one of these: ")
    combine = section_of(text, "How effects combine")
    ends_sentence = sentence_containing(combine, "ends the season with it")
    for turn in turns:
        # Which turns end the season is read off the sentence that
        # says so: "A turn that ends the career or the life ends the
        # season with it". A turn phrase whose noun that sentence
        # names as ending ("career", "life") carries the flag.
        noun = [w for w in re.findall(r"[a-z]+", turn.lower())
                if w not in ARTICLES]
        ends_season = ("ends" in noun and any(
            f"the {n}" in ends_sentence for n in noun if n != "ends"))
        create(ops, "Turn", f"@turn_{key_of(turn)}", {
            "name": turn,
            "turn_key": key_of(turn),
            "turn_ends_season": "true" if ends_season else "false",
            "turn_ends_life": "true" if ends_season and "life" in noun
            else "false",
            "source_section": "What a moment may do",
            "source_kind": "sentence",
            "source_quote": turn_sentence,
        })
    constant(ops, "characteristic_move_limit", "1", "What a moment may do",
             sentence_containing(may, "by 1 and never more"))

    hits = section_of(text, "How hard a moment hits")
    weight_sentence = sentence_containing(hits, "a weight is one of these")
    weights = listed_after(weight_sentence, "one of these: ")
    numerals = {"one": "1", "two": "2", "three": "3", "four": "4",
                "five": "5", "six": "6"}
    for weight in weights:
        # What the rung permits, from its own sentence: "At a mark, it
        # may do one thing ... and may not turn the life." The number
        # is the book's word for it; "only leave its record" is a
        # count of none, carried by implied_by; "as it likes" is no
        # cap at all.
        rung = sentence_containing(hits, f"At {weight},")
        properties = {
            "name": weight,
            "weight_key": key_of(weight),
            "weight_may_turn": "true" if "may turn the life" in rung
            else "false",
            "weight_unbounded": "true" if "as it likes" in rung else "false",
            "source_section": "How hard a moment hits",
            "source_kind": "sentence",
            "source_quote": rung,
        }
        if "only leave its record" in rung:
            properties["weight_effect_limit"] = "0"
            properties["implied_by"] = "only leave its record"
        elif "as it likes" not in rung:
            count = re.search(r"\b(" + "|".join(numerals) + r")\b", rung)
            if not count:
                sys.exit(f"the rung '{weight}' states no count")
            properties["weight_effect_limit"] = numerals[count.group(1)]
        create(ops, "Weight", f"@weight_{key_of(weight)}", properties)

    questions = unsettled_ops(ops, text, CHAPTER_THREE)
    return envelope(CHAPTER_THREE, revision, ops, {
        "StandingKind": len(standings),
        "EffectKind": len(effects),
        "Turn": len(turns),
        "Weight": len(weights),
        "RuleConstant": 1,
        "UnsettledQuestion": questions,
    })


# ----------------------------------------------------- chapter four


def build_chapter_four(text: str, revision: str):
    ops = []
    arrival = sentence_containing(section_of(text, "How events arrive"),
                                  "never below 0.01 and never above 0.9")
    constant(ops, "arrival_chance_floor", "0.01", "How events arrive",
             arrival)
    constant(ops, "arrival_chance_ceiling", "0.9", "How events arrive",
             arrival)

    doors_body = section_of(text, "The doors")
    doors = []
    for found in re.finditer(r"The ([a-z]+) door [^.]*\.", doors_body):
        doors.append((found.group(1), found.group(0)))
    if not doors:
        sys.exit("the doors section names no doors")
    for key, quote in doors:
        create(ops, "Door", f"@door_{key}", {
            "name": key,
            "door_key": key,
            # The book says which door is the player's to write; the
            # C++ reads this flag and never spells a door.
            "door_is_players": "true" if "is the player's" in quote
            else "false",
            "source_section": "The doors",
            "source_kind": "sentence",
            "source_quote": quote,
        })

    examples_body = section_of(text, "The kinds, by example")
    examples = 0
    door_examples = 0
    for found in re.finditer(r"^For ([A-Z][a-z]+), (.*)$", examples_body,
                             re.M):
        kind = found.group(1).lower()
        for part in re.finditer(r"[Aa]t (?:a )?([a-z]+): ([^.]*\.)",
                                found.group(0)):
            examples += 1
            create(ops, "PlaybookExample",
                   f"@example_{kind}_{part.group(1)}", {
                       "name": f"{kind} at {part.group(1)}",
                       # By KEY, not by entity: the kinds live in
                       # another chapter's seed, and a key the enum
                       # closes over is as checkable as a reference.
                       "moment_kind_key": kind,
                       "weight_key": part.group(1),
                       "source_section": "The kinds, by example",
                       "source_kind": "sentence",
                       "source_quote": part.group(0),
                   })
    for found in re.finditer(r"^Its doors, [^:]+: (.*)$", examples_body,
                             re.M):
        for part in re.finditer(r"([A-Za-z]+), ([^.]*\.)", found.group(1)):
            key = part.group(1).lower()
            if key not in {d for d, _ in doors}:
                continue
            door_examples += 1
            create(ops, "PlaybookDoorExample", f"@door_example_{key}", {
                "name": f"violence door {key}",
                "moment_kind_key": "violence",
                "door_key": key,
                "source_section": "The kinds, by example",
                "source_kind": "sentence",
                "source_quote": part.group(0),
            })
    questions = unsettled_ops(ops, text, CHAPTER_FOUR)
    return envelope(CHAPTER_FOUR, revision, ops, {
        "RuleConstant": 2,
        "Door": len(doors),
        "PlaybookExample": examples,
        "PlaybookDoorExample": door_examples,
        "UnsettledQuestion": questions,
    })


# --------------------------------------------------- the procedure


def build_life_procedure(text: str, revision: str):
    """One season: spend it, learn what lands, face it, and loop.

    Step ORDER and the routes are composition, and live here in the
    game's own tooling; each step and each route still cites the
    sentence that makes it a rule. Emitted by the extractor because
    every seed of the book must carry the book's one revision.
    """
    ops = []
    ending = sentence_containing(section_of(text, "When the making ends"),
                                 "The making of a character ends")
    arrival = sentence_containing(section_of(text, "How events arrive"),
                                  "the director states the chance")
    doors = sentence_containing(section_of(text, "The doors"),
                                "Almost every event offers four ways")
    create(ops, "Procedure", "@voyager_life", {
        "name": "voyager_life",
        "source_section": "How events arrive",
        "source_kind": "heading",
        "source_quote": "How events arrive",
    })
    steps = [
        ("@spend_season", "spend_season", "When the making ends", ending),
        ("@propose_arrival", "propose_arrival", "How events arrive",
         arrival),
        ("@face_moment", "face_moment", "The doors", doors),
        ("@end_making", "end_making", "When the making ends", ending),
    ]
    for index, (alias, name, section, quote) in enumerate(steps):
        create(ops, "ProcedureStep", alias, {
            "name": name,
            "step_index": str(index),
            "primitive_ref": name,
            "source_section": section,
            "source_kind": "sentence",
            "source_quote": quote,
        })
        ops.append({"op": "set_relation", "from": "@voyager_life",
                    "relation": "HAS_PART", "to": alias})
    routes = [
        ("@spend_season", "enough", "@end_making", "When the making ends",
         ending),
        ("@propose_arrival", "unbroken", "@spend_season",
         "How events arrive", arrival),
        ("@face_moment", "continue", "@spend_season", "The doors", doors),
        ("@face_moment", "ended", "@end_making", "When the making ends",
         ending),
    ]
    for step, label, target, section, quote in routes:
        alias = f"@route_{step[1:]}_{label}"
        create(ops, "StepRoute", alias, {
            "name": f"{step[1:]} {label}",
            "route_label": label,
            "next_step": target,
            "source_section": section,
            "source_kind": "sentence",
            "source_quote": quote,
        })
        ops.append({"op": "set_relation", "from": step,
                    "relation": "HAS_PART", "to": alias})
    return envelope(CHAPTER_FOUR, revision, ops, {
        "Procedure": 1,
        "ProcedureStep": len(steps),
        "StepRoute": len(routes),
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

    texts = {c: read_chapter(corpus_root, c) for c in CHAPTERS}
    seeds = {
        "voyager_book_rules.json":
            build_chapter_one(texts[CHAPTER_ONE], revision),
        "voyager_book_play_rules.json":
            build_chapter_two(texts[CHAPTER_TWO], revision),
        "voyager_book_carry_rules.json":
            build_chapter_three(texts[CHAPTER_THREE], revision),
        "voyager_book_playbook.json":
            build_chapter_four(texts[CHAPTER_FOUR], revision),
        "voyager_life_procedure.json":
            build_life_procedure(texts[CHAPTER_FOUR], revision),
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
