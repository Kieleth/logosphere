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

### 2026-08-13 · UPDATE · INV-29 mechanism — registry landed, gate landed, residuals named

Stages B-E of the physics-schema campaign landed (commits a6ee723,
cb2d051, c1e0867, 150a6d9, 9652a08, 3a61032 + the Stage E gate):

- schema/physics.yaml: standalone LinkML root (never imported by the KG
  ontology — createEntity of a physics concept can never work). 37
  concepts, the Invariant record contract (this file's rows now
  validate with linkml-validate), and the constants registry: 12
  groups, 68 constants, each with value, UCUM unit, group and doc.
- generate_ontology.py header-only mode emits the registry as
  src/generated/physics_constants.h (namespace PhysicsV4).
- Extraction was VALUE-IDENTICAL by decree, proven the strong way:
  test_physics_characterization held bit-identical against pinned
  baseline c64f3caf02622e3f through all four extraction commits, and
  the harness stayed 27/27. Behavior gates on the final batch all
  passed (walk_through_grass 0 detonations / 0.30 m worst drift,
  foliage, both gluon drive convergence tests, light_body_ringing,
  settling_flat, rotated_box_contact, drive_gravity_ff).
- The gate: test_inv29_constants_gate, a self-checked magic-float
  ratchet over the physics TUs. Discrimination rule documented in the
  test: identity/algebra {0, 1, -1, 0.5, 2}, precision guards
  <= 1e-5, sentinels >= 1e9 are not magic; every other float literal
  is. Integer literals are not scanned (stated limitation: every
  physical integer input the census found is already in the registry).

**INV-29 stays ASPIRATIONAL.** The scan finds 35 residual float sites
in 17 token-groups, pinned exactly by the gate's KNOWN_RESIDUALS
table. Classification:

- Extraction candidates (physical, follow-up constant-work):
  physics_system_v4.cpp 0.05 (wake-check gap), 0.15 (persistent-
  contact gap), 0.1 x3 (horizontal-normal classifier, elastic gate),
  0.3 x3 (receding-speed wake gate, support-normal component), 0.95
  (damping impulse clamp), 0.999 x2 (cap guard), 0.99 (near-breaking
  warning), 5.0/24.0 (structural damping bounds), 0.001 x5 and
  0.01 x6 and 0.0001 x3 (mm-and-below dimensioned gates and pads).
- Algebra staying put: 4.0f x2 (hysteresis 2^2; I_sec = A^2/(4*pi)).
- Display conversions: explosion_detector.cpp 1000.0 x2 (J->kJ in the
  warning printf), 999.0 (ratio fallback when previous KE was 0).
- INV-9 tension worth its own follow-up: physics_system.h
  GluonConstraint defaults angular_stiffness 100.0 / angular_damping
  10.0 are DECLARED bond parameters; INV-9 says derive them.

Extraction lessons already banked: the census pattern (constexpr-only)
missed three NAMED const float constants (ANGULAR_BETA, MAX_OMEGA,
ANGULAR_DRAG — extracted in 3a61032); and the engine declares TWO
gravities, GRAVITY 9.8 (solver) vs ENERGY_LEDGER_G 9.81 (energy
ledger PE bucket), 0.1% apart — extracted AS FOUND, unification is
constant-work for a future ruling, not an extraction-time fix.

### 2026-08-13 · LINK · test_inv29_constants_gate, test_inv15_owner_blindness

Two headless standalone binaries added (add_headless_test, pure file
IO, run in both build profiles), both appended to TEST_AUDIT.jsonl (no
existing row touched):

- test_inv29_constants_gate proves INV-29's ratchet (above).
- test_inv15_owner_blindness closes the coverage hole this ledger
  named the cheapest to close (INV-15 had one touching test, zero
  provers). Expect-fail style per the task-#43 ruling: it PASSES while
  the owner reads in physics TUs are exactly the seven known
  quat-gravity-family sites (7 ParticleOwner / 7 .owner in
  physics_system_v4.cpp, lines 530/1876/1886/3082/3087/3225/3236 at
  time of landing, zero in every other TU) and FAILS when the count
  moves either way — a rise is a new bleed, a drop is task-#43
  progress that must shrink the test's table, the audit and this
  ledger in the same commit.

