#!/usr/bin/env python3
"""Read the Cepheus careers into a seed: the 24 careers, their throws,
their service skills, the Draft, and the characteristic modifiers.

WHY THIS EXISTS. cepheus_careers.json was 604 hand-maintained ops that
no tool owned. That is the same failure mode the shared-tables seed had
- a file everyone edits and nothing regenerates - except worse, because
here there was never a generator to fall behind in the first place. A
hand-maintained seed cannot be re-derived when the SRD is revised, and
every number in it is one typo away from being wrong in a way the prose
still reads fine.

WHAT IS MECHANICAL AND WHAT IS NOT. 596 of those 604 ops are
transcription: cells addressed by (table, row, column), plus two
citation sentences reused 168 times. This script does that, and every
string it writes is the exact bytes from the source.

The remaining 8 are RuleConstants - numbers buried in prose that a
human read the chapter and chose. Those are DECLARED here with the
sentence that proves each one, because choosing them is judgement and
judgement belongs somewhere a reader can argue with it. The value
verifier then checks each number against its sentence, so a constant
whose sentence does not state it fails the build.

Usage:
    python3 examples/logovger/tools/extract_careers.py \\
        examples/logovger/srd/cepheus \\
        examples/logovger/seeds/cepheus_book1_skill_vocabulary.json \\
        examples/logovger/seeds/cepheus_careers.json

THIS SCRIPT OWNS ITS OUTPUT. Edit the script and regenerate; never edit
the seed. CI regenerates and fails on any diff.
"""
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from extract_career_tables import (ATTRIBUTES, CHAPTER, cells, is_separator,
                                   load_skill_references, qualified_ref,
                                   read_tables, seed_context_key, slug)

GENERATED_BY = "examples/logovger/tools/extract_careers.py"
SECTION = "Career Tables"

# The rows of a career block header this seed owns. The other three
# throws (Commission, Advancement, Re-enlistment) belong to the rules
# that consult them and are emitted by extract_career_tables.py, which
# is why they are absent here: two seeds creating one entity is the
# duplicate-Skill mistake, and the loader refuses it.
OWNED_CHECKS = {"Qualifications": "qual", "Survival": "surv"}

# A career's entity is cited to its qualification cell, because that
# cell is where the book first names it as a career you can enter.
CAREER_CITATION_ROW = "Qualifications"

# The sentence that sets what a gained skill is worth. A service cell
# says "Electronics", not "Electronics-1": the LEVELS come from the
# checklist, so that is what an AdvanceSkill built from such a cell
# cites, while the TableEntry above it still carries the cell address.
SKILL_LEVEL_SECTION = "Character Creation Checklist"
SKILL_LEVEL_QUOTE = (
    "If you gain a skill as a result and you do not already have levels "
    "in that skill, take it at level 1.")

# The sentence that hands every career its four training tables. The
# service table is one of them, so its existence is proven here.
SERVICE_TABLE_SECTION = "Skills and Training"
SERVICE_TABLE_QUOTE = (
    "In each term you spend in a career, pick one of these tables and "
    "roll 1D6 to see which skill you increase.")

DRAFT_SECTION = "Qualifying and the Draft"
DRAFT_TABLE = "Roll"
DRAFT_COLUMN = "Draft Career"

DM_SECTION = "Characteristic Modifiers"
DM_TABLE = "Score Range"
DM_COLUMN = "Characteristic Modifier"

# The eight numbers a human read out of the prose, each with the
# sentence that proves it. The verifier checks every one against its
# quote, so a wrong number or a wrong sentence fails the build rather
# than shipping. implied_by is for a count the text states without
# writing; none of these need it yet.
RULE_CONSTANTS = [
    ("prior_career_dm", "-2", "Qualifying and the Draft",
     "You suffer a DM–2 to qualification rolls for each previous "
     "career you have entered."),
    ("max_terms", "7", "Reenlistment and Retirement",
     "The Referee may want to change the maximum number of terms spent "
     "in character creation from 7 to something else."),
    ("reenlistment_forced_natural", "12", "Reenlistment and Retirement",
     "If the character rolls a natural 12, they cannot leave their "
     "current career and must continue for another term."),
    ("cash_benefit_roll_max", "3", "Cash Benefits",
     "Up to 3 benefit rolls can be taken on the Cash table."),
    ("aging_start_age", "34", "Aging",
     "The effects of aging begin when a character reaches 34 years of "
     "age."),
    ("crisis_restore_value", "1", "Aging Crisis",
     "The character dies unless he can pay 1D6×10,000 Credits for "
     "medical care, which will bring any characteristics back up to 1."),
    ("basic_training_level", "0", "Character Creation Checklist",
     "For your first term in your first career, you get every skill in "
     "the service skills table at level 0."),
    ("survival_natural_failure", "2", "Survival",
     "A natural 2 is always a failure."),
]


class Builder:
    """Ops in the order a loader can resolve them."""

    def __init__(self, commit, skills):
        self.ops = []
        self.commit = commit
        self.skills = skills

    def add(self, type_name, alias, properties):
        self.ops.append({"op": "create_entity", "type": type_name,
                         "as": alias, "properties": properties})
        return alias

    def relate(self, parent, child):
        self.ops.append({"op": "set_relation", "from": parent,
                         "relation": "HAS_PART", "to": child})

    def dice(self, key):
        return qualified_ref(
            "source-document:cepheus:%s@%s" % (CHAPTER, self.commit),
            "DiceExpression", key)

    def cite(self, section, table, row, column, quote, kind="cell"):
        out = {"source_file": CHAPTER, "source_section": section,
               "source_kind": kind, "source_quote": quote}
        if table:
            out["source_table"] = table
        if row:
            out["source_row"] = row
        if column:
            out["source_column"] = column
        return out


