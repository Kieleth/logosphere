#!/usr/bin/env python3
"""Read the chapter, write the seed. Voyager's own extraction, and the
only thing that may write examples/voyager/seeds/voyager_cepheus_rules.json.

WHAT COMES OUT, and nothing else comes out. This slice ends at career
selection, so it needs exactly five kinds of thing and it takes exactly
five:

  1. the dice a characteristic throw is rolled with (2D6);
  2. the six characteristics character creation rolls, in the order the
     book prints them, each with the short name the book prints beside
     it;
  3. the Characteristic Modifier by Score Range table, because the
     ontology refuses a throw that names a characteristic and no lookup
     to turn a score into a modifier, and because stating the odds is
     the point;
  4. the age of majority, which is the one number this slice fixes;
  5. the twenty-four careers, each with its qualification throw, its
     survival throw, and the sentence the book uses to say what it is.

Nothing else. No training tables, no benefits, no ranks, no mishaps.
Those are real rules and they are not in this slice, so absorbing them
would put content in the graph that nothing reads, which is the defect
this module already has a name for.

WHAT THIS TOOL DECIDES, said out loud. Extraction judges STRUCTURE and
copies CONTENT: every quote below is a byte slice of the chapter, never
retyped. Three structural judgements are this tool's and are worth
arguing with:

  * WHICH SIX. The book prints SEVEN characteristics in one numbered
    list and then says the seventh "cannot be rolled or bought during
    character creation without the Referee's permission". So the set is
    read from the sentence that says what to roll — "Roll your six
    characteristics using 2D6, and record them in the standard order:
    ..." — and each one's position and short name come from the
    numbered list. Psionic Strength is in the list and not in that
    sentence, so it is not seeded. The C++ knows neither six nor seven.

  * WHICH NAME. A career is named by its COLUMN in the career tables
    ("Aerospace Defense"), because that is the heading the throws are
    printed under and a throw must cite the cell it came from. The
    Career Descriptions section writes some of them longer ("Aerospace
    System Defense"); that longer form is recorded in source_aliases,
    which is what that slot is for.

  * WHICH SLOT. attribute_ref must name a declared slot on Character.
    The mapping from the book's word to the slot name is lower case
    with spaces as underscores, applied once, here, and never in C++:
    every runtime reader takes the slot name off the Characteristic
    entity in the graph.

Usage:
    python3 extract_chargen.py --corpus <corpora/cepheus-srd> \
                              --out <examples/voyager/seeds/...json>

The corpus root is passed in and never derived. A corpus is bytes as
published, vendored once outside every game; deriving its path from
this file's location is how a book comes to live inside one game's
tree.
"""

import argparse
import json
import re
import sys
from pathlib import Path

CHAPTER = "book1/character-creation.md"
LAYER = "cepheus"

# The one sentence that says what character creation rolls. Read for
# membership and order; each characteristic's index and short name come
# from the numbered list under "The Explanation".
ROLL_SENTENCE_MARK = "Roll your six characteristics using 2D6, and record them"

# The sentence that says what a characteristic throw is rolled with.
DICE_SENTENCE_MARK = "Most of these throws are characteristic throws"

# The sentence that fixes the starting age.
MAJORITY_MARK = "All characters begin at the age of majority"


# ---------------------------------------------------------------- text


def read_chapter(corpus_root: Path) -> str:
    path = corpus_root / CHAPTER
    if not path.is_file():
        sys.exit(f"no chapter at {path}: is --corpus the corpus root?")
    return path.read_text(encoding="utf-8")


