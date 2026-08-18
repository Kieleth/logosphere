#!/usr/bin/env python3
"""Read the Cepheus career tables into a seed, verbatim and addressed.

The career tables are laid out in four blocks of six careers, and each
block restates its column headers for every sub-table it contains. That
shape is why a regex over the whole chapter got this wrong once: the
sub-tables are only distinguishable by their own header rows, so this
reads them structurally instead, and every string it writes is the
exact bytes from the source.

WHAT IS MECHANICAL AND WHAT IS NOT
    Transcription is mechanical, because a model that misreads a number
    produces a rule that is wrong in a way the prose still reads fine.
    Meaning is separate: each distinct cell value is classified by its
    FORM (a number is money, "+1 Int" is a characteristic, an em dash
    is no effect, a named thing is a possession, anything else is a
    skill), and every skill it produces is then resolved against the
    vocabulary seed. Nothing that fails to resolve is guessed at; it is
    reported and the run fails.

    That check is not decoration. It is what found Perception and
    Prospecting, two skills the career tables grant that the SRD never
    defines.

Every entity carries a cell citation: file, heading trail, table, row,
column and the exact text, so the verifier can resolve it back and
prove it.

Usage:
    python3 examples/logovger/tools/extract_career_tables.py \
        examples/logovger/srd/cepheus \
        examples/logovger/seeds/cepheus_book1_skill_vocabulary.json \
        examples/logovger/seeds/<out>.json
"""
from collections import defaultdict
import hashlib
import json
import os
import re
import sys
from urllib.parse import quote

SECTION = "Career Tables"
CHAPTER = "book1/character-creation.md"
GENERATED_BY = "examples/logovger/tools/extract_career_tables.py"
REVIEWED_SOURCE_SHA256 = (
    "aec772042129694210d60204031829618a8c71f863d3b381ce8689be60bd14c7"
)
ARBITER = "Codex, owner-directed Career Tables migration"
LEGACY_SOURCE_FIELDS = {
    "source_file", "source_section", "source_kind", "source_table",
    "source_row", "source_column", "source_quote",
}
LEDGER_ENTITY_TYPES = {
    "ByteRangeSelector", "TextQuoteSelector", "SourceTarget",
    "SourceCoverage", "CoverageDecision", "IngestionClaim",
    "ClaimDecision",
}

# The sub-tables inside a career block, and what each row means. The
# second block titles its cash table "Cost Benefits" where the other
# three say "Cash Benefits"; both are transcribed as printed, because a
# citation that does not match the source is not a citation. Reported
# upstream at orffen/cepheus-srd#36.
# Service Skills is absent deliberately: cepheus_careers.json already
# owns those 24 tables. Two seeds creating the same named table is the
# duplicate-Skill mistake in a new costume, and the loader refuses it.
SKILL_TABLES = ("Personal Development", "Specialist", "Adv Education")
CASH_TABLES = ("Cash Benefits", "Cost Benefits")
MATERIAL_TABLES = ("Material Benefits",)
RANK_TABLES = ("Ranks and Skills",)

# Rows of the block's own header table that are throws.
CHECK_ROWS = {
    "Qualifications": "qualification",
    "Survival": "survival",
    "Commission": "commission",
    "Advancement": "advancement",
    "Re-enlistment": "reenlistment",
}

ATTRIBUTES = {
    "Str": "strength", "Dex": "dexterity", "End": "endurance",
    "Int": "intelligence", "Edu": "education", "Soc": "social_standing",
}

# Named things the benefits tables hand out that are neither money nor
# a skill. The book describes most of them in prose under Material
# Benefits; that description is attached where it exists.
POSSESSIONS = ("Low Passage", "Mid Passage", "High Passage", "Weapon",
               "Explorers' Society", "Ship Shares", "Courier Vessel",
               "Research Vessel")

EM_DASH = "—"

# A table is not a cell, so it cannot be addressed as one. Each family
# of tables cites the sentence that says how it is used; the ROWS carry
# the cell citations. This is the pattern the careers seed already
# follows, and citing a table with an empty row instead is how the
# first attempt failed verification 24 times over.
TABLE_CITATIONS = {
    "skill": ("Skills and Training",
              "In each term you spend in a career, pick one of these "
              "tables and roll 1D6 to see which skill you increase."),
    "benefit": ("Mustering Out Benefits",
                "Characters who end their careers receive one benefit "
                "per term served in which they did not lose benefits."),
    "rank": ("Commission and Advancement",
             "You also get any benefits listed for your new rank."),
}

# Advanced Education is the one skill table a character can be barred
# from, and the sentence that bars them is not the one that introduces
# the tables. It has to be the table's own citation, because the value
# verifier proves requires_minimum = 8 against the quote the entity
# carries, and the general sentence prints no 8.
ADV_EDUCATION_GATE = (
    "Skills and Training",
    "You may only roll on the Advanced Education table if your "
    "character has Education 8+.",
    "education", "8")

