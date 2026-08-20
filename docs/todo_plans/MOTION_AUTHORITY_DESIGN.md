# Motion authority: one answer to "who may move this body right now"

Read-only design study. Branch `feat/rube-goldberg-machine`, HEAD `e04cd43`.
No source was modified. Every claim cites `file:line`; inference is
labelled INFERRED and measurement is labelled MEASURED.
`src/platform/platform_macos.mm` was neither read nor touched.

This study follows `docs/todo_plans/KINEMATIC_AUDIT.md` (the census that
started it) and the five rulings of 2026-08-14 in
`tests/invariants/LEDGER.md`. It designs one law and one mechanism; it
does NOT design the unmodelled-mass substrate, which the owner named as
the honest home for scenery immobility and which is a separate study.

---

## 0. Method, and four corrections this study makes to its own brief

Read in order: the physics skill, all 31 invariants, the ledger tail from
2026-08-14, the KINEMATIC audit, the pipeline sequence, the two humanoid
ladders, then the solver itself. Built `test_humanoid_knockback` and
`test_humanoid_ragdoll` in `build-release` and ran them headless. Used
`CANARY_PID` / `CANARY_FRAME_MAX` (raw `getenv` in the solver, so they
work in standalone tests, unlike `LOGOSPHERE_PHYS_TRACE`, which
self-initializes only under `Engine` at `src/core/engine.cpp:1094`).

Four things the brief and the audit state that the measurements
contradict. Bad news first.

### Correction 1 (MEASURED). `test_humanoid_knockback` measures nothing today. Its harness never builds the BVH.

`PhysicsSystem::solve_contacts_v3` takes the broad phase from
`particle_system_->get_shadow_bvh()` (`physics_system_v4.cpp:808`) and
queries it under `if (bvh && bvh->is_ready())` (`:1004`). `is_built`
starts false (`include/logosphere/physics/bvh.h:115`) and is set only by
`ParticleSystem::update_bvh()` (`src/core/particle_system.cpp:320-333`),
which nothing in `PhysicsSystem::update` calls. `tests/test_humanoid_knockback.cpp`
never calls it either.

So in that process the candidate list is always empty, no box-box pair is
ever formed, and **no contact between the boulder and the humanoid can
exist regardless of any authority mechanism**. Turtle rows are built in a
separate BVH-free loop (`:821-930`), which is why the scene is not
completely inert.

MEASURED, `CANARY_PID=17 CANARY_FRAME_MAX=90`: the boulder crosses the
chest's full span with its velocity untouched.

```
[CANARY F29 START] P17 pos=(-0.0670531,0,1.37094) vel=(7.99312,0,-1.18366)
[CANARY F31 START] P17 pos=(-0.000444803,0,1.36091) vel=(7.99262,0,-1.26525)
[CANARY F32 START] P17 pos=(0.0328578,0,1.35564) vel=(7.99238,0,-1.30604)
```

The chest is at `(0, 0, 1.35)`, half-extent 0.125, so `x ∈ [-0.125,
+0.125]`; the boulder half-extent is 0.2. They interpenetrate fully at
substeps 29 to 32. `vx` falls by 0.00074 m/s across the crossing, which is
the quadratic air drag in `integrate_positions`, not a contact. Zero
`[CANARY ... SOLVE]` lines for either body in the window.

Consequence: R1's RED is a harness gap. The test cannot prove or refute
anything about authority until it builds the BVH. That is rung 0 of the
ladder below.

### Correction 2 (MEASURED). The test's own HIT/MISSED readout is confounded.

`test_humanoid_knockback.cpp:194-196` prints `boulder vx 8.00 -> 0.61 (it
HIT the humanoid)`. It did not. Over 90 frames the boulder falls 11 m,
reaches the turtle, and is decelerated by turtle friction and drag well
after the humanoid is behind it. A minimum-over-all-frames statistic
cannot distinguish a strike from a landing.

### Correction 3 (MEASURED + code). The seven owner-reads do not block linear contact response. The inertness has a four-link chain, and three different mechanisms are in it.

`inv_mass_momentum` (`:501-506`) returns zero only for KINEMATIC, massless,
and (resolver off) sleeping. A chest that is `solver_mode = DYNAMIC`,
`mass = 15.625 kg`, `is_quat_driven = 1`, `owner = DYNAMICS` is **not**
refused by that predicate at spawn. The chain that makes it inert is:

1. `:608` skips gravity for it, because it is quat-driven and not
   PHYSICS-owned. MEASURED: the `[CANARY GRAVITY]` line at `:591-594`
   prints "receives gravity" and then the very next statement skips it,
   which is itself a diagnostic that lies.
2. With no gravity and no drive running (R1 calls only `physics.update`),
   its velocity stays exactly zero.
3. `update_rest_state` (`:4729`) counts ten consecutive quiet frames
   (`REST_FRAMES_REQUIRED = 10`, `src/generated/physics_constants.h:304`)
   and sets `is_at_rest = true`.
4. `inv_mass_momentum` now returns 0 for it, because sleep is priced as
   immovability with the resolver off (`:503`).

MEASURED, `KB_PROBE=1`: `chest ... rest=0` at f6-f8 and `rest=1` from f9
onward. A 15.6 kg body becomes infinite mass to momentum in nine frames,
because a flag describing how its orientation is stored suppressed its
weight.

That chain is the whole thesis of this document: mechanism C (the
representation flag) manufactures the condition that mechanism B (the
sleep cache) converts into mechanism A's answer (immovable).

### Correction 4 (MEASURED). `test_humanoid_impact` is vacuous, but not for the recorded reason.

`tests/test_humanoid_impact.cpp:399` and `:638`: `bool pass =
impact_detected;`. The displacement figures at `:381-382` are computed and
printed with tick marks; they are **not** asserted. The test cannot fail
on knockback magnitude.

And its humanoid is not the pinned one. It is built by
`HumanoidGenerator::generate_humanoid_physics` (`:738`), never registered
with `HumanoidLocomotion`, so nothing ever stamps it KINEMATIC or
quat-driven. MEASURED headless: `Max hips displacement: 0.13m`, `Max chest
displacement: 0.14m`. The hips move.

So the audit's claim that the hips term is "structurally zero" is false
for this test. It is testing a different body under a different authority
regime than the one the ragdoll and knockback ladders test. It still needs
rewriting; the reason changes from "it asserts a zero" to "it asserts only
that a collision event fired, on a rig that no driver owns".

---

## 1. THE CENSUS

Every mechanism in the tree that answers some form of "can this body
move". Column **Class** is the defect classification the owner named:

- **AUTH** — a genuine authority question, correctly asked of authority.
- **REPR→AUTH** — an authority question answered by reading a
  *representation* choice. Category error.
- **CACHE→AUTH** — answered by reading a *performance cache*.
- **GAME→AUTH** — answered by reading a *game category*.
- **REPR** — a representation question, correctly asked. Legitimate.
- **PAIR** — a declared pair policy, not a body authority. Legitimate,
  listed because it also stops bodies moving each other.

### 1.1 Mechanism A — `Particle::solver_mode`

