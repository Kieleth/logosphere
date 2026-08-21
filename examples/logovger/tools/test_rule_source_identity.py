import os
import unittest

from rule_source_identity import ingestion_edition_context_key


class RuleSourceIdentityTests(unittest.TestCase):
    def test_vendored_production_corpus_has_the_committed_edition_identity(self):
        # The corpus is vendored outside every game, so this walks up
        # to the repository root rather than to the game directory.
        source_root = os.path.abspath(os.path.join(
            os.path.dirname(__file__), "..", "..", "..",
            "corpora", "cepheus-srd"))
        self.assertEqual(
            ingestion_edition_context_key(source_root),
            "ingestion-edition:v1:7:cepheus:sha256:"
            "76ecc50c30ab15fccef9e21ff22db79883d916dffe4006ee7c6c1bd51946dd85",
        )


if __name__ == "__main__":
    unittest.main()
