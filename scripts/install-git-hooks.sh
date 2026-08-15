#!/usr/bin/env bash
#
# install-git-hooks.sh — point this clone's git at .githooks/ and turn off
# the settings that make an accidental rebase possible.
#
# Idempotent. Run it as often as you like. `cmake -S . -B build` runs it
# for you, so a fresh clone that follows README.md is protected without
# anyone remembering a setup step.
#
# What it configures, all repo-local, nothing global:
#
#   core.hooksPath=.githooks   the pre-rebase and pre-push hooks, which
#                              are tracked in the repo and therefore
#                              travel with a clone (.git/hooks does not)
#   pull.rebase=false          `git pull` never rebases
#   pull.ff=only               `git pull` fast-forwards or fails; it
#                              never invents a merge commit behind your
#                              back. Diverged? Merge origin/main by hand.
#   branch.autoSetupRebase=never   new branches do not inherit a
#                              rebasing pull from anywhere
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

chmod +x .githooks/* 2>/dev/null || true

previous="$(git config --local --get core.hooksPath || true)"
if [ -n "$previous" ] && [ "$previous" != ".githooks" ]; then
    echo "install-git-hooks: replacing core.hooksPath '$previous' with '.githooks'" >&2
fi

git config --local core.hooksPath .githooks
git config --local pull.rebase false
git config --local pull.ff only
git config --local branch.autoSetupRebase never
git config --local merge.ff true

echo "install-git-hooks: hooks -> .githooks (pre-rebase, pre-push); pull.rebase=false, pull.ff=only"
