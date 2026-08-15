# F1 — where the momentum goes when a boulder hits a person

RCA and design. Branch `feat/rube-goldberg-machine`, HEAD `0c4f49f`. No
source was modified; the tree is as it was found. Every claim carries a
`file:line` or a measurement taken in this session. Measurements are
labelled MEASURED, code-only conclusions ESTABLISHED FROM CODE, and
anything else INFERRED. `src/platform/platform_macos.mm` was neither
read nor touched.

Instruments: four standalone probes linked against
`build-release/liblogosphere_{dynamics,physics,core}.a`, reproducing
`tests/test_humanoid_knockback.cpp`'s fixture exactly and accounting
`Σ m·vx` over **every body in the world** at each phase boundary. The
physics decision tracer was driven by calling
`logosphere::phystrace::set_level()` directly from the probe, which works
in a standalone test where `LOGOSPHERE_PHYS_TRACE` does not
(`init_from_env` is called only from `src/core/engine.cpp`).

---

## 0. Bad news first: five corrections to the brief

**C-1 (MEASURED). The chest is not immovable and it is not refused. It
moves.** With the locomotion writer not running, the boulder's strike is
delivered in full: the chest reaches 6.25 m/s, travels **1.593 m in 20
frames and 8.735 m in 90**, and world momentum is conserved — `Σ m·vx`
goes 1280.0 → 1275.6 over the strike window against an air-drag baseline
of −4.4. The chest measures `is_at_rest = 0` at every frame from 0 to 15,
`solver_mode = DYNAMIC`, and `inv_mass_momentum` returns 1/15.625.

**C-2 (MEASURED). "boulder vx 8.00 → −0.00" is the boulder landing, not
the strike.** At frame 89 the boulder sits at `z = 0.203` (on the
turtle), `vx = 0.000`, `is_at_rest = 1`, at `x = 3.933` — four metres
downrange, with the chest at `x = 8.735` still doing 6.077 m/s. The
strike itself takes the boulder 8.00 → 6.19 (writer off) or 8.00 → 4.11
(writer on). `tests/test_humanoid_knockback.cpp:211-213` still reports a
**velocity minimum over 90 frames**, which is exactly the confounded
statistic `MOTION_AUTHORITY_DESIGN.md` Correction 2 flagged. C1 landed
the BVH; it did not land the readout. Every "it HIT" claim from this
file remains unsafe.

