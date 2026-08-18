import json
import os
import tempfile
import unittest

from extract_career_tables import (assert_canonical_references,
                                   load_career_seed_references)


class CareerReferenceTests(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
