#!/usr/bin/env python3
"""The tables that belong to no career: aging, mishaps, injuries.

Unlike the career tables, these are prose in cells rather than single
values, so classification by FORM does not reach them: "Reduce three
physical characteristics by 2, reduce one mental characteristic by 1"
is two clauses over two groups. Each row is transcribed verbatim and
mapped to outcomes by a table written HERE, in the open, where the
reading of each row can be checked against the cell it came from.

WHAT IS NOT EXPRESSED IS RECORDED. Two clauses in this chapter exceed
what the outcome vocabulary can say:

  * "reduce both OTHER physical characteristics by 2" depends on which
    one a previous clause reduced, and an outcome cannot refer to a
    choice made earlier in its own sequence.
  * "Alternatively, roll twice on the Injury table and take the lower
    result" is a re-roll policy, not an outcome.

Those rows carry `unmodelled` saying so. A partial rule that admits it
is honest; one that looks complete is not.

The mishap and injury tables already exist, partly: an earlier seed
created both with a handful of sample rows. That seed OWNS them, and a
seed may not add to what another owns, so completion happens inside
that file. This tool reads it, keeps every alias exactly as it is,
because other seeds reference them, and appends only what is missing.

Usage:
    python3 examples/logovger/tools/extract_shared_tables.py \\
        examples/logovger/srd/cepheus \\
        examples/logovger/seeds/cepheus_book1_shared_tables.json

THIS SCRIPT OWNS ITS OUTPUT. Edit the script and regenerate; never
edit the seed. CI regenerates all three seeds and fails on any diff,
which is the check that was missing when two PRs hand-edited this one.
"""
import json
import os
import re
import sys

CHAPTER = "book1/character-creation.md"


def qualified(alias, type_name, commit, file=CHAPTER):
    """A reference to an entity another seed owns.

    Names the origin document AND the commit, then the type and the
    alias the owning seed bound it to. Scoped by construction, so it
    cannot be answered by a same-named entity from somewhere else.
    """
    from urllib.parse import quote
    context = quote(f"source-document:cepheus:{file}@{commit}", safe="")
    return f"@@entity/{context}/{type_name}/{alias}"

# The book's own sentence about which abilities are which. Quoted, not
# assumed: a rule that reduces "three physical characteristics" has to
# know which three are eligible.
GROUPS = {
    "physical": ("strength; dexterity; endurance", "Characteristics"),
    "mental": ("intelligence; education; social_standing", "Characteristics"),
    # All six, from the same sentence that splits them in two. The
    # crisis rules ask about the whole set - "If any characteristic is
    # reduced to 0", "will bring any characteristics back up to 1" -
    # and without a group naming it, every one of those rules was
    # written out six times in C++, four separate times.
    "characteristic": (
        "strength; dexterity; endurance; intelligence; education; "
        "social_standing", "Characteristics"),
}
GROUP_QUOTE = (
    "Strength, Dexterity, and Endurance are called physical abilities, "
    "whereas Intelligence, Education, and Social Standing are loosely "
    "termed mental abilities.")

# Aging: 2D6 with the character's total terms as a negative DM. Bands
# are transcribed as printed. The asymmetry at the ends is the book's:
# the top says "1+" and the bottom says a plain "-6" with no "or less".
#
# DIVERGENCE, and the reason for it. The printed table has a hole. The
# DM is total terms, and a natural 12 on re-enlistment outranks the
# seven-term cap ("unless they roll a natural 12 during Reenlistment
# and must serve another term of service"), so two of those put a
# character at nine terms and 2D6-9 reaches -7. That total lands on no
# row and the run dies with a coverage error, which happens to about
# one in 1296 of the characters who reach term 7. The bottom row is
# therefore marked as catching everything under it. That is a ruling
# the book does not print, recorded here and in
# docs/VOYAGER_EXPANSION.md rather than clamped into the procedure.
AGING_FLOOR_BAND = "-6"
AGING_FLOOR_DEFECT = (
    "The Aging Table's bottom row is printed as a plain '-6' while its "
    "top row is printed '1+', so the table is open at one end and "
    "closed at the other. The DM is the character's total terms, and a "
    "natural 12 on Reenlistment overrides the seven-term cap, so a "
    "character can reach nine terms and roll 2D6-9 for a total of -7, "
    "which no row claims. Read as '-6 or less', which is what the open "
    "top row does at the other end.")

