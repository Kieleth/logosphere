# Changelog

All notable changes to Logosphere are documented here. The format
follows [Keep a Changelog](https://keepachangelog.com/); versions
follow [Semantic Versioning](https://semver.org) on a 0.x line
(minor versions may break public API until 1.0).

## [Unreleased]

### Changed
- **A body has one orientation.** The engine kept every body's
  orientation twice, as an Euler triple (read by rendering and
  collision) and a quaternion (written by spin integration), with no
  copy between them for ordinary bodies: a spinning cube's turn landed
  in a field nothing read, and the visible world missed rotation
  wherever physics produced it. The quaternion is now the single truth:
  every dynamic body's Euler triple is published from it after angular
  integration, every frame. Bodies that never rotate are bit-for-bit
  unaffected (proved by trajectory hash), KINEMATIC bodies still belong
  to their external writer, and `LOGOSPHERE_QUAT_TRUTH=0` restores the
  old split for bisection.
- **Relations are declared one class per predicate.** `WorldRelation`
  and `EdenRelation` are now abstract, with a concrete subclass per
  member of their enum: each pins `relation_type` with `equals_string`
  and declares its own `source_id` / `target_id`. `rule_language`
  gains the same for its thirteen. Endpoint constraints moved off the
  enums' `valid_source_types` / `valid_target_types` annotations,
  which are deleted along with `relation_type_enum`; the enums remain
  the vocabulary. The generator reads `Relation` subclasses.

  **Impact on games.** A schema that declares its own relation enum and
  a single open relation class will no longer generate: split it, one
  class per predicate. The generator now says so by name. Endpoints
  must also be `Entity` subtypes, which a mixin is not.

  **What this fixes.** Every schema in the repository failed to
  construct under malleus, so none had been judged past construction
  and eight rites had never run against any of them. Eden's five
  relation types reached no registry at all, because harvesting from
  enums required an opt-in annotation Eden never set, while game code
  wrote those edges. `LET_EXPRESSION_HAS_BINDING` declared an endpoint
  (`LetExpression`) that is a mixin marker no instance can belong to.

  The 208 previously generated `addRelationType` triples are unchanged
  byte for byte; the only additions are Eden's five.

### Fixed
- **A sphere no longer falls through a rotated box.** The sphere-vs-box
  narrow phase treated every box as axis-aligned, so a sphere released
  onto a tilted ramp fell through the visible face and came to rest
  inside the box, permanently, on an invented horizontal surface
  (measured: 1.9 m of penetration on a 40-degree ramp). Rotated boxes
  now go through an oriented handler: the sphere meets the real solid,
  slides the real slope, and a sphere resting inside a box exits along
  the box's own nearest face. Unrotated boxes keep the previous path
  bit-identical. Affects every sphere against rotated scenery,
  including fallen tree logs.

### Added
- **A sanitizer lane in CI (`sanitizers-linux`).** Builds the `core`
  profile with AddressSanitizer and UndefinedBehaviorSanitizer and runs
  the headless suite plus twelve chargen lives under them. It exists
  because a use-after-lifetime bug shipped and was invisible on every
  platform CI ran: a pointer into a vector, read after the vector was
  cleared, which libc++ and libstdc++ tolerate and MSVC does not. ASan
  found it in one run. Advisory, not merge-blocking, so a finding is
  loud without blocking an unrelated merge. Built with clang and libc++,
  and both are load-bearing: measured against the two real bugs, gcc with
  libstdc++ detects neither, clang with libstdc++ detects one, and clang
  with libc++ detects both. Container-overflow detection comes from
  annotations the standard library emits, so the library is half the
  tool. `test_chargen` is excluded and the exclusion is named in the
  workflow: it costs 565s under the sanitizers against 74s without them,
  and the lives step walks the same code. Known finding on landing:
  `test_mutation_playback` aborts with a
  stack-use-after-scope in its own fixture (a `CountingPlay` destructor
  writes to two counters declared after the registry that owns it, so
  they die first). The test's subject is unaffected; the fixture is
  wrong. Not fixed here.
- **Git integration policy, mechanically enforced.** Rebasing is
  refused by a tracked `pre-rebase` hook with no override; force-pushes
  and pushes to `main` are refused by `pre-push`. A third check refuses
  a branch that deletes lines which landed on `main` after the branch
  started, which is how three merged pull requests were reverted with
  CI green throughout. `cmake -S . -B build` installs the hooks and
  sets `pull.rebase=false` / `pull.ff=only`, or run
  `./scripts/install-git-hooks.sh`. The rule and its reason are in
  CONTRIBUTING.md and CLAUDE.md; the `merge-policy` CI job re-checks
  it on every PR.
- **`test_collision_bounds_rotation`, and one canonical answer to "does
  collision apply rotation?"** The table now lives only in
  `include/logosphere/physics/narrow_phase.h` and every line of it is an
  asserted check in the new headless test. Rotated BOXes get oriented
  bounds at every site and an oriented box-box narrow phase; unrotated
  BOXes keep the raw extents bit-identical; SPHERE is rotation-invariant;
  ELLIPSOID stays a conservative axis-aligned fallback. The test also
  PINS a live INV-12 defect it did not fix: sphere-vs-BOX still collides
  against the box's world-axis slab, so a sphere on a 30-degree ramp is
  handed `(0, 0, 1)` where a box is correctly handed `(0, -0.5, 0.866)`.
  Two worldgen comments that asserted the opposite of all this are
  corrected; no behaviour changed.

### Removed
- **BREAKING: `ParticleSolverMode::STATIC` is gone.** It was accepted
  from the KG (`solver_authority: STATIC`) and handled by nothing in
  the solver, so a body declared STATIC fell silently. Use `KINEMATIC`
  for a body whose position an external writer owns. Terrain and
  scenery are not immovable by declaration: they rest on the turtle
  boundary or on anchored bonds (INV-1).

### Changed
- **The DCO sign-off is checked on `main`, not on branch commits.** It
  runs on the single commit a squash merge produces, over the push
  event, and no longer walks every commit in a pull request. The old
  gate could not coexist with the merge policy: a branch behind `main`
  catches up with `git merge origin/main`, and that merge commit has no
  authored content, cannot be signed at creation, and cannot be signed
  later without the force-push the policy refuses. A branch could obey
  one rule or the other, never both, and it cost a branch (#131,
  reopened as #133). Contributors see no difference: GitHub builds the
  squash message from the pull request title plus the branch commit
  messages, so `git commit -s` carries the trailer through by itself.
  What the gate catches is a squash message rewritten in the merge box
  with the trailer deleted. `scripts/check-signoff.sh` is the checker,
  `scripts/test-signoff-on-main.sh` proves it refuses as well as
  accepts, and the `merge-policy` CI job runs that test on every PR.
- **Contact rules now apply in order of meaning, general first.** When
  several rules match one contact they all fire, as before. The
  sequence is no longer whatever the internal hash map produced (which
  was unportable and, measured, inverted: the rule authored second
  applied first). A rule with no condition matches every contact, so it
  applies BEFORE a rule that names one, and the narrower rule's write
  is the one left standing. This is what makes a blanket default
  overridable by a specific case. Rules that cannot yet be ranked by
  meaning fall back to authoring order.
- **`KINEMATIC` is a transient authority, not a label.** It is held
  while an external writer drives a body's position and must be
  RELEASED when that writer stops; a body left KINEMATIC can never
  fall, tip or ragdoll again. The schema text that told readers to
  "set KINEMATIC to make something genuinely immovable" is corrected.
- **Unregistering a humanoid releases it to physics.**
  `HumanoidLocomotion::unregister_humanoid` now hands every particle
  back (DYNAMIC, gravity-eligible), so a released body falls. It
  previously tore down the plant anchors and released nothing, which
  is why a humanoid could not ragdoll at all.
- The frame-gated rest damper is removed: it taxed any body 10% of its
  velocity per tick below 0.4 m/s, forbidding slow coasting outright.
  Rest belongs to the sleep law; dissipation belongs to modelled
  processes (INV-19).
- Sleep now covers angular velocity: a resting body no longer spins.

### Added
- `verify_and_load_seed_sequence`, the single ordered path for cumulative
  seed verification and loading. `logosphere-verify` now accepts repeated
  `--prerequisite <seed.json>` arguments, verifies every seed in order, and
  refuses a dependent seed when its prior graph is absent.
- `PhysicsSystem::record_refused_impulse` / `take_refused_impulse`:
  momentum a body's authority refuses is BOOKED and available to
  whoever owns that body instead of being discarded. Contacts,
  friction and warm starts all book it (measured 99.9% of the momentum
  refused); turtle rows are excluded, since a boundary's support is
  not a shove.
- `HasSimpleAppearance` mixin declaring the simple-entity contract
  the engine's `simple_entity_activator` reads (`size`, `r`, `g`,
  `b`), attached to `LightSource`, `Cube`, `CelestialBody`, and
  `Rock`; the earth pack's duplicate `size` slot is removed in its
  favor. Found by interactive QA: Eden aborted at startup on the
  gate because its light writes were never declared.
- `capability.*` writes are now declared in the ontology:
  `LivingEntity` carries the five aggregated profile keys
  (`capability.speed_cap/locomotion/manipulation/rotation/perception`)
  as typed Float slots plus an open `capability.` namespace for
  game-registered capabilities, so `CapabilityStore::write_back`
  passes the setProperty gate.

### Added (engine instrumentation)
- Frame-chain guard: `Engine::render()` warns loudly when a display
  exists, 60 frames have rendered, and `present()` was never called
  (the window silently shows an unpainted white surface otherwise);
  `renders_completed()` / `presents_completed()` counters let tests
  assert the windowed chain directly.

### Fixed
- **Logovger chargen: the Draft answer was read out of a list that had
  already been cleared.** `find_choice` returned a pointer into the
  session's offer list, and every caller cleared that list before it
  was done reading the answer. libc++ and libstdc++ leave a cleared
  vector's bytes in place, so the read kept working on macOS and
  Linux; MSVC's `std::string` destructor zeroes the small-string
  buffer, so on Windows `"2"` read as `""`, the Draft branch was never
  taken, and every character who submitted to the Draft quietly became
  a Drifter. The Draft Career table was then reached by no life at all
  and `headless-windows` failed the absorbed-table coverage gate.
  `find_choice` now returns an owning `std::optional<Choice>`, and a
  `static_assert` refuses a borrowing return type. Dice are not
  involved: `DiceService` is engine-owned xorshift64* over `uint64_t`
  and already produced identical rolls on all three platforms.
- Build on toolchains without `<execinfo.h>` (MSVC): the
  TURTLE_TRACE caller backtrace in `particle_system.cpp` is now
  guarded by `__has_include`; the turtle violation itself still
  reports and aborts everywhere.

### Changed
- **Particle shape extents now document their actual collision semantics.**
  `Particle::width`, `height`, and `thickness` are documented as local-axis
  extents for boxes, including oriented-box collision under rotation; sphere
  and ellipsoid fallback behavior are now stated explicitly under INV-12.
- **Silent fallbacks destroyed across the physics layer** (owner
  order; blocks PR #75). Missing/unset physics data now refuses
  loudly instead of running on invented values. What changed for
  callers:
  - `GluonConstraintBase::angular_stiffness/angular_damping` class
    defaults (100/10) are gone; 0/0 is the UNDECLARED sentinel.
    Organic bonds get both derived from materials (Phase C); other
    creators that consume the angular law must declare it.
  - Every bond must be born with `calculate_breaking_force() > 0`;
    `NailGluon::breaking_force` no longer defaults to indeterminate
    memory. The bond doors (`add_particle_with_gluon_to`,
    `add_gluon_between`) abort with an actionable message on an
    undeclared bond, a null gluon, or an uninitialized system
    (`GLUON_LENIENT=1` / `PHYSICS_LENIENT=1` downgrade to printed
    violations for inventory sweeps).
  - A second live bond for the same particle pair refuses (INV-22)
    instead of a console warning; stale-gluon pruning aborts after
    its diagnostics instead of repair-and-continue.
  - The solver refuses instead of inventing: no more `eff_mass = 1`
    for the turtle row, no `I = 0.01` for invalid inertia, no silent
    NaN-velocity reset, no phantom contact point from an empty
    manifold, no default +Z constraint jacobian.
  - The explosion detector prints an honest `inf` KE ratio and
    refuses an unparseable `LOGOSPHERE_EXPLOSION_SPEED`;
    `PhysicsSystem::add_force` states loudly (once) that registry
    forces are query data, not applied forces.
  - `ElasticGluon::stiffness` (which shadowed the base field, so
    declarations landed where nothing read them) and the dead
    uninitialized `GluonConstraintBase::last_force_magnitude` are
    deleted.
  Full map and breakage inventory:
  `docs/todo_plans/SILENT_FALLBACK_INVENTORY.md`.

### Added
- **`setProperty` is gated by the ontology (Malleus H1).** Every KG
  property write now runs the same declared-property + value-type +
  schema-range check the LLM KGOp path always had, via the shared
  `kg::validate_property_write`. A violation prints an actionable
  `[KG GATE VIOLATION]` message and aborts; `KG_GATE_LENIENT=1`
  downgrades to a printed violation (inventory mode) and lets the
  write proceed. An empty registry (no ontology loaded) skips the
  check, matching `createEntity`'s existing guard. Measured cost is
  printed by `test_kg_property_gate`.
- **Every property the engine writes is declared in the schema.** The
  gate's inventory surfaced ~60 undeclared keys across the generators
  (humanoid rig + gait, FORGE `cap.*` keys, constraint serialization,
  grass/tree/rock/butterfly/snake/totem bookkeeping, chunk stamps
  `x`/`y`/`z`/`chunk_x`/`chunk_y`, streaming config). All are now
  declared in `schema/logosphere.yaml` and `schema/packs/earth.yaml`.
  Dotted runtime keys (`cap.<name>.<field>`) are declared through a
  new `kg_key` slot annotation that `generate_registry.py` prefers
  over the (identifier-safe) slot name.

### Changed
- **Butterfly segment chains are comma-separated lists.** The
  butterfly generator now writes `thorax_particles`,
  `left_wing_particles`, `right_wing_particles` (same pattern as the
  humanoid limb chains) instead of unbounded per-index keys
  (`thorax_0`, `left_wing_1`, ...) that no schema could declare.
  `ButterflyFlight` reads the new format.
- **Floor generator provenance key renamed.** Floor and strata
  generators stamp `floor_kind` = `floor` | `strata` (declared on
  `Floor`) instead of the undeclared `type` shadow key.

### Removed
- **Redundant `type` property stamps.** Five generators (humanoid,
  butterfly, snake, celestial, scene chunk) wrote a `type` property
  duplicating `Entity::type`; nothing ever read it. Also removed the
  humanoid's write-only `behavior` = `look_at_mouse` stamp (input
  policy is game territory). The organic generator's
  `growth_is_mature` write drifted from the declared
  `Growable.is_mature` slot and now uses the declared name.
- **Physics constants are engine inputs (INV-29).** Every named
  physics constant — solver schedule, position-correction gains and
  caps, contact tolerances, turtle boundary, gravity and drag,
  rest/sleep/damping thresholds, wake gates, warm-start memory, the
  explosion-detector calibration — is now declared in
  `schema/physics.yaml` (68 constants, each with value, UCUM unit,
  group, and the RCA that earned it) and generated into
  `src/generated/physics_constants.h` (namespace `PhysicsV4`) by
  `scripts/generate_ontology.py`'s new header-only mode. Extraction
  was value-identical: the characterization checksum held bit-for-bit.
  To retune the engine, edit the schema and regenerate; constant-work
  is a separate effort from code-work by decree.
- **Physics layer ontology.** `schema/physics.yaml` also carries 37
  documentation concepts (bodies, constraints, contact geometry, the
  turtle, the frame pipeline, the instruments) and the LinkML contract
  that `tests/invariants/INVARIANTS.jsonl` rows validate against. It
  is a standalone schema root, never imported by the KG ontology:
  physics concepts are not world entities and cannot reach
  `createEntity`.
- **Two static invariant gates run with the fleet.**
  `test_inv29_constants_gate` (a self-checked magic-float ratchet over
  the physics translation units; 35 documented residual sites, exact
  in both directions) and `test_inv15_owner_blindness` (pins the
  solver's game-layer owner reads at the seven known task-#43 sites,
  failing on any change either way). Both headless, both profiles.
  `scripts/validate_invariants.py` additionally validates the
  invariants files (ids, enums, dangling/cyclic `derives_from`, audit
  links, mechanism symbols still present in the tree) at every
  `physics_sweep.py` start.

### Changed
- **Test executables are headless by default.** Seven standalone tests
  opened a window unless told otherwise (`test_animation_layering`,
  `test_damage_visual`, `test_dynamics_override`, `test_spine_fk_lookat`
  defaulted to windowed; `test_animation_primitives`,
  `test_leg_primitives`, `test_punch_evolution_primitives` always opened
  one). All seven now default to headless and open a window only when
  `INTERACTIVE=1` is set, matching the rest of the test fleet. The three
  FK demos print `SKIPPED (headless)` without a window because their
  main loops are GLFW-input-driven (SPACE/ESC).

### Fixed
- **Space-colonization trees no longer seat the trunk base below the
  turtle.** A leaning trunk placed by axis arithmetic dipped its first
  segment millimetres under z=0 (0.55·L·(1−dir_z) under the door
  guard's measure, plus the cross-section's tilt reach under the
  solver's oriented extent), which the strict turtle guard rightly
  aborts. The generator now measures segment 0's true vertical reach
  with the engine's oriented-box math and lifts the whole tree by the
  worst dip — a rigid translation, so seams, offsets and bonds are
  untouched and born-at-rest is preserved.
- **Walkers no longer sink through the world floor (CLASS-1
  foot-sink).** The humanoid ground-support probes accepted only BVH
  particles as ground; the turtle plane at z = TURTLE_Z was invisible
  to them, and the solver's own turtle lift applies to solver-DYNAMIC
  bodies only — never to a registered humanoid (owner DYNAMICS, solver
  KINEMATIC). A walker on the bare turtle, or one whose floor tiles
  streamed out from under it, free-fell through the world floor until
  a heel-strike queued its pin anchor below z=0 and tripped the
  TURTLE_STRICT abort. The turtle is now support of last resort in
  both probes, touch-only and lift-only: it grounds a body at the
  plane and never snaps one down onto it (an early unconditional
  version yanked the walk-through-grass walker 9.5 cm into its floor
  tiles; that gate measures 10.70 m advance, 0 detonations either
  way now). Diagnostic lever: `TURTLE_GROUND_OFF=1`. Regression:
  `test_turtle_ground_support`.
- **Gluon rows refresh their effective mass to live state before the
  solve (heel-strike +-100 m/s transients).** Contact rows already got
  this refresh; gluon rows kept build-time masses, and wake-on-strain
  fires DURING the gluon build — a gluon's own strain check runs after
  its rows are sized, so a heel-strike replant that strains the stance
  pin wakes leg-chain bodies whose rows were sized with them immovable.
  Those rows overcorrect x1.96 per sweep (eff = m for one-movable, true
  response 2x), a geometric divergence that exhausts all 32 iterations
  and lands +-100 m/s on the chain's drive child for one substep per
  heel strike. Walk gate: detector events 2 (worst 92.3 m/s) -> 0
  (worst 1.2 m/s), all four criteria green, 614/614 grass bonds intact.
- **Physics-drive joints converge on humanoid rigs (the shoulder
  standing error).** Four mechanisms, each measured on the two-joints
  gate. (1) The solver's converged exits were blind to angular rows: a
  drive impulse of 1.5e-4 N*m*s sat 60x under the 0.01 N*s door while
  being 6.5 rad/s on the 2.3e-5 kg*m^2 shoulder-bridge bone, so drive
  substeps exited after 1 iteration and the shoulder crawled at 1/65th
  of its commanded rate. Both exit doors now also require the last
  sweep's angular impulses to spin nothing faster than
  `ANGULAR_RESIDUAL_FLOOR`. (2) FK-owned humanoid bones now declare what
  they are: the FK write site stamps them KINEMATIC and clears
  `is_quat_driven`. They were featherweight free rotational DOFs
  threaded between the drive joints — the 0.091 kg shoulder bone
  absorbed ~98.5% of every angular impulse the shoulder drive exchanged
  and its two unbounded rows pumped it into a sustained ~3 rad/s
  precession that re-rotated the drive's error axis 30-45 deg per
  substep. (3) Angular rows get the split-impulse treatment the linear
  rows already had: bias out of the velocity solve, orientation error
  repaired by a discarded pseudo-omega position pass. Two scopes keep it
  honest: force-bounded bonds keep their bias in momentum (a spring's
  bias IS its force; splitting it catapulted a grass blade 27.80 m),
  and a contact-coupled row whose bias sits at the cap keeps momentum
  too (a saturated bias is motion, not repair, and motion against live
  contacts must be negotiated — contacts carry no angular Jacobian to
  answer a position-level rotation). (4) The scalar drive enable flips
  its child to quat when the gluon carries a quat target: the scalar
  Euler integrator adds omega_z to the CW store while quat-space rows
  emit CCW omega, and that masked sign contradiction rammed the head to
  target-minus-pi at the full bias cap once the FK stamp exposed it.
  Numbers: two-joints shoulder hold 0.1368 -> 0.0274 rad (budget
  0.0627), final 0.0721 -> 0.0181 (budget 0.0314), both tests PASS;
  arm-chain all six assertions PASS with post-settle drift 0.2 -> 0.006
  rad; walk-through-grass GREEN for the first time — worst blade drift
  3.19 -> 1.81 m (< 2.0), hips advance 11.99 m, detonations 0.
- **FK descendants of a physics-drive child ride the live pose.** The
  joint-hierarchy world transform for a drive child is read back from
  the solver (position + rotation_q) before descendants compose, so a
  forearm no longer hangs at the clip pose fighting the driven upper
  arm through the elbow weld.
- **The turtle plane reads a rotated box's oriented down-reach.** The
  world-boundary contact and clamp used raw thickness as the world-Z
  extent, so a quarter-turned plate was held up by the length it no
  longer spans: test_settling_flat measured 276 mm of air under a
  resting plate (178 mm of it from the turtle alone after the narrow
  phase fix). Rotated boxes now derive their bottom from oriented
  bounds in both turtle sites (kept in lock-step), with a half-diagonal
  clearance guard so bodies that cannot reach the plane skip the exact
  extent. The plate now rests with 1 mm of air. The turtle's normal
  stays +Z: that is the plane's own geometry, not a gravity assumption.
- **Split-impulse position repair prices impulses in the masses it
  spends them on.** The position pass computed pseudo-impulses with the
  velocity solve's effective mass (sleep = infinite) but applied them
  through positional inverse masses (sleep = real mass). A walker's
  foot resting a contact row on a sleeping 1.6 g blade teleported the
  blade 13.9 m in one substep - position only, velocity untouched, so
  the explosion detector never fired and the blade was "at rest" a
  field away. The pass now recomputes each row's effective mass
  against the same inverse masses it applies the correction with;
  the same row moves the blade ~1 mm.

### Changed
- **Contacts skip a bonded structure's internal pairs.** The existing
  rule (a gluoned pair gets no contact rows - the bond owns it) now
  applies transitively across the gluon graph, connected through
  DYNAMIC bodies only: a tree crown's deliberately-crossing branches
  and foliage boxes no longer fight their own bonds with contact rows,
  while two plants rooted to the same immovable tile remain separate
  structures whose blades still collide. Components rebuild from the
  live gluon list every step, so torn bonds dissolve them immediately.
- **Box-box collision is rotation-aware.** Rotated boxes now collide as
  their oriented shapes (SAT over 15 axes + reference-face clipping),
  not as world-axis slabs of their raw extents. Contact normals follow
  the bodies' actual orientations: a 30-degree-tipped plate resting on
  a slab reports the slab's face normal with its low edge's true 5 mm
  depth, a yawed blade's face contact carries both lateral components
  (|nx/ny| = tan(yaw) exactly), and a quarter-turned plate settles ON
  its support with 1 mm of air instead of hovering at its unrotated
  height. Unrotated boxes keep the existing axis-aligned path bit for
  bit, including the static-tile surface merging. Broad-phase bounds
  (pair AABBs and BVH leaves) now enclose the oriented box, so rotated
  overlaps the raw extents under-covered are no longer missed. New
  regression test: `test_rotated_box_contact`.
- **Phase C complete for organic bonds: bending derives too.** Angular
  stiffness K = I / (L_a/E_a + L_b/E_b) with I = A^2/(4*pi) from the
  bond's contact area (N*m/rad; a grass blade derives ~8e-4, an oak
  branch ~6e5), angular damping from the loss factor against the pair's
  reduced moment of inertia. Joint semantics (enable flag, rotation
  limits) stay declared: policy, not material.
- **Solver: contact rows are never oversized, manifolds split their mass.**
  Contact rows built before the wake pass could freeze an effective mass
  that treated a sleeping body as infinite; the impulses then landed on
  the woken body at up to 59x their intended velocity change and
  multi-point manifolds pumped the error geometrically (a 0.03 kg blade
  left a walked-through lawn at 245 m/s). Rows now shrink to the live
  predicate at solve start (never grow, never zero: a sleeping row keeps
  its warm cache for the frame it wakes), and each of a manifold's N
  same-normal rows carries 1/N of the pair's effective mass. Walk gate:
  detonations 1 -> 0, worst blade drift 13.93 -> 3.19 m, torn bonds
  18 -> 4. Eden headless 1600px: 6.3 -> 142.9 FPS.
- **Solver: convergence exits are dimensionally honest, sleeping bonds
  build no rows.** The converged exit is the impulse threshold alone; the
  velocity AND-term added during the leaf-sink investigation was
  unreachable from both directions in mixed-mass scenes (heavy rows hold
  impulses that mean 3e-5 m/s; 1 mm/s on a 0.1 g blade is 1e-7 N*s,
  below solver noise), and the leaf sink is cured at the mechanism now
  (momentum-unit memory cap + derived force law, foliage gate green).
  Bonds whose endpoints are both immovable skip row build entirely after
  the wake-on-strain check. Eden headless 1600px: 2.5 -> 6.3 FPS, rows
  58k -> 24k, solver 27 -> 9.7 ms/substep.
- **Organic bonds derive their force law from materials (Phase C, axial).**
  `k = A / (L_a/E_a + L_b/E_b)` (material spans in series between the
  attachment points) and `c = eta * sqrt(k * mu)` (loss factor against the
  reduced mass), computed at bond registration from contact area, the
  materials table and the particles themselves. The declared stiffness/
  damping constants in the tree, strata, rock and KG-organic paths are
  deleted; a force-bounded bond can no longer be born hollow (ledger G6:
  branch bonds with an unset force law stood a canopy on nothing).
  The engine also refuses to register a force-bounded bond whose force law
  is missing (loud crash naming both particles; `GLUON_LENIENT=1` for
  inventory sweeps).
- **Solver: one immovability predicate, one door for momentum.** "Can this
  body receive momentum?" (massless, sleeping, KINEMATIC: no) is now
  answered by a single function (`inv_mass_momentum`) used by gravity,
  constraint-row build, warm starts, impulse application and damping,
  replacing inline variants that disagreed with each other. Material
  damping is re-expressed as the reduced-mass impulse it always was.
  Fixes: bonds can no longer drag sleeping bodies (a sleeping branch
  sank 6.74 m under its leaves' bond damping while reading at rest), and
  a bond between two immovable bodies now has effective mass zero
  instead of a phantom 1 kg that poisoned warm start and impulse memory
  and detonated light bodies on wake.

### Added
- **Bonds are honest spring-dampers, and they can rotate what they hold.**
  Force-bounded gluons (`force_bounded()`, organics) exert at most
  `(stiffness x error + damping x v_rel) x dt` per row — `stiffness` was
  previously decorative and every bond was an infinitely strong
  rate-limited constraint. Angular drive rows get the same treatment
  (`angular_stiffness` as a real torque budget). Gluon axis rows carry
  their anchor lever arms: impulses torque quaternion-driven bodies
  (`omega += r x J*P / I`), with the matching `omega x r` term in relative
  velocity and `(r x J)^2 / I` in the row's effective mass. A pushed chain
  can now bend by rotating instead of shearing. Opt-in per body via
  `is_quat_driven`; nothing else changes behavior.
- **Sleepers wake when the world demands it**: a kinematic body wakes a
  sleeping one on approach (closing speed, not raw speed, so gliding
  foot-plant anchors stay silent); a bond strained past 2 cm wakes its
  endpoints; a joint bent past its angular target wakes for recovery.
- **Enums can declare case-insensitive membership** (`annotations:
  case_insensitive: true` on the enum). The setProperty gate and the
  KGOp validator then fold case when checking membership, matching
  consumers that normalize on read (the interaction loader upper-cases
  `TransformationRule.trigger`, and the schema has always promised that
  "ON_CONTACT" and "on_contact" both resolve). Everything else keeps
  exact, closed-enum matching.

### Fixed
- **Solver: constraints can no longer create energy** (#47). Three
  unbounded impulse/bias paths turned ordinary footsteps into
  detonations (measured: a 9 m/s leg swing handed a 2-gram grass
  segment 675 m/s; a walk across three grass patches produced bodies
  at 2.36e9 m/s). Contact rows are now capture-bounded (a contact may
  stop an approach plus a support cushion, never amplify it), gluon
  position bias is velocity-capped like V4.14 capped contacts, and
  angular bias — whose 0.4·error/dt formula requested exactly the
  151 rad/s the walk gate measured — is rate-capped the same way.
  Walk-through-grass now completes with zero detonation events and a
  worst body speed of 3.2 m/s; the 8-scenario physics battery is
  unchanged.
- **Organic joints can now actually tear.** Breaking force used a
  1e-4 m² area floor against the 100 MPa particle default, so every
  grass joint held 10 kN. Bonds now use the real tear cross-section
  (the two smallest extents), specs carry `material_strength` (grass:
  5 MPa, ~60 N joints), and gluons gain a strain criterion
  (`max_strain_ratio`, organic default 2.0x rest) because force-based
  breaking is unreachable for gram-scale bodies regardless of load.

---

## [0.4.5] - 2026-08-15

### Fixed

- `logovger-headless` finishes lives that reach a rule's printed fork.
  Injury 3 offers Strength or Dexterity, mishap 1 offers "the same as a
  result of 2" or "roll twice and take the lower", and the executor
  refuses to guess between them by design. The headless driver wired
  its attribute-selection seam to the answer source and never wired the
  choice seam, so those lives ended at the fork: 19 seeds in 200, about
  one life in ten. The choice now goes to the same source as every
  other answer, so `--record` tapes which branch was taken and
  `--replay` reproduces it.

### Added

- CI plays 200 lives end to end on the headless lane. The test suite
  proves the rules behave; it cannot prove the binary that plays them
  can finish. That gap is how a one-in-ten abort stayed green.

### Note for anyone holding a tape

A tape recorded before this release has no answers at the new
`referee.choice` site. Replaying it through a life that reaches a fork
is refused rather than guessed, which is the tape contract working.
Re-record those lives.

## [0.4.4] - 2026-08-14

A rule can now be asked whether the game ever ran it. Absorbing a
rulebook already proved two things: that the data matches the book, and
that the graph hangs together. Neither says anything about whether the
engine acts on it, and that gap had been hiding real defects.

### Added

- `OutcomeExecutor::rules_reached()`: every outcome entity the executor
  has applied. Only applied plans count, so a rule that planned to act
  and stopped on a pending choice is not in it. `OutcomePlan` carries
  the same list per apply, in visit order, excluding branches not taken.
- `ChargenRequest::taste`: an optional hook for the choices the book
  leaves open, defaulted so every existing caller behaves exactly as
  before. Fixed taste is invisible in a green suite, and this is how a
  sweep asks for something other than the auto-player's habits.
- A coverage gate: play lives across every career, ask the executor
  which absorbed rules were applied, and fail if any table in the graph
  was reached by nothing. One row is enough to prove a table is wired,
  so the gate survives sampling while still catching a table that is
  wired to nothing.

### Fixed

- The Draft applies its outcome instead of reading a slot off it. The
  Draft table's six services are `EnterCareer` outcomes, and the
  primitive reached into the entity for its `drafted_career` property
  and never ran the rule. It worked, and it meant the one absorbed rule
  that changes which career you are in was the only rule the executor
  never executed, invisible to anything watching what the rules do.

### Note on what this found

With the auto-player's standing taste, 84 of 171 tables in the graph
had never been reached by any test in any release: every Personal
Development table, every Specialist table and every Advanced Education
table, for all 24 careers. Three of the four training tables the book
gives, 432 cited and verified outcomes, never once applied to a
character. Varying the sweep's choices took that to zero and raised
outcome coverage from 459 to 922 of 1173.
## [0.4.2] - 2026-08-14

### Fixed

- Joining a career grants the skill its rank 0 row prints. Twenty-three
  of Cepheus' twenty-four careers start a character on something (the
  Navy on Zero-G-1, a Physician on Medicine-1); those grants were in
  the graph, cited to the cell they came from, and nothing read them,
  because promotion consulted the rank track only from rank 1 up.
  Every character finished a skill level short, and a Navy character
  never held Zero-G at all, since it is not on the service table
  either. The graph now drives a `grant_rank_zero` procedure step, and
  the test derives the expected skill per career from the rank tracks
  rather than from a list, so all twenty-three are checked.
- Timelines print what a rule's cell says, not the markdown that
  carries it: `[Athletics-1]` instead of `\[Athletics-1\]`. The stored
  citation is untouched, so the verifier still compares against the
  source text byte for byte.

## [0.4.1] - 2026-08-14

### Fixed

- The character-generation auto-player answers the Draft-versus-Drifter
  question the book asks a refused character, alternating on the seed
  so both paths are exercised and a given seed still replays exactly.
  It previously refused, and about 29% of swept seeds produced no life
  at all: a third of the space the sweeps exist to search went
  unsearched. Sweeps now generate 330 lives where they generated 210,
  with none abandoned mid-question.

## [0.4.0] - 2026-08-14

The rules a game plays by can now live in the graph and check
themselves. A published rulebook is read in as cited data, every roll
records which rule made it, and the graph plus the dice journal is
enough to re-derive what a rule permitted and compare it to what
happened. Guide: docs/RULES_AS_DATA.md.

From here the project releases per merged PR, so a reader can tell
which release introduced what. See docs/RELEASING.md.

### Added

- **Rules as Data** (`docs/RULES_AS_DATA.md`): how to absorb an existing
  body of rules so the graph can check the game obeys them. Where the
  data/code line falls, why a rule must never be found by its printed
  name, how a value proves itself against the text stating it, and the
  failure modes we hit.
- A `DiceRoll` records the rule entity that produced it, so the dice
  journal plus the graph is enough to re-derive what a rule permitted
  and compare it to what happened.
- `ProcedureStep` may declare an `outcome` — something the book says
  happens when the step is taken, applied through the executor rather
  than computed by the primitive — and may name a single `table`.
- `implied_by`: proof for a number the text states without writing, such
  as a count carried by an indefinite article. The phrase must appear in
  the quote, so an invented marker fails as a wrong digit does.
- `miss_is_nothing` on `LookupTable`: a key with no row means the rule
  does not apply rather than being an error. `LookupTableResult` gains
  `missed`, distinct from a match and from a failure.
- `ChoiceResolver` on `OutcomeExecutor`, the sibling of
  `AttributeSelector`: answers a branch the book prints and does not
  decide. No default — a rule with nobody to answer it stops the run.
- `TaskCheckOptions`: a natural-result floor ("a natural 2 is always a
  failure") and a situational modifier, both reported on the execution.
- `roll_min_unbounded` on `TableEntry`, the mirror of
  `roll_max_unbounded`, for a band written "N or less".
- Seed extractors own their output: `generated_by` names the script,
  `regenerate_seeds.sh` rebuilds every generated seed, and CI fails on
  any diff.

### Fixed

- Rules the book states that chapter 1 got wrong: the Advanced Education
  gate, a natural 2 always failing survival, a promotion's extra
  training roll adding to the term's own rather than replacing it, and
  the years a mishap costs.
- Outcomes that ask a question no longer abort the run. Two rows of the
  mishap and injury tables were killing 8% of generated lives.
- An answer reaches the question that was asked: a crisis raised inside
  the mishap step read "pay for care" as "take the mishap".
- Table and benefit choices resolve by entity, not by position in a list
  that can be rebuilt between the offer and the answer.
- The character sheet's money is re-derived wherever it is read, so a
  rule moving money between muster-outs cannot leave it stale.

### Changed

- The book's numbers moved out of the procedure into the graph, as cited
  constants or — where they are effects rather than thresholds — as
  outcomes.
- Basic training grants skills through `EnsureSkillLevel` outcomes
  instead of writing `SkillRating` by hand.
- Nothing in a run reads a table's printed name. Renaming every table in
  the graph produces identical lives.

---

## [0.3.0] - 2026-08-11

A published rulebook now plays: rules are read out of the book, verified
against the cell they came from, and executed by the engine. The release
also names the two ways to observe change, the event bus for what has
happened and `PlannedWorld` for what a rule is about to do.

### Added
- **The ingestion verifier reads numbers written as words.** Books spell small
  counts out, and "Reduce three physical characteristics by 2" states the
  three as plainly as a numeral. Whole words only, one through twelve plus
  "both"; a number the text does not state still fails.
- **`N+` is a band the verifier understands**, the shorthand for "N or higher"
  that the SRD prints three times. `TableEntry` gained `roll_max_unbounded`
  to carry it, matching `key_max_unbounded` on `LookupEntry`.
- **`unmodelled` on the `Cited` mixin**: the part of a rule the graph does not
  yet express, in plain words. Absorption is sometimes partial, and a partial
  rule that admits it is honest where one that looks whole is not.
- **Aging, mishaps and injuries are absorbed.** Twenty rows across three
  tables, each addressed to the cell it came from: the aging table with its
  signed bands and its open-ended top, the survival-mishap table the Referee
  may allow instead of death, and the injury table mishaps send you to. Two
  clauses exceed what the outcome vocabulary can say and carry `unmodelled`
  saying which and why.
- `attribute_delta_dice` for rolled reductions ("reduce one physical
  characteristic by 1D6") and `ForfeitBenefits` for "lose all benefits".
- **Subject-keyed lookups** (`SubjectLookupTable`, `SubjectLookupEntry`).
  `LookupTable` answers "score 12 gives +2"; this answers "the Navy row".
  Rules that vary by career, class, species or faction all have that shape.
  A `ProcedureStep` names the table it consults through a schema slot, so a
  step reaches its data without matching a name in code.
- **Progression, attribute groups and possessions in the rulebook meta-pack.**
  `ProgressionTrack` / `ProgressionStep` / `ProgressionStanding` model a ladder
  of standing within a profession and where a character stands on it;
  `AttributeGroup` plus `ModifyAttributesInGroup` express "reduce three
  physical characteristics by 2", which names a count and a group rather than
  an attribute; `Possession` / `GainPossession` / `PossessionHolding` cover
  what a book hands out that is neither money nor a skill. Standing and
  holdings are state, so they carry no citation, matching `SkillRating`.
- **`PlannedWorld`: read the world as your in-flight rule will leave it.**
  A rule's operations are applied atomically at the very end, so until then
  the graph reads as it did before the rule started and the event bus stays
  silent. Both are guarantees, and the cost is that a handler which reads a
  value, changes it and writes it back could silently discard an earlier step
  of the same rule. Outcome handlers now receive the view on their context.
  It answers `property`, `has_property`, `type_of`, `exists`, `related` and
  `was_written`, the last being the question a value read cannot answer:
  writing the number something already held is invisible to a comparison and
  is still a write. Guide: `docs/OBSERVING_CHANGE.md`.
- **The chooser of a grouped attribute change is told what the rule already
  took.** `AttributeSelectionRequest` carries the planned value of every
  eligible attribute and the names of those this outcome already changed, so
  "reduce two physical characteristics by 2, reduce one physical
  characteristic by 1" can hit three distinct attributes. Reported, not
  enforced: a rule may legitimately strike the same attribute twice.
- **`AttributeSelector`**, the seam where a game answers what a rule leaves
  open. Where the count covers the whole group there is nothing to choose and
  no selector is consulted; where it does not, the engine refuses to pick and
  the game supplies a prompt, a roll, or a referee. An answer that is not
  exactly the count the book set, distinct, and drawn from the group is
  refused with nothing moved.
- **`roll_selection` on `GrantTableRoll`** (`EACH`, `LOWEST`, `HIGHEST`) for
  "roll twice on the table and take the lower result". Absent means `EACH`,
  since a bare instruction to roll says nothing about choosing between
  results. `LOWEST` names the lowest dice total, not the kinder outcome:
  which end of a table is kind belongs to the table.
- **`attribute_delta_reduces`.** Dice say how much, never which way. A fixed
  delta spells out its sign and a rolled one printed nothing, so the engine
  was inferring direction from the first table it met. A rolled change now
  states it or is refused.
- **A change past a schema bound lands on it and says so.** Reducing an
  attribute below its declared minimum used to fail the whole rule; rulebooks
  floor characteristics and then have rules about reaching the floor, so this
  is expected play. `OutcomeResult::attributes_limited` reports what was asked
  for against what was allowed, and what that means is the game's to decide.

### Changed
- **Logovger ages, and a referee decides what the years take.** Aging runs at
  the end of the fourth term and every term after, with total terms as a
  negative modifier folded into the recorded roll, so a life reads
  "2D6-5 = -2 (5 terms as a negative DM)" and the row that hit you is provable
  from the dice journal. Which characteristics decline is the referee's call,
  answered by an LLM reading the character's history, with **no fallback**: no
  model means the game refuses to run rather than quietly deciding for itself.
  A crisis at zero prices medical care at 1D6x10,000 before asking, restores
  to 1 if paid, and marks the survivor as failing every future qualification
  check without spending a die.
- **Documentation for observing change**, `docs/OBSERVING_CHANGE.md`: which of
  the two mechanisms to reach for, the channels that exist, why events are
  transactional, and the read-modify-write trap. The channel table in
  `GAME_LAYER.md` listed an `animation_contacts()` channel that does not exist
  and omitted four that do.
- `TaskCheck`'s attribute and its modifier lookup are now optional, and
  travel together: neither, or both. `TaskCheckRunner` throws unmodified
  checks with a modifier of zero, and `TaskCheckExecution::lookup()` returns
  a pointer that is null when there was nothing to look up.
- **Logovger mustering out.** Leaving a career pays: one benefit roll per
  term served, plus one at rank 4, two at 5 and three at 6, with at most
  three taken as cash. `GainPossession` is now an outcome the executor
  applies, so passages, weapons and ship shares are held in the graph with
  their counts.
- **Logovger basic training.** A first term in a first career grants every
  skill on the career's service table at level 0; a first term in a later
  career grants one of them, chosen. Both career entries route through it,
  including the Draft.
- **Logovger commission and advancement.** Rank 0 characters may try for a
  commission, Rank 1 and above for advancement, once each per term and only
  where the book gives the career a hierarchy. A promotion takes the new
  rank's title and bonus skill and buys an extra training roll.
- **Logovger training is a choice, not an automatic roll.** The book says
  "choose one of the Skills and Training tables for this career and roll on
  it", and offers four: Personal Development, Service Skills, Specialist and
  Adv Education. Training also stopped assuming every row grants a skill,
  since Personal Development grants characteristics.
- **Logovger re-enlistment is a throw, not a decision.** At the end of a term
  the book rolls first and honours the player's wish only if the roll allows
  it: a failure forces them out of the career, and a natural 12 forces them
  to stay, outranking the seven-term cap exactly as the book states. Cepheus prints re-enlistment as a
  bare "6+", a throw no characteristic modifies, and a check without a
  modifier source is still a check. Dice and a target remain required.
- **`source_aliases`, `source_defect` and `suggested_reading` on the `Cited`
  mixin.** Absorbing a book finds holes in it, and every absorbed book will
  have some. A defective entity now enters the graph as the source writes
  it, marked, with the reasoning recorded in a slot that nothing consumes:
  rules read the book, never a guess. Aliases are the source's own alternate
  names, so `@@Type:Name` resolves through them, exact names first.
- **Verification follows dependency order.** `verify_seed` accepts the seeds
  a seed depends on and loads them into the same scratch world, because a
  seed that references what another owns cannot be verified alone.
- **Cross-seed entity references.** A seed can now address an entity another
  seed created, written `@@Type:Name` in any `entity_ref` property or op
  target. Aliases remain file-local; this is the one way across that
  boundary and it is deliberately narrow, requiring exactly one match.
  Nothing found, or more than one, fails the load with an error naming what
  it looked for. This is what lets one seed own a vocabulary (skills,
  careers, constants) while others reference it instead of re-creating it.
- **Logovger character creation is playable end to end**, with the sheet,
  the personnel file, and the book's own citations on one screen. Careers
  are a scrollable list you can read before you join; every value on the
  sheet answers where it came from; skills and the service record grow
  with the life instead of capping at a fixed number of rows.
- **A narrated file, written live.** Each beat asks the narrator for the
  scene and for one clipped clause, and the clause is appended to the
  character's file as the life happens. Narration never names a die, a
  target or a characteristic: the numbers are already on screen, and the
  prose is the part they cannot say. Every narration is stored as a
  `Narration` entity carrying the roll ids it was written from.
- Two-phase rollable-table execution. `RollableTableRunner` validates a
  complete table and every reachable dice total before consuming randomness,
  then commits one citable selection containing the table, row, typed outcome,
  and exact roll. Outcome application remains a separate explicit
  `OutcomeExecutor` call, so failures and choices reuse the original
  selection without rerolling. Logovger training now uses this engine path;
  its handwritten dice parsing and row matching were deleted.
- **Source locators** (`logosphere/text/source_document.h`,
  `source_locator.h`): address a piece of a source text and resolve it
  back, so captured data can be proven against the text it came from.
  A source is normalized once into a document model (sections,
  sentences, tables with keyed rows, list items); a locator addresses
  that model by heading trail plus table/row/column or by sentence with
  optional context. Closes a real hole: a value cited to a table LINE
  could borrow a neighbouring column's number, and a claim that Scout
  qualifies on 5+ passed verification because "5" was in the row as
  Pirate's. A cell citation refuses it, with the source's own answer in
  the failure. Markdown parses today; a new source format needs a
  parser into the model and nothing else. Docs: docs/SOURCE_LOCATORS.md.
- Data-driven procedure execution. `ProcedureRunner` validates a complete
  Procedure graph against exact game-declared primitive and route contracts
  before invoking handlers, follows seeded routes, suspends and resumes typed
  choices, rejects cross-procedure jumps, and stops synchronous cycles. The
  seed verifier applies the same primitive and routing contracts at ingestion.
  Logovger's existing playable chargen slice now runs as an eight-step cited
  Procedure instead of a handwritten control-flow chain.
- Typed, atomic rulebook outcome execution. `OutcomeExecutor` resolves
  ordered sequences and suspending choices, dispatches exact concrete
  outcome types, validates one complete KG-operation plan, and commits its
  KG and dice effects together. Built-in handlers cover attribute changes,
  skill minimums and advances, fixed and rolled per-currency money, no-op,
  and pending table-roll requests. Games can register exact concrete
  handlers that return typed procedure signals. Unknown outcomes and
  malformed or incomplete data fail without partial state or events.
- Reusable atomic KG-operation batches and dice transactions. Failed KG
  batches restore created entities, properties, and relations, while failed
  dice transactions restore streams, roll IDs, and journal state. Events are
  held until their transaction commits.
- Typed rule-table results. `LookupEntry` is now an abstract selection
  shape and games declare concrete result rows; `LookupTable.entry_type`
  is verified against every attached row. Rollable rows require one typed
  outcome, with `NoEffect` for intentional no-ops and ordered
  `OutcomeSequence` / `OutcomeStep` composition for multi-effect results.
  The seed verifier now rejects semantically incompatible tables and
  malformed outcome sequences.
- **`rulebook` ontology pack** (`schema/packs/rulebook.yaml`): the
  meta-ontology for tabletop-derived rulesets, what any book of rules
  is made of. `Cited` mixin (source file, section, verbatim quote on
  every rule entity), `DiceExpression`, `TaskCheck`, `RollableTable` /
  `TableEntry`, `LookupTable` / `LookupEntry` (state-keyed tables),
  abstract `Outcome` with typed kinds (`EnsureSkillLevel`, `AdvanceSkill`,
  `ModifyAttribute`, `GainFixedMoney`, `GainRolledMoney`, `GrantTableRoll`),
  ordered sequences, typed choices, `RuleConstant`, `Procedure` /
  `ProcedureStep` / `StepRoute` (the book's gotos as routing data), and
  `JudgmentPoint`. Opt-in like every pack:
  `kg.extendOntology(rulebook::ontology::registry())`. Classes earn
  existence by the rule of two; first instances come from the Cepheus
  SRD chapter 1 and are verbatim-checked against the vendored source in
  `test_rulebook_pack`. Design record: docs/RPG_MODULE.md.
- `SkillRating` in the rulebook pack: a held skill at a level (typed
  `skill` ref + `skill_level`), game state rather than book content,
  attached to its holder with `HAS_PART` and written by the generic skill
  outcome handlers through the validated path.
- `SPECIALIZES` relation in the core vocabulary: a narrower thing
  refines a broader one (skill cascades, taxonomies).
- **Typed entity references on the validated write path.** A
  class-ranged slot (`TableEntry.outcome` ranging `Outcome`) now
  generates `OntologyRegistry::addRefProperty` with the target class,
  and `validate_kg_op` rejects any value that is not the id of an
  existing entity of that class or a subtype. Previously such slots
  validated as pass-through strings.
- `DiceExpression` grammar gains an `xK` multiplier suffix
  (`1D6x10000`): total = (dice sum + modifier) * multiplier, journaled
  and event-carried like every roll.
- `DiceService` (engine core): the only place randomness becomes fact.
  Named, independently seeded streams replay deterministically (the
  chargen stream replays the same life regardless of interleaving);
  every roll is journaled and citable by monotonic id; a strict
  expression grammar refuses anything malformed; `DiceRollEvent` on a
  new `dice_rolls()` bus channel carries each roll as a fact. Built for
  the RPG module's honesty rule, "the referee cannot roll"
  (docs/RPG_MODULE.md), and generic to any game that wants provenance
  on its randomness.

### Changed
- **Breaking (NPC layer): the engine/game AI boundary is now real.** The
  engine keeps GOAP *mechanism* — planner, A* pathfinding,
  `ExecutorRegistry`, `GOAPPlanExecutor`, and the generic executors
  (PURSUE, SCAN, ESCAPE_BLOCK, GIVE_UP, INVESTIGATE_SMELL). Diet is
  *policy* and left: `EatExecutor`, `GrabPreyExecutor` and `FoodState`
  now live in `examples/predator/ai/`, registered like any game
  behaviour, and `GOAPAction` in the ontology lost EAT and GRAB_PREY (a
  game declares its own actions in its own schema — see
  `examples/predator/schema`). `ExecutionContext` lost
  `mouth_volume_cm3`/`food_state` and gained an opaque `game_data`
  passthrough the engine never reads; `CreatureParams` likewise
  (`eat_duration` is now `action_duration`).
- **Breaking (NPC layer): targets route by declaration, not by name.**
  `goap::Action` gains `target_key`; the brain publishes named targets in
  `CreatureParams.targets` and the plan executor routes by the action's
  declaration. The hardcoded PURSUE/EAT/INVESTIGATE_SMELL/ESCAPE_BLOCK
  name ladder in `build_context` is gone, and with it the engine's last
  knowledge of game action vocabulary.

### Fixed
- **A headless engine no longer starts a GUI application, which made the
  test suite deadlock under `ctest -j`.** `PlatformMacOS` called
  `glfwInit()` from its constructor, so `create_display = false` still
  ran GLFW's Cocoa path, which ends in `[NSApp run]` and blocks on the
  window server's launch handshake. That handshake is session-global and
  serialized across processes: measured 63 ms for one process on an M4
  Max and 550 ms with 32 asking at once, and under load some process
  never received `applicationDidFinishLaunching:` and sat in `mach_msg`
  forever. Every test passed alone; the suite hung on a different test
  each parallel run. GLFW now comes up in `create_window()` and nowhere
  else. `IPlatformSystem::get_time()` is monotonic seconds since the
  platform was created (it was `glfwGetTime()`, same semantics) so that
  reading a clock no longer forces a window server connection. On this
  machine `ctest -j 8` went from intermittent 120 s timeouts to 12 s,
  five runs green, and three concurrent `ctest -j 8` invocations against
  one build directory now finish in 15 s.
- **GOAP: a target at the world origin was mistaken for "no target"**
  (#44). Routing used `food_x != 0` per axis as a presence test, so a
  goal at the origin was silently swapped for the smell target, and a
  goal on one axis produced a coordinate welded from two different
  places — x from the food, y from the smell, a point where nothing is.
  Presence is now the key being present in `CreatureParams.targets`;
  there is no sentinel.
- **GOAP: a plan executor with no registry reported the plan as
  COMPLETED** (#45), the same signal a finished plan gives, so a brain
  picking its next goal on `plan_completed` concluded the creature had
  achieved something while it silently did nothing every frame. Now
  reported as `action_failed` + `needs_replan`, plan left intact.
- **GOAP: an action could start into a world that does not satisfy its
  preconditions.** A failed action correctly withholds its effects, but
  the plan loop ran the next action anyway: the predator AT's control
  caught EAT consuming imaginary food at a place PURSUE never reached.
  Actions now start only if `can_execute(world_state)` holds; a stale
  plan reports `needs_replan`. In-flight actions are exempt (they may
  legitimately consume the state that admitted them).

### Added
- `CapabilityStore` (engine): capability response rules evaluated for ANY
  tracked entity, not just humanoids — the missing piece between
  DamageSystem and the NPC layer (#36 finding, #37 roadmap). Opt-in per
  entity; eager (recomputes on STATE_CHANGE, so emit_event effects fire
  when the state changes); part writes resolve to their creature through
  reverse HAS_PART. The profile is WRITTEN BACK into the KG as
  `capability.*` properties, so a director or an LLM reads what an entity
  can DO from the medium itself. The write-back emits the very event that
  triggers recomputes; the two-layer loop guard is proven by count in
  `test_capability_store` (five wounds = exactly six recomputes).
- The search scene now carries the store's first consumer: a thorn patch
  on the hunt's measured route. Crossing it fires an `on_contact` rule
  (`with_type:Thorns -> wound_leg`), DamageSystem drives the leg below
  the capability rule's threshold, the store recomputes off the bus, and
  the predator finishes the pursuit at half pace — contact rules, damage,
  capability rules, the store and GOAP in one unscripted chain, asserted
  headless (a wounded pursuit never reaches 60% of the healthy sprint)
  and watchable (panel shows leg hp and pace live).
- `at_predator_search`: the full NPC loop in one watchable scene — walls,
  meander, smell, getting lost, dinner. Sensors own the perception facts
  and overwrite them every frame; the planner replans as information
  arrives and the precondition gate breaks plans honestly when it leaves.
  Nothing in the arc is scripted: quartering meander (game-side
  `MeanderExecutor`) until the nose crosses the odor radius, scent
  followed with casting when a wall blocks the line, losses at the radius
  edge (the smell model's own dropout, with hysteresis), a myopic 9 m eye
  so smell-first is geometry rather than script, and a proprioceptive
  stuck-detector because an in-flight action is exempt from the
  precondition gate and can push a wall forever. Headless asserts the
  structure (meandered first, smelled before it saw, fed at the end) and
  runs the control: an odorless carcass produces zero scent events and a
  predator that stays lost. Interactive: SPACE releases / reruns, ESC
  quits, event log with timestamps on the panel.
- `at_predator_hunger_visual`: the hunger loop ON SCREEN — the predator
  walks in, the carcass shrinks bite by bite (FoodState mass driving
  particle scale), and the AI panel shows goal / action / distance /
  grams / bites. Same loop and same `[measure]` numbers headless and
  windowed; glyph pixels and frame brightness asserted, not promised.
  First feature under the project rule that every delivered feature is
  visually verifiable (`docs/testing_guidelines.md` rule 12), which this
  pair now demonstrates: `at_predator_hunger` proves the numbers in the
  headless core, the visual shows them.
- `examples/predator/`: the reference for the AI boundary. A game-side
  diet (EAT bite-by-bite through `FoodState`, GRAB_PREY), its own
  schema declaring its action vocabulary, a `PredatorContext` riding the
  engine's `game_data` slot, and `at_predator_hunger` — a headless AT
  where a hungry predator plans PURSUE→EAT, walks to a carcass AT THE
  WORLD ORIGIN (the #44 regression geometry), eats it bite by bite, and
  a control with no published target that must starve honestly.
- **Breaking (generated ontology):** `TransformationRule.trigger` is now
  typed `TransformationTrigger` instead of `std::string`. Event rules
  answer three separable questions and now have one slot each: `trigger`
  is WHICH engine event source to listen to (a closed enum, because every
  value is a queue the interaction system owns and a game cannot add one
  without engine code), `condition` is WHETHER a given occurrence matters
  (open string, new), and `effect` is WHAT to do (open string, unchanged).
  `condition` generalizes `trigger_profile`, which is the same idea
  hardcoded to a single comparison; new rules should prefer it. KG storage
  is unaffected: rules are still authored as string properties and the
  loader normalizes case.
- Spheres now render at subdivision **1** (80 triangles) instead of 2 (320),
  with **analytic smooth normals** on by default. The G-buffer derives the
  normal per pixel as `normalize(world_pos - centre)`, which decouples shading
  smoothness from triangle count: 80 triangles now render a round sphere that
  looks better than 320 did flat. Quarter of the geometry on every downstream
  cost (surfaces, shadow triangles, acceleration structure input, uploads).
  **Subdivision 2 is retained as a quality setting**, because smooth normals do
  not help the SHADOW: shadow rays hit the real triangles, so a level-1
  silhouette stays an 80-gon and shows on a magnified shadow. Override either
  with `LOGOSPHERE_SPHERE_LOD=<0..4>` and `LOGOSPHERE_SMOOTH_SPHERES=0`.

### Added
- **Contact response**: an `ON_CONTACT` rule can now select on who touched
  you and do something about it. `ContactConditionRegistry` and
  `ContactEffectRegistry` are open the way the capability registries are, so
  bleeding and armour absorption are game effects registered by name and the
  engine ships neither. Built-in conditions `with_type:<EntityType>`,
  `with_part_type:<EntityType>` and `impact_above:<m/s>`; built-in effects
  `knockback:<speed>` and `emit_event:<name>`.

  Conditions ask about the OTHER party and effects act on SELF, so a rule
  says "how I react to being touched by X", never "what I do to X". An
  entity with no rule of its own is unaffected: nothing happens to you that
  your own ontology did not allow.
- `ParticleInteractionSystem::deposit_impulse` / `take_impulse`: a
  pending-impulse inbox, sparse and keyed by stable `KGParticleID`, so it
  costs nothing per particle and survives swap-and-pop. `knockback` deposits
  here and moves nothing, because a KINEMATIC body's position belongs to an
  external writer and the solver will never push it. Whoever owns the
  position drains the inbox and decides what a shove means; ignoring one is
  a legitimate choice.
- `KGModule::getRelatedReverse(id, relation)` walks a relation backwards:
  `getRelated(creature, "HAS_PART")` lists the parts, and this takes a part
  back to its creature. Needed wherever a system starts from something
  physical (a particle, a contact) and has to reach the entity that owns it,
  which was previously impossible at any price. Reads the incoming-relation
  index the KG has maintained since relations existed and nothing ever
  queried; same cost as the forward query, no new bookkeeping.
- `CollisionEvent` now carries the contact itself, not just the fact of one:
  `normal_x/y/z`, `contact_x/y/z`, `penetration`, `approach_speed`, and
  `source_part_id` / `target_part_id`. A consequence needs the geometry and
  the energy (knockback needs the normal, absorption needs the speed, injury
  is per-part), and previously none of it survived the layer boundary. The
  normal comes from the contact manifold, so it is meaningful on walls,
  ceilings and in zero-g; `approach_speed` keeps the solver's sign
  convention, negative when approaching. Groundwork for issue #36.
- `TransformationTrigger.ON_CONTACT` and `TransformationEffect.KNOCKBACK`.
  `ON_CONTACT` is the rigid-contact counterpart of the existing
  `ON_CONTACT_FILTERED`. `KNOCKBACK` deposits an impulse along the contact
  normal into the target's pending-impulse inbox and deliberately does not
  move anything: a KINEMATIC body's position belongs to an external writer,
  so the impulse is delivered to that owner to apply or ignore.
- `tests/test_shadow_lod_wall`: judges LOD by the SHADOW an object casts rather
  than by the object. Four identical spheres at increasing distance from one
  light throw shadows magnified 10x, 3.3x, 1.7x and 1.1x onto a wall, which is
  the case a camera-distance LOD rule cannot see. Interactive (SPACE cycles
  LOD, S toggles smooth normals, ESC exits).
- `logosphere::set_async_gpu_prep()` / `LOGOSPHERE_ASYNC_PREP=1` make the
  async GPU-prep path runtime-switchable instead of compile-time. Default
  is unchanged (off). Proven pixel-identical to the synchronous path by
  `tests/test_async_prep_equivalence.cpp`, and now measurably faster:
  +1.88 ms at retina (9.2%) and +3.43 ms windowed (27.4%). Still OFF by
  default pending a call on the remaining risks; see study journal S14.
- Serialized GPU diagnostic mode: `Logosphere::set_gpu_serialized_diagnostic()`
  or `LOGOSPHERE_GPU_SERIALIZED=1` makes every render pass block until it
  completes before the next is encoded, so each runs alone and its GPU
  timestamp is its true isolated cost. Profiling only: it destroys CPU/GPU
  overlap by construction and measured 2.54x frame time on Eden at retina,
  so read the per-stage split from it and ignore its frame time.

### Fixed
- **`CollisionEvent`'s contact normal pointed the wrong way.** The field is
  documented "unit normal from A toward B" and shipped pointing from B
  toward A, so every consumer written against the documented contract got
  the negated vector. Measured with a mirrored approach (`A` left of `B`
  reported `-x`, `A` right of `B` reported `+x`), so it flipped with the
  geometry and was a genuine inverted convention rather than a constant.

  Consequences, both fixed by the same correction: the humanoid obstacle
  push (`humanoid_locomotion.cpp`) applies `is_a ? -normal : normal` to
  move away from an obstacle and was therefore driving humanoids **into**
  them; and `relative_velocity`, documented "negative = approaching", was
  positive while closing, so anything thresholding on approach speed could
  never fire on a real contact.

  Corrected at the emission site only. The solver's manifold normal is left
  untouched because the constraint jacobians are written for its actual
  sign; `contact_manifold.h` now states that plainly instead of claiming the
  opposite. The composed-corner path already emitted A toward B, so the two
  writers now agree. Locked by `tests/test_knockback_scene`.
- Trees under about 4 m came out as bare poles. Space colonization
  deletes every attractor within `kill_distance` of any node, and that
  distance had a hard floor of 1.5 m while the crown is capped at 60%
  of the tree's height. For a small tree the kill radius was wider than
  the entire crown, so the root node deleted all 80 attractors on the
  first iteration: one segment, every time, for every tree up to 3 m.
  The collapse retry could not help, because it raises `crown_radius`
  and the height cap discards it. Attraction range, kill distance and
  segment length are now fractions of the crown, calibrated so a
  full-size tree lands where it always did (20 m tree: 262 particles
  before, 268 after) while a 1 m tree goes from 4 particles to 218.
  Note that small trees are now as detailed as large ones, since
  `attractor_count` still floors at 80; that is a tuning question, not
  a defect. Regression test:
  `tests/test_tree_collapse_threshold.cpp` (issue #21).
- Entity activation no longer reports a destroyed entity as one that is
  missing chunk coordinates. The activation queue holds entity ids, and
  an id can legitimately die before it is processed: a generator that
  self-queues and is then thrown away by its caller (Logogenesis grows a
  tree, sees it came out collapsed, destroys it and grows another in the
  same breath) left a dead id in the queue. Because a destroyed entity
  returns empty for every property, reading `chunk_x` first made a corpse
  look like a generator that had forgotten to set its coordinates, and
  sent the investigation after the wrong cause. Existence is now checked
  first: a discarded entity is skipped quietly, while a live entity with
  no coordinates stays a loud error and now names its type and the fix
  (`createEntityAtPosition`). Regression test:
  `tests/test_activation_queue_stale_ids.cpp` (issue #21).
- The async GPU-prep handoff copied the frame's entire input TWICE per
  frame: the worker lambda captured the already-copied surfaces and
  particles BY VALUE, constructing 103,914 surfaces and 19,104 particles
  a second time into the closure. Now a move-capture. `render_handoff`
  drops 4.57 to 2.44 ms, which is what turns async prep from net zero
  into a real win.
- `telemetry::GpuWindow` now publishes `start_s` / `end_s`, the absolute
  bounds of a frame's GPU window. Without them, GPU occupancy could only
  be derived by summing per-frame `busy_ms`, which double counts:
  consecutive frames' windows overlap, because the GPU runs about a frame
  behind the CPU. That sum reported 122% of wall clock on Eden at retina.
  Correct occupancy is the union of the intervals, which gives 95.8%.
  The header now states that per-frame windows must not be summed across
  frames, and `tests/test_gpu_occupancy_sanity.cpp` fails if any occupancy
  figure exceeds 100%.
- `earth` setting pack (`schema/packs/earth.yaml`): `Plant`, `Tree`,
  `PhysicsTree`, `Grass`, `GrassPatch`, `Branch`, `Leaves`,
  `FallenTree`, `Rock`, `PhysicsRock`, `Snake`, `Butterfly`, `Totem`,
  with `TreeSpecies`, `OrganicType`, `LogType` and `RockSize`. Eden
  and Logogenesis import it; Logotron needs neither it nor `space`.

### Changed
- Earth-like life and terrain moved out of the base schema into the
  `earth` pack. The core keeps the abstract bases the engine reasons
  about (`LivingEntity`, `Creature`, `NaturalFormation`, `Structure`,
  `Floor`); what moved is the particular. A world has no trees until
  a game asks for them.

### Removed
- The legacy `TreeVariant` enum and its `tree_variant` slot. It baked
  age into the species name (`SAPLING`, `YOUNG_OAK`, `ANCIENT_OAK`
  beside `PINE` and `WILLOW`), so it could not describe a young pine
  at all. `TreeSpecies` carries species and the anatomy slots
  (`canopy_start`, `lower_branches`) carry age. No C++ referenced it.

### Added
- Ontology layering (universe core / setting pack / game extension):
  the generator now discovers setting packs in `schema/packs/`,
  generating a header, registry and namespace for each and staging
  them so any schema importing one resolves. The first pack is
  `space` (`CelestialBody`, `Sky`, `Planet`, orbital slots,
  `CelestialKind`), and those types have LEFT the core: a world has
  no astronomy until a game asks for it. See
  `docs/ONTOLOGY_LAYERS.md`.

### Changed
- `CelestialBody`, `Sky` and `Planet` moved from the base schema to
  the `space` pack. Games that use them import `space` (Logogenesis
  does). A game that does not gets a loud rejection at
  `createEntity` rather than a half-working type — which is what
  makes the layering structural rather than documentary.

### Added
- Shadow acceleration backend seam: `GPURasterizer::shadow_accel_backend()`
  reports whether shadow rays trace a driver-owned structure (`HardwareRT`) or
  engine-built CPU trees (`SoftwareBVH`). Anything building acceleration data
  consults it instead of assuming. Override with
  `LOGOSPHERE_SHADOW_ACCEL=hardware|software`. Ports that add DXR or Vulkan RT
  implement `HardwareRT` and the CPU trees go dormant with no render-pipeline
  changes. See `docs/PORTING_SHADOWS.md`.
- `test_shadow_accel_backend`: guards the seam by contract rather than by pixel
  diff, asserting the CPU trees are dormant under `HardwareRT`, built under
  `SoftwareBVH`, and that the scene is actually lit. Includes an A-vs-A noise
  floor, without which this engine's equal-depth nondeterminism reads as signal.

### Changed
- The CPU shadow BVHs (`TriangleBVH`, `EntityBVH`) are no longer built when the
  platform traces a hardware acceleration structure. They were rebuilt and
  uploaded every frame while `trace_shadows_deterministic` traced the driver's
  structure and never bound them: 2.16 ms of a 21.7 ms Eden frame, and up to
  97 ms on a single frame in a spawning scene. `prep_bvh` is now 0.00 ms under
  `HardwareRT` and unchanged under `SoftwareBVH`. Unaffected:
  `ParticleSystem::shadow_bvh_`, a different BVH over particles that physics
  and animation query.

### Fixed
- Documented (not yet repaired) that the `SoftwareBVH` shadow fallback renders
  no lighting: its output is byte-identical to the same scene unlit. No
  supported target reaches it, which is why it went unnoticed. A port to
  hardware without ray tracing must fix it first.

### Added
- The prince planet: `PlanetGenerator` builds a small bonded-sphere
  world (kinematic core, Fibonacci-sphere crust of stones bonded
  through the constraint API) floating free of the world floor, and
  the base ontology gains the `Planet` type. In Logogenesis,
  `PlanetSeed` is the grandest wish: radius, altitude, crust
  palette, `with_rose` (a red-crowned flower at the pole) and
  `with_prince` (a real physics walker standing at the apex).
- Ontology levers: the knowledge graph is now a control surface, not
  only a record. `solver_authority` (DYNAMIC / KINEMATIC / STATIC) and
  `BONDED_TO` + `bond_strength` are declared on every `WorldEntity`
  through the new `HasSolverAuthority` and `Bondable` mixins, and
  `EntityPhysicalState` resolves them onto the particles an entity
  owns, following HAS_PART so one setting covers a whole body. Engine
  arms it during `initialize()`, so setting a property reaches the
  world immediately in any game, before or after activation.
  `is_at_rest` deliberately has no lever: it is a solver optimisation,
  and a structure held up by it collapses when touched.

### Added
- Logogenesis menagerie: `SerpentSeed` (garden snake / python /
  coral, length and scale colors), `FallenTreeSeed` (trunk / log /
  branch / twigs), and `TotemSeed` (stacked carved wood) join the
  wish grammar, materializing through the engine's existing snake,
  fallen-tree, and totem generators.

### Changed
- Logogenesis persona: the creator is now playful and theatrical.
  Wishes beyond the vocabulary get a decline with flourish plus the
  two or three nearest things it CAN do; questions get thoughts-only
  answers that read the world snapshot. Declines and answers send
  zero ops by contract (locked by AT).

### Fixed
- A bare `cmake -S . -B build` (the README configure line) produced
  an unoptimized engine: no build type meant no `-O` flags, and the
  renderer ran ~8x slower (Eden at 6 FPS instead of ~50). The build
  now defaults to Release when no `CMAKE_BUILD_TYPE` is given;
  explicit build types and multi-config generators are untouched.

### Added
- Isometric camera orbit: `CameraSystem::set_view_azimuth(radians)`
  rotates the isometric view around world +Z (clockwise-positive,
  compass convention; 0 is the classic view, bit-identical to
  before). The whole chain honors the angle: projection and depth,
  mouse picking (both inverse transforms), frustum-culling probe
  directions, the compass widget needle, camera follow offset, and
  GPU temporal shadow reprojection (history invalidates while
  orbiting). Games drive the animation; the engine provides the
  parameter.
- Logogenesis: `OrbitSeed` joins the creative vocabulary. "Orbit
  around the scene" swings the camera through a smoothstep-eased
  revolution (revolutions 0.25-3, duration 2-60 s) and lands exactly
  on its final bearing; the world never moves, only the eye.
- CMake install/export: `cmake --install` now ships the headless
  core, and external projects consume it with
  `find_package(logosphere 0.2)` + `target_link_libraries(app
  PRIVATE logosphere::core)`. Curated headers install to
  `include/logosphere/`; the internal closure they still depend on
  installs under `include/logosphere/internal/` (no stability
  promise there). `examples/consumer-smoke/` is the reference
  external consumer and runs in CI against a fresh install. The
  rendering / physics stack is not installable yet; games needing
  it keep building in-tree.
- High-energy impact energy-budget AT (`test_strata_earth_impact`):
  drops an 8x-mass boulder and audits total mechanical energy
  (kinetic + potential) of boulder + ground every frame. Measured
  today: the deep-penetration impact frame creates 1.19 MJ of
  solver energy (position correction + capped bias velocity across
  many heavy contacts) against a 29 MJ budget; free fall and the
  ballistic ejecta phase conserve energy cleanly. The AT ratchets
  the current scale so escalation fails loudly while the
  dissipation-only contract lands (tracked in issue #5).
- The ontology regeneration toolchain is now vendored:
  `scripts/cppgen/` carries the maintainer-authored LinkML C++
  generator, invoked via `scripts/gen_cpp_header.py`, with
  dependencies declared in `environment.yml`. Contributors can edit
  schema YAML and regenerate the committed sources reproducibly
  (verified byte-identical from a clean environment).
- `PixelBuffer::sync_debug_from_native()`: pulls the native BGRA
  framebuffer into the `EnhancedPixel` debug buffer so tests can
  inspect rendered pixels after a GPU render pass.

### Changed
- The `DEBUG_BUILD` compile definition is gone from every target
  (it was defined `PUBLIC` in all configurations, so release builds
  and downstream consumers carried debug paths). The pixel debug
  buffer is now a runtime opt-in via
  `PixelBuffer::set_debug_mode(true)` with lazy allocation; builds
  no longer pay for it unless a test enables it.

## [0.2.0] - 2026-07-30

First public release. Everything below describes the engine as it
ships today.

### The engine

- **Particle-first world model.** Walls, creatures, trees, terrain,
  fire: all particles with mass, friction, contacts, and
  constraints. Every particle is a node in a queryable knowledge
  graph. The world turtle (an absolute floor at z = 0) is the only
  immovable thing.
- **Software rasterization + Metal compute.** No OpenGL, Vulkan, or
  DirectX: a software rasterizer writes a direct framebuffer, with
  Metal compute shaders for shadow rays, soft shadows, SSAO, SSGI,
  and DDGI probes.
- **Physics V4.** Sequential-impulse solver with SAT face-clipping
  manifolds, speculative contacts, momentum-based sleep/wake, a
  speed-capped Baumgarte push-out, gluon constraint family
  (nail / organic / angular drives) with cluster-aware structural
  damping, and an absolute turtle boundary.
- **Knowledge graph + ontology.** LinkML-defined type system with
  generated C++ registries, runtime extension, schema-validated
  KG operations (the LLM-facing creation grammar), typed event
  journal with reader cursors, and a query algebra with prompt-ready
  renderers.
- **Humanoid locomotion.** Kinematic-root gait (stance foot pinned,
  hips derived), two-bone IK in the committed yaw frame, an
  eyes-head-torso-hips yaw cascade with per-segment time constants,
  twist-step replanting, and a permanent particle write-tracer for
  causal debugging.
- **Worldgen.** Space-colonization trees with species presets and
  growth time-lapse, grass with painterly clustered distribution,
  rocks (scenery and gluon-bonded physics boulders), layered
  strata ground with a settle-based earth preset, streamed chunked
  terrain, butterflies, and full humanoid rigs.
- **Celestial system.** Sun, moons, and stars as real orbiting
  particles far enough that only their light enters the frame;
  color and emission curves keyed to the day fraction; time
  acceleration with exact-hour arrival.
- **LLM integration.** Engine-side HTTP client (Anthropic, OpenAI,
  local servers) with prompt caching support, plus the KG-ops
  grammar that lets a model create and mutate world state under
  schema validation.
- **Three example games.** Logogenesis (conversational world
  creation), Eden (knowledge-garden tableau), Logotron (light-cycle
  arena with an LLM director).

### Known issues

- Small blockers close to the ground produce a sub-pixel penumbra
  kernel, so their shadow edges render hard instead of soft
  (`test_shadow_penumbra_softness` documents the collapse and
  ratchets it from regressing further).
- No CMake install/export surface yet: Logosphere builds in-tree and
  games live in `examples/`; `find_package(logosphere)` consumption
  is the first post-release packaging milestone.

- A humanoid crossing a step snaps up or down it in a single frame
  instead of climbing. `step_climb.boost` sets a 3.93 m/s climb
  velocity that `shape.ground_correct` cancels on the same frame,
  every frame, and the height change is finally applied as a one-frame
  ground snap at the edge. Because the foot is planted while the hips
  jump, the leg visibly pops at the knee: hips-to-foot distance changes
  0.26 to 0.35 m in one frame. Documented as known-red smoothness
  checks in `test_humanoid_terrain_scenarios` (issue #30).
- A walking humanoid is not stopped by a wall taller than she can
  step onto. Animation writes the hips through the face, and once she
  is inside, the depenetration pushes from every wall tile she
  overlaps sum and drive her deeper, reaching more tiles: measured
  0.73, 1.16, 3.07, 3.73 m in consecutive frames, ending past the far
  side of a 12 m wall and below z = 0. Documented as a known-red
  scenario in `test_humanoid_terrain_scenarios`, which reports it on
  every run without gating; it flips to gating when the fix lands
  (issue #29).
- A heavy boulder impact on layered ground can ripple outward into
  an oversized explosion of tiles. Physics work in progress; the
  crater contract (local splash, far field still, bedrock intact)
  is enforced by test at moderate energies.
- GPU frame stalls during BVH rebuilds on chunk streaming under
  very high particle counts.
- The ontology regeneration toolchain (LinkML C++ generator) is not
  yet published; generated sources are committed, so regeneration
  is only needed when editing schemas.
- Windows builds of the headless core are structurally supported
  but untested.
