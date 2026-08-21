import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

from extract_career_tables import (assert_canonical_references,
                                   load_career_seed_references,
                                   load_skill_references)
from audit_classifications import seed_classifications


TOOLS = os.path.dirname(__file__)
REPO = os.path.abspath(os.path.join(TOOLS, "..", "..", ".."))
SRD = os.path.join(REPO, "examples", "logovger", "srd", "cepheus")
SOURCE = os.path.join(SRD, "book1", "character-creation.md")
SEEDS = os.path.join(REPO, "examples", "logovger", "seeds")
VOCABULARY = os.path.join(SEEDS, "cepheus_book1_skill_vocabulary.json")
CAREERS = os.path.join(SEEDS, "cepheus_careers.json")
CAREER_TABLES = os.path.join(
    SEEDS, "cepheus_book1_career_tables.json")
MIGRATION_SEEDS = (VOCABULARY, CAREERS, CAREER_TABLES)
LEGACY_LOCATORS = {
    "source_file", "source_section", "source_kind", "source_table",
    "source_row", "source_column", "source_quote",
}
CLAIM_PREFIX = "@career_table_claim_"
# Every CLAIM_MATERIALIZES a Career Tables claim owns, split by what
# kind of claim it is. 1695 -> 1712: seventeen careers print a rank
# ladder a character can climb, and each top rung now also carries the
# reading that the source does not say what advancement does there.
MIGRATED_MATERIALIZATIONS = 1712
CELL_TRANSCRIPTIONS = 1693
RECORDED_READINGS = 19
NAMELESS_LEDGER_TYPES = {
    "ByteRangeSelector", "TextQuoteSelector", "SourceTarget",
    "SourceCoverage", "CoverageDecision", "IngestionClaim",
    "ClaimDecision",
}


def load_seed(path):
    with open(path, encoding="utf-8") as source:
        return json.load(source)


def creates(seed):
    return {
        op["as"]: op
        for op in seed["ops"]
        if op["op"] == "create_entity"
    }


def relations(seed, relation):
    return [
        op for op in seed["ops"]
        if op["op"] == "set_relation" and op["relation"] == relation
    ]


