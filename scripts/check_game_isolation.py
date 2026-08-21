#!/usr/bin/env python3
"""No game compiles another game's files. The vendored corpus is the one
path games share, and sharing it goes through a declaration.

THE RULE, stated once. A game is a directory under `examples/`. Its
source belongs to it. Engine headers are shared because that is what an
engine is; corpora under `corpora/` are shared because they are bytes as
published that nobody transformed. Everything else a game's compiler
opens must be the game's own.

This is a check about the RULE, not about the games that happen to exist
today. Add `examples/whatever/` tomorrow and it is covered the moment it
has a translation unit, with nothing to register and no list to update.

WHY IT EXISTS. Cepheus chargen (`examples/logovger`) is frozen as a
milestone, and its successor will be written beside it rather than on
top of it. "Isolated" is a claim that decays the first time somebody
adds an include, and an include is invisible in a review that is looking
at behaviour. So it is measured.

TWO PASSES.

  1. WHAT THE COMPILER OPENS (`--compile-commands`, repo-wide, every
     game). Each translation unit is re-run through the preprocessor's
     dependency mode; its header closure may not contain a file
     belonging to a different game. This one cannot be talked around: it
     reads what the compiler actually opened, not what the source
     claims. The same pass rejects an `-I` or `-isystem` directory
     inside another game even when nothing has reached through it yet,
     because a loaded gun is worth failing on, and rejects a game's
     source compiled into another game's target.

     Runs with no configuration. A new game is covered automatically.

  2. TEXT, for DECLARED PAIRS (`ISOLATED_PAIRS`). Some pairs need more
     than "the compiler does not follow it": a frozen module and its
     successor must not name each other in a path or a string either,
     because the next person to reach across will do it in a literal
     before they do it in an include.

     This pass is a declared list rather than repo-wide ON PURPOSE, and
     the reason is measured: the examples in this repository cite each
     other deliberately. `logotriste` reads `logotron/src/walls.h` for
     its comment, the newcomer logs quote each other by path, and
     several generated docstrings name `examples/predator`. Forty-odd
     such lines exist and every one of them is a feature. Pass 1 is the
     rule; pass 2 is the extra discipline a declared pair asks for.

     A declared side that does not exist yet is REPORTED AND SKIPPED,
     not a failure. Declaring the pair before the directory exists is
     the point: the rule is in place before there is anything to break
     it, and it starts biting the moment the directory appears.

THE ONE EXCEPTION. `corpora/` holds vendored source text, shared by
whoever cites it. Reading one is not a cross-game read. The check
requires the sharing to go through `logosphere_game_corpus()`, so a game
reaching for vendored bytes by hand fails here rather than quietly
inventing a second mechanism.

Exit 0 clean, 1 with every violation printed.
"""

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

# Pairs held to the stricter text discipline of pass 2, with the reason.
# Symmetric. A side that does not exist yet is skipped, loudly.
ISOLATED_PAIRS = [
    ("logovger", "voyager",
     "Cepheus chargen is frozen as a milestone. Its successor is written "
     "from zero beside it, never from a copy of it, and the only thing the "
     "two may share is the vendored corpus."),
]

# Exceptions to pass 2, keyed (game, the-name-it-may-say), with the
# reason. An exception exists when the name is not only the other
# game's. Never add one to silence a finding; add one when the finding
# is about a word rather than about a dependency.
NAME_EXCEPTIONS = {
    ("logovger", "voyager"):
        "voyager is the name of logovger's OWN ontology layer "
        "(schema/voyager.yaml, namespace voyager::ontology, class Voyager) "
        "and of its lore file. All three predate the game directory that "
        "borrowed the word. Paths are still forbidden, and pass 1 still "
        "forbids opening a file over there.",
}

SKIP_DIRS = {"__pycache__", ".git", "build", "node_modules"}


# ---------------------------------------------------------------------
# Pass 1: what the compiler opens.
# ---------------------------------------------------------------------

def _argv(entry):
    if "arguments" in entry:
        return list(entry["arguments"])
    return shlex.split(entry["command"])