Both tests carry self-checks (the control: a synthetic violation must
be found, a comment/string occurrence must not) and were additionally
control-tested against the live tree: an injected 0.777f and an
injected ParticleOwner read each turned their gate red, then were
removed. INV-15's mechanism line now names its prover.

### 2026-08-13 · LINK · class-1 and class-2 cleared

The two largest known-open classes are fixed and their fourteen
clearances applied: class-1-foot-sink (turtle plane as support of last
resort in the humanoid ground probes, f36a479 — the walker finally has
the world floor under it when no floor particle qualifies) and
class-2-spawn-placement (scenes made legal + the leaning trunk's
oriented dip fixed at the generator arithmetic, 88b6bfe).
test_humanoid_movement reclassified single:locomotion-calibration
(117/126, nine distance near-misses; its two masking defects — grass
12.6 mm below turtle and a never-built KG body graph zeroing
capability — both fixed). New prover: test_turtle_ground_support
(INV-2 for walkers). test_soft_shadows recorded as VACUOUS.

### 2026-08-13 · RULINGS · gravity, INV-12 widened, INV-30, order-capture

Owner rulings, second round:

- GRAVITY IS NOT A CONSTANT. It is a gameplay lever available to games:
  a configurable engine INPUT (PhysicsConfig at init), one value read by
  solver and energy ledger alike. The 9.8-vs-9.81 split dissolves into
  one injected value with 9.81 as the documented default. The registry
  documents defaults, never truths. (Task #48.)
- INV-12 WIDENED with bounds-never-under-cover (miner candidate 16),
  per the merge ruling.
- INV-30 external-writers-place-nothing-illegal CREATED, enforcement
  strict-first: "harden first to catch issues and incorporate fast."
- Order/dependency capture between physics processes ruled a
  foundational micro-project: "if we do not capture order/dependence
  between physics, we'll not be able to build anything properly" —
  possibly via the ontology's process concepts + RDF export for logical
  automation. Study commissioned before any build.
- Doctrine recorded for lane priority: foundations before symptom
  fixes. The waste in physics work is not building foundation, metrics,
  assertions, laws, order-capture and enforcement — hacking on top is
  the failure mode. Coverage work (provers, INV-6 witnesses, audit
  links) outranks the known-open singles.

### 2026-08-13 · RULINGS · zero tolerance, silent fallbacks, malleus, order-capture build

- INV-29 ruled ZERO TOLERANCE (option a): no bare numeric literal
  anywhere in ENGINE code — not exempted as algebra, not as display.
  Even 2^2 must carry its meaning, captured ontologically, in a
  hierarchy of constants/config that makes sense; hidden/scattered
  meaning is the disease. Tests exempt. The ontology-physics bridge is
  the capture mechanism.
- SILENT FALLBACKS: DESTROY ALL in physics, the hard way — every
  default that fills in for missing/unset data, every silently
  inherited class default, every catch-and-continue. Let physics break
  loudly, then and only then flows are controlled. THIS BLOCKS THE PR
  MERGE by owner order. The gluon class defaults (angular 100/10)
  ruling is subsumed: destroyed, not derived, not registered. To be
  repeated across the entire engine after physics.
- MALLEUS_INQUISITION.md commands adhered to: H4 (materials 9-switch
  table, 50e6 x34 vs table 50x disagreement) is the same work stream as
  zero-tolerance + fallback destruction; H1 (setProperty validates
  nothing) is the KG-gate fallback. H1-H4 join the compliance queue.
- Order-capture: build approved per the study's Option 1 — sequenced in
  slices to avoid rewrites, no time framing (owner: slices properly
  sequenced, as slow as needed for fundamentals). D2: the schema's
  process concepts get RECUT to true pipeline granularity. D3: the
  layer-2 experiment runs; if the step-function holds, the conditional
  order-independence invariant is adopted.
- Eden/logotron merge verdict: keyboard is OS-level (other session
  owns it); owner satisfied in principle pending the fallback
  destruction, which now gates the merge.

### 2026-08-13 · EXECUTED · silent-fallback destruction, physics layer

The owner's PR-gating order carried out. Map + breakage inventory:
docs/todo_plans/SILENT_FALLBACK_INVENTORY.md (committed BEFORE the
code, per method). Counts:

- 40 candidate sites cataloged; 13 destroyed into loud refusals;
  19 judged declared-mechanism and kept (each with a written
  justification the review can veto); 8 catalogued for owner
  decisions.
- Destroyed, headline items: the GluonConstraintBase angular 100/10
  class defaults (the subsumed ruling — destroyed, not derived, not
  registered; 0/0 is now the UNDECLARED sentinel and the bond doors
  refuse consumers without a law); a new breaking-force>0 door on
  EVERY bond; the third copy of the S22 eff-mass phantom (turtle
  row); the invented I=0.01 inertia; the silent NaN-velocity reset;
  the INV-22 duplicate-pair overwrite; stale-gluon repair-and-
  continue; silent bond drops at both creation doors; the +Z default
  constraint jacobian; the detector's 999.0 display invention and
  silently-ignored env ceiling.
- Levers: GLUON_LENIENT (bond doors, existing), PHYSICS_LENIENT
  (new, solver refusals). TURTLE_LENIENT untouched.
- Breakage: full strict sweep (285 tests) found ONE real new red —
  test_spirit_light_artifacts, SIGABRT at the duplicate-pair door,
  root: ParticleSystem::clear_particles resets ids but never clears
  bonds, so a respawned scene re-binds 28 stale bonds from the dead
  world. Audit row reclassified fallback-destruction:GLUON_LENIENT;
  the lifecycle fix is an owner decision. The other three moles:
  the INV-29 ratchet's sanctioned drop (table shrunk 35 -> 31 in the
  same effort, gate green) and two solo-unreproducible
  infrastructure flakes (physics_battery HUNG at deadline, 2/2 PASS
  solo at 0.19 s; async_prep_equivalence, PASS solo).
