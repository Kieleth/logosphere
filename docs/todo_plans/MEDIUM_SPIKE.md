# D7 spike: what is a medium, in a pure particle engine?

Seed document. The owner's framing, verbatim, because it is the source
and because paraphrasing it would lose the questions. A spike runs
against this; the owner participates in the answer.

## The owner's framing (2026-08-16, unedited)

> we need to send a subagent opus armed with physics skill and just
> spike/brainstorm on how to capture the 'medium', this is an
> interesting problem tho, and I want to participate as usual, how to
> capture/model a 'medium' in a pure particle system... how can we model
> a space scene with a pressure chamber for example, and air moving from
> one to the other... I wonder... can we truly use particles? i.e. can
> we mimic air and air pressure as particles that are not rendered, but
> are simulated? hmmmmmmmmmm...... or better, we do not need to model
> particles of air, we just need to model their effects using the
> physics, but we can have levels of modeling, for example wind... what
> is wind? we get into liquid territory, how to present it in the
> engine? elegantly? a liquid is nothing else that how we think about a
> boundary between two different materials that present different
> properties due to composition, right....? how can we model 'water'
> without particles... first is the concept of particles that are
> modeled but are invisible, air, can we? can we do a fractal way to
> represent them in which we fill/glob air particles into a huge
> particle easy to physic-model, but when it comes to more granular
> interactions with other particles like water, we reduce granularity
> automatically, so we can model waves in a pond where a rock falls into
> the water?

## The questions, extracted

1. **Can a medium be particles?** Bodies that are simulated but not
   rendered. Note that this does NOT violate "no decorative particles":
   invisibility is a rendering property, and a body that occupies space
   and collides is a body whether or not light bounces off it. The
   invariant forbids ornament, not transparency.
2. **Or only effects?** Model what a medium DOES (drag, buoyancy,
   pressure, lift) without modelling what it IS. This is what the engine
   does today for declared volumes.
3. **Levels of modelling.** Both, chosen per situation. What decides,
   and does the choice have to be declared or can it be derived?
4. **Adaptive granularity.** Glob the bulk into few coarse bodies where
   nothing interesting happens; refine near an interaction, so a rock
   dropped in a pond raises real waves. What triggers refinement, and
   what conserves across a split and a merge?
5. **What is wind?** A field, a flow, or the bulk motion of a medium
   that has its own state?
6. **What is a liquid?** The owner's proposal, worth taking seriously
   as a modelling primitive: *"a boundary between two different
   materials that present different properties due to composition"*.
7. **The hard scene.** A pressure chamber in space, venting to vacuum.
   Compressible, a real pressure gradient, a hard boundary, and no
   gravity anywhere in it.

## Non-negotiables the answer must satisfy

- **INV-1.** The turtle is the only immovable thing. A medium is not
  scenery pinned in place.
- **INV-3.** Energy is never created. A refinement or a merge that
  changes total energy is wrong.
- **INV-6.** No gravity assumptions. The pressure-chamber scene is in
  zero-g, so buoyancy cannot be "up", and every mechanism must hold on
  a wall and in orbit.
- **INV-15.** Physics never reads game categories. "This is air" cannot
  be a branch in the solver.
- **INV-19.** Damping only where a real dissipation process is
  modelled. This whole front exists because `ANGULAR_DRAG` violated it.
- **INV-29.** No magic numbers. Densities, viscosities and thresholds
  are declared inputs with units.
- **Engine vs game.** The engine ships mechanism; a game declares that
  its world has air at one atmosphere.
- **Code like a haiku.** A correct design that needs ten thousand lines
  loses to a nearly-correct one that needs eight hundred.

## What exists today

- Media are **volumes you enter**: `InteractionProfile` declares
  `drag_coefficient`, `buoyancy_factor` and a field force; overlaps are
  detected; forces are applied to the intruder. Drag is taken against
  RELATIVE velocity, which is the right form.
- **No ambient medium.** Zero hits repo-wide. The unstated default of
  every scene is vacuum.
- **Linear only.** `apply_volume_forces` writes `vx/vy/vz` and never
  touches `omega` or `torque`.
- The solver is **XPBD / sequential impulse**, which matters: position
  based fluids come from the same family and may compose rather than
  bolt on.

Related: GEDANKEN-2 (sphere into mud), 25 (spin in vacuum), 26 (the
medium you are already in), 27 (the paddle in water). Board fronts D3
(the substrate: model the effect of what we do not simulate) and D7.
