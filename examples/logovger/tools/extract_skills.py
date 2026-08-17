#!/usr/bin/env python3
"""Read every skill the Cepheus skills chapter defines, verbatim.

Part of the ingestion pipeline, and deliberately dumb. It transcribes
headings and the cascade lists inside them; it does not decide what
anything means. A model that transcribes a skill name wrong produces a
rule that is wrong in a way the prose still reads fine, so this half of
the work is mechanical and every string is the exact bytes from the
source. Judgement about meaning happens elsewhere, against text this
tool has already proved.

Output is a seed envelope: one file that OWNS the skill vocabulary.
Everything else references its exact ingestion edition, Skill type, and seed
alias rather than creating its own copy, which is what the four duplicate Gun
Combat entities taught us.

Usage:
    python3 examples/logovger/tools/extract_skills.py \
        examples/logovger/srd/cepheus examples/logovger/seeds/....json
"""
import json
import os
import re
import sys

# Headings in the skills chapter that are not skills. They are the
# chapter's own scaffolding, and taking them for skills would put five
# entities in the graph that no rule can ever grant.
NOT_SKILLS = {
    "Task Description Format",
    "Informal Skill Check Descriptions",
    "Untrained and Zero-Level Skills",
    "Going Faster or Slower",
    "Multiple Actions",
    "Local Law Level",
}

GENERATED_BY = "examples/logovger/tools/extract_skills.py"

# Skills the career tables grant that the skills chapter never defines.
# They enter the graph as the book writes them, marked, with the
# reasoning recorded and consumed by nothing. Both were found the same
# way: by resolving every career-table cell against this vocabulary and
# refusing to guess at what did not resolve. See orffen/cepheus-srd#36.
UNDEFINED_IN_CHAPTER = {
    "Perception": {
        "source_file": "book1/character-creation.md",
        "source_section": "Career Tables",
        "source_kind": "cell",
        "source_table": "Specialist",
        "source_row": "3",
        "source_column": "Bureaucrat",
        "source_quote": "Perception",
        "source_defect": (
            "Granted by the Bureaucrat Specialist table but defined "
            "nowhere: 'Perception' occurs exactly once in the SRD, in "
            "this cell, and has no entry in book1/skills.md nor a line "
            "in the Available Skills List."
        ),
        "suggested_reading": (
            "Recon is the only defined skill that covers observing and "
            "noticing, and it fills the equivalent slot in the "
            "Aerospace Defense, Agent and Barbarian columns. A guess, "
            "not a reading the text proves, so nothing acts on it."
        ),
    },
    "Prospecting": {
        "source_file": "book1/character-creation.md",
        "source_section": "Career Tables",
        "source_kind": "cell",
        "source_table": "Service Skills",
        "source_row": "5",
        "source_column": "Belter",
        "source_quote": "Prospecting",
        "source_defect": (
            "Granted by the Belter Service Skills and Belter Specialist "
            "tables but defined nowhere: 'Prospecting' occurs exactly "
            "twice in the SRD, both in career tables, and has no entry "
            "in book1/skills.md nor a line in the Available Skills "
            "List."
        ),
        "suggested_reading": (
            "No defined skill covers finding and assessing ore. Unlike "
            "Perception, there is no near neighbour to name: the Belter "
            "career is built around this skill, so the likely reading "
            "is that the entry is missing from the chapter rather than "
            "that the cell is a typo for something else."
        ),
    },
}


def alias_slug(name):
    """A stable seed alias for a skill name."""
    return "@sk_" + re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_")


def parse_heading(text):
    """'Aircraft (Cascade Skill)' -> ('Aircraft', True, []).

    'Jack-of-All-Trades (Jack o' Trades or JoT)' ->
        ('Jack-of-All-Trades', False, ["Jack o' Trades", 'JoT']).

    The parenthetical is the source's own words either way, so the
    aliases it names are citable rather than invented.
    """
    m = re.match(r"^(.*?)\s*\((.*)\)\s*$", text)
    if not m:
        return text.strip(), False, []
    name, inside = m.group(1).strip(), m.group(2).strip()
    if inside.lower() == "cascade skill":
        return name, True, []
    aliases = [a.strip() for a in re.split(r"\bor\b|,", inside) if a.strip()]
    return name, False, aliases


def main():
    root, out_path = sys.argv[1], sys.argv[2]
    path = os.path.join(root, "book1", "skills.md")
    lines = open(path, encoding="utf-8").read().split("\n")

    commit = open(os.path.join(root, "SOURCE_COMMIT"),
                  encoding="utf-8").read().strip()

    skills = []
    for i, line in enumerate(lines):
        if not line.startswith("### "):
            continue
        heading = line[4:].strip()
        name, cascade, aliases = parse_heading(heading)
        if name in NOT_SKILLS or heading in NOT_SKILLS:
            continue
        skills.append({
            "name": name,
            "cascade": cascade,
            "aliases": aliases,
            "heading": heading,       # cited verbatim, parenthetical and all
        })

    ops = []
    for s in skills:
        props = {
            "name": s["name"],
            "source_file": "book1/skills.md",
            "source_section": s["heading"],
            "source_kind": "heading",
            "source_quote": s["heading"],
        }
        if s["cascade"]:
            props["is_cascade"] = "true"
        if s["aliases"]:
            props["source_aliases"] = "; ".join(s["aliases"])
        ops.append({
            "op": "create_entity",
            "type": "Skill",
            "as": alias_slug(s["name"]),
            "properties": props,
        })

    for name, props in UNDEFINED_IN_CHAPTER.items():
        entry = {"name": name}
        entry.update(props)
        ops.append({
            "op": "create_entity",
            "type": "Skill",
            "as": alias_slug(name),
            "properties": entry,
        })

    names = [s["name"] for s in skills] + list(UNDEFINED_IN_CHAPTER)
    seed = {
        "source": {"file": "book1/skills.md", "commit": commit},
        "layer": "cepheus",
        "generated_by": GENERATED_BY,
        "invariants": {
            "count_of_type": {"Skill": len(ops)},
            "unique_name_per_type": ["Skill"],
        },
        "ops": ops,
    }
    json.dump(seed, open(out_path, "w"), indent=1)
    json_out = open(out_path, "a")
    json_out.write("\n")

    cascades = [s["name"] for s in skills if s["cascade"]]
    aliased = [s["name"] for s in skills if s["aliases"]]
    print(f"skills defined in the chapter: {len(skills)}")
    print(f"cascade parents: {len(cascades)}  {cascades}")
    print(f"carrying aliases: {aliased}")
    print(f"marked defective: {list(UNDEFINED_IN_CHAPTER)}")
    print(f"total Skill ops: {len(ops)} -> {out_path}")
    dupes = {n for n in names if names.count(n) > 1}
    if dupes:
        print(f"REFUSED: the chapter defines these twice: {sorted(dupes)}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
