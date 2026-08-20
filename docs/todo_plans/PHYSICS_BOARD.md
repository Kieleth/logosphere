# The physics board

One page for every open front, so no one has to hold them all in their
head. Written 2026-08-14, after a day that closed three defects and
opened four questions.

**How to read this.** Each front is in exactly one class:

- **CLEAN NOW** — the mechanism is known, the fix adds no debt, and
  nothing has to be decided first. Do these whenever there is capacity;
  they make everything else measurable.
- **NEEDS DESIGN** — the fix is not knowable yet. A study exists or is
  owed. Doing these by instinct is how the last month's rework happened.
- **OWNER RULING** — the engineering is ready and blocked on a decision
  only the maintainer can make.
- **PARKED** — real, understood, deliberately not now, with the reason.

Every front cites its evidence. Nothing here is a hunch.

---

## The shape of the mess, in one paragraph

Three separate mechanisms answer "who may move this body" and disagree
(`solver_mode`, `is_at_rest`, `is_quat_driven`+`owner`). One of them is
a *representation* flag being read as an authority. Underneath that, the
angular side of the solver has four immovability formulas and no door,
where the linear side has had one since the deeper-law campaign. Most
open fronts are downstream of those two facts. The rest are honest
follow-ups: coverage the invariants promised and do not yet have, and a
substrate direction that would retire the whole class of "pin it so it
does not fall over."

---

## CLEAN NOW

Do in any order. None needs a decision; all reduce debt.

> **C1-C4 LANDED 2026-08-14.** Measured: the knockback fixture now
> builds a BVH and forms real contacts (the boulder goes 8.00 -> -0.00
> m/s against the humanoid where it previously lost 0.00074 m/s to air
> drag); `test_humanoid_impact` asserts displacement and passes on
> 0.13 m hips / 0.14 m chest instead of asserting nothing; the four
> friction sites route through the door and measure true relative
> velocity; sleep now owns its angular half. Default path bit-identical
> across the canary set. **C1 exposed a new front — see F1 below.**
> C5-C7 remain.

| # | Front | Evidence | Why it is clean |
|---|---|---|---|
| C1 | **Harness truth: `test_humanoid_knockback` never builds a BVH** | study §corrections; `physics_system_v4.cpp:1004` `bvh->is_ready()` false, candidate list always empty; boulder loses 0.00074 m/s crossing the chest (air drag) | The test measures nothing today. One `update_bvh()` call in the fixture. Until then every conclusion drawn from it is void — including two I already published and retracted. |
| C2 | **`test_humanoid_impact` asserts nothing** | `bool pass = impact_detected` is the entire assertion; displacements printed, never asserted; its humanoid is never registered so it has no pin at all (hips move 0.13 m, measured) | Rewrite the assertions to measure what the name claims. No mechanism change needed. |
| C3 | **Two friction applies still re-ask the immovability question** | audit E-2: `physics_system_v4.cpp:3463` and `:3485` carry the `!pb.is_at_rest && != KINEMATIC` guard on top of `inv_mb` — the identical bug fixed at `:3324` on 2026-08-14 | Same one-line cure, already proven. Destroys momentum in every frictional contact against a sleeper the moment WAKE_RESOLVER flips. |
| C4 | **A sleeping body can spin forever** | study D-3: enter-rest zeroes only `vx/vy/vz` (`:4759`); `integrate_angular_velocities` gates only on KINEMATIC (`:4867`); `resolve_sleep_wakes` judges only linear (`:545`) | Sleep's contract (INV-18) already says what it must do; the angular half was never written. |
| C5 | **Machine stages S5+ choreography** — *2026-08-15: the bridge is now built FROM the tear law instead of guessed. A nail breaks on SUSTAINED load (force = impulse/dt for 12 consecutive frames, physics_system_v4.cpp), not on an impulse — so "a fall, not a slide" was the wrong prescription and the bridge must be STOOD ON. It now fills the gap flush with the deck, carries its own 883 N on two 700 N nails (441 N each, 63% of hold), and a 62.5 kg post mid-span would load them to 748 N each (107%) — a tear by arithmetic. What is missing is the last 0.30 m: the post stops at x=7.61, the bridge starts at 7.90, and moving the post east makes it WORSE (the arrival is ballistic, so a post at 7.00 is struck lower and leaves with 0.43 m/s instead of 0.86, ending at 7.24). The leg needs a carrier that arrives ON the bridge, which is a redesign, not a constant.* — *2026-08-14: the two-box stack became a single post standing on the deck (the old top box slid on the LOWER BOX at mu=0.5 and stopped in 0.17 m while the polished deck was doing nothing — a scene that misrepresented its own physics). The post now decelerates at the deck's 0.5 m/s^2 and reaches the bridge. S5 still red: it arrives exhausted, and a body with just enough energy to arrive has none left to tear nails. The bridge needs an impulsive load — a fall, not a slide.* | `test_rube_goldberg_machine` halts at S5 with the resolver on (5/10); the struck box stops where friction says it stops, short of the bridge | Scene design, not physics. The ratchet is honest at 3 (default) / 5 (resolver). |
| C6 | **INV-17..28 have no proving tests** — *2026-08-15: 14 unproven -> 11. INV-6 WITNESSED for the first time (same press on +X/+Y/-Z: penetration spread 0.000000 m, gap spread 0.000169 m, normal residual <1e-5 m/s). INV-27 and INV-23 gained coverage by LINKING provers that already existed unlinked. The 11 remaining are mostly static-gate shaped (INV-5 springs, INV-19 damping, INV-28 one attachment definition), like the INV-29 constants gate.* | sweep: "COVERAGE HOLES (no proving test): INV-16..28, INV-5, INV-6"; task #45 | Writing provers cannot break anything. INV-6 (no gravity assumptions) has *never* been witnessed on a wall, a ceiling, or in zero-g. |
| C8 | ~~**The refusal ledger books 2.5% of the truth**~~ **LANDED 2026-08-14: 99.9%** (1440.2 refused / 1439.2 booked, `test_refused_momentum_ledger`) | F1 RCA: a clean airborne two-body test shows a KINEMATIC target refusing 1357.8 kg*m/s and booking **33.6** — the friction block (`physics_system_v4.cpp:3447-3550`) books nothing at all, and the warm-start apply (`:2801-2810`) spends outside the booking loop | My mechanism, incomplete. **A drain that receives 2.5% of the truth is worse than none**, and D1's S7 depends on it. Do this FIRST of the remaining clean items. |
| C9 | ~~**The Linux precheck swallows its own build errors**~~ **LANDED 2026-08-15** — every build step now names itself and prints 40 lines of real compiler output on failure; proved itself immediately by diagnosing an OOM-killed `cc1plus` in one read | `scripts/precheck_linux.sh:56-57` sends configure and build output to `/dev/null`, so a real link failure printed `PRECHECK FAIL (exit 1)` with nothing actionable — the error had to be reproduced by hand in docker. The script's own comment at `:63` documents a previous instance of this exact class | The gate is load-bearing (it caught a GNU-ld failure today that macOS could not see); a gate whose output you cannot read costs a debugging round every time it fires. |
| C7 | ~~**Energy ledger has no dissipation bucket**~~ **LANDED 2026-08-15** — four buckets, each naming a process: friction, material damping, air drag, and the sleep cache's absorption booked SEPARATELY because it is an optimisation, not physics. Measured on the machine: 26.3 J of friction at the impact frame, microjoules of cache absorption, near-zero when quiet | INV-19's own commitment; task #44 | Bookkeeping only. Needed before any future damping can be called "modelled". |