# A cell that says "Carousing" states a skill, not a level. The LEVELS
# come from the checklist sentence that sets them, so that is what an
# AdvanceSkill built from such a cell cites; the TableEntry above it
# still carries the cell address. The careers seed established this,
# and the value verifier enforces it: every number on an entity must
# appear in the text that entity quotes.
# Each per-career throw table belongs to the rule that consults it.
RULE_CITATIONS = {
    "commission": (
        "Commission and Advancement",
        "Within military careers, a Commission check represents an "
        "opportunity to join the ranks of the commissioned officers."),
    "advancement": (
        "Commission and Advancement",
        "Each career that has a commission check also has an "
        "Advancement roll, representing your character's ability to "
        "advance with the ranks of your chosen career's hierarchy."),
    "reenlistment": (
        "Reenlistment and Retirement",
        "If continuation is desired, the character must make a "
        "successful Reenlistment check as listed for their current "
        "profession or service."),
}

SKILL_LEVEL_SECTION = "Character Creation Checklist"
SKILL_LEVEL_QUOTE = (
    "If you gain a skill as a result and you do not already have levels "
    "in that skill, take it at level 1.")

# Misspellings the source PROVES: for each, the skills chapter defines
# exactly one near-identical skill and no other candidate exists, so
# the intent is not in question. The outcome points at the real skill
# while the citation still quotes the cell verbatim, and the divergence
# is recorded on the entity rather than quietly smoothed away. Same
# standard as the Slug Rifle correction: correct at the source only
# where the source proves the correction, otherwise mark and ask.
PROVEN_TYPOS = {
    "Liaision": ("Liaison",
                 "The source writes 'Liaision-1' in the Colonist rank 3 "
                 "cell. 'Liaision' occurs exactly once in the whole SRD; "
                 "the skills chapter defines 'Liaison' and nothing else "
                 "resembles it."),
    "Pilot": ("Piloting",
              "The source writes 'Pilot-1' in three rank cells. The "
              "skills chapter has no 'Pilot' entry; it defines "
              "'Piloting', and no other skill resembles it."),
}

PERCEPTION_LOCATOR = ("Specialist", "3", "Bureaucrat", "Perception")
PROSPECTING_SERVICE_LOCATOR = (
    "Service Skills", "5", "Belter", "Prospecting")
PROSPECTING_SPECIALIST_LOCATOR = (
    "Specialist", "4", "Belter", "Prospecting")
PERCEPTION_DEFECT = (
    "Granted by the Bureaucrat Specialist table but defined nowhere: "
    "'Perception' occurs exactly once in the SRD and has no entry in "
    "book1/skills.md nor a line in the Available Skills List.")
PERCEPTION_READING = (
    "Recon is the nearest defined skill, but treating this cell as Recon "
    "would be a guess, so the graph retains Perception as a source gap.")


def slug(text):
    return re.sub(r"[^a-z0-9]+", "_", text.lower()).strip("_")


def cells(line):
    if not line.startswith("|"):
        return None
    return [c.strip() for c in line.rstrip("\n").split("|")[1:-1]]


def cell_records(line, line_byte_start):
    """Return stripped Markdown cells and their exact UTF-8 byte ranges."""
    text = line.rstrip("\r\n")
    if not text.startswith("|") or not text.endswith("|"):
        return None
    pipes = [index for index, char in enumerate(text) if char == "|"]
    records = []
    for left_pipe, right_pipe in zip(pipes, pipes[1:]):
        raw = text[left_pipe + 1:right_pipe]
        left = len(raw) - len(raw.lstrip())
        right = len(raw.rstrip())
        begin_char = left_pipe + 1 + left
        end_char = left_pipe + 1 + right
        records.append({
            "text": text[begin_char:end_char],
            "start": line_byte_start + len(text[:begin_char].encode("utf-8")),
            "end": line_byte_start + len(text[:end_char].encode("utf-8")),
        })
    return records


def is_separator(row):
    return all(set(c) <= set("-: ") for c in row)


# Every title that starts a sub-table inside a career block. Detecting
# boundaries by "the header row is restated" fails on the first one,
# because the block header writes "Aerospace Defense" while its
# sub-tables write "Aerospace", so the columns do not match and three
# whole tables get swallowed as rows. The titles are the reliable
# signal, and getting this wrong is silent: the counts simply come out
# short.
TABLE_TITLES = frozenset(
    ("Career", "Service Skills") + SKILL_TABLES + CASH_TABLES +
    MATERIAL_TABLES + RANK_TABLES)


def read_tables(path):
    """Every pipe table in the chapter, with its header and rows."""
    with open(path, "rb") as source:
        source_bytes = source.read()
    encoded_lines = source_bytes.splitlines(keepends=True)
    lines = [line.decode("utf-8") for line in encoded_lines]
    offsets, cursor = [], 0
    for line in encoded_lines:
        offsets.append(cursor)
        cursor += len(line)
    if not encoded_lines or not encoded_lines[-1].endswith((b"\n", b"\r")):
        offsets.append(cursor)
    out, i = [], 0
    while i < len(lines):
        head_records = cell_records(lines[i], offsets[i])
        head = [record["text"] for record in head_records or ()]
        if not head or len(head) < 2:
            i += 1
            continue
        rows, row_spans, j = [], [], i + 1
        while j < len(lines):
            records = cell_records(lines[j], offsets[j])
            row = [record["text"] for record in records or ()]
            if not row or len(row) != len(head):
                break
            if is_separator(row):
                j += 1
                continue
            # A known title in the first cell starts the next
            # sub-table, whether or not its columns are spelled the
            # same as this one's.
            if row[0] in TABLE_TITLES:
                break
            rows.append(row)
            row_spans.append(records)
            j += 1
        if rows:
            out.append({"title": head[0], "columns": head[1:],
                        "rows": rows, "line": i + 1,
                        "title_span": head_records[0],
                        "column_spans": head_records[1:],
                        "row_spans": row_spans})
        i = j if rows else i + 1
    return out


