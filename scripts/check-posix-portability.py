#!/usr/bin/env python3
"""Refuse POSIX-only constructs in the sources the `core` profile compiles.

WHY THIS EXISTS. `headless-windows` is the lane that proves the core
profile is the portable C++17 it claims to be, and it is also the
slowest lane in the file (25 to 28 minutes measured). It no longer runs
on pull requests. Four of the six root causes it found in a ten-day
window were not subtle behaviour differences — they were constructs
MSVC does not have, visible in the source with no compiler involved:

  <execinfo.h>            94aaf2a, unguarded include in particle_system
  setenv / unsetenv       86a67da, three test binaries
  <sys/wait.h>, fork()    86a67da, the death-test framework
  a literal "/tmp" path   the run-tape test wrote where nothing existed

A grep sees all four in under a second. This is that grep, made precise
enough to run on every pull request without crying wolf.

WHAT IT IS NOT. It does not replace the Windows lane. Two of the six
root causes were real behaviour differences (a destroyed small string
that MSVC zeroes and libc++ does not, and a timeout) and no static
check can see those. The lane still runs on main and nightly.

PRECISION. A construct is a finding only when it is UNGUARDED. These
are all fine and none of them report:

    #if __has_include(<execinfo.h>)          // the header may be absent
    #ifdef _WIN32 ... #else  setenv(...)     // the POSIX branch
    #ifndef _WIN32   fork();                 // same, other spelling
    #define LOGOSPHERE_HAS_X  inside such a region, then #ifdef X

String literals and comments are separated before matching, because
"/tmp" is a finding only inside a literal and `fork()` is a finding
only outside one.

SCOPE. Exactly the translation units the `core` profile configures,
plus every in-repo header they reach. Taken from a configured build's
compile_commands.json rather than from a hand-kept list, so it cannot
go stale when CMakeLists changes.

Usage:
    check-posix-portability.py --compile-commands build/compile_commands.json
    check-posix-portability.py --files a.cpp b.h        # for the self-test

Exit 0 = clean. Exit 1 = findings, one line each. Exit 2 = bad input.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

# --------------------------------------------------------------------------
# The vocabulary. Every entry is something MSVC does not ship under that
# name; nothing here is a style preference.
# --------------------------------------------------------------------------

POSIX_HEADERS = {
    "execinfo.h", "unistd.h", "dirent.h", "pthread.h", "strings.h",
    "libgen.h", "termios.h", "poll.h", "glob.h", "pwd.h", "grp.h",
    "syslog.h", "netdb.h", "fnmatch.h", "ftw.h", "alloca.h", "endian.h",
    "sys/wait.h", "sys/mman.h", "sys/ioctl.h", "sys/socket.h", "sys/un.h",
    "sys/param.h", "sys/resource.h", "sys/select.h", "sys/uio.h",
    "netinet/in.h", "netinet/tcp.h", "arpa/inet.h",
}

# Free functions with no MSVC equivalent under this spelling. Deliberately
# short: a name that MSVC also has (getpid, strdup, access) is not a
# portability break, it is a deprecation warning, and putting it here
# would be the crying-wolf this gate must not do.
POSIX_SYMBOLS = {
    "setenv", "unsetenv",
    "fork", "vfork", "waitpid", "wait3", "wait4",
    "execvp", "execlp", "execve",
    "usleep", "nanosleep", "gettimeofday",
    "mkstemp", "mkdtemp", "realpath",
    "strcasecmp", "strncasecmp",
    "sysconf", "readlink", "symlink", "ftruncate",
    "sigaction", "backtrace", "backtrace_symbols",
}

# A path literal that only exists on a POSIX filesystem. The fix is
# std::filesystem::temp_directory_path().
POSIX_PATH_RE = re.compile(r"^/(tmp|var/tmp|dev/(null|shm|urandom|random))(/|$)")

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')
DIRECTIVE_RE = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif|define)\b(.*)$")
SYMBOL_RE = re.compile(
    r"(?<![\w.>:])(" + "|".join(sorted(POSIX_SYMBOLS)) + r")\s*\(")


# --------------------------------------------------------------------------
# Line splitting: code with literals blanked out, and the literals apart.
# --------------------------------------------------------------------------

def split_line(line: str, in_block_comment: bool):
    """Return (code, literals, still_in_block_comment).

    `code` has every string/char literal replaced by spaces and every
    comment removed, so a symbol match in it is a real call. `literals`
    is the list of string-literal contents, which is where a "/tmp" path
    can hide. Raw strings and escapes are handled; trigraphs are not,
    and nothing in this tree uses them.
    """
    code, literals = [], []
    i, n = 0, len(line)
    while i < n:
        if in_block_comment:
            end = line.find("*/", i)
            if end < 0:
                return "".join(code), literals, True
            i = end + 2
            in_block_comment = False
            continue
        c = line[i]
        two = line[i:i + 2]
        if two == "//":
            break
        if two == "/*":
            in_block_comment = True
            i += 2
            continue
        if c in '"\'':
            quote = c
            j = i + 1
            buf = []
            while j < n:
                if line[j] == "\\" and j + 1 < n:
                    buf.append(line[j + 1])
                    j += 2
                    continue
                if line[j] == quote:
                    break
                buf.append(line[j])
                j += 1
            if quote == '"':
                literals.append("".join(buf))
            code.append(" " * (min(j, n - 1) - i + 1))
            i = j + 1
            continue
        code.append(c)
        i += 1
    return "".join(code), literals, in_block_comment


# --------------------------------------------------------------------------
# Guard tracking: is this line inside a region MSVC never compiles?
# --------------------------------------------------------------------------

WIN32_NEG_RE = re.compile(r"!\s*defined\s*\(\s*_WIN32\s*\)|!\s*defined\s+_WIN32")
WIN32_POS_RE = re.compile(r"(?<![!\w])defined\s*\(?\s*_WIN32")


def _condition_shields(expr: str, gated_macros: set) -> bool:
    """True when a region under `expr` is one MSVC does not compile.

    __has_include counts: the region exists only where the header does.
    """
    if "__has_include" in expr:
        return True
    if WIN32_NEG_RE.search(expr):
        return True
    for macro in gated_macros:
        if re.search(r"(?<![\w])" + re.escape(macro) + r"(?![\w])", expr):
            return True
    return False


def scan_file(path: Path, text: str):
    """Yield (line_no, kind, detail, line) for every UNGUARDED finding."""
    stack = []            # one bool per open conditional: shielded?
    gated_macros = set()  # macros defined only inside a shielded region
    in_block_comment = False

    for line_no, raw in enumerate(text.splitlines(), 1):
        code, literals, in_block_comment = split_line(raw, in_block_comment)

        directive = DIRECTIVE_RE.match(code)
        if directive:
            word, rest = directive.group(1), directive.group(2)
            if word in ("if", "ifdef", "ifndef"):
                if word == "ifndef":
                    shield = (rest.strip().split() or [""])[0] == "_WIN32"
                    shield = shield or _condition_shields(rest, gated_macros)
                elif word == "ifdef":
                    name = (rest.strip().split() or [""])[0]
                    shield = name in gated_macros
                else:
                    shield = _condition_shields(rest, gated_macros)
                stack.append(shield)
                continue
            if word == "elif":
                if stack:
                    stack[-1] = _condition_shields(rest, gated_macros)
                continue
            if word == "else":
                # The other side of `#ifdef _WIN32` is the POSIX side.
                if stack:
                    stack[-1] = not stack[-1]
                continue
            if word == "endif":
                if stack:
                    stack.pop()
                continue
            if word == "define" and any(stack):
                name = (rest.strip().split() or [""])[0]
                if name:
                    gated_macros.add(name)
                continue

        shielded = any(stack)

        include = INCLUDE_RE.match(code)
        if include and include.group(1) in POSIX_HEADERS and not shielded:
            yield (line_no, "header", f"<{include.group(1)}>", raw.strip())
            continue
        if include:
            continue

        if not shielded:
            for match in SYMBOL_RE.finditer(code):
                yield (line_no, "symbol", match.group(1) + "()", raw.strip())
            for literal in literals:
                if POSIX_PATH_RE.match(literal):
                    yield (line_no, "path", f'"{literal}"', raw.strip())


# --------------------------------------------------------------------------
# Scope: what the core profile actually compiles.
# --------------------------------------------------------------------------

def files_from_compile_commands(cc_path: Path, repo_root: Path):
    """Translation units plus every in-repo header they reach."""
    entries = json.loads(cc_path.read_text())
    include_roots, queue = set(), []
    for entry in entries:
        queue.append(Path(entry["file"]).resolve())
        command = entry.get("command") or " ".join(entry.get("arguments", []))
        for token in command.split():
            if token.startswith("-I"):
                include_roots.add(Path(token[2:]).resolve())

    seen = set()
    while queue:
        current = queue.pop()
        if current in seen or not current.is_file():
            continue
        try:
            current.relative_to(repo_root)
        except ValueError:
            continue
        seen.add(current)
        try:
            text = current.read_text(errors="replace")
        except OSError:
            continue
        for line in text.splitlines():
            match = INCLUDE_RE.match(line)
            if not match:
                continue
            for base in [current.parent] + sorted(include_roots):
                candidate = base / match.group(1)
                if candidate.is_file():
                    queue.append(candidate.resolve())
                    break
    return sorted(seen)


FIXES = {
    "header": "guard with `#if __has_include(<...>)` or `#ifdef _WIN32 ... #else`",
    "symbol": "use the portable shim (tests/test_env_portable.h) or guard the block",
    "path": "use std::filesystem::temp_directory_path()",
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compile-commands", type=Path)
    parser.add_argument("--files", type=Path, nargs="*", default=[])
    parser.add_argument("--repo-root", type=Path,
                        default=Path(__file__).resolve().parent.parent)
    args = parser.parse_args()

    repo_root = args.repo_root.resolve()
    if args.compile_commands:
        if not args.compile_commands.is_file():
            print(f"no compile_commands.json at {args.compile_commands}. "
                  f"Configure the core profile first:\n"
                  f"  cmake -S . -B build-portability -DLOGOSPHERE_PROFILE=core"
                  f" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON", file=sys.stderr)
            return 2
        targets = files_from_compile_commands(args.compile_commands, repo_root)
    elif args.files:
        targets = [p.resolve() for p in args.files]
    else:
        print("need --compile-commands or --files", file=sys.stderr)
        return 2

    findings = []
    for path in targets:
        try:
            text = path.read_text(errors="replace")
        except OSError:
            continue
        for line_no, kind, detail, line in scan_file(path, text):
            try:
                shown = path.relative_to(repo_root)
            except ValueError:
                shown = path
            findings.append((str(shown), line_no, kind, detail, line))

    if not findings:
        print(f"POSIX portability: {len(targets)} core-profile files scanned, "
              f"nothing unguarded")
        return 0

    print(f"POSIX portability: {len(findings)} unguarded construct(s) in "
          f"{len(targets)} core-profile files.")
    print("MSVC does not have these, and headless-windows no longer runs on "
          "pull requests, so this is where they get caught.\n")
    for name, line_no, kind, detail, line in findings:
        print(f"  {name}:{line_no}: {detail}")
        print(f"      {line}")
        print(f"      fix: {FIXES[kind]}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