- Every other new door fired in ZERO of 285 tests: those flows are
  armed and today's fleet passes them clean.
- test_humanoid_tuning_coverage stays red by the same disease it
  always named, now stated honestly: 8 head bonds carrying 0/0
  (absence) instead of 100/10 (a number pretending to be a choice).
### 2026-08-13 · LINK · Malleus H1 closed — the KG property door

`KGCore::setProperty` validated nothing while the LLM KGOp path ran
full validation; ontology_registry.h:46 claimed otherwise. Closed with
the shared check (`kg::validate_property_write`: declared-on-type +
value-type coercion + schema min/max) now run by BOTH paths — one
implementation, no drift. Strict by default (print + abort, turtle-door
pattern); `KG_GATE_LENIENT=1` prints and allows (inventory mode).
Empty registry (no ontology loaded) skips, same guard createEntity has
had since the registry existed. Cost measured by
`test_kg_property_gate`: 153 ns/call for the check, 193 ns for full
setProperty; the full KGOp validator was rejected for the engine side
because its type-resolution walk is O(entities) — 744 ns/call at only
2001 entities and growing linearly.

The lenient inventory across the harness + ctest + gate battery found
49 distinct (type,key) violators, all missing declarations, none
wrong-typed. All engine writes are now declared (~130 new slots across
schema/logosphere.yaml + packs/earth.yaml, including the dotted
`cap.*` and bounded `rule.0-7.*` namespaces via the new `kg_key` slot
annotation). Wrong writes destroyed instead of declared: five
redundant `type` property stamps (shadowing Entity::type, zero
readers), the humanoid's write-only `behavior` stamp, the organic
generator's `growth_is_mature` drift (declared name is `is_mature`),
and the butterfly's unbounded `thorax_<i>`/`*_wing_<i>` keys
(refactored to comma lists, the humanoid limb-chain pattern).
test_walk_through_grass extended the ontology by hand with mixin-less
ancestors; it now loads the real earth pack.

