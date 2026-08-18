#!/usr/bin/env python3
"""Build the reviewed Cepheus skill vocabulary and its exact source ledger.

The parser is mechanical: it computes UTF-8 byte ranges for headings,
sentences and Markdown table cells. The semantic policy below is specific to
the reviewed skills.md bytes. It is not a general Markdown-to-rules parser and
it refuses a changed source hash until a reader reviews the new text.

Output is the one seed that owns the skill vocabulary and the complete source
partition for book1/skills.md. Other seeds reference its exact ingestion
edition, Skill type and seed alias instead of creating another vocabulary.

Usage:
    python3 examples/logovger/tools/extract_skills.py \
        examples/logovger/srd/cepheus examples/logovger/seeds/....json
"""
from collections import Counter, defaultdict
import hashlib
import json
import os
import re
import sys


NOT_SKILLS = {
    "Task Description Format",
    "Informal Skill Check Descriptions",
    "Untrained and Zero-Level Skills",
    "Going Faster or Slower",
    "Multiple Actions",
    "Local Law Level",
}

GENERATED_BY = "examples/logovger/tools/extract_skills.py"
REVIEWED_SOURCE_SHA256 = (
    "5ea1976cce85c5b9dad5d4db40653c5ad20e30ba09f450ea497c5c850ca62a90"
)
ARBITER = "Codex, owner-directed skills migration"

# These exact sentences describe the document or navigate elsewhere. Every
# other prose sentence in the hash-pinned chapter states gameplay or ontology
# content and becomes an explicit claim. There is no unknown-content default.
NO_RULE_SENTENCES = {
    "Characters in Cepheus Engine games engage in a variety of activities, "
    "using their various skills to accomplish the challenges that confront "
    "them.",
    "Skills and their usage are described in this chapter.",
    "The basics of the task resolution system can be found in the "
    "[Introduction chapter](../introduction.md), under "
    "[Die Rolls](../introduction.md#die-rolls).",
    "Task descriptions can be formally written in a specific format, as "
    "follows.",
    "In print, this is often italicized to help it stand out.",
    "The following is a list of the available skills used in the core "
    "Cepheus Engine rules.",
    "This section describes each skill found in the Cepheus Engine, "
    "including its common uses.",
}

TABLE_HEADERS = {
    ("Time Frame", "Base Increment"),
    ("Law Level", "Difficulty"),
    ("Basic Skills", "Weapon Skills", "Transport Skills"),
    ("Offense", "DM", "Minimum Bribe"),
    ("Odds of Winning", "DM", "Payoff", "Maximum Bet"),
}

# Skills the career tables grant that this chapter never defines. They remain
# on their character-creation evidence until that representation is migrated.
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
    """Return the stable seed alias for a skill name."""
    return "@sk_" + re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_")


def normalized_name(name):
    return re.sub(r"[^a-z0-9]+", " ", name.lower()).strip()


def parse_heading(text):
    """Split a skill heading into name, cascade marker and source aliases."""
    match = re.match(r"^(.*?)\s*\((.*)\)\s*$", text)
    if not match:
        return text.strip(), False, []
    name, inside = match.group(1).strip(), match.group(2).strip()
    if inside.lower() == "cascade skill":
        return name, True, []
    aliases = [
        alias.strip()
        for alias in re.split(r"\bor\b|,", inside)
        if alias.strip()
    ]
    return name, False, aliases


def create(ops, entity_type, alias, properties=None):
    ops.append({
        "op": "create_entity",
        "type": entity_type,
        "as": alias,
        "properties": properties or {},
    })


def relation(ops, source, predicate, target):
    ops.append({
        "op": "set_relation",
        "from": source,
        "relation": predicate,
        "to": target,
    })


def semantic_text(text):
    """Remove presentation markup from a claim statement, not its evidence."""
    text = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", text)
    text = text.replace("**", "").replace("_", "")
    return text.strip()


def clean_cell(text):
    return semantic_text(text.replace("\\-", "-"))


def sentence_spans(text):
    """Return source character spans for the reviewed chapter's sentences."""
    spans = []
    start = 0
    index = 0
    while index < len(text):
        char = text[index]
        boundary = char in ".!?" and (
            index + 1 == len(text) or text[index + 1].isspace()
        )
        abbreviation = text[max(0, index - 3):index + 1].lower() in {
            "i.e.", "e.g.",
        }
        if boundary and not abbreviation:
            spans.append((start, index + 1))
            start = index + 1
            while start < len(text) and text[start].isspace():
                start += 1
            index = start
            continue
        index += 1
    if start < len(text):
        spans.append((start, len(text)))
    return [(start, end) for start, end in spans if text[start:end].strip()]