Declared `src/particle_types.h:78-92`. Post-2026-08-14: `DYNAMIC` and
`KINEMATIC` only; `STATIC` eradicated in `b42ebbe`. The header now states
the transient-authority doctrine verbatim.

| # | Site | Gates | Class | Note |
|---|---|---|---|---|
| A1 | `physics_system_v4.cpp:502` (`inv_mass_momentum`) | momentum, and via `:585` gravity | AUTH | the one door (INV-7) |
| A2 | `:3904` (`inv_mass_positional`) | geometric repair | AUTH | deliberate second predicate (INV-21) |
| A3 | `:4470` (`integrate_positions`) | position integration | AUTH | |
| A4 | `:4867` (`integrate_angular_velocities`) | orientation integration | AUTH | KINEMATIC only; sleep NOT gated, see D-6 |
| A5 | `:4067`, `:4097` (position pass spend) | pseudo-velocity spend | AUTH | |
| A6 | `:1698` (gluon build, both-KINEMATIC skip) | row existence | AUTH | INV-23 |
| A7 | `:1880-1886`, `:2332-2337` (wake-on-strain) | wake eligibility | AUTH | correct: a pinned body has no sleep state to clear |
| A8 | `:1153` (`should_wake`, KINEMATIC branch) | wake of a sleeper by a driven pusher | AUTH | closing-speed form |
| A9 | `:2086-2087`, `:2377-2379`, `:3146`, `:3153`, `:3944-3946` | angular effective inertia, five sites | AUTH | KINEMATIC-only formula |
| A10 | `:5966-5967` (`project_gluon_positions`) | gluon position repair | CACHE→AUTH | contradicts A2 on sleep. Audit E-3, still open |
| A11 | `:3474`, `:3504` (friction `v_rel`) | what the row measures | CACHE→AUTH | audit E-2, still open |
| A12 | `:3495`, `:3517` (friction apply) | what the row spends | CACHE→AUTH + AUTH | audit E-2, still open |
| A13 | `:1336-1337`, `:1559-1560` (pre-resolver arm) | contact row pricing | CACHE→AUTH | dies at the WAKE_RESOLVER flip |
| A14 | `update_rest_state` `:4729-4770` | **has no authority guard at all** | omission | audit E-1, still open |

Write-side, `src/` only, after `f5d05af`:

| | SET sites | RELEASE sites |
|---|---|---|
| `src/worldgen/` | 6 | 0 |
| `src/animation/humanoid_locomotion.cpp` | 5 | 5 |
| `src/animation/butterfly_flight.cpp` | 1 | 0 |
| **total `src/`** | **12** | **5** |

The fifth release is new: `humanoid_locomotion.cpp:1873`, landed by
`f5d05af`, which walks `all_particle_indices` and returns every particle
of an unregistered humanoid to DYNAMIC. MEASURED (`test_humanoid_ragdoll`,
green): `still KINEMATIC after unregister: 0 of 17`, `hips fell 1.231 m in
0.5 s` against an analytic 1.226. That is the release contract working,
once, in one file.

### 1.2 Mechanism B — `Particle::is_at_rest`

54 reads in `physics_system_v4.cpp`. The sleep cache. INV-18 makes it a
cache with a coherence contract; INV-31 (aspirational, `WAKE_RESOLVER=1`)
makes it stop claiming immovability.

| # | Site | Gates | Class |
|---|---|---|---|
| B1 | `:503` in `inv_mass_momentum` | momentum | CACHE→AUTH by design today; INV-31 removes it |
| B2 | `:590` gravity skip | force | AUTH, and exact: a resting body's weight is carried by its support, so skipping both sides of the balanced pair is not an immovability claim (comment `:586-589`) |
| B3 | `:968` broad-phase query skip | contact detection | AUTH-adjacent, performance |
| B4 | `:828` turtle detect skip | turtle rows | AUTH-adjacent |
| B5 | `:1212-1234` wake calls | wake | AUTH |
| B6 | `:2255-2264` motor-torque apply | angular | CACHE→AUTH, and the ONLY angular site that reads sleep, see D-2 |
| B7 | `:5966-5967` | gluon repair | CACHE→AUTH (also A10) |
| B8 | `:3474`, `:3504`, `:3495`, `:3517` | friction | CACHE→AUTH (also A11/A12) |
| B9 | `resolve_sleep_wakes` `:539-557` | wake, from the solved result | AUTH, INV-31's mechanism |
| B10 | `:4759-4760` enter-rest zeroing | zeroes `vx/vy/vz` only | **omission**, see D-6 |

### 1.3 Mechanism C — `is_quat_driven` (+ `owner`), the unnamed third pin

`is_quat_driven` is declared at `src/particle_core.h:77-86` as a
**representation** choice, in those words:

> "When true, `rotation_q` is the truth for this particle's orientation;
> the solver integrates it from omega and derives Euler `rotation_x/y/z`
> from it after integration. ... When false (default), Euler remains the
> truth."

It says nothing about who may move the body. Verified: the declaration is
purely about which field carries orientation.

**The seven reads, each classified.** All seven are in
`physics_system_v4.cpp` and all seven are the complete set of
`ParticleOwner` reads in the physics translation units (grep over
`src/core/physics_*.{h,cpp}`).

| # | Line | Form | What it gates | Class |
|---|---|---|---|---|
| C1 | `:608` | `is_quat_driven && owner != PHYSICS` | **gravity** | **REPR→AUTH + GAME→AUTH.** A representation flag and a game category decide momentum eligibility. This is the category error. |
| C2 | `:2005` | `is_quat_driven && owner == PHYSICS && != KINEMATIC` | gluon row `K` gains `(r×J)²/I` | AUTH (rotational), asked of the wrong fields |
| C3 | `:2015` | same, body b | same | AUTH (rotational), wrong fields |
| C4 | `:3224` | `is_quat_driven && owner == PHYSICS` | `v_rel` gains `ω×r` at the anchor | AUTH (rotational), wrong fields |
| C5 | `:3229` | same, body b | same | AUTH (rotational), wrong fields |
| C6 | `:3393` | `is_quat_driven && owner == PHYSICS && inv_ma > 0` | anchor torque **apply** | AUTH (rotational), wrong fields |
| C7 | `:3403` | same, body b | same | AUTH (rotational), wrong fields |

The classification the coordinator asked for, stated plainly:

- **C1 is a different animal from C2-C7.** C1 gates a *linear force* on a
  *representation* flag. Nothing about "rotation_q is the truth" implies
  "this body has no weight".
- **C2-C7 are all one question asked six times**: *does physics own this
  body's orientation?* They are price (C2/C3), measure (C4/C5) and spend
  (C6/C7) of the same rotational authority, and they are consistent with
  each other, which is why they have not produced a visible bug. They are
  reading the wrong fields to ask a right question. Under INV-20 they must
  stay consistent, and today that consistency is maintained by hand across
  six sites.

**Correct representation reads** (these survive any rename and must NOT
move into an authority enum):

