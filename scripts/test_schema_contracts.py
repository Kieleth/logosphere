"""The schema contracts that malleus cannot check for us.

Three defect classes, all found on 2026-08-16, all of the same shape: a
contract you opt out of by writing nothing. Malleus's own rites catch
two of them, but only in schemas its inspector can LOAD, and this
repository's packs import by bare name so several of them will not load
without an explicit import map. `ServiceTerm` sat with an open
event_type in exactly that blind spot. So the audit reads the YAML
directly and asks the same questions of every schema, loadable or not.

Run: python -m unittest scripts/test_schema_contracts.py

The audit itself is `scripts/audit_schema_contracts.py`, which is also
runnable by hand for its full report.
"""
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
AUDIT = ROOT / "scripts" / "audit_schema_contracts.py"


def run_audit(root=ROOT):
    result = subprocess.run(
        [sys.executable, str(AUDIT), str(root)],
        capture_output=True, text=True, check=False,
    )
    return result.stdout


class SchemaContracts(unittest.TestCase):
    def test_no_open_discriminators_or_dead_ranges(self):
        """Every schema in the repo is clean, and say so with numbers."""
        out = run_audit()
        header = out.splitlines()[0] if out else "<no output>"
        findings = [l for l in out.splitlines() if l.startswith("[")]
        self.assertIn("schemas read:", header,
                      f"audit produced no header, it probably crashed:\n{out}")
        self.assertEqual(
            [], findings,
            "\n[measure] " + header + "\n" + "\n".join(findings))
        print(f"\n[measure] {header}, 0 findings")

    def test_the_audit_is_not_vacuous(self):
        """A gate that never fires and one that always fires are the
        same colour. Plant each defect in a throwaway tree and require
        the audit to catch it.

        This is the check that matters. The first version of this audit
        crashed on every pack while a grep swallowed the traceback, and
        printed a clean sheet for a repository with two live defects in
        it."""
        import shutil
        import tempfile

        cases = {
            "C open-discriminator": (
                "schema/planted.yaml",
                "id: https://x/planted\nname: planted\n"
                "prefixes:\n  linkml: https://w3id.org/linkml/\n"
                "imports:\n  - linkml:types\n"
                "classes:\n"
                "  Event:\n"
                "  PlantedEvent:\n    is_a: Event\n"
                "    description: unpinned type slot\n",
            ),
            "B uninhabitable-range": (
                "schema/planted.yaml",
                "id: https://x/planted\nname: planted\n"
                "prefixes:\n  linkml: https://w3id.org/linkml/\n"
                "imports:\n  - linkml:types\n"
                "classes:\n"
                "  Entity:\n"
                "  Marker:\n    mixin: true\n"
                "  PlantedThing:\n    is_a: Entity\n"
                "    slot_usage:\n      whatever:\n        range: Marker\n",
            ),
        }

        for expected, (rel, body) in cases.items():
            with tempfile.TemporaryDirectory() as tmp:
                tree = Path(tmp)
                (tree / "schema").mkdir(parents=True)
                (tree / rel).write_text(body)
                out = run_audit(tree)
                self.assertTrue(
                    any(expected in line for line in out.splitlines()),
                    f"audit did not catch a planted {expected}:\n{out}")

        # And the control: the same tree without the defect stays clean.
        with tempfile.TemporaryDirectory() as tmp:
            tree = Path(tmp)
            (tree / "schema").mkdir(parents=True)
            (tree / "schema/clean.yaml").write_text(
                "id: https://x/clean\nname: clean\n"
                "prefixes:\n  linkml: https://w3id.org/linkml/\n"
                "imports:\n  - linkml:types\n"
                "enums:\n  Kind:\n    permissible_values:\n      A:\n"
                "classes:\n"
                "  Event:\n"
                "  CleanEvent:\n    is_a: Event\n"
                "    slot_usage:\n      event_type:\n"
                "        equals_string: A\n")
            out = run_audit(tree)
            findings = [l for l in out.splitlines() if l.startswith("[")]
            self.assertEqual([], findings,
                             f"audit fired on a clean tree:\n{out}")
        print("\n[measure] planted C and B both caught; clean tree stays clean")


if __name__ == "__main__":
    unittest.main()