def source_skills(source_bytes):
    skills = []
    for raw in source_bytes.splitlines():
        if not raw.startswith(b"### "):
            continue
        heading = raw[4:].decode("utf-8").strip()
        name, cascade, aliases = parse_heading(heading)
        if name in NOT_SKILLS or heading in NOT_SKILLS:
            continue
        skills.append({
            "name": name,
            "cascade": cascade,
            "aliases": aliases,
            "heading": heading,
        })
    names = [skill["name"] for skill in skills]
    duplicates = sorted({name for name in names if names.count(name) > 1})
    if duplicates:
        raise ValueError(f"chapter defines skills twice: {duplicates}")
    return skills


def enumerate_source(source_bytes, skills):
    """Enumerate exact semantic leaves and visible syntax/layout exclusions."""
    skill_names = {
        normalized_name(skill["name"]): skill["name"] for skill in skills
    }
    leaves = []
    exclusions = []
    cursor = 0
    section = None
    current_skill = None
    active_table = None

    def exclude(line, label, start, end, kind):
        if start == end:
            return
        exclusions.append({
            "base": f"skills_l{line:04d}_{label}",
            "start": start,
            "end": end,
            "kind": kind,
        })

    def target(line, label, start, end, kind, **metadata):
        if start == end:
            raise ValueError(f"empty source target at line {line}: {label}")
        leaves.append({
            "base": f"skills_l{line:04d}_{label}",
            "start": start,
            "end": end,
            "text": source_bytes[start:end].decode("utf-8"),
            "kind": kind,
            "section": section,
            "skill": current_skill,
            "claims": [],
            **metadata,
        })

    for line_number, raw_line in enumerate(
            source_bytes.splitlines(keepends=True), 1):
        body = raw_line.rstrip(b"\r\n")
        newline = raw_line[len(body):]
        body_start = cursor
        body_end = body_start + len(body)
        newline_start = body_end

        heading_match = re.match(br"^(#{1,6}) ", body)
        if heading_match:
            active_table = None
            prefix_end = body_start + heading_match.end()
            heading = body[heading_match.end():].decode("utf-8")
            level = len(heading_match.group(1))
            if level == 2:
                section = heading
                current_skill = None
            elif level == 3:
                parsed_name, _, _ = parse_heading(heading)
                current_skill = skill_names.get(normalized_name(parsed_name))
            exclude(line_number, "heading_syntax", body_start, prefix_end,
                    "SYNTAX")
            target(line_number, "heading", prefix_end, body_end, "heading",
                   heading=heading, heading_level=level)
        elif body.startswith(b"|"):
            if not body.endswith(b"|"):
                raise ValueError(
                    f"line {line_number}: reviewed table row has no final pipe")
            pipes = [index for index, byte in enumerate(body) if byte == ord("|")]
            cells = [
                body[pipes[index] + 1:pipes[index + 1]].decode("utf-8")
                for index in range(len(pipes) - 1)
            ]
            separator = bool(cells) and all(
                re.fullmatch(r":?-+:?", cell.strip()) for cell in cells
            )
            if separator:
                if active_table is None:
                    raise ValueError(
                        f"line {line_number}: separator has no table header")
                exclude(line_number, "table_separator", body_start, body_end,
                        "SYNTAX")
            else:
                values = tuple(clean_cell(cell) for cell in cells)
                if active_table is None:
                    if values not in TABLE_HEADERS:
                        raise ValueError(
                            f"line {line_number}: unreviewed table {values}")
                    active_table = values
                    row_kind = "header"
                else:
                    row_kind = "data"
                for pipe_index, pipe in enumerate(pipes):
                    exclude(line_number, f"table_pipe_{pipe_index}",
                            body_start + pipe, body_start + pipe + 1, "SYNTAX")
                for cell_index in range(len(pipes) - 1):
                    start = body_start + pipes[cell_index] + 1
                    end = body_start + pipes[cell_index + 1]
                    target(
                        line_number, f"table_cell_{cell_index}", start, end,
                        "table_cell", table_header=active_table,
                        table_row_kind=row_kind, table_row=line_number,
                        table_column=cell_index, table_values=values,
                    )
        else:
            active_table = None
            prose_start = 0
            if body.startswith(b"> "):
                exclude(line_number, "blockquote_syntax", body_start,
                        body_start + 2, "SYNTAX")
                prose_start = 2
            text = body[prose_start:].decode("utf-8")
            spans = sentence_spans(text)
            local_cursor = 0
            for sentence_index, (char_start, char_end) in enumerate(spans):
                byte_start = len(text[:char_start].encode("utf-8"))
                byte_end = len(text[:char_end].encode("utf-8"))
                if byte_start > local_cursor:
                    exclude(
                        line_number, f"sentence_gap_{sentence_index}",
                        body_start + prose_start + local_cursor,
                        body_start + prose_start + byte_start, "LAYOUT")
                target(
                    line_number, f"sentence_{sentence_index}",
                    body_start + prose_start + byte_start,
                    body_start + prose_start + byte_end, "sentence",
                )
                local_cursor = byte_end
            if local_cursor < len(body) - prose_start:
                exclude(line_number, "prose_layout", body_start + prose_start +
                        local_cursor, body_end, "LAYOUT")

        if newline:
            exclude(line_number, "newline", newline_start,
                    newline_start + len(newline), "LAYOUT")
        cursor += len(raw_line)

    if cursor != len(source_bytes):
        raise ValueError(
            "source does not end on a splitlines boundary; bytes were omitted")
    return leaves, exclusions


