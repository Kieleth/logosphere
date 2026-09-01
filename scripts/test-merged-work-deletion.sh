#!/usr/bin/env bash
#
# test-merged-work-deletion.sh — prove check-merged-work-deletion.sh is not
# vacuous.
#
# A guard that never fires and a guard that always fires look identical
# from a green CI run, so this asserts both directions in a throwaway
# repository built from scratch:
#
#   CASE 1 (must FIRE)  a branch started before a commit landed on main,
#                       then deletes that commit's lines. This is the
#                       accident: three merged PRs went this way.
#   CASE 2 (must be QUIET, the control) the same deletion, by a branch
#                       whose work started after the commit landed. The
#                       author saw the code and meant to remove it.
#   CASE 3 (must be QUIET) a branch that only adds lines.
#   CASE 4 (must be QUIET) the squash workflow: a branch's own lines
#                       return to it as one squash commit on main; after
#                       merging main in, the branch edits those lines.
#                       They blame to the merge-base, but the branch
#                       authored them (measured 2026-09-01: a real branch
#                       refused against its own squash).
#
#   CASE 5 (must be QUIET) the same after a NEWER squash: the branch's
#                       own squash is only an ancestor of the merge-base.
#
# Prints what it measured. Exit 0 = all five behaved.

set -uo pipefail

