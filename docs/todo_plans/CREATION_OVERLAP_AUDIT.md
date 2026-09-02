# CREATION-OVERLAP AUDIT — INV-37 (owner decree 2026-09-01)

Owner: "under no circumstances, any creation of particles should be
allowed to overlap in space with another" and "dispatch to audit if this
is happening anywhere, we should have an assert/except for any
creation-overlapping moment."

Three read-only audits of the tree at the INV-37 commit, every claim
with file:line. Nothing built, nothing run; overlaps are derived from
the literals in the code. The chat delivery carries the conclusions;
this file is the archive.

## 1. The engine: where a birth can be refused

**One choke point exists.** Every path that puts a particle into the
live array ends in `ParticleSystem::add_particle`
(`src/core/particle_system.cpp:119`, the `push_back` at `:170`):

| Entry point | Callers | Funnels through |
|---|---|---|
| `queue_particle_addition` `:357` → `flush_pending_particles` `:370` → `add_particle` `:374` | organic floor generator, `humanoid_locomotion.cpp:2118`, tests | `add_particle` |
| Wrappers `create_static_particle` `:513`, `create_light` `:523`, `add_particle_to_entity` `:554`, `create_ground_particles` `:608`, `create_floor_grid` `:652`, `create_*` `:734-851`, `add_particle_relative` `:1053` | butterfly, snake, rigid assembly, celestial | `add_particle` |
| `Engine::add_particle` `src/core/engine.cpp:1998` | strata generators, app scenes | `add_particle` |
| Chunk sync `ChunkSystem::create_chunk` `src/worldgen/chunk_system.cpp:343`; async apply `apply_ready_chunks` `:593` (main thread; the worker only prepares) | worldgen | `add_particle` |
| Entity activation `scene_chunk_generator.cpp:508` | chunk drain | `add_particle` |
| Generators direct: humanoid `:1159,1204,1352,1450`, rock `:159`, tree `:975`, scene_manager `:213`, test_context | worldgen / tests | `add_particle` |
| `PhysicsSystem::add_particle_with_gluon_to` `physics_system_v4.cpp:6571` → `:6606` | bonded generators | `add_particle` |
| KG store `setKGParticleData` `kg_particle_store.cpp:76` | stores a copy; the live insertion is a chunk row | — |

Structural bypass in principle only: the mutable `WriteView::get_particles()`
(`particle_system.h:90`, friends `:324-334`); no code inserts through it.

**No overlap check exists at creation today.** What exists:
`assert_above_turtle` (`particle_system.cpp:71-117`, turtle only, abort
unless `TURTLE_LENIENT`), the KG twin (`kg_particle_store.cpp:57-74`), the
opt-in placement queries `can_place_at` / `try_place_with_retry`
(`:1123-1193`, two generator callers, not a gate), and the harness test
`tests/test_no_overlap_at_creation.cpp` (issue #38: a tree swept for
overlaps after generation, audited `expect: pass`). INV-30's frame-start
sweep has no code.

**The creation door is on an unmerged branch**,
`origin/feat/creation-overlap-door` (15 commits, 27 files, +1712/-240,
including its own registry rows that collide with ours: its G-48 is the
door, ours is the stack). On that branch `add_particle` enrols newborns,
`flush_pending_particles` runs `audit_creation_overlaps()` on the
previous batch (BVH over all bodies, exact pair tests against SLOP),
prints `[CREATION VIOLATION]`, aborts only with
`LOGOSPHERE_CREATION_STRICT`, exempts bonded structures unless
`LOGOSPHERE_CREATION_STRICT_STRUCTURAL`, and never removes the body.
That is not INV-37's behaviour.

**Where the refusal belongs, and its hazards.** Inside `add_particle`
before the `push_back`: the only point every path crosses. A flush-located
door misses the direct adds (chunk `:343/:593`, activation `:508`,
generators, physics `:6606`) until the next flush, and headless runs never
flush unless the caller does (`flush_pending_particles` is called from
`Engine::render` `:1479` and `chunk_system.cpp:666` only). Data: the live
array, the world BVH, `SLOP = 0.001 m` (`physics_constants.h:87`).
Hazards: the BVH physics reads (`shadow_bvh_`) is rebuilt only in
`update_bvh()` when dirty; adds between rebuilds are not in it (the branch
rebuilds at flush: 20.4 ms for 12,440 bodies); newborns in one batch must
also be tested against each other; the pending queue is a bare
`std::queue` (`particle_system.h:357`) pushed without a lock; bonds are
registered after bodies, so a per-add door cannot know structure
membership. INV-37 makes the last point irrelevant: no exempt class.

## 2. The generators and Eden's scene builder