| Site | Use |
|---|---|
| `physics_system_v4.cpp:2283`, `:2286` | `q = is_quat_driven ? rotation_q : Quat::from_euler(...)` — which field is the orientation truth |
| `:4098` | pseudo-angular spend: quaternion exponential vs Euler-Z increment |
| `:4957` | publish Euler from `rotation_q` after integration |
| `src/core/narrow_phase.cpp:661`, `:677` | which orientation builds the OBB |
| `src/animation/humanoid_locomotion.cpp:5520` | parent orientation for the FK cascade |

So: `is_quat_driven` **alone** is representation and is used correctly at
six sites. `is_quat_driven && owner` is authority and is used wrongly at
seven. The discriminator is exact and grep-able.

**Provenance, stated without invention.** `git log -S"is_quat_driven" --
src/particle_core.h` returns only `6e9847d "chore: publish initial
Logosphere source"`. This repo's published history begins with the flag
already present, so nothing here establishes when it was introduced or by
what agreement. What history does show:

- At `6e9847d` the solver already carried `if (p.is_quat_driven) continue;`
  as a blanket gravity exemption, on the representation flag alone.
- `de62450` (2026-08-09 13:33) narrowed it to `owner == ANIMATION`, to fix
  a freed organic segment that "floated on its last velocity forever, no
  bond, no contact, no WEIGHT".
- `6cd624c` (2026-08-09 14:53, eighty minutes later) added `owner ==
  PHYSICS` to all six anchor-spin sites and flipped C1 to `owner !=
  PHYSICS`. Commit message: "human is doing VERY WEIRD STUFF, all its
  particles are rotating. Census: 20 of her 30 particles spinning >1
  rad/s".

Both commits are the same shape: physics and animation were writing the
same body, and the cure was a game-category branch inside the solver
instead of a statement of who owns it. That is the missing mechanism this
document specifies.

### 1.4 Mechanism D — the interaction-profile filter (listed for completeness)

`physics_system_v4.cpp:1106-1113` skips narrow phase entirely when
`InteractionProfile::collides_with` declines the pair
(`include/logosphere/interaction/particle_interaction_system.h:82`). It is
**PAIR** class, not authority: it is a declared material/medium policy,
body-symmetric, and the overlap is recorded in `filtered_overlaps_` rather
than dropped. It belongs in the census because it is the fourth way a body
stops being pushed, and because `butterfly_flight.cpp:100-119` combines it
with a permanent KINEMATIC pin: a registered butterfly is outside physics
in both directions at once.

### 1.5 The disagreements, ranked by blast radius

**D-1 (CRITICAL). Gravity is decided by a representation flag; sleep then
converts that into immovability.**
`:608` + `:4729` + `:503`. Two mechanisms in series, neither of which
knows about the other. Blast radius: every animation-driven body in the
engine. A humanoid's chest is infinite mass to momentum nine frames after
spawn while reporting `solver_mode = DYNAMIC`. MEASURED (Correction 3).
This is the reason a person cannot be hit, and no audit of the KINEMATIC
enum could ever have found it.

**D-2 (HIGH). The angular side has four immovability formulas and no
door.**
The linear side has had one door since 2026-08-12. The angular side has:
KINEMATIC-only (`:2086-2087`, `:2377-2379`, `:3146`, `:3153`,
`:3944-3946`); KINEMATIC-and-sleeping (`:2255-2264`);
quat-driven-and-PHYSICS-owned (C2-C7); and no gate at all in
`integrate_angular_velocities` beyond KINEMATIC (`:4867`). Four answers to
"may physics change this body's rotation". Blast radius: the entire
rotation campaign is built on top of this, and its §2.3 already specifies
the fix (`inv_inertia_momentum(p) -> Mat3`).

**D-3 (HIGH). A sleeping body may spin forever.**
`update_rest_state` zeroes `vx/vy/vz` on entering rest (`:4759-4760`) and
does not touch `omega_x/y/z`. `integrate_angular_velocities` gates on
KINEMATIC only (`:4867`), so it keeps integrating that omega.
`resolve_sleep_wakes` judges `v_sq` only (`:545`) and never wakes for
rotation. Established from code; not observed in a run. A sleeping body
with residual omega rotates indefinitely while every linear mechanism
believes it is at equilibrium. INV-18 violation ("sleep hides nothing").

**D-4 (HIGH). Two positional passes disagree about sleep.**
`inv_mass_positional` prices sleep as movable on purpose (`:3903-3907`,
with the 2.35 m bond-error RCA in its comment). `project_gluon_positions`
prices it immovable (`:5966-5967`). Audit E-3, unchanged at HEAD.

**D-5 (HIGH). Friction re-asks the question the door already answered.**
`:3495` and `:3517` guard the apply with `!pb.is_at_rest && solver_mode !=
KINEMATIC` **on top of** multiplying by `inv_mb`; `:3474` and `:3504`
compute `v_rel` as if a sleeping `pb` were static. `pa` is charged
unconditionally at `:3483-3485` and `:3513-3515`. The instant the resolver
makes `inv_mb` nonzero for a sleeper, body A pays an impulse body B never
receives. Audit E-2, unchanged at HEAD. This is the exact bug already
fixed at the normal-impulse apply (`:3342-3350` records it).

**D-6 (HIGH). `update_rest_state` has no authority guard.**
`:4729-4770` skips only massless bodies. A KINEMATIC body gets a sleep
state it has no use for, and `:4750-4754` clears `is_at_rest` on any
dissatisfied constraint. That is what wakes a pinned anchor so the
pre-resolver pricing calls it a 2.38 gram feather while the apply refuses
to move it. Audit E-1, unchanged at HEAD, and the direct cause of the two
tests blocking the WAKE_RESOLVER flip.

**D-7 (MEDIUM). Six inline inverse-mass derivations bypass both
predicates.**
`:1336-1337`, `:1559-1560` (pre-resolver arms, die at the flip);
`:5966-5967` (D-4). Every one of them is an independent opinion that must
be kept in sync by hand.

**D-8 (MEDIUM). Seven SET sites in `src/` have no release, and nothing
detects it.**
Six in `src/worldgen/`, one in `butterfly_flight.cpp:116`. There is no
runtime or static check that a body left EXTERNAL still has a live writer.
The ruling makes a set-without-release a defect; nothing in the tree can
observe one.

**D-9 (LOW, diagnostic). The gravity canary prints a lie.**
`:591-594` prints "P<i> receives gravity" before the exemption at `:608`
decides otherwise. Anyone debugging a driven limb with `CANARY_PID` is
told the opposite of what happens. MEASURED.

---

## 2. THE LAW — candidate INV-32

Proposed line for `tests/invariants/INVARIANTS.jsonl`, same schema and
field order as INV-31.