def sentences_of(text: str):
    """Every sentence-ish run of the chapter, for exact-quote lookup.

    Deliberately crude and deliberately non-normalising: the point is to
    hand back BYTES that occur in the file, so a quote built from one
    string-matches at verification time. The engine's own markdown
    parser decides what a sentence is when it resolves the citation;
    this only has to find the same span.
    """
    for raw in text.split("\n"):
        line = raw.strip()
        if not line or line.startswith("|") or line.startswith("#"):
            continue
        line = re.sub(r"^\s*[-*]\s+", "", line)
        line = re.sub(r"^\s*\d+\.\s+", "", line)
        for part in re.split(r"(?<=[.!?])\s+(?=[A-Z(])", line):
            part = part.strip()
            if part:
                yield part


def sentence_containing(text: str, mark: str) -> str:
    for sentence in sentences_of(text):
        if mark in sentence:
            return sentence
    sys.exit(f"the chapter has no sentence containing {mark!r}")


def section_of(text: str, heading_suffix: str) -> str:
    """The body of the section whose heading ends with heading_suffix."""
    lines = text.split("\n")
    start = None
    level = 0
    for i, line in enumerate(lines):
        m = re.match(r"^(#+)\s+(.*)$", line)
        if not m:
            continue
        if start is None:
            if m.group(2).strip() == heading_suffix:
                start = i + 1
                level = len(m.group(1))
            continue
        if len(m.group(1)) <= level:
            return "\n".join(lines[start:i])
    if start is None:
        sys.exit(f"the chapter has no section {heading_suffix!r}")
    return "\n".join(lines[start:])


def markdown_tables(body: str):
    """Every pipe table in a section body, as (label, columns, rows)."""
    tables = []
    rows = []
    for raw in body.split("\n"):
        line = raw.strip()
        if line.startswith("|") and line.endswith("|"):
            cells = [c.strip() for c in line[1:-1].split("|")]
            if all(set(c) <= set("-: ") and c for c in cells):
                continue          # the alignment row
            rows.append(cells)
            continue
        if rows:
            tables.append((rows[0][0], rows[0], rows[1:]))
            rows = []
    if rows:
        tables.append((rows[0][0], rows[0], rows[1:]))
    return tables


# ------------------------------------------------------------- pieces


def slot_name(book_name: str) -> str:
    return book_name.strip().lower().replace(" ", "_")


def characteristics(text: str):
    """The six that character creation rolls, in the book's order.

    Membership from the sentence that says what to roll; position and
    short name from the numbered list that prints the profile.
    """
    explanation = section_of(text, "The Explanation")
    printed = []
    for raw in explanation.split("\n"):
        m = re.match(r"^\s*(\d+)\.\s+\*\*(.+?)\*\*\s+\((\w+)\)\s*$",
                     raw.rstrip())
        if m:
            printed.append({
                "index": int(m.group(1)),
                "name": m.group(2),
                "abbreviation": m.group(3),
                "quote": raw.strip(),
            })
    if not printed:
        sys.exit("the profile list under 'The Explanation' did not parse")

    rolled_sentence = sentence_containing(text, ROLL_SENTENCE_MARK)
    out = []
    for entry in printed:
        if f"{entry['name']} ({entry['abbreviation']})" not in rolled_sentence:
            continue
        out.append(entry)
    if not out:
        sys.exit("no characteristic in the profile list is named by the "
                 "sentence that says what to roll")
    return out, printed


def modifier_rows(text: str):
    body = section_of(text, "Characteristic Modifiers")
    for label, columns, rows in markdown_tables(body):
        if label != "Score Range":
            continue
        column = "Characteristic Modifier"
        at = columns.index(column)
        out = []
        for row in rows:
            key = row[0]
            cell = row[at]
            m = re.match(r"^(\d+)\s+through\s+(\d+)$", key)
            if m:
                out.append((key, int(m.group(1)), int(m.group(2)), False,
                            cell))
                continue
            m = re.match(r"^(\d+)\s+or\s+higher$", key)
            if m:
                out.append((key, int(m.group(1)), None, True, cell))
                continue
            sys.exit(f"unreadable score range {key!r}")
        return out, column
    sys.exit("no 'Score Range' table under 'Characteristic Modifiers'")