CHECKER="$(cd "$(dirname "$0")" && pwd)/check-merged-work-deletion.sh"
[ -x "$CHECKER" ] || { echo "FAIL: $CHECKER is missing or not executable"; exit 1; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

export GIT_AUTHOR_NAME=test GIT_AUTHOR_EMAIL=test@example.com
export GIT_COMMITTER_NAME=test GIT_COMMITTER_EMAIL=test@example.com

DAY=86400
T_OLD=$((1700000000))            # branch work starts here
T_LANDED=$((T_OLD + 10 * DAY))   # main gains a commit here
T_NEW=$((T_LANDED + 10 * DAY))   # a later branch starts here

commit_at () {  # commit_at <author-epoch> <message>
    GIT_AUTHOR_DATE="$1 +0000" GIT_COMMITTER_DATE="$1 +0000" \
        git commit -q -m "$2"
}

cd "$TMP"
git init -q -b main .
git config user.name test
git config user.email test@example.com

printf 'alpha\nbeta\ngamma\n' > shared.txt
git add shared.txt
commit_at "$T_OLD" "base"
BASE_SHA="$(git rev-parse HEAD)"

# A pull request lands on main ten days later, adding two lines.
printf 'alpha\nbeta\ngamma\nMERGED-PR-LINE-1\nMERGED-PR-LINE-2\n' > shared.txt
git add shared.txt
commit_at "$T_LANDED" "feat: the merged pull request"

failures=0
check () {  # check <name> <branch> <expect: fire|quiet>
    local name="$1" branch="$2" expect="$3" out rc verdict
    out="$("$CHECKER" main "$branch" 2>&1)"; rc=$?
    # Non-zero alone would also be scored by a crashing script. The
    # refusal has to be the checker's verdict, and name the lines.
    if [ "$rc" -ne 0 ] && printf '%s' "$out" | grep -q "REFUSED"; then
        verdict=fire
    elif [ "$rc" -ne 0 ]; then
        verdict="failed-without-refusing"
    else
        verdict=quiet
    fi
    if [ "$verdict" = "$expect" ]; then
        echo "PASS  $name: expected $expect, got $verdict (exit $rc)"
    else
        echo "FAIL  $name: expected $expect, got $verdict (exit $rc)"
        echo "$out" | sed 's/^/        /'
        failures=$((failures + 1))
    fi
}

# CASE 1: branch predates the merged PR, and deletes its lines.
git checkout -q -b stale "$BASE_SHA"
printf 'alpha\nbeta\ngamma\nbranch-own-work\n' > shared.txt
git add shared.txt
commit_at "$T_OLD" "work started before the PR landed"
git merge -q --no-edit -X ours main -m "merge main, keeping our side" 2>/dev/null \
    || { git checkout -q --ours shared.txt 2>/dev/null; git add shared.txt; \
         GIT_AUTHOR_DATE="$T_OLD +0000" GIT_COMMITTER_DATE="$T_OLD +0000" \
         git commit -q --no-edit; }
echo "[case 1] branch content after keeping our side:"
sed 's/^/    /' shared.txt
check "case 1 stale branch deletes merged PR lines" stale fire

# CASE 2 (CONTROL): identical deletion, branch started AFTER the PR landed.
git checkout -q -b informed main
printf 'alpha\nbeta\ngamma\n' > shared.txt
git add shared.txt
commit_at "$T_NEW" "deliberately remove the PR lines, having seen them"
check "case 2 CONTROL informed branch deletes the same lines" informed quiet

# CASE 3: pure addition.
git checkout -q -b additive main
printf 'alpha\nbeta\ngamma\nMERGED-PR-LINE-1\nMERGED-PR-LINE-2\nnew\n' > shared.txt
git add shared.txt
commit_at "$T_OLD" "add a line, delete nothing"
check "case 3 additive branch" additive quiet

# CASE 4: the squash workflow. A branch adds its own lines; main gains a
# squash commit with that exact content (later date); the branch merges
# main in and then edits its own lines. Every removed line blames to the
# merge-base (the squash) with a fresh date - and every one of them was
# added by the branch's own commits. Must be QUIET.
git checkout -q -b squashed "$BASE_SHA"
printf 'alpha\nbeta\ngamma\nOWN-LINE-1\nOWN-LINE-2\n' > shared.txt
git add shared.txt
commit_at "$T_OLD" "feat: the branch writes its own lines"
git checkout -q main
git checkout -q -b main-with-squash "$BASE_SHA"
printf 'alpha\nbeta\ngamma\nOWN-LINE-1\nOWN-LINE-2\n' > shared.txt
git add shared.txt
commit_at "$T_LANDED" "feat: squash of the branch (#1)"
git checkout -q squashed
git merge -q --no-edit main-with-squash
printf 'alpha\nbeta\ngamma\nOWN-LINE-1-EDITED\nOWN-LINE-2\n' > shared.txt
git add shared.txt
commit_at "$T_NEW" "edit my own line after the squash"
out4="$("$CHECKER" main-with-squash squashed 2>&1)"; rc4=$?
if [ "$rc4" -eq 0 ]; then
    echo "PASS  case 4 squashed branch edits its own lines: expected quiet, got quiet (exit 0)"
else
    echo "FAIL  case 4 squashed branch edits its own lines: expected quiet, got fire (exit $rc4)"
    echo "$out4" | sed 's/^/        /'
    failures=$((failures + 1))
fi

# CASE 5: two squashes. The branch's lines landed as an EARLIER squash;
# main then gained an unrelated commit, so after merging main the
# branch's own squash is only an ancestor of the merge-base. Editing its
# own lines must still be QUIET (measured 2026-09-01 on a real branch).
git checkout -q main-with-squash
printf 'alpha\nbeta\ngamma\nOWN-LINE-1\nOWN-LINE-2\nUNRELATED-LATER\n' > shared.txt
git add shared.txt
commit_at "$((T_LANDED + DAY))" "feat: an unrelated later commit on main"
git checkout -q -b squashed-twice "$BASE_SHA"
printf 'alpha\nbeta\ngamma\nOWN-LINE-1\nOWN-LINE-2\n' > shared.txt
git add shared.txt
commit_at "$T_OLD" "feat: the branch writes its own lines (again)"
git merge -q --no-edit main-with-squash
printf 'alpha\nbeta\ngamma\nOWN-LINE-1-EDITED\nOWN-LINE-2\nUNRELATED-LATER\n' > shared.txt
git add shared.txt
commit_at "$T_NEW" "edit my own line under a newer merge-base"
out5="$("$CHECKER" main-with-squash squashed-twice 2>&1)"; rc5=$?
if [ "$rc5" -eq 0 ]; then
    echo "PASS  case 5 own lines under a newer merge-base: expected quiet, got quiet (exit 0)"
else
    echo "FAIL  case 5 own lines under a newer merge-base: expected quiet, got fire (exit $rc5)"
    echo "$out5" | sed 's/^/        /'
    failures=$((failures + 1))
fi

echo
if [ "$failures" -eq 0 ]; then
    echo "merged-work detector: 5/5 cases behaved (fires on the accident, quiet on the controls)"
    exit 0
fi
echo "merged-work detector: $failures case(s) wrong"
exit 1
