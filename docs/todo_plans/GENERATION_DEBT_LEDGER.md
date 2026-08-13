# Generation debt ledger

**Thesis:** the old solver silently absorbed bad geometry. We did not break
trees — we stopped hiding them.

Every "physics regression" of 2026-08-10 was latent generation debt becoming
visible. A structure born overlapping or stretched used to be quietly damped by
a solver that injected and dissipated energy in roughly equal measure. Remove
the injection (split impulse) and the bad geometry is suddenly the loudest thing
in the scene.

That is the pattern this ledger tracks. Append one entry per finding. Lean:
what was measured, what it means, what changed.

---

## The invariant this ledger enforces

> **A structure is born at rest.**
>
> At frame zero — before gravity, before one solver iteration — every bond
> reads a strain of 1.0 and no two bodies overlap beyond contact slop.

This is a **generator** invariant, not a physics one. No solver change can hold
a structure that arrives already broken.

Gate: `test_tree_bonds_born_at_rest` (trees). The other generators are
unaudited.

---

## Entries

### G1 — Half the tree is born strained · RED · 2026-08-10

```
bonds inspected at frame ZERO (no physics has run)     153
mean strain at birth                                 1.0628   (want 1.0000)
worst strain at birth                                1.8116
bonds born TAUT  (> 1.10x)                              75
bonds born TORN  (>= 2.00x)                              0
```

**Root cause, from the placement code** (`physics_tree_generator.cpp`,
`generate_branch`):

```cpp
float branch_center_x = parent_x + dir_x * length * 0.5f;      // parent CENTRE x
float branch_center_y = parent_y + dir_y * length * 0.5f;      // parent CENTRE y
float branch_center_z = parent_top_z + dir_z * length * 0.5f;  // parent TOP z
```

The z is measured from the parent's top; x and y from the parent's centre. The
bond, meanwhile, wants a centre-to-centre vector of
`R_a·offset_a − R_b·offset_b`, i.e. measured from the parent's top on **all
three** axes.

For an upright parent the top is directly above the centre, the horizontal
terms are zero, and the two agree. **For a tilted parent the top is displaced
horizontally and the placement never adds it.** Strain equals the horizontal
component of the parent's own half-length vector: zero for vertical segments,
growing with tilt.

The signature encodes the bug: `parent_top_z` is a **scalar**. A tilted
segment's top is a **vector**.

Predicts what was measured — the angled branches are the taut ones, the upright
trunk chain is fine, and worst case scales with tilt rather than being uniform.

**Status: THAT ROOT CAUSE IS WRONG.** See G1b. The RED is real, the
explanation was not.

### G1b — the placement is exonerated · 2026-08-10

`test_branch_placement_ladder`, rung 1: upright trunk, one branch. Under G1's
explanation every angle must read 1.000. Measured 0.7454 / 0.8189 / 0.8819 /
0.9326 — red on the one case the theory said was clean.

Hand-worked, parent half 1.0, child half 0.5:

```
                        elev 0    15      30      45
placed centre distance  1.1180  1.2283  1.3229  1.3990
unrotated rest (flat)   1.5000  1.5000  1.5000  1.5000  -> 0.7454 0.8189 0.8819 0.9326
rotated rest            1.1180  1.2283  1.3229  1.3990  -> 1.0000 1.0000 1.0000 1.0000
```

The unrotated column reproduces the measurement to four decimals. **Placement
is correct at every angle**; rung 1 was red because the TEST used the wrong
rest formula.

Consequence: `1fc74be`, filed as a failed experiment, was the correct fix.

The frame-zero RED survives the formula correction (mean 1.0628 -> 1.0821,
worst 1.8116, 75 taut both ways), so the tree really is born strained and the
cause is **not** single-branch placement. Next rungs: branch-off-branch, the
root system, leaf attachment.

**A test that defers to the code under test is not independent.** The audit
inherited the tear law's rest formula deliberately, to avoid "inventing its own
definition" — exactly backwards when that formula is the suspect.

### G1c — GREEN: the plate's geometry, and the measurement that found it · 2026-08-10

```
bonds inspected at frame ZERO     153
worst strain at birth          1.0000    (was 1.8116)
bonds born TAUT                     0    (was 75)
bonds born TORN                     0
BORN AT REST.  PASS
```

**Two lines, both in the root system:**

```cpp
plate_center_z = ground_z - plate_height;        // sank a FULL height
face_center_z  = ground_z + plate_height * 0.5f; // GUESSED the plate's centre
```

The plate sank a whole height below ground, so its top sat 0.125 m below where
the trunk's bottom was placed — on a bond declaring `target_distance = 0`,
which means "these two points must coincide". And the root code guessed where
the plate's centre was instead of reading it, missing by exactly 0.375 m.

Those are the two gaps the audit reported, to the millimetre, seed-independent.
Both now derive from the plate's actual geometry: its top IS the ground, and
its side faces are at its own centre height.

#### THE EXPENSIVE PART — the instrument was measuring the wrong quantity

Four wrong fixes and five instrument errors preceded this, all one mistake:

> **The audit compared CENTRE-to-CENTRE against a rest that governs ATTACHMENT
> POINTS.**