---

## F1 — RCA COMPLETE: the animation erases what the solver delivered

**Resolved 2026-08-14** by `F1_MOMENTUM_LOSS_RCA.md`. The standing
hypothesis below was WRONG and is kept only so the correction is
visible.

**The cause is outside physics.** The solver delivers the strike
correctly — 89-110 kg*m/s per frame into chest, head, neck and arms,
zero `BOTH_IMMOVABLE` rows. In the same frame
`HumanoidLocomotion::update_locomotion` broadcasts a SINGLE
hips-derived scalar onto all seventeen particles
(`humanoid_locomotion.cpp:4440-4442`) and `maintain_entity_shape`
snaps their positions back (`:5449`). The hips are KINEMATIC, so
`hips.vx` is permanently 0 and the broadcast value is 0. The erasure
is total, instantaneous, outside every door, booked nowhere, and it
runs for every standing registered humanoid every frame.
Smoking gun: seventeen different velocities in, one value out.

**The load-bearing property is none of the suspects.** Plain two-box
isolation: all five DYNAMIC variants — including the chest's exact
flags (`DYNAMICS` + `is_quat_driven`=1) and a forced-asleep box — are
BIT-IDENTICAL to the control (target moves +4.469 m). Only KINEMATIC
stops a body. The four-link chain is refuted at link 4: the chest is
never asleep at impact.

**Two readings I published were wrong.** The chest DOES move — 1.593 m
in 20 frames — whenever the locomotion writer is not running.
"8.00 -> -0.00 m/s" is the boulder landing on the turtle four metres
downrange, not the strike; the test's velocity-minimum statistic is
the same flaw the motion-authority study had already flagged.

**Goes to D1 as slice S5b** — "the FK rig holds what it writes, and
drains its book" — after C8 below.

### Superseded hypothesis (kept for the record)

Opened 2026-08-14 by C1, once the harness could finally form contacts.

A boulder at 8 m/s strikes a humanoid's chest and stops DEAD (8.00 ->
-0.00 m/s). Neither the chest nor the hips moves a micron, and the
refusal ledger books nothing along the strike. The momentum is simply
gone — an INV-3 violation at full scale, invisible until the BVH fix
made the collision real.

The chest at impact: `solver_mode` DYNAMIC, `owner` DYNAMICS,
`is_quat_driven` 1, mass 15.625 kg, and asleep (the four-link chain in
the motion-authority study: representation flag skips its gravity ->
velocity stays exactly zero -> sleep law rests it -> the momentum
predicate answers 0).

This is **D1's territory, not a separate fix** — it is the strongest
single piece of evidence for the authority unification, and it should
become a rung on D1's ladder rather than being patched here. Recorded
as its own front so it is not lost.

*(A second, smaller finding from the same run: a humanoid books
-14.23 N*s downward every frame from its own thigh resting on its
KINEMATIC hips. True, and not a shove. If the refusal ledger is ever
consumed by policy, structural refusals must be distinguishable from
external ones.)*

---

## F2 — NEW FRONT: a sphere will not slide a ramp that a cube slides

**MEASURED 2026-08-16, and now watchable.** `tests/test_ramp_race` (+ `_visual`) releases a cube and a sphere together on one 40 degree ramp: the cube travels **6.356 m**, the sphere travels **0.000 m**. Born red. Mechanism: the sphere-vs-box branch builds the box side with `aabb_of_box_particle`, which never reads rotation (`src/core/narrow_phase.cpp:957-976`), so the sphere meets the ramp's upright bounding slab and stands on an invented `(0,0,1)` normal. Two lines wide. INV-12 broken live.

Found 2026-08-15 by the machine's twin-path experiment, on its first
run. Two ramps side by side, same 40-degree slope, same STONE, same
drop, same mass class; a cube on one lane and a sphere on the other.

    f160   cube x=0.66 v=1.06 m/s    sphere x=0.45 v=0.00
    f220   cube x=2.52 v=2.65 m/s    sphere x=0.45 v=0.00
    f300   cube x=5.08 v=0.88 m/s    sphere x=0.45 v=0.00

The sphere lands and never moves again. 40 degrees is far past the
engine's own friction angle (mu=0.5 gives 26.6 degrees), so nothing
should hold it, and the cube beside it leaves at 2.65 m/s. Whatever the
sphere is resting on is not the surface the cube is resting on.