```json
{"id": "INV-32", "slug": "one-motion-authority", "kind": "law", "derives_from": ["INV-7", "INV-15", "INV-18", "INV-1"], "statement": "Exactly one state answers 'who owns this body's motion right now': an authority that is HELD by a named writer and RELEASED by that same writer, read by physics through the predicates that price momentum, geometric repair and rotation, and by nothing else. A representation choice (which field carries orientation), a performance cache (sleep) and a game category (ParticleOwner) never answer an authority question: two bodies identical in mass, geometry and authority receive identical force, identical momentum and identical repair however their orientation is represented, whoever last wrote them, and whether or not one is asleep. Momentum an authority refuses is BOOKED to the holder, never discarded, at every door without exception. An authority held with no live holder is a leak and is reported.", "mechanism": "NOT YET SHIPPABLE. MotionAuthority state + opaque holder id on Particle; three predicates (inv_mass_momentum, inv_mass_positional, inv_inertia_momentum) as the sole readers; refusal booked inside apply_pair_impulse so 'forgot to book' is not writable; static ratchet test_inv32_authority_gate counting inline inverse-mass/inertia derivations, ParticleOwner reads and cache-as-authority reads in the physics TUs, all to zero; leak sweep over held authorities with no live holder. Ladder: tests/test_motion_authority.cpp, red-first.", "verification": "runtime", "origin": "owner ruling 2026-08-14: is_quat_driven+owner is 'completely wrong... needs to be folded into solver_mode, since it's the same logic/essence'; the 15.625 kg chest that a boulder passes through, priced infinite by a chain of three mechanisms none of which is the KINEMATIC enum", "status": "aspirational"}
```

**Falsifiable clauses, each mapped to a ladder rung in §4:**

| Clause | Rung | Measured predicate |
|---|---|---|
| representation is not authority | R1 | twin bodies differing only in the orientation-truth flag: identical `vz` after gravity, identical `Δv` after an identical strike |
| game category is not authority | R2 | twin bodies differing only in `ParticleOwner`: identical, same measurements |
| cache is not authority | R7 | the existing twin-scene invisibility check, `test_sleep_wake_resolver` |
| held and released by a named writer | R3 | after release, the body falls; the leak sweep reports zero |
| refusal is booked at every door | R4 | `Σ booked == Σ refused` across normal, friction, gluon and warm-start doors, within 1% |
| one reader per question | R6 | static count of inline derivations == 0 |

---

## 3. THE MECHANISM

### 3.1 The three options, evaluated

**(a) One enum with an explicit holder and a release contract.**

```cpp
// src/particle_core.h
enum class MotionAuthority : uint8_t {
    SOLVER   = 0,   // physics owns this body's motion
    EXTERNAL = 1,   // a named writer owns it, right now
};
MotionAuthority motion_authority = MotionAuthority::SOLVER;
uint16_t        authority_holder = 0;   // opaque to physics; 0 = none
```

`ParticleSolverMode` already IS this enum minus the holder:
`DYNAMIC == SOLVER`, `KINEMATIC == EXTERNAL`, same bit, same meaning after
`b42ebbe`. So (a) is not a new state, it is the existing state plus the
two things the 2026-08-14 ruling demands and the current field cannot
express: **who holds it**, and therefore **who must give it back** and
**who receives the refused momentum**.

`authority_holder` is opaque to physics. The solver copies it into the
refused-impulse ledger and never compares it to anything, which is what
keeps INV-15 true by construction rather than by review.

- Pro: expresses the release obligation; gives the refused ledger a
  destination; makes a set-without-release detectable (a holder id that no
  registered writer claims is a leak).
- Con: one new field per particle (2 bytes), a writer registry, and every
  SET site must name itself.

**(b) Keep `solver_mode`; make quat-drive and sleep express through it.**

Delete C1-C7 and let the drive express itself by holding KINEMATIC; let
sleep stop claiming immovability (INV-31, already built).

- Pro: no new state, smallest diff, and it is most of what the owner
  literally asked for ("folded into solver_mode").
- Con: it cannot express the release obligation, so D-8 stays unobservable;
  and the refused ledger has no destination, so `take_refused_impulse` stays
  a polling API that any subsystem may drain, including the wrong one. The
  ruling says "whoever takes authority owns giving it back". A state with
  no holder cannot name that whoever.

**(c) A capability-style predicate set.**

`may_receive_momentum(p)`, `may_be_repaired(p)`, `may_receive_force(p)`,
`may_rotate(p)` as named predicates.

- Pro: this is the shape the code already half has, and it is the only
  option that addresses the angular side (D-2), which today has four
  formulas and no door.
- Con: predicates alone do not stop a call site re-deriving the answer
  inline. Six sites do exactly that today (D-7). Necessary, not sufficient.

### 3.2 Recommendation: (a) composed with (c), and (b) is the subset that is already true

**One state with a holder (a); exactly three predicates read it (c); and
nothing else in the physics TUs may derive an inverse mass or inverse
inertia (the gate that makes it stick).**

Reasoning, visible:

- (b) alone fails the ruling's second half. The owner ruled that KINEMATIC
  is "set when needed ... but then RELEASED". A field with no holder cannot
  express an obligation, and D-8 shows that seven live sites already break
  it silently. The holder is the smallest addition that makes the
  obligation observable.
- (c) alone fails D-7 and D-2. Naming the questions does not stop the
  answers being re-derived. The mechanical part is the gate, not the names.
- (a) alone fails D-2, because a linear-only state does not price rotation.
- Composed, they make the defect class **unwritable** rather than fixed
  three times: the state is the only source, the three predicates are the
  only readers, and a static ratchet counts every attempt to be a fourth.

**The three predicates, and only these three:**

```cpp
float inv_mass_momentum   (const Particle&);        // may receive momentum (INV-7)
float inv_mass_positional (const Particle&);        // may be moved for repair (INV-21)
Mat3  inv_inertia_momentum(const Particle&);        // may physics rotate it (INV-16, D-2)
```

They differ on exactly one axis and it is documented: sleep is immovable
to momentum only while INV-31 is off, and is always movable to repair,
because a sleeping body's overlap is as real as an awake one's
(`:3888-3902` already argues this). `inv_inertia_momentum` returns the zero
matrix for EXTERNAL, massless, and bodies with no rotational DOF.
Forces (gravity) route through `inv_mass_momentum`, which is already true
at `:585`.

### 3.3 What changes, site by site

**Row build.** `:1331-1338` already calls `inv_mass_momentum` behind the
resolver lever. The `else` arm (`:1336-1337`) and its twin (`:1559-1560`)
are deleted at the flip. Contact rows gain the angular term from
`inv_inertia_momentum` when the rotation campaign's block row lands; until
then, C2/C3 are replaced by `inv_inertia_momentum(pa) != 0`.

**Apply.** The inline copy at `:3338-3372` routes through
`apply_pair_impulse`. The friction applies at `:3495` and `:3517` drop
their guards and multiply by `inv_mb` alone; the `v_rel` guards at `:3474`
and `:3504` are deleted, because a body the door will move must be
measured as moving (INV-20). C6/C7 become
`inv_inertia_momentum(pa) != Mat3::zero()`.

**Force site.** `:585` unchanged. `:590` unchanged and correct: it is the
cache's own exact-balance argument, not an immovability claim, and the
ruling on 2026-08-14 confirmed it. **`:608` is deleted**, see §3.5.

**Position pass.** `:3903-3907` unchanged. `:5966-5967` in
`project_gluon_positions` is replaced by `inv_mass_positional` (D-4).
`:3944-3947` becomes `inv_inertia_positional`, the repair sibling.