Prover: test_kg_property_gate (14 checks: three rejection classes
observed as SIGABRT with the actionable message asserted, pass-path
controls, lenient semantics, measured costs printed).

## 2026-08-14 — Wake is solved physics (owner decree, machine catch #1)

The Rube Goldberg machine's S3 red RCA'd to the contact wake gate:
`(m_a/(m_a+m_s))·speed >= WAKE_TRANSFER_SPEED (1.0 m/s)`, a pre-solve
guess. Below the gate a sleeper is priced immovable and the mover's
momentum is destroyed (probe f271: ball+box0 at 1.67 m/s, 343 kg·m/s
-> ~-24 in one frame against sleeping box1, equal mass, which never
woke). Owner ruling, verbatim intent: "we need a smart solver with
law-INV founded rules, not a simple 1 m/s interaction; the awake
moment needs to be calculated, or even pre-calculated; a grain of
sand hit by a 1 m/s particle is not the same as a castle wall hit by
a grain at 1 m/s. A sleep-awake-resolver, ontology-INV driven."
Becomes INV-31 (aspirational) + task #56. Red ladder first; mechanism
behind a default-off lever; WAKE_TRANSFER_SPEED path deleted only
after the resolver goes green through machine + sweep + Eden
characterization.

## 2026-08-14 — The rest damper is eradicated (owner decree)

RCA of machine S3 / resolver R1 found the frame-gated damper
(DAMPING_FACTOR 0.90/tick below 0.4 m/s, counter-gated, reset only
above 0.8 m/s) destroying real coasting momentum: 72 kg*m/s -> 11 in
17 frames on a mu=0.02 floor, with contacts ferrying neighbours'
momentum into the sink. Owner: "we need to eradicate this, quick...
this dampening is a hack, the same as spring-modeling. A dampening is
a dampening if it's a dampening; anything else is energy transference
between different materials and consequences of that — physics."
Sequencing also owner-ruled: damper first, THEN the manifold pricing
(INV-20 x0.75 cascade) and the wake gate (INV-31), because those
calibrate against honest physics only after the artificial sink is
gone. Evidence after removal: resolver R1 awake twin CONSERVES
(85.5 -> 63.9 vs 30.1 budget); ringing 1044:1, settling flat/wiggle,
idle pose, walk gate, battery, foliage all green. The damper's
constants stay in the registry until the cleanup commit.

## 2026-08-14 — WITHDRAWN: the "manifold overshoot" (defect 2)

I reported the manifold rows as pricing with the UNSPLIT effective
mass (INV-20 violation, "3.5x overshoot") from a level-5 trace
showing eff=12.8 on all four rows of a face contact plus an exact
x0.75 impulse decay. Both readings were wrong:

- The traced field is `c.effective_mass`, which is ALREADY the split
  share (physics_system_v4.cpp: `c.effective_mass = effective_mass *
  c.eff_mass_share`, share = 1/N with SPLIT_OFF unset). eff_full was
  51.2 (target priced immovable while asleep), 12.8 per row.
- The x0.75 decay is the CORRECT Gauss-Seidel signature of a properly
  split manifold: each of four rows removes 1/4 of the remaining
  approach speed, and 1 + .75 + .5625 + ... converges to
  eff_full * v_rel = 76.15 N*s — exactly the right total.

Measured proof, now permanent as ladder rung R5 (equal masses,
frictionless, both awake): both bodies end at +0.795/+0.796 m/s
against an analytic +0.800, momentum conserved to 0.54%.