**Mechanism located 2026-08-16, and it is the suspect.** The call site is
`narrow_phase_particle_pair` (`src/core/narrow_phase.cpp:957-977`): for a
sphere-vs-box pair it builds the box side with `aabb_of_box_particle`
(`:459-466`), which reads `width`/`height`/`thickness` as world extents and
never looks at rotation. So a sphere meets a ROTATED ramp as the ramp's
upright bounding slab. Measured in `tests/test_collision_bounds_rotation.cpp`
part 5: on one 30-degree ramp, a box is correctly told `(0, -0.5, 0.866)`
and a sphere is told `(0, 0, 1)`. A flat shelf, with no lateral component
to drive it downhill, which is exactly the "lands and never moves again"
in the numbers above.

The gap is the sphere-vs-box PAIR, not the sphere: broad-phase bounds,
BVH leaves, turtle down-reach and box-box narrow phase all went oriented on
2026-08-12. That makes this INV-12 broken in one handler rather than a
missing capability, and the test now pins the wrong answer so a fix cannot
land silently. Still owed: the fix itself (an oriented sphere-vs-OBB
closest-point handler), and confirmation on the live ramp scene that this
is the whole of F2 rather than the first half of it.

Marked as the machine's frontier stage (SR TWIN PATHS), placed last so
a red measurement cannot starve the passing stages of coverage. When
contact torque lands, this same stage also becomes the rolling-versus-
sliding witness: it prints the speed ratio against the analytic 0.845
for a rolling solid sphere, every run.

---

## NEEDS DESIGN

| # | Front | Study | State |
|---|---|---|---|
| D0 | **THE INTERACTION ROUTER** (owner vision, 2026-08-15) | `INTERACTION_ROUTER_DESIGN.md` (excavation + design running) | The general form of D1's Q1, and the front the owner calls "one of the most fun/rewarding designs we need to do". A ROUTER between volitional and physical entities, *"like a networking router"*: routing between interactions and entities, loadable and dynamically loadable, **hierarchical** (`humanoid->torso` vs `humanoid->arm->hand->finger`), able to catch the interaction between particles, and **modifiable by any other system** — a combat system subscribing to weapon use so a silver bullet means one thing to a werewolf and another to a hippo. Routing is DATA in the KG, not engine code. Composable, so `hand is burning -> entity retracts in panic -> hand flies -> hits cabinet -> hand bounces` is a chain of semantically rich, physics-aware links **created from semantics**. Partially prototyped in `logomancers`' combat system, now being excavated. **This subsumes the "Eva walks into a tree" case: the routed outcome is "Eva stops", the rooted trunk unmoved, no energy created — which is exactly what the engine cannot express today.** |
|---|---|---|---|
| D1 | **Motion authority unification** | `MOTION_AUTHORITY_DESIGN.md` | **Study complete.** One state with an opaque holder id, three predicates, a static ratchet making a fourth reader uncompilable; `apply_pair_impulse` books its own refusals; `:608` deleted; the seven owner-reads go to zero so task #43 dissolves. **`is_quat_driven`: the board said "renamed not folded" and that CONTRADICTED a direct owner instruction, found by transcript audit 2026-08-16.** The owner: *"`is_quat_driven + owner` is completely wrong, and I think it was added without my knowledge/agreement, and needs to be folded into solver_mode, since it's the same logic/essence."* The study's counter-finding, which is why the disposition drifted: the flag is TWO things. Alone it is representation (which orientation field is the truth) and is used correctly at six sites; paired with `owner` it is authority and is used wrongly at seven. The authority half folds as the owner said. The representation half cannot fold into `solver_mode` without losing which quaternion is truth. **RESOLVED 2026-08-16: the owner ruled the quaternion is the only orientation truth, so the representation half ceases to exist and the flag is DELETED, not renamed. His original instruction stands unqualified. The failure was never putting the counter-finding back to him. See the ledger and R7.** Ladder R0-R8, slices S0-S7. **7 questions owed (see below).** |
| D2 | **Rotation campaign** | `ROTATION_CAMPAIGN_DESIGN.md` | **Study complete.** The full-Jacobian row already exists on `feat/joint-block-solver` (`4c92518`, stalls −96%) and both reasons it was parked have since landed. Slices S0-S10. **6 questions owed.** Collides with D1: both specify the same `inv_inertia_momentum` predicate — whichever lands first must own it, or the angular side grows a fifth immovability formula. |
| D3 | **The substrate: model the effect of what we don't simulate** | **none yet — owed** | The owner's direction: gravity is the effect of unmodelled mass, ground support is that same substrate pushing back, floating is an exemption from the field. Retires the entire "pin it so it doesn't fall over" class, subsumes task #48 (gravity as a lever), and is the honest replacement for the worldgen pins in P1. Needs a study before any code. |
| D4 | **The unexplained 500:1 / 1044:1 bond tear** | KINEMATIC audit, residual risk | Established: the resolver did not cause it and the old pricing quirk concealed it (`SLEEP_LAW_OFF=1` with the resolver off reproduces it to three decimals). Unexplained: why a bond between unequal masses amplifies 1.2 m/s into 1.79 m/s. An open INV-17 suspect that the flip makes visible in CI. |
| D7 | **THE AMBIENT MEDIUM: what is a body surrounded by?** (owner, 2026-08-16) | **none yet, owed** | Media exist and the model is sound (drag against RELATIVE velocity, buoyancy, fields, per `InteractionProfile`), and there IS an ambient air model (quadratic drag against `RHO_AIR = 1.225`, every moving body, every substep, `physics_system_v4.cpp:4674-4683`), which I first reported as absent because the grep searched for the word `ambient` and the mechanism is spelled `RHO_AIR`. The real defect is worse: **two drag laws run at once and neither knows about the other**, ambient quadratic booked to dissipation and medium linear booked nowhere, so a body in declared water is in water AND in air simultaneously. Neither is declarable, so no scene can be placed in vacuum. And the medium path is **linear only**, never touching `omega` or `torque`, so a paddle spins in water exactly as long as in air. `ANGULAR_DRAG = 0.95` per substep papers over BOTH holes with one number that ignores body, fluid and relative velocity: a body spinning in vacuum keeps 4.5 parts per million of its spin after one second. Its own comment confesses the INV-19 exposure and defers it; we then eradicated its linear twin `DAMPING_FACTOR` and left this one standing, **and the file said they were twins**. A fourth damper hides as a bare literal at `humanoid_locomotion.cpp:5277` (`const float ANGULAR_DRAG = 0.98f`), never extracted, a live INV-29 violation. **Same shape as D3**: gravity is the effect of mass we do not simulate, ambient drag is the effect of air we do not simulate. Design them together. Ontology work, Malleus discipline. GEDANKEN-25/26/27. **REVIEW ON LANDING: `tests/test_angular_dissipation` is born red against this front** (`tests/scenes/scene_spinning_cube.h` + the two drivers). It goes green when angular dissipation derives from a medium's density and the body's extents acting on RELATIVE angular velocity. It would ALSO go green if `ANGULAR_DRAG` were simply deleted, which would be wrong, because a body in real air must still slow. So the test does not by itself force the correct fix: GEDANKEN-27's half does, and that half is not instrumentable until the medium path touches `omega` and ambient air is declarable. Re-read the test's assertion when this front lands and do not weaken it. **Owner: "maybe even before rotation"**, and the sequencing argument agrees: deleting the constant without filling the holes leaves free bodies spinning forever, so this precedes D2's baseline rather than following it. |
| D5 | **Order-capture slices 2+** | `PHYSICS_PIPELINE_SEQUENCE.md`, task #52 | Slice 1 landed (23 nodes, six seed edges, three carrier hazards). The load-bearing deliverable — the producer/consumer hazard scan — is still owed, and every solve-loop change above compounds the need. |