def validate_character_creation(path):
    with open(path, "rb") as source:
        source_bytes = source.read()
    digest = hashlib.sha256(source_bytes).hexdigest()
    if digest != REVIEWED_SOURCE_SHA256:
        raise ValueError(
            "unreviewed character-creation.md bytes: expected sha256 "
            f"{REVIEWED_SOURCE_SHA256}, got {digest}")
    return source_bytes


def qualified_ref(context_key, type_name, entity_key):
    """Build the engine's canonical, percent-encoded entity reference."""
    safe = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._~-"
    parts = (context_key, type_name, entity_key)
    if any(not part or part in (".", "..") for part in parts):
        raise ValueError("qualified reference segments must be non-empty")
    return "@@entity/" + "/".join(quote(part, safe=safe) for part in parts)


def load_skill_references(vocabulary_path, context_key):
    """Canonical skill references, indexed by every source-proven name."""
    with open(vocabulary_path, encoding="utf-8") as source:
        seed = json.load(source)
    names = {}
    for index, op in enumerate(seed["ops"]):
        if op.get("op") != "create_entity" or op.get("type") != "Skill":
            continue
        entity_key = op.get("as", "").removeprefix("@")
        if not entity_key:
            raise ValueError(
                f"Skill vocabulary ops[{index}] is missing its create alias")
        props = op.get("properties")
        if not isinstance(props, dict):
            raise ValueError(
                f"Skill vocabulary ops[{index}] is missing required "
                "properties")
        name = props.get("name")
        if not name:
            raise ValueError(
                f"Skill vocabulary ops[{index}] is missing required name")
        reference = qualified_ref(context_key, op["type"], entity_key)
        names[name] = reference
        for alias in filter(None, props.get("source_aliases", "").split("; ")):
            names[alias] = reference
    return names


def load_career_seed_references(vocabulary_path, context_key):
    """Canonical Career-seed references indexed by type and exact name."""
    career_path = os.path.join(
        os.path.dirname(vocabulary_path), "cepheus_careers.json")
    with open(career_path, encoding="utf-8") as source:
        seed = json.load(source)
    references = {"Career": {}, "RollableTable": {}, "Skill": {}}
    for op in seed["ops"]:
        type_name = op.get("type")
        if op.get("op") != "create_entity" or type_name not in references:
            continue
        entity_key = op.get("as", "").removeprefix("@")
        if not entity_key:
            raise ValueError(
                f"Career seed {type_name} is missing its create alias")
        name = op["properties"]["name"]
        if name in references[type_name]:
            raise ValueError(
                f"Career seed has duplicate {type_name} name {name!r}")
        references[type_name][name] = qualified_ref(
            context_key, type_name, entity_key)
    return references


def load_career_seed_coverages(vocabulary_path, context_key):
    """Exact Career-seed coverages indexed by canonical byte range."""
    career_path = os.path.join(
        os.path.dirname(vocabulary_path), "cepheus_careers.json")
    with open(career_path, encoding="utf-8") as source:
        seed = json.load(source)
    entities = {
        op["as"]: op for op in seed["ops"]
        if op.get("op") == "create_entity"
    }
    ranges = {}
    for alias, entity in entities.items():
        if entity.get("type") != "SourceCoverage":
            continue
        target = entities[entity["properties"]["coverage_target"]]
        selector = entities[target["properties"]["target_primary_selector"]]
        properties = selector["properties"]
        key = (properties["source_byte_start"],
               properties["source_byte_end"])
        if key in ranges:
            raise ValueError(
                f"Career seed has duplicate SourceCoverage range {key}")
        ranges[key] = qualified_ref(
            context_key, "SourceCoverage", alias.removeprefix("@"))
    return ranges


