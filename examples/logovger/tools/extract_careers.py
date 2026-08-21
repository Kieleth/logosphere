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

The remaining RuleConstants are numbers buried in prose that a
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

from extract_career_tables import (ATTRIBUTES, CHAPTER, CareerTableLedger,
                                   LEDGER_ENTITY_TYPES, cells, is_separator,
                                   load_skill_references, qualified_ref,
                                   read_tables, slug,
                                   validate_character_creation)
from rule_source_identity import ingestion_edition_context_key

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

# "For your first career only, you get all the skills listed in the
# Service Skills table at Level 0 as your basic training. For any
# subsequent careers, you may pick any one skill listed in the Service
# Skills table at Level 0 as your basic training."
#
# One EnsureSkillLevel per skill, gathered into a sequence the career
# points at. A first career applies the sequence; a later one applies
# the single step the player picks. The level lives on the outcome
# rather than in a constant the procedure reads, because it is what the
# book DOES to a character, not a threshold the procedure tests.
BASIC_TRAINING_SECTION = "Basic Training"
BASIC_TRAINING_QUOTE = (
    "On the first term of a new career, you gain Basic Training as you "
    "learn the basics for your chosen career. For your first career "
    "only, you get all the skills listed in the Service Skills table at "
    "Level 0 as your basic training. For any subsequent careers, you "
    "may pick any one skill listed in the Service Skills table at Level "
    "0 as your basic training.")

DRAFT_SECTION = "Qualifying and the Draft"
DRAFT_TABLE = "Roll"
DRAFT_COLUMN = "Draft Career"
# The Draft names four of its six services differently from the career
# tables: "Aerospace System Defense (Planetary Air Force)" is the
# career the block header calls "Aerospace Defense". The cell is
# transcribed verbatim and the career it means is declared here,
# because deciding that those two strings are one service is a reading
# and not a transcription. A cell that resolves to no career fails the
# run rather than being guessed at.
DRAFT_CAREERS = {
    "Aerospace System Defense (Planetary Air Force)": "Aerospace Defense",
    "Marine": "Marine",
    "Maritime System Defense (Planetary Navy)": "Maritime Defense",
    "Navy": "Navy",
    "Scout": "Scout",
    "Surface System Defense (Planetary Army)": "Surface Defense",
}

# "Characters who end their careers receive one benefit per term served
# in which they did not lose benefits. An additional benefit is gained
# if the character held rank O4, and two for rank O5. A character with
# rank O6 gains three extra benefits."
#
# Only the rank half is absorbed here. The per-term rate, the count of
# eligible terms and the sum of the two need a rule language that today
# ships a type system and no operators, so they stay in the procedure
# and the muster step says so in `unmodelled`.
#
# The O4 row states its count with the indefinite article and no
# number, so it carries implied_by and is proved by those words. The
# other two write "two" and "three", which the verifier reads.
RANK_BONUS_SECTION = "Mustering Out Benefits"
RANK_BONUS_QUOTE = (
    "An additional benefit is gained if the character held rank O4, and "
    "two for rank O5. A character with rank O6 gains three extra "
    "benefits.")
RANK_BONUS_ROWS = [
    (4, 1, "An additional benefit is gained"),
    (5, 2, None),
    (6, 3, None),
]
RANK_BONUS_UNMODELLED = (
    "Only the rank half of this sentence is in the graph. The base "
    "count - one benefit per term served in which benefits were not "
    "lost - is a rate over a filtered count, which needs arithmetic "
    "and a count the rule language does not yet have, so the procedure "
    "still does it.")

DM_SECTION = "Characteristic Modifiers"
DM_TABLE = "Score Range"
DM_COLUMN = "Characteristic Modifier"

