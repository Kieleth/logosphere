#!/usr/bin/env bash
#
# test-posix-portability.sh — prove check-posix-portability.py is not
# vacuous.
#
# A grep that matches nothing and a grep that matches everything are the
# same colour in CI, and this one runs on every pull request in place of
# a Windows lane that no longer does. So both directions are asserted
# here, on planted files written from scratch:
#
#   MUST FIRE   the four constructs that actually broke headless-windows,
#               each unguarded, exactly as they were when they broke it.
#   MUST BE
#   QUIET       the same four constructs, each behind the guard that
#               fixed it, and once more as dead text inside comments.
#               These are the control: without them, a checker that
#               reports every occurrence would pass the fire cases and
#               be useless.
#
# Prints what it measured. Exit 0 = every case behaved.

set -uo pipefail

CHECKER="$(cd "$(dirname "$0")" && pwd)/check-posix-portability.py"
[ -f "$CHECKER" ] || { echo "FAIL: $CHECKER is missing"; exit 1; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

failures=0

# check <name> <file> <expect: fire|quiet> [substring the report must name]
check () {
    local name="$1" file="$2" expect="$3" needle="${4:-}" out rc verdict
    out="$(python3 "$CHECKER" --files "$TMP/$file" 2>&1)"; rc=$?
    if [ "$rc" -eq 1 ]; then verdict=fire
    elif [ "$rc" -eq 0 ]; then verdict=quiet
    else verdict="crashed(exit $rc)"; fi

    if [ "$verdict" != "$expect" ]; then
        echo "FAIL  $name: expected $expect, got $verdict"
        echo "$out" | sed 's/^/        /'
        failures=$((failures + 1))
        return
    fi
    if [ -n "$needle" ] && ! printf '%s' "$out" | grep -q -- "$needle"; then
        echo "FAIL  $name: fired but the report never names '$needle'"
        echo "$out" | sed 's/^/        /'
        failures=$((failures + 1))
        return
    fi
    echo "PASS  $name: $verdict${needle:+ (report names $needle)}"
}

# ---------------------------------------------------------------- MUST FIRE

cat > "$TMP/fire_execinfo.cpp" <<'EOF'
#include <execinfo.h>
int main() { return 0; }
EOF

cat > "$TMP/fire_setenv.cpp" <<'EOF'
#include <cstdlib>
int main() { setenv("K", "1", 1); unsetenv("K"); return 0; }
EOF

cat > "$TMP/fire_syswait.cpp" <<'EOF'
#include <sys/wait.h>
#include <unistd.h>
int main() { pid_t p = fork(); (void)p; return 0; }
EOF

cat > "$TMP/fire_tmp_path.cpp" <<'EOF'
#include <cstdio>
int main() { FILE* f = fopen("/tmp/logosphere-tape.jsonl", "wb"); return f ? 0 : 1; }
EOF

check "unguarded <execinfo.h> (94aaf2a)"        fire_execinfo.cpp   fire "execinfo.h"
check "unguarded setenv/unsetenv (86a67da)"     fire_setenv.cpp     fire "setenv()"
check "unguarded <sys/wait.h> + fork (86a67da)" fire_syswait.cpp    fire "sys/wait.h"
check "hardcoded /tmp path (run-tape)"          fire_tmp_path.cpp   fire "/tmp/logosphere-tape.jsonl"

# --------------------------------------------------- MUST BE QUIET (control)
# Each is the guard that actually shipped as the fix for the case above.

cat > "$TMP/quiet_execinfo.cpp" <<'EOF'
#if __has_include(<execinfo.h>)
#include <execinfo.h>
#define LOGOSPHERE_HAS_EXECINFO 1
#endif
int main() {
#if defined(LOGOSPHERE_HAS_EXECINFO)
    void* frames[4];
    (void)backtrace(frames, 4);
#endif
    return 0;
}
EOF

cat > "$TMP/quiet_setenv.h" <<'EOF'
#pragma once
#include <cstdlib>
namespace test_env {
inline void set(const char* k, const char* v) {
#ifdef _WIN32
    _putenv_s(k, v);
#else
    setenv(k, v, 1);
#endif
}
inline void unset(const char* k) {
#ifdef _WIN32
    _putenv_s(k, "");
#else
    unsetenv(k);
#endif
}
}  // namespace test_env
EOF

cat > "$TMP/quiet_syswait.cpp" <<'EOF'
#ifdef _WIN32
#include <iostream>
int main() { std::cout << "SKIP: death tests are fork()-based\n"; return 0; }
#else
#include <sys/wait.h>
#include <unistd.h>
int main() { pid_t p = fork(); int s = 0; waitpid(p, &s, 0); return 0; }
#endif
EOF

cat > "$TMP/quiet_tmp_path.cpp" <<'EOF'
#include <filesystem>
#include <string>
// Not "/tmp": that path does not exist on Windows, where the writes
// silently went nowhere. This comment must not be a finding.
std::string tape_path(const char* name) {
    return (std::filesystem::temp_directory_path() / name).string();
}
std::string home_dir() {
#if defined(_WIN32)
    if (const char* p = std::getenv("USERPROFILE")) return p;
    return ".";
#else
    if (const char* h = std::getenv("HOME")) return h;
    return "/tmp";  // harmless fallback
#endif
}
EOF

cat > "$TMP/quiet_portable.cpp" <<'EOF'
#include <chrono>
#include <cstdlib>
#include <thread>
// A file with nothing POSIX in it at all: the flat control.
struct Pipe { void pipe(int) {} };
int main() {
    Pipe p; p.pipe(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return 0;
}
EOF

cat > "$TMP/quiet_commented.cpp" <<'EOF'
/*
 * The history of this file, in a block comment:
 *   #include <execinfo.h>
 *   setenv("K", "1", 1);
 *   fopen("/tmp/old.log", "w");
 * All three are dead text and none of them is a finding.
 */
#include <string>
// #include <sys/wait.h>   was here before the shim
int main() { return 0; }
EOF

check "CONTROL __has_include-guarded execinfo + backtrace" quiet_execinfo.cpp  quiet
check "CONTROL _WIN32/#else setenv shim"                   quiet_setenv.h      quiet
check "CONTROL whole-file _WIN32 SKIP envelope"            quiet_syswait.cpp   quiet
check "CONTROL temp_directory_path + /tmp in a comment"    quiet_tmp_path.cpp  quiet
check "CONTROL portable code, member named pipe()"         quiet_portable.cpp  quiet
check "CONTROL all three constructs, commented out"        quiet_commented.cpp quiet

echo
if [ "$failures" -eq 0 ]; then
    echo "POSIX portability gate: 10/10 cases behaved (fires on all four root causes, quiet on every guard and on dead text)"
    exit 0
fi
echo "POSIX portability gate: $failures case(s) wrong"
exit 1
