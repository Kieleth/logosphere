# Logosphere: Agent Instructions

Instructions for AI coding agents working in this repository. The
human-facing versions of these rules live in the linked docs; when
they disagree, the docs win.

## Read first

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): the engine
  invariants. They are non-negotiable: if a fix appears to require
  breaking one, the diagnosis is wrong. Go one level deeper.
- [CONTRIBUTING.md](CONTRIBUTING.md): repo layout, build profiles,
  test patterns, submission process (DCO sign-off on every commit).
- [docs/GAME_LAYER.md](docs/GAME_LAYER.md): the API surface games
  consume.
- [docs/testing_guidelines.md](docs/testing_guidelines.md): how a test
  earns the right to claim what its name says. Read it BEFORE writing
  or changing a test. Every rule in it has a real failure behind it,
  most of them tests that passed while the thing they were named after
  was broken.
- Physics work: load the `physics` skill
  ([.claude/skills/physics/SKILL.md](.claude/skills/physics/SKILL.md))
  before touching solver, constraint, contact, or physics-ontology
  code. It carries the operating discipline — invariants framework,
  sweep semantics, ruling protocol, owner QA contract.

## Git integration policy (hard rule, no exceptions)

1. **Never rebase.** Not `git rebase`, not `git rebase -i`, not
   `git pull --rebase`, not "Rebase and merge" on GitHub. Ever.
2. **Fast-forward where possible.** `git merge --ff-only` is the
   preferred local integration. Behind main? `git fetch origin main
   && git merge origin/main`. Merge it in; never rebase onto it.
3. **Only squash merges land on main.** No merge commits, no
   rebase-merges. main is never pushed to, only squash-merged into.
4. **Commits inside a branch do not matter.** Do not tidy them, do
   not rewrite them, do not squash them locally, do not amend to
   make them pretty. The squash merge collapses the branch to one
   commit. Nobody will ever see them. Time spent on them is time
   spent operating the exact tool that caused the damage below.

**Why, precisely.** A rebase replays commits onto a base they were
never written against. Whatever that base has and the replay drops,
or whatever a conflict resolution keeps from your side, is silently
deleted. An agent session rebased repeatedly in this repository and
reverted three already-merged pull requests. CI was green the whole
way through, because a branch that undoes main's work still compiles
and its tests still pass. No test in this repo can catch it. That is
why the rule is mechanical and not advisory.