Same failure class as the five withdrawn conclusions already in this
ledger: a number read without checking its definition. The cure this
time is a prover in the tree, not a resolution to be careful.

Standing after this: defect 1 (rest damper) was real and is
eradicated; defect 3 (the wake gate, INV-31) is real and open.

## 2026-08-14 — Sleep-awake resolver: mechanism landed behind WAKE_RESOLVER

INV-31's mechanism exists (physics_system_v4.cpp: predicate no longer
claims sleep is immovability, gravity skips sleepers on the cache's
own reason, resolve_sleep_wakes judges the SOLVED velocity against
REST_VELOCITY_THRESHOLD). Measured with WAKE_RESOLVER=1:

- Ladder R1: the sleeping target now behaves EXACTLY like the awake
  twin (0.690/0.691 vs 0.690/0.691, delta 0.001; momentum 70.8 vs
  70.7 against a 23.4 budget). The cache is invisible. 12/12 rungs.
- Machine: 3/10 -> 5/10. S3 (Newton's alley) and S4 (the wake-up:
  a two-second-asleep stack registers an arrival, wakes, carries it)
  both green for the first time.
- Two chased-down door bugs found on the way, both INV-7/INV-20
  violations that predate the resolver: the contact row build carried
  its own inline immovability opinion (no KINEMATIC check), and the
  apply site re-asked the question with `!pb.is_at_rest` on top of an
  inv_mb that already answered it — body A paying an impulse body B
  never received.

OPEN, and the reason the default is still OFF: with the resolver on,
test_light_body_ringing and test_grass_yields go red. Both involve
KINEMATIC anchors, and the resolver's pricing correction changes the
effective mass of every kinematic contact (a KINEMATIC body that is
not at_rest used to be priced MOVABLE while the apply refused to move
it). Whether those two tests encode the old mismatch or a real
regression is the next question, and it decides the flip.

## 2026-08-14 — KINEMATIC is a transient authority; STATIC is eradicated

The audit (docs/todo_plans/KINEMATIC_AUDIT.md) found the repo's own
written doctrine contradicting the owner's: schema/logosphere.yaml,
INV-1's text and entity_physical_state.h all declared KINEMATIC the
sanctioned way to make scenery permanently immovable, and 11 sites
followed that spec. src/ had 12 SET sites and 4 RELEASE sites; every
release lives in humanoid_locomotion; worldgen and examples release
nothing. Consequence: a humanoid's hips are pinned by a path neither
release reaches, so it cannot be knocked over or ragdoll (KNOCKBACK
is an enum with no implementation, and test_humanoid_impact asserts a
displacement that is structurally zero).

OWNER RULING:
1. `ParticleSolverMode::STATIC` — "needs to be totally eradicated,
   legacy." It was accepted from the KG and handled by NOTHING in the
   solver: a body set STATIC fell silently. Done in this commit.
2. KINEMATIC stays, ONLY as the volitional/animation escape hatch:
   "driving complex animations via physics is just insane for us to
   try" — a concession made deliberately, with eyes open.
3. KINEMATIC is a STATE, not a constant: "set when needed, i.e.
   animation, but then RELEASED so physics can act normally on them
   when no volition is done or animation complex is in effect."
   A set with no release is a pin, and a pin is the HEAVY_STATIC
   disease wearing a new mask.
4. Immobility for scenery is NOT a pin. The direction instead:
   "instead of using KINEMATIC for things that do not move, we could
   SIMULATE THE EFFECT of particles we do not need to model, to
   achieve gravity for example" — the unmodelled substrate as a
   field, which is also where gravity comes from. "And for floating
   things, we allow particles to escape gravity effect on them" — an
   exemption from the field, not a nail in the air.

This supersedes the schema text and INV-1's mechanism note, both
rewritten here. The SET/RELEASE table and the bucket-B site list are
the action lists; the substrate direction subsumes task #48 (gravity
as an input): gravity stops being a constant and becomes the effect
of mass we chose not to simulate.
