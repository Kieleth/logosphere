#!/usr/bin/env python3
"""Prove scripts/env_gate.py refuses and accepts the right things.

A gate that never fires and a gate that always fires are the same colour
from a green CI run, and this one runs on every ontology regeneration —
including in CI, where a false refusal would stop the lane that writes
committed source. So both directions are asserted, and the refuse cases
use the EXACT version string measured on the dev machine rather than an
invented one.

The last case is the wiring: a gate that generate_ontology.py stopped
calling, or called after importing linkml, would pass every test above
and protect nothing.
"""
import subprocess
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import env_gate  # noqa: E402

REPO = Path(__file__).resolve().parent.parent

# The version the dev machine's editable checkout actually reported, and
# the reason this gate exists.
EDITABLE = "1.10.0.post230.dev0+2909900a4"


class DeclaredFloors(unittest.TestCase):
    """The floors come from environment.yml. No second declaration."""

    def test_reads_the_pins_the_file_declares(self):
        spec = env_gate.declared()
        self.assertEqual(spec["name"], "logosphere")
        self.assertEqual(spec["python"], "3.12")
        self.assertEqual(spec["linkml_floor"], "1.11.0")

    def test_does_not_mistake_linkml_runtime_for_linkml(self):
        # linkml-runtime arrives as a dependency and carries its own
        # version. Reading it as the floor would compare the wrong pin.
        text = (REPO / "environment.yml").read_text()
        self.assertIn("linkml >=1.11.0", text)
        self.assertNotIn("- linkml-runtime", text)


class Accepts(unittest.TestCase):

    def test_the_version_ci_resolves(self):
        self.assertEqual(
            env_gate.problems(linkml_version="1.11.1", python_version="3.12",
                              floor="1.11.0", python_pin="3.12"), [])

    def test_a_later_release(self):
        self.assertEqual(
            env_gate.problems(linkml_version="1.12.0", python_version="3.12",
                              floor="1.11.0", python_pin="3.12"), [])

    def test_a_patch_pinned_python(self):
        self.assertEqual(
            env_gate.problems(linkml_version="1.11.1",
                              python_version="3.12.13",
                              floor="1.11.0", python_pin="3.12"), [])


class Refuses(unittest.TestCase):

    def _one(self, **kwargs):
        found = env_gate.problems(**kwargs)
        self.assertTrue(found, f"expected a refusal for {kwargs}")
        return " ".join(found)

    def test_the_editable_install_this_gate_was_built_for(self):
        # Below the floor AND a development build: both reasons must be
        # named, because fixing only one of them is not a fix.
        text = self._one(linkml_version=EDITABLE, python_version="3.12",
                         floor="1.11.0", python_pin="3.12")
        self.assertIn("BELOW the declared floor", text)
        self.assertIn("development build", text)

    def test_a_dev_build_above_the_floor(self):
        text = self._one(linkml_version="1.12.0.dev5", python_version="3.12",
                         floor="1.11.0", python_pin="3.12")
        self.assertIn("development build", text)
        self.assertNotIn("BELOW the declared floor", text)

    def test_a_local_version(self):
        text = self._one(linkml_version="1.11.1+dirty", python_version="3.12",
                         floor="1.11.0", python_pin="3.12")
        self.assertIn("development build", text)

    def test_linkml_absent(self):
        text = self._one(linkml_version=None, python_version="3.12",
                         floor="1.11.0", python_pin="3.12")
        self.assertIn("not installed", text)
        self.assertIn("linkml-runtime alone", text)

    def test_the_wrong_python(self):
        text = self._one(linkml_version="1.11.1", python_version="3.11",
                         floor="1.11.0", python_pin="3.12")
        self.assertIn("not the declared", text)

    def test_a_missing_floor_is_itself_a_refusal(self):
        text = self._one(linkml_version="1.11.1", python_version="3.12",
                         floor=None, python_pin="3.12")
        self.assertIn("Restore the pin", text)


class Enforcement(unittest.TestCase):
    """The pure check is wired to an exit code and an actionable message."""

    def _run(self, version_literal):
        code = (
            "import sys; sys.path.insert(0, %r); import env_gate; "
            "env_gate.enforce(linkml_version=%s, python_version='3.12')"
            % (str(REPO / "scripts"), version_literal))
        return subprocess.run([sys.executable, "-c", code],
                              capture_output=True, text=True)

    def test_a_good_environment_exits_zero_and_says_nothing(self):
        done = self._run("'1.11.1'")
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(done.stderr.strip(), "")

    def test_the_editable_install_exits_two_with_the_fix(self):
        done = self._run(repr(EDITABLE))
        self.assertEqual(done.returncode, 2)
        self.assertIn("REFUSED TO RUN", done.stderr)
        self.assertIn("conda env create -f environment.yml", done.stderr)
        self.assertIn("conda run -n logosphere", done.stderr)
        self.assertIn("no override", done.stderr)


class Wiring(unittest.TestCase):
    """A gate nothing calls is a comment with a test suite."""

    def test_generate_ontology_enforces_before_it_imports_linkml(self):
        lines = (REPO / "scripts/generate_ontology.py").read_text().splitlines()
        imports = [i for i, l in enumerate(lines)
                   if l.startswith("from env_gate import")]
        calls = [i for i, l in enumerate(lines)
                 if l.strip().startswith("enforce_declared_environment()")]
        linkml = [i for i, l in enumerate(lines)
                  if l.startswith("from linkml")]
        self.assertTrue(imports, "generate_ontology.py does not import the gate")
        self.assertTrue(calls, "generate_ontology.py imports the gate and "
                               "never calls it")
        self.assertTrue(linkml, "generate_ontology.py no longer imports linkml; "
                                "this test needs rewriting, not deleting")
        self.assertLess(calls[0], linkml[0],
                        "the gate must run BEFORE linkml is imported, or the "
                        "refusal arrives as an ImportError traceback instead "
                        "of an instruction")


if __name__ == "__main__":
    unittest.main()