AGING_SECTION = "Aging"
AGING_TABLE = "2D6"
AGING_COLUMN = "Effects of Aging"
AGING_ROWS = {
    "-6": [("physical", 3, -2), ("mental", 1, -1)],
    "-5": [("physical", 3, -2)],
    "-4": [("physical", 2, -2), ("physical", 1, -1)],
    "-3": [("physical", 1, -2), ("physical", 2, -1)],
    "-2": [("physical", 3, -1)],
    "-1": [("physical", 2, -1)],
    "0":  [("physical", 1, -1)],
    "1+": [],
}

INJURY_SECTION = "Injuries"
INJURY_TABLE = "1D6"
INJURY_COLUMN = "Injury"
# roll -> (clauses, unmodelled). A clause is one of:
#   ("group", group, count, delta)          fixed reduction
#   ("group_dice", group, count, "1D6")     rolled reduction
#   ("choice", [(attribute, delta), ...])   the book names the options
INJURY_ROWS = {
    "1": ([("group_dice", "physical", 1, "1D6")],
          "The second clause, 'reduce both other physical "
          "characteristics by 2 (or one of them by 4)', depends on "
          "which characteristic the first clause reduced. An outcome "
          "cannot refer to a choice made earlier in its own sequence, "
          "so only the 1D6 reduction is expressed."),
    "2": ([("group_dice", "physical", 1, "1D6")], ""),
    "3": ([("choice", [("strength", -2), ("dexterity", -2)])], ""),
    "4": ([("group", "physical", 1, -2)], ""),
    "5": ([("group", "physical", 1, -1)], ""),
    "6": ([], ""),
}

GENERATED_BY = "examples/logovger/tools/extract_shared_tables.py"

MISHAP_SECTION = "Survival"
MISHAP_TABLE = "1D6"
MISHAP_COLUMN = "Mishaps"

# Leaving is not printed in every mishap cell. Rows 2 through 5 say
# "discharged" themselves; row 1 says only "Injured in action", and the
# rule that ends the career for it is the sentence that introduces the
# whole table. So that row's EndCareer cites the sentence, because a
# citation has to prove the clause it carries.
MISHAP_END_CAREER_QUOTE = (
    "This mishap is always enough to force you to leave the service "
    "after half a term, or two years of service.")

# roll -> (clauses, unmodelled). Clauses here are outcome kinds.
MISHAP_ROWS = {
    # "Injured in action. (This is the same as a result of 2 on the
    # Injury table.) Alternatively, roll twice on the Injury table and
    # take the lower result." Two named alternatives, and the book does
    # not say which: that is a choice, and the player owns it.
    "1": ([("end_career_by_sentence",),
           ("choice", "player",
            [("as a result of 2",
              "as a result of 2 on the Injury table",
              "@injury_2_c0"),
             ("roll twice, take the lower",
              "roll twice on the Injury table and take the lower result",
              "twice")]),
           ("half_term_years",)], ""),
    "2": ([("end_career",), ("half_term_years",)], ""),
    "3": ([("end_career",), ("money", -10000), ("half_term_years",)], ""),
    "4": ([("end_career",), ("forfeit",), ("half_term_years",)], ""),
    # No half-term years here. The row states its own four years of
    # imprisonment, and the owner's reading is that they ABSORB the two
    # a mishap costs rather than adding to them: four years total, not
    # six. The book does not say, so it is recorded rather than assumed.
    "5": ([("end_career",), ("age", 4), ("forfeit",)], ""),
    "6": ([("end_career",), ("table_roll", "Injury"), ("half_term_years",)],
          ""),
}
MISHAP_YEARS_UNMODELLED = (
    "The book does not say whether mishap 5's four years of "
    "imprisonment include the two years a mishap costs on leaving, or "
    "come on top of them. Read as including: four years total. Every "
    "other mishap row carries the two years on its own.")


COMMIT = ""


def slug(text):
    return re.sub(r"[^a-z0-9]+", "_", text.lower()).strip("_")


def band_alias(band):
    """A key that keeps the sign.

    slug() strips punctuation, so "-1" and "1+" both come out as "1"
    and the second silently overwrites the first. Aging bands are the
    only place in this book where a row key is signed, and losing the
    sign loses the row.
    """
    return band.replace("-", "m").replace("+", "p")


def cells(line):
    if not line.startswith("|"):
        return None
    return [c.strip() for c in line.rstrip("\n").split("|")[1:-1]]


def read_table(lines, header_first_cell, header_second_cell=None):
    """The rows of the table with this header.

    The mishap and injury tables are BOTH headed "| 1D6 | ... |", so
    the first cell alone selects whichever comes first in the file.
    That is how the injury rows ended up carrying mishap text. The
    second cell is what tells them apart, and it is also the column a
    cell citation has to name.
    """
    for i, line in enumerate(lines):
        row = cells(line)
        if not row or row[0] != header_first_cell:
            continue
        if header_second_cell and (len(row) < 2 or
                                   row[1] != header_second_cell):
            continue
        out = []
        for j in range(i + 1, len(lines)):
            body = cells(lines[j])
            if not body or len(body) != len(row):
                break
            if set(body[0]) <= set("-: "):
                continue
            out.append(body)
        if out:
            return out
    return []