---

## F3 — EXTERNAL authority over-delivers by 3.9x (measured)

Found 2026-08-15 by the effect-algebra study, GEDANKEN-4 (Eva walks into
a tree). `inv_mass_momentum` returns 0 for KINEMATIC
(`physics_system_v4.cpp:532-537`), so the contact prices the driven body
as infinitely heavy and the rooted trunk absorbs the ENTIRE approach
cancellation: **300 kg*m/s in one frame against an honest 77.8**.

This is the same defect INV-31 abolished for sleep — a PRE-SOLVE GUESS
about what a body can receive, decided before the solve rather than by
it — and the argument against it is already written in prose at
`:515-526` of the same file.

**It is a physics-board item, not a router item.** No routing table can
fix a row that is priced wrong: Kamaji decides what an interaction
MEANS, and the trunk is being handed momentum that physics should never
have delivered. Sits directly beside D1 (motion authority) and is
probably one of its slices.

---

## F4 — three engine capabilities the vocabulary assumed and does not have

All three surfaced while defining what interaction words may MEAN
(`EFFECT_ALGEBRA_DESIGN.md`), and each is small, sharp and checkable:

1. **`restitution` has no reader.** Declared at `physics_system.h:53`,
   zero hits across `src/`. Every free-pair contact in v4 is perfectly
   inelastic — so **`bounce` names something the engine cannot do**,
   including link 6 of the burning-hand chain the owner described.
2. **An impact cannot break a weld.** Welds tear only after 12
   CONSECUTIVE frames at threshold
   (`physics_system_v4.cpp:4396-4419`); a bullet delivers 24 N against a
   700 N hold. **No fast, light body can break anything in this engine.**
   (Same law C5 rediscovered from the other side.)
3. **`hit` is not a physical signal.** It requires intent, and no
   measurement distinguishes a punch from a stumble — which is exactly
   why logomancers needed a game-declared attack window. Deliberately
   NOT an engine word.

---

## D2 1.2 SLICE A LANDED (2026-08-19): normal-row contact torque, levered

Behind `CONTACT_TORQUE=1`, default off, default path bit-identical
(audited ladder numbers hold exactly). The manifold point the solver
computed and discarded is now the row's lever arm; effective mass gains
(r x J)^2/I per INV-20; the gate is `solver_mode == DYNAMIC` alone
(G-39: no owner read, no representation flag, KINEMATIC excluded by
what it IS).

**Measured under the lever: THE DIE FALLS FLAT.** Drop-ladder R1 green:
the 20-degree cube spins to 5.97 rad/s, rights itself, settles at
rot_y 0.0001 on the slab. Two honest residuals, both recorded, neither
papered: R0's flat-drop control acquires a deterministic 0.1929 rad/s
transient wobble (unstable equilibrium + sequential per-point solving),
decaying to 0.0002 — the control stays strict on the default path and
the lever's flip will need it rewritten as decay-bounded, owner ruling.
And the ramp stays at zero spin under the lever, WHICH IS THE SLICE
BOUNDARY BY INSTRUMENT: a sphere's normal passes through its centre
(r x n = 0 exactly) and a face-slider's point torques cancel — rolling
and tumbling are FRICTION torque, slice B.

## D2 1.2 SLICES B+C LANDED (2026-08-19): friction torque + turtle support vertex

Same `CONTACT_TORQUE` lever, default off, default path bit-identical
(audited reds hold, harness 27/27).

