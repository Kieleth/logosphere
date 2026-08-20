import subprocess
import unittest
from pathlib import Path


TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parents[2]
SOURCE_ROOT = ROOT / "examples" / "logovger" / "srd" / "cepheus"
FIXTURE_ROOT = ROOT / "tests" / "fixtures" / "source_partition"


class SourceCheckoutContractTests(unittest.TestCase):
    def test_byte_addressed_markdown_keeps_git_object_line_endings(self):
        sources = sorted(SOURCE_ROOT.rglob("*.md")) + sorted(
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
