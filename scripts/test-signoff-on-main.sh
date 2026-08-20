#!/usr/bin/env bash
#
# test-signoff-on-main.sh — prove check-signoff.sh is not vacuous.
#
# A gate that never fires and a gate that always fires are the same colour
# in CI, so this asserts both directions against a throwaway repository
# built from scratch. Twelve cases, each one a `before`/`after` pair of
# the shape the GitHub push event delivers:
#
#   1  one signed commit lands                       ACCEPT
#   2  one unsigned commit lands                      REFUSE
#   3  several commits land, all signed               ACCEPT
#   4  several land, an unsigned one in the MIDDLE    REFUSE
#      (a tip-only check passes this; that is why it is here, and the
#       assertion also demands the offending sha be named)
#   5  branch created by the push, tip signed         ACCEPT
#   6  branch created by the push, tip unsigned       REFUSE
#   7  a merge commit alone in the range              ACCEPT, with a note
#   8  a merge commit AND an unsigned commit          REFUSE
#      (proves the merge skip does not swallow the rest of the range)
#   9  a realistic squash message, trailer inherited  ACCEPT
#  10  the same squash message, trailer hand-edited
#      out of the GitHub merge box                    REFUSE
#  11  `before` absent from the clone, tip signed     ACCEPT, degraded
#  12  `before` absent from the clone, tip unsigned   REFUSE
#
# Cases 9 and 10 are the ones this gate exists for. GitHub composes a
# squash message as the pull request title plus the concatenated branch
# commit messages, so a contributor who ran `git commit -s` carries the
# trailer through without doing anything. It goes missing when someone
# rewrites that message in the merge box, and case 10 is that.
#
# Prints what it measured. Exit 0 = all twelve behaved.

set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
CHECKER="$HERE/check-signoff.sh"