# The numbers this seed owns from prose, each with the
# sentence that proves it. The verifier checks every one against its
# quote, so a wrong number or a wrong sentence fails the build rather
# than shipping. implied_by is for a count the text states without
# writing; none of these need it yet.
#
# Fields: name, value, section, the sentence that proves it, and what
# the graph does NOT do with it. That fifth field is the `unmodelled`
# slot, and it is None for every constant a rule actually reads. It is
# not a comment: a number the book prints that nothing applies looks
# exactly like a number something applies, and the difference has to be
# in the graph or it is nowhere at all. prior_career_dm sat here cited,
# typed, and unread, and no check could see the difference.
RULE_CONSTANTS = [
    ("prior_career_dm", "-2", "Qualifying and the Draft",
     "You suffer a DM–2 to qualification rolls for each previous "
     "career you have entered.",
     "The modifier is typed and nothing applies it. A qualification "
     "throw is modified only by what its own TaskCheck declares, and "
     "the rule language cannot state a modifier that comes from the "
     "character's history rather than from the throw. Applying it "
     "needs a mechanism that does not exist yet; inventing one here "
     "would be a rule this book did not print."),
    ("max_terms", "7", "Reenlistment and Retirement",
     "The Referee may want to change the maximum number of terms spent "
     "in character creation from 7 to something else.", None),
    ("reenlistment_forced_natural", "12", "Reenlistment and Retirement",
     "If the character rolls a natural 12, they cannot leave their "
     "current career and must continue for another term.", None),
    ("cash_benefit_roll_max", "3", "Cash Benefits",
     "Up to 3 benefit rolls can be taken on the Cash table.", None),
    ("survival_natural_failure", "2", "Survival",
     "A natural 2 is always a failure.", None),
]

PROSPECTING_LOCATOR = (
    "Service Skills", "5", "Belter", "Prospecting")
PROSPECTING_DEFECT = (
    "Granted by the Belter Service Skills and Belter Specialist tables but "
    "defined nowhere: 'Prospecting' occurs exactly twice in the SRD, both "
    "in career tables, and has no entry in book1/skills.md nor a line in "
    "the Available Skills List.")
PROSPECTING_READING = (
    "No defined skill covers finding and assessing ore. The likely reading "
    "is that the skill entry is missing from the chapter, but the source "
    "does not prove a replacement.")


