# Changelog

All notable changes to Logosphere are documented here. The format
follows [Keep a Changelog](https://keepachangelog.com/); versions
follow [Semantic Versioning](https://semver.org) on a 0.x line
(minor versions may break public API until 1.0).

## [Unreleased]

### Added
- **Progression, attribute groups and possessions in the rulebook meta-pack.**
  `ProgressionTrack` / `ProgressionStep` / `ProgressionStanding` model a ladder
  of standing within a profession and where a character stands on it;
  `AttributeGroup` plus `ModifyAttributesInGroup` express "reduce three
  physical characteristics by 2", which names a count and a group rather than
  an attribute; `Possession` / `GainPossession` / `PossessionHolding` cover
  what a book hands out that is neither money nor a skill. Standing and
  holdings are state, so they carry no citation, matching `SkillRating`.

### Changed
- `TaskCheck`'s attribute and its modifier lookup are now optional, and
  travel together: neither, or both. Cepheus prints re-enlistment as a
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
