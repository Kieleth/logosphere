#!/usr/bin/env bash
#
# check-signoff.sh — every commit that LANDS ON MAIN carries a DCO
# Signed-off-by trailer.
#
# This checks main, not branches, and that is the whole point.
#
# The rule used to be "every commit in the pull request is signed", run on
# the pull_request event over `git rev-list base..head`. That rule and the
# merge policy could not both be satisfied. Rebasing is refused
# (.githooks/pre-rebase, no override) and force-pushing is refused
# (.githooks/pre-push), so a branch that falls behind main catches up with
# `git merge origin/main`. The resulting merge commit has no authored
# content, cannot be signed at creation, and cannot be signed afterwards
# because amending it needs the force-push the same policy refuses. A
# branch behind main could obey the merge rule or pass the sign-off gate,
# never both. That cost a branch: pull request #131, closed and reopened
# as #133.
#
# Squash merging is the only method enabled on this repository, so exactly
# one commit lands on main per pull request. That commit is the artifact
# the DCO is about, and it is the one this checks.
#
# Usage:  check-signoff.sh <before-sha> <after-sha>
#
# Both come from the push event payload. Exit 0 = every landed commit is
# signed. Exit 1 = at least one is not, and it is named.

set -uo pipefail

BEFORE="${1:-}"
AFTER="${2:-}"

if [ -z "$AFTER" ]; then
    echo "usage: $0 <before-sha> <after-sha>" >&2
    exit 2
fi

ZERO="0000000000000000000000000000000000000000"

have_commit () { git cat-file -e "${1}^{commit}" 2>/dev/null; }

# --- which commits this push actually landed -------------------------------
#
# Three cases, all of them reachable in practice:
#
#   1. Ordinary push. `before` is the previous tip and `before..after` is
#      the set of new commits. After a squash merge that set has exactly
#      one member; after a batch it has several, and every one of them is
#      checked, not just the tip.
#   2. The branch is CREATED by this push. GitHub sends `before` as forty
#      zeros. There is no range to walk, and walking the whole history
#      instead would judge every commit that ever existed, including the
#      ones written before this rule did. Only the new tip is checked.
#   3. `before` is not an object in this clone. A force-push to main can
#      leave the old tip unreachable, and a shallow or reference clone may
#      not have it. Rather than crash and be read as green by a `|| true`
#      somewhere upstream, this degrades to the tip and says so.
if [ -z "$BEFORE" ] || [ "$BEFORE" = "$ZERO" ]; then
    range_note="branch created by this push, so only its tip is checked"
    commits="$AFTER"
elif ! have_commit "$BEFORE"; then
    range_note="before-sha ${BEFORE} is not present in this clone, so only the tip is checked"
    commits="$AFTER"
else
    range_note="commits in ${BEFORE:0:9}..${AFTER:0:9}"
    commits="$(git rev-list "$BEFORE".."$AFTER")" || {
        echo "FAIL: could not list ${BEFORE}..${AFTER}" >&2
        exit 2
    }
fi

echo "[measure] range: $range_note"

# --- the check -------------------------------------------------------------
#
# A squash merge's message is the pull request title followed by the
# concatenated messages of the branch commits. A contributor who ran
# `git commit -s` therefore carries the trailer into the squash for free,
# which is why this is not a burden in the normal case. The case it exists
# to catch is the other one: a squash message hand-edited in the GitHub
# merge box, where the trailer is deleted along with the commit log nobody
# wanted to read. The trailer can sit anywhere in the message body, so the
# match is per line and not anchored to the end.
#
# Presence is all that is checked, not that the sign-off name matches the
# author. That is a deliberate limit: contributions arrive through forks,
# and the author of a squashed commit is the branch author while the
# trailer may legitimately name whoever holds the right to submit.
missing=0
examined=0
skipped_merges=0

for sha in $commits; do
    subject="$(git log -1 --format=%s "$sha")"

    # A merge commit has no authored content. It cannot be signed at
    # creation and cannot be signed later without the force-push the
    # policy refuses, which is the exact deadlock this file replaced.
    # Under squash-only merging one cannot reach main through a pull
    # request at all, so if this line ever prints, something pushed to
    # main out of band and the note is the record of it.
    if [ "$(git rev-list --no-walk --count --merges "$sha")" -eq 1 ]; then
        echo "note      merge commit skipped (no authored content): ${sha:0:9} $subject"
        skipped_merges=$((skipped_merges + 1))
        continue
    fi

    examined=$((examined + 1))
    if git log -1 --format=%B "$sha" | grep -q "^Signed-off-by: "; then
        echo "signed    ${sha:0:9} $subject"
    else
        echo "MISSING   ${sha:0:9} $subject"
        missing=$((missing + 1))
    fi
done

echo "[measure] commits examined: $examined, merge commits skipped: $skipped_merges, missing sign-off: $missing"

if [ "$missing" -ne 0 ]; then
    cat >&2 <<'EOF'

REFUSED: a commit landed on main without a Signed-off-by trailer.

Everything reaches main by squash merge, so the squash message is what
carries the sign-off. It is inherited from the branch commits when they
were made with `git commit -s`; deleting it out of the merge box in the
GitHub UI is how it goes missing.

See CONTRIBUTING.md, Developer Certificate of Origin.
EOF
    exit 1
fi

echo "Every commit that landed carries a sign-off."