class CareerReferenceTests(unittest.TestCase):
    def test_skill_loader_ignores_ledger_and_relation_operations(self):
        seed = {
            "ops": [
                {"op": "create_entity", "type": "Skill", "as": "@admin",
                 "properties": {"name": "Admin"}},
                {"op": "create_entity", "type": "SourceTarget",
                 "as": "@target", "properties": {
                     "target_primary_selector": "@range"}},
                {"op": "set_relation", "from": "@claim",
                 "relation": "CLAIM_MATERIALIZES", "to": "@admin"},
            ]
        }
        with tempfile.TemporaryDirectory() as directory:
            vocabulary = os.path.join(directory, "skills.json")
            with open(vocabulary, "w", encoding="utf-8") as output:
                json.dump(seed, output)
            references = load_skill_references(
                vocabulary, "ingestion-edition:v1:test")

        self.assertEqual(
            references,
            {"Admin": "@@entity/ingestion-edition%3Av1%3Atest/Skill/admin"},
        )

    def test_skill_loader_names_missing_required_properties(self):
        seed = {
            "ops": [{"op": "create_entity", "type": "Skill",
                     "as": "@admin"}]
        }
        with tempfile.TemporaryDirectory() as directory:
            vocabulary = os.path.join(directory, "skills.json")
            with open(vocabulary, "w", encoding="utf-8") as output:
                json.dump(seed, output)
            with self.assertRaisesRegex(
                    ValueError, r"ops\[0\].*required properties"):
                load_skill_references(
                    vocabulary, "ingestion-edition:v1:test")

    def test_mixed_seed_operations_only_read_supported_entity_creates(self):
        seed = {
            "source": {"file": "book.md", "commit": "abc123"},
            "layer": "cepheus",
            "ops": [
                {"op": "set_relation", "from": "@rules",
                 "relation": "HAS_PART", "to": "@career"},
                {"op": "create_entity", "type": "RuleConstant",
                 "as": "@constant", "properties": {"name": "Constant"}},
                {"op": "create_entity", "type": "Career",
                 "as": "@aerospace_defense",
                 "properties": {"name": "Aerospace Defense"}},
                {"op": "create_entity", "type": "RollableTable",
                 "as": "@aerospace_defense_svc",
                 "properties": {
                     "name": "Aerospace Defense Service Skills"}},
            ],
        }
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "cepheus_careers.json")
            with open(path, "w", encoding="utf-8") as output:
                json.dump(seed, output)
            vocabulary = os.path.join(directory, "skills.json")
            references = load_career_seed_references(
                vocabulary, "ingestion-edition:v1:test")

        self.assertEqual(
            references["Career"],
            {"Aerospace Defense":
             "@@entity/ingestion-edition%3Av1%3Atest/"
             "Career/aerospace_defense"},
        )
        self.assertEqual(
            references["RollableTable"],
            {"Aerospace Defense Service Skills":
             "@@entity/ingestion-edition%3Av1%3Atest/"
             "RollableTable/aerospace_defense_svc"},
        )

    def test_obsolete_reference_is_refused_at_its_exact_path(self):
        with self.assertRaisesRegex(
                ValueError,
                r"seed\.ops\[0\]\.properties\.rollable_table: obsolete"):
            assert_canonical_references({
                "ops": [{"properties": {
                    "rollable_table": "@@RollableTable:Service Skills"}}]
            })

        assert_canonical_references({
            "local": "@table",
            "entity": "@@entity/context/RollableTable/table",
            "meta": "@@meta/context/EntityType/table",
        })

        with self.assertRaisesRegex(ValueError, "document-scoped identity"):
            assert_canonical_references({
                "entity": "@@entity/source-document%3Atest/Type/key",
            })

    def test_every_logovger_seed_uses_canonical_qualified_references(self):
        seed_directory = os.path.abspath(os.path.join(
            os.path.dirname(__file__), "..", "seeds"))
        for filename in sorted(os.listdir(seed_directory)):
            if not filename.endswith(".json"):
                continue
            with self.subTest(seed=filename):
                path = os.path.join(seed_directory, filename)
                with open(path, encoding="utf-8") as source:
                    assert_canonical_references(
                        json.load(source), path=f"seed:{filename}")

    def test_career_table_materializations_use_exact_claims_only(self):
        migrated = []
        for path in MIGRATION_SEEDS:
            seed = load_seed(path)
            entities = creates(seed)
            for entity in entities.values():
                self.assertNotEqual(
                    entity.get("properties", {}).get("source_section"),
                    "Career Tables",
                    f"{path}: {entity['as']} retains Career Tables locator",
                )
            for relation in relations(seed, "CLAIM_MATERIALIZES"):
                if not relation["from"].startswith(CLAIM_PREFIX):
                    continue
                decision = entities[relation["from"] + "_decision"]
                migrated.append((path, relation, decision["properties"]))
                target = entities[relation["to"]]
                self.assertTrue(
                    LEGACY_LOCATORS.isdisjoint(target["properties"]),
                    f"{path}: {relation['to']} mixes exact and legacy evidence",
                )

        self.assertEqual(
            len(migrated), MIGRATED_MATERIALIZATIONS,
            "the migration must retain every previously cited materialization",
        )

        # Two kinds of claim materialize a Career Tables entity, and they
        # are not interchangeable.
        #
        # A TRANSCRIPTION says a cell is in the graph, and there is
        # exactly one per entity: two would mean the migration owned the
        # same cell twice, which is the corruption this check was written
        # to catch.
        #
        # A READING says what the source stops short of, over evidence
        # already transcribed - the undefined Prospecting and Perception
        # skills, and the seventeen rank ladders whose last rung the book
        # never follows anyone past. It is PARTIAL by definition (part of
        # it IS in the graph, and the ledger refuses a PARTIAL claim that
        # materializes nothing), so it shares a target with the
        # transcription of that target. That is the only permitted
        # sharing, and the check below is that it stays the only one.
        transcriptions = [
            (path, relation["to"]) for path, relation, decision in migrated
            if decision["claim_disposition"] == "MATERIALIZED"
        ]
        readings = [
            (path, relation["to"], decision)
            for path, relation, decision in migrated
            if decision["claim_disposition"] != "MATERIALIZED"
        ]
        self.assertEqual(
            len(transcriptions), CELL_TRANSCRIPTIONS,
            "every transcribed Career Tables cell keeps its claim",
        )
        self.assertEqual(
            len(set(transcriptions)), CELL_TRANSCRIPTIONS,
            "each migrated entity has exactly one transcription claim owner",
        )
        self.assertEqual(
            len(readings), RECORDED_READINGS,
            "the readings recorded over Career Tables evidence are these",
        )
        for path, target, decision in readings:
            self.assertEqual(
                decision["claim_disposition"], "PARTIAL",
                f"{path}: {target} shares a target on a non-PARTIAL claim",
            )
            self.assertIn(
                "claim_gap_kind", decision,
                f"{path}: {target}'s reading names no kind of gap",
            )

    def test_career_table_claims_have_converging_exact_support(self):
        with open(SOURCE, "rb") as source:
            source_bytes = source.read()

        target_count = 0
        claim_count = 0
        for path in MIGRATION_SEEDS:
            seed = load_seed(path)
            entities = creates(seed)
            supported = relations(seed, "CLAIM_SUPPORTED_BY")
            support_by_claim = {}
            for relation in supported:
                support_by_claim.setdefault(relation["from"], []).append(
                    relation["to"])

            for alias, entity in entities.items():
                if (entity["type"] == "IngestionClaim" and
                        alias.startswith(CLAIM_PREFIX)):
                    claim_count += 1
                    self.assertTrue(
                        support_by_claim.get(alias),
                        f"{path}: {alias} has no exact source support",
                    )
                if (entity["type"] != "SourceTarget" or
                        not alias.startswith("@career_table_")):
                    continue
                target_count += 1
                properties = entity["properties"]
                range_op = entities[properties["target_primary_selector"]]
                quote_op = entities[properties["target_quote_selector"]]
                start = range_op["properties"]["source_byte_start"]
                end = range_op["properties"]["source_byte_end"]
                self.assertLess(start, end, f"empty range at {alias}")
                self.assertEqual(
                    source_bytes[start:end].decode("utf-8"),
                    quote_op["properties"]["source_quote_exact"],
                    f"quote/range drift at {alias}",
                )

        self.assertGreater(target_count, 900, "the exact migration is vacuous")
        self.assertGreater(claim_count, 900, "the claim migration is vacuous")

    def test_name_invariants_exclude_nameless_ledger_nodes(self):
        for path in (CAREERS, CAREER_TABLES):
            asserted = set(
                load_seed(path)["invariants"]["unique_name_per_type"])
            self.assertTrue(
                NAMELESS_LEDGER_TYPES.isdisjoint(asserted),
                f"{path}: nameless ledger types cannot have name invariants",
            )

    def test_classification_audit_reads_exact_claim_support(self):
        with open(os.path.join(SEEDS, "classification_audit.json"),
                  encoding="utf-8") as source:
            audited = json.load(source)["classifications"]
        self.assertEqual(seed_classifications(load_seed(CAREER_TABLES)),
                         audited)

    def test_undefined_skills_move_to_their_evidence_owners(self):
        vocabulary = creates(load_seed(VOCABULARY))
        careers = creates(load_seed(CAREERS))
        tables_seed = load_seed(CAREER_TABLES)
        tables = creates(tables_seed)

        self.assertNotIn("@sk_perception", vocabulary)
        self.assertNotIn("@sk_prospecting", vocabulary)
        self.assertEqual(careers["@sk_prospecting"]["type"], "Skill")
        self.assertEqual(tables["@sk_perception"]["type"], "Skill")
        self.assertTrue(LEGACY_LOCATORS.isdisjoint(
            careers["@sk_prospecting"]["properties"]))
        self.assertTrue(LEGACY_LOCATORS.isdisjoint(
            tables["@sk_perception"]["properties"]))

        decisions = [
            op["properties"] for op in tables.values()
            if op["type"] == "ClaimDecision"
        ]
        self.assertTrue(any(
            decision.get("claim_disposition") == "DUPLICATE" and
            "prospecting" in decision.get("related_claim", "")
            for decision in decisions
        ), "the later Prospecting occurrence must retain distinct provenance")

    def test_unreviewed_character_creation_bytes_are_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            book = os.path.join(directory, "book1")
            os.makedirs(book)
            shutil.copy(
                os.path.join(SRD, "SOURCE_COMMIT"),
                os.path.join(directory, "SOURCE_COMMIT"),
            )
            shutil.copy(
                os.path.join(SRD, "book1", "skills.md"),
                os.path.join(book, "skills.md"),
            )
            with open(SOURCE, "rb") as source:
                changed = source.read().replace(
                    b"## Career Tables", b"## Changed Career Tables", 1)
            with open(os.path.join(book, "character-creation.md"),
                      "wb") as output:
                output.write(changed)

            result = subprocess.run(
                [sys.executable, os.path.join(TOOLS, "extract_careers.py"),
                 directory, VOCABULARY,
                 os.path.join(directory, "careers.json")],
                capture_output=True,
                text=True,
            )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unreviewed character-creation.md bytes", result.stderr)


if __name__ == "__main__":
    unittest.main()
