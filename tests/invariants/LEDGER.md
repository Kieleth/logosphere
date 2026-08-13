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

### 2026-08-13 · LINK · +17 phantom registry names: all dead, none audited

The first validated sweep (head adaedb8) reported 17 registry tests as
UNAUDITED that the audit's population did not contain. Root cause is an
enumeration-method gap: physics_sweep.py's discover_registry() ran
re.findall over the whole of src/unified_test_runner.cpp, which matches
registrations inside FULL-LINE COMMENTS; the audit enumerated only
uncommented `registry[` lines. The 17 are all commented out — a "TODO:
Convert remaining tests to use TestContext" block (15 names) plus two
tombstones ("REMOVED: dead feature" test_load_bearing, "FILE MISSING"
test_gpu_multi_triangle).

None is a runnable test. Their sweep rows prove it: every one FAILed
with rc=1 in 0.1 s — the harness's "Unknown test" error branch, before
any engine or test body exists. Fourteen exist only as `return false`
stubs in tests/test_stubs.cpp (link fillers for the old grid API);
test_load_bearing and test_gpu_multi_triangle have no function anywhere
(the is_load_bearing_ field was removed from the engine — nothing here
for the INV-2/INV-8 thin-coverage list); test_shadow_casting was only
ever a commented alias for test_basic_shadow_casting.

Resolution: the audit enumeration was right; the runner's was reading
dead code. discover_registry() now strips full-line comments and
returns exactly the audit's 170 registry names. No TEST_AUDIT.jsonl
entries added — auditing ghosts would turn tombstones into obligations.
If anyone uncomments the TODO block, those names instantly surface as
UNAUDITED moles on the next sweep, which is the correct tripwire.
Coverage numbers are unchanged.

### 2026-08-13 · CREATE · INV-16 (first aspirational invariant)

Owner ruling: invariants MAY be born red. `status: "aspirational"` means
the statement is diagnosed true and the code does not yet honor it; the
sweep reports it as standing debt, never as a mole, and it appears in
the coverage-hole list intentionally until the mechanism ships and a
prover exists. INV-16 anchor-pivot-law is the first: correct mechanics
(rotation-ladder rung 3 evidence), unshippable through independent
scalar rows (measured unstable at 99.6 m/s), waiting on a full-Jacobian
row formulation.

### 2026-08-13 · RULING · INV-15 violations + orientation truth

- INV-15: seven live owner-reads in physics_system_v4.cpp (the
  quat-gravity exemption family) ruled a FIX TASK, not a sanctioned
  annotation. Tracked as task #43; the static-grep prover test lands
  with the fix.
- one-orientation-truth (miner candidate 14): ruling DEFERRED by the
  owner until the rotation-ladder work forces the representation
  question. Parked here; not written as an invariant.

### 2026-08-13 · CREATE · INV-17..INV-28 (the miner's 12, owner-amended)

All twelve adopted after per-item owner rulings, with two schema
additions retrofitted to every entry: `kind` (law | corollary |
convention | modeling-boundary | architecture) and `derives_from`,
because several of these are consequences, and the file must say what
they are consequences OF. The owner's rulings, condensed:

- INV-17 contact-never-amplifies: adopted AS a consequence of energy
  conservation (a contact is passive, restitution <= 1) — kind
  corollary, derives from INV-3.
- INV-18 sleep-hides-nothing: adopted with the owner's framing baked
  in: sleep is a masked CACHE over dynamics; wake conditions are its
  dirty-hit invalidation strategies.
- INV-19 damping-is-physical-dissipation: STRENGTHENED beyond the
  candidate. Owner: damping is a smell of uncaptured physics unless it
  models real dissipation (KE -> heat, which leaves the system and is
  booked). Only then, and only on relative/internal motion. This also
  commits the ledger to a dissipation bucket (task #44).
- INV-20 size-and-spend-in-the-same-model: adopted, grounded in
  momentum conservation — mismatched metrics deliver momentum the
  books never saw.
- INV-21 no-position-bias-in-momentum: adopted as kind
  modeling-boundary, the owner's words honored: a necessary hack —
  we cannot simulate microscopic contact physics, so repairs get
  wiggle room between the cracks, bounded to imperceptibility
  (micro-corrections well under ~300 ms; few-frame adjustments OK)
  and NEVER entering momentum. The perceptual bounds are to be tuned
  in play.
- INV-22 one-mechanism-owns-a-pair: adopted as corollary with sources
  named (INV-7 momentum door + INV-3), per the owner's ruling that a
  derived consequence must cite what generates it.
- INV-23, INV-24, INV-26: adopted as proposed.
- INV-25 contact-normal-has-one-sign: adopted as kind CONVENTION —
  the owner's distinction: this is how the engine makes sense of
  physics' effects, not a law of the world.
- INV-27 deterministic-given-seeds: REWORDED from plain determinism:
  bit-identical GIVEN SEEDS, with all randomness through declared
  seeded channels — designing ahead for dice throws and probabilistic
  effects the owner wants possible.
- INV-28 one-attachment-point-definition: adopted as kind
  architecture — the owner: the connection between software
  engineering and the physics engine.

File now holds 28 invariants: 15 charter + INV-16 aspirational + these
twelve. Coverage links for 17-28 are OWED (TEST_AUDIT proves/touches
refer to 1-15 only); until linked they appear as coverage holes, which
is correct and loud.

### 2026-08-13 · CREATE · INV-29 constants-are-inputs (owner decree)

Dictated by the owner as a true invariant of this engine: not one magic
number in the physics engine. Constants are INPUTS that prepare and
adjust the engine; tweaking/adding/deleting/grouping them is an
explicit effort separated from writing the code that uses them, which
keeps the code honest about pursuing underlying solutions rather than
higher-level patches. Born aspirational: the code holds ~50 named
constants and ~20 in-function literals today; the invariant flips to
active when the extraction lands and the static gate proves zero
remaining literals.

Same ruling reversed the study's sequencing: the physics concept
ontology (schema/physics.yaml) comes FIRST as the base where the
validation/hardening work flourishes, drift fixes folded into its
first stage; the constants registry is part of that schema. The
study's Option 4 rejection is overruled with the resolving insight:
constants as build-time INPUTS (schema -> generated header) do not
violate physics' runtime blindness (INV-15) — the engine receives its
configuration, it does not read the game's ontology.
