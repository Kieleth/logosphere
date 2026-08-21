# A corpus belongs to no game, and neither does the check that its bytes
# survive a checkout. This lived in examples/logovger/tools/ while the
# Cepheus SRD lived inside that game; the two moved out together, and a
# second game reading the same corpus inherits this check instead of
# copying it. It now covers EVERY corpus, not one game's copy of one.
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CORPORA_ROOT = ROOT / "corpora"
FIXTURE_ROOT = ROOT / "tests" / "fixtures" / "source_partition"


class SourceCheckoutContractTests(unittest.TestCase):
    def test_byte_addressed_markdown_keeps_git_object_line_endings(self):
        sources = sorted(CORPORA_ROOT.rglob("*.md")) + sorted(
            FIXTURE_ROOT.rglob("*.md")
        )
        relative = [path.relative_to(ROOT).as_posix() for path in sources]
        self.assertTrue(relative, "repository-backed byte sources are empty")

        checked = subprocess.run(
            ["git", "-C", str(ROOT), "check-attr", "eol", "--", *relative],
            check=True,
            capture_output=True,
            text=True,
        )
        attributes = set(checked.stdout.splitlines())
        for path, name in zip(sources, relative):
            with self.subTest(source=name):
                self.assertIn(
                    f"{name}: eol: lf",
                    attributes,
                    "byte-addressed source must declare eol=lf",
                )
                self.assertNotIn(
                    b"\r\n",
                    path.read_bytes(),
                    "checkout rewrote byte-addressed source to CRLF",
                )


if __name__ == "__main__":
    unittest.main()
