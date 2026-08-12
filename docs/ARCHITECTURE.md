# Logosphere Architecture Invariants

The rules that hold everywhere in the engine. If a fix appears to
require breaking one of these, the diagnosis is wrong: go one level
deeper.

## Everything is a particle

**Light is computed, never faked.** The renderer transports light;
where occluded rays leave space unlit, that darkness is the shadow.
There is no shadow feature to toggle, only light and its absence —
the same discipline as matter: no ornaments, only mechanism.

**Particles are bodies.** If it's a particle, it occupies space and
collides. There are no decorative particles, no skip-this-case
branches in contact handling, no visual-only ornament. Mass,
friction, contacts, and constraints are the mechanisms; there is
nothing else. Even the sun, moons, and stars are particles, placed
far enough away that only their light enters the frame.

**The world turtle is the only immovable thing.** Everything else,
floor tiles, strata, statues, boulders, is a particle with mass.
The turtle is an absolute boundary at z = 0; nothing passes below
it. Immobility above it is achieved through mass, friction, gluon
bonds with anchor points, or kinematic solver mode (an external
writer owns the position). `is_at_rest` is a solver optimization,
NOT immobility: anything asleep must wake and respond when struck
hard enough.

**Physics never reads game-layer categories.** The solver sees
`Particle::solver_mode` (DYNAMIC / KINEMATIC / STATIC) and nothing
else about ownership. Per-consumer bookkeeping lives in the systems
that need it and is invisible to the solver.

**No gravity assumptions.** Every mechanism must work on walls,
ceilings, and in zero gravity. That rules out floor-Z clamps,
push-up heuristics, and axis biases keyed to world-up. Contact
normals come from geometry.

**No springs or damping as patches.** Compliance and spring-damper
models are not emergency fixes for boundary problems. OrganicGluon
as a physical cohesion model is fine; a spring added to hide
penetration or oscillation is not.

**No special-case fixes.** If a specific geometry, size ratio, or
owner exposes a bug, the fix is never to branch on that case. Find
the missing mechanism.

## Orientation conventions

Right-handed world: `+X` east, `+Y` north, `+Z` up.

**Facing at rest is +Y (north).** Any multi-particle rig that
defines its own forward must put it along local +Y.

**Rotation sign: `rotation_z` is clockwise viewed from +Z**
(compass N→E→S→W is positive):

- `yaw = 0` → facing +Y (north)
- `yaw = +π/2` → facing +X (east)
- `yaw = +π` → facing −Y (south)
- `yaw = −π/2` → facing −X (west)

Position math turning local offsets into world positions must
rotate clockwise too. Using the math-textbook counter-clockwise
matrix for positions while geometry rotates clockwise produces
rigs whose bodies and wheels rotate in opposite directions.
`logosphere::assembly::part_world_position` is the canonical
helper; use it instead of hand-rolled sin/cos.

## Engine mechanism, game policy

The engine stops at generic mechanism; games provide policy. The
test: if a change makes equal sense in a farming game and a combat
game, it's engine. If it only makes sense in one genre, it's game.

- Engine: capability aggregation across body parts. Game: which
  capabilities exist and what damage means.
- Engine: event bus with typed channels. Game: what to emit and
  when to react. Events are **transactional**: a batch that rolls back
  emits nothing, and nothing is announced part-way through one, so the
  bus only ever speaks about facts. That makes it the right tool for
  reacting to what happened and the wrong one for watching a change in
  progress; for that, a rule reads `PlannedWorld`. See
  [OBSERVING_CHANGE.md](OBSERVING_CHANGE.md).
- Engine: physics, animation, worldgen generators. Game: what
  entities exist and why.

New entity types, events, and relations flow through the ontology
schema (`schema/*.yaml` plus regeneration), never hand-edited
generated files.

Two kinds of AI agents, two contracts: in-world agents (drivers,
creatures) read the world through a perception layer and never read
the knowledge graph directly; meta-agents (directors, narrators)
stand outside the world, read the full ontology-KG, and may mutate
it.

## Humanoid locomotion

Two coupled subsystems; touching either requires understanding both.

**KinematicRoot.** Real bipedal gait pins the stance foot to the
ground and derives the hips, which vault over the pinned foot as
the joints rotate. On each half-cycle boundary the newly planted
foot becomes the root, anchored at the intended plant target. The
stance leg is rebuilt by two-bone IK aiming at the ankle pivot; the
swing leg is clip-driven FK, with post-FK chain projection keeping
chain integrity. Stance-IK bone orientations are built inside the
committed yaw frame so they never fight FK's yaw (a no-twist bone
quat blended against a yawed one takes the slerp short path
sideways).

**Yaw cascade ("Biomechanical rotation + sidestep").** Steering
propagates eyes → head → torso → hips with time constants of
roughly 80 / 180 / 350 ms and ±45° comfort clamps between
segments, producing the visible lag chain of a real turn. Head
children (eyes, ears, hair) ride the head's yaw, limbs ride their
segment's yaw. When the hips rotate past π/4 from the committed
foot orientation, the non-stance foot replants under the new
heading: a pivot in place, not a forward step.

## Build profiles

- **Full engine** (default, macOS): rendering, physics, Metal,
  examples, all tests.
- **Headless physics** (`-DLOGOSPHERE_PROFILE=physics`): the
  render-free engine — solver, dynamics, worldgen — running the
  locomotion guard suite on Linux CI for every PR.
- **Headless core** (`-DLOGOSPHERE_HEADLESS_ONLY=ON`, any C++17
  toolchain): knowledge graph, capability, damage, events,
  ontology, game time, and the headless test set. Also in Linux CI.

See [GETTING_STARTED.md](GETTING_STARTED.md) for where to register
new sources and tests so both profiles stay green.