**Sleep.** `update_rest_state` (`:4729`) gains the authority guard: a body
whose motion an external writer owns has no equilibrium to cache (D-6). It
also zeroes `omega_x/y/z` alongside the linear velocity, and
`resolve_sleep_wakes` judges rotation as well as translation (D-3).

**Integration.** `:4470` and `:4867` unchanged in meaning; they read the
state through the same names as everyone else.

### 3.4 Refusal is booked inside the door

Today only one site books: `:3358-3371`, at the normal-impulse apply. The
friction applies, the gluon rows, the warm-start apply (`:2774-2807`) and
the angular applies all drop the EXTERNAL half in silence. That is INV-7's
"every velocity write passes through one door" honoured for the delivery
and abandoned for the refusal.

The fix is structural, not additional: **`apply_pair_impulse` books what
it could not deliver.**

```cpp
static inline void apply_pair_impulse(Particle& a, size_t ia,
                                      Particle& b, size_t ib,
                                      float jx, float jy, float jz) {
    const float inv_a = inv_mass_momentum(a);
    const float inv_b = inv_mass_momentum(b);
    a.vx += jx * inv_a;  a.vy += jy * inv_a;  a.vz += jz * inv_a;
    b.vx -= jx * inv_b;  b.vy -= jy * inv_b;  b.vz -= jz * inv_b;
    if (inv_a == 0.0f && a.motion_authority == MotionAuthority::EXTERNAL)
        record_refused_impulse(ia,  jx,  jy,  jz);
    if (inv_b == 0.0f && b.motion_authority == MotionAuthority::EXTERNAL)
        record_refused_impulse(ib, -jx, -jy, -jz);
}
```

The condition is EXTERNAL only, never sleeping and never massless. A
sleeper is not refused under INV-31; it is priced at true mass and the
resolver judges the result. A massless body has nothing to refuse. One
reason, one ledger.

With the door booking, "forgot to book" stops being a writable bug at any
call site, which is the same property the door already gives delivery.

### 3.5 What replaces `:608`, exactly

Delete the line. Nothing replaces it at that site. The intent is
re-expressed by the authority state, and it splits into three regimes that
today are one:

| Regime | Who writes the bone | State | Gravity | Contact response |
|---|---|---|---|---|
| **FK-written** (a clip sets the position) | animation | EXTERNAL, holder = the locomotion writer | none, refused by `inv_mass_momentum` at `:585`, no exemption needed | contact impulse computed, refused, **booked** to the holder |
| **Physics-driven** (a gluon quat-drive row holds the pose) | physics | SOLVER | **applies** | full: the body moves, the drive pulls it back |
| **Released** (ragdoll) | nobody | SOLVER, holder = 0 | applies | full |

So a driven limb's gravity, authority-held versus released:

- **Authority held (FK-written).** The door refuses gravity because an
  external writer owns the position. This is exactly today's observable
  behaviour for those bones, produced by the law instead of by an
  exemption. The difference that matters: the strike is now booked and
  handed to the writer, which is what KNOCKBACK needs.
- **Released.** Gravity applies. It falls. `test_humanoid_ragdoll` already
  measures this at 1.231 m in 0.5 s.