**C-3 (MEASURED). The standing four-link chain is not what destroys the
momentum.** Links 1 and 2 hold (`physics_system_v4.cpp:617` skips the
chest's gravity; its velocity stays zero). Link 3 holds in general but is
**irrelevant here**: the strike lands at frames 3–7 and the chest is
never asleep in that window. Link 4 is **false**: measured, the chest
gains 89–110 kg·m/s per frame during the strike. The isolation experiment
(§3) closes it: a plain box that *is* forced asleep behaves
bit-identically to the awake control, because the pre-solve wake gate
wakes it (0.91 × 8 m/s ≫ `WAKE_TRANSFER_SPEED`).

**C-4 (MEASURED). The destruction is real — 623 kg·m/s, 49 % of the
world — but it is not in the physics engine, and it is not INV-3.** Its
site is `src/animation/humanoid_locomotion.cpp:4440-4442`. INV-3 governs
energy inside the solver; INV-7 scopes its door to the solver. **No
active invariant forbids what that line does.**

**C-5. `tests/invariants/TEST_AUDIT.jsonl`'s entry for
`test_humanoid_knockback` still records the retracted claim** that the
boulder "passes through a humanoid chest at a constant 7.99 m/s" and that
`is_quat_driven` "exempts it from gravity and contact response entirely".
The contact-response half is now measurably false. Correction owed in
whichever commit consumes this document.

---

## 1. The defect, reproduced

```
./build-release/test_humanoid_knockback
  [measure] right after registration: chest mode=0 owner=1 | hips mode=1 owner=1
  [measure] chest mass=15.625 kg quat_driven=1
  [measure] boulder vx 8.00 -> -0.00 (it HIT the humanoid)
  [FAIL] R1: a strike on a KINEMATIC body is BOOKED, not dropped
  [FAIL] R1: and it points along the strike (+x)
  [measure] hips x: 0.000 -> 0.000 (moved +0.000 m) | chest x=+0.000
  [FAIL] R2: the struck humanoid is displaced by the hit
  [FAIL] R3: struck from the west, it staggers EAST
  KNOCKBACK INCOMPLETE (4 failures)
```

Masses in the fixture (MEASURED): hips 8.000, abdomen 8.000, chest
15.625, neck 1.000, head 8.000, twelve limb segments 1.728 each →
**61.361 kg** of humanoid, struck by a **160.0 kg** boulder at 8 m/s
(`Px = 1280.0`).

R1 runs `physics.update` alone; R2/R3 run `humanoid.update_pre_physics` →
`physics.update` → `humanoid.update_post_physics`. That difference is the
whole defect.

---

## 2. The measurement that decides it

Eight configurations of the real knockback scene, 20 frames (the boulder
is still airborne throughout — verified `z ≥ 1.15` at frame 20 — so no
turtle-landing friction is in these numbers). `Σ m·vx` is taken over
every body at every phase boundary. Air-drag baseline over the window:
**−4.4**.

| | writer | friction | Px end | lost **in physics** | lost **in the writer** | booked +x | chest x |
|---|---|---|---|---|---|---|---|
| A | off | as spawned | 1229.2 | −50.8 | 0 | **0.0** | +1.550 |
| **B** | **on** | as spawned | **657.3** | −123.6 | **−499.1** | **0.0** | **+0.000** |
| C | on | all zero | 803.9 | −2.0 | −474.1 | 0.0 | +0.000 |
| **D** | **off** | all zero | **1275.6** | **−4.4** | **0** | **0.0** | **+1.593** |
| E | on | hips forced DYNAMIC | 751.1 | −34.1 | −494.8 | 0.0 | +0.798 |
| F | off | hips forced DYNAMIC | 1275.0 | −5.0 | 0 | 0.0 | +1.587 |
| G | on | humanoid zero | 803.9 | −2.0 | −474.1 | 0.0 | +0.000 |
| H | off | humanoid zero | 1275.6 | −4.4 | 0 | 0.0 | +1.593 |

Read three pairs and the RCA is done:

- **D vs B.** Same collision, same solver, same flags. Writer off:
  momentum conserved to 0.34 %. Writer on: 49 % of the world's momentum
  gone. **The writer is the defect.**
- **C/G vs D/H.** With friction removed entirely, physics loses nothing
  (−2.0) and the writer still eats −474.1. **The writer's loss is not a
  friction artefact.**
- **A vs F** (−50.8 vs −5.0) and **B vs E** (−123.6 vs −34.1). Freeing
  the hips removes 45.8 / 89.5 kg·m/s of in-physics loss. That is a
  **second, smaller sink**: friction against the KINEMATIC hips (§4).

Per-frame, writer on (`Σ m·Δvx` by body, physics phase then writer
phase):

```
f5  PHYSICS   chest +109.5  head +53.4                 BOULDER -163.3
    POST_ANIM chest -109.5  head -53.4
f6  PHYSICS   chest  +89.5  neck +5.8  head +46.3  ra1 +9.9  BOULDER -151.7
    POST_ANIM chest  -89.5  neck -5.8  head -46.3  ra1 -9.9
```

The solver delivers correctly. The writer deletes it, to the decimal, in
the same frame.

---

## 3. The isolation experiment: which property is load-bearing?

Two plain boxes, no humanoid, no gluons, no writer, friction forced to
zero. Target is 15.625 kg (the chest's mass exactly), boulder 160 kg at
8 m/s. Only the named property varies. Run twice: **early** (frame 2,
nothing has had time to sleep) and **late** (frame 40, everything that
can sleep, has). MEASURED:

| target | Px | target moved | booked +x |
|---|---|---|---|
| control: `PHYSICS`, quat = 0 | 1280.0 → 1272.1 | +4.469 m | 0.0 |
| `PHYSICS`, quat = 1 | 1280.0 → 1272.1 | +4.469 m | 0.0 |
| **`DYNAMICS`, quat = 1 — the chest's exact flags** | **1280.0 → 1272.1** | **+4.469 m** | **0.0** |
| `DYNAMICS`, quat = 0 | 1280.0 → 1272.1 | +4.469 m | 0.0 |
| forced `is_at_rest` before the strike | 1280.0 → 1272.1 | +4.469 m | 0.0 |
| `KINEMATIC` | 1280.0 → −80.2 | +0.000 m | −604.3 |

Identical in every digit for all five DYNAMIC variants, early and late.
The 0.6 % is air drag.

**The load-bearing property is not `is_quat_driven`, not `ParticleOwner`,
and not sleep.** Each of those was the accused; each is exonerated by a
twin scene that differs in nothing else. The only property that stops a
body is `solver_mode = KINEMATIC` — which is the honest mechanism, is
declared, and does book (partially — §5).

---

## 4. The causal chain, link by link

### Link 1 — the contact is real (ESTABLISHED, tracer level 5, frame 7)

```
268 rows  a=1(abdomen) b=17(boulder)  why=contact
 89 rows  a=2(chest)   b=17           why=contact
 49 rows  a=3(neck)    b=17           why=contact
 13 rows  a=12(la1)    b=17           why=contact
  4 rows  a=13(la2)    b=17           why=contact
```

Zero `BOTH_IMMOVABLE`. The C1 BVH fix works.

### Link 2 — the solver delivers (MEASURED)

`inv_mass_momentum` (`physics_system_v4.cpp:501-506`) returns 1/15.625
for the chest: not KINEMATIC, not massless, not at rest. The chest
receives 89–110 kg·m/s per frame. Config D conserves the whole strike.

### Link 3 — the writer overwrites it (ESTABLISHED FROM CODE + MEASURED)

`HumanoidLocomotion::update_post_physics`
(`src/animation/humanoid_locomotion.cpp:198`) calls `update_locomotion`
at `:1401`, which ends:

```cpp
// src/animation/humanoid_locomotion.cpp:4392
float vx = hips.vx;                       // the ONLY velocity it reads
...
// :4436-4442
float new_vx = vx + dvx;
float new_vy = vy + dvy;

// 8. Apply to ALL entity particles (key insight!)
for (unsigned int id : parts.all_particle_indices) {
    particles_view[id].vx = new_vx;
    particles_view[id].vy = new_vy;
}
```

One scalar, derived from the hips alone, is broadcast onto all seventeen
particles. Whatever the solver just wrote into the chest, the head, the
neck and the arms is discarded. Positions are separately snapped back to
`hips + rest_offset` at `:5449-5451` ("Hard position correction (no
spring - immediate snap)").

**The hips are KINEMATIC**, so `hips.vx` is permanently 0, and
`parts.target_vx` is 0 for an idle humanoid. Therefore `dvx = 0` and
`new_vx = 0`: the erasure is total and instantaneous, not a deceleration.
The acceleration clamp at `:4426-4431` never engages because there is
nothing to clamp — the reference is the hips, not the particle.

The broadcast signature is unmistakable (MEASURED, frame 6, per-particle
`vx` immediately before and after the writer):

```
KINEMATIC hips:  0.00 0.00 5.73 5.78 5.79 0.00×10 0.01 0.00 5.73 0.00
              -> 0.00 0.00 0.00 0.00 0.00 0.00 ... 0.00 0.00 0.00 0.00

hips DYNAMIC:    0.47 0.52 5.71 5.76 5.77 0.18 0.18 0.47 0.18 ... 5.71 0.18
              -> 0.34 0.34 0.34 0.34 0.34 0.34 0.34 0.34 0.34 ... 0.34 0.34
```

Seventeen different velocities in, one value out. That is `:4441`.

This runs for **every registered, standing humanoid, every frame** — the
only guard on the path is `if (parts.is_lying_down) continue;`. It is not
a fixture quirk.

### Link 4 — nothing is booked, and nothing could be (ESTABLISHED)

`record_refused_impulse` has exactly two call sites in the entire tree,
both inside the normal-impulse apply: `physics_system_v4.cpp:3373` and
`:3385`. The writer does not call it, and **could not honestly**: the
chest refused nothing. The solver delivered; a game-layer writer then
overwrote a body it does not hold. The refusal ledger is the wrong
instrument for this loss, and the right one does not exist.

MEASURED over every configuration and every frame: the only bookings in
the whole run are on the hips and are pure −Z (−74.34, −62.07, −39.48,
−28.26, −26.30, −13.87 at frames 7–12) — the structural weight of the
thighs resting on the KINEMATIC hips, already noted on the board.
**Booked +x is 0.00 everywhere, always.**

### The chain, in one paragraph

The boulder's momentum is delivered correctly by the solver into the
chest, head, neck and arms. In the same frame,
`HumanoidLocomotion::update_locomotion` broadcasts a single hips-derived
velocity — zero, because the hips are KINEMATIC and cannot receive
velocity — onto all seventeen particles, and `maintain_entity_shape`
snaps their positions back to the hips. 499 kg·m/s (39 % of the world)
is deleted per 20-frame strike, outside the solver, through no door, and
booked nowhere. A further ~90 kg·m/s is destroyed inside the solver by
friction against the KINEMATIC hips, also unbooked. Total: 623 kg·m/s,
49 %.

---

## 5. Two further unbooked doors, found on the way

Both are real, both are separable from F1, both belong on the board.

**U-1. The friction block does not book.** `physics_system_v4.cpp:3447-3550`
applies four impulses (two tangents × two bodies) through
`inv_ma`/`inv_mb` and contains no `record_refused_impulse`. When one side
is immovable it is charged nothing and nothing is recorded. C3
(2026-08-14) removed the redundant guard on top of `inv_mb`; it did not
add booking. MEASURED cost in this scene: 45.8 kg·m/s (writer off) to
89.5 kg·m/s (writer on) per 20-frame window, all of it friction against
the KINEMATIC hips.

**U-2. The one door that does book, books 2.5 % of what it refuses.**
Clean two-body scene: KINEMATIC target, airborne (no turtle contact),
frictionless, nothing else in the world. MEASURED over 14 frames:

```
Px 1280.0 -> -77.8   world lost 1357.8   booked +x = +33.6   (2.5%)
tracer level 5:  419 row_impulse records,  Σ impulse = 33.5768
```

The ledger is **exactly** the sum of the iteration-loop deltas
(33.5768 vs 33.6), so the booking is complete for the door it sits in and
for no other. ESTABLISHED FROM CODE: the warm-start apply at
`physics_system_v4.cpp:2801-2810` writes velocity through
`inv_ma`/`inv_mb` with no booking, and `:2829` seeds
`c.accumulated_impulse` with that warm impulse — so the iteration deltas
exclude the warm-start share **by construction**. INFERRED, not measured
to the kg·m/s: warm start is the dominant unbooked path for the remaining
1324 kg·m/s.

This matters beyond bookkeeping: D1's R4 and S7 both assume the drain
carries the truth. Today a writer that drained the ledger would receive
2.5 % of the strike. **A drain that receives 2.5 % of the truth is worse
than no drain, because it looks like it works.**

---

## 6. The law being violated

> **A body whose motion the solver owns may not have its velocity or
> position overwritten by any other writer. A writer that takes a body's
> motion takes its authority first; the momentum the solver then declines
> to deliver is booked to that writer and handed back, never deleted.**

**Is an existing invariant enough? No.** Stated plainly:

- **INV-7** ("every velocity write passes equal-and-opposite through one
  door") reads, literally, as if it covers `:4441`. But its `mechanism`
  field names `inv_mass_momentum + apply_pair_impulse in
  physics_system_v4.cpp`, and no prover in the tree can see
  `src/animation/`. It is a solver-internal law wearing engine-wide
  words.
- **INV-15** is the mirror (physics blind to game). Nothing states the
  converse — that the game layer may not silently overwrite solved
  physics state.
- **INV-3** is about energy inside the solver, and 623 kg·m/s of
  *momentum* deleted by an animation loop is outside its mechanism
  (`energy ledger`, `equal-and-opposite momentum door`).
- **INV-32 as drafted** in `MOTION_AUTHORITY_DESIGN.md` §2 covers momentum
  "an authority **refuses**". The chest refuses nothing. INV-32 does not
  yet cover a writer overwriting a body that **accepted**.

**A clause is owed.** Recommended as INV-32 clause 7 (it is D1's law and
this is D1's evidence), with INV-7 gaining an explicit scope note saying
it governs the solver's own writes:

> *No writer outside the solver writes `v` or `x` on a body whose
> authority it does not hold. A held body receives no momentum and its
> declined share is booked to its holder; an unheld body is the solver's
> and is left alone. A writer's presence is invisible to `Σ m·v`.*

---

## 7. The red-first test

`tests/test_motion_authority.cpp`, new rung. House style follows
`tests/test_sleep_wake_resolver.cpp`: **twin scene plus a physical
anchor, no expected value invented by the test.**

**R9 — the writer is invisible to momentum.**

Two runs of the same strike on the same rig.

- **Scene A (the budget).** Humanoid registered, boulder thrown, the
  locomotion writer **never run**. This is not the desired behaviour —
  the torso tears off the KINEMATIC hips and flies (chest at +8.735 m by
  frame 90). It is the *momentum truth*, exactly as
  `test_sleep_wake_resolver`'s awake twin is a budget and not a target.
- **Scene B (the subject).** Identical, with the writer running.

Snapshot at frame 20, guarded by `boulder.z > 0.5` so the reading is the
strike and not a turtle landing.

| # | Predicate | Today (MEASURED) |
|---|---|---|
| P1 | `\|Px_B − Px_A\| ≤ \|Px_A(0) − Px_A(20)\|` — the writer may cost no more than the world's own drag | 618 vs a budget of 4.4. **RED, off by 140×** |
| P2 | `\|Σ_frames (ΔPx_writer + handed_back)\| ≤` same budget — every kg·m/s the writer removes is drained in the frame it removes it | writer removes 499.1, hands back 0.0. **RED** |
| P3 | humanoid `Σ m·vx` in B equals humanoid `Σ m·vx` in A, same budget — the *rig*, not just the world, carries what the solver gave it | A: the rig carries it. B: 0.0. **RED** |
| P4 | sign anchor: humanoid `Σ m·vx > 0` in B (struck from the west, momentum points east) | 0.0. **RED** |

P3 is the rung that cannot be satisfied by "book it and drop it": the
momentum must still be **on the rig**, not merely accounted for. P1
alone would pass if the writer never ran; P3 makes the test demand a
working writer.

No tolerance is invented anywhere: the budget is Scene A's own drag loss,
the target is Scene A's own result. The test fails today and can only
pass when the writer stops deleting and starts receiving.

**Also owed as its own rung, from §5:** *R4' — the door books what it
refuses.* Two bodies, one KINEMATIC, airborne, frictionless, drained
every frame: `|booked| / |striker Δp| ≥ 0.99`. Today: **0.025**. This
must be green before S7 can trust the drain.

---

## 8. The mechanism

The defect class is: *an external writer sets state on a body it does not
hold, discarding the solve*. The generic cure is D1's, applied to the
write side rather than the read side.

**M1 — the writer takes authority for every body it writes.**
Today `humanoid_locomotion` stamps only the hips (`solver_mode =
KINEMATIC`); the other sixteen particles are `DYNAMIC` and are written
anyway. Under D1 §3.5's *FK-written* regime the whole rig takes
`MotionAuthority::EXTERNAL` with `authority_holder = <the locomotion
writer>` for as long as the clip drives it. Then:

- `inv_mass_momentum` returns 0 for all seventeen, so the solver never
  delivers momentum that is about to be deleted;
- `apply_pair_impulse` (D1 §3.4) books the declined share to the holder;
- nothing is destroyed, because nothing was ever delivered into a body
  the writer was going to overwrite.

Sites: `humanoid_locomotion.cpp:1547` (take, at registration / clip
start) and the five existing release sites `:1873`, `:1932`, `:2017`,
`:2486`, `:2572`. The broadcast at `:4440-4442` and the snap at `:5449`
are then legal writes to bodies the writer holds — unchanged code, honest
reason.

**M2 — the handback is keyed to the holder and the writer drains it.**
`take_refused_impulse` (`physics_system_v4.cpp:574`) is per-particle and
polled by nobody: MEASURED, its only caller in the tree is the knockback
test itself. D1's S5 makes the ledger holder-keyed. The locomotion writer
drains its own book each frame; what a push *means* is game policy under
CLAUDE.md's boundary, and it is D1 Q1's decision, not this document's.

**M3 — the ratchet that makes the class unwritable.**
`tests/test_inv32_writer_gate.cpp`, same shape as
`test_inv15_owner_blindness` and `test_inv29_constants_gate`: count every
direct `.vx/.vy/.vz/.x/.y/.z` assignment to a `Particle` outside the
physics TUs, pin the current set, and require each writing TU to declare
the authority it holds. This is the **converse of INV-15's gate** — INV-15
stops physics reading the game, this stops the game writing over physics
— and it is the only part of the fix that cannot be undone by the next
person who needs a limb to hold still.

**M4 — the two doors of §5 start booking.** `:3447-3550` (friction, four
applies) and `:2801-2810` (warm start) call `record_refused_impulse` on
the side the predicate zeroed. This is the same one-line cure already
proven at `:3373`. It needs no holder and no lever and is independently
landable.

**Lever.** `MOTION_AUTHORITY_HANDBACK=1`, default off. Off: the rig
stamps only the hips and `:4441` stands — bit-identical to today. On:
the rig takes authority for all seventeen and the solver books instead of
delivering.

**A benefit worth naming.** Under M1, the FK-written bones are `EXTERNAL`
by declaration, so `inv_mass_momentum` already refuses their gravity —
which is what `physics_system_v4.cpp:617` does today by reading a
representation flag. **`:617`'s humanoid case dies for free**, and D1's
S3 shrinks to the genuinely painful part (physics-driven bones holding
their pose under their own weight, the INV-13 debt). This slice does not
collide with S3; it de-risks it.

---

## 9. What must NOT be done

1. **Do not blend the solved velocity into `new_vx` at `:4436`.** It
   would make the humanoid's response to a strike depend on the accident
   that its velocity is broadcast from the hips, it is unbookable, and it
   gives a different answer for a body struck while walking
   (`target_vx ≠ 0`) than for one struck while idle. An if-statement edge
   fix in physics costume.
2. **Do not exempt struck particles from the broadcast** ("skip the
   overwrite for any particle with a contact event this frame"). That is
   per-particle special-casing inside a write loop, and it dismembers the
   rig: the chest leaves, the abdomen stays.
3. **Do not make the hips DYNAMIC and call it fixed.** MEASURED (config
   E): 494.8 kg·m/s still destroyed with the writer on. It moves the
   number, not the class, and it gives up the pin locomotion needs.
4. **Do not add damping, a spring, or a "knockback absorber".** INV-19.
   There is no dissipation process here. The momentum is not converted to
   heat; it is deleted by an assignment.
5. **Do not book the writer's loss through `record_refused_impulse`.** It
   is not a refusal — the solver delivered. Putting it in that ledger
   would make the one book that has to stay trustworthy carry a lie, and
   the ledger is already unable to distinguish the humanoid's own
   structural −14.23 N·s from an external shove. The handback needs its
   own name and its own reason field.
6. **Do not fix it anywhere in `src/core/physics_*`.** The write is in
   `src/animation/`. A physics-side guard would be the solver reading a
   game-layer fact — INV-15, straight through the front door.

---

## 10. Sequencing

This is **not a new front**. It is D1's ladder, and it is the first slice
with a *consumer* rather than a *reader*.

**It becomes D1 slice S5b — "the FK rig holds what it writes, and drains
its book."**

**Must precede it:**
- **S1** (the three predicates exist and are the only readers). Without
  the door there is nothing to refuse through.
- **S5** (the holder + writer registry). Without a holder the book has no
  destination; today it is per-particle and polled by nobody.
- **M4 / R4'** (§5): the friction and warm-start doors must book before
  any drain can be trusted. Measured at 2.5 % fidelity today, this is the
  hard prerequisite, and it is independently landable ahead of everything
  else.

**Does not depend on:**
- **S2, S3, S4** — all invisible to this defect.
- **The WAKE_RESOLVER flip / R3** — MEASURED irrelevant: the chest is
  never asleep at impact, and the plain-box isolation is bit-identical
  asleep or awake. F1 gives the flip no new urgency and takes none from
  it.
- **D2 (rotation)** — the loss is entirely linear.

**What it unblocks:**
- **S7** entirely. S7 says "the locomotion writer drains its booked
  momentum"; today there is nothing to drain, because the momentum was
  never refused — it was overwritten.
- **R5** (the strike is invisible across the authority boundary) becomes
  measurable on a humanoid.
- **`test_humanoid_knockback` R2/R3** retire, and `test_humanoid_impact`
  gets a rig whose authority regime is declared.
- **S3** shrinks (see §8).

**Recommended position in the board's order:** ahead of S2/S3/S4. It is
the only open slice that closes the largest measured leak in the engine
(623 kg·m/s, 49 % of the world, on the most common entity in the game),
and the slices it would displace are all bit-identical refactors.

---

## 11. Decisions this forces on the owner

**Q-A. Per-particle or per-entity authority?** (D1's Q3, now forced.)
F1 adds evidence: the writer **already behaves per-entity** — `:4440` and
`:5449` both walk `parts.all_particle_indices` whole. And
`EntityPhysicalState::apply_solver_authority`
(`src/kg/entity_physical_state.cpp:35`) is per-entity, `HAS_PART`-
recursive, tested bidirectionally, and MEASURED to have **zero callers
outside its own file**. Options: (i) per-entity take/release — one call,
trivially leak-detectable, matches what the writer actually does;
(ii) per-particle — matches the twelve existing SET sites and is what
D1's Q2 "yes" branch (partial ragdoll) requires. They are not exclusive;
the *authoritative unit* is what decides what a leak sweep can ask.

**Q-B. What does the writer do with the drained momentum?** (D1's Q1,
now with numbers.) The budget is 623 kg·m/s over 20 frames against a
61.361 kg rig. F1 adds one piece of evidence for the shape of the
decision: the strike delivers **5.7–6.2 m/s to the chest in a single
frame**, far past any walking speed, so "absorb as a stagger" and "break
authority and ragdoll" are visibly different outcomes here, not a corner
case. The threshold option stays INV-10-hostile and would still need a
derived, mass-uniform quantity that does not exist.

**Q-C. Widen INV-7, or add a clause to INV-32?** (i) INV-7's wording
already covers `:4441`; widening it is honest to the text but re-baselines
a law that is currently active and green, with a prover that must learn
to read `src/animation/`. (ii) Add clause 7 to INV-32 (aspirational, born
red, no re-baseline) and give INV-7 an explicit "solver-internal" scope
note. Evidence for (ii): the handback is meaningless without a holder,
and the holder lives in INV-32.

**Q-D. Do the friction and warm-start doors book now, as a CLEAN NOW
item?** MEASURED cost: 45.8–89.5 kg·m/s in this scene from friction, and
the warm-start gap leaves the one working ledger at 2.5 % fidelity. The
cure is the same line already proven at `:3373`, needs no holder, and
does not wait on any ruling. The board does not currently list it — C3
closed the double-guard, not the booking. **Recommend adding it to CLEAN
NOW as C8 and doing it first**, because every later measurement of a
drain depends on it.

---

## Method notes

- Build: `cmake --build build-release -j8 --target test_humanoid_knockback`,
  exit 0. Probes compiled outside CMake with the target's own flags
  (`-O3 -DNDEBUG -std=gnu++17 -arch arm64 -DHAS_LLAMA`) against
  `liblogosphere_{dynamics,physics,core}.a`.
- All runs headless. No window opened. No source file modified; the four
  probes live outside the repo.
- `logosphere::phystrace::set_level()` called directly from the probe is
  the way to get the decision tracer in a standalone test —
  `LOGOSPHERE_PHYS_TRACE` does nothing there, because `init_from_env` is
  reached only through `Engine`.
- A read lock held across `update_post_physics` deadlocks
  (`lock_particles_for_read` vs the writer's `lock_particles_for_write`).
  Cost one probe run; noted so the next reader does not pay it.
- The 60-frame window originally used for §2 was discarded: the boulder
  lands on the turtle at `z = 0.203` in every configuration, and turtle
  friction (sanctioned, and excluded from booking by design) then
  dominates the reading. 20 frames keeps the measurement about the
  strike. The same confounder is what makes
  `test_humanoid_knockback.cpp:211-213`'s velocity-minimum readout unsafe.
- Nothing was fixed. This is an RCA and a design.