A gluon holds `|attach_a - attach_b| == target_distance`, where each attachment
is `centre + R·offset`. Comparing centre separation against that is comparing
two different quantities, and for anything with a non-zero offset — every leaf,
every branch — it is simply a different number.

What that cost:

| symptom | actual meaning |
|---|---|
| 75 of 153 bonds "born taut" | mostly satisfied bonds, measured wrongly |
| worst strain pinned at 1.8116 through 4 edits | I was editing a file with no bug in it |
| every placement fix made the mean WORSE | shuffling bodies against a phantom target |
| the ladder green at rung 1, red at rung 2 | the ladder mirrored the generator instead of calling it |

The moment the audit asked what the bond actually enforces, **145 of 153 bonds
read exactly 1.0** and the remaining 8 named themselves in two lines with round,
seed-independent constants.

#### The rules this earns

1. **Measure the quantity the code enforces, not a proxy for it.** A constraint
   on attachment points is not a constraint on centres.
2. **A test that defers to the code under test is not independent.** The audit
   inherited the tear law's rest formula deliberately, to avoid "inventing its
   own definition" — exactly backwards when that formula is the suspect.
3. **A test that MIRRORS the code cannot validate a fix to it.** The placement
   ladder reimplemented the generator's arithmetic, so it stayed red no matter
   what the generator did.
4. **A number that does not move when you change things is telling you that you
   are in the wrong file.** 1.8116 said so four times before I listened.

### G2 — Mixed-frame branch offsets · FIXED · `ea67ad1`

One gluon carried two frames: `offset_a` local, `offset_b` built from the
world growth direction under a comment claiming local. Fixed at both sites
(direct and KG-stored).

Real bug. **Did not fix the collapse** — 51→49 tears, drift unchanged. Found
while chasing G1 from three layers downstream.

### G3 — Entities materialised twice · FIXED · `0d619ea`

`create_scene_chunk`'s duplicate guard read a render index that the same
function assigns later, so it never saw its own work. 771 of 1583 activations
were repeats; a grass patch materialised 301 bodies where the generator built
238. Duplicate bodies arrived co-located and bonded to nothing.

Entity-agnostic: hierarchical rocks and trees hit it identically.

### G4 — Grass was never rooted · FIXED · `0d619ea`

`generate_trunk()` returns empty below 0.05 m; `grass_blade()` asks for
`trunk_ratio 0.1`, so short grass (0.015 m) never had a trunk — and both the
KINEMATIC rooting and the fallback bond were written `if (!trunk.empty())`.

### G5 — Rest-length mismatch in grass · OPEN · unexamined

`P92<->P93`: dist 0.0579 against rest 0.0278, both bodies stationary, rooted
side KINEMATIC. Almost exactly 2x, same signature as G1. Likely the same class
of placement-vs-bond disagreement in the organic generator. Not audited.

---

## Unaudited generators

`test_tree_bonds_born_at_rest` covers trees only. The same frame-zero audit
should exist for each of these, and none does:

| generator | audited |
|---|---|
| `physics_tree_generator` | yes (G1) |
| `organic_generator` (grass, plants) | **no** — G5 suggests it fails |
| `humanoid_generator` | **no** |
| `physics_rock_generator` | **no** |
| `strata_floor_generator` | **no** |

---

## Method note

Three real bugs were found and fixed downstream of G1 before G1 itself was
measured, because each was true and near the crime scene. Truth near the
symptom is not causation. The frame-zero audit is cheap, deterministic, and
asks the generator directly — it should be the FIRST question about any
structure that misbehaves, not the last.

### G6 — Branch bonds born without stiffness · ROOT-CAUSED · 2026-08-12

```
[CANARY ROWBUILD] P6<->P7 eff=399.2 bias=-4 budget=[-0,0]
                  breaking_per_axis=80167.4 err=2.99886 str_a=5e7 str_b=5e7
```

`GluonConstraintBase::stiffness` and `damping` have no initializer, and the
tree generator's BRANCH gluon never sets them (trunk declares 100000/1000,
leaf declares 5000/100, branch declares nothing; `make_unique`
value-initializes to zero). Under force-bounded budgeting (rung 3) a bond's
per-substep impulse budget is `(k*err + d*vrel)*dt`, so k=d=0 means ZERO
FORCE AT ANY ERROR: the row above shows a bond 3 m stretched, 80 kN of
breaking budget available, allowed to exert exactly nothing.

Consequence: every branch-to-branch bond in every generated tree is
hollow. The trunk chain falls through its own bonds at cluster-damping
terminal velocity (~1.35 m/s), its sleeping branches hold still as
infinite-mass anchors, and the bond tears at the 2x ratio with both
bodies near-stationary. This is issue #38's canopy drop. It predates the
2026-08-10/12 solver work; the at-rest damping-blend bug used to drag the
sleeping branches down WITH the falling trunk, which made the tree sink
coherently and hid the hollow bonds behind a second bug.

Three probes were needed because two greps filtered the evidence first
(anchored grep hid the gravity lines; a frame-ungated probe read as a
different frame's rows). Unfiltered first, every time.

The fix is not "declare 100000 on the branch too" (fourth copy of a
number that should not exist). It is the already-chosen law: bonds refuse
to build undeclared, or Phase C derives k = E*A/L and d from the loss
factor out of the materials the particles already carry. Owner decides
which.
