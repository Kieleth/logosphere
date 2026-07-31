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

## Commits

Present-tense imperative subject with conventional prefixes
(`feat:`, `fix:`, `docs:`, `test:`, `refactor:`, `perf:`, `chore:`).
Body explains why, not what. Every commit carries a DCO sign-off
(`git commit -s`). Keep each PR to one logical change.