def _preprocess_only(argv):
    """The same command, dependency listing to stdout, no object file."""
    out, skip = [], False
    for arg in argv:
        if skip:
            skip = False
            continue
        if arg == "-o":
            skip = True
            continue
        if arg.startswith("-o") and len(arg) > 2:
            continue
        if arg == "-c":
            continue
        out.append(arg)
    out.append("-MM")
    return out


def _is_under(path, parent):
    try:
        Path(path).resolve().relative_to(Path(parent).resolve())
        return True
    except ValueError:
        return False


def _owning_game(examples, path):
    """The game a file belongs to, or None if it is not a game's."""
    try:
        relative = Path(path).resolve().relative_to(examples.resolve())
    except (ValueError, OSError):
        return None
    return relative.parts[0] if relative.parts else None


def check_compiler_closure(examples, compile_commands, findings):
    entries = json.loads(Path(compile_commands).read_text())
    checked = 0
    games_seen = set()
    for entry in entries:
        source = Path(entry["file"])
        game = _owning_game(examples, source)
        if game is None:
            continue
        games_seen.add(game)
        argv = _argv(entry)

        # A game's source compiled into another game's target. The
        # target name is not authoritative, but a source belonging to a
        # DIFFERENT game than its siblings in the same target is, and
        # that is what the closure below catches for headers. For
        # sources, the object path carries the target.
        for index, arg in enumerate(argv):
            candidates = []
            if arg in ("-I", "-isystem") and index + 1 < len(argv):
                candidates = [argv[index + 1]]
            elif arg.startswith("-I") and len(arg) > 2:
                candidates = [arg[2:]]
            for candidate in candidates:
                resolved = (Path(entry["directory"]) / candidate)
                other = _owning_game(examples, resolved)
                if other is not None and other != game:
                    findings.append(
                        f"{_rel(source)}: '{game}' compiles with an include "
                        f"path inside '{other}': {candidate}")

        result = subprocess.run(_preprocess_only(argv),
                                cwd=entry["directory"],
                                capture_output=True, text=True)
        if result.returncode != 0:
            findings.append(
                f"{_rel(source)}: could not enumerate the header closure, "
                f"so nothing here is proven: "
                f"{result.stderr.strip().splitlines()[-1][:200] if result.stderr.strip() else 'no stderr'}")
            continue
        checked += 1
        for token in re.split(r"\s+", result.stdout.replace("\\\n", " ")):
            token = token.strip()
            if not token or token.endswith(":"):
                continue
            resolved = Path(entry["directory"]) / token
            other = _owning_game(examples, resolved)
            if other is not None and other != game:
                findings.append(
                    f"{_rel(source)}: '{game}' opens a file belonging to "
                    f"'{other}': {token}")
    return checked, sorted(games_seen)


# ---------------------------------------------------------------------
# Pass 2: text, for declared pairs.
# ---------------------------------------------------------------------

def _text_files(game_root):
    for dirpath, dirnames, filenames in os.walk(game_root):
        dirnames[:] = [d for d in sorted(dirnames) if d not in SKIP_DIRS]
        for name in sorted(filenames):
            path = Path(dirpath) / name
            try:
                yield path, path.read_text(encoding="utf-8")
            except (UnicodeDecodeError, OSError):
                continue


def _rel(path):
    try:
        return str(Path(path).resolve().relative_to(ROOT))
    except (ValueError, OSError):
        return str(path)


def check_paths_in_text(examples, game, other, findings):
    named = re.compile(r"examples[/\\]" + re.escape(other) + r"\b")
    relative = re.compile(r"(?:^|[^\w.])\.\.[/\\](?:[\w.\-]+[/\\])*?"
                          + re.escape(other) + r"\b")
    for path, text in _text_files(examples / game):
        for line_no, line in enumerate(text.splitlines(), 1):
            if named.search(line) or relative.search(line):
                findings.append(
                    f"{_rel(path)}:{line_no}: '{game}' names a path inside "
                    f"'{other}': {line.strip()[:110]}")


def check_names_in_text(examples, game, other, findings):
    if (game, other) in NAME_EXCEPTIONS:
        return
    pattern = re.compile(re.escape(other), re.IGNORECASE)
    for path, text in _text_files(examples / game):
        for line_no, line in enumerate(text.splitlines(), 1):
            if pattern.search(line):
                findings.append(
                    f"{_rel(path)}:{line_no}: '{game}' names '{other}': "
                    f"{line.strip()[:110]}")