**B, friction torque**: friction measures the CONTACT-POINT relative
velocity (v + omega x r, both bodies), prices each tangent with its own
(r x t)^2/I (the normal row's K is the wrong Jacobian for a tangent),
and twists DYNAMIC bodies. **THE SPHERE ROLLS**: peak 5.30 rad/s, and
it now out-travels the sliding cube (6.259 vs 6.225 m), the physically
correct ordering. The cube tumbles at 4.07 rad/s.

**C, turtle support vertex**: a rotated box meets the plane at its
lowest vertex (centre minus signed half-extent axes — plane geometry,
INV-6-clean), priced per INV-20. Fixed: the cube that froze mid-tumble
balanced on its edge on the turtle (rot_y 0.8345, for ever). Bonus,
measured: proper pricing cut R0's flat-drop wobble 0.1929 -> 0.0198
rad/s. Unrotated boxes keep the no-torque row: a flat face's point
torques cancel by symmetry.

**Residuals, all measured, none papered:**
- **6.4 ANSWERED BY INSTRUMENT: the friction-basis fix comes WITH the
  row work, not after.** Under the lever the tumbling cube WALKS OUT OF
  ITS LANE (y -1.20 -> -2.05) in a scene with no lateral force: the
  axis-aligned tangent picks (study 1.5) put t1=(1,0,0) out of the
  ramp's face plane, and torque coupling turns that long-known wrongness
  into visible sideways drift. Next slice: tangents from the actual
  contact plane.
- The tumbling cube is still settling at frame 240 on the turtle
  (z 0.35): rocking under ANGULAR_DRAG; needs longer runs or the D7 fix.
- Ceilings: true rolling wants omega = v/r ~ 29 rad/s; MAX_OMEGA caps
  at 6.28 and ANGULAR_DRAG (D7) fights every spin. Both boarded already.

## D2 1.2 SLICE D LANDED (2026-08-19): in-plane friction basis + turtle contact patch

Same lever, default bit-identical (audited reds hold, harness 27/27).

**The basis (G-40)**: tangents derive from the contact — least-aligned
world axis, t1 = normalize(a x n), t2 = n x t1 — so on the ramp t1 is
exactly downhill and t2 exactly lateral, where the old axis-pick put
sin40 = 0.64 of t1 ALONG the normal, a normal impulse in disguise.

**The patch**: the single turtle support vertex was a step function in
orientation, and the lane trace convicted it precisely — y = -1.200
EXACT for the entire 135-frame descent, then the walk begins at
touchdown. Every corner within SUPPORT_PATCH_BAND (5 mm, schema) of the
lowest now carries a row, load shared, priced per corner (INV-20), the
same answer box-box gives through clipping. Lane kick cut 3.4x:
worst deviation 0.877 -> 0.261 m.

**The two mechanisms left, named by the trace, neither guessed:**
- **Seed asymmetry**: omega_x = +-0.08 appears at the FIRST slab strike
  (f15), before anything lateral exists — sequential per-point
  Gauss-Seidel order inside one manifold. The charted remedy is the
  block/manifold-simultaneous solve (`feat/joint-block-solver` prior
  art, stalls -96%); a per-manifold two-pass symmetrisation is the
  cheaper sibling. Needs its own slice.
- **The 45-degree parking**: the tumbling cube ends BALANCED AT EXACTLY
  45 deg (z = 0.346 = 0.2(cos45+sin45)), the unstable equilibrium,
  because ANGULAR_DRAG (D7) eats the angular momentum that would carry
  it past the balance point. The basics keep pointing at D7, again.

## D2's measurement ladder: the cube drop (2026-08-19)

Owner method: divide and conquer, G's first, asserts from the G's, then
measure, then solutions. GEDANKEN-35/36/37 -> `test_cube_drop_ladder`
(+ `_visual`), born red, one mechanism per rung:

| rung | claim (from the G) | measured today | names |
|---|---|---|---|
| R0 control | flat drop invents no rotation | **GREEN**: settles flat, z 0.2031, omega 0 | the reds below are physics, not noise |
| R1 | a cube dropped tilted 20 deg must TIP FLAT | **RED**: rests at rot_y 0.3491 (its release tilt), z 0.2595 (edge height), peak omega_y 0.0000 | D2 1.2 + the gate + the Euler publish (locks 1-3) |
| R2 | spin survives flight, dies at the face | **RED**: 0.0306 of the spin reaches the floor | D7, ANGULAR_DRAG |
| R3 | per flight frame, retention 1.0 to noise | **RED**: worst frame 0.8145 = 0.95^4 exactly | the leak is the constant, alone |

R1 is the sharpest single number on the rotation front: a body balanced
on an edge forever, with a prediction that contains no material constant
(20 degrees, nearest face, geometry). Solutions deliberately NOT chosen
yet — the owner rules after reading the measurements.

## The orientation-truth instruments (2026-08-19, pre-change)

`test_orientation_truth` (+ `_visual`), born red, tracked
direction-locked in physics-linux beside the drop ladder:

- **O0 GREEN**: the from_euler/to_euler round trip is identity away from
  the gimbal band, worst basis error 0.000002 over ~1,700 poses. The
  compass convention survives the trip; the unification cannot blame
  the conversions.
- **O1 RED, and the number mirrors**: twin cubes, identical spin about
  Y. The quat-truth twin ends at visible rot_y **0.3167** with
  divergence 0.0000; the Euler-truth twin at visible rot_y **0.0000**
  with divergence **0.3167**. The same angle, once as motion, once as
  incoherence — the default body's entire turn lands in a quaternion
  nothing reads. G-23's observable form.
- **O2 GREEN (lever landed 2026-08-19)**: `LOGOSPHERE_QUAT_TRUTH` exists,
  default OFF, env + `PhysicsSystem::set_quat_truth` for in-process A/B.
  Bit-identical baseline holds: hash `45c17b966f908416` lever off and
  on for a never-rotating body (the publish canonicalizes IEEE -0.0 so
  identity is a bitwise no-op). Spawn now seeds `rotation_q` from the
  Euler triple at the single `add_particle` choke point, so every body
  is born with ONE orientation.
- **O3 GREEN, and THE FLIP IS RULED AND DONE (2026-08-19, ledger)**:
  quaternion truth is the DEFAULT. `test_orientation_truth` fully green
  (ONE ORIENTATION, 0 failures) and promoted to the CI smoke list;
  `LOGOSPHERE_QUAT_TRUTH=0` is the kill switch. Per-frame Argus
  testimony post-flip: every body coherent every frame, identical spins
  identically visible, trajectories and omegas exactly equal across the
  ledger flag. **Next on this front: `is_quat_driven` dies — and the 2026-08-19
  survey on merged main found 32 reads across 6 files, not the study's
  13 (the extra 19 live mostly in humanoid_locomotion). G-38 states the
  hazard that orders the work: the flip's publish excludes KINEMATIC,
  so a KINEMATIC FK bone still carries a stale quaternion and the flag
  still selects correctly for exactly those bodies. The deletion is a
  WRITE-CONTRACT change first (every KINEMATIC orientation writer
  maintains BOTH ledgers at its write site, guarded by the fifteen
  locomotion tests), read-site cleanup second, field deletion last.
  Doing it in the other order breaks humanoids visibly.
  **SLICE 1 LANDED 2026-08-19**: the humanoid frame-end sync in
  update_post_physics (`test_humanoid_orientation_coherence`, red at
  1.4496 rad of trapped turn, green at 0.0007). Found and fixed on the
  way: the yaw cascade was a SILENT NO-OP headless (`if (!impl_->engine)
  return;` with the engine used nowhere else in the function), so the
  source-of-truth environment had never run it. Remaining: FK-clip
  writers on jointed rigs, then the read sites, then the field.**

This is the unification's red ladder. The lever lands into it.

## ARGUS (landed 2026-08-19) + the assert-or-waive discipline

`src/core/argus.h`, pure engine module (owner: "not only in physics...
combat etc."), engine accessor beside the ParticleTracer. Declarative
watch-list; per-frame state; relative queries (separation, approach,
spin, peaks, divergence); narration dump; read-only by construction;
zero cost unwatched. Both skills now carry the full-state-narration /
assert-or-waive directive, with the cube-drop ladder as the pattern.
The witness does not perturb: every audited ladder number unchanged.

**Rollout, 2026-08-19.** Ten physics tests re-run one at a time and
audited against the narration discipline; every verdict matched
TEST_AUDIT and none moved. Asserts went from 41 to 122. Argus is wired
into eight of them (n/a by judgement in `test_collision_bounds_rotation`,
which has no ParticleSystem and no time). Two defects the new eyes
caught, both now fixed in the tests: `test_humanoid_knockback` called a
run-wide velocity minimum a strike (a boulder braked by the turtle
downrange reads identically), and `test_refused_momentum_ledger` summed
its off-axis ledger columns and dropped them — the Z column holds
-132.449 kg*m/s of real friction refusal, 99% of the striker's own
vertical momentum deficit, which is the F1 RCA's second door proved to
book in the right direction. Two NEW fronts, measured and recorded in
the audit gaps rather than asserted: `test_pin_gluon_lifecycle`'s anchor
sits exactly one foot width (0.1000 m) to the side of the foot it pins,
because the plant target carries no body-lateral term; and
`test_ramp_race`'s sphere now has its own D2 1.2 assert (peak |omega|
exactly 0.0000 over 240 frames on a 40 degree slope, which a sphere
cannot do — friction acts a full radius from its centre). Argus wishlist
from the rollout: no history query beyond `latest`/`previous` (every
test latches its own minima), no min-separation or closest-approach
query, and `dump()` prints `rotY` only, so a body spinning about Z
narrates as motionless.

## D7 EXECUTED (2026-08-20): ANGULAR_DRAG is dead, the law is derived

Owner order standing since 08-16, done. The constant (and its frame-one
theft, fingerprinted by Argus at 6.0 -> 4.887) is deleted from the
schema; in its place, G-42's derivation: quadratic pressure drag
integrated over the box's faces, tau = -(RHO_AIR*DRAG_CD*L_i*(L_j^4 +
L_k^4)/32) w|w| per body axis, body-frame via the quat truth,
unconditionally stable form, same ambient constants as the linear law
so D8's declarable medium will feed both. Spheres take bounding extents
as a sanctioned INV-21 over-estimate; honest sphere spin drag (skin
friction, viscosity) boarded.

