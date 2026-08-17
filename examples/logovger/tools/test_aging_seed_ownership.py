import json
import os
import unittest


TOOLS = os.path.dirname(__file__)
SEEDS = os.path.abspath(os.path.join(TOOLS, "..", "seeds"))
LEGACY_LOCATORS = {
    "source_file", "source_section", "source_kind", "source_quote",
    "source_table", "source_row", "source_column",
}
AGING_RULE_TYPES = {
    "RuleConstant", "RollableTable", "ModifyAttributesInGroup",
    "OutcomeSequence", "OutcomeStep", "TableEntry", "NoEffect",
}


def load_seed(filename):
    with open(os.path.join(SEEDS, filename), encoding="utf-8") as source:
        return json.load(source)


class AgingSeedOwnershipTests(unittest.TestCase):
    def test_shared_seed_owns_constant_and_exact_aging_rules(self):
        shared = load_seed("cepheus_book1_shared_tables.json")
        careers = load_seed("cepheus_careers.json")

        shared_creates = {
            op.get("as"): op for op in shared["ops"]
            if op.get("op") == "create_entity"
        }
        career_aliases = {
            op.get("as") for op in careers["ops"]
            if op.get("op") == "create_entity"
        }
        self.assertIn("@aging_start_age", shared_creates)
        self.assertNotIn("@aging_start_age", career_aliases)

        aging_rules = [
            op for alias, op in shared_creates.items()
            if (alias == "@aging_table" or alias.startswith("@aging_"))
            and op["type"] in AGING_RULE_TYPES
        ]
        self.assertEqual(len(aging_rules), 30)
        for rule in aging_rules:
            with self.subTest(rule=rule["as"]):
                self.assertTrue(
                    LEGACY_LOCATORS.isdisjoint(rule.get("properties", {})),
                    f"{rule['as']} still has structural locator evidence",
                )


if __name__ == "__main__":
    unittest.main()