def classify(leaves, skills):
    """Apply the reviewed, source-specific semantic decisions."""
    claims = []
    skill_aliases = {
        normalized_name(skill["name"]): alias_slug(skill["name"])
        for skill in skills
    }
    heading_claims = {}

    def add_claim(alias, statement, disposition, reason, supporting,
                  gap_kind=None, materializes=None, related=None):
        claim = {
            "alias": alias,
            "statement": statement,
            "disposition": disposition,
            "reason": reason,
            "supporting": supporting,
            "gap_kind": gap_kind,
            "materializes": materializes,
            "related": related,
        }
        claims.append(claim)
        for leaf in supporting:
            leaf["claims"].append(alias)
        return claim

    for leaf in leaves:
        if leaf["kind"] != "heading" or leaf["heading_level"] != 3:
            continue
        parsed_name, _, _ = parse_heading(leaf["heading"])
        key = normalized_name(parsed_name)
        skill_alias = skill_aliases.get(key)
        if skill_alias is None:
            continue
        claim_alias = "@claim_skill_" + skill_alias.removeprefix("@sk_")
        heading_claims[key] = claim_alias
        add_claim(
            claim_alias,
            f"The chapter defines {parsed_name} as a skill.",
            "MATERIALIZED",
            "The Skill entity captures the chapter-defined vocabulary entry.",
            [leaf],
            materializes=skill_alias,
        )

    reviewed_no_rule = set()
    for leaf in leaves:
        if leaf["kind"] != "sentence":
            continue
        if leaf["text"] in NO_RULE_SENTENCES:
            reviewed_no_rule.add(leaf["text"])
            continue
        in_skill_description = (
            leaf["section"] == "Skill Descriptions" and leaf["skill"]
        )
        gap_kind = "ONTOLOGY_GAP" if in_skill_description else \
            "RULE_LANGUAGE_GAP"
        reason = (
            "The current Skill ontology does not represent this meaning or "
            "capability."
            if in_skill_description else
            "The current rule language does not express this gameplay rule."
        )
        add_claim(
            "@claim_" + leaf["base"], semantic_text(leaf["text"]),
            "RAISED", reason, [leaf], gap_kind=gap_kind,
        )

    if reviewed_no_rule != NO_RULE_SENTENCES:
        missing = sorted(NO_RULE_SENTENCES - reviewed_no_rule)
        raise ValueError(
            f"reviewed no-rule sentences were not enumerated: {missing}")

    rows = defaultdict(list)
    for leaf in leaves:
        if (leaf["kind"] == "table_cell" and
                leaf["table_row_kind"] == "data"):
            rows[(leaf["table_header"], leaf["table_row"])].append(leaf)

    available_header = (
        "Basic Skills", "Weapon Skills", "Transport Skills")
    for (header, line), row_leaves in sorted(rows.items(), key=lambda item: item[0][1]):
        nonempty = [leaf for leaf in row_leaves if clean_cell(leaf["text"])]
        if header == available_header:
            for leaf in nonempty:
                displayed = clean_cell(leaf["text"])
                parsed_name, _, _ = parse_heading(displayed)
                key = normalized_name(parsed_name)
                if key in heading_claims:
                    add_claim(
                        "@claim_" + leaf["base"],
                        f"{displayed} is listed as an available skill.",
                        "DUPLICATE",
                        "The later skill heading is the canonical vocabulary "
                        "claim for this repeated list entry.",
                        [leaf], related=heading_claims[key],
                    )
                elif parsed_name == "Airship":
                    add_claim(
                        "@claim_" + leaf["base"],
                        "Airship is listed as an available transport skill.",
                        "RAISED",
                        "The chapter links Airship to another book but defines "
                        "no Airship skill heading here.",
                        [leaf], gap_kind="SOURCE_GAP",
                    )
                else:
                    raise ValueError(
                        f"line {line}: available skill has no reviewed "
                        f"definition decision: {displayed}")
            continue

        values = row_leaves[0]["table_values"]
        if header == ("Time Frame", "Base Increment"):
            statement = f"{values[0]} has a base increment of {values[1]}."
        elif header == ("Law Level", "Difficulty"):
            statement = f"Law Level {values[0]} has difficulty {values[1]}."
        elif header == ("Offense", "DM", "Minimum Bribe"):
            statement = (
                f"{values[0]} has Bribery DM {values[1]} and minimum bribe "
                f"{values[2]}."
            )
        elif header == ("Odds of Winning", "DM", "Payoff", "Maximum Bet"):
            statement = (
                f"{values[0]} gambling odds have DM {values[1]}, payoff "
                f"{values[2]}, and maximum bet {values[3]}."
            )
        else:
            raise ValueError(f"line {line}: table has no semantic policy")
        add_claim(
            f"@claim_table_l{line:04d}", statement, "RAISED",
            "The current rule language does not express this table row.",
            nonempty, gap_kind="RULE_LANGUAGE_GAP",
        )

    expected_heading_claims = set(skill_aliases)
    if set(heading_claims) != expected_heading_claims:
        missing = sorted(expected_heading_claims - set(heading_claims))
        extra = sorted(set(heading_claims) - expected_heading_claims)
        raise ValueError(
            f"skill heading claim mismatch, missing={missing}, extra={extra}")
    return claims