**Proof through mechanism**: test_angular_dissipation GREEN, measured
0.998541 retained vs derived 0.998941 — promoted to CI smoke. Ladder
R2/R3 GREEN by default (flight honest). A stone flywheel barely slows
in air, which is the physics.

**Three findings the honest law surfaced, each charted:**
1. Speculative contact rows brake spin BEFORE the z-threshold
   "touchdown": the flight window must end at first CONTACT EVENT
   (interactions directive), then the lever contracts re-clamp.
2. **G-21 live**: a real tumble crosses the gimbal band every
   revolution and the publish's fold trips the per-frame coherence
   assert — the lossy-view hazard, now measured, needs its ruling.
3. The lane walk grew to 0.99 m under persistent spin: the per-point
   seed compounds longer. The block-solve's bill has come due.

Also derived from the witness: a face-spinning top DIES FAST at contact
(6.0 -> 1.65 in two frames of touch) because face friction is strong —
G-36's own arithmetic. Real tops last by spinning on a POINT; the
long-spin drama belongs to the ICE floor of the materials matrix.

## D9 grows: the owner's materials orders (2026-08-20, post-QA)

Ordered after QA'ing the torque slices, folded here as D9's first
concrete instances, G-first when built:

- **Drop ladder**: floor MATERIALS under the spinning rungs — ice, rock,
  and onward ("what happens... just for fun"). G-41 already names the
  expectation: ice barely converts spin to translation, rock converts
  hard.
- **Ramp**: (1) a PLATE BODY instead of the turtle as the next landing
  case (box-box rows all the way down); (2) an ICE body on the slope;
  (3) the materials play — which is D9's original matrix, now with its
  first three assignments.

Landed already (G-41, rungs R4-R6 of the drop ladder): per-axis spin
touchdown. Measured under the lever: the Z-top brakes in place; the
X-wheel drives itself +0.0525 m along Y; the Y-wheel drives itself
-0.0526 m along X — the same magnitude on mirrored axes, one mechanism,
no axis preferred. Default mode: nothing moves, as audited.

## D9 — THE SURFACE MATRIX: ice, stone, tarmac, sand (owner, 2026-08-19)

The owner, watching the ramp: *"sphere and cube arrive at the same time,
which is a no no unless it was ice surface... we could have ice vs sand
vs tarmac etc."*

