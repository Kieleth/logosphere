#!/usr/bin/env bash
#
# test-sanitizer-audit.sh — prove check-sanitizer-audit.py is not vacuous.
#
# The gate it guards is "red means new". A gate that accepts everything
# would restore exactly the situation it was built to end: nine reds and
# no way to see the tenth. A gate that accepts nothing would make the
# lane permanently red again, which is the same thing wearing the other
# colour. So both directions are asserted, plus the case a checker is
# most likely to get wrong: nothing to read at all.
#
#   CASE 1  the audited set, exactly            -> QUIET
#   CASE 2  a tenth test fails                  -> FIRE, and name it
#   CASE 3  an audited test stops failing       -> FIRE, and name it
#   CASE 4  every test passes                   -> FIRE (nine went quiet)
#   CASE 5  no LastTestsFailed.log at all       -> FIRE, exit 2, not clean
#   CASE 6  an audit row with expect != fail    -> FIRE, exit 2
#
# CASE 1 is built FROM the real tests/invariants/SANITIZER_AUDIT.jsonl,
# so this also proves the shipped file parses and carries the shape the
# checker requires.
#
# Prints what it measured. Exit 0 = every case behaved.

set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
CHECKER="$HERE/check-sanitizer-audit.py"
AUDIT="$(cd "$HERE/.." && pwd)/tests/invariants/SANITIZER_AUDIT.jsonl"
[ -f "$CHECKER" ] || { echo "FAIL: $CHECKER is missing"; exit 1; }
[ -f "$AUDIT" ]   || { echo "FAIL: $AUDIT is missing"; exit 1; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# The audited names, in CTest's own LastTestsFailed.log format.
python3 - "$AUDIT" "$TMP/audited.log" <<'PY'
import json, sys
rows = [json.loads(l) for l in open(sys.argv[1]) if l.strip()]
with open(sys.argv[2], "w") as out:
    for i, r in enumerate(rows, 1):
        out.write(f"{i}:{r['test']}\n")
print(f"[fixture] {len(rows)} audited findings read from the shipped file")
PY

AUDITED_COUNT="$(wc -l < "$TMP/audited.log" | tr -d ' ')"
FIRST_AUDITED="$(head -1 "$TMP/audited.log" | cut -d: -f2)"

failures=0

# check <name> <log-arg> <expect: quiet|fire|unreadable> [needle] [audit override]
check () {
    local name="$1" log="$2" expect="$3" needle="${4:-}" audit="${5:-$AUDIT}"
    local out rc verdict
    out="$(python3 "$CHECKER" --last-failed "$log" --audit "$audit" 2>&1)"; rc=$?
    case "$rc" in
        0) verdict=quiet ;;
        1) verdict=fire ;;
        2) verdict=unreadable ;;
        *) verdict="crashed(exit $rc)" ;;
    esac
    if [ "$verdict" != "$expect" ]; then
        echo "FAIL  $name: expected $expect, got $verdict"
        echo "$out" | sed 's/^/        /'
        failures=$((failures + 1))
        return
    fi
    if [ -n "$needle" ] && ! printf '%s' "$out" | grep -q -- "$needle"; then
        echo "FAIL  $name: $verdict, but the report never names '$needle'"
        echo "$out" | sed 's/^/        /'
        failures=$((failures + 1))
        return
    fi
    echo "PASS  $name: $verdict${needle:+ (names $needle)}"
}

# CASE 1 — the audited set, exactly.
check "the audited $AUDITED_COUNT, exactly" "$TMP/audited.log" quiet

# CASE 2 — a tenth. This is the whole point.
cp "$TMP/audited.log" "$TMP/plus_one.log"
echo "99:test_a_brand_new_memory_error" >> "$TMP/plus_one.log"
check "a tenth test fails" "$TMP/plus_one.log" fire "test_a_brand_new_memory_error"

# CASE 3 — an audited finding goes quiet. Direction-locked.
grep -v ":$FIRST_AUDITED\$" "$TMP/audited.log" > "$TMP/minus_one.log"
check "an audited finding stops reporting" "$TMP/minus_one.log" fire "$FIRST_AUDITED"

# CASE 4 — everything green. Looks like success; is a change.
: > "$TMP/all_green.log"
check "every test passes" "$TMP/all_green.log" fire "WENT QUIET"

# CASE 5 — the log is missing. A lane that reported nothing is not clean.
check "no LastTestsFailed.log" "$TMP/absent.log" unreadable "NOT a clean run"

# CASE 6 — an audit row that is not an accepted red.
python3 - "$AUDIT" "$TMP/bad_audit.jsonl" <<'PY'
import json, sys
rows = [json.loads(l) for l in open(sys.argv[1]) if l.strip()]
rows[0]["expect"] = "pass"
with open(sys.argv[2], "w") as out:
    for r in rows:
        out.write(json.dumps(r) + "\n")
PY
check "an audit row says expect=pass" "$TMP/audited.log" unreadable \
      "records accepted REDS only" "$TMP/bad_audit.jsonl"

echo
if [ "$failures" -eq 0 ]; then
    echo "sanitizer audit gate: 6/6 cases behaved (fires on a new red, on a red that went quiet, and on nothing to read)"
    exit 0
fi
echo "sanitizer audit gate: $failures case(s) wrong"
exit 1
