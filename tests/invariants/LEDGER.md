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

## 2026-08-14 — The board (coherence over throughput)

The owner: "it's getting really hard for me to manage all these open
fronts and advance/provide guidance in a coherent mode. I need help to
capture all this in the documentation/ledger and divide-and-conquer,
study what needs to be fixed/can be fixed without accumulating more
tech debt, and which things require deep, long design."

docs/todo_plans/PHYSICS_BOARD.md is that page: every open front in
exactly one class — CLEAN NOW (mechanism known, no debt, no decision
owed), NEEDS DESIGN (a study exists or is owed), OWNER RULING
(engineering ready, decision blocked), PARKED (real, understood, with
the reason). Every entry cites its evidence.

Standing rule from here: a front that is not on the board does not get
worked, and a front that moves class gets a ledger line saying why.
The board is maintained in the same commit as the work it describes,
like the skill and the invariants.

Recommended order recorded there: C1-C4 (harness truth, the vacuous
impact test, the two unfixed friction applies, the sleeping body that
can spin) → authority slices S0-S2 (specified bit-identical) → the
WAKE_RESOLVER flip → rotation after authority owns the shared inertia
predicate. The substrate study runs in parallel because it is design
only and blocks the largest parked item (the worldgen pins).

## 2026-08-14 — Board C1-C4 landed; front F1 opened

The four CLEAN NOW items are done and the board moved in the same
commit, per the skill's directive. C3 (four friction sites) and C4
(sleep owns its angular half) are engine changes measured
bit-identical on the default path. C1 (BVH in the knockback fixture)
and C2 (real assertions in the impact test) are harness truth.

C1 immediately opened F1: with contacts finally forming, a boulder at
8 m/s strikes a humanoid chest, stops dead, and the momentum is
destroyed outright — nothing moves and nothing is booked. That is the
four-link authority chain the motion-authority study describes, seen
end to end, and it belongs to D1's ladder rather than to a patch.

Also noted on the board: a humanoid books -14.23 N*s downward every
frame from its own thigh resting on its KINEMATIC hips. Structural
refusals and external shoves are currently indistinguishable in the
ledger; whoever consumes it will need them separated.

## 2026-08-14 — F1 RCA: the animation erases what the solver delivered

Full RCA (docs/todo_plans/F1_MOMENTUM_LOSS_RCA.md) refuted the standing
hypothesis. Physics is not destroying the momentum: the solver delivers
89-110 kg*m/s per frame into the struck body parts, with zero
BOTH_IMMOVABLE rows. HumanoidLocomotion::update_locomotion then
broadcasts ONE hips-derived scalar onto all seventeen particles
(humanoid_locomotion.cpp:4440-4442) and maintain_entity_shape snaps
their positions back (:5449). The hips are KINEMATIC, so the broadcast
value is 0 and the erasure is total — outside every door, booked
nowhere, every frame, for every standing registered humanoid.

The load-bearing property is NOT is_quat_driven, NOT ParticleOwner and
NOT sleep: in plain two-box isolation all five DYNAMIC variants are
bit-identical to the control, and only KINEMATIC stops a body. The
four-link chain is refuted at link 4 (the chest is never asleep at
impact).

Two of my published readings were wrong and are retracted here: the
chest DOES move (1.593 m in 20 frames) when the locomotion writer is
not running, and "8.00 -> -0.00 m/s" was the boulder landing on the
turtle downrange, not the strike — the same velocity-minimum statistic
the motion-authority study had already flagged, used twice.

The RCA also found my own refusal ledger books 2.5% of the truth (1357.8
kg*m/s refused, 33.6 booked): the friction block books nothing and the
warm-start apply spends outside the booking loop. That is now board
item C8 and precedes everything else, because a drain that receives
2.5% of the truth is worse than none.

F1 becomes D1 slice S5b: "the FK rig holds what it writes, and drains
its book."

## 2026-08-14 — C8: the refusal ledger is complete (2.5% -> 99.9%)

The warm-start apply and the entire friction block spent momentum
outside the booking loop, so the ledger I added yesterday held 33.6 of
1357.8 kg*m/s. Both doors now book. Measured by the new
test_refused_momentum_ledger (striker 160 kg at 9 m/s into a braced
KINEMATIC target, airborne so the turtle cannot muddy the accounting):
1440.2 refused, 1439.2 booked, 99.9%, and the braced body does not
move. The striker's own momentum delta is the truth the book is
checked against — no expected value invented.

Known and stated: linear only. Angular refusals are booked nowhere,
because the angular side still has no door at all (board D1/D2).

## 2026-08-15 — C9 landed, C6 advanced: INV-6 has a witness at last

C9: every build step in scripts/precheck_linux.sh now names itself and
prints the real compiler output on failure. It proved itself on its
first run by diagnosing "Killed signal terminated program cc1plus" —
the OOM killer, caused by running two prechecks concurrently, not by
any code defect. Solo rerun: PRECHECK GREEN. Prechecks run ALONE, like
the sweep.

C6: unproven invariants 14 -> 11. INV-6 (no-gravity-assumptions) is
WITNESSED for the first time since the engine's first month — the same
press applied on +X, +Y and -Z resolves identically (penetration
spread 0.000000 m, resting-gap spread 0.000169 m, normal-direction
residual under 1e-5 m/s). Three false readings on the way there, all
recorded in the test: gravity left on made the axes non-mirrors;
cancelling it AFTER the solve left exactly 9.8/60 = 0.16333 m/s of
uneaten correction that read as a 4x bias; and measuring the full
velocity vector reported a tangential drift (gravity is applied once
per SUBSTEP, a test can only cancel once per frame) as a normal-axis
bias. The invariant concerns the contact normal, so the normal is what
is measured.

INV-27 and INV-23 gained coverage with no new code:
test_determinism_guards and test_immovable_pair_phantom_impulse both
existed and were never linked, so two laws read as uncovered while
their provers sat in the tree. Half of task #45, closed as a side
effect of asking the audit the right question.

## 2026-08-15 — C7: the energy ledger says where the joules went

INV-19 promised that damping exists only where a real dissipation
process is modelled AND that the conversion is booked. The first half
was enforced (the rest damper was eradicated for failing it); the
second half was never built, so every joule that left the world left
silently.

Four buckets now, each naming a process rather than a fudge: friction
(Coulomb work at contacts), material (gluon damping, c = eta*sqrt(k*mu)),
drag (quadratic air drag at integration), and sleep — the residue the
cache absorbs below its quietness bound. Sleep is booked SEPARATELY on
purpose: it is not a physical process, it is an optimisation, and
keeping it in its own column means it can never hide inside the honest
ones.

Measured on the machine: 26.3 J of friction at the impact frame against
a -67.6 J contact row, drag rising quadratically through free fall,
cache absorption in microjoules, and near-zero everywhere the world is
quiet.

## 2026-08-15 — C5: the bridge is sized by the tear law, and one leg is short

I had written that the bridge needed "an impulsive load — a fall, not a
slide." Reading the tear law disproved it: for welds (nails) the engine
computes force = impulse/dt each frame and tears only after 12
CONSECUTIVE frames at or above the threshold. An impulse cannot break a
nail; a WEIGHT can. So the bridge is rebuilt to be stood on — it fills
the gap flush with the deck instead of resting on top of it, and its
nails are sized from the arithmetic rather than invented:

  plank alone    90.0 kg ->  883 N -> 441 N/nail  (63% of a 700 N hold)
  plank + post  152.5 kg -> 1496 N -> 748 N/nail  (107%)

A bridge that carries itself and fails under the load it was never
meant to take. The old 2200 N was a number I made up.

What remains is 0.30 m of geometry, and the measurements say it cannot
be nudged: the post stops at x=7.61 and the bridge begins at 7.90.
Moving the post toward the bridge makes it worse, because the arrival
is ballistic — a post at 7.00 is struck lower and leaves with 0.43 m/s
instead of 0.86, stopping at 7.24. Strike strength falls faster than
distance does. The leg needs a body that arrives ON the bridge, which
is a redesign of that link and stays on the board as C5's remainder.

## 2026-08-15 — Instrument the interactions (owner directive)