**He is right and the arithmetic makes it a discriminator, not a
nicety.** A sliding body accelerates at `g(sin - mu*cos)`; a sphere
rolling without slipping accelerates at `(5/7)g*sin`, INDEPENDENT of mu,
because rolling dissipates nothing at the contact. On this 40 degree
ramp `g*sin = 6.31 m/s2`, and a sphere needs `mu >= (2/7)tan = 0.240`
to roll at all:

| surface | mu | cube | sphere | who wins |
|---|---|---|---|---|
| ice | 0.05 | 5.93 | 5.93 (slides, cannot grip) | **tie, and correct** |
| stone | 0.50 | 2.55 | 4.50 (rolls) | sphere, comfortably |
| tarmac | 0.80 | 0.29 | 4.50 (rolls) | sphere, hugely |
| sand | 1.10 | 0.00 (never moves) | 4.50 (rolls) | sphere, absolutely |

Four surfaces, four different orderings, exactly one of them a tie.
**A test that runs all four cannot be passed by an engine that ignores
either rotation or friction**, which is what makes it worth building.

Measured today on stone: cube 6.356 m, sphere 6.251 m, a tie inside the
noise of two shapes sliding. **The engine is giving the ICE answer on
stone**, because D2 1.2 leaves contacts with no lever arm so the sphere
never spins up and slides like a ball of ice whatever it is made of.

Cheap: one scene, one material parameter, four runs, and every expected
value has closed-form arithmetic to check against. GEDANKEN-34.
**Blocked on D2 1.2** — until a contact can apply torque, three of the
four rows are unreachable and only the ice row would pass.

---

## F5 — the plant anchor pins one foot-width to the SIDE of the foot (2026-08-19, Argus audit)

Found by the assert audit on a GREEN test and deliberately NOT committed
as a failing assert (the prime directive): `test_pin_gluon_lifecycle`
measures the plant anchor at x = 0.000, the body midline, while the
foot it pins stands at x = -0.100. The `FOOT_PLANT` trace agrees at the
source: `target=(0.000, 0.325, 0.055)` — **the plant target carries no
body-lateral term**. Sibling finding, same file: the stance foot drifts
past **0.24 m** from its plant target while plant_blend is 1.0.

