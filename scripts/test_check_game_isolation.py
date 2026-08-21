"""The isolation gate fires. Every rule it claims, shown failing.

A gate nobody has watched fail is a gate nobody knows is connected. This
builds a small repository with two games in it, breaks one rule at a
time, and requires the checker to refuse each time and to name what it
found. The clean case is asserted too, because a checker that refuses
everything is as useless as one that refuses nothing.

The compiler pass uses a REAL compiler on a REAL compile_commands.json.
No compiler, no test: skipping it here would be the exact vacuity this
file exists to rule out.
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


CHECKER = Path(__file__).resolve().parent / "check_game_isolation.py"


def _find_compiler():
    """A compiler that actually runs. $CXX is checked and then TRIED,
    because a conda environment can name one it did not install and a
    checker that cannot compile must not report itself proven."""
    for candidate in (os.environ.get("CXX"), "c++", "g++", "clang++"):
        if not candidate:
            continue
        resolved = shutil.which(candidate) or (
            candidate if Path(candidate).is_file() else None)
        if not resolved:
            continue
        try:
            probe = subprocess.run([resolved, "--version"],
                                   capture_output=True, timeout=30)
        except (OSError, subprocess.SubprocessError):
            continue
        if probe.returncode == 0:
            return resolved
    return None


CXX = _find_compiler()


class Fixture:
    """A repository with two games, an engine header, and a corpus."""

    def __init__(self, root):
        self.root = Path(root)
        (self.root / "corpora" / "somebook").mkdir(parents=True)
        (self.root / "corpora" / "somebook" / "chapter.md").write_text("text\n")
        (self.root / "include").mkdir()
        (self.root / "include" / "engine.h").write_text(
            "#pragma once\nint engine_answer();\n")
        for game in ("alpha", "beta"):
            src = self.root / "examples" / game / "src"
            src.mkdir(parents=True)
            (src / f"{game}_app.h").write_text(
                f"#pragma once\nint {game}_answer() {{ return 1; }}\n")
            (src / "main.cpp").write_text(
                f'#include "engine.h"\n#include "{game}_app.h"\n'
                f"int main() {{ return {game}_answer(); }}\n")
            (self.root / "examples" / game / "CMakeLists.txt").write_text(
                "logosphere_game_corpus(%s somebook %s)\n"
                % (game, game.upper()))

    def compile_commands(self, extra_includes=None):
        """One entry per game, engine + own src on the include path."""
        entries = []
        for game in ("alpha", "beta"):
            includes = ["-I", str(self.root / "include"),
                        "-I", str(self.root / "examples" / game / "src")]
            includes += list((extra_includes or {}).get(game, []))
            source = self.root / "examples" / game / "src" / "main.cpp"
            entries.append({
                "directory": str(self.root),
                "file": str(source),
                "arguments": [CXX, *includes, "-c", str(source),
                              "-o", f"{game}.o"],
            })
        path = self.root / "compile_commands.json"
        path.write_text(json.dumps(entries))
        return path


def run_checker(root, compile_commands=None, pair="alpha:beta"):
    argv = [sys.executable, str(CHECKER), "--root", str(root)]
    if pair:
        argv += ["--pair", pair]
    if compile_commands:
        argv += ["--compile-commands", str(compile_commands)]
    return subprocess.run(argv, capture_output=True, text=True)


class TheProofIsPossibleAtAll(unittest.TestCase):
    """A skipped proof of the strongest pass is the vacuity this file
    exists to rule out, so the absence of a compiler is a FAILURE with
    a name, never a quiet skip in a green run."""

    def test_a_compiler_exists_to_prove_the_compiler_pass_with(self):
        self.assertIsNotNone(
            CXX,
            "no C++ compiler on PATH (set CXX). The isolation gate's "
            "strongest pass reads what the compiler opens, and it cannot "
            "be proven without one. An unproven gate must not report "
            "itself green.")


@unittest.skipIf(CXX is None, "no compiler; the failure is reported by "
                              "TheProofIsPossibleAtAll")
class IsolationGateFires(unittest.TestCase):

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.fixture = Fixture(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    # -- the clean case, so a checker that refuses everything is caught --

    def test_two_isolated_games_pass(self):
        result = run_checker(self.fixture.root,
                             self.fixture.compile_commands())
        self.assertEqual(result.returncode, 0,
                         f"clean tree refused:\n{result.stdout}\n{result.stderr}")
        self.assertIn("2 translation unit(s)", result.stdout)

    # -- pass 1: what the compiler opens -------------------------------

    def test_an_include_into_the_other_game_is_caught(self):
        main = self.fixture.root / "examples" / "alpha" / "src" / "main.cpp"
        main.write_text(
            '#include "engine.h"\n'
            '#include "../../beta/src/beta_app.h"\n'
            "int main() { return beta_answer(); }\n")
        result = run_checker(self.fixture.root,
                             self.fixture.compile_commands())
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("opens a file belonging to 'beta'", result.stdout)

    def test_an_unused_include_path_into_the_other_game_is_caught(self):
        # Nothing has reached through it yet. It is still a loaded gun,
        # and the next include costs nobody a build error.
        commands = self.fixture.compile_commands(extra_includes={
            "alpha": ["-I", str(self.fixture.root / "examples" / "beta"
                                / "src")],
        })
        result = run_checker(self.fixture.root, commands)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("include path inside 'beta'", result.stdout)

    def test_the_other_games_source_in_this_games_target_is_caught(self):
        # A header reached through -I, which is how a build file bleeds
        # rather than how a source file does.
        shared = self.fixture.root / "examples" / "beta" / "src" / "shared.h"
        shared.write_text("#pragma once\nint shared_answer() { return 2; }\n")
        main = self.fixture.root / "examples" / "alpha" / "src" / "main.cpp"
        main.write_text('#include "shared.h"\nint main() { return '
                        'shared_answer(); }\n')
        commands = self.fixture.compile_commands(extra_includes={
            "alpha": ["-I", str(self.fixture.root / "examples" / "beta"
                                / "src")],
        })
        result = run_checker(self.fixture.root, commands)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("opens a file belonging to 'beta'", result.stdout)

    def test_compile_commands_with_no_game_translation_unit_refuses(self):
        # The failure mode that makes a green gate meaningless: it ran,
        # it found nothing, and finding nothing is not the same as
        # there being nothing.
        empty = self.fixture.root / "empty_commands.json"
        empty.write_text(json.dumps([]))
        result = run_checker(self.fixture.root, empty)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("proved nothing", result.stdout)

    # -- pass 2: text, for a declared pair ------------------------------

    def test_a_path_into_the_other_game_in_a_string_is_caught(self):
        note = self.fixture.root / "examples" / "alpha" / "src" / "note.cpp"
        note.write_text('const char* p = "examples/beta/seeds/x.json";\n')
        result = run_checker(self.fixture.root,
                             self.fixture.compile_commands())
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("names a path inside 'beta'", result.stdout)

    def test_a_relative_path_into_the_other_game_is_caught(self):
        note = self.fixture.root / "examples" / "alpha" / "src" / "note.cpp"
        note.write_text('const char* p = "../beta/seeds/x.json";\n')
        result = run_checker(self.fixture.root,
                             self.fixture.compile_commands())
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("names a path inside 'beta'", result.stdout)

    def test_naming_the_other_game_at_all_is_caught(self):
        # No path, no include. Just the name, which is how the first
        # reach across always looks.
        note = self.fixture.root / "examples" / "alpha" / "README.md"
        note.write_text("Copied the loader from beta.\n")
        result = run_checker(self.fixture.root,
                             self.fixture.compile_commands())
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("names 'beta'", result.stdout)

    # -- the corpus exception is real, and goes through the mechanism --

    def test_reaching_into_corpora_by_hand_is_caught(self):
        cmake = self.fixture.root / "examples" / "alpha" / "CMakeLists.txt"
        cmake.write_text(
            'target_compile_definitions(alpha PRIVATE\n'
            '    ALPHA_CORPUS_DIR="${CMAKE_SOURCE_DIR}/corpora/somebook")\n')
        result = run_checker(self.fixture.root,
                             self.fixture.compile_commands())
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("reaches into corpora/ by hand", result.stdout)

    def test_the_corpus_itself_is_not_a_cross_game_read(self):
        # Both games read the same corpus, declared. That is the whole
        # point of the exception and it must stay green.
        for game in ("alpha", "beta"):
            note = self.fixture.root / "examples" / game / "src" / "read.cpp"
            note.write_text(
                'const char* corpus = SOMEBOOK_CORPUS_DIR "/chapter.md";\n')
        result = run_checker(self.fixture.root,
                             self.fixture.compile_commands())
        self.assertEqual(result.returncode, 0,
                         f"a shared corpus was refused:\n{result.stdout}")

    # -- a declared pair whose other side does not exist yet ------------

    def test_a_declared_pair_with_an_absent_side_still_passes_and_says_so(self):
        shutil.rmtree(self.fixture.root / "examples" / "beta")
        commands = self.fixture.root / "alpha_only.json"
        source = self.fixture.root / "examples" / "alpha" / "src" / "main.cpp"
        commands.write_text(json.dumps([{
            "directory": str(self.fixture.root),
            "file": str(source),
            "arguments": [CXX,
                          "-I", str(self.fixture.root / "include"),
                          "-I", str(self.fixture.root / "examples" / "alpha"
                                    / "src"),
                          "-c", str(source), "-o", "alpha.o"],
        }]))
        result = run_checker(self.fixture.root, commands)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("does not exist yet", result.stdout)


class TheGateIsWiredToTheRealTree(unittest.TestCase):
    """The declared pair is the one this repository actually needs."""

    def test_the_frozen_module_and_its_successor_are_declared(self):
        sys.path.insert(0, str(CHECKER.parent))
        import check_game_isolation as gate
        declared = {(a, b) for a, b, _ in gate.ISOLATED_PAIRS}
        self.assertIn(
            ("logovger", "voyager"), declared,
            "the frozen Cepheus module and its successor must be a declared "
            "pair; without it the text pass checks nothing that matters")


if __name__ == "__main__":
    unittest.main()