Eden's floor is three layers (0.30 + 0.15 + 0.10, tiles 4 x 4), top at
**0.55 m** (`examples/eden/src/main.cpp:1204-1222`); 0.30 in the trench
(`:1226-1232`). Tiles are created synchronously at `:1234` before every
ad-hoc spawn below, so the ad-hoc body is always the second arrival.

| Site | Creates | Height reference | Overlap, derived | Verdict |
|---|---|---|---|---|
| `main.cpp:1257-1259` three red cubes | 0.5 x 0.5 x 1.0 | literal z 0.6 | spans 0.10..1.10 vs floor 0..0.55: 0.45 m inside | mistake |
| `main.cpp:1273-1275` A/B cubes | 0.3 x 0.3 x 0.8 | literal z 0.6 | 0.35 m inside | mistake |
| `main.cpp:1304-1316` cairn | 5 boxes | literal `floor_top = 0.55` | touching, 0 | hard-coded, correct today |
| `main.cpp:1333-1355` plateau | 27 boxes | literal `floor_top = 0.55` | touching, 0 | hard-coded, correct today |
| `main.cpp:1373-1374` Eva, `:1818-1819` three NPCs | humanoid, foot 0.08 thick | literal z 0.5 | foot 0.50..0.58 vs top 0.55: 0.05 m inside | mistake |
| `main.cpp:1668-1670` pole | 0.1 x 0.1 x 5.0 | literal z 2.5 | 0.0..5.0: 0.55 m inside, all three layers | mistake |
| `main.cpp:1736-1738` 80 scattered rocks | 0.15..0.47 x 0.15..0.33 x 0.10..0.26 | literal `rock.z = 0.15` | 0.02..0.28: fully inside the bedrock layer, 0.27..0.40 m under the surface | mistake (the G-67 census population) |
| `main.cpp:1764-1765` ruin north wall, 6 blocks | 0.4 x 0.3 x 1.0..1.4 | `z = th/2 + (i%2)*0.1` | bottom 0.0 or 0.10: 0.45..0.55 m inside | mistake (comment assumes floor at 0) |
| `main.cpp:1779-1780` ruin west wall, 4 blocks | 0.3 x 0.4 x 0.8/1.1 | `z = 0.5 + (i%2)*0.15` | 0.45 m inside | mistake |
| `main.cpp:247` keyboard serpent segments | size 0.5, KG-stored | literal `1.0 + sin*0.5` | 0.30 m inside if materialised (activator not traced) | mistake |
| `main.cpp:1711-1716` trees, `:1800-1803` grass, `:1841-1846` serpents | deferred | `GroundLocator::locate` (`ground_locator.cpp:105-108`, reads the tile top) | none | correct |
| `main.cpp:1859-1863` butterflies, `:429-704` mouse spawns | | `surface_at` / `ground_here` | none | correct |
| `chunk_system.cpp:309-334` sync, `:591-593` async | whatever the callback prepared | no audit of existing bodies | a chunk re-created after unload (Eden unloads at 70 m, `main.cpp:1181`) is born around any ad-hoc body left there: they are `AutoParticle`, not chunk-tracked (`particle_system.cpp:127`) | mistake; G-68's case, ruled refused |
| `strata_floor_generator.cpp:183-207` | tiles | `base_z + th/2`, stacked | none with its own layers; no check against pre-existing bodies | mechanism gap |
| `physics_tree_generator.cpp:361-380` main branches, `:497-499` children | boxes, siblings start at one point | structural | sibling-sibling and child-parent at the junction; the board's 51 pairs, deepest 0.85 m (not re-measured) | **by design** |
| `physics_tree_generator.cpp:683-689` leaves | | `try_place_with_retry(gap 0.02)`, skipped on failure | none at birth | correct |
| `physics_tree_generator.cpp:926,325-328,1136-1147` plate, trunk, roots | | caller z | touching | correct |
| `organic_generator.cpp:383-410,216` grass stem segments | consecutive segments at `length * 1.1` | caller z | 10 % overlap at every seam; tilted blades dip their bottom corners below the surface | **by design** (seam) / mistake (tilt) |
| `organic_generator.cpp:557-582` grass foliage | 1 mm leaves around the stem | no check | leaf-leaf and leaf-stem possible | mistake |
| `physics_rock_generator.cpp:333-341,354-360`, `:153-159` | core + satellites, 2 cm in-plane gap | caller z | tilts of 0.1..0.2 rad not widened by the check: a 0.4 m core dips ~2 cm into the floor; `:153-159` checks then adds anyway on failure | mistake |
| `humanoid_generator.cpp:1271-1281` | thigh "visually inside the hip region" | caller z | internal box-box; the board's 23 penetrating boxes | **by design** |
| `snake_generator.cpp:179,198,218` | head, segments, tail | caller z | none | correct |
| `examples/predator/at_predator_hunger_visual.cpp:144-181` | tiles 8 x 8 x 0.8 at z 0.4; predator sphere 1.6 at z 0.8; carcass 1 m cube at z 0.6 | literals | sphere 0.8 m inside the tile; carcass 0.7 m inside | mistake |
| `examples/predator/at_predator_search.cpp:203-204` | tiles at z -0.4, thickness 0.8 | literal | below the turtle (reported, not refused) | mistake |
| logotron bike pad, minimal | | literals | none | correct |
| logomanpac, logotriste, logogenesis | no Particle construction | | | not found |

