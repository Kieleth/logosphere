import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest


TOOLS = os.path.dirname(__file__)
REPO = os.path.abspath(os.path.join(TOOLS, "..", "..", ".."))
SRD = os.path.join(REPO, "corpora", "cepheus-srd")
SOURCE = os.path.join(SRD, "book1", "skills.md")
SEED = os.path.join(
    REPO, "examples", "logovger", "seeds",
    "cepheus_book1_skill_vocabulary.json")
LEGACY_LOCATORS = {
    "source_file", "source_section", "source_kind", "source_table",
    "source_row", "source_column", "source_quote",
}
UNDEFINED_SKILLS = {"Perception", "Prospecting"}


def load_seed(path=SEED):
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


class SkillExtractionTests(unittest.TestCase):
    def test_generated_seed_is_the_committed_seed(self):
        with tempfile.TemporaryDirectory() as directory:
            generated = os.path.join(directory, "skills.json")
            subprocess.run(
                [sys.executable, os.path.join(TOOLS, "extract_skills.py"),
                 SRD, generated],
                check=True,
                capture_output=True,
                text=True,
            )
            with open(generated, "rb") as actual, open(SEED, "rb") as expected:
                self.assertEqual(actual.read(), expected.read())

    def test_unreviewed_source_bytes_are_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            book = os.path.join(directory, "book1")
            os.makedirs(book)
            shutil.copy(
                os.path.join(SRD, "SOURCE_COMMIT"),
                os.path.join(directory, "SOURCE_COMMIT"),
            )
            with open(SOURCE, "rb") as source:
                changed = source.read().replace(b"# Chapter 2: Skills",
                                                b"# Chapter Two: Skills", 1)
            with open(os.path.join(book, "skills.md"), "wb") as output:
                output.write(changed)
            result = subprocess.run(
                [sys.executable, os.path.join(TOOLS, "extract_skills.py"),
                 directory, os.path.join(directory, "skills.json")],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("unreviewed skills.md bytes", result.stderr)

    def test_seed_asserts_an_exact_complete_source_partition(self):
        seed = load_seed()
        entities = creates(seed)
        partitions = [
            op for op in entities.values()
            if op["type"] == "CompleteSourcePartition"
        ]
        self.assertEqual(
            len(partitions), 1,
            "skills.md must opt into the shipped exact-cover gate once")

        with open(SOURCE, "rb") as source:
            source_bytes = source.read()

        intervals = []
        targets = [
            op for op in entities.values() if op["type"] == "SourceTarget"
        ]
        exclusions = [
            op for op in entities.values()
            if op["type"] == "SourceExclusion"
        ]
        self.assertTrue(targets, "a complete partition needs semantic leaves")
        self.assertTrue(exclusions, "Markdown syntax/layout must stay visible")

        for target in targets:
            props = target["properties"]
            range_op = entities[props["target_primary_selector"]]
            quote_op = entities[props["target_quote_selector"]]
            self.assertEqual(range_op["type"], "ByteRangeSelector")
            self.assertEqual(quote_op["type"], "TextQuoteSelector")
            start = range_op["properties"]["source_byte_start"]
            end = range_op["properties"]["source_byte_end"]
            self.assertLess(start, end, f"empty target {target['as']}")
            self.assertEqual(
                source_bytes[start:end].decode("utf-8"),
                quote_op["properties"]["source_quote_exact"],
                f"quote/range drift at {target['as']}",
            )
            intervals.append((start, end, target["as"]))

        for exclusion in exclusions:
            props = exclusion["properties"]
            self.assertIn(props["exclusion_kind"], {"SYNTAX", "LAYOUT"})
            range_op = entities[props["exclusion_selector"]]
            self.assertEqual(range_op["type"], "ByteRangeSelector")
            start = range_op["properties"]["source_byte_start"]
            end = range_op["properties"]["source_byte_end"]
            self.assertLess(start, end, f"empty exclusion {exclusion['as']}")
            intervals.append((start, end, exclusion["as"]))

        cursor = 0
        for start, end, alias in sorted(intervals):
            self.assertEqual(start, cursor, f"gap or overlap before {alias}")
            cursor = end
        self.assertEqual(cursor, len(source_bytes))

    def test_every_leaf_has_one_current_coverage_judgement(self):
        seed = load_seed()
        entities = creates(seed)
        targets = {
            alias for alias, op in entities.items()
            if op["type"] == "SourceTarget"
        }
        self.assertTrue(targets, "coverage cannot pass vacuously with no leaves")
        coverages = {
            alias: op for alias, op in entities.items()
            if op["type"] == "SourceCoverage"
        }
        coverage_by_target = {}
        for alias, coverage in coverages.items():
            target = coverage["properties"]["coverage_target"]
            self.assertNotIn(target, coverage_by_target)
            coverage_by_target[target] = alias
        self.assertEqual(set(coverage_by_target), targets)

        supported = relations(seed, "CLAIM_SUPPORTED_BY")
        claim_count = {}
        for relation in supported:
            claim_count[relation["to"]] = claim_count.get(relation["to"], 0) + 1

        decisions = {}
        for alias, decision in entities.items():
            if decision["type"] != "CoverageDecision":
                continue
            subject = decision["properties"]["decision_subject"]
            decisions.setdefault(subject, []).append(decision)

        for coverage in coverages:
            history = sorted(
                decisions.get(coverage, []),
                key=lambda op: op["properties"]["decision_sequence"],
            )
            self.assertTrue(history, f"missing decision for {coverage}")
            self.assertEqual(
                [op["properties"]["decision_sequence"] for op in history],
                list(range(len(history))),
            )
            judgement = history[-1]["properties"]["coverage_judgement"]
            if claim_count.get(coverage, 0):
                self.assertEqual(judgement, "CLAIMS_PRESENT")
            else:
                self.assertEqual(judgement, "NO_RULE_CONTENT")

    def test_chapter_skills_use_claims_not_legacy_locators(self):
        seed = load_seed()
        entities = creates(seed)
        skills = {
            alias: op for alias, op in entities.items()
            if op["type"] == "Skill"
        }
        self.assertTrue(
            UNDEFINED_SKILLS.isdisjoint(
                skill["properties"]["name"] for skill in skills.values()),
            "skills.md cannot own terms found only in character-creation.md",
        )
        materializations = relations(seed, "CLAIM_MATERIALIZES")
        materialized_by = {}
        for relation in materializations:
            materialized_by.setdefault(relation["to"], []).append(
                relation["from"])

        for alias, skill in skills.items():
            name = skill["properties"]["name"]
            self.assertTrue(
                LEGACY_LOCATORS.isdisjoint(skill["properties"]),
                f"{name} retains a legacy source locator",
            )
            self.assertEqual(
                len(materialized_by.get(alias, [])), 1,
                f"{name} must be materialized by exactly one reviewed claim",
            )


if __name__ == "__main__":
    unittest.main()