# ---------------------------------------------------------------------
# The exception has to be real, and has to go through the mechanism.
# ---------------------------------------------------------------------

def check_the_shared_path_is_declared(examples, corpora, findings):
    if not corpora.is_dir():
        return []
    reached_by_hand = []
    for cmake in sorted(examples.glob("*/CMakeLists.txt")):
        text = cmake.read_text(encoding="utf-8")
        if re.search(r"\bcorpora\b", text) and \
                "logosphere_game_corpus" not in text:
            reached_by_hand.append(cmake)
            findings.append(
                f"{_rel(cmake)}: reaches into corpora/ by hand. Declare it: "
                f"logosphere_game_corpus(<target> <corpus> <PREFIX>).")
    return reached_by_hand


def main():
    parser = argparse.ArgumentParser(
        description="No game compiles another game's files.")
    parser.add_argument("--compile-commands",
                        help="compile_commands.json. Enables the pass that "
                             "reads what the compiler actually opens, which "
                             "is the one that cannot be talked around.")
    parser.add_argument("--root", default=None,
                        help="repository root to inspect (the gate's own "
                             "test uses this)")
    parser.add_argument("--pair", action="append", default=None,
                        metavar="A:B",
                        help="check this text pair instead of the declared "
                             "ones (the gate's own test uses this)")
    args = parser.parse_args()

    global ROOT
    if args.root:
        ROOT = Path(args.root).resolve()
    examples = ROOT / "examples"
    corpora = ROOT / "corpora"

    if not examples.is_dir():
        print(f"no examples/ under {ROOT}", file=sys.stderr)
        return 1

    if args.pair:
        pairs = [(*spec.split(":", 1), "declared on the command line")
                 for spec in args.pair]
    else:
        pairs = ISOLATED_PAIRS

    findings = []
    absent = []
    for a, b, _reason in pairs:
        for game, other in ((a, b), (b, a)):
            if not (examples / game).is_dir():
                absent.append(game)
                continue
            check_paths_in_text(examples, game, other, findings)
            check_names_in_text(examples, game, other, findings)
    check_the_shared_path_is_declared(examples, corpora, findings)

    checked, games_seen = 0, []
    if args.compile_commands:
        checked, games_seen = check_compiler_closure(
            examples, args.compile_commands, findings)
        if checked == 0:
            findings.append(
                "no translation unit under examples/ was enumerated, so the "
                "compiler pass proved nothing. A gate that checks nothing "
                "passes for the wrong reason.")

    if findings:
        print("GAMES ARE NOT ISOLATED:")
        for finding in findings:
            print("  " + finding)
        print(f"\n{len(findings)} violation(s). A game compiles its own "
              f"files, the engine's, and the corpora it declares. Nothing "
              f"else.")
        return 1

    if args.compile_commands:
        print(f"compiler: {checked} translation unit(s) across "
              f"{len(games_seen)} game(s) ({', '.join(games_seen)}); none "
              f"opens a file belonging to another game, and none carries an "
              f"include path into one")
    else:
        print("compiler: SKIPPED. Pass --compile-commands for the pass that "
              "reads what the compiler actually opens.")
    for a, b, reason in pairs:
        missing = [side for side in (a, b) if side in absent]
        note = ""
        if missing:
            note = (f"  [{' and '.join(missing)} does not exist yet; the "
                    f"rule is in place for when it does]")
        print(f"text pair: {a} <-> {b}{note}")
        print(f"           {reason}")
    for (game, other), reason in sorted(NAME_EXCEPTIONS.items()):
        if any(game in pair[:2] and other in pair[:2] for pair in pairs):
            print(f"  declared exception: '{game}' may say '{other}'")
            print(f"    {reason}")
    if corpora.is_dir():
        shared = sorted(p.name for p in corpora.iterdir() if p.is_dir())
        print(f"shared:   corpora/ only ({', '.join(shared)}), and only "
              f"through logosphere_game_corpus()")
    return 0


if __name__ == "__main__":
    sys.exit(main())
