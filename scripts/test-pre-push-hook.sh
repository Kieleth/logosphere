#!/usr/bin/env bash
#
# test-pre-push-hook.sh — exercise .githooks/pre-push end to end against a
# real push to a real (local, bare) remote.
#
# Not a reading of the script: an actual `git push` that either completes
# or does not, with the remote ref inspected afterwards. Four cases:
#
#   A  push to main                          must REFUSE
#   B  force-push over a rewritten history   must REFUSE
#   C  stale branch that reverts main AND deletes the checker
#                                            must REFUSE
#   D  an honest branch that only adds       must PASS
#
# C is here because it already got through once. The first version of the
# hook read scripts/check-merged-work-deletion.sh out of the working tree,
# and a branch reverting main reverts the checker along with it: the push
# was waved through with a "check SKIPPED" notice. The hook now reads the
# checker from origin/main, so the branch cannot vote itself unchecked.
#
# Prints what it measured. Exit 0 = all four behaved.

set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
HOOK="$REPO/.githooks/pre-push"
CHECKER="$HERE/check-merged-work-deletion.sh"

[ -x "$HOOK" ]    || { echo "FAIL: $HOOK missing or not executable"; exit 1; }
[ -x "$CHECKER" ] || { echo "FAIL: $CHECKER missing or not executable"; exit 1; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

export GIT_AUTHOR_NAME=test GIT_AUTHOR_EMAIL=test@example.com
export GIT_COMMITTER_NAME=test GIT_COMMITTER_EMAIL=test@example.com

DAY=86400
T_OLD=$((1700000000))
T_LANDED=$((T_OLD + 10 * DAY))

commit_at () { GIT_AUTHOR_DATE="$1 +0000" GIT_COMMITTER_DATE="$1 +0000" git commit -q -m "$2"; }

git init -q --bare "$TMP/remote.git"
git init -q -b main "$TMP/work"
cd "$TMP/work"
git config user.name test
git config user.email test@example.com
git remote add origin "$TMP/remote.git"

printf 'alpha\nbeta\n' > shared.txt
git add -A
commit_at "$T_OLD" "base"
BASE_SHA="$(git rev-parse HEAD)"

# The merged pull request lands the checker itself, so a branch based
# before it does NOT have the checker in its tree. That is case C.
mkdir -p scripts
cp "$CHECKER" scripts/check-merged-work-deletion.sh
chmod +x scripts/check-merged-work-deletion.sh
printf 'alpha\nbeta\nMERGED-PR-LINE\n' > shared.txt
git add -A
commit_at "$T_LANDED" "feat: the merged pull request"
git push -q origin main
git fetch -q origin main

# The hook under test, installed the way install-git-hooks.sh installs it.
cp "$HOOK" .git/hooks/pre-push
chmod +x .git/hooks/pre-push

failures=0
check () {  # check <name> <expect: refuse|pass> <push args...>
    local name="$1" expect="$2"; shift 2
    local out rc verdict
    out="$(git push "$@" 2>&1)"; rc=$?
    # A non-zero exit is not enough: any git error would score as a
    # refusal. The refusal has to come from the hook and say so.
    if [ "$rc" -ne 0 ] && printf '%s' "$out" | grep -q "REFUSED"; then
        verdict=refuse
    elif [ "$rc" -ne 0 ]; then
        verdict="failed-without-refusing"
    else
        verdict=pass
    fi
    if [ "$verdict" = "$expect" ]; then
        echo "PASS  $name: expected $expect, got $verdict (exit $rc)"
    else
        echo "FAIL  $name: expected $expect, got $verdict (exit $rc)"
        echo "$out" | sed 's/^/        /'
        failures=$((failures + 1))
    fi
}

# --- A: push to main ------------------------------------------------------
git checkout -q -b feature main
printf 'alpha\nbeta\nMERGED-PR-LINE\nfeature\n' > shared.txt
git add shared.txt
commit_at "$T_LANDED" "a feature"
check "A push to main" refuse origin HEAD:main

# --- D: honest branch (run before B, which needs it on the remote) --------
check "D honest additive branch" pass origin feature:feature

# --- B: force-push over rewritten history ---------------------------------
BEFORE="$(git --git-dir="$TMP/remote.git" rev-parse feature)"
git commit -q --amend --no-edit --date="now"
check "B force-push after amend" refuse --force origin feature:feature
AFTER="$(git --git-dir="$TMP/remote.git" rev-parse feature)"
if [ "$BEFORE" = "$AFTER" ]; then
    echo "PASS  B remote ref unchanged: ${BEFORE:0:9}"
else
    echo "FAIL  B remote ref moved ${BEFORE:0:9} -> ${AFTER:0:9}"
    failures=$((failures + 1))
fi

# --- C: stale branch reverts main and deletes the checker with it ---------
git checkout -q -b stale "$BASE_SHA"
printf 'alpha\nbeta\nbranch-own-work\n' > shared.txt
git add shared.txt
commit_at "$T_OLD" "work started before the PR landed"
# -s ours: take main into the history, keep our tree. Exactly what a
# mis-resolved replay leaves behind, checker deleted and all.
GIT_AUTHOR_DATE="$T_LANDED +0000" GIT_COMMITTER_DATE="$T_LANDED +0000" \
    git merge -q -s ours --no-edit origin/main -m "merge main, keep our side"
if [ -e scripts/check-merged-work-deletion.sh ]; then
    echo "note  C setup: checker still present, case is weaker than intended"
else
    echo "note  C setup: checker deleted from the branch, as intended"
fi
check "C stale branch reverting main (checker deleted)" refuse origin stale:stale

echo
if [ "$failures" -eq 0 ]; then
    echo "pre-push hook: 4/4 cases behaved"
    exit 0
fi
echo "pre-push hook: $failures case(s) wrong"
exit 1