**What enforces it** (all of it is in the repo, none of it is in
anyone's head):

- `.githooks/pre-rebase` refuses every rebase, unconditionally, with
  no override. Git runs it for `git rebase` and for the rebase inside
  `git pull --rebase`.
- `.githooks/pre-push` refuses a push to main, refuses a force-push
  (a rebase only becomes everyone's problem when it is force-pushed),
  and refuses a branch that deletes lines which landed on main after
  the branch started.
- `scripts/install-git-hooks.sh` copies both hooks into `.git/hooks`
  and sets `pull.rebase=false`, `pull.ff=only`,
  `branch.autoSetupRebase=never`. `cmake -S . -B build` runs it, so a
  clone is protected by its first build. It copies rather than setting
  `core.hooksPath=.githooks` because that path is resolved against the
  working tree: on a branch checked out from before this policy landed
  the directory is absent and git runs the rebase in silence. Measured,
  not assumed. `.git/hooks` is the same directory on every branch and
  in every worktree.
- GitHub: squash merging is the only method enabled. Merge commits
  and rebase merges are off at the repository level, so the buttons
  do not exist.
- CI: the `merge-policy` job re-runs the merged-work check on every
  PR, verifies the hooks are still executable (a hook without its
  executable bit is ignored by git in silence), and runs both
  self-tests: `scripts/test-merged-work-deletion.sh` proves the
  detector fires on the accident and stays quiet on the control, and
  `scripts/test-pre-push-hook.sh` performs real pushes to a real
  local remote and asserts which ones are refused.

If a hook refuses you, it is right and you are wrong. Do not reach
for `--no-verify`, do not edit the hook, do not delete it out of
`.git/hooks`. Stop and ask the owner.

## The invariants, in one breath

Everything is a particle; particles are bodies that occupy space and
collide. The world turtle at z = 0 is the only immovable thing.
Physics never reads game-layer categories. No gravity assumptions
(fixes must work on walls, ceilings, zero-g). No springs or damping
as patches. No special-case fixes keyed to a geometry, size ratio,
or owner: find the missing mechanism. Light is computed, never
faked; shadow is its absence. Details and orientation conventions
(compass-clockwise yaw, +Y forward) are in ARCHITECTURE.md; the
conventions bite, honor them exactly.

## Engine vs game

The engine stops at generic mechanism; games provide policy. Before
adding anything to `src/`, check: would this make equal sense in a
farming game and a combat game? If it only makes sense in one genre,
it belongs in the game under `examples/`. Never hardcode a game type
name in engine code; make it data-driven through the ontology.

## Working discipline

- **Test first, headless first.** Reproduce bugs in a headless test
  before fixing them; do not debug by watching the screen. Headless
  is the source of truth: if headless passes and interactive fails,
  investigate the difference.
- **Every bug fix ships a regression test; every new public API
  ships a test.** Tests fail loudly and print measured values, not
  bare PASS/FAIL. Thresholds come from measurements, not guesses.
- **Follow failures to their cause.** Instrument, trace, read the
  actual values. For "a particle ended up somewhere wrong and I do
  not know who moved it," use the ParticleTracer
  (`engine.get_particle_tracer()`, see ARCHITECTURE.md and the
  write-site list in `src/core/particle_dynamics_system.cpp`).
- **Changelog duty.** User-facing changes add a line to
  CHANGELOG.md under `[Unreleased]`, written for the library user,
  not the committer.
- **One owner per thing; nobody edits another owner's content.**
  Ingested entities carry the document and revision they came from,
  and that origin is sealed: a seed may point at what another owns,
  never write onto it. When something is partial, the fix is never
  "edit the file that owns it" but "decide who owns the complete
  thing". Check who actually depends on it first; a thing nothing
  references can simply change hands. This is invariant 8 in
  ENGINE_INVARIANTS.md, and it has been paid for three times: two
  seeds each creating a `Gun Combat`, two each creating a `1D6`, and
  a sampler seed holding three rows hostage.

## Ontology changes

Schemas under `schema/` and `examples/*/schema/` are the source of
truth. Edit the YAML, run `python scripts/generate_ontology.py`
(dependencies in `environment.yml`), commit the YAML and the
regenerated `*/generated/*` files together. Never hand-edit
generated files.

## Builds and tests

Three profiles: `full` (macOS arm64, default), `physics` and `core`
(any C++17 toolchain). See CONTRIBUTING.md for commands. The
combined harness is `./build/logosphere-tests --no-head`; standalone
tests are individual executables under `build/`. CI runs the
headless profiles plus a DCO check on every PR, and all required
checks must be green before merge.

Tests are held to [docs/testing_guidelines.md](docs/testing_guidelines.md).
The short version, and none of it is optional:

- Assert the thing the test is NAMED after. A `..._walk` test asserts
  distance travelled.
- A test that cannot fail is not a test. Ask what would make it red
  before believing it green.
- Prove a check is not vacuous by running the CONTROL, the case where
  the answer must come out the other way.
- Measure first, assert second. Print `[measure]` values in every run.
- Verify through the path production uses, not through a
  reconstruction of it.
- Test the negative, the boundary, and one step past it.
- Instrument before concluding. Do not reason about what must be
  happening.
- Never loosen an assertion to make it pass; say which was wrong, the
  code or the expectation.
- Headless is the source of truth. On-screen output is testable
  headless, so a HUD gets a pixel assertion rather than a promise.

## Project flow

Work is tracked in GitHub issues; the
[pinned roadmap issue](https://github.com/Kieleth/logosphere/issues/10)
maps the current lanes (Adoption, Rendering, Physics, Knowledge
Graph) and links what is actively being pushed on.

- **Before starting anything non-trivial, find or file the issue.**
  Check the roadmap and the open issues first; if the work has no
  issue, create one stating the problem with measured facts, not
  intentions. Label it with the matching `area:*` label and
  `roadmap` if it belongs on the map.
- **Reference the issue from the work.** PRs and commits close their
  issue with `Closes #N` in the body. One logical change per PR.
- **Milestones are release commitments.** The active milestone (for
  example `v0.3.0 Adoption`) holds what the next release promises;
  do not add to it without the maintainer.
- **`good first issue` means scoped and mentored**; keep those
  well-defined when filing them.
- **Questions go to Discussions**, concrete defects and proposals go
  to issues.
- **Known-issue discipline.** A bug that cannot be fixed now still
  gets: a test that reproduces it and ratchets it from getting
  worse, a line in the CHANGELOG known issues, and a tracked issue.
  Silent breakage is the only forbidden state.

## Commits

Present-tense imperative subject with conventional prefixes
(`feat:`, `fix:`, `docs:`, `test:`, `refactor:`, `perf:`, `chore:`).
Body explains why, not what. Every commit carries a DCO sign-off
(`git commit -s`). Keep each PR to one logical change.

## Never revert without explicit consent

Do not revert, discard, `git checkout --`, stash away, or otherwise undo
work without the owner asking for it. This includes your own failed
experiments.

**Why:** a failed experiment is evidence. Reverting it destroys the
state the owner may want to inspect, question, or fix, and it makes the
owner's own review impossible: they cannot look at something that is
already gone. Deciding on their behalf that an attempt is worthless is
their call, not yours.

**How to apply:** when an experiment fails, STOP and report. Say what
was tried, what the numbers were, what you believe is wrong, and what
the options are (keep and iterate, keep behind a flag, revert). Then
wait. If the work is unstable and must not become the default, put it
behind an env lever or a branch and say so, but leave the code in place.
Commit failed experiments with honest messages rather than erasing them.

The same applies to deleting files, dropping branches, resetting, and
force-pushing.

## Ordo Malleus

An Ordo Malleus inquisition is on file: MALLEUS_INQUISITION.md. Consult it before touching schema or KG code.