Answers. (1) Certainly born in overlap today: Eden's three red cubes, the
A/B cubes, the pole, the 80 rocks, the 10 ruin blocks, Eva's and the
NPCs' feet by 5 cm, every tree crown, every grass blade's seams, every
humanoid's hip/thigh boxes; the predator examples' sphere and carcass;
the predator search floor below the turtle. (2) By design, to be
redesigned under INV-37: tree crown junctions, grass stem seams, the
humanoid's hip/thigh nesting; the rock clump's tilt-blind check and its
add-anyway path. (3) Hard-coded ground instead of the real surface:
Eden `:1257, :1273, :1304, :1333, :1374, :1668, :1736, :1765, :1780,
:1819, :247`; both predator files. Everything deferred or mouse-spawned
in Eden queries the locator and is correct.

## 3. The humanoid, the KG spawns and the test suite

Placement law for every gluon-attached part: `pb = pa + offset_a -
offset_b` (`physics_system_v4.cpp:6597-6601`). Depths are the min-axis
AABB depth, the same measure `test_no_overlap_at_creation.cpp:77-83` uses.

**Two humanoid builders.** Eden, logotron, logogenesis and ~45 tests use
`generate_humanoid_physics` (29 boxes with eyes). The KG path
`generate_humanoid` (`humanoid_generator.cpp:511-1076`) is the "23 unbonded
boxes": its constraints are KG entities loaded by `load_constraints_from_kg`,
an empty stub (`physics_system_v4.cpp:6416-6418`); its only caller is
`create_and_activate_eva` ← `tests/test_stiffness_stability.cpp:566`.

| Site | Born | Offsets / reference | Overlap | Depth | Declared? |
|---|---|---|---|---|---|
| `humanoid_generator.cpp:1884-1885` (+R `:1950`) | upper arm vs shoulder cube | arm top at the shoulder centre | yes | Eva 22.5 mm, default 30, hunter 37.5 | no |
| `:1660` vs `:1678-1679` | upper hair vs back hair | | yes | 9.1 mm | no |
| `:1735,1746` + `.h:62` | eye plates vs head | `eye_forward_offset = -0.005` | yes, by design ("embedded, flush") | 5 / 3 mm | comment only |
| `:1346,1444,1397-1636` | feet, legs, spine, head | edge-to-edge | touching | 0 | "no overlap" comments |
| KG `:843-868` | upper arm vs shoulder | `arm_z = shoulder_z - 0.15` | yes | 30 mm | no |
| KG `:814-838` | ears vs head | | yes | 20 mm (x2) | no |
| KG `:782` vs `:799` | hair vs hair | | yes | 10 mm | no |

The physics rig's 7 overlapping pairs are NailGluon-bonded and unioned
(`physics_system_v4.cpp:927-935`), so their contact rows are denied
(`:1381-1385`) except when a member is KINEMATIC (`:929-931`), which
locomotion sets at registration (`humanoid_locomotion.cpp:1870-1872`).
The KG rig's 5 pairs have zero bonds.

| Game-layer / KG spawn | Reference height | Overlap | Depth |
|---|---|---|---|
| Eden Eva + 3 NPCs `main.cpp:1373-1374, 1818-1819` | `world_z = 0.5` vs strata top 0.55 | yes, feet in the organic tile | 50 mm |
| Eden rocks `:1736` | `rock.z = 0.15` | yes, inside bedrock | up to 0.20 m |
| logogenesis `logogenesis_app.h:1050-1052, 1114-1115` | `surface_z + 0.1` | no | |
| logotron Program `program_actor.cpp:42-44` | `z = 0.5`, grid top 0.05 | no (the main floor slab `:1122` not read) | |
| chunk activation `chunk_system.cpp:351-356, 601-606`, `scene_chunk_generator.cpp:526-531` | the stored Particle verbatim | no neighbour check; turtle only (`kg_particle_store.cpp:56-72`) | |
| logovger, voyager, predator, logomanpac src | no add_particle | not found | |

