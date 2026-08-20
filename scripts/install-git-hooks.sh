#!/usr/bin/env bash
#
# install-git-hooks.sh — install the tracked hooks into this clone and turn
# off the settings that make an accidental rebase possible.
#
# Idempotent. Run it as often as you like. `cmake -S . -B build` runs it for
# you, so a fresh clone that follows README.md is protected by its first
# build, with no setup step for anyone to forget.
#
# Why COPY into .git/hooks instead of setting core.hooksPath=.githooks:
#
#   core.hooksPath is resolved relative to the top of the WORKING TREE.
#   Check out a branch from before this commit and .githooks/ is not
#   there, so git finds no hook and runs the rebase without a word. That
#   was measured, not guessed: with core.hooksPath=.githooks set,
#   `git rebase origin/main` on a branch based one commit earlier
#   completed successfully and printed nothing. The hole is exactly the
#   branches most likely to be rebased, the old ones.
#
#   .git/hooks lives in the git common directory. It is the same
#   directory for every branch and every linked worktree, so the refusal
#   holds no matter what is checked out.
#
#   The cost is that the installed copy can drift from the tracked
#   source. Re-running this script (or cmake) re-copies, and the
#   merge-policy CI job checks the tracked originals.
#
# Also configured, all repo-local, nothing global:
#
#   pull.rebase=false          `git pull` never rebases
#   pull.ff=only               `git pull` fast-forwards or fails; it never
#                              invents a merge commit behind your back.
#                              Diverged? Merge origin/main by hand.
#   branch.autoSetupRebase=never   new branches do not inherit a rebasing
#                              pull from anywhere
#   merge.ff=true              a merge that can fast-forward, does
#
# See CLAUDE.md, "Git integration policy".

set -euo pipefail

cd "$(dirname "$0")/.."

if ! git rev-parse --git-dir >/dev/null 2>&1; then
    echo "install-git-hooks: not a git repository, nothing to do"
    exit 0
fi

if [ ! -d .githooks ]; then
    echo "install-git-hooks: .githooks/ is missing, nothing to do" >&2
    exit 0
fi

common_dir="$(git rev-parse --git-common-dir)"
case "$common_dir" in
    /*) ;;
    *)  common_dir="$(cd "$common_dir" && pwd)" ;;
esac
target="$common_dir/hooks"
mkdir -p "$target"

# A hooksPath pointing at the tracked directory is the trap described
# above. If a previous version of this script set it, take it back out.
current_path="$(git config --local --get core.hooksPath || true)"
if [ "$current_path" = ".githooks" ]; then
    git config --local --unset core.hooksPath
    echo "install-git-hooks: removed core.hooksPath=.githooks (branch-dependent, see header)"
elif [ -n "$current_path" ]; then
    echo "install-git-hooks: WARNING core.hooksPath=$current_path is set." >&2
    echo "install-git-hooks: hooks were copied to $target, which git will IGNORE." >&2
fi

installed=""
for hook in .githooks/*; do
    [ -f "$hook" ] || continue
    name="$(basename "$hook")"
    cp "$hook" "$target/$name"
    chmod +x "$target/$name"
    installed="$installed $name"
done

chmod +x .githooks/* 2>/dev/null || true

git config --local pull.rebase false
git config --local pull.ff only
git config --local branch.autoSetupRebase never
git config --local merge.ff true

echo "install-git-hooks: installed$installed into $target"
echo "install-git-hooks: pull.rebase=false, pull.ff=only, branch.autoSetupRebase=never"