class CareerTableLedger:
    """Replace Career Tables cell locators with exact, reviewed claims."""

    def __init__(self, source_bytes, tables, prior_coverages=None):
        self.source_bytes = source_bytes
        self.prior_coverages = prior_coverages or {}
        self.coverages = {}
        self.locators = defaultdict(list)
        for table in tables:
            for row, spans in zip(table["rows"], table["row_spans"]):
                for index, column in enumerate(table["columns"]):
                    locator = (table["title"], row[0], column,
                               row[index + 1])
                    evidence = (table["title_span"],
                                table["column_spans"][index],
                                spans[0], spans[index + 1])
                    self.locators[locator].append(evidence)

    @staticmethod
    def locator(properties):
        required = ("source_table", "source_row", "source_column",
                    "source_quote")
        missing = [name for name in required if not properties.get(name)]
        if missing:
            raise ValueError(
                "Career Tables cell locator is missing required fields: " +
                ", ".join(missing))
        if properties.get("source_kind") != "cell":
            raise ValueError(
                "Career Tables exact migration only accepts cell locators")
        return tuple(properties[name] for name in required)

    def evidence_for(self, locator):
        matches = self.locators.get(locator, ())
        if len(matches) != 1:
            raise ValueError(
                f"Career Tables locator {locator!r} resolved to "
                f"{len(matches)} source cells; expected exactly one")
        unique = []
        seen = set()
        for span in matches[0]:
            key = (span["start"], span["end"])
            if key not in seen:
                unique.append(span)
                seen.add(key)
        return unique

    @staticmethod
    def claim_alias(evidence, suffix=""):
        value = evidence[-1]
        return (f"@career_table_claim_b{value['start']}_e{value['end']}"
                f"{suffix}")

    def coverage_for(self, ops, span):
        key = (span["start"], span["end"])
        if key in self.prior_coverages:
            return self.prior_coverages[key]
        if key in self.coverages:
            return self.coverages[key]
        base = f"@career_table_b{span['start']}_e{span['end']}"
        range_alias = base + "_range"
        quote_alias = base + "_quote"
        target_alias = base + "_target"
        coverage_alias = base + "_coverage"
        exact = self.source_bytes[span["start"]:span["end"]].decode("utf-8")
        if exact != span["text"] or not exact:
            raise ValueError(
                f"Career Tables byte range {key} does not match its quote")
        ops.extend([
            {"op": "create_entity", "type": "ByteRangeSelector",
             "as": range_alias, "properties": {
                 "source_byte_start": span["start"],
                 "source_byte_end": span["end"]}},
            {"op": "create_entity", "type": "TextQuoteSelector",
             "as": quote_alias,
             "properties": {"source_quote_exact": exact}},
            {"op": "create_entity", "type": "SourceTarget",
             "as": target_alias, "properties": {
                 "target_primary_selector": range_alias,
                 "target_quote_selector": quote_alias}},
            {"op": "create_entity", "type": "SourceCoverage",
             "as": coverage_alias,
             "properties": {"coverage_target": target_alias}},
            {"op": "create_entity", "type": "CoverageDecision",
             "as": base + "_coverage_decision", "properties": {
                 "event_type": "ARBITER_DECISION",
                 "decision_subject": coverage_alias,
                 "decision_sequence": 0,
                 "coverage_judgement": "CLAIMS_PRESENT",
                 "decision_question":
                     "Does this exact table cell state or support a rule?",
                 "decision_reason":
                     "The reviewed cell supplies table context or value.",
                 "arbiter": ARBITER}},
        ])
        self.coverages[key] = coverage_alias
        return coverage_alias

    def append_claim(self, ops, locator, statement, disposition,
                     materializes=(), suffix="", gap_kind=None,
                     related_claim=None):
        evidence = self.evidence_for(locator)
        supports = [self.coverage_for(ops, span) for span in evidence]
        claim = self.claim_alias(evidence, suffix)
        ops.append({"op": "create_entity", "type": "IngestionClaim",
                    "as": claim,
                    "properties": {"claim_statement": statement}})
        decision = {
            "event_type": "ARBITER_DECISION",
            "decision_subject": claim,
            "decision_sequence": 0,
            "claim_disposition": disposition,
            "decision_question": "Can this table claim enter the typed graph?",
            "decision_reason": (
                "The reviewed cell is represented by the listed typed "
                "entities." if disposition == "MATERIALIZED" else
                "The decision records the source defect without guessing."),
            "arbiter": ARBITER,
        }
        if gap_kind:
            decision["claim_gap_kind"] = gap_kind
        if related_claim:
            decision["related_claim"] = related_claim
        ops.append({"op": "create_entity", "type": "ClaimDecision",
                    "as": claim + "_decision", "properties": decision})
        for support in supports:
            ops.append({"op": "set_relation", "from": claim,
                        "relation": "CLAIM_SUPPORTED_BY", "to": support})
        for entity in materializes:
            ops.append({"op": "set_relation", "from": claim,
                        "relation": "CLAIM_MATERIALIZES", "to": entity})
        return claim

    def migrate(self, ops):
        groups = defaultdict(list)
        for operation in ops:
            if operation.get("op") != "create_entity":
                continue
            properties = operation.get("properties", {})
            if properties.get("source_section") != SECTION:
                continue
            locator = self.locator(properties)
            groups[locator].append(operation["as"])
            for field in LEGACY_SOURCE_FIELDS:
                properties.pop(field, None)

        for locator, aliases in groups.items():
            table, row, column, value = locator
            self.append_claim(
                ops, locator,
                f"{table} row {row}, column {column} states {value!r}.",
                "MATERIALIZED", materializes=aliases)
        return sum(len(aliases) for aliases in groups.values())