**Tests that birth bodies in overlap on purpose:** `scene_jammed_sleep.h`
A and D (refusal asserted under INV-37); C's 10 cm interlock (a waiver
that CONTRADICTS INV-37's "no class list" - restaged as a drop, see §4);
`test_solver_residual.cpp:71-78,130,202` (two boxes 40 % interlocked, an
instrument); `test_humanoid_movement.cpp:340-344` (grass "rooted IN the
floor slab", comment only).

**Tests that bury bodies by accident, none declaring it:** fourteen
humanoid tests spawn at `world_z = 0.0` on a floor slab whose top is 0.10,
feet 80 mm inside (`test_eva_movement.cpp:397-411`,
`test_humanoid_ground.cpp:70-102`, `test_humanoid_ground_multitile.cpp:83,120`,
`test_hunter_rotation.cpp:649-660`, `test_humanoid_impact.cpp:742-779`,
`test_animation_isolated.cpp:76,117`, `test_animation_layering.cpp:78,108`,
`test_animation_primitives.cpp:71,103`, `test_bilateral_animation.cpp:100,129`,
`test_damage_visual.cpp:59,88`, `test_foot_planting.cpp:66,100`,
`test_humanoid_movement.cpp:169,203`, `test_idle_run.cpp:81,116`,
`test_leg_primitives.cpp:69,101`); `test_humanoid_strata_integrity.cpp:283-284,349-350`
(50 mm). Touching, correct: `test_grass_yields`, `test_divergence_microscope`,
`test_grass_natures` (humanoid at 0.1 on a 0.10 top); the stack, torsion and
mixed-mass scenes ("born touching, never overlapped").

**Existing guards.** `tests/test_no_overlap_at_creation.cpp` sweeps a
generated tree and then returns true on both branches (`:224-238`,
"PASS (diagnostic)", the 2026-08-02 policy "minimal overlap is accepted":
rejecting a colliding branch dropped its subtree, 149 bodies became 59);
the audit row credits it with proving INV-4. It cannot fail. Nothing
audits humanoid or game-layer creation. Only `scene_jammed_sleep` A and D
assert a refusal, red until the door lands.

## 4. The guardrail: an assert at the creation moment

**The mechanism.** One refusal inside `ParticleSystem::add_particle`,
before the `push_back`: query the live bodies the newborn's AABB touches
(the BVH, refit incrementally or rebuilt per batch), run the exact
box-box test against SLOP, and on overlap DROP the body with a
`[PHYSICS REFUSED] add_particle(P<new>): overlaps P<k> by <mm> mm` line
naming both. No lenient switch, no class list (INV-37). Same-batch
newborns are tested against each other before the batch is committed.
Cost model to measure before shipping: the branch's flush audit was
20.4 ms once for 12,440 bodies with a full BVH rebuild; per-add with an
incremental BVH refit is the target, and the headless bench with the
fixed instrument is the jury.

**Its test.** `test_jammed_sleep` A and D already assert the moment (the
offending body absent). `test_no_overlap_at_creation` asserts now (measured
the same day: 2 of 11,781 pairs overlap, worst 0.343 m, red) instead of
reporting, and its audit row says so; case C was restaged as a drop the
same day (born legal, green). A humanoid creation test is owed (the 7 rig
pairs).

**Order of the source fixes, each red before it is touched:**
1. Eden's literal heights against the 0.55 m floor (rocks, cubes, pole,
   ruin, Eva and the NPCs): read the ground locator like the deferred
   spawns do. Removes the frame-collapse population (G-67).
2. The chunk re-creation around leftover ad-hoc bodies (G-68): the door
   refuses the tile; the game must place ad-hoc bodies as chunk-tracked
   entities or re-place them.
3. The humanoid rig's arm-shoulder and hair-hair overlaps (22.5-37.5 mm,
   9 mm) and the eye plates (3-5 mm, by design): offsets, not exemptions.
4. The fourteen humanoid tests at `world_z = 0.0` and strata_integrity.
5. The generators by design: tree crown junctions, grass stem seams and
   tilted blades, grass foliage, the rock clump's tilt-blind check and
   add-anyway path, the predator examples.

**What the decree contradicts, on the record:** the 2026-08-02 policy that
accepted minimal overlap in trees ("the complexity IS the tree"). Under
INV-37 a crown generator that draws branches through siblings is refused
branch by branch until it places them; that is the redesign the board
already named.

## 4. The guardrail: an assert at the creation moment

To be written from the three audits: the refusal inside `add_particle`,
its cost model, its test (the moment itself, headless), and the order of
the source fixes.
