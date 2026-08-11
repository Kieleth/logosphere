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

**Status:** root cause identified, not yet fixed. Confirming rung is a trunk
with one branch at 0° (must read 1.0) and the same at 45° (must be strained by
exactly the predicted horizontal offset).

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