[ -x "$CHECKER" ] || { echo "FAIL: $CHECKER missing or not executable"; exit 1; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

export GIT_AUTHOR_NAME=test GIT_AUTHOR_EMAIL=test@example.com
export GIT_COMMITTER_NAME=test GIT_COMMITTER_EMAIL=test@example.com

ZERO="0000000000000000000000000000000000000000"

cd "$TMP"
git init -q -b main .
git config user.name test
git config user.email test@example.com

# Every commit writes its OWN file. Appending to one shared file made
# cases 8 and 9 conflict at merge time, which left a merge half-resolved
# and silently moved the ranges the later cases were asserting against.
# The fixture has to be boring or it tests itself instead of the checker.
n=0
touch_commit () {  # touch_commit <signed|unsigned> <subject>
    n=$((n + 1))
    echo "content $n" > "file_$n.txt"
    git add "file_$n.txt"
    if [ "$1" = signed ]; then
        git commit -q -s -m "$2"
    else
        git commit -q -m "$2"
    fi
}

failures=0
check () {  # check <name> <expect: accept|refuse> <before> <after> [must-contain]
    local name="$1" expect="$2" before="$3" after="$4" needle="${5:-}"
    local out rc verdict
    out="$("$CHECKER" "$before" "$after" 2>&1)"; rc=$?
    # A non-zero exit is not enough. A crashing script, a bad argument or
    # a missing object would all exit non-zero and score as a refusal.
    # The refusal has to be the checker's verdict and say so.
    if [ "$rc" -ne 0 ] && printf '%s' "$out" | grep -q "REFUSED"; then
        verdict=refuse
    elif [ "$rc" -ne 0 ]; then
        verdict="failed-without-refusing"
    else
        verdict=accept
    fi
    if [ "$verdict" != "$expect" ]; then
        echo "FAIL  $name: expected $expect, got $verdict (exit $rc)"
        printf '%s\n' "$out" | sed 's/^/        /'
        failures=$((failures + 1))
        return
    fi
    if [ -n "$needle" ] && ! printf '%s' "$out" | grep -q "$needle"; then
        echo "FAIL  $name: verdict $verdict was right but output never mentioned '$needle'"
        printf '%s\n' "$out" | sed 's/^/        /'
        failures=$((failures + 1))
        return
    fi
    echo "PASS  $name: expected $expect, got $verdict (exit $rc)"
}

# A base commit, so there is always a `before` to point at.
touch_commit signed "base"
BASE="$(git rev-parse HEAD)"

# --- 1: one signed commit lands -------------------------------------------
touch_commit signed "feat: a signed squash merge"
check "1  single signed commit" accept "$BASE" "$(git rev-parse HEAD)"
A1="$(git rev-parse HEAD)"

# --- 2: one unsigned commit lands -----------------------------------------
touch_commit unsigned "feat: nobody signed this"
check "2  single unsigned commit" refuse "$A1" "$(git rev-parse HEAD)"
A2="$(git rev-parse HEAD)"

# --- 3: several commits, all signed ---------------------------------------
touch_commit signed "feat: first of three"
touch_commit signed "feat: second of three"
touch_commit signed "feat: third of three"
check "3  three commits, all signed" accept "$A2" "$(git rev-parse HEAD)"
A3="$(git rev-parse HEAD)"

# --- 4: several commits, the UNSIGNED one is not the tip ------------------
# The one a tip-only check waves through. The assertion is stricter than
# the verdict: the offending sha has to appear in the output, because a
# refusal that does not say which commit is useless to whoever has to fix
# it, and a refusal for the wrong reason would still be a refusal.
touch_commit signed   "feat: signed, and first"
touch_commit unsigned "feat: unsigned, and buried in the middle"
BURIED="$(git rev-parse HEAD)"
touch_commit signed   "feat: signed, and last"
check "4  unsigned commit buried mid-range" refuse "$A3" "$(git rev-parse HEAD)" "${BURIED:0:9}"
A4="$(git rev-parse HEAD)"

# --- 5 and 6: the branch is created by the push ---------------------------
# GitHub sends forty zeros as `before`. There is no range to walk, so the
# tip is what gets judged. Walking the whole history instead would put
# every commit that predates this rule on trial.
git checkout -q -b created-signed "$A4"
touch_commit signed "feat: the first push of a new branch"
check "5  branch created, tip signed" accept "$ZERO" "$(git rev-parse HEAD)"

git checkout -q -b created-unsigned "$A4"
touch_commit unsigned "feat: the first push of a new branch, unsigned"
check "6  branch created, tip unsigned" refuse "$ZERO" "$(git rev-parse HEAD)"

# --- 7: a merge commit alone in the range ---------------------------------
# It has no authored content, cannot be signed at creation, and cannot be
# amended without the force-push the policy refuses. Skipped, and the skip
# is printed rather than silent.
git checkout -q main
git checkout -q -b side "$A4"
touch_commit signed "feat: work on a side branch"
git checkout -q main
git merge -q --no-ff --no-edit side -m "merge side into main"
check "7  merge commit alone in range" accept "$A4" "$(git rev-parse HEAD)" "merge commit skipped"
A7="$(git rev-parse HEAD)"

# --- 8: a merge commit AND an unsigned commit in the same range -----------
# The control for case 7. If the merge skip were implemented as "a range
# containing a merge is fine", this would pass and the gate would have a
# hole a single `git merge` wide.
git checkout -q -b side2 "$A7"
touch_commit unsigned "feat: unsigned work on a side branch"
UNSIGNED_SIDE="$(git rev-parse HEAD)"
git checkout -q main
touch_commit signed "feat: signed work on main"
git merge -q --no-ff --no-edit side2 -m "merge side2 into main"
check "8  merge commit plus an unsigned one" refuse "$A7" "$(git rev-parse HEAD)" "${UNSIGNED_SIDE:0:9}"
A8="$(git rev-parse HEAD)"

# --- 9: a realistic squash message, trailer inherited ---------------------
# What GitHub actually composes: the pull request title, then the branch
# commit messages, each of which carried its own trailer.
echo "content squash-ok" > file_squash_ok.txt
git add file_squash_ok.txt
git commit -q -F - <<'EOF'
feat(engine): the thing the pull request was titled (#140)

* first branch commit, doing half of it

Signed-off-by: Contributor <contributor@example.com>

* second branch commit, doing the other half

Signed-off-by: Contributor <contributor@example.com>
EOF
check "9  squash message with inherited trailer" accept "$A8" "$(git rev-parse HEAD)"
A9="$(git rev-parse HEAD)"

# --- 10: the same message, trailer hand-edited out -----------------------
# Someone tidied the merge box and deleted the commit log nobody wanted to
# read, taking the trailer with it. This is the case the gate is for.
echo "content squash-edited" > file_squash_edited.txt
git add file_squash_edited.txt
git commit -q -F - <<'EOF'
feat(engine): the thing the pull request was titled (#141)

Tidied in the merge box. The branch commit messages went, and the
sign-off trailer went with them.
EOF
check "10 squash message with the trailer edited out" refuse "$A9" "$(git rev-parse HEAD)"

# --- 11 and 12: `before` is not an object in this clone -------------------
# A force-push to main leaves the old tip unreachable, and a reference
# clone may never have fetched it. The checker degrades to the tip and
# says so. It must not crash: a crash exits non-zero, and a non-zero exit
# reads as a refusal to anyone skimming, which would hide a broken checker
# behind a plausible-looking red. So both directions are asserted here
# too, and the accept case demands the degrade actually be announced.
ABSENT="deadbeefdeadbeefdeadbeefdeadbeefdeadbeef"

touch_commit signed "feat: the tip, signed"
check "11 before-sha absent, tip signed" accept "$ABSENT" "$(git rev-parse HEAD)" "not present in this clone"

touch_commit unsigned "feat: the tip, unsigned"
check "12 before-sha absent, tip unsigned" refuse "$ABSENT" "$(git rev-parse HEAD)"

echo
if [ "$failures" -eq 0 ]; then
    echo "sign-off gate: 12/12 cases behaved (refuses the unsigned, accepts the signed)"
    exit 0
fi
echo "sign-off gate: $failures case(s) wrong"
exit 1
