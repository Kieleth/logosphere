# Physics invariants ledger

Append-only log of every change to `INVARIANTS.jsonl` and to test↔invariant
links in `TEST_AUDIT.jsonl`. One entry per change: what, why, evidence.
The JSONL files are the machine truth; this ledger is the human trail that
keeps them honest. Never edit an old entry; correct with a new one.

Format: `### YYYY-MM-DD · ACTION · id` then a short body.

---

### 2026-08-13 · CREATE · INV-1..INV-15

Initial set, distilled from two sources: the standing engine invariants in
CLAUDE.md (INV-1 turtle-only-immovable, INV-5 no-springs-as-patch, INV-6
no-gravity-assumptions, INV-15 physics-blind-to-game) and the laws proven
by the 2026-08-10..12 physics-surgery campaign, each carrying the RCA that
earned it:

- INV-3 energy-transforms — the owner's reframe that reset the campaign:
  energy is not lost, it transforms. The ledger (ENERGY_LEDGER=1) is its
  instrument.
- INV-4 born-at-rest — generation-debt ledger G1..G6.
- INV-7 momentum-one-door — the sleeping branch that sank 6.74 m under its
  own leaves' damping.
- INV-8 rows-live-sized — the 245 m/s blade and the ±110 m/s heel-strike
  foot, both rows sized for a stale world.
- INV-9 derived-not-declared — 28 hollow branch bonds holding a canopy on
  nothing (G6).
- INV-10 mass-uniform-limits — the 1046:1 ringing ladder.
- INV-11 no-detonation — issue #42's always-on tripwire.
- INV-12 true-geometry-contacts — the walk-gate snowplow and the tipped
  plate resting on 276 mm of air.
- INV-13 driven-joints-hold — the shoulder standing error; two integral
  attempts measured worse and preserved as evidence commits.
- INV-14 tears-need-strain — bonds tearing between two resting satisfied
  bodies; measure what the bond enforces, not a proxy (G1c).
- INV-2 no-penetration — the settling gates and the wedged-boulder 72 m/s
  ejection that bounded repair speed.

Linking to tests happens in `TEST_AUDIT.jsonl` (one line per test: status,
what it proves vs what it merely touches, INV links). The sweep runner
(`scripts/physics_sweep.py`) joins the two at run time and reports broken
invariants, not just broken tests.

### 2026-08-13 · LINK · TEST_AUDIT.jsonl (initial, all 282 tests)

Every runnable test linked: 170 unified-runner registrations + 112
standalone binaries, judged from source + the 2026-08-12 full headless
sweep (all 282 run serially at head 5353f9c + the headless-default
conversions).

Status counts: 257 good · 11 legacy · 4 red-by-design · 6
interactive-only · 4 env-dependent. Expect: 257 pass · 15 fail · 10
skip. 31 good tests are currently red and carry `known_open`:
5 class-1-foot-sink (stance foot integrates through the floor, then a
re-queued particle trips TURTLE_STRICT — includes both CI smoke tests
and the guard bundle), 10 class-2-spawn-placement (scenes or the tree
generator place a body bottom below the turtle, mm to 1 m), and 16
singles.

**Coverage holes — invariants with zero proving tests:**

- **INV-5 no-springs-as-patch** (1 touching). Review-discipline
  invariant; nothing mechanical guards it.
- **INV-6 no-gravity-assumptions** (2 touching). No wall/ceiling/zero-g
  scenario exists anywhere in the fleet. test_butterfly_flight_liveliness
  and test_ground_locator brush the "no assumed heights" half only.
- **INV-15 physics-blind-to-game** (1 touching). The stated mechanism is
  a static grep audit; no test runs it. This is the cheapest hole to
  close: a headless test that greps src/core/physics_*.{h,cpp} for
  ParticleOwner reads.

All three holes are the "static" verification invariants. Every
runtime-verified invariant has at least one prover. Thin spots worth
naming: INV-8 rows-live-sized has exactly one prover
(test_immovable_pair_phantom_impulse; the walk gate exercises it but
asserts detonation/bonds, not row sizing), and INV-9
derived-not-declared's only prover (test_humanoid_tuning_coverage) is
currently red — the invariant is enforced at the bond doors but
unwitnessed while that test stays down.

**Judgments that differ from the sweep report's first impressions:**

- test_rotation_cascade_yaw: the sweep flagged it as a possible branch
  regression; physics_guard_runner.cpp marks it XFAIL "documented
  known-red". Kept good + known_open single:cascade-timing,
  pre-existing.
- test_branch_placement_ladder: judged red-by-design (the fourth
  ladder) — rungs encode analytically predicted gaps, red-first by
  construction (issue #57).
- test_grass_yields: kept good (expect pass), NOT red-by-design — the
  owner's ruling is an open bug that must stay loud, not future design.
- test_gluon_tree_v34, test_totem_gluon_nails, test_physics_experiment_01:
  legacy. Their declared break-force / free-fall-below-z=0 expectations
  predate INV-9's derivation law and INV-1's turtle respectively.
- The seven FK-era animation standalones + test_walk_locomotion:
  legacy per CMakeLists' own "stale FK-era reds" note. walk_locomotion
  additionally never terminates headless (4.3M frames at the 300 s
  kill) — the sweep runner must deadline it.
- The four env-dependent tests report wrong today: the two HTTP tests
  exit 1 (not skip) without a localhost:8000 server; at_logotron_
  director_mlx_smoke and eval_logogenesis_llm exit 0 when they skip.
  The runner needs an env mapping, or the tests need honest skips.
- test_strata_generator's header still claims "KNOWN FAILING AS OF
  2026-04-12"; it passes today. Header is stale, test is good.
- test_tile_sticking's header calls itself "TDD red"; classified good +
  known_open single:seam-normals — it guards an open bug (26 horizontal
  seam normals on the sweep), not future design. test_body_coherence
  shares the same class.