class Builder:
    """Ops in the order a loader can resolve them."""

    def __init__(self, context_key, skills):
        self.ops = []
        self.context_key = context_key
        self.skills = skills

    def add(self, type_name, alias, properties):
        self.ops.append({"op": "create_entity", "type": type_name,
                         "as": alias, "properties": properties})
        return alias

    def relate(self, parent, child):
        self.ops.append({"op": "set_relation", "from": parent,
                         "relation": "HAS_PART", "to": child})

    def dice(self, key):
        return qualified_ref(self.context_key, "DiceExpression", key)

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
    chapter = os.path.join(root, CHAPTER)
    try:
        source_bytes = validate_character_creation(chapter)
    except ValueError as error:
        print(f"REFUSED: {error}", file=sys.stderr)
        return 1
    commit = open(os.path.join(root, "SOURCE_COMMIT"),
                  encoding="utf-8").read().strip()
    context_key = ingestion_edition_context_key(root)
    skills = load_skill_references(vocabulary_path, context_key)
    skills["Prospecting"] = "@sk_prospecting"
    tables = read_tables(chapter)
    b = Builder(context_key, skills)
    b.add("Skill", "@sk_prospecting", {
        "name": "Prospecting",
        "source_defect": PROSPECTING_DEFECT,
        "suggested_reading": PROSPECTING_READING,
    })

    careers = {}
    counts = {"career": 0, "check": 0, "service_row": 0, "draft": 0, "dm": 0}

    # The service skills, read before anything is emitted: a Career
    # points at its basic training, so the training has to exist first,
    # and the training is made of the skills on a table that appears
    # further down the chapter than the career block does.
    service = {}
    block = []
    for table in tables:
        if table["title"] == "Career":
            block = list(table["columns"])
            continue
        if table["title"] != "Service Skills":
            continue
        for index, printed in enumerate(table["columns"]):
            career = block[index] if index < len(block) else printed
            service[career] = (printed,
                               [(r[0], r[index + 1]) for r in table["rows"]])

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
        # "33 or higher" has one number and an open top, and its alias
        # says so rather than reading like a single-value band.
        alias = "@dm_row_" + "_".join(numbers)
        if len(numbers) == 1:
            alias += "_plus"
        citation = b.cite(DM_SECTION, DM_TABLE, band, DM_COLUMN, modifier)
        # These rows carry no source_file: the whole seed is one
        # chapter and the envelope already says which.
        citation.pop("source_file", None)
        props = dict(name=alias.lstrip("@"), key_min=int(numbers[0]),
                     pseudohex_min=symbols[0],
                     characteristic_modifier=int(modifier.replace("\\", "")),
                     **citation)
        props["pseudohex_max"] = symbols[-1]
        if len(numbers) > 1:
            props["key_max"] = int(numbers[1])
        else:
            # "33 or higher" is open at the top in SCORE, but its
            # pseudohex is the single symbol Z: the alphabet stops
            # there, so the symbol band is closed even though the
            # scores it stands for are not.
            props["key_max_unbounded"] = True
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
            # Basic training, before the Career that points at it.
            if career not in service:
                print("REFUSED: %r has no Service Skills table" % career)
                return 1
            training = alias + "_basic"
            steps = []
            for key, skill_name in service[career][1]:
                if skill_name not in b.skills:
                    print("REFUSED: %s service %s reads %r, which resolves "
                          "to no skill" % (career, key, skill_name))
                    return 1
                grant = "%s_at0_%s" % (alias, key)
                b.add("EnsureSkillLevel", grant, dict(
                    name="%s: %s at level 0" % (career, skill_name),
                    skill=b.skills[skill_name], skill_level="0",
                    source_file=CHAPTER,
                    source_section=BASIC_TRAINING_SECTION,
                    source_kind="sentence",
                    source_quote=BASIC_TRAINING_QUOTE))
                steps.append(grant)
            b.add("OutcomeSequence", training, dict(
                name="%s basic training" % career, source_file=CHAPTER,
                source_section=BASIC_TRAINING_SECTION,
                source_kind="sentence", source_quote=BASIC_TRAINING_QUOTE))
            for order, grant in enumerate(steps):
                step = "%s_s%d" % (training, order)
                b.add("OutcomeStep", step, dict(
                    name=step.lstrip("@"), step_index=str(order),
                    outcome=grant, source_file=CHAPTER,
                    source_section=BASIC_TRAINING_SECTION,
                    source_kind="sentence",
                    source_quote=BASIC_TRAINING_QUOTE))
                b.relate(training, step)
            b.add("Career", alias, dict(
                name=career,
                qualification_check=checks["qual"],
                survival_check=checks["surv"],
                basic_training=training,
                **b.cite(SECTION, "Career", CAREER_CITATION_ROW, career,
                         rows[CAREER_CITATION_ROW][index + 1])))
            counts["career"] += 1

    if not careers:
        print("REFUSED: no career block headers found")
        return 1

    # ---- service skills, one table per career -----------------------
    # A sub-table abbreviates its columns: the block header writes
    # "Aerospace Defense" and the Service Skills table under it writes
    # "Aerospace". They are the same career, matched by POSITION in the
    # block, which is the only thing that actually relates them. The
    # cell citation still records the abbreviation the book printed.
    block = []
    for table in tables:
        if table["title"] == "Career":
            block = list(table["columns"])
            continue
        if table["title"] != "Service Skills":
            continue
        for index, printed in enumerate(table["columns"]):
            career = block[index] if index < len(block) else printed
            if career not in careers:
                print("REFUSED: Service Skills column %r sits at position "
                      "%d of a block naming %r, which is not a career"
                      % (printed, index, career))
                return 1
            alias = careers[career]
            svc = alias + "_svc"
            b.add("RollableTable", svc, dict(
                name="%s Service Skills" % career, dice=b.dice("d1d6"),
                source_file=CHAPTER, source_section=SERVICE_TABLE_SECTION,
                source_kind="sentence", source_quote=SERVICE_TABLE_QUOTE))
            for row in table["rows"]:
                key, value = row[0], row[index + 1]
                if value not in b.skills:
                    print("REFUSED: %s service %s reads %r, which resolves "
                          "to no skill" % (career, key, value))
                    return 1
                grant = "%s_g%s" % (alias, key)
                b.add("AdvanceSkill", grant, dict(
                    name="%s: gain %s" % (career, value),
                    skill=b.skills[value], initial_skill_level="1",
                    existing_skill_delta="1", source_file=CHAPTER,
                    source_section=SKILL_LEVEL_SECTION,
                    source_kind="sentence", source_quote=SKILL_LEVEL_QUOTE))
                entry = "%s_r%s" % (alias, key)
                b.add("TableEntry", entry, dict(
                    name="%s service %s" % (career, key),
                    roll_min=key, roll_max=key, outcome=grant,
                    **b.cite(SECTION, "Service Skills", key,
                             table["columns"][index], value)))
                b.relate(svc, entry)
                counts["service_row"] += 1
            # The career owns its service table. Basic training reaches
            # it this way, and without the relation the table sits in
            # the graph belonging to nobody.
            b.relate(alias, svc)

    # ---- the Draft --------------------------------------------------
    draft = [t for t in tables
             if t["title"] == DRAFT_TABLE and t["columns"] == [DRAFT_COLUMN]]
    if not draft:
        print("REFUSED: no Draft table in the chapter")
        return 1
    b.add("RollableTable", "@draft_table", dict(
        name="Draft Career", dice=b.dice("d1d6"), source_file=CHAPTER,
        source_section=DRAFT_SECTION, source_kind="table",
        source_table=DRAFT_TABLE,
        source_quote="| %s | %s |" % (DRAFT_TABLE, DRAFT_COLUMN)))
    for row in draft[0]["rows"]:
        key, cell = row[0], row[1]
        career = DRAFT_CAREERS.get(cell)
        if career is None or career not in careers:
            print("REFUSED: the Draft names %r, which resolves to no "
                  "career" % cell)
            return 1
        out = "@draft_out_%s" % key
        b.add("EnterCareer", out, dict(
            name="drafted into %s" % career, drafted_career=careers[career],
            **b.cite(DRAFT_SECTION, DRAFT_TABLE, key, DRAFT_COLUMN, cell)))
        entry = "@draft_row_%s" % key
        b.add("TableEntry", entry, dict(
            name="draft %s" % key, roll_min=key, roll_max=key, outcome=out,
            **b.cite(DRAFT_SECTION, DRAFT_TABLE, key, DRAFT_COLUMN, cell)))
        b.relate("@draft_table", entry)
        counts["draft"] += 1

    # ---- extra benefits by rank -------------------------------------
    b.add("LookupTable", "@rank_benefits", dict(
        name="extra_benefits_by_rank",
        entry_type="MusteringRankBonusEntry",
        miss_is_nothing=True,
        source_file=CHAPTER, source_section=RANK_BONUS_SECTION,
        source_kind="sentence", source_quote=RANK_BONUS_QUOTE,
        unmodelled=RANK_BONUS_UNMODELLED))
    for rank, extra, implied in RANK_BONUS_ROWS:
        alias = "@rank_bonus_o%d" % rank
        props = dict(name="extra_benefits_rank_%d" % rank,
                     key_min=rank, key_max=rank,
                     extra_benefit_rolls=extra,
                     source_file=CHAPTER,
                     source_section=RANK_BONUS_SECTION,
                     source_kind="sentence",
                     source_quote=RANK_BONUS_QUOTE)
        if implied:
            props["implied_by"] = implied
        b.add("MusteringRankBonusEntry", alias, props)
        b.relate("@rank_benefits", alias)

    # ---- the eight numbers a human read out of the prose ------------
    for name, value, section, quote, unmodelled in RULE_CONSTANTS:
        properties = dict(
            name=name, constant_value=value, source_file=CHAPTER,
            source_section=section, source_kind="sentence",
            source_quote=quote)
        if unmodelled:
            properties["unmodelled"] = unmodelled
        b.add("RuleConstant", "@" + name, properties)

    ledger = CareerTableLedger(source_bytes, tables)
    migrated = ledger.migrate(b.ops)
    ledger.append_claim(
        b.ops, PROSPECTING_LOCATOR,
        "The Belter tables grant a Prospecting skill that the skills "
        "chapter does not define.",
        "PARTIAL", materializes=("@sk_prospecting",),
        suffix="_prospecting_undefined_skill", gap_kind="SOURCE_GAP")

    print("careers:      %d" % counts["career"])
    print("throws:       %d" % counts["check"])
    print("service rows: %d" % counts["service_row"])
    print("draft rows:   %d" % counts["draft"])
    print("modifiers:    %d" % counts["dm"])
    print("constants:    %d" % len(RULE_CONSTANTS))
    print("exact Career Tables materializations: %d" % (migrated + 1))
    # Counts and coverage, asserted. A change that silently drops a
    # career or half a table still verifies - every citation left
    # behind is still true - and the invariant is what turns that
    # silence into a failure.
    counted = {}
    for op in b.ops:
        if op["op"] == "create_entity":
            counted[op["type"]] = counted.get(op["type"], 0) + 1
    bands = {alias + "_svc": [1, 6] for alias in careers.values()}
    bands["@dm_table"] = [0, None]
    bands["@draft_table"] = [1, 6]
    seed = {"source": {"file": CHAPTER, "commit": commit},
            "layer": "cepheus", "generated_by": GENERATED_BY,
            "invariants": {
                "count_of_type": counted,
                "unique_name_per_type": sorted(
                    (set(counted) - LEDGER_ENTITY_TYPES) |
                    {"DiceExpression"}),
                "band_coverage": bands,
            },
            "ops": b.ops}
    json.dump(seed, open(out_path, "w", encoding="utf-8"), indent=1)
    open(out_path, "a", encoding="utf-8").write("\n")
    print("total ops:    %d -> %s" % (len(b.ops), out_path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
