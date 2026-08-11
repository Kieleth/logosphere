# Issue #47 handover — Eden's detonation, root-caused end to end

**Branch:** `fix/physics-surgery` (from `fix/organic-bonding`, branch point `96e2688`)
**Repo:** `logosphere-public-2`. Never pushed, no PR.
**Date of this handover:** 2026-08-10

Read this before touching physics. Every number below was measured, not
argued. Where a claim is unverified it says so.

---

## 1. The one-paragraph version

Eden was running at 1322 ms/frame with bodies at 2.2 million m/s. Bisect
put the start at exactly one commit. Three separate defects were found
and fixed, Eden is now at **127 ms** — faster than before the commit that
broke it — and the remaining chain has been traced to a single named
cause that is **not yet fixed** because the fix is a design decision.

---

## 2. How to measure anything here

**Never judge physics work on windowed Eden.** It exits after 4-11 frames
when launched from a background shell, and at multi-second frames it
shows a detonating world regardless of the change under test.

```bash
# the headline number
./build-release/eden/eden --bench 120 --headless --size 1600 1051 > /tmp/e.log 2>&1
grep -c 'EXPLOSION WARNING' /tmp/e.log
grep -oE 'took [0-9.]+ms' /tmp/e.log | sed 's/took //;s/ms//' \
  | awk '{s+=$1;n++} END {printf "mean %.0f ms over %d frames\n", s/n, n}'
```

`timeout` does not exist on macOS. Do not wrap runs in it; it silently
produces empty output and reads as a pass.

Bisect probe (worktree at `/tmp/lp2_base`, remove with
`git worktree remove /tmp/lp2_base`):
`scratchpad/probe.sh <sha> [frames]` — builds eden at that sha and prints
events / worst speed / stall frames / mean frame.

### Diagnostic levers, all env-gated, all default OFF

| lever | what it shows |
|---|---|
| `CANARY_PID=<id>` + `CANARY_FRAME_MIN/MAX` | every constraint row touching one body, per iteration |
| `SPLIT_DEBUG=1` | what the split-impulse position pass actually moved |
| `TEAR_DEBUG=1` | every bond that snaps, with both bodies' state |
| `IMPULSE_MEMORY_OFF=1` | disables the bond integral term |
| `ANCHOR_TORQUE_OFF=1` | disables r×J anchor torque |
| `SLEEP_LAW_OFF=1` | disables constraint-aware sleep |
| `GLUON_SKIP_DEBUG=1` | counts both-KINEMATIC gluon skips |
| `PHYS_SLOW_MS=<ms>` | phase breakdown for slow substeps |
| `RING_RATIO/RING_FRAMES/RING_TRACE` | the light-body repro |

---

## 3. The bisect

`eden --bench 120 --headless`:

| commit | events | worst m/s | stall frames | mean |
|---|---|---|---|---|
| `af55830` merge, 8 Aug | 1 | 97.1 | 4 / 120 | 155 ms |
| `aab1520` diag tracing | 2 | 97.1 | 5 / 120 | 142 ms |
| **`07ea3c3` organic bonded with gluons** | **6** | **331,006** | **120 / 120** | **482 ms** |
| `96e2688` branch point | 33 | 2,642,380 | 400 / 400 | 1695 ms |

`07ea3c3` is issue #47 fix 1. Its own message documents the regression
and names a population it could not explain. That population is now
identified (§4.1).

---

## 4. The three defects, all fixed

### 4.1 Grass was never rooted — `0d619ea`

`generate_trunk()` returns an **empty vector** below 0.05 m of trunk.
`grass_blade()` sets `trunk_ratio = 0.1`:

- short grass 0.15 × 0.1 = **0.015 m** → always trunkless
- tall grass 0.80 × 0.1 = 0.080 m → trunked, until the ±50% height
  jitter drops a blade under 0.05

`07ea3c3` wrote **both** its KINEMATIC rooting and its fallback bond as
`if (!trunk.empty())`, so grass — the entire subject of issue #47 — is
the one plant neither ever reached.

**Fix:** the root is the **lowest body the plant has**, trunk or no
trunk; parentless bodies in a trunkless plant hang off that base.

### 4.2 Entities materialised twice — `0d619ea`

`create_scene_chunk`'s duplicate guard asks the KG whether an entity's
particles carry a render index. Right question at the START of a pass and
wrong one DURING it: that function only **prepares** particles, and
render indices are assigned later on the main thread. **The guard never
sees its own work.** `chunk_entities` is flat, so after a patch activates
and recursively loads its blades, the loop reaches blade 1, is told "no
render index yet", and prepares those particles again. Bonds dedupe by
pair and get overwritten; **particles do not**.