"For each interaction in a test, internal checks need to be
instrumented and asserted with enough granularity that we understand
that the code is doing what it does. Also, bonus: these assertions in
time will need to be abstracted as physical interactions that we can
port back to the engine, with semantics like 'hit', 'rolling',
'impact' starting to have ontology-sourced meaning inside our engine —
they mean the same everywhere and can be used as building blocks later
on."

Recorded in the physics skill as a directive. The immediate
consequence: the Rube Goldberg machine asserted OUTCOMES (where bodies
ended up) while the prose described INTERACTIONS (the ball takes the
ramp, each box strikes the next, the flying box catches the post square
in the side) — none of which were checked. A position can be produced
by the wrong mechanism and look identical.

PhysicsSystem::get_collision_events() already reports every contact
with both bodies, the world point, the normal and the approach speed,
so the machine can assert the strikes it names.

## 2026-08-15 — The interaction router (owner vision)

While walking authority Q1 (what happens when a driven limb is struck),
the owner reframed the whole question. It is not "what does a struck
limb do" but "WHAT ROUTES THIS INTERACTION, AND WHO MAY INFLUENCE THE
ROUTING". In their words:

"My idea was always to have a ROUTER between these, like a networking
router actually, where we can load/pre-load/dynamically load ROUTING
between interactions and entities, even HIERARCHICALLY — humanoid->torso
vs humanoid->arm->hand->finger — and any other entity, and being able to
CATCH the event/interaction between particles, and allow the routing to
be MODIFIED BY ANY OTHER SYSTEM, like a combat system: if I want a
combat system that subscribes when weapons are used, I want to be able
to influence what my silver-bullet does against a werewolf vs a hippo."

"This is where ontology-powered semantics for these interactions and
COMPOSABLE interactions come into play, which would allow rules in the
KG to control these interactions with minimal code. Once those building
blocks exist: 'hand is burning, entity retracts in panic' -> engine
understands that animation, hand flies and hits a cabinet, hand
bounces... a chain of semantically rich, physics-aware, animation-capable
sequences that look and feel realistic, CREATED FROM SEMANTICS."

Boarded as D0, at the head of NEEDS DESIGN, because it is the general
case that D1's Q1 is a special case of. The scene that motivated it —
Eva walks into a tree trunk, and the correct outcome is "Eva stops, the
rooted trunk does not move, nobody gains energy" — is one the engine
cannot express today: her momentum is refused by her own authority and
the plant absorbs it, which is what tore canopies off trees earlier in
the campaign.

Also recorded: the vocabulary this produces (hit, rest, slide, bounce,
catch) is the same vocabulary the 2026-08-15 test-instrumentation
directive asks tests to build toward. Tests assert interactions; the
router routes them; the ontology names them. Same words, one meaning,
everywhere.

An Opus excavation of logomancers' combat system (design/
GAME_DESIGN_COMBAT_ENGINE.md, src/combat_system.*) is running to
retrieve what was already prototyped there — the wins and the painful
parts both.

## 2026-08-15 — Kamaji: SUBSUME (Q1 ruled), and effects must compose

The interaction router is named **KAMAJI**, after the six-armed boiler
keeper of Spirited Away who receives everything and routes it, one
specific arm to one specific drawer. Named beings with a job, like the
Turtle, Silk, Forge, Malleus and Weirden.

**Q1 RULED: SUBSUME.** Kamaji replaces TransformationRule's contact
triggers rather than sitting beside them. One place to look for "what
happens when things touch"; the old path is deleted, not left coexisting
(the repo rule for replacing a mechanism).

**And the owner raised what the study had not:** effects must be able to
INTERACT WITH EACH OTHER. When several are in play at once the outcome
must be resolved deterministically — "a logical, prolog way to solve
when multiple things are in effect at the same time". That is more
design than either study anticipated, and the owner's method for it:

1. Start from the SIMPLEST possible mental experiments — bullet-particle
   vs wall-particle, sphere vs mud — each captured the way an invariant
   is captured, in its own registry.
2. For each, study which effects must be in place so that the SUBSUMED
   COMBINATION produces the right overall behaviour, across animation
   and physics together.
3. From those, derive which language and semantic operations we allow
   over interactions: capturing the effect first and then operating on
   it — encode/decode inside rich interactions.

