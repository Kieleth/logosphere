# Engine invariants

_Rules that hold regardless of the task. If a fix appears to require breaking
one, the diagnosis is wrong. Stop and re-investigate rather than working
around it._

These were paid for. Most of them exist because the opposite was tried, shipped,
and cost weeks. They are listed here so a contributor meets them **before**
writing code rather than in a review comment afterwards.

---

## 1. No edge-case fixes in physics

**The rule.** If a specific geometry, size ratio, velocity, owner or particle
exposes a bug, the fix is not to special-case it. Go one level deeper until you
find the missing mechanism.

Rejected on sight: size-ratio branches, axis boosts, floor clamps,
owner-specific behaviour, and **magnitude thresholds used as state**.

**Why.** An edge-case fix is a guess about which situations exist. The situations
you did not imagine arrive later, in combination, and the failure looks like a
different bug in a different subsystem. This is the single most expensive
mistake in this codebase's history.

**The tell:** a bare literal deciding behaviour.

```cpp
// NO: "moving up fast" is not a state, it is a coincidence
if (hips.vz > 1.0f) return;          // skip ground correction

// YES: ask the question you actually mean
if (!is_grounded(parts)) return;     // airborne: ground correction does not apply
```

The first version changes behaviour for a knockback, an explosion, a bouncing
platform, a fast slope: every case nobody thought about. The second says what
it means, and stays correct when the world gets more complicated.

If the state you need does not exist yet, **add the state**. That is the
mechanism you were missing, and it is almost always reusable.

## 2. No gravity assumptions

Every fix must work on walls, ceilings and in zero-g. This rules out floor-Z
clamps, "push up" heuristics, and any axis bias keyed to world `+Z`.

Contact normals come from geometry, never from a world-up vector.

```cpp
// NO
support_box.min_z = -10.0f;       // assumes a floor, and that it is above -10
if (p.vz > threshold) ...         // assumes +Z is up

// YES: derive direction from the contact, not the axis
const Vec3 up = contact.normal;
```

Existing code violates this in places. That is debt, not licence: new code
should not add to it.

## 3. Particles are bodies

If it is a particle, it occupies space and collides. There are no decorative
particles, no "skip this case" in contact handling, no visual-only ornament.
Mass, friction, contacts and constraints are the mechanisms. There is nothing
else.

## 4. The turtle is the only immovable thing

Everything else (floor tiles, strata, statues, boulders) is a particle with
mass. Immobility is achieved through the turtle boundary, gluons with anchor
points, or kinematic solver mode (an external writer owns position).

`is_at_rest` is a solver optimisation, **not** immobility.

## 5. Physics never reads game-layer categories

The physics module sees `Particle::solver_mode` (DYNAMIC / KINEMATIC / STATIC)
and nothing else about ownership. Writing `if (p.owner == ParticleOwner::X)`
inside `src/core/physics_*.{h,cpp}` is a layering bleed.

Per-consumer ownership bookkeeping (`ParticleOwner::ANIMATION`,
`ParticleOwner::DYNAMICS`) belongs to the systems that need it: dynamics,
rendering and animation. It stays invisible to the solver.

## 6. No springs, no damping as a patch

Compliance, soft constraints and spring-damper models are not emergency fixes
for animation/physics boundary problems. `OrganicGluon` for cohesive static
bodies is fine: it is a physical model, not a band-aid. Adding a spring to
"fix" penetration or oscillation is not.

## 7. Follow failures to their cause

When something fails, instrument it, trace the actual path, read the actual
values. Do not guess, do not toggle flags at random, do not paper over the
symptom. Fix at the mechanism, add a regression test, move on.

The engine ships a permanent write tracer for exactly this, `ParticleTracer`,
so "who moved this particle, and when?" is a question with an answer rather
than a theory.

---

## World and orientation conventions

Right-handed world:

- `+X` east, `+Y` north, `+Z` up
- **Facing at rest is `+Y`.** `facing_angle = 0` means facing north. Any rig
  that defines its own forward must put it along **local +Y**, not +X.
- **Rotation is clockwise viewed from +Z** (compass N→E→S→W is positive):
  `yaw = 0` faces +Y, `+π/2` faces +X, `+π` faces −Y, `−π/2` faces −X.
- Position maths turning local offsets into world positions **must rotate CW
  too**. Using the textbook CCW matrix for positions while geometry rotates CW
  produces rigs whose parts rotate in opposite directions, a silent dismember
  bug. Use `logosphere::assembly::part_world_position` rather than rolling your
  own sin/cos.

Every downstream subsystem (vision, AI direction estimation, motion planning)
assumes this. A single rig with the other convention makes all of them subtly
wrong in ways that only appear under rotation and look like rendering
artifacts.

---

## 8. One owner per thing, and nobody edits another owner's content

Every entity ingested from a source carries the document and revision it came
from, and that origin is **sealed**. A seed may point at what another seed
owns; it may never write onto it. Concretely the validator refuses three
operations on sealed content: setting a property, destroying it, and creating a
relation *from* it. It permits new entities that reference it, and relations
*to* it.

The reason is provenance. Once a seed has said "this came from
`character-creation.md` at commit `efb8f9d`, and here is the quote", the stored
content must keep matching that citation forever. If a later file could edit
it, the citation stops being provable and the graph begins claiming the book
says something it does not.

**The corollary is the part that gets missed.** When something is partial and
needs finishing, the answer is never "edit the file that owns it". It is to
decide who owns the complete thing and give it exactly one owner. That is how
the skills vocabulary was fixed when two seeds each created a `Gun Combat`, how
the dice vocabulary was fixed when two seeds each created a `1D6`, and how the
aging, mishap and injury tables were fixed when a sampler seed held three
demonstration rows and blocked their completion. Each time the fix was
ownership, not surgery.

So before assuming a partial thing must be extended in place, **check who
depends on it**. For the sampler the answer was nobody: not another seed, not
any code. A thing nothing references can simply change hands.

The same rule decides link direction. A `Career` cannot carry a pointer to its
re-enlistment throw, because careers are created before their throws; instead
the re-enlistment **rule** owns a table keyed by career. That is also how the
book reads: the Commission row belongs to the commission rule, not to the state
of being a Navy officer.

---

## Where the boundary is

The engine stops at **generic mechanism**. Games provide **policy**.

If a change would make equal sense in a farming game and a combat game, it is
engine. If it only makes sense in one genre, it is game. When tempted to
hardcode something game-specific in `src/core/`, ask whether it could be a KG
property, a registered trigger or effect, or an `OntologyRegistry::extend()`
addition instead.

---

## Reviewing against this document

Reviewers cite the rule, not just the line. "This is invariant 1, a threshold
standing in for state" is a teachable comment; "change this" is not. If a rule
here is wrong or has outlived its reason, argue with it in an issue. It is
a record of what was expensive, not scripture.