def assert_partition(source_bytes, leaves, exclusions):
    intervals = [
        (leaf["start"], leaf["end"], leaf["base"]) for leaf in leaves
    ] + [
        (item["start"], item["end"], item["base"]) for item in exclusions
    ]
    aliases = [alias for _, _, alias in intervals]
    duplicates = sorted({alias for alias in aliases if aliases.count(alias) > 1})
    if duplicates:
        raise ValueError(f"duplicate partition aliases: {duplicates}")
    cursor = 0
    for start, end, alias in sorted(intervals):
        if start != cursor:
            raise ValueError(
                f"source partition gap/overlap before {alias}: "
                f"expected {cursor}, got {start}")
        if start >= end:
            raise ValueError(f"empty partition interval: {alias}")
        cursor = end
    if cursor != len(source_bytes):
        raise ValueError(
            f"source partition ends at {cursor}, source has "
            f"{len(source_bytes)} bytes")


def build_seed(source_bytes, commit):
    digest = hashlib.sha256(source_bytes).hexdigest()
    if digest != REVIEWED_SOURCE_SHA256:
        raise ValueError(
            "unreviewed skills.md bytes: expected sha256 "
            f"{REVIEWED_SOURCE_SHA256}, got {digest}")

    skills = source_skills(source_bytes)
    leaves, exclusions = enumerate_source(source_bytes, skills)
    claims = classify(leaves, skills)
    assert_partition(source_bytes, leaves, exclusions)

    ops = []
    for skill in skills:
        properties = {"name": skill["name"]}
        if skill["cascade"]:
            properties["is_cascade"] = "true"
        if skill["aliases"]:
            properties["source_aliases"] = "; ".join(skill["aliases"])
        create(ops, "Skill", alias_slug(skill["name"]), properties)

    for name, properties in UNDEFINED_IN_CHAPTER.items():
        create(ops, "Skill", alias_slug(name), {"name": name, **properties})

    for leaf in leaves:
        base = "@" + leaf["base"]
        create(ops, "ByteRangeSelector", base + "_range", {
            "source_byte_start": leaf["start"],
            "source_byte_end": leaf["end"],
        })
        create(ops, "TextQuoteSelector", base + "_quote", {
            "source_quote_exact": leaf["text"],
        })
        create(ops, "SourceTarget", base + "_target", {
            "target_primary_selector": base + "_range",
            "target_quote_selector": base + "_quote",
        })
        create(ops, "SourceCoverage", base + "_coverage", {
            "coverage_target": base + "_target",
        })
        has_claims = bool(leaf["claims"])
        create(ops, "CoverageDecision", base + "_coverage_decision", {
            "event_type": "ARBITER_DECISION",
            "decision_subject": base + "_coverage",
            "decision_sequence": 0,
            "coverage_judgement": (
                "CLAIMS_PRESENT" if has_claims else "NO_RULE_CONTENT"),
            "decision_question": (
                "Does this exact leaf state ingested rule or ontology content?"),
            "decision_reason": (
                "The reviewed leaf supports one or more ingestion claims."
                if has_claims else
                "The reviewed leaf is structural or states no independent "
                "gameplay or ontology claim."
            ),
            "arbiter": ARBITER,
        })

    for item in exclusions:
        base = "@" + item["base"]
        create(ops, "ByteRangeSelector", base + "_range", {
            "source_byte_start": item["start"],
            "source_byte_end": item["end"],
        })
        create(ops, "SourceExclusion", base + "_exclusion", {
            "exclusion_selector": base + "_range",
            "exclusion_kind": item["kind"],
        })

    create(ops, "CompleteSourcePartition", "@skills_complete_partition")

    for claim in claims:
        create(ops, "IngestionClaim", claim["alias"], {
            "claim_statement": claim["statement"],
        })
    for claim in claims:
        properties = {
            "event_type": "ARBITER_DECISION",
            "decision_subject": claim["alias"],
            "decision_sequence": 0,
            "claim_disposition": claim["disposition"],
            "decision_question": "Can this claim enter the typed graph?",
            "decision_reason": claim["reason"],
            "arbiter": ARBITER,
        }
        if claim["gap_kind"]:
            properties["claim_gap_kind"] = claim["gap_kind"]
        if claim["related"]:
            properties["related_claim"] = claim["related"]
        create(ops, "ClaimDecision", claim["alias"] + "_decision", properties)

    for claim in claims:
        for leaf in claim["supporting"]:
            relation(
                ops, claim["alias"], "CLAIM_SUPPORTED_BY",
                "@" + leaf["base"] + "_coverage",
            )
        if claim["materializes"]:
            relation(
                ops, claim["alias"], "CLAIM_MATERIALIZES",
                claim["materializes"],
            )

    counts = Counter(
        op["type"] for op in ops if op["op"] == "create_entity")
    return {
        "source": {"file": "book1/skills.md", "commit": commit},
        "layer": "cepheus",
        "generated_by": GENERATED_BY,
        "invariants": {
            "count_of_type": dict(sorted(counts.items())),
            "unique_name_per_type": ["Skill"],
        },
        "ops": ops,
    }, skills, leaves, exclusions, claims


def main():
    root, out_path = sys.argv[1], sys.argv[2]
    path = os.path.join(root, "book1", "skills.md")
    with open(path, "rb") as source:
        source_bytes = source.read()
    with open(os.path.join(root, "SOURCE_COMMIT"), encoding="utf-8") as source:
        commit = source.read().strip()

    try:
        seed, skills, leaves, exclusions, claims = build_seed(
            source_bytes, commit)
    except ValueError as error:
        print(f"REFUSED: {error}", file=sys.stderr)
        return 1

    with open(out_path, "w", encoding="utf-8") as output:
        json.dump(seed, output, indent=1)
        output.write("\n")

    print(f"skills defined in the chapter: {len(skills)}")
    print(f"source targets: {len(leaves)}")
    print(f"typed exclusions: {len(exclusions)}")
    print(f"ingestion claims: {len(claims)}")
    print(f"marked defective: {list(UNDEFINED_IN_CHAPTER)}")
    print(f"total ops: {len(seed['ops'])} -> {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