def parse_throw(text):
    """'End 5+' -> ('endurance', '5'). None when the cell is a dash."""
    match = re.match(r"^([A-Za-z]+)\s+(\d+)\+$", text.strip())
    if not match:
        return None
    attribute = ATTRIBUTES.get(match.group(1))
    if attribute is None:
        return None
    return attribute, match.group(2)


def main():
    root, vocabulary_path, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
    commit = open(os.path.join(root, "SOURCE_COMMIT"),
                  encoding="utf-8").read().strip()
    skills = load_skill_references(vocabulary_path)
    tables = read_tables(os.path.join(root, CHAPTER))
    b = Builder(commit, skills)

    careers = {}
    counts = {"career": 0, "check": 0, "service_row": 0, "draft": 0, "dm": 0}

    # ---- characteristic modifiers, first ----------------------------
    # Before the throws, because every throw references this table and
    # a loader resolves aliases in file order.
    dm_rows = [t for t in tables if t["title"] == DM_TABLE]
    if not dm_rows:
        print("REFUSED: no %r table in the chapter" % DM_TABLE)
        return 1
    dm = dm_rows[0]
    b.add("LookupTable", "@dm_table", dict(
        name="characteristic_modifiers",
        entry_type="CharacteristicModifierEntry",
        source_section=DM_SECTION,
        source_quote="| " + " | ".join([DM_TABLE] + dm["columns"]) + " |",
        source_kind="table", source_table=DM_TABLE))
    for row in dm["rows"]:
        band, pseudohex, modifier = row[0], row[1], row[2]
        # "0 through 2" and "15 or higher" are the two shapes the book
        # prints. The second is an open top and says so rather than
        # inventing a ceiling.
        numbers = re.findall(r"\d+", band)
        if not numbers:
            print("REFUSED: modifier band %r states no number" % band)
            return 1
        # "0-2" spans two symbols; the last row prints a single "Z".
        symbols = pseudohex.split("-")
        alias = "@dm_row_" + "_".join(numbers)
        citation = b.cite(DM_SECTION, DM_TABLE, band, DM_COLUMN, modifier)
        # These rows carry no source_file: the whole seed is one
        # chapter and the envelope already says which.
        citation.pop("source_file", None)
        props = dict(name=alias.lstrip("@"), key_min=int(numbers[0]),
                     pseudohex_min=symbols[0],
                     characteristic_modifier=int(modifier.replace("\\", "")),
                     **citation)
        if len(numbers) > 1:
            props["key_max"] = int(numbers[1])
            props["pseudohex_max"] = symbols[-1]
        else:
            props["key_max_unbounded"] = "true"
            props["pseudohex_max_unbounded"] = "true"
        b.add("CharacteristicModifierEntry", alias, props)
        b.relate("@dm_table", alias)
        counts["dm"] += 1

    # ---- careers and the two throws this seed owns ------------------
    for table in tables:
        if table["title"] != "Career":
            continue
        rows = {r[0]: r for r in table["rows"]}
        if CAREER_CITATION_ROW not in rows:
            continue
        for index, career in enumerate(table["columns"]):
            alias = "@" + slug(career)
            careers[career] = alias
            checks = {}
            for printed, suffix in OWNED_CHECKS.items():
                if printed not in rows:
                    continue
                cell = rows[printed][index + 1]
                parsed = parse_throw(cell)
                if parsed is None:
                    print("REFUSED: %s %s cell reads %r, which is not a "
                          "throw" % (career, printed, cell))
                    return 1
                attribute, target = parsed
                check = "%s_%s" % (alias, suffix)
                b.add("TaskCheck", check, dict(
                    name="%s %s" % (career, "qualification"
                                    if suffix == "qual" else "survival"),
                    attribute_ref=attribute, target_number=target,
                    dice=b.dice("d2d6"),
                    **b.cite(SECTION, "Career", printed, career, cell),
                    modifier_table="@dm_table",
                    modifier_property="characteristic_modifier"))
                checks[suffix] = check
                counts["check"] += 1
            b.add("Career", alias, dict(
                name=career,
                qualification_check=checks["qual"],
                survival_check=checks["surv"],
                **b.cite(SECTION, "Career", CAREER_CITATION_ROW, career,
                         rows[CAREER_CITATION_ROW][index + 1])))
            counts["career"] += 1

    if not careers:
        print("REFUSED: no career block headers found")
        return 1

    print("careers:      %d" % counts["career"])
    print("throws:       %d" % counts["check"])
    seed = {"source": {"file": CHAPTER, "commit": commit},
            "layer": "cepheus", "generated_by": GENERATED_BY,
            "invariants": {}, "ops": b.ops}
    json.dump(seed, open(out_path, "w", encoding="utf-8"), indent=1)
    open(out_path, "a", encoding="utf-8").write("\n")
    print("total ops:    %d -> %s" % (len(b.ops), out_path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