Whether a pin gluon is meant to hold a rest offset is a plant-code
question (humanoid locomotion's heel-strike transfer), not one a wiring
test may rule on. Needs the locomotion owner's read before any code.
Evidence printed as `[finding]` lines in the test and recorded in its
audit gaps.

## F6 — the sanitizer lane is red ON MAIN, and it is blocking every merge (2026-08-19)

Not physics, boarded because it gates all of us. The new required lane
`sanitizers-linux` (arrived via #137) fails on main's own commits:
`AddressSanitizer: alloc-dealloc-mismatch (operator new vs free)`
across the KG/ontology tests (`test_kg_ops_parse`,
`test_ontology_extension`, `test_ontology_validator`,
`test_outcome_executor`, more). Something in KG core allocates with
`new` and frees with `free` — a real defect the lane caught on day one.
PR #146 merged by owner admin override with every other lane green;
the ruling stands recorded there. **Owner: the KG/rules lane owns the
fix, and until it lands every PR needs the same override.**

## OWNER RULING

| # | Decision | Blocks |
|---|---|---|
| R1 | **The 7 authority questions** — what a struck driven limb does (absorb / break authority / threshold, with the threshold option flagged INV-10-hostile); whether partial ragdoll must be expressible; per-particle vs per-entity authority (note: `EntityPhysicalState::apply_solver_authority` is built, tested bidirectionally, and has **zero callers**); rename the enum?; rename or derive `is_quat_driven`?; flip sequencing vs D4; is the interaction-profile filter under this law or scoped out | All of D1's slices |
| R2 | **The 6 rotation questions** — orientation truth (deferred twice now); gyroscopic scope; torsion timing; friction-basis ordering; INV-16's wording; the ladder's real state | All of D2's slices |
| R3 | **The WAKE_RESOLVER flip** | INV-31 goes active; the machine's default reaches 5/10. Path is known: C3 + the `update_rest_state` KINEMATIC guard + re-baseline two tests whose greens encode the old quirk + flip. D4 is the residual risk you accept or clear first. |
| R4 | **The seed→world sanitisation question** | `at_logogenesis_creation`, the last sweep mole: the app materialises `tree_height=999` as an 80 m crown and the gate's range door refuses it. Should the creation pipeline clamp seed-derived values to schema bounds, and should the test then assert the refusal? |
| ~~R5~~ | **RULED 2026-08-15: compute both, the HIERARCHY declares which wins.** Two policies, `SPECIALISE` (subtype outranks, `super`-style) and `ENCAPSULATE` (filters outrank, today's behaviour), declared per hierarchy in the schema instead of fixed in the scorer. INV-29's shape applied to routing. | Residual owed: the default policy for a hierarchy that declares nothing, and whether the annotation sits on the root type or every type. Blocks the specificity scorer slice only. |
| ~~R7~~ | **DISSOLVED 2026-08-16, not answered.** Owner ruled the quaternion is the only orientation truth and Euler is a published view for every body. There is no representation half left to rehome, so the flag is deleted rather than renamed and the original instruction to fold it into `solver_mode` stands unqualified. This is D2 §6.1 Option A, deferred twice. **It is a PREREQUISITE of the rotation campaign, not a slice of it**: D2's slices write angular code, and writing that against a dual-truth representation means writing branch-on-flag code that must then be unwound. Six Gedankenexperimente recorded first (19-24). | Unblocks D1, which unblocks F3. |
| R6 | **The physics-default vocabulary is short** | The `else` branch (below) names outcomes from the measured floor, but `bounce` needs F4's absent restitution, `roll` needs angular state at the seam, `topple` is unimplemented. Ship the default with the words the engine can actually measure, or block it on F4? |

---

## PARKED (real, understood, not now)

- **Worldgen pins** — tree trunk roots, crown roots, root plates, planet core, 6000+ crust stones (`KINEMATIC_AUDIT.md` bucket B). One-way KINEMATIC with no release. *Parked until D3*: releasing them without the substrate would drop the world on the floor.
- **Engine-wide silent-fallback destruction** — physics is swept; the owner's order was "repeat this across the engine code entirely."
- **Malleus H2 / H3** — relation domain/range vacuous; generation reproducibility.
- **Stale-bond lifecycle** — `ParticleSystem::clear_particles()` never clears bonds; 28 stale bonds re-bound (fallback inventory, doored not fixed).
- **INV-29 residual** — 31 remaining magic-number sites + malleus H4 materials single-source (task #51).
- **Sweep audit debt: 30 unaudited tests from the rules/logovger lane** —
  arrived via main (#132, #133 and earlier merges). Until they carry
  audit rows the sweep verdict cannot read zero and the gate is
  degraded: a real regression in that lane would drown in the same
  bucket. The classification belongs to whoever owns those tests (the
  logosphere2 session); guessing what they prove from here would be
  worse than the debt. Physics-lane tests are fully audited as of
  2026-08-19.
- **GPU campaign** — issues #81-#87, the render session's lane.

---

## The recommended order, and why

**1. C1-C4 first, together.** They are four hours of honest plumbing that make every later measurement trustworthy. C1 especially: a test that cannot form a contact has been producing "findings" — mine included. C3 is a landmine under the flip. C4 is a law the engine already promised.

**2. Then D1 slices S0-S2** (harness truth, name the three questions, kill the inline opinions). S0-S2 are specified as **bit-identical** to today's behavior, so they are safe to land before any ruling, and S2 unblocks R3.

**3. Then R3, the flip** — with D4 either cleared or accepted in writing.

**4. D3's study runs in parallel** with all of the above, because it is pure design and blocks the largest parked item.

**5. D2 (rotation) after D1 owns the predicate** — otherwise the angular side grows its fifth immovability formula and we redo it.

The rest waits. Two rulings (R1, R2) can be taken whenever there is appetite; the questions are written and evidenced, and until they are answered their slices should not start.

---

## What changed today, for the record

Closed: the rest damper eradicated (INV-19), `ParticleSolverMode::STATIC` eradicated, the humanoid hips pin fixed (ragdoll works, 1.231 m in 0.5 s against an analytic 1.226), refused momentum booked instead of dropped, the KG gate's long tail swept, `test_grass_yields` recovered as a trophy of the damper removal.

Withdrawn: the "manifold overshoot" (defect 2) — the manifold was always correct, and rung R5 of the analytic ladder now proves it at 0.54% momentum error. The "boulder passes through the chest" evidence — a harness gap, not physics.

Opened: this board.

---

## 2026-08-15 — the doctrine question is ruled: escalation, not exception

RULED (`LEDGER.md`, same date): resolution is a **four-rung DIKW
ladder**, not one mechanism. Rung 0 the physics fact, rung 1 the
compiled route table, rung 2 the ontology's type lattice, rung 3
escalation (an LLM is admissible there). It reconciles with
`RULE_LANGUAGE.md:945-947` because the ruling's objection is to a
resolver that silently picks, and a ladder that orders deterministically
and escalates only what it provably cannot order is not that resolver.

Rung 3's contract is what keeps INV-27: it never runs inside a frame,
its output is a ROUTE and never an outcome, so the same conflict
escalates exactly once and runtime stays a compiled lookup. That is the
owner's required feedback loop, and it makes the investment permanent
instead of per-frame.

**Rung 2 repairs a defect the studies did not catch.** The specificity
key was purely syntactic, so two routes with the same address shape
naming a subtype and its supertype tied on every key and produced a
load-time error, when one is strictly more specific by inheritance.
Fixed as key 4 in router design §3.3.3; the machinery already exists
(`OntologyRegistry::isSubtypeOf` / `ancestorsOf`).

**And a gap that limits it:** `facets` do not inherit, explicitly and by
decision (`ontology_registry.h:50-51`). Rung 2 orders routes addressing
entity TYPES and buys nothing for facet filters.

**Measured, against the owner's "95% deterministic" expectation:** the
deterministic rungs missed **0 of 10** Gedankenexperimente. Six are
settled; the four open ones are open for reasons ordering cannot touch
(GEDANKEN-4 is F3, GEDANKEN-7 needs F4's absent restitution,
GEDANKEN-3 and 5 need owner rulings on how far a route's authority
reaches). Ten hand-built cases are a small, self-selected corpus. The
sequencing that follows: **design rung 3 now, build it last.**

New ruling owed: **R5** (where subsumption depth sits in the measure).

---

## 2026-08-15 (later) — R5 ruled, and the `else` branch is physics

**Correction taken first:** the ontology is read at LOAD, never in a
frame. The ladder as first written implied rungs firing in sequence at
runtime. Rungs 1 and 2 are compile steps that sort the table once; a
frame walks a sorted vector. Rung 3's output is a route, so it is spent
at load too. The table is armed before the world runs. Router design
3.3.4 corrected.

**R5 RULED: compute both, and the hierarchy declares which wins.** No
global answer, because hierarchies mean different things: some are
specialisation ladders where a subtype should override its parent, some
are encapsulation boundaries where a filter on the parent has no
business reaching inside. Two declared policies, `SPECIALISE` and
`ENCAPSULATE`, owned by whoever authored the hierarchy. It does not
dissolve the set-overlap finding; it converts an underivable fact into
a declared input, which is INV-29's shape.

**NEW FRONT, D6: the `else` branch is a physics default, and it is
counted.** When no route claims an occurrence the engine emits the
PHYSICS RESPONSE, derived from the solve and the materials, named from
the measured floor, with no game meaning attached. GEDANKEN-1's
identity case made non-empty: motion stays bit-identical, the event
gains a name. Every fall-through increments a counter keyed by
(occurrence kind, type pair), so authoring effort can follow measured
frequency instead of imagination. Owner's framing: the
Gedankenexperimente we can think of are the defaults we preload, and
the tracker finds the ones we failed to think of.

D6 is cheap, independent of the router's election machinery, and
useful before Kamaji exists: the counter can be built and left running
to gather the distribution while the rest is designed. Its vocabulary
limit is boarded as R6.