def career_descriptions(text: str):
    """Career name -> the book's own sentence about it."""
    body = section_of(text, "Career Descriptions")
    out = {}
    for raw in body.split("\n"):
        line = raw.strip()
        m = re.match(r"^\*\*(.+?):\*\*\s+(.+)$", line)
        if m:
            out[m.group(1).strip()] = line
    if not out:
        sys.exit("no career descriptions parsed")
    return out


def career_blocks(text: str):
    """Every career column of the career tables, with its two throws."""
    body = section_of(text, "Career Tables")
    blocks = []
    for label, columns, rows in markdown_tables(body):
        if label != "Career":
            continue
        by_key = {row[0]: row for row in rows}
        if "Qualifications" not in by_key or "Survival" not in by_key:
            continue
        for at in range(1, len(columns)):
            blocks.append({
                "name": columns[at],
                "qualification": by_key["Qualifications"][at],
                "survival": by_key["Survival"][at],
            })
    if not blocks:
        sys.exit("no career columns parsed from 'Career Tables'")
    return blocks


def throw(cell: str):
    """'End 8+' -> ('End', 8). A cell that is not a throw is fatal."""
    m = re.match(r"^([A-Za-z]+)\s+(\d+)\+$", cell.strip())
    if not m:
        sys.exit(f"unreadable throw cell {cell!r}")
    return m.group(1), int(m.group(2))


def alias_for(name: str) -> str:
    return "@" + re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_")


# --------------------------------------------------------------- seed