The proper term for these, adopted here: **GEDANKENEXPERIMENT**
(Einstein's own word), recorded as GEDANKEN-N in tests/invariants/
GEDANKEN.jsonl, beside the INVs.

Q2-Q8 are parked as TODO-to-check rather than answered now; the owner's
judgement is that they resolve one way or another once composition is
designed. Two studies are dispatched: one to inventory how simultaneous
effects are resolved in the tree today, one to build the
Gedankenexperiment set and the effect algebra. They synthesise here into
an implementation attack.

## 2026-08-15 — Doctrine RULED: escalation, not exception (the DIKW ladder)

The question put to the owner: `docs/RULE_LANGUAGE.md:945-947` bans
conflict sets by decision; Kamaji fires on pushed occurrences rather
than on graph state. Reconcile or contradict?

**RULED: reconcile, by making resolution a LADDER rather than a single
mechanism.** The owner's frame is DIKW: lower rungs are fast and
automatic, higher rungs are slower and handle what the rung below could
not order. The ruling's objection is to a resolver that silently picks.
A ladder that orders deterministically, and escalates only what it
provably cannot order, is not that resolver.

The four rungs, and what each one costs:

- **Rung 0, DATA. The physics fact.** The occurrence itself. No
  routing, no table, no cost beyond producing it.
- **Rung 1, INFORMATION. The table.** Address match plus the syntactic
  specificity key, compiled at load, compared over a sorted vector
  (router design 3.3.3). Per-frame, deterministic.
- **Rung 2, KNOWLEDGE. The ontology.** When rung 1 ties, the type
  lattice breaks it: a route naming a subtype outranks a route naming
  its supertype. Deterministic and still per-frame cheap, because the
  transitive ancestor sets are precomputed at load
  (`OntologyRegistry::ancestorsOf`, `ontology_registry.h:314`;
  `isSubtypeOf`, `:197`). **This rung is what the ruling adds, and it
  did not exist in the design before it.**
- **Rung 3, WISDOM. The escalation.** What two rungs of ordering cannot
  separate is a genuine authoring ambiguity. Today the design fails it
  closed at load (3.3.3: an unresolvable tie is a load-time error
  naming both routes, neither installed). Under this ruling it may
  instead be escalated, including to an LLM.

**The escalation contract, which is what keeps INV-27 intact.**

1. **It never runs inside the frame.** Not once, not cached, not
   asynchronously. A frame that reaches an unordered tie uses the
   fail-closed answer, refuse and report.
2. **Its output is never an outcome.** It emits a ROUTE, a `precedence`
   declaration, or an ontology edit. It writes rules, not results.
3. **Therefore the same conflict escalates exactly once.** The
   investment mutates the table; the table is what runs. This is the
   feedback loop the owner required, and it is the reason determinism
   survives: at runtime the answer always comes from a compiled table.
4. **What it emits carries provenance**: the conflict that triggered
   it, the corpus it reasoned over, the version that produced it. A
   human can read why the table says what it says, and revoke it.

**Measured, not asserted: the escalation rung currently has nothing to
do.** Of the ten Gedankenexperimente, six are settled and four are open
(GEDANKEN-3, 4, 5, 7). None of the four is open because ordering
failed. Two are blocked on missing engine physics (GEDANKEN-4 is F3's
mispricing, GEDANKEN-7 needs the restitution F4 says does not exist),
and two are owner rulings about how far a route's authority reaches
(GEDANKEN-3 may a route name a structural break, GEDANKEN-5 absorb
versus release). The deterministic rungs missed zero of ten. That
supports the owner's expectation that the large majority resolves
without escalation, with the honest caveat that ten hand-built cases
are a small and self-selected corpus. **Design rung 3 now, build it
last.**

**What the ruling changes in the design, concretely.** The specificity
key at router design 3.3.3 is purely syntactic: literal segment count,
property-filter count, chain depth. It has no notion of the type
lattice. So two routes whose addresses have the same SHAPE but name a
subtype and its supertype tie on every key and produce a load-time
error, when one of them is strictly more specific by inheritance. That
is the defect rung 2 repairs. The tiebreak is added AFTER the existing
syntactic keys, so every case that resolves today resolves identically
and only present-day load errors change behaviour.

**Owed to the owner (not decided here):** whether subsumption depth
should instead outrank property-filter count inside the measure. That
ordering decides which wins between a route naming a subtype and a
route naming the supertype with a property filter. Boarded, not
assumed.

**Also recorded, on the owner's reduction-to-tuples frame.** The
reduction they describe already has a countable size in the algebra: 8
occurrence kinds (CLOSED), 14 measured interactions (a floor, games
extend), 9 effects, 5 operators. The closed 8 are the alphabet; the
rest is composition over it.

## 2026-08-15 — R5 RULED, and the `else` branch is physics

Three owner decisions, taken together.

**1. CORRECTION: the ontology is read at load, never in a frame.** The
ladder as first written implied rungs firing in sequence at runtime.
Wrong framing. Rungs 1 and 2 are compile steps: they compute each
route's specificity key and sort the table once, at load. A frame walks
an already-sorted vector. Nothing consults the type lattice per contact
and nothing may. The table is armed before the world runs; that is the
whole point of the rung 3 contract (its output is a route, so it too is
spent at load). Router design 3.3.4 corrected.

**2. R5 RULED: compute both, and let the HIERARCHY declare which
wins.** The question was whether a route naming a type outranks a route
naming that type's ancestor plus a property filter. The ruling rejects
a single global answer, and the reason is that different hierarchies
mean different things: some are specialisation ladders where a subtype
is meant to override its parent the way `super` is overridden, and some
are encapsulation boundaries where the type is a closed box and a
filter on the parent has no business reaching inside it.

So both measures are computed, and the policy is DECLARED per
hierarchy in the schema rather than fixed in the scorer. Two named
policies to specify:

- **SPECIALISE**: subsumption depth outranks property-filter count. The
  subtype's route wins. `super`-style.
- **ENCAPSULATE**: property-filter count outranks subsumption depth,
  which is today's behaviour.

This is INV-29's shape applied to routing: the constant becomes a
declared input, owned by whoever authored the hierarchy and knows what
it means. It does not dissolve the set-overlap finding (a type route
and an ancestor-plus-filter route match crossing sets, because
`state.set` can mutate the filtered property mid-play). It converts an
underivable fact into a declared one, which is the honest move.

Owed before the scorer slice: the DEFAULT policy for a hierarchy that
declares nothing, and whether the annotation sits on the root type or
on every type.

**3. NEW: the `else` branch is a physics default, and it is counted.**
Owner: "we cannot capture 100% of the interactions of a rich world, but
we can approximate via physics." When no route claims an occurrence,
the engine does not fall silent. It emits the PHYSICS RESPONSE: the
outcome derived from what the solver already computed plus the
materials involved (density, friction, restitution when F4 delivers
it), named from the measured floor in the effect algebra 5.2 (`touch`,
`impact`, `rest`, `slide`, `stop`, `block`, `separate`). No game
meaning, no semantics, just the word physics has earned.

This is GEDANKEN-1's identity case made non-empty: the run with an
empty route table stays bit-identical in MOTION, and gains a name.

**And it is instrumented.** Every fall-through increments a counter
keyed by (occurrence kind, type pair). The counter is the statistical
instrument for finding which unauthored interactions actually matter in
a running world, so authoring effort follows measured frequency instead
of imagination. The owner's framing: the Gedankenexperimente we can
think of are the defaults we preload; the tracker tells us which ones
we failed to think of.

**Honest limit to state with it:** the default's vocabulary is smaller
than 5.2 promises. `bounce` needs the restitution F4 measured as
declared-with-zero-readers, `roll` needs angular state at the seam, and
`topple` is unimplemented. The physics default can only say what the
engine can measure today.

## 2026-08-15 — TWO CORRECTIONS, and the composition rule inverts

**CORRECTION 1, mine.** I wrote "today the engine counts extra
conditions, so the rule with a property filter wins." That is false.
There is no specificity mechanism anywhere in the interaction system:
`specificity`, `precedence` and `priority` return ZERO hits across
`src/interaction/` and `include/logosphere/interaction/`. The key I
described was authored in the router design study and never ruled. I
presented a proposal as shipped behaviour. The owner caught it.

**CORRECTION 2, and it is the more important one.** Exclusivity was
never the status quo either. At `particle_interaction_system.cpp:
364-369` **[E]** every matching rule fires:

```cpp
for (const auto& [rid, r] : rules_) {
    if (r.trigger != Trigger::ON_CONTACT) continue;
    if (!conditions.evaluate(r.condition, view)) continue;
    ContactEffectContext ectx{view, *this, bus};
    effects.apply(r.effect_expr, ectx);
}
```

Both apply. Always. The single-winner election is something the router
design INTRODUCED with `claim: CLAIM`; it is not what the engine does.
The owner's instinct ("both need to be applied when there is a way to
do so") is closer to the existing code than the design was.

**So the composition rule inverts.** Not "one wins and silences the
other", which was the wrong question I answered twice. The rule:

- **Both rules apply, by default.** Preserved from the code.
- **Exclusivity is COMPUTED, never declared.** Two effects cannot both
  land only when the effect's own declared CARDINALITY forbids a second
  application to the same target in the same frame. That is already
  written down in the algebra 5.4 and needs no new authoring concept:
  NARRATIVE (`emit_event`) is unbounded, `damage.accumulate` is
  additive, `state.set` is idempotent per property, `knockback` is once
  per (occurrence, side) drawing on one budget, AUTHORITY is once per
  (body, frame), STRUCTURE is once per (bond or particle, frame). Only
  the last two are genuinely exclusive, plus `state.set` when two rules
  write DIFFERENT values to the SAME property.
- **The outcome NAME stays exclusive**, one per (occurrence, side), per
  GEDANKEN-9. Specificity picks the name. It does not silence effects.
- **Therefore `claim: CLAIM` / `claim: OBSERVE` stops being a flag.** A
  route that declares an `outcome:` is naming; a route carrying only
  `do:` is adding consequences. Derived from what the route writes,
  not guessed in advance by an author who cannot see the other routes.

Specificity is demoted from an ELECTION to an ORDER. It decides the
sequence in which effects apply and which name is published. It does
not decide who is allowed to run.

**A live defect this exposes, in shipped code, today.** That loop
iterates an `unordered_map` (`particle_interaction_system.h:335`), so
the order in which matching rules apply is hash order. For any effect
whose cardinality makes it last-write-wins (`state.set`,
`profile.swap`), **hash order decides the outcome**. That is an INV-27
violation in the tree right now, not a design risk. It is the
prerequisite the composition inventory flagged as blocking, and it is
cheap: sort at load, iterate a vector.

**Precision on the line above (appended same day, before the fix
landed).** I called the hash-order defect "an INV-27 violation in the
tree right now", which reads as run-to-run nondeterminism. It is not
that. For one binary and one set of EntityIDs the order is stable, so
two runs of the same build agree. The defect is that the order is
UNPORTABLE and UNPREDICTABLE: it changes when a rule is added and the
map rehashes, when ids shift, or when the toolchain does, with nothing
in the authored content different. Measured before the fix, it was also
inverted, the rule authored second applying first. Fixed in 80a9136 by
sorting at load; the precise statement is in the test header.

## 2026-08-16 — Transcript audit: what the owner said that was never written down

An audit of the full session transcript (1,083 owner messages, March to
August) checked every directive against this repo. The August physics
work is captured well: 31 invariants, this ledger, the board, the skill.
The gaps fall in two places.

**ONE LIVE CONTRADICTION, and it is the reason for this entry.**
`PHYSICS_BOARD.md` recorded `is_quat_driven` as "renamed not folded".
The owner had instructed the opposite:

> "`is_quat_driven + owner` is completely wrong, and I think it was
> added without my knowledge/agreement, and needs to be folded into
> solver_mode, since it's the same logic/essence, would you agree?"

No ruling was ever recorded and the board carried the opposite
disposition. What actually happened is worth stating exactly, because
the failure is not that the study reached a different answer. The study
found something real: the flag does two jobs. Alone it is
REPRESENTATION, naming which orientation field is the truth, and it is
used correctly at six sites. Paired with `owner` it is AUTHORITY, and it
is used wrongly at seven. The authority half folds precisely as the
owner said. The representation half has nowhere to fold, because
`solver_mode` carries no notion of which quaternion is truth.

**The failure is that this distinction was never put back to him.** A
finding that contradicts an instruction is a question owed to the
owner, not a licence to change the disposition quietly. Boarded as R7.

**Standing process rules that lived only in conversation**, now in
`CLAUDE.md`: always merge and never rebase (said twice, four months
apart); no assistant attribution anywhere in this repo; no em dashes or
assistant writing tells; one physics engine with no scene ever tuned;
unused code is dangerous code and gets deleted; no submodules; wind is
modelled and never faked. Maintainer voice for contributors, corrected
three times on drafted replies, is captured too.

**In the skill**: the Gedankenexperiment registry was absent from the
read-first list entirely, and the owner's directive to spawn
experiments before options is now recorded there. So is the prohibition
on piping a run the owner is meant to see through `head`, `tail` or
`grep`, which broke five runs before it was written down.

**Seven instructions had to be repeated**, which the audit calls the
strongest signal in it. Visual QA was asked for at least eight times in
one week, four of them the identical sentence. "Never revert without
consent" was sent twice two minutes apart. "No if-statement edge fixes"
appears at least nine times across five months. Headless-only took four
escalating messages inside one hour. Each repetition is a place where
the first telling produced agreement and no change.

**A provenance warning, recorded rather than acted on.** Two rules this
session has been treating as standing, "never `git add -A`" and "never
touch `src/platform/platform_macos.mm`", appear in the transcript ONLY
inside an assistant-written compaction summary and in no owner message.
They may come from another session or may be invented. They are being
honoured as conservative defaults, and they are NOT recorded as owner
rules until he confirms them.

## 2026-08-16 — RULED: the quaternion is the only orientation truth

The owner, on reading `is_quat_driven`'s own declaration:

> "this is totally wrong, I do not remember coding this or allowing this
> to happen, must've been one of our rotation experiments making it into
> the main, but this does not feel right at all, rotations and general
> solving should not be either one or the other, we need a general way
> to solve these."

**RULED: make the quaternion the only orientation truth, derive Euler
from it always, and the flag has nothing left to be.**

This is Option A of `ROTATION_CAMPAIGN_DESIGN.md` §6.1, a ruling
DEFERRED TWICE: first "until the rotation-ladder work forces the
representation question", then again to slice S6. It has now been forced
from both ends, by S6's inertia tensor needing exactly one R per body,
and by the owner looking at the flag.

**Provenance, checked rather than guessed.** `6d09bf4`, 2026-04-18, the
owner's own commit, titled *"rotational-DOF Stage 3 — quat-driven
humanoid joints + Euler bridge"*. Its message describes the flag as
flipping "who owns the particle's orientation truth", with `false =
legacy Euler-owned`. It was a BRIDGE for a staged migration, with
"legacy" written into its own description, and it was never crossed.
The owner did author it; what he did not authorise was it becoming
permanent. Footprint today: 305 references to `rotation_x/y/z` in
`src/` against 37 to `rotation_q`.

**This dissolves R7 rather than answering it.** The board's question was
what to do with the flag: fold the authority half and rename the rest,
or fold both. Under this ruling there is no representation half to
rehome, because there is no longer a choice to record. The seven
`is_quat_driven && owner` authority reads fold into `solver_mode`
exactly as the owner originally instructed on 2026-08-15, and the six
representation reads become unconditional. His first instruction was
right without qualification.

**SEQUENCING, and this is the part the study did not state.** The
unification is a PREREQUISITE of the rotation campaign, not a slice of
it. D2's slices write new angular code; writing that against a
dual-truth representation means writing branch-on-flag code that then
has to be unwound. Unify first, build on one truth.

**A live disagreement, verified in the tree today.** For a quat-driven
body between the solver's quaternion integration and the Euler publish,
the narrow phase orients its collision box from `rotation_q`
(`narrow_phase.cpp:677-678`) while `GetInertiaAboutAxis` builds its body
frame from the Euler triple UNCONDITIONALLY (`particle_core.h:302-309`).
Same body, same instant, two orientations. That is INV-20's shape: a row
priced for a different world than the one it acts on. Recorded as
GEDANKEN-23.

**Six Gedankenexperimente recorded FIRST, per the 2026-08-16 directive,
before any code:** GEDANKEN-19 the body that never turned (the
bit-identical baseline), 20 the compass round trip (does +pi/2 still
face east), 21 the pitch at ninety (the publish is provably lossy at the
gimbal boundary; the question is whether anything writes back from it),
22 three writers one orientation (the flag, the yaw cascade's
`drive_set`, and `solver_mode` are three overlapping answers, and the
ruling removes only the first), 23 the inertia and the box disagree, 24
the sphere that does not care (the cost half: any skip must derive from
physics or shape, never from a per-body flag, or the flag returns
wearing a new name).

**Owed before code:** the remaining five D2 questions, to be brought one
at a time with the education first, starting with §6.2 `ANGULAR_DRAG`.

## 2026-08-16 — ANGULAR_DRAG is the damper we missed, and the medium hole under it

Owner, on being shown D2 §6.2: *"I thought we had extracted all these
constants and we were aware of these, this is masqueraded dampening in
rotation, we need to get rid of this disease and introduce proper
dissipation of rotation indeed due to air resistance, but, heh, when
there's air present."*

**He is right that we extracted it, and the extraction wrote down the
objection.** `ANGULAR_DRAG`'s own comment
(`src/generated/physics_constants.h:296-301`):

> "Same INV-19 exposure as DAMPING_FACTOR: absolute-motion damping whose
> dissipation story is thin. **Extracted as-is by decree; any retuning is
> ledger follow-up.**"

INV-29 was satisfied. The constant is named, unit-tagged, homed and
grouped. **INV-19 was never asked**, the comment said so, and nobody
came back for the follow-up. Extraction makes a constant legible; it
does not make it legitimate.

**We killed the linear twin and left the angular one, and the file said
they were twins.** `DAMPING_FACTOR` now has zero readers, eradicated
with the rest damper. `ANGULAR_DRAG = 0.95` is still applied at three
sites per substep (`physics_system_v4.cpp:5064`, `:5086-5087`), four
substeps a frame. A body spinning in vacuum retains 0.95^240 of its
spin after one second, about 4.5 parts per million. GEDANKEN-25.

**A fourth angular damper nobody has ever seen.**
`src/animation/humanoid_locomotion.cpp:5277` declares a LOCAL
`const float ANGULAR_DRAG = 0.98f;`, shadowing the extracted name with a
different value, never extracted, a bare literal. That is a live INV-29
violation the extraction campaign missed because it was hunting the
name it already knew.

**THE HOLE UNDERNEATH, and it is bigger than the constant.** The owner:
*"we never thought of capturing air or void or water or any other
medium... big todo here, maybe even before rotation, full ontology
rabbit hole."* Half right, and the half that is wrong matters.

Media DO exist and the model is sound: a profile declares
`drag_coefficient`, `buoyancy_factor` and field forces; an overlap is
detected; drag is taken against RELATIVE velocity (`v - v_medium`),
which is correct rather than a hack. GEDANKEN-2 already covers a sphere
falling into mud.

Two things it cannot do:

1. **A medium is something you ENTER, never something you are already
   in.** Zero hits repo-wide for an ambient, default or world medium,
   or for air. So the unstated default of every scene is VACUUM, and an
   open-air world is secretly in space.
2. **The medium path is LINEAR ONLY.** `apply_volume_forces` writes
   `vx`, `vy` and `vz` across drag, buoyancy and field, and never
   touches `omega` or `torque`. A paddle spinning in water slows at
   exactly the rate it would in air, because the only angular
   dissipation in the tree is a constant that knows nothing about
   either. GEDANKEN-27.

**`ANGULAR_DRAG` fills both holes at once**, fusing the ambient case the
system cannot state with the angular case it does not implement, into
one number that ignores the body, the fluid and the relative velocity
alike. Deleting it without filling the holes leaves free bodies
spinning forever; filling them gives it a real mechanism to be replaced
by. That is the argument for doing this BEFORE the rotation campaign
rather than inside it.

**And it is the same shape as D3, the substrate direction already
boarded.** Gravity is the effect of mass we do not simulate; ambient
drag is the effect of air we do not simulate. One mechanism family, two
instances. The ambient medium should be designed as part of D3, not
beside it.

Boarded as **D7**, ontology work under Malleus discipline, with three
Gedankenexperimente recorded first (25, 26, 27).

## 2026-08-16 — CORRECTION: the default is not vacuum, it is air at sea level

I asserted, in this ledger, on the board, in GEDANKEN-26 and in the
spike brief, that no ambient medium exists and that "the unstated
default of every scene is vacuum". **False.**

`src/core/physics_system_v4.cpp:4674-4683`, inside `integrate_positions`,
every substep, every moving body:

```cpp
float cross_section = p.width * p.height;
float drag_coeff = 0.5f * RHO_AIR * DRAG_CD * cross_section / p.GetMass();
```

`RHO_AIR = 1.225 kg/m3` (`physics_constants.h:236`), sea-level air.
Extracted, unit-tagged, INV-29 compliant. Booked to the dissipation
ledger.

**How the error was made, because the method matters more than the
fact.** The grep searched for `ambient`, `default_medium`, `world_medium`
and the quoted string `"air"`. The mechanism is spelled `RHO_AIR`. The
global rule this violates is written down and I quote it regularly: *a
filter encodes a hypothesis; running it first means the evidence can
only confirm what you already believe.* I searched for the shape of the
thing I expected to be missing.

**The corrected finding is worse than the one it replaces, and sharper.**
Not "there is no ambient medium" but **two drag laws run in the same
engine and neither knows about the other**:

| | ambient | declared medium |
|---|---|---|
| law | quadratic, `0.5·rho·Cd·A/m` | linear Stokes |
| when | every substep | once per frame |
| booked | dissipation ledger | nowhere |
| declarable | **no** | yes |

A body inside a declared water volume receives BOTH. It is in water and
in air at the same time, silently. And because the ambient law is not
declarable, **no scene can be placed in vacuum, underwater, or on
Mars** — which is why the owner's pressure-chamber-in-space scene
cannot be built at all.

The ambient law is also motion-blind: `cross_section` is `width*height`
regardless of travel direction, so a plank presents the same area
edge-on as face-on.

**What does NOT change:** the angular finding. Both drag laws are
linear-velocity-only and neither touches `omega`, so `ANGULAR_DRAG` is
still the only thing damping a spin and is still a constant standing in
for a mechanism. `test_angular_dissipation` stays red for exactly the
reason it says.

Corrected in place: GEDANKEN-26 (rewritten around the real mechanism,
with the failure recorded in its own notes), the D7 board row,
`MEDIUM_SPIKE.md`, and the test's own header.

## 2026-08-19 — F2 is a PENETRATION bug, not a friction one, and INV-2 is live

The owner, watching the ramp scene: *"I think sphere just falls behind
the ramp, not even going down."* Right, and sharper than the reading I
had published twice.

I had described F2 as "the sphere meets a flat normal so nothing drives
it downhill". Measured, it is worse and simpler:

```
sphere rests with its bottom at z =        2.924
  the ramp's REAL tilted face at that x:   4.831
  the ramp's UNROTATED box top:            2.924
```

Exact to three decimals. The sphere does not rest on the ramp at all. It
falls **1.907 m THROUGH** the tilted face, past the point where it should
have landed, and comes to rest on the horizontal top of the ramp's
**unrotated** box: the flat shelf `aabb_of_box_particle` invents by
discarding rotation (`src/core/narrow_phase.cpp:957-976`).

**Three consequences the earlier framing missed.**

1. **The zero travel needs no explanation about normals or friction.**
   The shelf really is flat, so there is nothing to slide down. The body
   is behaving correctly on the surface it was handed; the surface is
   fiction.
2. **This is an INV-2 violation, live and permanent.** "No two bodies
   interpenetrate beyond SLOP (1 mm) in steady state, and penetration is
   never accepted as a rest state." This sphere rests 1.9 m inside a box,
   for ever. Not marginal.
3. **It reaches every sphere against every rotated box in the engine**,
   which includes tree log segments (`create_segment` sets `rotation_y =
   pi/2`). Any spherical body meeting a felled log passes through it.

Recorded because the correction matters more than the finding: I gave
the same wrong reading twice, and the owner got it right by watching
the thing move.

## 2026-08-19 — F2 FIXED: spheres meet rotated boxes as the solid they are

Owner: *"make sure that sphere does not fall, I've seen that."*

`narrow_phase_sphere_obb` added (`src/core/narrow_phase.cpp`), and
`narrow_phase_particle_pair` routes both sphere-box branches through it
whenever `box_particle_is_rotated()`. Unrotated boxes keep the
axis-aligned path, bit-identical, on purpose.

The algorithm is the textbook one and deliberately mirrors the
axis-aligned version so the contract cannot drift: project the offset
onto each of the box's own axes, clamp to the half extents, rebuild the
closest point in world, normal from B toward A (INV-25), penetration
`r - d`. The deep case, sphere centre inside the box, exits along the
box's OWN axis of least penetration rather than a world face; getting
that wrong pushes a deeply penetrating sphere sideways through the solid.

**Measured, before and after, same scene** (`test_ramp_race`, 40 deg ramp):

| | before | after |
|---|---|---|
| sphere travel | 0.000 m | **6.251 m** |
| sphere rest, bottom z | 2.924 (inside the ramp) | **0.003 (on the turtle)** |
| the real face at that column | 4.831 | — |

It no longer falls through. It runs the ramp and lands beside the cube,
which travelled 6.356 m.

**The tripwire did its job, which is the part worth recording.**
`test_collision_bounds_rotation` part 5 pinned the WRONG answer on
purpose so that fixing the pair would force whoever did it to correct
the canonical table. It went red the same minute, and part 5 and the
table in `narrow_phase.h` are corrected in this commit. That is the
mechanism working exactly as designed, on its first firing, and it is
the reason two comments could rot here before and this one cannot.

Part 5 now also covers the deep case, which the axis-aligned path could
not express at all.

**Still red, and a different front.** The cube slides 6.356 m and never
turns: peak |omega| exactly 0.0000 over 240 frames including leaving the
ramp edge and landing. That is D2 1.2, contacts carry no lever arm.

## 2026-08-19 — Argus, and the assert-or-waive discipline

Owner, diagnosing the week's loose assertions and prescribing the cure:

> "what can we adjust in the physics skill that prevents these loose
> assertions... and then there's a physics-aware in the logs that tracks
> from high level, particle position, distance to other particles, xyz
> and relative to others, so you 'see' each particle and gets logged,
> when doing these experiments or engine needs it."

And on scope: **"pure engine module to use not only in physics, we
might have to use it for others too, like combat etc."** Named by the
owner: **ARGUS**, the many-eyed watchman.

**The disease, named precisely**: assertions were sampled from a
partial expectation instead of derived from a complete narration. The
ramp test asserted travel and never rotation; the drop test could not
say WHERE the sphere rested. The owner's eye kept doing the job of an
observer that should have existed in code.

**The two-part cure, both landed:**

1. **Discipline (both skills)**: before the asserts, the full-state
   narration — every DOF of every tracked body, per phase — then every
   narrated DOF is asserted or waived by name. An unasserted DOF is a
   visible decision.
2. **Instrument (`src/core/argus.h`)**: declarative watch-list,
   per-frame state records, relative queries (separation, approach
   speed, spin, peaks, q-vs-Euler divergence), narration dump.
   Read-only over particles by construction, zero cost unwatched, core
   profile, engine accessor beside the ParticleTracer. The Tracer
   answers "who wrote this" (causal); Argus answers "what is the state
   and geometry, continuously" (observational).

**Proving ground**: the cube-drop ladder observes through Argus. Every
audited number unchanged (the witness does not perturb), and the new
eyes immediately earned their keep: separation 0.3002 against the
derived 0.3000 resting contact, and peak spin 2.4435 = 3.0 x 0.8145,
one frame of ANGULAR_DRAG, the disease fingerprinted by the witness on
its first frame of duty.

## 2026-08-19 — RULED: the flip. Quaternion truth is the DEFAULT.

Owner: *"add this decision into our ledger for physics, and enable."*

The lever `LOGOSPHERE_QUAT_TRUTH` becomes the default: every DYNAMIC
body's Euler ledger is published from its quaternion after angular
integration, always. KINEMATIC bodies remain their external writer's.
The env var inverts to a kill switch: `LOGOSPHERE_QUAT_TRUTH=0`
restores the old split for A/B and bisection, and
`PhysicsSystem::set_quat_truth(false)` remains for in-process baselines
(O2 keeps proving the no-rotation case bit-identical both ways).

Evidence the ruling stands on, all measured: O0 round trip identity to
0.000002 over 1,700 poses; O2 bit-identical hash for a never-rotating
body; O3 the frozen twin healed, divergence 0.0000, twins visually
identical; per-frame Argus testimony that the flag never moved a body
and never touched spin (exact zeros, 60 frames). Owner QA'd the
lever-on twins interactively (both cubes turning) before ruling. The
full-scene run (Eden / Rube under the lever) was offered and the owner
ruled directly; recorded as his call.

Consequences, in order: O1 goes green and test_orientation_truth is
promoted from the CI red-list to the smoke list with its audit row
(the direction-locked ratchet demanding exactly this bookkeeping);
then `is_quat_driven` dies as a truth-selector — six representation
reads become unconditional, seven authority reads fold into
solver_mode, the R7-dissolution work proper. What the flip does NOT
fix, so nothing is oversold: contacts still carry no lever arm (D2)
and ANGULAR_DRAG still eats spin (D7); the flip makes rotation VISIBLE
wherever it exists, which is lock 3 of 3 removed.

## 2026-08-20 — DECREE: "fixed" is a protocol, not a sentence

Owner, after four contact-torque slices were reported with the word
"fixed" attached to lever-mode measurements no assertion enforced:

> "we do not declare anything as fixed until Argus has verified the
> physic-semantics of the solution as clamped by assertions in the
> tests, plus a QA by a human in interactive mode that understands the
> changes and why."

Recorded as owner decree, into the physics skill in this commit. FIXED
now means, and only means, all three:

1. **Argus-witnessed**: the claimed physics is observed by the witness,
   not read off a print.
2. **Assertion-clamped**: a test enforces the claimed semantics in the
   exact mode (lever state included) the claim is made for, and its
   audit row says so. A mechanism behind a lever needs the lever-mode
   contract asserted, or the claim has no enforcement.
3. **Human-QA'd, informed**: the owner watches it interactively AND has
   been given the education to know what changed and why, before the
   word is used.

The violation that earned this: slices A-D's lever-mode claims ("the
die falls flat", "the sphere rolls", "lane kick cut 3.4x") were
measured by grep over test output, while the tests' assertions ran
their DEFAULT-mode contract. True statements, unenforced — one refactor
away from silently becoming false. The lever-mode contracts are being
clamped now, and no interactive QA of the torque work has happened yet,
so nothing in slices A-D is "fixed" under this decree until both halves
close.

## 2026-08-20 — Owner QA closes the decree on slices A-D: FIXED

Owner, after watching the drop ladder and the ramp race under
CONTACT_TORQUE interactively, with the education delivered first:
"its working!!! nice!!!" — and the confirmation question that matters:
"no hacks, no conditional trees have been used, we're solving physics,
right?"

**ACK, with the evidence enumerated rather than asserted.** What the
four slices added is mechanics, not policy:

- Torque = r x J at the REAL contact point the narrow phase computes
  (box-box manifold points; the turtle's corner patch). No invented
  geometry: the one invented corner that existed for a single commit
  was measured lying (the lane walk) and replaced by the patch.
- Pricing: every row's effective mass carries (r x J)^2/I for each body
  its torque may spin (INV-20, measure = apply).
- Friction measures the contact-point relative velocity (v + omega x r,
  both bodies) and its tangents are derived IN the contact plane from
  the normal. Rolling emerges from slip-braking; it is not scripted.
- Gates: solver_mode == DYNAMIC, one authority question, no owner
  reads (INV-15), no material/size/shape special cases, no per-scene
  tuning; the two constants added (SUPPORT_PATCH_BAND, the lever-mode
  test bounds) are declared in schema/scene with their measurements.

**The honest caveats attached to the word FIXED:**
1. `CONTACT_TORQUE` itself is a conditional — the lever discipline's
   temporary kind, default-off, awaiting its flip ruling; at the flip it
   is deleted, not kept.
2. Two named numeric residuals are NOT covered by the word: the
   per-point omega seed (block-solve territory) and the 45-degree
   parking (D7's damper), both boarded, the lane bound ratcheted at
   0.30 m until they fall.

**New experiments ordered by the owner, G-first:** the drop ladder
gains a fast-spinning touchdown rung and per-axis spin rungs (X, then
Y, then Z, one at a time, observe the surface interaction); floor
materials (ice, rock, ...) later, for fun. The ramp gains: a plate
BODY instead of the turtle as a next case, an ice body, and the
materials play — folded into D9's matrix as its first concrete
instances.

## 2026-08-20 — The sweep gated four-day-old binaries, and caught itself

Process finding, severity high, recorded before anything else moves:
`scripts/physics_sweep.py` runs `build-release/`, which was last built
2026-08-16. Every sweep verdict reported since — the gates over the
quat-truth flip, the four contact-torque slices, and D7's law — ran
STALE binaries against current audits. Those "new-red 0" verdicts were
vacuous for engine behaviour changes; they gated only test-file and
audit edits.

How it surfaced is the design working: D7 flipped
test_angular_dissipation's audit to expect=pass, the stale release
binary still ran the old red, and the contradiction raised a MOLE
within one sweep. An audited baseline plus a live verdict cannot both
be stale and quiet for long.

Consequences: build-release is being rebuilt now and the TRUE sweep
over this week's engine changes runs after it; the sweep script should
refuse to run when the build tree is older than HEAD (boarded); and the
earlier gate claims stand corrected in place rather than erased.

Also booked: INV-29's gate refused the derived /32 in the new drag law
— correctly, against my own commit message's rhetoric. Derived
coefficients get NAMES (ROT_DRAG_FACE_INTEGRAL, schema, with the
integral in its description), like the 12s before it.

## 2026-08-20 — G-43 THE CORNER ATTRACTOR: solved at three laws (owner ruling A)

Owner ruled A: diagnose the toppling channel before items 4-6. The corner
attractor was three stacked confiscations, each fixed at its own law, no
scene tuning, no special cases:

1. **G-44, the sleep law's missing half.** Rest entry priced linear speed
   only, then zeroed omega. A corner topple starts as near-pure rotation
   (extremity 0.026 m/s < 0.1 threshold, exponential lambda ~3.4/s), so
   the 10-frame window froze every budding topple. Law: quietness is ONE
   currency (v^2 + omega^2 r_ext^2) and must be NON-GROWING
   (REST_GROWTH_TOLERANCE, REST_GROWTH_FLOOR, both in schema). A cache
   may only cache a fixed point.
2. **Turtle rows were rotation-blind.** The omega-cross-r anchor term
   lived only in the box-box branch of v_rel, and its gate
   (is_quat_driven) contradicted the apply gate (DYNAMIC, G-39) so plain
   bodies' rotation was invisible to every contact row. Measure-gate now
   equals apply-gate, both branches.
3. **Warm start was a second solver with different physics.** Cached
   support applied LINEAR-ONLY to the first row of each key (hash dedup
   dropped the rest): support without lever arms is support without
   torque, and the turtle key hits every substep. Rebuilt as iteration
   zero through the FULL Jacobian, distributed across the key's rows by
   eff_mass_share; the store sums the group (the old loop stored only the
   first row, undercounting every multi-point contact). The documented
   hash-collision dedup defect retired with the hash dedup itself.

Measured: R7 falls at 3.19 rad/s onto the slab; R8 falls at 3.35 onto
the turtle (z 0.3997 vs face 0.4000); the ramp cube ends FLAT on the
turtle (z 0.20, clean quarter-turn), lane walk 0.9902 -> 0.1382 m,
INSIDE the 0.30 ratchet: the torque-free warm channel was also the lane
driver, so rotation item 5's block-solve suspicion is likely moot.
Lever ladder R0-R8 fully green. Harness 27/27. Honest new default red:
R2 settled-spin passed via sleep confiscation; it now names the absent
default torque law alongside R1's pair.

Remaining on the rotation list: rolling-vs-sliding travel contract
re-clamp (item 2 tail), G-21/G-23 gimbal ruling with measurement (item
4), CONTACT_TORQUE flip ruling + ice (item 6). FIXED protocol: Argus
asserts clamped; owner interactive QA pending.

## 2026-08-20 — Certifying sweep after G-43/G-44: CLEAN

SWEEP_VERDICT: new-red 0, gone-green 0. The four moles resolved: inv29
green (0.25f renamed), refused_momentum green (truth over measured awake
frames, 107% in band), minimal_v2 green (36.5 s, was 221.5 under the
unrefined growth gate), tree_wiggly booked known-open pending owner
ruling (G-44 unmasked a sustained gluon-tree oscillation the old sleep
entry absorbed; RCA tasked). The 39 unaudited are the other session's
Kamaji/rules/KG tests plus visual drivers, unchanged from the previous
sweep. FIXED protocol clause 3 (owner interactive QA) remains open for
the whole G-43 arc.

## 2026-08-20 — G-45: friction acts only through a touching contact (owner QA find)

Owner watched the window and said what the green ladder could not: "4/8
is wrong... 7/8 is not falling." The continuous world, rebuilt headless
with Argus per the owner's order, exposed three stacked defects: bodies
teleported between cases kept their solver history (fixed: arm/park
void history, PhysicsSystem::forget_body); the flight-window instrument
accepted the wrong body's landing (fixed: actor-only); and the real
one, G-45: speculative rows (bias < 0, gap open) were transmitting
Coulomb friction sized by their CAPTURE impulses — 52.1 N*s of phantom
friction killed a 3 rad/s spin in the last 3 cm of fall. Law: bias < 0
rows transmit no friction and warm no equilibrium. Yields: wheels walk
0.19 m (2.4x), ramp lane 0.0036 m (0.99 at arc start, three orders),
R2 spin alive to the floor, lever ladder R0-R8 green in BOTH worlds.
Drops raised to 0.6 m (owner order, honest post-D7); the wheels release
at 0.05 m by DERIVATION (corner-knock band, measured non-monotonic).
Instrument debt paid: phystrace frames tick in headless, friction and
warm-start applies traced, per-phase omega probe. Ramp's one remaining
lever red is G-23 (item 4). FIXED protocol: owner window QA pending on
the corrected build.

## 2026-08-20 — Owner QA closes G-43 + G-45 (FIXED protocol, clause 3)

Owner verdict on the corrected ladder window: "total success and is 99%
according with physics... these are totally great... excellent work,
almost magic." Argus-clamped asserts (clause 1) + certified sweep
(new-red 0) + owner interactive QA (clause 3): both arcs FIXED. Owner
orders added to the board: spin-lift on impact, rotation-rate sweep for
falling cubes, spinning spheres vs floor. Merge of the branch ordered.

## 2026-08-21 — G-21 ruled and closed: two-band coherence (adaptive thresholds)

Measurement first: a 2M-sample round-trip sweep of
from_euler(to_euler_zyx(q)) gives mean error 0.0002 rad away from the
gimbal fold and worst 0.014 rad inside +-0.04 rad of |pitch| = pi/2 — a
float32 representational ceiling, not a physics or branch bug. Every
live spike sat inside it (ramp 0.0137/0.0130, ladder R6 0.0110) and the
vertical-spin control measured exactly zero. Owner ruling: "adaptive
thresholds are good, i.e. I do not care about 1 cm/h on a 200 km/h
movible." Contract: divergence < 0.01 away from the fold, < 0.015
inside Argus::FOLD_BAND (0.05 rad), accumulated per band by Argus at
observe time. Result: the lever ramp race is FULLY GREEN for the first
time; ladder lever reds down to the R5/R6 tall-fall walk pair (re-clamp
still unruled). Rotation item 4 closed.

## 2026-08-21 — Two owner orders from ramp QA

1. **G-46 born red, math first.** "the sphere, based on physics, ramp
   angle, mass, friction (do the math first) should end farther than
   the cube. and that is not happening." Derivation in the registry:
   rolling 5/7 g sin th = 4.50 m/s^2 vs sliding 2.55, and nothing
   brakes a roller on the flat. Assert added lever-mode, born red
   (sphere 4.21 m vs cube 4.81). Enhancement front on the board; the
   owner's word: "Not sure this is our focus now, but needs to be
   enhanced."
2. **Every assert names its registered law.** "any physics
   requirement/law should be registered and maintained, and thus, it
   should be registered and present and explicit in the tests
   themselves." Panel and headless assert texts now carry their
   registry IDs (INV-x / G-x); a claim with no registry entry gets one
   BEFORE its assert lands. Skill updated in the same commit.

## 2026-08-21 — Owner ratifies the migration's three proposals ("add these for sure")

INV-33 finite-state (NaN disables three laws silently), INV-34
rest-is-reached (an untouched scene settles and stays settled; the
tree oscillation and test_physics_minimal's accepted wiggle are the
first known violations), INV-35 one-position-writer (the 2x
displacement RCA). All three aspirational with mechanisms owed, listed
in each record. The eight to-investigate tests go to a dedicated
review agent whose dispositions come back to the owner in chat.

## 2026-08-21 — Owner rules R8/R9/R10 and case 5

**R8 (overlap at birth)**: a generator that overlaps bodies is a BUG,
full stop. Owner: "the generator should NOT put things overlapping...
I'd not fix for penetration, I'd just avoid it fully... the only
penetration I'd allow is in the physics delta in extreme cases where
things are super fast and the tic does not allow to capture it...
enforce nothing that is created, ever, can coexist — particles are
always, 100% guaranteed not to overlap in 3d, ever. Solve that, and do
not care about that in our physics engine, reducing complexity, and
just simply refuse to run — raise exception... or even just raise and
exit(1) catastrophically with full detail." INV-30 stands strengthened:
the mechanism is a CREATION DOOR (doors-not-fallbacks), refusing loudly
at spawn; the physics engine carries no creation-overlap tolerance.
Subagent dispatched: the door, the fallen-tree generator fix (C10), the
two overlapping foliage placements, and test_no_overlap_at_creation
made real. The 2026-08-02 "minimal overlap accepted" position is hereby
recorded as SUPERSEDED (its only prior carrier was a test comment).

**R9**: test_sleep_diagnostics — owner: "delete". Retired fully: it
asserted nothing and prescribed a mechanism eradicated 2026-08-14. Its
story stays in the audit row and G-44.

**R10**: test_tree_wiggly — owner: "delete... and create a new battery
of tests whenever we start to work on controlled movement, like wind."
Owner hypothesis, recorded: the tree wiggle is RELATED TO R8 — bodies
born overlapping are compressed springs, and the G-44 unmasked
oscillation may be birth energy. To test the day the creation door
lands. The G-44 oak-oscillation RCA front stays on the board; its
tripwire moves from the deleted test to the board entry until the wind
battery exists.

**Case 5**: regression adjudication stands; owner: "if we need to
rename, do it" — renamed so the file's name stops calling itself a
diagnostic while carrying a verdict.

## 2026-08-26 — G-48 step 2 executed: the stack pump was two mechanisms, both levered

The owner-ratified plan's step 2 ("examine manifold persistence") ran
as an examination, and the examination found the pump was not the
per-substep rebuild itself but two defects underneath it, each now a
registered law with its code behind a default-off lever:

**G-51, the support spans the face** (`MANIFOLD_SPAN=1`). Witnessed at
substep granularity via the canary instrument extended to print
manifold point positions and warm anchors: the deepest-4 reduction
(narrow_phase.cpp) clusters all four points on the downhill edge of a
micro-tilted interface (tilts ~3e-4 rad, depth spread ~0.3 mm across a
metre of face), the warm start pushes its constant 86.9 N*s through
centroids alternating +-0.25..0.45 m side to side on CONSECUTIVE
substeps, injecting 22-40 N*m*s of alternating torque against a 0.15
noise floor. Delta-omega per injection 0.070 rad/s = the measured
wobble band. Mechanism: spanning reduction (deepest + farthest +
max-area completion, constant-free argmax). Unit instrument
test_manifold_reduction born red at 0.3774 m worst centroid offset,
green at 0.0001 the same day. Default bit-identical.

**G-52, the cache learns what sustains** (`WARM_LEARN=1`). The store
side's V4.6 equilibrium-freeze writes a warm-started row's own warm
share back, so a cached key can never learn: the canary shows the
iterations rebuilding the true static support EVERY substep
(accumulated ~290-305 N*s, analytic need 306.25) and the store
discarding it, keeping the frozen first-touch 86.9 for ever. Under the
lever the cache converges to 0.3% of analytic, the column and pile
stand to sub-mm, spins read 0.0000, the world reaches true rest and
SLEEPS, and the energy ledger goes flat zero. The freeze's own reason
(bias-contaminated totals) predates split impulse, under which the
accumulated impulse is pure. The raw learn rule's one measured cost -
the ramp's tumbling cube caching its corner-strike captures, +5.8%
travel - fell to +2.2% under the refined store (accumulated minus the
approach-cancellation part, recovered from the row's own capture
bound). Protocol honesty per the GEDANKEN-11 rule: the measurement
lever preceded the G-52 record by hours; the record says so.

**Evidence state**: test_stack_stands FULLY GREEN under WARM_LEARN in
every lever combination (span, priced boundary, both); ladder, square
strike, refused ledger, ramp show fail sets identical to default; the
combined harness is green in BOTH worlds. Defaults untouched
(stack measures byte-identical with levers off).

**Owed to the owner**: QA (a windowed stack scene with the live assert
panel, being built next) and the flip ruling for the three levers
(WARM_LEARN / MANIFOLD_SPAN / TURTLE_PRICED) plus INV-32's default,
which G-48 holds until the column stands in the DEFAULT world.

**Sweep addendum, same day**: the sweep (run alone, fresh binaries)
returned new-red 3: test_physics_battery, test_rotated_box_contact,
test_inv6_gravity_blindness — exactly the three flip moles the board
had classified as "ONE front" (G-48) when the INV-32 gates flipped,
whose audit rows were never moved. That was a miss in the flip slice:
the board and the registry carried the classification, the audit did
not, so the sweep verdict could not read clean. All three rows now
carry expect: fail with G-48-citing known_open, direction-locked on
the flip ruling. The 41 unaudited moles are the boarded rules-lane
debt, unchanged.

## 2026-08-26 (later) — the contrast controls: torsion and mixed masses (owner order)

Owner: a case "where some of the cubes are expected to have rotation
and check for torsion on other cubes, and another more complex one
where cubes of different sizes/masses are combined to produce
different torsions and effects... ok to be red if its informative."

Registered G-53 (torsion walks the column) and G-54 (mixed masses,
mixed verdicts) BEFORE the cases; the stack instrument grew to four
cases and the window to three SPACE-advanced views, each re-armed via
the teleport law. First measurements, WARM_LEARN world:

- **G-54 fully green, and the verdicts are real**: the centred 8:1
  pair stands, the overhung cube DEPARTS its perch and lands flat on
  the turtle (z 0.7002, bounded at 2.6 m/s), stone-on-soft-wood stands
  to 3 mm. No phantom support.
- **G-53 mostly green**: the spinner brakes 3.0 -> 0.09 rad/s, L_z is
  never created (peak exactly = initial 937.5), the turtle drains it
  to 9.2, and torsion transmits BOTH ways with the asymmetry the
  anchoring derivation predicts (above 0.46, below 0.073; the original
  symmetric 0.10 threshold was an underived guess, corrected in the
  record with the derivation - the below box sits under the full
  column's turtle anchor).
- **ONE new front, booked red**: the SPINNING-INTERFACE GRIND. During
  the spin episode the box0-box1 interface loses 0.232 m of normal
  separation and the interpenetration STANDS at run end; everything
  above rides it down. Spinning under load destroys normal support -
  invisible to the null case, exactly the class of finding the owner
  ordered these controls to catch. RCA owed.
- Default world contrast: torsion sink compounds to half a metre;
  mixed masses amplify the frozen-cache deficit (0.41 m and 0.94 m of
  standing crush) while the overhung cube still departs.

## 2026-08-27 — RULED: KISS. The torsion experiment rebuilds from the irreducible case

Owner: "let's make all this simpler in a 2 cube first, asserts in
place, and then three cubes if needed, and more single-purposed tests,
instead of that complex one please, KISS."

This is the Gedanken method's own rule 1 applied to me: I jumped to
the four-cube sandwich (the interesting case) instead of the
irreducible one. Executed as:

- **test_stack_stands returns to the statics pair only** (G-48's
  single purpose: no torque exists, none may be invented).
- **test_torsion_transmission, new, single-purpose (G-53
  restructured as a rung ladder)**: R1 one cube spinning on the
  turtle (brake, no creation, stands); R2 spinner UNDER a free
  passenger (no anchor above - being dragged is GUARANTEED,
  assertable from below); R3 spinner ON a carrier (the turtle anchor
  wins - the guaranteed physics is an UPPER bound on the carrier's
  motion). The four-cube sandwich retires until a rung needs it.
- **test_mixed_mass_stands, new, single-purpose (G-54)**: the three
  verdicts, stand / FALL / stand.
- **The pending threshold question DISSOLVES**: the disputed
  "transmits below >= 0.05" assert demanded something sub-unity
  physics never guarantees (static friction may hold the carrier at
  exactly zero). The rung form asserts only what each configuration
  guarantees. The 0.10-vs-0.05 split and its post-hoc derivation are
  retired with the sandwich case; no ruling needed.

## 2026-08-27 (later) — RULED: the 50 ms episode is the root; make it realistic

Owner, on the R2 Argus verdict ("the whole experiment lasts 50
milliseconds, three frames"): "correct, this is the root, change this,
make it realistic." The braking rate is the defect: a real stone cube
spun at 3 rad/s dies in ~0.33 s (face-integral Coulomb friction, mean
radius 0.3826 L, alpha ~ 9 rad/s^2), the engine kills it in ~0.05 s.
Registered as G-55 (the grindstone law) with the derivation and the
three compounding causes measured by the RCA: corner anchors (1.85x),
the box friction cone (up to 1.41x), per-row normal inflation under
spin. Mechanism lands behind FRICTION_TWIST=1, default off: face
contacts get a dedicated twist-friction row limited by mu*N*<r>, their
tangent rows go linear-only, and the tangent pair is clamped as a
vector (circular cone). Born-red assert: R1's stop time inside
[0.2, 0.6] s. The engine's box I_z (legacy cylinder 0.125 m L^2 vs the
true m L^2/6) is booked as its OWN front - correcting it moves every
rotation number in the battery and deserves a deliberate campaign
slice, not a rider.

**G-55 landed, same day.** The grindstone law's mechanism went in
behind FRICTION_TWIST=1: face contacts (>= 3-point patches only) get a
twist-friction row limited by mu * N_total * <r> with N summed exactly
over the pair's rows, their tangents go linear-only, and the tangent
pair closes the friction cone as a vector. R1 now brakes in a
perfectly linear Coulomb decay to a 0.217 s stop (band [0.2, 0.6]);
BOTH of G-53's booked reds fell out of the same mechanism (passenger
0.6666, residual dead). Two laws re-learned and guarded on the way,
each caught by the audited battery within minutes: G-45 binds the
twist row (no torsion across an open gap - airborne spin died until
gated), and a face treatment needs a face patch (a sphere's one-point
"face" lost its rolling torque until the gate demanded >= 3 points).
Default byte-identical; harness green under the lever; one booked
marginal in the visual's combined world (R3 tail just above noise).

## 2026-08-28 — OWNER QA VERDICT: the grindstone window is GOOD

Owner ran the torsion visual at 5dd8168 under FRICTION_TWIST=1
WARM_LEARN=1, two windows (/tmp/torsion_qa2.log, /tmp/torsion_qa3.log),
both closed with 0 red on the panel. Verdict: "is good." G-55's QA
loop is CLOSED: R1's quarter-second Coulomb spin-down, R2's watchable
passenger ride, R3's anchored carrier all owner-witnessed. What
remains on this front is decisions, not evidence: the flip ruling on
the four levers (FRICTION_TWIST / WARM_LEARN / MANIFOLD_SPAN /
TURTLE_PRICED — the INV-32 default component stays held by G-47 per
item 6), the grind RCA at the 73.5 kN interface (3-cube bracket), and
the R3 combined-world marginal.
