# Logosphere roadmap

Logosphere is an open-source particle-based engine: everything that exists is a
body with mass that occupies space and collides. No decorative geometry, no
visual-only ornament.

This roadmap is also a **contribution board**. Items marked **`[open]`** are
unclaimed and specced enough to start on. If you want one, open an issue saying
so. Items marked **`[bounty]`** are ones we would particularly like help with.

---

## Now

### Generation correctness
Every structure must be **born at rest**: at frame zero, before gravity or a
single solver iteration, every bond reads a strain of 1.0 and nothing overlaps
beyond contact slop.

This turned out to be the root of a long class of "physics" bugs. The old
solver injected and dissipated energy in roughly equal measure, which quietly
absorbed badly-placed geometry. Once the injection was removed the bad geometry
became the loudest thing in the scene — and looked exactly like a physics
regression.

- Fix tree branch placement (tilted parent's top is a vector, not a scalar).
  See `docs/todo_plans/GENERATION_DEBT_LEDGER.md`, entry G1.
- **`[open]`** Frame-zero audits for the generators that have none: organic
  (grass, plants), humanoid, rock, strata. The tree one
  (`test_tree_bonds_born_at_rest`) is the template — it is ~100 lines and
  needs no physics to run.

### Physics
- Bounded integral authority for driven joints under split impulse
  (`test_physics_drive_two_joints`).
- **`[open]`** **Energy ledger.** Report per frame, broken down by constraint
  row type: kinetic + gravitational + elastic strain + dissipated. The
  invariant is that the total may fall, never rise. Every term already exists
  in the data (½mv², mgh, ½kx², damping impulse × relative velocity). This
  turns "which scene misbehaves" into "which row type is the source", and
  would have answered four separate investigations in one run each.

---

## Next

- **`[open]`** Rotation-aware narrow phase. The broad and narrow phases read
  AABBs that ignore orientation, so a tipped plate collides as its upright
  slab and rests on air (`test_settling_flat`, 276 mm). Capsules or an SDF.
- **`[open]`** Derive bond properties instead of declaring them: axial
  `k = E·A/L`, bending `K = E·I/L`, torsion `K = G·J/L`, damping from loss
  factor. `Materials` already carries E, ν, three strengths and a loss factor.
  Target: **zero** content-declared solver values (currently 63).

---

## v2 and beyond

### `[bounty]` A tree that grows

Replace procedural tree *generation* with tree *growth*. Start from a seedling
and simulate over game time: apical and lateral meristems, resource budget from
leaf area, phototropism toward real light sources, gravitropism, self-shading
pruning, secondary thickening that responds to the load the branch actually
carries.

This is a genuinely interesting problem and it fits the engine's grain
unusually well. Logosphere already has: game time with branching timelines, a
knowledge graph that can carry per-entity growth state, real light sources that
know where they are, and a physics layer that can tell a branch how much load
it is under. A tree that thickens where it is stressed and reaches toward where
the light actually is would be emergent rather than authored.

It also dissolves the entire class of bug this roadmap opens with: a structure
that **grew** into its configuration is born at rest by construction, because
each increment is placed relative to the tissue that already exists.

Wants: someone interested in L-systems or space colonisation, plant
biomechanics, or procedural generation with real constraints. Start by reading
`src/worldgen/physics_tree_generator.cpp` and
`docs/todo_plans/GENERATION_DEBT_LEDGER.md` to see why the current approach
fights itself.

### `[open]` Other generators, same treatment
Rocks that fracture along real planes. Terrain that erodes. Creatures whose
body plans come from the ontology rather than from a hardcoded rig.

---

## Contributing

Pick anything marked `[open]` or `[bounty]`, or open an issue proposing
something. Read `CLAUDE.md` first — it carries the engine invariants, and they
are non-negotiable rather than stylistic. The short version:

- **Particles are bodies.** If it exists, it has mass and it collides.
- **No gravity assumptions.** Every fix must work on walls, ceilings and in
  zero-g. Contact normals come from geometry, never from a world-up vector.
- **No if-statement edge fixes.** If a specific size ratio or entity type
  exposes a bug, go one level deeper and find the missing mechanism.
- **Every bug fix ships with a regression test**, and tests show measured
  values rather than just PASS/FAIL.
