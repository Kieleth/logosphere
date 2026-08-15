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
| C5 | **Machine stages S5+ choreography** | `test_rube_goldberg_machine` halts at S5 with the resolver on (5/10); the struck box stops where friction says it stops, short of the bridge | Scene design, not physics. The ratchet is honest at 3 (default) / 5 (resolver). |
| C6 | **INV-17..28 have no proving tests** | sweep: "COVERAGE HOLES (no proving test): INV-16..28, INV-5, INV-6"; task #45 | Writing provers cannot break anything. INV-6 (no gravity assumptions) has *never* been witnessed on a wall, a ceiling, or in zero-g. |
| C7 | **Energy ledger has no dissipation bucket** | INV-19's own commitment; task #44 | Bookkeeping only. Needed before any future damping can be called "modelled". |

---

## F1 — NEW FRONT: momentum destroyed in a strike on a sleeping driven body

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

## NEEDS DESIGN

| # | Front | Study | State |
|---|---|---|---|
| D1 | **Motion authority unification** | `MOTION_AUTHORITY_DESIGN.md` | **Study complete.** One state with an opaque holder id, three predicates, a static ratchet making a fourth reader uncompilable; `apply_pair_impulse` books its own refusals; `:608` deleted; `is_quat_driven` renamed not folded; the seven owner-reads go to zero so task #43 dissolves. Ladder R0-R8, slices S0-S7. **7 questions owed (see below).** |
| D2 | **Rotation campaign** | `ROTATION_CAMPAIGN_DESIGN.md` | **Study complete.** The full-Jacobian row already exists on `feat/joint-block-solver` (`4c92518`, stalls −96%) and both reasons it was parked have since landed. Slices S0-S10. **6 questions owed.** Collides with D1: both specify the same `inv_inertia_momentum` predicate — whichever lands first must own it, or the angular side grows a fifth immovability formula. |
| D3 | **The substrate: model the effect of what we don't simulate** | **none yet — owed** | The owner's direction: gravity is the effect of unmodelled mass, ground support is that same substrate pushing back, floating is an exemption from the field. Retires the entire "pin it so it doesn't fall over" class, subsumes task #48 (gravity as a lever), and is the honest replacement for the worldgen pins in P1. Needs a study before any code. |
| D4 | **The unexplained 500:1 / 1044:1 bond tear** | KINEMATIC audit, residual risk | Established: the resolver did not cause it and the old pricing quirk concealed it (`SLEEP_LAW_OFF=1` with the resolver off reproduces it to three decimals). Unexplained: why a bond between unequal masses amplifies 1.2 m/s into 1.79 m/s. An open INV-17 suspect that the flip makes visible in CI. |
| D5 | **Order-capture slices 2+** | `PHYSICS_PIPELINE_SEQUENCE.md`, task #52 | Slice 1 landed (23 nodes, six seed edges, three carrier hazards). The load-bearing deliverable — the producer/consumer hazard scan — is still owed, and every solve-loop change above compounds the need. |

---

## OWNER RULING

| # | Decision | Blocks |
|---|---|---|
| R1 | **The 7 authority questions** — what a struck driven limb does (absorb / break authority / threshold, with the threshold option flagged INV-10-hostile); whether partial ragdoll must be expressible; per-particle vs per-entity authority (note: `EntityPhysicalState::apply_solver_authority` is built, tested bidirectionally, and has **zero callers**); rename the enum?; rename or derive `is_quat_driven`?; flip sequencing vs D4; is the interaction-profile filter under this law or scoped out | All of D1's slices |
| R2 | **The 6 rotation questions** — orientation truth (deferred twice now); gyroscopic scope; torsion timing; friction-basis ordering; INV-16's wording; the ladder's real state | All of D2's slices |
| R3 | **The WAKE_RESOLVER flip** | INV-31 goes active; the machine's default reaches 5/10. Path is known: C3 + the `update_rest_state` KINEMATIC guard + re-baseline two tests whose greens encode the old quirk + flip. D4 is the residual risk you accept or clear first. |
| R4 | **The seed→world sanitisation question** | `at_logogenesis_creation`, the last sweep mole: the app materialises `tree_height=999` as an 80 m crown and the gate's range door refuses it. Should the creation pipeline clamp seed-derived values to schema bounds, and should the test then assert the refusal? |

---

## PARKED (real, understood, not now)

- **Worldgen pins** — tree trunk roots, crown roots, root plates, planet core, 6000+ crust stones (`KINEMATIC_AUDIT.md` bucket B). One-way KINEMATIC with no release. *Parked until D3*: releasing them without the substrate would drop the world on the floor.
- **Engine-wide silent-fallback destruction** — physics is swept; the owner's order was "repeat this across the engine code entirely."
- **Malleus H2 / H3** — relation domain/range vacuous; generation reproducibility.
- **Stale-bond lifecycle** — `ParticleSystem::clear_particles()` never clears bonds; 28 stale bonds re-bound (fallback inventory, doored not fixed).
- **INV-29 residual** — 31 remaining magic-number sites + malleus H4 materials single-source (task #51).
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

Withdrawn: the "manifold overshoot" (defect 2) — the manifold was always correct, and R5 now proves it at 0.54% momentum error. The "boulder passes through the chest" evidence — a harness gap, not physics.

Opened: this board.