Measured: **771 of 1583 activations were repeats**, 6,792
`[GLUON_INDEX] Overwriting existing gluon` warnings, a patch
materialising 301 bodies where the generator built 238.

**Fix:** the pass keeps its own memory of what it has materialised.
Entity-agnostic — hierarchical rocks and trees hit this identically.

### 4.3 The Baumgarte ratchet — `60bc585`, `a5285ff`, `028d1f0`

Position bias was injected into **real velocity** and never removed. The
canary on Eden P3946:

```
F1 I0  v_rel=-0.01  impulse=-1.03  dvz=+3.90   satisfied, impulsed anyway
F1 I2  v_rel=-4.00  impulse= 0.00              converged — to the CAP
F2 START vel=(3.05,-3.05,3.31)                 carried across frames
MAX_UP  vz 3.73 6.62 12.17 20.07 30.98 44.94 60.75 78.09
```

The solver was not diverging. It converged perfectly to
`GLUON_MAX_BIAS_VELOCITY = 4.0` every frame, and velocity carries, so an
error that cannot close adds the cap forever. Capping the bias bounds the
per-frame dose, not the total.

**Fix:** split impulse for every row. The velocity solve runs with bias
zeroed; a separate position pass solves the same rows into a
**pseudo-velocity** that moves positions and is discarded.

**The subtlety that cost a regression:** a contact row with a NEGATIVE
bias is *speculative* — the bodies have a gap and that bias is the speed
they are allowed to close it at. It is a velocity constraint, not
geometry, and must stay in the velocity solve. Zeroing it made a dropped
box rest 80 mm above the floor having never touched it. **Rule now in the
code: bias that pushes bodies APART is geometry and goes to the position
pass; bias that LIMITS APPROACH stays in the velocity solve.** Gluon rows
are repair in both directions.

Also: `is_at_rest` is not immobility for position repair. Sleep is about
momentum; a sleeping body's overlap is still real geometry. Only
KINEMATIC is immovable to the position pass.

---

## 5. Results

| Eden, 120 headless frames | mean frame | events |
|---|---|---|
| `af55830` before bonding existed | 155 ms | 1 |
| start of this session | 1322 ms | 11 |
| **now** | **127 ms** | **2** |

**10.4x**, and faster than before the commit that broke it, while
carrying bonded grass the original never had.

Green: `test_baumgarte_ratchet` (0 violations), `test_plants_are_rooted`,
`test_grass_holds_together`, `test_settling_wiggle`,
`test_physics_battery` 8/8.

Characterization baseline history, each re-pin deliberate:
`7120ff46938a7b0d` → `1b701dcda5d29d8b` → `5962bcfd9ad272b8` →
`db2d1f7ad9d245bf`. Determinism control green at every step.

---

## 6. THE OPEN ONE — impulse memory amplifies

This is where a resuming session should start.

`test_light_body_ringing`: rooted 3-body chain at grass scale (rest
0.06 m, light end 0.00238 kg), one 1.2 m/s push, then untouched.
200 frames per ratio.

| ratio | WITH memory (shipped) | `IMPULSE_MEMORY_OFF=1` |
|---|---|---|
| 1.0x | 1.200 → 0.000 rings down | 1.200 → 0.000 |
| 2.0x | 1.200 → 0.000 rings down | 1.200 → 0.000 |
| 5.0x | 1.200 → 0.000 rings down | 1.209 → 0.192 |
| 10.0x | 3.535 → 4.180 **GROWING** | 1.274 → 0.000 |
| 15.0x | 7.439 → 4.857 **TORE** | 1.275 → 0.000 |
| 22.8x | 4.831 → 5.526 **GROWING** | 1.239 → 0.000 |
| 25.0x | 4.057 → 0.000 **TORE** | 1.235 → 0.000 |
| | **4 of 7 fail** | **0 of 7 fail** |

A 1.2 m/s push becomes 7.4 m/s at 15x: a **6x gain on energy the system
was never given**. Frame time throughout is 0.01 ms — it is not slow and
not hung, it simply grows until the numbers stop being numbers.

Three suspects eliminated by A/B, one confirmed:

