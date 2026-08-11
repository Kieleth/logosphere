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
        examples/logovger/seeds/cepheus_book1_tables.json
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
}
GROUP_QUOTE = (
    "Strength, Dexterity, and Endurance are called physical abilities, "
    "whereas Intelligence, Education, and Social Standing are loosely "
    "termed mental abilities.")

# Aging: 2D6 with the character's total terms as a negative DM. Bands
# are transcribed as printed, including the unmarked bottom row: the
# top says "1+" and the bottom says "-6" with no "or less", so a total
# of -7 lands on nothing. That asymmetry is the book's, not ours.
AGING_SECTION = "Aging"
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

MISHAP_SECTION = "Survival"
# roll -> (clauses, unmodelled). Clauses here are outcome kinds.
MISHAP_ROWS = {
    "1": ([("table_roll", "Injury")],
          "The book says this is the same as a result of 2 on the "
          "Injury table, and offers an alternative of rolling twice "
          "and taking the lower. The alternative is a re-roll policy, "
          "which the outcome vocabulary cannot say, so this rolls on "
          "the Injury table."),
    "2": ([("end_career",)], ""),
    "3": ([("end_career",), ("money", -10000)], ""),
    "4": ([("end_career",), ("forfeit",)], ""),
    "5": ([("end_career",), ("age", 4), ("forfeit",)], ""),
    "6": ([("end_career",), ("table_roll", "Injury")], ""),
}


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


def read_table(lines, header_first_cell):
    """The rows of the one table whose header starts with this cell."""
    for i, line in enumerate(lines):
        row = cells(line)
        if not row or row[0] != header_first_cell:
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

    def cite(self, section, table, row, quote, kind="cell"):
        out = {"source_file": CHAPTER, "source_section": section,
               "source_kind": kind, "source_quote": quote}
        if table:
            out["source_table"] = table
        if row:
            out["source_row"] = row
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
            props["attribute_delta_dice"] = "@d" + dice.lower()
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
    aging_rows = read_table(lines, "2D6")
    printed = {r[0].replace("\\", ""): r[1] for r in aging_rows}
    b.add("RollableTable", "@aging_table", dict(
        name="Effects of Aging",
        dice="@d2d6",
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
        citation = b.cite(AGING_SECTION, "2D6", band, quote)
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
        b.add("TableEntry", entry, props)
        b.relate("@aging_table", entry)
        counts["aging"] += 1

    # ---- injuries -------------------------------------------------
    injury_rows = {r[0]: r[1] for r in read_table(lines, "1D6")}
    b.add("RollableTable", "@injury_table", dict(
        name="Injury", dice="@d1d6",
        **b.cite(INJURY_SECTION, None, None,
                 "Characters that are wounded in combat or accidents "
                 "during character creation must roll on the Injury "
                 "table.", "sentence")))
    for roll, (clauses, unmodelled) in INJURY_ROWS.items():
        quote = injury_rows.get(roll)
        if quote is None or "njur" not in quote and roll != "6":
            pass
        citation = b.cite(INJURY_SECTION, "Injury", roll, quote or "")
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
        dice="@d1d6",
        **b.cite(MISHAP_SECTION, None, None,
                 "With the Referee's approval, you can keep the character "
                 "that fails a survival roll and roll on the Survival "
                 "Mishaps table instead.", "sentence")))
    mishap_rows = {}
    for row in read_table(lines, "1D6"):
        if "ischarged" in row[1] or "Injured in action" in row[1]:
            mishap_rows[row[0]] = row[1]
    for roll, (clauses, unmodelled) in MISHAP_ROWS.items():
        quote = mishap_rows.get(roll, "")
        citation = b.cite(MISHAP_SECTION, "Mishaps", roll, quote)
        parts = []
        for index, clause in enumerate(clauses):
            alias = f"@mishap_{roll}_c{index}"
            props = dict(name=alias.lstrip("@"), **citation)
            if unmodelled and index == 0:
                props["unmodelled"] = unmodelled
            if clause[0] == "end_career":
                parts.append(b.add("EndCareer", alias, props))
            elif clause[0] == "forfeit":
                parts.append(b.add("ForfeitBenefits", alias, props))
            elif clause[0] == "money":
                props.update(amount=str(clause[1]),
                             currency="@credits")
                parts.append(b.add("GainFixedMoney", alias, props))
            elif clause[0] == "age":
                props.update(attribute_ref="age_years",
                             attribute_delta=str(clause[1]))
                parts.append(b.add("ModifyAttribute", alias, props))
            elif clause[0] == "table_roll":
                props["table"] = "@injury_table"
                props["roll_count"] = "1"
                parts.append(b.add("GrantTableRoll", alias, props))
        outcome = sequence(f"@mishap_{roll}", parts, citation)
        entry = f"@mishap_row_{roll}"
        b.add("TableEntry", entry, dict(
            name=f"mishap {roll}", roll_min=roll, roll_max=roll,
            outcome=outcome, **citation))
        b.relate("@mishap_table", entry)
        counts["mishap"] += 1

    # Merge into the seed that already owns the mishap and injury
    # tables. Existing ops are untouched: their aliases are referenced
    # from other seeds, and a stable alias is the whole basis of a
    # qualified reference.
    seed = json.load(open(out_path, encoding="utf-8"))
    have = {op.get("as") for op in seed["ops"] if op.get("as")}
    existing_relations = {
        (op.get("from"), op.get("to")) for op in seed["ops"]
        if op.get("op") == "set_relation"}
    added = 0
    for op in b.ops:
        if op.get("op") == "create_entity":
            if op["as"] in have:
                continue          # the sampler already wrote this row
            have.add(op["as"])
        elif (op.get("from"), op.get("to")) in existing_relations:
            continue
        # A relation whose target was skipped would dangle.
        if op.get("op") == "set_relation" and op.get("to") not in have:
            continue
        seed["ops"].append(op)
        added += 1

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
    print(f"appended {added} op(s) into the seed that owns these tables")

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
