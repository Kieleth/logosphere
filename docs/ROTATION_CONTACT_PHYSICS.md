# Rotation and Contact: the 2026-08 campaign record

This document records a physics campaign that took the engine from
"contacts cannot spin a body" to a measured, law-derived rotation
pipeline: contact torque, honest air drag, honest sleep, and a friction
model that acts only where contact actually exists. Every claim below
carries the number that proved it. The test vehicle is the cube drop
ladder (`tests/test_cube_drop_ladder.cpp`) and the ramp race
(`tests/test_ramp_race.cpp`), both observed through Argus
(`src/core/argus.h`), the engine's read-only physics witness.

All contact-torque behavior sits behind the `CONTACT_TORQUE` lever
(default off) until the default-flip ruling.

## What works now, measured

- A cube dropped flat lands flat and invents no rotation (control).
- A cube dropped tilted 20 degrees rights itself and settles flat; the
  righting is real rotation through contact torque, coherent in one
  orientation representation throughout.
- A cube spinning 3 rad/s keeps 0.9996 of its spin through a 0.6 m
  fall (torque-free flight conserves angular momentum per frame) and is
  braked to rest only by floor friction after touchdown.
- A fast top (6 rad/s about the vertical) brakes in place.
- Wheel-spun cubes (6 rad/s about a horizontal axis) drive along the
  correct perpendicular axis on landing.
- A cube balanced on one corner, nudged 0.02 rad, falls to a face at
  the inverted-pendulum rate the geometry predicts (about 3.4 rad/s
  peak), on both contact paths: box-box manifold rows and turtle
  boundary rows. It lands within a millimetre of the face-rest height.
- A sphere on a 40 degree ramp rolls (5.3 rad/s) instead of sliding,
  and out-travels expectations set by pure sliding.
- Lane deviation of a tumbling cube racing down a ramp: 0.0036 m over
  a ~7 m run. At the start of the campaign it was 0.99 m.

## The defects that were found and their laws

### G-43: a cube stood on its corner forever

The most unstable pose a rigid cube has behaved as a stable attractor:
bodies deterministically parked at z equal to half the space diagonal,
run after run, across four unrelated mechanism edits. Three stacked
confiscations produced it, each fixed at its own law:

1. **Sleep entry priced linear speed only** and zeroed angular velocity
   on entry. A corner topple begins as near-pure rotation about the
   pivot (extremity speed 0.026 m/s, far under the 0.1 m/s threshold,
   growing exponentially at ~3.4/s), so the ten-frame rest window froze
   every budding fall at exactly frame ten. Law (G-44): quietness is
   one currency, linear plus angular extremity speed, and it must not
   be growing. A cache may only cache a fixed point of the dynamics.
   Refinement, earned by the oak trees: a topple grows monotonically
   while solver jitter alternates, so only `REST_GROWTH_RUN` (3)
   consecutive growing frames block sleep. Constants
   `REST_GROWTH_TOLERANCE` and `REST_GROWTH_FLOOR` are
   schema-registered.
2. **Turtle rows were rotation-blind.** The omega-cross-r term of the
   contact row's relative velocity existed only in the box-box branch,
   and its gate (`is_quat_driven`, a humanoid-animation flag) was never
   true for plain bodies. A row that applies torque it never measures
   over-corrects; a row that never fires cannot topple anything.
   Measure-gates now equal apply-gates: `solver_mode == DYNAMIC` for
   contact rows.
3. **Warm starting was a second solver with different physics.** The
   cached equilibrium support was applied linear-only, on one row per
   contact key. Support without its lever arms is support without
   torque. Warm starting is now iteration zero through the full
   Jacobian: the cached impulse distributes across the key's rows by
   share and applies both linear and angular halves under the same
   gates the iterations use.

Instrument: ladder rungs R7 and R8 place the same corner stand on the
slab (box-box rows) and on the bare turtle (boundary rows). During the
hunt, R7 fell while R8 stood, which localized the surviving mechanism
to the turtle path in one run.

### G-45: friction of a phantom normal force

A cube spinning 3 rad/s lost its entire spin in the last 3 cm of fall,
three frames before touching. Speculative contact rows (created from
predicted positions to capture fast approaches) were transmitting
Coulomb friction sized by their capture impulses: 52.1 N*s of friction
across a still-open gap. A capture impulse is not a transmitted contact
force, and mu times a phantom is fiction. Law: a row whose bias is
negative (the row's own statement that the gap is open) transmits no
friction and receives no warm-start impulse; its normal half keeps its
approach-limiting job untouched.

This one defect wore three costumes: the airborne spin deaths, the
wheels' weak walks (0.081 m, now 0.19 m from a low release), and the
ramp lane drift (0.99 m, now 0.0036 m).

### Teleported bodies kept their history

Re-positioning a live body (test harness arming, spawners, directors)
left its rest counters, quietness history, and cached contact impulses
intact, so re-armed bodies inherited the previous configuration's
support or fell asleep at frame zero mid-experiment.
`PhysicsSystem::forget_body(id)` voids a repositioned body's cached
impulses; callers that write positions directly are expected to call
it and to reset the transient rest state.

## The instruments this campaign built

- **Argus** (`src/core/argus.h`): read-only watch-list witness.
  Narration lines, touchdown/spin-dead/motion-stopped milestones,
  peak trackers, quaternion-vs-Euler divergence. The asserts and the
  on-screen readout consume the same queries, so they cannot drift.
- **Continuous-world parity**: the interactive window runs all cases in
  one world; the headless test now runs the same sequence after its
  per-rung isolated pass, because three window-only bugs proved that a
  world the asserts never see is a world where defects live. Scenery
  (backdrop pillars) exists in both worlds for the same reason.
- **Flight windows end at the first contact event** of the rung's own
  actor, never at a height threshold and never at another body's
  landing.
- **Physics decision tracer** works headless now: frames tick from
  `PhysicsSystem::update`, and the friction and warm-start apply sites
  emit trace events (they were the blind spots that hid G-45).
- **The lecture standard**: every interactive case opens with a held
  countdown, advances on SPACE only, and stages a contrast twin (the
  identical body without the spin) so the difference is the lesson.

## Honest physics that surprised us

- A cube spinning about a horizontal axis sweeps its corners 0.166 m
  below its own face plane. Dropped from height, those corners strike
  the floor during descent and every knock costs spin: measured walks
  0.081 / 0.000 / 0.016 m from 0.05 / 0.25 / 0.6 m releases. A real
  die does this. The tall-fall walk asserts are therefore open pending
  a re-clamp ruling.
- A resting cube kicked to the engine's maximum spin (6.28 rad/s cap)
  can rock up onto one edge but cannot tumble over it: the rotational
  energy exceeds the barrier by only ~18 percent and face friction eats
  the margin during the climb. Walking requires either a landing assist
  or a body that actually rolls.
- G-44 unmasked a sustained 0.0294 m/s oscillation in gluon-built oak
  trees that the old sleep entry had been absorbing silently. The
  energy source is under RCA; the test is booked open rather than
  re-masked.

## Open items

- G-21/G-23: orientation coherence during hard tumbles (the Euler
  gimbal fold) is the ramp's one remaining lever red.
- `CONTACT_TORQUE` default-flip ruling, plus floor materials (ice).
- Owner-ordered next experiments: spin-lift on impact (fast spin
  should buy a visible hop), a rotation-rate sweep for falling cubes
  (horizontal travel should emerge with rate), and spinning spheres
  versus the floor (no corner band, so spin survives any fall).