def assert_canonical_references(value, path="seed"):
    """Refuse obsolete typed references before a generated seed is written."""
    if isinstance(value, dict):
        for key, child in value.items():
            assert_canonical_references(child, f"{path}.{key}")
    elif isinstance(value, list):
        for index, child in enumerate(value):
            assert_canonical_references(child, f"{path}[{index}]")
    elif isinstance(value, str) and value.startswith("@@") and not value.startswith(
            ("@@entity/", "@@meta/")):
        raise ValueError(
            f"{path}: obsolete qualified reference {value!r}; expected "
            "@@entity/... or @@meta/...")
    elif isinstance(value, str) and value.startswith(
            "@@entity/source-document"):
        raise ValueError(
            f"{path}: legacy document-scoped identity {value!r}; expected "
            "the exact ingestion edition")


class Builder:
    def __init__(self, skills, careers, service_tables, context_key):
        self.skills = skills
        self.careers = careers
        self.service_tables = service_tables
        self.context_key = context_key
        self.ops = []
        self.unresolved = []
        self.possessions = {}
        self.pending_rows = []

    def add(self, type_name, alias, props):
        self.ops.append({"op": "create_entity", "type": type_name,
                         "as": alias, "properties": props})
        return alias

    def cite(self, table, row, column, quote):
        return {"source_file": CHAPTER, "source_section": SECTION,
                "source_kind": "cell", "source_table": table,
                "source_row": row, "source_column": column,
                "source_quote": quote}

    def ref(self, type_name, entity_key):
        return qualified_ref(self.context_key, type_name, entity_key)

    def service_table(self, career):
        name = f"{career} Service Skills"
        try:
            return self.service_tables[name]
        except KeyError:
            raise ValueError(
                f"Career seed has no RollableTable named {name!r}") from None

    def possession(self, name):
        if name not in self.possessions:
            alias = "@poss_" + slug(name)
            self.possessions[name] = alias
        return self.possessions[name]

    def outcome_for(self, value, table, row, column, tag):
        """One cell, classified by form. Returns an alias or None."""
        citation = self.cite(table, row, column, value)

        if value == EM_DASH or value == "-":
            return self.add("NoEffect", f"@{tag}_none",
                            dict(name=f"{tag}: nothing", **citation))

        if re.fullmatch(r"\d+", value):
            return self.add("GainFixedMoney", f"@{tag}_cash", dict(
                name=f"{tag}: Cr{value}", amount=value,
                currency=self.ref("Currency", "credits"), **citation))

        modifier = re.fullmatch(r"\+(\d+) (\w+)", value)
        if modifier and modifier.group(2) in ATTRIBUTES:
            return self.add("ModifyAttribute", f"@{tag}_attr", dict(
                name=f"{tag}: {value}",
                attribute_ref=ATTRIBUTES[modifier.group(2)],
                attribute_delta=modifier.group(1), **citation))

        rolled = re.fullmatch(r"(\d+)D(\d+) (.+)", value)
        if rolled and rolled.group(3) in POSSESSIONS:
            return self.add("GainPossession", f"@{tag}_poss", dict(
                name=f"{tag}: {value}",
                possession=self.possession(rolled.group(3)),
                possession_count_dice=self.ref(
                    "DiceExpression",
                    "d" + rolled.group(1) + "d" + rolled.group(2)),
                **citation))

        if value in POSSESSIONS:
            # No count: the book prints a bare name, and an explicit 1
            # would be a number the source never wrote. Absent means
            # one, and the schema says so.
            return self.add("GainPossession", f"@{tag}_poss", dict(
                name=f"{tag}: {value}", possession=self.possession(value),
                **citation))

        if value in self.skills:
            return self.add("AdvanceSkill", f"@{tag}_skill", dict(
                name=f"{tag}: {value}",
                skill=self.skills[value],
                initial_skill_level="1", existing_skill_delta="1",
                source_file=CHAPTER, source_section=SKILL_LEVEL_SECTION,
                source_kind="sentence", source_quote=SKILL_LEVEL_QUOTE))

        self.unresolved.append((value, table, row, column))
        return None