def build(text: str, commit: str):
    ops = []

    def create(entity_type, alias, properties):
        ops.append({"op": "create_entity", "type": entity_type,
                    "as": alias, "properties": properties})

    def part(whole, piece):
        ops.append({"op": "set_relation", "from": whole,
                    "relation": "HAS_PART", "to": piece})

    # 1. the dice every characteristic throw is rolled with
    create("DiceExpression", "@d2d6", {
        "name": "2D6",
        "dice_count": "2",
        "dice_sides": "6",
        "source_section": "Careers",
        "source_kind": "sentence",
        "source_quote": sentence_containing(text, DICE_SENTENCE_MARK),
    })

    # 2. the six characteristics character creation rolls
    rolled, _printed = characteristics(text)
    for entry in rolled:
        create("Characteristic", alias_for("char " + entry["name"]), {
            "name": entry["name"],
            "attribute_ref": slot_name(entry["name"]),
            "characteristic_abbreviation": entry["abbreviation"],
            "characteristic_index": str(entry["index"]),
            "characteristic_dice": "@d2d6",
            "source_section": "The Explanation",
            "source_kind": "list_item",
            "source_quote": entry["quote"],
        })

    # 3. score -> modifier, which is what lets a throw state its odds
    create("LookupTable", "@characteristic_modifiers", {
        "name": "characteristic_modifiers",
        "entry_type": "CharacteristicModifierEntry",
        "source_section": "Characteristic Modifiers",
        "source_kind": "table",
        "source_table": "Score Range",
        "source_quote": "| Score Range | PseudoHex | Characteristic Modifier |",
    })
    rows, column = modifier_rows(text)
    for key, low, high, unbounded, cell in rows:
        alias = alias_for("dm " + key)
        properties = {
            "name": "characteristic modifier, score " + key,
            "key_min": str(low),
            "characteristic_modifier": cell.replace("\\", ""),
            "source_section": "Characteristic Modifiers",
            "source_kind": "cell",
            "source_table": "Score Range",
            "source_row": key,
            "source_column": column,
            "source_quote": cell,
        }
        if unbounded:
            properties["key_max_unbounded"] = True
        else:
            properties["key_max"] = str(high)
        create("CharacteristicModifierEntry", alias, properties)
        part("@characteristic_modifiers", alias)

    # 4. the one number this slice fixes
    majority = sentence_containing(text, MAJORITY_MARK)
    create("RuleConstant", "@age_of_majority", {
        "name": "age_of_majority",
        "constant_value": "18",
        "source_section": "Chapter 1: Character Creation",
        "source_kind": "sentence",
        "source_quote": majority,
    })

    # 5. the careers, each with the two throws this slice shows
    descriptions = career_descriptions(text)
    by_short = {}
    for long_name, sentence in descriptions.items():
        by_short.setdefault(long_name, (long_name, sentence))
        short = long_name.replace(" System ", " ")
        by_short.setdefault(short, (long_name, sentence))

    careers = 0
    for block in career_blocks(text):
        name = block["name"]
        if name not in by_short:
            sys.exit(f"career column {name!r} has no description in "
                     f"'Career Descriptions'")
        long_name, sentence = by_short[name]
        career_alias = alias_for("career " + name)
        for role, cell, slot in (
                ("Qualifications", block["qualification"],
                 "qualification_check"),
                ("Survival", block["survival"], "survival_check")):
            short, target = throw(cell)
            attribute = None
            for entry in rolled:
                if entry["abbreviation"] == short:
                    attribute = slot_name(entry["name"])
            if attribute is None:
                sys.exit(f"throw {cell!r} names {short!r}, which is not one "
                         f"of the characteristics this chapter rolls")
            check_alias = alias_for(f"{name} {role}")
            create("TaskCheck", check_alias, {
                "name": f"{name} {role.lower()}",
                "attribute_ref": attribute,
                "target_number": str(target),
                "dice": "@d2d6",
                "modifier_table": "@characteristic_modifiers",
                "modifier_property": "characteristic_modifier",
                "source_section": "Career Tables",
                "source_kind": "cell",
                "source_table": "Career",
                "source_row": role,
                "source_column": name,
                "source_quote": cell,
            })
        properties = {
            "name": name,
            "career_summary": sentence,
            "qualification_check": alias_for(f"{name} Qualifications"),
            "survival_check": alias_for(f"{name} Survival"),
            "source_section": "Career Descriptions",
            "source_kind": "sentence",
            "source_quote": sentence,
        }
        if long_name != name:
            properties["source_aliases"] = long_name
        create("Career", career_alias, properties)
        careers += 1

    invariants = {
        "count_of_type": {
            "DiceExpression": 1,
            "Characteristic": len(rolled),
            "LookupTable": 1,
            "CharacteristicModifierEntry": len(rows),
            "RuleConstant": 1,
            "Career": careers,
            "TaskCheck": careers * 2,
        },
        "unique_name_per_type": [
            "Characteristic", "Career", "TaskCheck",
            "CharacteristicModifierEntry",
        ],
        "band_coverage": {"@characteristic_modifiers": [0, None]},
    }
    return {
        "source": {"file": CHAPTER, "commit": commit},
        "layer": LAYER,
        "generated_by": "examples/voyager/tools/extract_chargen.py",
        "invariants": invariants,
        "ops": ops,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus", required=True,
                        help="root of the vendored corpus (declared, "
                             "never derived from this file's location)")
    parser.add_argument("--out", required=True, help="seed file to write")
    args = parser.parse_args()

    corpus_root = Path(args.corpus).resolve()
    text = read_chapter(corpus_root)
    commit_file = corpus_root / "SOURCE_COMMIT"
    if not commit_file.is_file():
        sys.exit(f"no SOURCE_COMMIT under {corpus_root}: the seed cannot "
                 f"pin the bytes it was read from")
    commit = commit_file.read_text(encoding="utf-8").strip()

    seed = build(text, commit)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(seed, indent=2, ensure_ascii=False) + "\n",
                   encoding="utf-8")
    counts = seed["invariants"]["count_of_type"]
    print(f"{out}: {len(seed['ops'])} ops, " +
          ", ".join(f"{k} {v}" for k, v in sorted(counts.items())))


if __name__ == "__main__":
    main()
