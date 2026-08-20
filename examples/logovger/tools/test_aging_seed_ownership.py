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

    def test_general_crisis_claims_own_repeated_evidence_and_rules(self):
        tables = load_seed("cepheus_book1_tables.json")
        careers = load_seed("cepheus_careers.json")
        procedure = load_seed("cepheus_basic_chargen_procedure.json")

        table_creates = {
            op.get("as"): op for op in tables["ops"]
            if op.get("op") == "create_entity"
        }
        career_aliases = {
            op.get("as") for op in careers["ops"]
            if op.get("op") == "create_entity"
        }
        procedure_creates = {
            op.get("as"): op for op in procedure["ops"]
            if op.get("op") == "create_entity"
        }
        self.assertIn("@crisis_restore_value", table_creates)
        self.assertNotIn("@crisis_restore_value", career_aliases)
        self.assertTrue(LEGACY_LOCATORS.isdisjoint(
            table_creates["@crisis_restore_value"].get("properties", {})))
        self.assertTrue(LEGACY_LOCATORS.isdisjoint(
            procedure_creates["@aging_crisis_unpaid"].get("properties", {})))

        relations = [
            op for op in tables["ops"] if op.get("op") == "set_relation"
        ]
        evidence = {
            "@crisis_death_claim": {
                "@injury_crisis_medical_coverage",
                "@aging_crisis_medical_coverage",
            },
            "@crisis_cost_claim": {
                "@injury_crisis_medical_coverage",
                "@aging_crisis_medical_coverage",
            },
            "@crisis_restore_claim": {
                "@injury_crisis_medical_coverage",
                "@aging_crisis_medical_coverage",
            },
            "@crisis_qualification_claim": {
                "@injury_crisis_qualification_coverage",
                "@aging_crisis_qualification_coverage",
            },
            "@crisis_career_claim": {
                "@injury_crisis_qualification_coverage",
                "@aging_crisis_qualification_coverage",
            },
        }
        for claim, expected in evidence.items():
            with self.subTest(claim=claim):
                actual = {
                    op["to"] for op in relations
                    if op["from"] == claim
                    and op["relation"] == "CLAIM_SUPPORTED_BY"
                }
                self.assertEqual(actual, expected)

        for old, replacement in {
            "death": "death", "cost": "cost", "restore": "restore",
            "qualification": "qualification", "career": "career",
        }.items():
            subject = f"@injury_crisis_{old}_claim"
            decisions = [
                op for op in table_creates.values()
                if op["type"] == "ClaimDecision"
                and op["properties"].get("decision_subject") == subject
            ]
            decisions.sort(key=lambda op: op["properties"]["decision_sequence"])
            with self.subTest(superseded=subject):
                self.assertEqual(len(decisions), 2)
                self.assertEqual(
                    decisions[-1]["properties"]["claim_disposition"],
                    "SUPERSEDED",
                )
                self.assertEqual(
                    decisions[-1]["properties"]["related_claim"],
                    f"@crisis_{replacement}_claim",
                )


if __name__ == "__main__":
    unittest.main()