def main():
    root, vocabulary_path, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
    chapter = os.path.join(root, "book1", "character-creation.md")
    try:
        source_bytes = validate_character_creation(chapter)
    except ValueError as error:
        print(f"REFUSED: {error}", file=sys.stderr)
        return 1
    commit = open(os.path.join(root, "SOURCE_COMMIT"),
                  encoding="utf-8").read().strip()
    from rule_source_identity import ingestion_edition_context_key
    context_key = ingestion_edition_context_key(root)

    tables = read_tables(chapter)
    career_seed = load_career_seed_references(vocabulary_path, context_key)
    skills = load_skill_references(vocabulary_path, context_key)
    skills.update(career_seed["Skill"])
    skills["Perception"] = "@sk_perception"
    builder = Builder(skills,
                      career_seed["Career"], career_seed["RollableTable"],
                      context_key)
    builder.add("Skill", "@sk_perception", {
        "name": "Perception",
        "source_defect": PERCEPTION_DEFECT,
        "suggested_reading": PERCEPTION_READING,
    })
    # The Draft table is ALSO titled "Career", with columns "Roll of
    # 4+" and friends. A block header is identified by what it
    # contains, not by its title: only a career block has a
    # Qualifications row.
    def is_block_header(table):
        return (table["title"] == "Career" and
                any(row[0] == "Qualifications" for row in table["rows"]))

    ALL_CAREERS = []
    for table in tables:
        if is_block_header(table):
            ALL_CAREERS.extend(table["columns"])

    counts = {"skill_table": 0, "cash": 0, "material": 0, "rank": 0,
              "check": 0}

    # A block's own header names careers in full ("Aerospace
    # Defense"); its sub-tables abbreviate ("Aerospace"). The columns
    # line up by position, so the header is what says WHICH career a
    # sub-table column is, while citations keep quoting the short form
    # the page actually prints.
    block_careers = []

    for table in tables:
        title, columns = table["title"], table["columns"]
        if is_block_header(table):
            block_careers = list(columns)
        canonical = {printed: (block_careers[i] if i < len(block_careers)
                               else printed)
                     for i, printed in enumerate(columns)}

        if title in SKILL_TABLES or title in CASH_TABLES or \
                title in MATERIAL_TABLES:
            for index, career in enumerate(columns):
                tag = slug(f"{career} {title}")
                entries = []
                for row in table["rows"]:
                    key, value = row[0], row[index + 1]
                    outcome = builder.outcome_for(
                        value, title, key, career, f"{tag}_{key}")
                    if outcome is None:
                        continue
                    entries.append((key, outcome, value))
                if not entries:
                    continue
                family = "skill" if title in SKILL_TABLES else "benefit"
                section, quote = TABLE_CITATIONS[family]
                gate = {}
                if title == "Adv Education":
                    section, quote, attribute, minimum = ADV_EDUCATION_GATE
                    gate = dict(requires_attribute=attribute,
                                requires_minimum=minimum)
                builder.add("RollableTable", f"@{tag}", dict(
                    name=f"{canonical[career]} {title}",
                    dice=builder.ref("DiceExpression", "d1d6"),
                    source_file=CHAPTER, source_section=section,
                    source_kind="sentence", source_quote=quote, **gate))
                # The training rule owns which tables a career offers,
                # for the same reason the throw rules do: a career is
                # created by an earlier seed and cannot be written to.
                if title in CASH_TABLES or title in MATERIAL_TABLES:
                    brow = f"@benefit_opt_{slug(canonical[career])}_{slug(title)}"
                    bsection, bquote = TABLE_CITATIONS["benefit"]
                    # Which KIND of benefit table this is, so the rule
                    # that caps cash rolls asks the row instead of
                    # matching the table's printed name. The book prints
                    # one of them as "Cost Benefits" and the rest as
                    # "Cash Benefits"; both are the cash table, and a
                    # substring match on either was the whole mechanism.
                    role = "cash" if title in CASH_TABLES else "material"
                    builder.add("CareerTableEntry", brow, dict(
                        name=f"{title} for {canonical[career]}",
                        subject=builder.careers[canonical[career]],
                        rollable_table=f"@{tag}", table_role=role,
                        source_file=CHAPTER, source_section=bsection,
                        source_kind="sentence", source_quote=bquote))
                    builder.ops.append({"op": "set_relation",
                                        "from": "@benefit_tables",
                                        "relation": "HAS_PART", "to": brow})
                if title in SKILL_TABLES:
                    # What a career OFFERS is proven by the sentence that
                    # hands every career the four tables, including for
                    # Adv Education, whose own citation proves the gate
                    # rather than the offer.
                    tsection, tquote = TABLE_CITATIONS["skill"]
                    row = f"@training_opt_{slug(canonical[career])}_{slug(title)}"
                    builder.add("CareerTableEntry", row, dict(
                        name=f"{title} for {canonical[career]}",
                        subject=builder.careers[canonical[career]],
                        rollable_table=f"@{tag}",
                        source_file=CHAPTER, source_section=tsection,
                        source_kind="sentence", source_quote=tquote))
                    builder.ops.append({"op": "set_relation",
                                        "from": "@training_tables",
                                        "relation": "HAS_PART",
                                        "to": row})
                for key, outcome, value in entries:
                    builder.add("TableEntry", f"@{tag}_e{key}", dict(
                        name=f"{canonical[career]} {title} {key}",
                        roll_min=key, roll_max=key, outcome=outcome,
                        **builder.cite(title, key, career, value)))
                    # A row belongs to its table. Creating the entries
                    # and never relating them leaves 96 tables that
                    # roll on nothing.
                    builder.ops.append({"op": "set_relation",
                                        "from": f"@{tag}",
                                        "relation": "HAS_PART",
                                        "to": f"@{tag}_e{key}"})
                counts["skill_table" if title in SKILL_TABLES else
                       ("cash" if title in CASH_TABLES else "material")] += 1

        elif title in RANK_TABLES:
            for index, career in enumerate(columns):
                # Keyed by the career's full name, because the
                # promotion rows in the block header use that and both
                # must name the same ladder.
                tag = slug(f"{canonical[career]} ranks")
                section, quote = TABLE_CITATIONS["rank"]
                # The promotion rules need to find a career's ladder,
                # and a career cannot be written to, so the mapping is
                # owned by the rule like every other one here.
                builder.add("ProgressionTrack", f"@{tag}", dict(
                    name=f"{canonical[career]} ranks",
                    source_file=CHAPTER, source_section=section,
                    source_kind="sentence", source_quote=quote))
                for row in table["rows"]:
                    key, value = row[0], row[index + 1]
                    if value in (EM_DASH, "-", ""):
                        continue
                    grant, title_text = None, value
                    bracket = re.search(r"\\\[(.+?)\\\]", value)
                    if bracket:
                        skill_text = bracket.group(1)
                        title_text = value[:bracket.start()].strip()
                        name = skill_text.rsplit("-", 1)[0]
                        level = skill_text.rsplit("-", 1)[-1]
                        divergence = None
                        if name in PROVEN_TYPOS:
                            name, divergence = PROVEN_TYPOS[name]
                        if name in builder.skills:
                            grant = builder.add(
                                "AdvanceSkill", f"@{tag}_{key}_grant", dict(
                                    name=f"{career} rank {key}: {skill_text}",
                                    skill=builder.skills[name],
                                    initial_skill_level=level,
                                    existing_skill_delta="1",
                                    **builder.cite(title, key, career, value),
                                    **({"source_defect": divergence,
                                        "suggested_reading":
                                            "Read as " + name + "."}
                                       if divergence else {})))
                        else:
                            builder.unresolved.append(
                                (name, title, key, career))
                    step = dict(name=f"{career} rank {key}", step_index=key,
                                **builder.cite(title, key, career, value))
                    if title_text:
                        step["step_title"] = title_text
                    if grant:
                        step["grants"] = grant
                    builder.add("ProgressionStep", f"@{tag}_s{key}", step)
                    builder.ops.append({"op": "set_relation",
                                        "from": f"@{tag}",
                                        "relation": "HAS_PART",
                                        "to": f"@{tag}_s{key}"})
                # The promotion rules need to find a career's ladder,
                # and a career cannot be written to, so the mapping is
                # owned by the rule like every other one here.
                track_row = f"@rank_track_{slug(canonical[career])}"
                builder.add("CareerTrackEntry", track_row, dict(
                    name=f"ranks for {canonical[career]}",
                    subject=builder.careers[canonical[career]],
                    track=f"@{tag}",
                    source_file=CHAPTER, source_section=section,
                    source_kind="sentence", source_quote=quote))
                builder.ops.append({"op": "set_relation",
                                    "from": "@rank_tracks",
                                    "relation": "HAS_PART",
                                    "to": track_row})
                counts["rank"] += 1

        elif is_block_header(table):
            for row in table["rows"]:
                row_label = row[0]
                kind = CHECK_ROWS.get(row[0])
                if kind not in ("commission", "advancement", "reenlistment"):
                    continue          # qualification and survival are seeded
                for index, career in enumerate(columns):
                    value = row[index + 1]
                    if value in (EM_DASH, "-", ""):
                        continue
                    props = dict(
                        name=f"{career} {kind}",
                        dice=builder.ref("DiceExpression", "d2d6"),
                        **builder.cite(title, row[0], career, value))
                    throw = re.fullmatch(r"(\w+) (\d+)\+", value)
                    flat = re.fullmatch(r"(\d+)\+", value)
                    if throw and throw.group(1) in ATTRIBUTES:
                        props["attribute_ref"] = ATTRIBUTES[throw.group(1)]
                        props["target_number"] = throw.group(2)
                        props["modifier_table"] = builder.ref(
                            "LookupTable", "dm_table")
                        props["modifier_property"] = "characteristic_modifier"
                    elif flat:
                        # Re-enlistment is printed as a bare "6+": a
                        # throw no characteristic modifies.
                        props["target_number"] = flat.group(1)
                    else:
                        builder.unresolved.append((value, title, row[0],
                                                   career))
                        continue
                    alias = builder.add(
                        "TaskCheck", f"@{slug(career)}_{kind}", props)
                    # The RULE owns the mapping. A career carries no
                    # pointer to its throw: the rule's table names the
                    # career and the throw it makes. Careers come from
                    # an earlier seed, and a seed may point at what
                    # another owns but never write onto it.
                    throw_row = f"@{kind}_row_{slug(career)}"
                    props_row = dict(
                        name=f"{kind} throw for {career}",
                        subject=builder.careers[career],
                        throw_check=alias,
                        **builder.cite(title, row_label, career, value))
                    if kind in ("commission", "advancement"):
                        props_row["track"] = f"@{slug(career)}_ranks"
                    builder.pending_rows.append((kind, throw_row, props_row))
                    counts["check"] += 1

    # Emitted here, after every rank ladder exists, because a promotion
    # row names the ladder it moves you along.
    for kind, alias, props in builder.pending_rows:
        builder.add("CareerThrowEntry", alias, props)
        builder.ops.append({"op": "set_relation", "from": f"@{kind}_table",
                            "relation": "HAS_PART", "to": alias})

    # One table per rule, created before its rows reference it.
    # A rule-owned table cites the rule that owns it. Addressing the
    # Career table's Commission row instead is ambiguous by four: the
    # chapter prints one Career table per block of six careers, and
    # each has that row. The ROWS below carry the unambiguous cell
    # addresses, column included.
    # The Service Skills tables belong to the careers seed, so they are
    # referenced by name rather than re-created; the other three are
    # created above. All four are options on the same rule.
    for career in ALL_CAREERS:
        row = f"@training_opt_{slug(career)}_service_skills"
        section, quote = TABLE_CITATIONS["skill"]
        # The one training table a rule singles out: "you get every
        # skill in the SERVICE SKILLS table at level 0". Basic training
        # found it by comparing the last fourteen characters of the
        # table's name.
        builder.add("CareerTableEntry", row, dict(
            name=f"Service Skills for {career}",
            subject=builder.careers[career],
            rollable_table=builder.service_table(career),
            table_role="service",
            source_file=CHAPTER, source_section=section,
            source_kind="sentence", source_quote=quote))
        builder.ops.append({"op": "set_relation", "from": "@training_tables",
                            "relation": "HAS_PART", "to": row})

    section, quote = TABLE_CITATIONS["benefit"]
    builder.ops.insert(0, {
        "op": "create_entity", "type": "SubjectLookupTable",
        "as": "@benefit_tables",
        "properties": dict(
            name="benefit table by career",
            source_file=CHAPTER, source_section=section,
            source_kind="sentence", source_quote=quote)})

    section, quote = TABLE_CITATIONS["rank"]
    builder.ops.insert(0, {
        "op": "create_entity", "type": "SubjectLookupTable",
        "as": "@rank_tracks",
        "properties": dict(
            name="rank track by career",
            source_file=CHAPTER, source_section=section,
            source_kind="sentence", source_quote=quote)})

    section, quote = TABLE_CITATIONS["skill"]
    builder.ops.insert(0, {
        "op": "create_entity", "type": "SubjectLookupTable",
        "as": "@training_tables",
        "properties": dict(
            name="training table by career",
            source_file=CHAPTER, source_section=section,
            source_kind="sentence", source_quote=quote)})

    for kind, (section, quote) in RULE_CITATIONS.items():
        builder.ops.insert(0, {
            "op": "create_entity", "type": "SubjectLookupTable",
            "as": f"@{kind}_table",
            "properties": dict(
                name=f"{kind} throw by career",
                source_file=CHAPTER, source_section=section,
                source_kind="sentence", source_quote=quote)})

    # Possessions last, so only the ones actually referenced exist.
    for name, alias in sorted(builder.possessions.items()):
        builder.ops.insert(0, {
            "op": "create_entity", "type": "Possession", "as": alias,
            "properties": dict(
                name=name,
                source_file=CHAPTER, source_section="Material Benefits",
                source_kind="sentence",
                source_quote="Material benefits may be characteristics "
                             "alterations, passages or ship shares.")})

    if builder.unresolved:
        print("REFUSED: these cells resolve to nothing, and guessing at "
              "them is how a rulebook silently becomes fiction:")
        for value, table, row, column in builder.unresolved[:20]:
            print(f"  {value!r}  ({table} row {row}, column {column})")
        return 1

    ledger = CareerTableLedger(
        source_bytes, tables,
        load_career_seed_coverages(vocabulary_path, context_key))
    migrated = ledger.migrate(builder.ops)
    ledger.append_claim(
        builder.ops, PERCEPTION_LOCATOR,
        "The Bureaucrat Specialist table grants a Perception skill that "
        "the skills chapter does not define.",
        "PARTIAL", materializes=("@sk_perception",),
        suffix="_perception_undefined_skill", gap_kind="SOURCE_GAP")
    prior_prospecting = ledger.claim_alias(
        ledger.evidence_for(PROSPECTING_SERVICE_LOCATOR),
        "_prospecting_undefined_skill")
    ledger.append_claim(
        builder.ops, PROSPECTING_SPECIALIST_LOCATOR,
        "The Belter Specialist table repeats the undefined Prospecting "
        "skill already granted by Service Skills.",
        "DUPLICATE", suffix="_prospecting_undefined_skill_duplicate",
        related_claim=qualified_ref(
            context_key, "IngestionClaim",
            prior_prospecting.removeprefix("@")))

    seed = {
        "source": {"file": CHAPTER, "commit": commit},
        "layer": "cepheus",
        "generated_by": GENERATED_BY,
        # Counts, asserted. A str.replace that matched nothing once
        # left all three rule tables with zero rows, and everything
        # still verified: empty tables break no citation. The invariant
        # is what turns that silence into a failure.
        "invariants": {"count_of_type": {
                           type_name: sum(
                               op.get("op") == "create_entity" and
                               op.get("type") == type_name
                               for op in builder.ops)
                           for type_name in sorted({
                               op["type"] for op in builder.ops
                               if op.get("op") == "create_entity"})},
                       "unique_name_per_type": ["RollableTable", "TaskCheck",
                                                "ProgressionTrack",
                                                "SubjectLookupTable",
                                                "Possession"]},
        "ops": builder.ops,
    }
    assert_canonical_references(seed)
    json.dump(seed, open(out_path, "w"), indent=1)
    open(out_path, "a").write("\n")

    print(f"skill tables:    {counts['skill_table']}")
    print(f"cash tables:     {counts['cash']}")
    print(f"material tables: {counts['material']}")
    print(f"rank tracks:     {counts['rank']}")
    print(f"checks:          {counts['check']}")
    print(f"possessions:     {len(builder.possessions)}")
    print(f"exact Career Tables materializations: {migrated + 1}")
    print(f"total ops:       {len(builder.ops)} -> {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
