#!/usr/bin/env bash
#
# check-merged-work-deletion.sh <base-ref> <head-ref>
#
# Answers one question: does this branch DELETE lines that landed on the
# base branch AFTER the branch's own work started?
#
# That is the signature of the accident this repository has already paid
# for. An agent rebased a branch onto a moved main, resolved the replay
# by keeping its own side, and pushed. The branch then genuinely deleted
# the lines three merged pull requests had added. Every test passed: a
# branch that undoes main's work still compiles. Nobody looks at a diff
# when CI is green, so it merged, three times.
#
# How the detection works:
#
#   base = merge-base(<base-ref>, <head-ref>)          the branch point
#   T    = earliest AUTHOR date among the branch's own commits
#          (author dates survive a rebase; committer dates do not,
#           which is exactly why this uses author dates)
#
#   For every line the branch removes relative to `base`, blame that
#   line in `base` and read when its commit landed. If it landed after
#   T, the branch is deleting work that appeared after the branch
#   started, which its author never saw. Report it.
#
# Honest limits: this is a heuristic, not a proof. It cannot tell a
# deliberate cleanup of last week's code from an accidental revert of
# it; both delete recently-landed lines. It is tuned to be quiet in
# normal work and loud in the failure mode above. It will not fire at
# all on a branch whose commits were re-authored with fresh dates.
#
# Exit 0 = clean, 1 = deletions of recently-landed work found.

set -uo pipefail

if [ $# -ne 2 ]; then
    echo "usage: $0 <base-ref> <head-ref>" >&2
    exit 2
fi

BASE_REF="$1"
HEAD_REF="$2"

MERGE_BASE="$(git merge-base "$BASE_REF" "$HEAD_REF" 2>/dev/null || true)"
if [ -z "$MERGE_BASE" ]; then
    echo "[merged-work] no merge base between $BASE_REF and $HEAD_REF; nothing to check"
    exit 0
fi

# The branch's own commits, oldest author date first.
BRANCH_START="$(git log --format=%at "$MERGE_BASE..$HEAD_REF" 2>/dev/null \
    | sort -n | head -1)"
if [ -z "$BRANCH_START" ]; then
    echo "[merged-work] $HEAD_REF adds no commits over $BASE_REF; nothing to check"
    exit 0
fi

echo "[merged-work] base-ref    $BASE_REF"
echo "[merged-work] head-ref    $HEAD_REF"
echo "[merged-work] merge-base  $MERGE_BASE"
echo "[merged-work] work began  $(date -u -r "$BRANCH_START" '+%Y-%m-%d %H:%M UTC' 2>/dev/null \
    || date -u -d "@$BRANCH_START" '+%Y-%m-%d %H:%M UTC' 2>/dev/null \
    || echo "epoch $BRANCH_START")"

findings=0
report=""

# Files the branch modifies or deletes. Added files cannot delete anything.
while IFS= read -r file; do
    [ -n "$file" ] || continue

    # Hunk headers only: @@ -old_start,old_count +new_start,new_count @@
    # old_count omitted means 1. old_count 0 means a pure insertion.
    ranges="$(git diff --unified=0 --no-color -M "$MERGE_BASE" "$HEAD_REF" -- "$file" \
        | awk '
            /^@@ / {
                split($2, a, ",")
                start = substr(a[1], 2) + 0
                count = (2 in a) ? a[2] + 0 : 1
                if (count > 0) printf "%d,+%d\n", start, count
            }')"
    [ -n "$ranges" ] || continue

    blame_args=()
    while IFS= read -r r; do
        [ -n "$r" ] && blame_args+=("-L" "$r")
    done <<< "$ranges"
    [ ${#blame_args[@]} -gt 0 ] || continue

    # One blame call per file, all removed ranges at once. --line-porcelain
    # repeats the commit header for every line, so the sha and its
    # committer-time always arrive as a pair (plain --porcelain emits the
    # header only the first time a commit appears, which silently
    # mis-pairs them).
    culprits="$(git blame --line-porcelain "${blame_args[@]}" "$MERGE_BASE" -- "$file" 2>/dev/null \
        | awk -v cutoff="$BRANCH_START" '
            /^[0-9a-f]{40} / { sha = $1 }
            /^committer-time / { ct = $2 }
            /^summary / {
                sub(/^summary /, "")
                if (ct + 0 > cutoff + 0 && !(sha in seen)) {
                    seen[sha] = 1
                    print substr(sha, 1, 9) "  " $0
                }
            }')"

    if [ -n "$culprits" ]; then
        findings=$((findings + 1))
        report="${report}
  $file
$(echo "$culprits" | sed 's/^/      /')"
    fi
done < <(git diff --name-only --diff-filter=MDR "$MERGE_BASE" "$HEAD_REF")

if [ "$findings" -eq 0 ]; then
    echo "[merged-work] OK: deletes nothing that landed after this branch started"
    exit 0
fi

cat >&2 <<EOF

REFUSED: this branch deletes work that landed on $BASE_REF after it started.

Files, and the already-merged commits whose lines are being removed:
$report

Read that list. One of two things is true:

  (a) You did not mean to. A rebase, or a conflict resolution that kept
      your side, threw away someone else's merged pull request. This is
      the accident that cost this repository three merged PRs. Fix it:
      merge $BASE_REF into your branch and keep BOTH sides.

          git fetch origin main && git merge origin/main

  (b) You did mean to, and deleting that code is the point of the
      branch. Then merge $BASE_REF in first so the deletion shows up as
      a deliberate change against current main, and say so in the PR.

Override, only after reading the list and meaning it:

    LOGOSPHERE_ALLOW_DELETING_MERGED_WORK=1 git push ...

EOF
exit 1