```
ANCHOR_TORQUE_OFF=1   1.160 m/s   identical to baseline
SLEEP_LAW_OFF=1       1.160 m/s   identical to baseline
IMPULSE_MEMORY_OFF=1  0.590 m/s   halved, below the push
```

### The chain this closes

walk gate tears 17 bonds per pass → the blades are **not dragged**, they
reach 29 m/s while Eva walks at 1.2 → every tearing pair is light-bonded-
to-heavy (mean 7.7x, worst 22.8x) → a 3-body chain at 10x amplifies 12%
per cycle untouched → **the integral term is the motor**.

Same defect as rotation-ladder rung 3.

### Why it is NOT fixed

Impulse memory was added deliberately (`56a93b1`), with anti-windup
(`3b4b425`), and took the humanoid shoulder from a 0.64 rad standing
error to 0.0000. Deleting it trades one defect for another. Gating it on
a mass ratio is the if-statement edge fix the charter rejects.

**Three options, owner's call, none taken:**

1. **Bound the memory by the lighter endpoint's momentum.** Today the cap
   is breaking force × dt, enormous against a 2 g blade. Scaling to what
   the light body can carry is a physical bound, not a special case.
2. **Fix the integral properly.** Anti-windup only bleeds faster when the
   fresh impulse OPPOSES the carried load. It does nothing when the load
   is in phase with the oscillation, which is exactly the growing case.
3. **Retire it and re-solve the shoulder** on the split-impulse solver it
   predates, since the position-repair path it partly compensated for now
   exists.

---

## 7. Other open items

| item | state |
|---|---|
| walk gate | red, blade drift **2.56 m** vs 2.0 gate (was 6.75). Should close with §6. |
| Eden residual | **2 events**, worst 133,160 m/s. Pre-bonding baseline was 1 event / 97 m/s. |
| `test_grass_yields` | red, 66% retention vs 75% gate. Was 79% on a world with phantom duplicates and unrooted grass. **Gate needs an owner ruling; do not move a threshold to turn a red green.** |
| `test_physics_drive_arm_chain` | red, predates this session (inertia tensor) |
| `test_humanoid_tuning_coverage` | red BY DESIGN until derivation lands |
| second tear population | `P92<->P93`: both bodies still, rooted side KINEMATIC, dist 0.0579 vs rest 0.0278. A generation-side rest-length mismatch, not an instability. Unexamined. |
| CHANGELOG | **no entries for any of this.** Project convention requires them. |

### The derivation campaign (the original plan, interrupted)

Phase A and B landed. C and D never started.

- **Measured:** 20 of 28 humanoid bonds have their declared value
  overwritten by a uniform 2000/60; **0 survive**; **8 run on
  `GluonConstraintBase`'s 100/10 class default** — hair, ears, eyes,
  which nobody ever gave an angular opinion.
- **63 content-declared solver values, target zero.**
- Phase C: derive `k = E·A/L`, `K = E·I/L`, `K = G·J/L`, damping from
  loss factor. `Materials` already carries E, ν, three strengths and loss
  factor (`09fa5c0`).
- Owner ruling on record: a bond created without material + geometry
  should **refuse to build** (hard error), which lands with Phase C since
  it would fail the 8 today.
- Debt: the rotation ladder carries a **×4.5 angular rescale** to be
  retired once derivation lands.
- `OrganicSpec::gluon_quat_drive` exists, is documented as "blades bend
  by ROTATING", is wired through to every bond, and is **false on every
  species**. Not the walk-gate cause (proven — §6), but still a
  capability built and switched off.

---

## 8. Method notes, earned the hard way

- **Three reductions measured inert before being believed.** ROCKET with
  bonded overlapping boxes (bonded pairs skip contact generation
  entirely, `physics_system_v4.cpp:710`); SLING beyond the 2.0x tear
  ratio (the bond correctly tore and the test measured free fall);
  the kinematic sweeper through grass (green at 8 m/s, 6.7x walking).
  Build the reduction, then check it reproduces, before drawing anything
  from it.
- **Two of my own assertions were wrong, not the physics.** "compressive
  ≥ tensile" is false for wood parallel to grain. A capped rise on a
  column whose bonds must expand 1.6 m calls a correct repair a failure.
- **Silent mechanisms cost multiple wrong hypotheses each.** A tear
  leaves no trace; position repair leaves no velocity. Both now have
  levers. Add one before theorising about anything invisible.
- **A lever is worth more than an argument.** Three of four suspects in
  §6 were eliminated in a single command because they already had env
  gates. The fourth stayed a hypothesis purely because it did not.