class Builder:
    def __init__(self):
        self.ops = []

    def add(self, type_name, alias, props):
        self.ops.append({"op": "create_entity", "type": type_name,
                         "as": alias, "properties": props})
        return alias

    def relate(self, parent, child):
        self.ops.append({"op": "set_relation", "from": parent,
                         "relation": "HAS_PART", "to": child})

    def cite(self, section, table, row, quote, kind="cell", column=None):
        out = {"source_file": CHAPTER, "source_section": section,
               "source_kind": kind, "source_quote": quote}
        if table:
            out["source_table"] = table
        if row:
            out["source_row"] = row
        if column:
            out["source_column"] = column
        return out


def main():
    root, out_path = sys.argv[1], sys.argv[2]
    lines = open(os.path.join(root, CHAPTER), encoding="utf-8").read().split(
        "\n")
    global COMMIT
    COMMIT = open(os.path.join(root, "SOURCE_COMMIT"),
                  encoding="utf-8").read().strip()
    commit = COMMIT
    b = Builder()

    # The groups, quoted from the sentence that defines them.
    for name, (members, section) in GROUPS.items():
        b.add("AttributeGroup", f"@group_{name}", dict(
            name=f"{name} characteristics", attribute_refs=members,
            source_file=CHAPTER, source_section=section,
            source_kind="sentence", source_quote=GROUP_QUOTE))

    def group_clause(alias, group, count, delta, dice, citation, unmodelled):
        props = dict(name=alias.lstrip("@"),
                     attribute_group=f"@group_{group}",
                     affected_count=str(count), **citation)
        if dice:
            props["attribute_delta_dice"] = qualified(
                "d" + dice.lower(), "DiceExpression", COMMIT)
            # A rolled delta carries no sign, so the row has to say
            # which way it goes. Every rolled delta in this chapter is
            # a reduction ("Reduce one physical characteristic by 1D6"),
            # and the executor refuses one that does not say rather
            # than guessing the direction.
            props["attribute_delta_reduces"] = "true"
        else:
            props["attribute_delta"] = str(delta)
        if unmodelled:
            props["unmodelled"] = unmodelled
        return b.add("ModifyAttributesInGroup", alias, props)

    def sequence(alias, parts, citation):
        """One outcome from several clauses, in the book's order."""
        if len(parts) == 1:
            return parts[0]
        b.add("OutcomeSequence", alias,
              dict(name=alias.lstrip("@"), **citation))
        for index, part in enumerate(parts):
            step = f"{alias}_s{index}"
            b.add("OutcomeStep", step, dict(
                name=step.lstrip("@"), step_index=str(index),
                outcome=part, **citation))
            b.relate(alias, step)
        return alias

    counts = {"aging": 0, "injury": 0, "mishap": 0}

    # ---- aging ----------------------------------------------------
    aging_rows = read_table(lines, AGING_TABLE, AGING_COLUMN)
    # The page escapes negative keys ("| \\-6 |"), and a locator
    # addresses the row as printed, so keep both forms: the clean one
    # to look the row up, the printed one to cite it.
    printed = {r[0].replace("\\", ""): r[1] for r in aging_rows}
    as_printed = {r[0].replace("\\", ""): r[0] for r in aging_rows}
    b.add("RollableTable", "@aging_table", dict(
        name="Effects of Aging",
        dice=qualified("d2d6", "DiceExpression", COMMIT),
        **b.cite(AGING_SECTION, None, None,
                 "At the end of the fourth term, and at the end of every "
                 "term thereafter, the character must roll 2D6 on the "
                 "Aging Table.", "sentence")))
    for band, clauses in AGING_ROWS.items():
        quote = printed.get(band)
        if quote is None:
            print(f"REFUSED: the aging table has no row {band!r}; the "
                  f"source prints {sorted(printed)}")
            return 1
        citation = b.cite(AGING_SECTION, AGING_TABLE,
                          as_printed.get(band, band), quote,
                          column=AGING_COLUMN)
        parts = []
        for index, (group, count, delta) in enumerate(clauses):
            parts.append(group_clause(
                f"@aging_{band_alias(band)}_c{index}", group, count, delta, None,
                citation, ""))
        if not parts:
            parts = [b.add("NoEffect", f"@aging_{band_alias(band)}_none",
                           dict(name="aging: no effect", **citation))]
        outcome = sequence(f"@aging_{band_alias(band)}", parts, citation)
        entry = f"@aging_row_{band_alias(band)}"
        open_top = band.endswith("+")
        props = dict(name=f"aging {band}", roll_min=band.rstrip("+"),
                     outcome=outcome, **citation)
        if open_top:
            props["roll_max_unbounded"] = "true"
        else:
            props["roll_max"] = band
        # The bottom row catches everything under it, and says on the
        # row itself that this is a reading rather than a transcription
        # - the same treatment the misspelled skill cells get.
        if band == AGING_FLOOR_BAND:
            props["roll_min_unbounded"] = "true"
            props["source_defect"] = AGING_FLOOR_DEFECT
            props["suggested_reading"] = "Read as -6 or less."
        b.add("TableEntry", entry, props)
        b.relate("@aging_table", entry)
        counts["aging"] += 1

    # ---- injuries -------------------------------------------------
    injury_rows = {r[0]: r[1]
                   for r in read_table(lines, INJURY_TABLE, INJURY_COLUMN)}
    b.add("RollableTable", "@injury_table", dict(
        name="Injury", dice=qualified("d1d6", "DiceExpression", COMMIT),
        **b.cite(INJURY_SECTION, None, None,
                 "Characters that are wounded in combat or accidents "
                 "during character creation must roll on the Injury "
                 "table.", "sentence")))
    for roll, (clauses, unmodelled) in INJURY_ROWS.items():
        quote = injury_rows.get(roll)
        if quote is None or "njur" not in quote and roll != "6":
            pass
        citation = b.cite(INJURY_SECTION, INJURY_TABLE, roll, quote or "",
                          column=INJURY_COLUMN)
        parts = []
        for index, clause in enumerate(clauses):
            alias = f"@injury_{roll}_c{index}"
            if clause[0] == "group":
                parts.append(group_clause(alias, clause[1], clause[2],
                                          clause[3], None, citation,
                                          unmodelled))
            elif clause[0] == "group_dice":
                parts.append(group_clause(alias, clause[1], clause[2], 0,
                                          clause[3], citation, unmodelled))
            elif clause[0] == "choice":
                b.add("OutcomeChoice", alias, dict(
                    name=alias.lstrip("@"), choice_authority="player",
                    **citation))
                for k, (attribute, delta) in enumerate(clause[1]):
                    option = f"{alias}_o{k}"
                    effect = f"{alias}_e{k}"
                    b.add("ModifyAttribute", effect, dict(
                        name=f"{attribute} {delta}", attribute_ref=attribute,
                        attribute_delta=str(delta), **citation))
                    b.add("OutcomeOption", option, dict(
                        name=f"reduce {attribute}", option_index=str(k),
                        option_label=f"reduce {attribute}",
                        outcome=effect, **citation))
                    b.relate(alias, option)
                parts.append(alias)
        if not parts:
            props = dict(name="injury: no permanent effect", **citation)
            if unmodelled:
                props["unmodelled"] = unmodelled
            parts = [b.add("NoEffect", f"@injury_{roll}_none", props)]
        outcome = sequence(f"@injury_{roll}", parts, citation)
        entry = f"@injury_row_{roll}"
        b.add("TableEntry", entry, dict(
            name=f"injury {roll}", roll_min=roll, roll_max=roll,
            outcome=outcome, **citation))
        b.relate("@injury_table", entry)
        counts["injury"] += 1

    # ---- mishaps --------------------------------------------------
    b.add("RollableTable", "@mishap_table", dict(
        name="Survival Mishaps",
        dice=qualified("d1d6", "DiceExpression", COMMIT),
        **b.cite(MISHAP_SECTION, None, None,
                 "With the Referee's approval, you can keep the character "
                 "that fails a survival roll and roll on the Survival "
                 "Mishaps table instead.", "sentence")))
    mishap_rows = {r[0]: r[1]
                   for r in read_table(lines, MISHAP_TABLE, MISHAP_COLUMN)}
    for roll, (clauses, unmodelled) in MISHAP_ROWS.items():
        quote = mishap_rows.get(roll, "")
        citation = b.cite(MISHAP_SECTION, MISHAP_TABLE, roll, quote,
                          column=MISHAP_COLUMN)
        parts = []
        for index, clause in enumerate(clauses):
            alias = f"@mishap_{roll}_c{index}"
            props = dict(name=alias.lstrip("@"), **citation)
            if unmodelled and index == 0:
                props["unmodelled"] = unmodelled
            if clause[0] == "end_career":
                parts.append(b.add("EndCareer", alias, props))
            elif clause[0] == "end_career_by_sentence":
                parts.append(b.add("EndCareer", alias, dict(
                    name=alias.lstrip("@"),
                    **b.cite(MISHAP_SECTION, None, None,
                             MISHAP_END_CAREER_QUOTE, "sentence"))))
            elif clause[0] == "choice":
                # The book names both branches, so both are entities and
                # the pick is a question rather than a policy the
                # extractor decides. The alternative that rolls twice is
                # its own GrantTableRoll, because "take the lower" is a
                # property of THAT roll and not of the choice.
                _, authority, options = clause
                # Targets first, then the options that point at them: an
                # alias has to exist before an op refers to it.
                targets = []
                for label, printed, target in options:
                    if target == "twice":
                        target = f"@mishap_{roll}_twice"
                        b.add("GrantTableRoll", target, dict(
                            name=target.lstrip("@"), table="@injury_table",
                            roll_count="2", roll_selection="LOWEST",
                            **citation))
                    targets.append((label, printed, target))
                for option_index, (label, printed, target) in enumerate(
                        targets):
                    option = f"@mishap_{roll}_o{option_index}"
                    b.add("OutcomeOption", option, dict(
                        name=label, option_index=str(option_index),
                        option_label=printed, outcome=target, **citation))
                b.add("OutcomeChoice", alias,
                      dict(name=alias.lstrip("@"),
                           choice_authority=authority, **citation))
                for option_index in range(len(options)):
                    b.relate(alias, f"@mishap_{roll}_o{option_index}")
                parts.append(alias)
            elif clause[0] == "forfeit":
                parts.append(b.add("ForfeitBenefits", alias, props))
            elif clause[0] == "money":
                props.update(amount=str(clause[1]),
                             currency=qualified("credits", "Currency",
                                                COMMIT))
                parts.append(b.add("GainFixedMoney", alias, props))
            elif clause[0] == "age":
                props.update(attribute_ref="age_years",
                             attribute_delta=str(clause[1]),
                             unmodelled=MISHAP_YEARS_UNMODELLED)
                parts.append(b.add("ModifyAttribute", alias, props))
            elif clause[0] == "half_term_years":
                # "leave the service after half a term, or two years of
                # service" - an effect the book applies, so it is an
                # outcome on the row rather than an addition the
                # procedure performs. Cited to the sentence that states
                # it, which writes the two as a word.
                parts.append(b.add("ModifyAttribute", alias, dict(
                    name=alias.lstrip("@"), attribute_ref="age_years",
                    attribute_delta="2",
                    **b.cite(MISHAP_SECTION, None, None,
                             MISHAP_END_CAREER_QUOTE, "sentence"))))
            elif clause[0] == "table_roll":
                props["table"] = "@injury_table"
                parts.append(b.add("GrantTableRoll", alias, props))
        outcome = sequence(f"@mishap_{roll}", parts, citation)
        entry = f"@mishap_row_{roll}"
        b.add("TableEntry", entry, dict(
            name=f"mishap {roll}", roll_min=roll, roll_max=roll,
            outcome=outcome, **citation))
        b.relate("@mishap_table", entry)
        counts["mishap"] += 1

    seed = {"source": {"file": CHAPTER, "commit": COMMIT},
            "layer": "cepheus",
            "generated_by": GENERATED_BY, "invariants": {}, "ops": []}
    seed["ops"] = b.ops
    added = len(b.ops)

    counts_by_type = {}
    for op in seed["ops"]:
        if op.get("op") == "create_entity":
            counts_by_type[op["type"]] = counts_by_type.get(op["type"], 0) + 1
    invariants = seed.setdefault("invariants", {})
    invariants["count_of_type"] = {
        t: counts_by_type[t]
        for t in ("AttributeGroup", "RollableTable", "TableEntry")
        if t in counts_by_type}
    unique = invariants.setdefault("unique_name_per_type", [])
    for t in ("AttributeGroup", "RollableTable"):
        if t not in unique:
            unique.append(t)
    unique.sort()
    json.dump(seed, open(out_path, "w"), indent=1)
    open(out_path, "a").write("\n")
    print(f"this seed owns {added} op(s)")

    partial = sum(1 for op in b.ops
                  if op.get("properties", {}).get("unmodelled"))
    print(f"aging rows:   {counts['aging']}")
    print(f"injury rows:  {counts['injury']}")
    print(f"mishap rows:  {counts['mishap']}")
    print(f"partly expressed, and saying so: {partial}")
    print(f"total ops:    {len(b.ops)} -> {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