- **Physics-driven.** Gravity applies, and the drive must hold the pose
  under its own weight. **This is INV-13, and it is where deleting `:608`
  hurts.** `6cd624c` records the symptom honestly: giving DYNAMICS bones
  weight "put visible churn into the human's limbs", and physics torquing
  quat-driven bones spun 20 of her 30 particles. The exemption is a patch
  over an INV-13 debt (a driven joint that cannot hold its commanded pose
  under load) and an INV-22 debt (FK and the solver both writing one
  bone's orientation). Deleting it exposes both. That is correct and it is
  the point; it is also why S3 lands behind a lever and why the rotation
  campaign is a hard dependency, not a neighbour.

### 3.6 What happens to `is_quat_driven`

**It does not move into the authority enum.** Folding it there would
rebuild the conflation inside the new state, one level down. It is renamed
to say what it is:

```cpp
// src/particle_core.h — renamed, semantics unchanged
bool orientation_is_quaternion = false;   // was: is_quat_driven
```

or, better if it proves derivable, deleted in favour of a predicate over
whether the body carries rotational DOFs (`GetInertiaAboutAxis` nonzero on
more than the Z axis, or an explicit `has_rotational_dof`). That is an
owner question, §6 Q5.

Under the rename:

- **Survive** (representation, correct): `:2283`, `:2286`, `:4098`,
  `:4957`, `narrow_phase.cpp:661`, `:677`,
  `humanoid_locomotion.cpp:5520`. Seven reads, all asking "which field is
  the truth".
- **Vanish**: C1 deleted outright; C2-C7 replaced by
  `inv_inertia_momentum(p)`, and in the rotation campaign's block-row form
  they collapse from six sites to one (price, measure and spend are one
  matrix in one row).
- **Setters** (`humanoid_locomotion.cpp:1935`, `:2020`, `:2507`, `:2575`,
  `entity_manager.cpp:148`) keep setting it; they are declaring a
  representation, which is legitimate. They additionally take or release
  authority, explicitly, which today they do by side effect.

### 3.7 What happens to `ParticleOwner`

It stays exactly where CLAUDE.md puts it: game-layer bookkeeping in the
systems that need it, invisible to the solver. After S4 the physics TUs
contain zero reads of it, and `test_inv15_owner_blindness`'s
`KNOWN_OWNER_READS` table empties. **Task #43 dissolves rather than
moves**: there is no site left to relocate, because the question those
seven reads were asking is answered by the authority state.

`authority_holder` is not `ParticleOwner` renamed. It is an opaque writer
id that physics stores and never interprets; `ParticleOwner` is a
three-valued game category that physics must not see. Conflating them
would be the same defect in a new field.

### 3.8 The gate that makes the class unwritable

`tests/test_inv32_authority_gate.cpp`, a static ratchet in the style of
`test_inv29_constants_gate` and `test_inv15_owner_blindness` (comment and
string stripping reused from either). It counts, over the physics TUs
only, outside the three predicate functions:

| Counted | Start value (MEASURED at `e04cd43`) | Target |
|---|---|---|
| inline `1.0f / *.GetMass()` derivations | 6 (`:1336`, `:1337`, `:1559`, `:1560`, `:5966`, `:5967`) | 0 |
| inline angular immovability formulas | 7 sites, 4 distinct formulas (§D-2) | 0 |
| `ParticleOwner` / `.owner` reads | 7 | 0 |
| `is_at_rest` read to answer an authority question | to be captured in S0 (54 total reads, most legitimate cache use) | 0 |

The last row needs its baseline captured before the ratchet arms, because
most of the 54 `is_at_rest` reads are the cache doing its job. The
distinguishing pattern is `is_at_rest` appearing in an expression that
produces an inverse mass, an inverse inertia or a skip of an impulse
apply. S0 captures the list; the owner reviews it once; it is pinned.

---

## 4. THE TDD LADDER

Red-first. Style follows `tests/test_sleep_wake_resolver.cpp`: twin-scene
invisibility plus a physical anchor, no expected values invented by the
test. New file `tests/test_motion_authority.cpp` unless noted.

**R0 — the harness tells the truth.** *(pre-requisite, proves nothing)*
`test_humanoid_knockback` builds the BVH before stepping, and its HIT
readout uses a contact event rather than a velocity minimum.
Predicate: at least one box-box `CollisionEvent` pairs the boulder with a
humanoid particle within 40 frames; boulder `vx` at the frame of that
event is below 7.5 m/s.
Status: **RED**. MEASURED zero box-box rows in that process (Correction 1).
INV: none. Without it, R4 and the knockback rungs are unmeasurable.

**R1 — representation is not authority.** *(the tripwire the owner asked
for)*
Two boxes, identical mass, geometry, position modulo an offset, and
authority. One has the orientation-truth flag set, the other does not.
Predicate A: after 60 frames of gravity, `|vz_a - vz_b| < 1e-6`.
Predicate B: struck by identical boulders, `|Δvx_a - Δvx_b| < 1e-6`.
Status: **RED**. The quat twin receives no gravity (`:608`) and then
sleeps into infinite mass (Correction 3).
INV-32 clause 1, INV-15.

**R2 — game category is not authority.**
Same twin scene, differing only in `ParticleOwner` (PHYSICS vs DYNAMICS),
representation flag equal.
Predicate: identical `vz` and identical `Δvx`, same tolerances.
Status: **RED** when the representation flag is also set, GREEN when it is
clear (because C1 requires both). Both cases are asserted so the
conjunction cannot hide either half.
INV-32 clause 2, INV-15.

**R3 — authority is held and released by a named writer.**
A writer takes authority over a body, drives it for 30 frames, releases it.
Predicate A: while held, the body does not fall and `inv_mass_momentum`
reports zero.
Predicate B: after release, it falls within 5% of `0.5 g t²`.
Predicate C: the leak sweep reports zero held authorities without a live
holder, and reports exactly one after a deliberate leak is planted.
Status: A and B **GREEN** in substance today via
`tests/test_humanoid_ragdoll.cpp` (MEASURED: 0 of 17 pinned, 1.231 m fall)
— that test **becomes the humanoid prover for R3A/R3B**. C is **RED**: no
leak sweep exists.
INV-32 clause 4, INV-1.

**R4 — refusal is booked at every door.**
Strike an EXTERNAL body obliquely so the normal row and both friction
tangents all fire, against a rough floor so warm starts engage.
Predicate: `|Σ booked − Σ (impulse × refused side)| / Σ ≤ 0.01`, drained
via `take_refused_impulse` each frame.
Status: **RED**. Only the normal apply books (`:3358-3371`); friction
(`:3495`, `:3517`), warm start (`:2774-2807`) and the gluon rows drop it.
INV-32 clause 5, INV-7, INV-3.

**R5 — the strike is invisible across the authority boundary.**
Twin scenes, same as the resolver ladder's instrument. Scene A: an EXTERNAL
body whose holder drains the booked momentum and applies it as a position
write of `J/m·dt`. Scene B: a plain SOLVER body of the same mass.
Predicate: positions agree within the engine's own quietness bound
(`REST_VELOCITY_THRESHOLD`, 0.1 m/s, times the elapsed time) at frame 60.
Status: **RED**. This is the knockback law stated as an invisibility
check, and it is what `test_humanoid_knockback` R2/R3 measure informally.
INV-32 clause 5, INV-3.

**R6 — one reader per question.** *(static)*
`test_inv32_authority_gate` (§3.8). Ratchet, expect-fail style like
`test_inv15_owner_blindness`: passes while the counts match the pinned
table, fails when any count changes in either direction.
Status: **RED-by-construction at the pinned values**; goes green at zero.
INV-32 clause 6, INV-15, INV-29's discipline.

**R7 — the cache is invisible.**
`tests/test_sleep_wake_resolver.cpp`, unchanged. It already asserts the
twin-scene invisibility of sleep, and MEASURED with `WAKE_RESOLVER=1` it
reads 12/12 with delta 0.001 (ledger 2026-08-14).
Status: **GREEN under the lever, RED as default**. Becomes an INV-32
prover the moment the lever flips.
INV-32 clause 3, INV-31, INV-18.

**R8 — a sleeping body does not spin.** *(D-3)*
Give a body residual `omega` below the linear rest threshold, let it sleep,
step 300 frames.
Predicate: `|rotation change| < ANGULAR_SLOP` over the whole window.
Status: **RED**, established from code (`:4759-4760` zeroes only linear;
`:4867` gates only on KINEMATIC; `:545` judges only linear). Not yet
observed in a run; S0 must observe it before the rung is trusted.
INV-18, INV-32 clause 3.

### 4.1 Existing tests, classified

**Become provers, no change:**
`tests/test_humanoid_ragdoll.cpp` (R3A/R3B, green),
`tests/test_sleep_wake_resolver.cpp` (R7),
`tests/test_inv15_owner_blindness.cpp` (R6's owner column; its ratchet
must be updated in the same commit that empties it),
`tests/test_knockback_scene.cpp` (the clearest correct KINEMATIC-writer
statement in the tree per the audit; it becomes the bucket-A control).

**Need re-baselining, and why:**
`tests/test_light_body_ringing.cpp` and `tests/test_grass_yields.cpp`.
The audit §6 establishes, with a control experiment (`SLEEP_LAW_OFF=1`,
resolver OFF, reproducing the resolver's numbers to three decimals), that
both green baselines are produced by D-6: a pinned anchor woken by
`update_rest_state`, priced as a 2.38 gram feather, supporting nothing.
`peak_early = 1.200` at every ratio is exactly the injected push, which a
collapsed heap cannot exceed. Re-baseline against a standing chain and a
real wade; the 500x/1044x tear (1.79 m/s from a 1.2 m/s push, a real
INV-17 violation) and the 67% grass retention become open defects, which
is what they always were.
`tests/test_physics_characterization.cpp`: any authority change moves the
pinned hash. Re-pin once, in its own commit, with the diff reviewed
(same protocol the rotation campaign sets for its S1).

**Vacuous, must be rewritten:**
`tests/test_humanoid_impact.cpp`. `bool pass = impact_detected` at `:399`
and `:638` is the whole assertion; the displacement figures are decoration.
And its rig is a `HumanoidGenerator` physics humanoid, never registered
with locomotion, so it exercises no authority mechanism at all (MEASURED:
hips move 0.13 m). Rewrite it to assert a booked-and-drained knockback on
a locomotion-registered humanoid, i.e. make it the scene-level instance of
R5, or retire it in favour of `test_humanoid_knockback`.
`tests/test_humanoid_knockback.cpp` R1's HIT/MISSED print (Correction 2)
and its header's causal claim (Correction 3) both need correcting in the
same commit as R0.

---

## 5. THE SLICE SEQUENCE

Dependency-ordered. No estimates. Each slice names the mechanism it lands,
its lever, the rung that goes green, and what gets rebuilt if it is done
out of order.

**S0 — Harness truth and baseline capture.**
Mechanism: `update_bvh()` in `test_humanoid_knockback`; contact-event HIT
readout; header corrections; capture the `is_at_rest`-as-authority read
list for the R6 ratchet; observe D-3 (the spinning sleeper) in a run.
Lever: none, test-only.
Green: R0.
Out of order: every measurement taken in that file before S0 is a phantom,
including the one that motivated this study.

**S1 — Name the three questions.**
Mechanism: extract `inv_inertia_momentum` (and its positional sibling);
make the three predicates the only functions in the physics TUs that
produce an inverse mass or inverse inertia; route the inline apply at
`:3338-3372` through `apply_pair_impulse`.
Lever: none needed. This is a pure extraction and must be **bit-identical**
against the INV-27 characterization baseline; that is the proof it landed
correctly.
Green: nothing yet. Arms R6's counter.
Out of order: S2, S3 and S4 all have nowhere to land their deletions.

**S2 — The remaining inline opinions die.**
Mechanism: D-5 (friction applies `:3495`, `:3517` and `v_rel` guards
`:3474`, `:3504`); D-6 (`update_rest_state` authority guard, plus zero
`omega` on entering rest); D-4 (`project_gluon_positions` `:5966-5967`
through `inv_mass_positional`); D-3 (`resolve_sleep_wakes` judges
rotation); D-9 (move the gravity canary below the exemption, or delete the
exemption first and the canary becomes honest for free).
Lever: `AUTHORITY_DOOR=1`, default off, because D-6 changes the two tests
below.
Green: R8. Partially arms R4.
Out of order: done before S1 it duplicates the extraction; done after S6
it is the flip's blocker rather than its prerequisite.

**S3 — Delete `:608`. The category error dies.**
Mechanism: gravity flows through `inv_mass_momentum` alone. FK-written
bones take EXTERNAL authority explicitly at
`humanoid_locomotion.cpp:1547` and release it at `:1873`, `:1932`,
`:2017`, `:2486`, `:2572`. Physics-driven bones stay SOLVER and weigh.
Lever: `QUAT_GRAVITY_EXEMPT_OFF=1`, default off (preserving today's
behaviour).
Green: R1, R2.
**This is the slice that hurts.** It exposes the INV-13 debt `6cd624c`
patched (a drive that cannot hold its pose under its own weight) and the
INV-22 debt underneath it (FK and the solver writing one bone's
orientation). Expect the churn that commit describes. That is the
diagnosis surfacing, not a regression, and it must not be re-patched with
a category branch.
Out of order: done before S1 there is no predicate to route gravity
through; done before S2 the sleep chain (Correction 3) keeps
manufacturing the immovability the deletion is meant to remove, and the
rung reads green for the wrong reason.

**S4 — Rename the representation flag; the six rotational reads dissolve.**
Mechanism: `is_quat_driven` becomes an orientation-truth name (or is
derived, §6 Q5); C2-C7 route through `inv_inertia_momentum`.
Lever: none, semantics-preserving.
Green: R6's owner column reaches zero.
`test_inv15_owner_blindness`'s `KNOWN_OWNER_READS`, `TEST_AUDIT.jsonl` and
the ledger update in the same commit, as that test's own header requires.
Out of order: done before S1 there is no predicate; done before S3, C1 is
still alive and the count cannot reach zero.

**S5 — The holder and the release contract.**
Mechanism: `authority_holder` on `Particle`; a writer registry; `take` and
`release` calls that name the holder; the leak sweep. The refused ledger is
keyed to the holder rather than polled by anyone.
Lever: `AUTHORITY_LEAK_STRICT=1` for the sweep's abort mode, lenient by
default for the inventory pass (the TURTLE_STRICT pattern).
Green: R3C.
Independent of S2/S3/S4 after S1; can run in parallel.
Out of order: before S1 it has no readers.

**S6 — The door books, and the WAKE_RESOLVER flip.**
Mechanism: booking moves inside `apply_pair_impulse` (§3.4); the
pre-resolver arms at `:1336-1337`, `:1559-1560` are deleted; the lever
becomes the default and `WAKE_TRANSFER_SPEED`'s pre-solve gate is deleted
per INV-31's own text.
Lever: `WAKE_RESOLVER` flips to default-on and then the env read is
removed.
Green: R4, R7.
**Collision, named.** The flip is blocked today on
`test_light_body_ringing` and `test_grass_yields`, whose baselines encode
the D-6 quirk. S2 removes that quirk with the lever **off**, which is when
both tests go red honestly; the re-baseline happens in S6 against a
standing chain and a real wade. The residual INV-17 tear at 500x and 1044x
is a genuine open defect that the quirk was concealing; whether to flip
before or after diagnosing it is §6 Q6.

**S7 — Knockback as a game-layer policy.**
Mechanism: the locomotion writer drains its booked momentum and decides
what a push means. `TransformationEffect::KNOCKBACK`
(`src/generated/space_ontology.h:617`) gets its first implementation, at
the game layer, where it belongs.
Lever: none; new behaviour behind a KG property the game sets.
Green: R5, and `test_humanoid_knockback` R2/R3.
Out of order: before S6 there is nothing reliable to drain.

### 5.1 The three named collisions

**With the WAKE_RESOLVER flip (INV-31).** Owned by S6, unblocked by S2.
The two blocking tests are not regressions; the audit's control experiment
(resolver OFF plus `SLEEP_LAW_OFF=1`, reproducing the resolver's numbers to
three decimal places) establishes that their green comes from D-6. S2 makes
that red with the lever off, which is the honest state, and S6 re-baselines.
Additionally: S3 shrinks the resolver's blast radius on humanoids, because
a limb that gets gravity stops sleeping into infinite mass in the first
place.

**With the rotation campaign (`docs/todo_plans/ROTATION_CAMPAIGN_DESIGN.md`).**
Hard dependency, both directions. The campaign's §2.3 already specifies
`inv_inertia_momentum(p) -> Mat3` as "one predicate, two return types, so a
body can never be immovable to translation and free to spin", and its §4.2
already expects to shrink the INV-15 table. This study's S1 and S4 land
exactly that predicate. Whichever lands first, the other **must** consume
it rather than re-derive: contact torque must move bodies, so every contact
row inherits the authority answer. If the block row lands first and derives
its own opinion, D-2 grows a fifth formula instead of losing four. Name the
predicate's owner explicitly in whichever campaign moves first.

**With task #43 (remove the seven owner-reads).** Dissolved by S1 plus S3
plus S4, not relocated. There is no site left to move, because the question
those seven reads asked is answered by the authority state. The ledger
entry closing #43 should say "dissolved", and
`test_inv15_owner_blindness`'s table empties in S4's commit.

---

## 6. OPEN QUESTIONS FOR THE OWNER

Each is a real fork with the evidence, not a recommendation in disguise.

**Q1. A driven limb is struck. What should happen?**

- *Absorb into the drive.* The writer drains the booked momentum and moves
  the whole body (a stagger). Keeps the animation coherent; a person hit in
  the chest steps back rather than dismembering. Evidence: this is what
  `test_humanoid_knockback` R2/R3 already assume ("the decision is: absorb
  it into the body's world position"), and the refused ledger's header at
  `include/logosphere/physics/physics_system.h:401-420` describes exactly
  this handoff.
- *Break authority and ragdoll.* The limb goes SOLVER and physics takes it.
  Physically honest; visually violent for a shoulder-check. Evidence: the
  mechanism exists and is green (`test_humanoid_ragdoll`, 1.231 m fall),
  and `unregister_humanoid` is already the total-release path.
- *A threshold.* Absorb below some booked-momentum bound, break above it.
  Evidence against, stated plainly: INV-10 forbids a raw impulse threshold
  ("a shrug to a branch and a shove to a leaf"); it would have to be
  expressed in a mass-uniform quantity, most likely the velocity the strike
  would impart to the limb, compared against something derived rather than
  declared (INV-9, INV-29). No such derivation exists today, and inventing
  one is a design decision, not a tuning knob.
- Note: this is a **game-layer** decision under CLAUDE.md's boundary. The
  engine owes the booking and the drain; the policy is the game's. What the
  owner rules here determines whether the engine also owes a *partial*
  release API, which is Q2.

**Q2. Must a partial ragdoll be expressible: one limb released, the rest
driven?**

- *Yes.* Then authority is **per-particle**, releases must be per-particle,
  and the joint gluon between a released and a held bone becomes a
  contact between a SOLVER body and an EXTERNAL one. Evidence that this
  works: `physics_system_v4.cpp:1698` already skips only *both*-EXTERNAL
  gluon pairs, so a mixed pair already solves, and the pin-gluon lifecycle
  (`humanoid_locomotion.cpp:2028-2110`, released at `:2041-2044`, helper
  particle deleted at `:1853-1854`) is a working mixed-authority
  attachment. Cost: the leak sweep and the drain both become per-particle,
  and a half-released humanoid is a state the animation layer must be able
  to represent (it currently cannot; `all_particle_indices` is walked
  whole).
- *No.* Authority is per-entity, releases are all-or-nothing, and a
  disabled arm is an animation, not a physics state. Cost: no severed
  limbs, no "this arm is broken" without a clip for it.
- Evidence either way is thin: nothing in the tree expresses a partial
  release today, and no test asks for one.

**Q3. Is animation authority per-particle or per-entity?**

Related to Q2 but separable, because the *storage* question and the
*release granularity* question can differ.

- *Per-particle* (today's shape): `solver_mode` is a `Particle` field, 12
  SET sites write it directly, and the two joint-walking release paths
  cover only joint children, which is exactly the bug `f5d05af` fixed for
  the hips. Evidence: the hips are nobody's child, and that structural fact
  produced a two-year-invisible pin.
- *Per-entity*: `EntityPhysicalState::apply_solver_authority`
  (`src/kg/entity_physical_state.cpp:34-78`) already walks `HAS_PART`
  recursively, works live and pre-activation, wakes on release, is armed at
  engine init (`src/core/engine.cpp:445-453`) and is proven bidirectional
  by `tests/test_ontology_levers.cpp:140-160`. **It has zero callers in
  `src/worldgen/`, `src/animation/` or `examples/`.** The transient
  mechanism the doctrine asks for is built, tested, and wired to nothing.
- Evidence for the fork: per-entity makes "release everything this writer
  holds" a single call and makes leaks trivially detectable; per-particle
  is what every existing site does and what Q2's "yes" branch requires.
  They are not exclusive (a per-entity call that writes per-particle state
  is the obvious middle), but the *authoritative unit* determines what the
  leak sweep can even ask.

**Q4. Does the enum get renamed?**

- *Rename* `ParticleSolverMode::{DYNAMIC, KINEMATIC}` to
  `MotionAuthority::{SOLVER, EXTERNAL}`. The words then say the law, and
  the schema text, `entity_physical_state.h` and INV-1's mechanism note
  (all three rewritten on 2026-08-14) stop needing a paragraph to explain
  that "kinematic" does not mean "immovable".
- *Keep the name.* 43 reads in the solver, 12 SET sites, 80 test sites, the
  KG string values `"KINEMATIC"`/`"DYNAMIC"`, and the schema all move
  together or none do. Pure churn against pure clarity.
- Evidence: the name is demonstrably misleading. `physics_tree_generator.cpp:917-918`
  quotes the old licence verbatim to justify a permanent pin, and 11 sites
  followed the spec that the name suggested. The name did that.

**Q5. Is `is_quat_driven` renamed, or derived and deleted?**

- *Renamed* to an orientation-truth name. Cheapest, and the seven
  legitimate reads keep working unchanged.
- *Derived*: a body's orientation truth is the quaternion whenever it has
  more than one rotational DOF in play. Would delete the flag and the class
  of "someone forgot to set it" bugs. Evidence for feasibility:
  `GetInertiaAboutAxis` already answers per-axis, and the campaign's §1.3
  is already re-deriving the inertia model. Evidence against: `:2283-2286`
  currently syncs `rotation_q` from Euler on the fly for non-quat bodies,
  so "derived" changes when that sync happens, which is a behaviour change
  inside the rotation campaign's blast radius.
- This is the one place where "fold it into solver_mode" would be actively
  wrong, and the reason is worth stating: the folded version would make a
  body's orientation storage decide who may move it, which is the defect
  restated one level down.

**Q6. Flip sequencing: before or after diagnosing the 500x/1044x tear?**

Carried forward from the audit §7 Q5 because S6 forces the decision.

- *Before*: CI shows the real INV-17 defect immediately; two tests stay red
  until it is fixed.
- *After*: CI stays green, but the quirk keeps hiding the tear and every
  measurement taken meanwhile is taken against a collapsed chain.
- Evidence: the tear reproduces today with `SLEEP_LAW_OFF=1` and the lever
  off, so diagnosis does not require the flip.

**Q7. Does the interaction-profile filter belong under this law?**

- *Yes*: it is a fourth way a body stops being moved, and the butterfly
  shows the combination (pin plus `collides_with = 0`) taking a creature
  out of physics in both directions with no release for either half.
- *No*: it is a declared pair policy about *media and materials*, not about
  authority; it is symmetric, it books the overlap in
  `filtered_overlaps_`, and it is the engine's honest answer to "this body
  is present but not solid" (`butterfly_flight.cpp:100-102`).
- Evidence: the mechanisms are genuinely different in kind. The question is
  whether INV-32's statement should name it as explicitly out of scope, so
  that a future reader does not add a fifth answer under its cover.

---

## Method notes

- Build: `cmake --build build-release -j8 --target test_humanoid_knockback
  test_humanoid_ragdoll`, exit 0.
- All runs headless. No window was opened.
- Levers used: `KB_PROBE`, `CANARY_PID`, `CANARY_FRAME_MAX`,
  `WAKE_RESOLVER`. `LOGOSPHERE_PHYS_TRACE` was **not** usable in the
  standalone ladders: `phystrace::init_from_env` is called only from
  `src/core/engine.cpp:1094`, so a test that does not construct an `Engine`
  gets level 0 regardless of the environment. The `CANARY_*` probes read
  `getenv` directly (`physics_system_v4.cpp:670-677`) and work everywhere.
- `phys_frame` in the canary output increments **per substep**, four per
  `physics.update`, and `CANARY_FRAME_MAX` defaults to 15. Both facts cost
  a wrong reading before they were checked; noted so the next reader does
  not pay again.
- Nothing was fixed. This is a design study.
