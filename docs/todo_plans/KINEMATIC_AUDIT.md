# KINEMATIC audit — census, verdicts, and the WAKE_RESOLVER flip

Read-only survey. Branch `feat/rube-goldberg-machine`, HEAD `c02979e`.
No source was modified. Every claim below cites `file:line`. Where a
claim is inference rather than measurement it is labelled INFERRED.

## The standard audited against

`ParticleSolverMode::KINEMATIC` means: an external writer owns this
body's position, so the solver must not move it. Two tests per site,
the second added by owner amendment mid-audit:

1. **SET** — is something genuinely writing this body's position while
   it is KINEMATIC?
2. **RELEASE** — does the body return to DYNAMIC when that writer
   stops, and does code exist that performs the release?

A site that sets KINEMATIC and never clears it is a permanent pin.
That is the immobility trick, regardless of the original reason.

### Finding 0 — the repo's written doctrine contradicts the audited doctrine

This is the single most important finding, and it reframes bucket B.
**Three normative sources in this repo declare KINEMATIC to be the
sanctioned permanent immobility mechanism**, not a transient state:

- `tests/invariants/INVARIANTS.jsonl` INV-1: *"immobility exists only
  through the turtle, gluon anchors, or **KINEMATIC solver mode**."*
- `schema/logosphere.yaml:1184-1191` (`solver_authority`): *"Set
  KINEMATIC to make something **genuinely immovable** — terrain, a
  floor, a world. Never try to hold a structure up by leaving it still;
  stillness is an optimisation the physics undoes on contact."*
- `include/logosphere/kg/entity_physical_state.h:28-32`:
  *"solver_authority — who owns a body's position. **The only honest way
  to make something immovable.**"*
- Corroborating, `schema/logosphere.yaml:56-58` (the HEAVY_STATIC
  deprecation): *"to make a body genuinely immovable **set
  solver_authority KINEMATIC**."*

Every bucket-B site below cites this doctrine in its own comment.
`src/worldgen/physics_tree_generator.cpp:917-918` quotes the licence
verbatim — *"KINEMATIC is one of the three sanctioned immobility
mechanisms"* — to justify a permanent pin.

**So under the owner's doctrine these sites are the disease; under the
repo's written doctrine they are compliance.** The authors were not
being sloppy, they were following the spec. Both readings are reported
below. Reconciling the text with the doctrine is prerequisite to acting
on bucket B, and it is a bigger edit than the code: the schema, the KG
header, and INV-1 all move together or none do.

---

## 1. Census

Generated ontology files (`src/generated/*`, `examples/*/src/generated/*`)
declare the enum and are excluded as noise — 7 declarations x 11 files.

| Bucket | Count | Where |
|---|---|---|
| **A** legitimate escape hatch | 8 | 6 in `tests/`, 2 in `examples/` (both predators) |
| **B** immobility trick | 11 | 6 `src/worldgen/`, 5 `src/animation/` (see §2) |
| **C** scenery / floor | 74 | `tests/` almost entirely |
| **D** game layer | 4 | `examples/` (2 partial, 2 pinned props) |
| **E** solver reads | 41 | `src/core/physics_system_v4.cpp` |

Plus 9 read-only assertions in `tests/` (listed in §4) and 4 in
`examples/logogenesis/tests/at_logogenesis_creation.cpp`.

Write-side totals, the number that matters for the transient test:

| | SET sites | RELEASE sites |
|---|---|---|
| `src/` (non-generated) | **12** | **4** |
| `examples/` (non-generated) | 6 | 0 |
| `tests/` | 80 | 9 |

The four releases in `src/` are all in one file,
`src/animation/humanoid_locomotion.cpp:1899, 1984, 2453, 2539`. There is
no release anywhere in `src/core/`, `src/worldgen/`, `src/interaction/`,
or `examples/`. Established by:

```
grep -rn "solver_mode = ParticleSolverMode::DYNAMIC" src/ examples/
```

whose only other hit is the struct default at `src/particle_core.h:167`.

One indirect release path exists: the KG door at
`src/kg/entity_physical_state.cpp:42` rewrites every particle of an
entity when the KG property `solver_authority` is set. Nothing in
`src/animation/`, `src/worldgen/`, or `examples/` ever sets that
property; the only setters in the repo are
`tests/test_ontology_levers.cpp:113, 149, 154, 183`. It is a lever a
game *could* pull, not a release the engine performs.

---

## 2. Bucket B in full — the immobility tricks

This is the action list.

### B.1 `src/worldgen/` — six permanent pins, zero releases

All six set `solver_mode = KINEMATIC` **and** `is_at_rest = true` at
generation. Nothing writes their positions afterward and nothing
releases them.

| # | Site | Body | Verdict |
|---|---|---|---|
| B1 | `src/worldgen/planet_generator.cpp:65-66` | planet core sphere | pinned |
| B2 | `src/worldgen/planet_generator.cpp:138-139` | every surface stone | pinned |
| B3 | `src/worldgen/organic_generator.cpp:97-98` | plant trunk base | pinned |
| B4 | `src/worldgen/organic_generator.cpp:178-179` | crown root (trunkless plants) | pinned |
| B5 | `src/worldgen/physics_tree_generator.cpp:919-920` | tree root plate, direct path | pinned |
| B6 | `src/worldgen/physics_tree_generator.cpp:1302-1303` | tree root plate, KG/streaming path | pinned |

**B2 is the best-argued of the six and deserves reading before it is
touched.** The comment at `planet_generator.cpp:131-137` records a
measured failure: a ~15 kg body part at 1.5 m/s cleared the wake
threshold against a 1.35 kg grain, `wake_particle_with_propagation`
cascaded to neighbours, and *"six thousand stones began falling in -Z at
once."* The conclusion drawn — *"A world you can stand on must own its
position; that is what KINEMATIC means"* — is the honest reading of the
old doctrine and the wrong reading of the new one. Nothing owns those
stones' positions. They are pinned.

**Honest mechanisms available today** — and each carries a caveat that
must be read before it is recommended:

- **Pin gluon (`NailGluon`).** Real and tested:
  `include/logosphere/physics/physics_system.h:288`, created via
  `add_gluon_between` (`:487`), worked example at
  `tests/test_pin_gluon_holds_particle.cpp:56-101`, production use at
  `src/animation/humanoid_locomotion.cpp:2028-2110`.
  **Caveat that changes the recommendation: the pin gluon's anchor is
  itself a KINEMATIC particle that is never released**
  (`tests/test_pin_gluon_holds_particle.cpp:80`;
  `humanoid_locomotion.cpp:2074`). Swapping a tree's root plate for a
  pin gluon **relocates** the permanent pin to a hidden 1 cm body — it
  does not eliminate it. No particle-free world-space anchor API exists;
  searched, none found.
  Its genuine advantage is force-boundedness: the anchored body stays
  DYNAMIC, receives gravity and impulses, and can be torn loose via
  `breaking_force`. A tree could be knocked down. The current plate
  cannot.
- **Turtle boundary.** **Measured as unavailable for B5/B6.**
  `physics_tree_generator.cpp:900-903` records that a plate resting
  exactly on the turtle is not supported at all — *"the boundary only
  corrects a body whose bottom goes BELOW it, so nothing held it up. It
  fell … P0 vel 0 -> -0.0817 -> -0.0996, accelerating."* And `:875-886`
  records that burying it makes the boundary an energy source
  (*"d_turtle +30.6 EVERY SUBSTEP, +15797 on the first"*). The turtle is
  a catch-net, not a floor you can stand a tree on. Recommending it here
  would be recommending a mechanism the repo has already falsified for
  this exact body.
- **Bonding.** `BONDED_TO` + `bond_strength`, resolved by
  `src/kg/entity_physical_state.cpp:80-105`. Cannot root anything on its
  own: `src/core/physics_system_v4.cpp:762-763` states *"Connectivity
  runs through DYNAMIC bodies only. An immovable anchor is ground, not
  structure"* — a bonded chain still needs one immovable end. Note also
  that the schema's own cited exemplar does not do what it claims:
  `schema/logosphere.yaml:57-59` points at the strata generator as
  "STONE plus bonding", but `src/worldgen/strata_floor_generator.cpp:204`
  uses `is_at_rest = true` and neither KINEMATIC nor visible bonding —
  and `is_at_rest` is declared not-immobility by two other sources.

**The release mechanism the doctrine asks for already exists and is
unused.** `EntityPhysicalState::apply_solver_authority`
(`src/kg/entity_physical_state.cpp:34-78`) accepts
`"KINEMATIC"|"STATIC"|"DYNAMIC"`, walks `HAS_PART` recursively (`:50`),
works live and pre-activation (`:55-76`), and wakes the body on release
(`:64-66`, `:74`). It is armed at engine init
(`src/core/engine.cpp:445-453`) and proven bidirectional by
`tests/test_ontology_levers.cpp:140-160`, whose header reads *"A lever
is not a one-way door: an agent can let go again."*

`grep -rn "solver_authority" src/worldgen/` returns **zero hits**. Every
bucket-B worldgen site bakes the mode into the `Particle` struct
directly, bypassing the lever — which is precisely *why* nothing can
release them. The transient mechanism is built, tested, and wired to
nothing.

**Cost of switching, per site:**
- B3/B4 (plants): without a substitute, `organic_generator.cpp:88-90`
  measured **20 of 20 short blades and 2 of 15 tall blades adrift**,
  plus divergence at `:194-197` (*"16k -> 4.5e8 in one frame; grass
  shrapnel at ~100 m/s"*).
- B5/B6 (tree plates): the plate resumes the measured sink; the
  substitute must be a pin gluon, so the pin moves rather than
  disappears. `tests/test_physics_tree_roots.cpp` and
  `tests/test_no_overlap_at_creation.cpp:137` read the plate and need
  re-baselining.
- B1/B2 (planet): largest by far. 6000+ stones become DYNAMIC, each
  needing bonds to the core that `planet_generator.cpp:71-73`
  deliberately deleted. The failure is already measured (the
  six-thousand-stone cascade). **Two acceptance assertions break by
  construction**:
  `examples/logogenesis/tests/at_logogenesis_creation.cpp:1204-1207`
  (`dynamic_crust == 0`) and `:1246-1249` (`worst < 0.01f`). The code
  already names the intended design at `planet_generator.cpp:73-75` —
  *"flipping a region back to DYNAMIC and bonding it then"* — which is a
  per-region `solver_authority` call that exists today.

### B.2 `src/animation/` — pins that survive the driver

The animation layer is where bucket A and bucket B are hardest to
separate, because the same file does both correctly and incorrectly.

**What is genuinely released** (these work, and are why walking works):
`humanoid_locomotion.cpp:1547` stamps every particle of the humanoid at
registration; `:1899` releases both leg chains and `:1984` releases the
12 upper-body joint children, both called unconditionally from `:1661`
and `:1673`. That is a real, total release for those particles.

**What survives the release:**

| # | Site | Body | Problem |
|---|---|---|---|
| B7 | `humanoid_locomotion.cpp:1547` residue | **the hips**, plus hair, ears, and the four eye particles | Never a joint *child* and in no limb list, so neither release site reaches it. Permanently KINEMATIC. |
| B8 | `humanoid_locomotion.cpp:307` | every ANIMATION-owned bone | Fires at `fk_time_ms >= fk_active_clip->duration_ms` — the exact moment driving *stops*. Hands `owner` back to DYNAMICS at `:306`, then pins `solver_mode` on the next line. The release site sets the pin. |
| B9 | `humanoid_locomotion.cpp:6138` | every FK-written joint child, every frame | Guarded only by `FK_KINEMATIC_OFF` being unset, i.e. on by default. Its own comment at `:6130-6132` claims drive enables restore DYNAMIC; the `continue` at `:6083` skips drive children before they reach the stamp, so that restoration is structurally impossible. Inert for a default humanoid (all 20 auto-registered joints are drive children), live for any joint added via `register_joint` at `:2565`. |
| B10 | `humanoid_locomotion.cpp:2131` | all ANIMATION-owned particles | In `reset_animation_owners`, whose header comment at `include/logosphere/animation/humanoid_locomotion.h:423` promises *"Return all animated particles to DYNAMICS ownership"* and whose body sets KINEMATIC. **Zero callers** — dead code that teaches the inversion. |
| B11 | `src/animation/butterfly_flight.cpp:116-117` | the entire butterfly: head, abdomen, every thorax segment, every wing | Unconditional at registration. `unregister_entity` at `:129-138` erases the tracking struct without unpinning. No release exists anywhere. |

**B7 is the worst single site in the repo.** The hips are the humanoid's
root — every other part is snapped to them via `rest_offsets`
(`humanoid_locomotion.cpp:5153-5160`). KINEMATIC is a hard door, not a
hint: `physics_system_v4.cpp:4438` skips integration, `:502` returns
inv-mass 0 for momentum, `:3872` returns 0 for positional repair,
`:1683` skips both-KINEMATIC gluon pairs. A humanoid on this branch
**cannot be knocked over, cannot ragdoll, and cannot fall**, because its
root has infinite mass to the solver and nothing hands it back.

There is no ragdoll, collapse, or death-handoff path to do the handing
back. `TransformationEffect::KNOCKBACK` exists as an enum value in
`src/generated/space_ontology.h:617` with no implementation anywhere
(`grep -rn "TransformationEffect" src/ | grep -v /generated/` returns
nothing).

`tests/test_humanoid_impact.cpp` is the test that would have caught
this. It throws a boulder and asserts `max_hips_displacement > 0.001f`
at `:381` and `> 0.05f` at `:382`. The hips term is structurally zero;
only the chest can move, and only because `:1984` released it. The
test's own fallback strings attribute the result to friction (`:388`)
and to a *"planted stance"* (`:394`). INFERRED, but strongly: nobody has
connected those messages to the infinite-mass root.

**B11 (butterfly) is arguably the honest case.** Flight does own the
butterfly's position continuously and forever, so there is no
"driving stopped" moment being ignored. The debt is bounded: a butterfly
can never be swatted, land, or die. Combined with `collides_with = 0u`
at `:106`, a registered butterfly is outside physics in both directions.

---

## 3. SET vs RELEASE — the transient-state ledger

Ranked by consequence.

| Rank | Site | Released? | Consequence |
|---|---|---|---|
| 1 | `humanoid_locomotion.cpp:1547` (hips residue) | **NO** | A body that must fall, cannot. Blocks ragdoll, knockback, death. |
| 2 | `worldgen/planet_generator.cpp:138` (6k stones) | **NO** | Every stone is an immovable post. Pins the whole world surface. |
| 3 | `humanoid_locomotion.cpp:307` | **NO** (inverted) | Pins at the moment of release. Teaches the inversion by example. |
| 4 | `butterfly_flight.cpp:116` | **NO** | Butterfly permanently outside physics. Bounded blast radius. |
| 5 | `worldgen/physics_tree_generator.cpp:919, 1302` | **NO** | Tree roots are posts, not roots. Cannot be uprooted. |
| 6 | `worldgen/organic_generator.cpp:97, 178` | **NO** | Grass/plant bases are posts. Drives the grass-yield problem in §5. |
| 7 | `worldgen/planet_generator.cpp:65` (core) | **NO** | Core is scenery; lowest practical harm. |
| 8 | `humanoid_locomotion.cpp:6138` | **NO** | Dormant by default, live for `register_joint` users. |
| 9 | `humanoid_locomotion.cpp:2131` | **NO** | Dead code (zero callers). Documentation hazard only. |
| — | `humanoid_locomotion.cpp:2080` (plant anchor) | n/a — **destroyed** | Genuinely transient: the helper particle is deleted at `:1853-1854`, and the foot itself is never KINEMATIC (the plant is a `NailGluon`, `:2085-2104`, removed on DISENGAGE at `:2041-2044`). **This is the pattern the rest should follow.** |

Partial/conditional releases, called out because a release that fires on
only some paths makes behavior depend on how the entity was created:

- `:1899` and `:1984` are **total** for the particles they cover (both
  called unconditionally). Their defect is coverage, not conditionality.
- `:2453` and `:2539` (`set_joint_physics_drive`,
  `set_joint_physics_drive_q`) fire **only when a caller names a joint**.
  A humanoid whose joints nobody names keeps whatever `:1547` and
  `:6138` stamped.

**The one correct transient in the whole repo** outside `:2080` is a
test: `tests/test_rube_goldberg_machine.cpp:140-148`, where
`Harness::make_kinematic` latches a ball above the ramp and `release()`
flips it back to DYNAMIC. That is the mechanism working as designed.

---

## 4. Bucket C — scenery, and what it teaches

74 sites, almost entirely `tests/`. Nothing writes their positions after
setup; verified globally rather than per-file — across all 51 test files
touching the enum, only 6 ever write a body's `.x/.y/.z` afterward, and
those 6 are bucket A.

Dominant idiom, 30 sites, copy-pasted rather than shared: a 1.0x1.0x0.1
STONE box at `z = 0.05f`, then the fixed triple

```cpp
solver_mode = ParticleSolverMode::KINEMATIC;
owner       = ParticleOwner::DYNAMICS;
is_at_rest  = true;
```

That triple appears at **58 of 80 set-sites in `tests/`**. It says
"pinned, and DYNAMICS nominally owns it" while in 74 of 80 cases nothing
in DYNAMICS ever writes it. The nearest thing to a canonical definition
is two identical `add_floor(Engine&, int half)` functions copied between
files: `tests/test_settling.cpp:67` (assignment at `:80`) and
`tests/test_baumgarte_ratchet.cpp:78` (assignment at `:90`).

**Honest alternative for test floors:** the turtle boundary already
exists and already supports bodies — 183 `why=turtle` contact rows were
observed in a three-body test in §5 below. For most of these 30 sites
the floor tiles are redundant with the turtle. INFERRED: a shared
`add_floor` helper that used the turtle, or a single shared pinned-floor
helper, would collapse 30 copies into one decision the owner can change
once. This is a test-hygiene item, not an engine defect.

Sub-buckets: 14 anchors/rooted posts for gluon and bond tests, 7 floating
occluders for shadow/LOD/GPU benchmarks, 3 debug-visualisation `dot()`
markers that borrow the physics body type as a render fiducial
(`tests/test_predator_senses.cpp:456`,
`tests/test_tree_collapse_demo.cpp:131`,
`tests/test_rotation_ladder.cpp:309`).

Bucket A in `tests/` (correct, and each states its contract):
`test_knockback_scene.cpp:154` with its writer at `:220-232`;
`test_grass_bends_not_tears.cpp:194` (`:207-211`);
`test_grass_natures.cpp:279` (`:349-355`);
`test_rotation_ladder.cpp:526` (`:661-669`) and `:822` (`:897-905`);
`test_predator_hunt.cpp:233` (`:377-382`). The comment at
`test_knockback_scene.cpp:151-153` is the clearest statement of the
doctrine in the codebase.

### Bucket D — `examples/`, game layer, not judged

6 set-sites, 4 reads. `grep -rn "ParticleSolverMode::DYNAMIC" examples/`
returns **zero** — nothing in any example ever releases.

Genuinely correct, writer confirmed:
- `examples/predator/at_predator_hunger_visual.cpp:165` (predator) —
  written every frame at `:321-322`, `:345-346`. Comment at `:157`: *"this
  loop owns its position."*
- `examples/predator/at_predator_search.cpp:247` (predator) — written at
  `:487`, `:686`. The cleanest correct use in the repo; `:643-644` notes
  *"a KINEMATIC creature owns its own refusal to enter geometry"* and
  implements its own nav-grid collision at `:645-649`.

Partial — scale is owned, position is pinned:
- `at_predator_hunger_visual.cpp:185` and `at_predator_search.cpp:291`
  (carcasses). `.width/.height/.thickness/.size` and a derived `.z` are
  written (`:323-326`, `:488-493`); `.x`/`.y` never are.

Game-layer pins:
- `at_predator_search.cpp:272` — 4 thorn bushes, index discarded at
  `:274`, pinned so a 1.6 m predator sphere does not bowl them across
  the map. The disease pattern, in an example, uncommented as such.
- `examples/eden/src/main.cpp:1286` — one A/B diagnostic cube, index
  discarded at `:1288`. Benign; nothing depends on it.

**One coupling smell worth naming.**
`examples/logogenesis/tests/at_logogenesis_creation.cpp:1280` uses
`solver_mode == KINEMATIC` as the *definition of "is terrain"*
(`if (view[k].solver_mode == KINEMATIC) continue; // crust`). That
promotes a physics-authority flag into a semantic type tag. Any future
legitimately-KINEMATIC body in a logogenesis scene — a scripted
creature, an animated limb — would be silently misclassified as
terrain. INFERRED consequence; no such body exists in that scene today.

---

## 5. Bucket E — remaining "is this body movable?" disagreements

41 code reads branch on KINEMATIC in `src/core/physics_system_v4.cpp`.
The two the owner already fixed today (contact row build at `~:1298`,
normal-impulse apply at `:3324`) are correct. Three disagreements
remain, ranked by blast radius.

### E-1 (HIGH) — `update_rest_state` has no KINEMATIC guard

`src/core/physics_system_v4.cpp:4697`. The loop skips only massless
bodies (`:4706`). At `:4719-4723`, any body whose constraint set is
dissatisfied gets `is_at_rest = false` — **KINEMATIC bodies included**:

```cpp
if (!satisfied) {
    p.frames_at_rest = 0;
    p.is_at_rest = false;
    p.low_velocity_frames = 0;
}
```

Sleep is meaningless for a body physics never integrates, so this is
incoherent on its face. It is also the direct cause of the live question
in §6: it is what wakes a pinned anchor so the old pricing can call it
movable. **This is the root cause, and it is one guard.**

### E-2 (HIGH) — the friction applies still re-ask the question

The normal-impulse apply at `:3324` was corrected today to let `inv_mb`
be the whole guard. The **two friction applies were not**:

- `src/core/physics_system_v4.cpp:3463`
- `src/core/physics_system_v4.cpp:3485`

Both still read `if (!c.is_turtle_contact && !pb.is_at_rest &&
pb.solver_mode != ParticleSolverMode::KINEMATIC)` **on top of** a
multiplication by `inv_mb`. This is the identical bug, in the identical
shape, with the identical consequence the commit message describes:
`pa` is always charged the friction impulse (`:3451-3453`, `:3481-3483`),
and when the resolver makes `inv_mb` nonzero for a sleeping body, `pb`
never receives it. Momentum destroyed at the friction door. INV-7 and
INV-20 both.

Related, same rows: `:3442` and `:3472` guard the *relative velocity*
computation with `!pb.is_at_rest`, so the row measures `v_rel` as if a
sleeping `pb` were static while the solver is now permitted to move it.
Priced in one model, spent in another.

Blast radius: every frictional contact against a sleeping body, which is
most resting stacks. This is live the moment the resolver flips.

### E-3 (MEDIUM) — two positional passes disagree about sleep

`inv_mass_positional` at `src/core/physics_system_v4.cpp:3871` prices
sleep as **movable** on purpose, and says so at `:3869-3870`: *"The
momentum-side predicate is inv_mass_momentum; the two differ on sleep,
deliberately."* Its comment block records the bug that motivated it — a
body finishing with a 2.35 m bond error, 117x the wake threshold.

`project_gluon_positions` at `:5832` is also a positional pass, and at
`:5934-5935` it holds the opposite opinion:

```cpp
float inv_mass_a = (pa.solver_mode == ParticleSolverMode::KINEMATIC || pa.is_at_rest) ? 0.0f : ...
float inv_mass_b = (pb.solver_mode == ParticleSolverMode::KINEMATIC || pb.is_at_rest) ? 0.0f : ...
```

Two positional passes, contradictory predicates, neither behind the
lever. This is the same disease class as the 2.35 m bond error, in the
gluon projection rather than the contact projection. Not implicated in
the §6 failures, so it is not blocking the flip.

### E-4 (LATENT) — `ParticleSolverMode::STATIC` is accepted but not implemented

Found in passing, unrelated to the flip, reported because it is a live
trapdoor. `src/kg/entity_physical_state.cpp:41` accepts the string
`"STATIC"` from the knowledge graph and writes
`ParticleSolverMode::STATIC` onto every particle of the entity.

`grep -n "SolverMode::STATIC" src/core/physics_system_v4.cpp` returns
**zero hits**. All 41 authority checks in the solver test `== KINEMATIC`.
A body an agent sets to `solver_authority: STATIC` through the KG is
therefore integrated as if DYNAMIC and falls — while the graph, the
schema, and the agent all believe it is the most immovable thing
available. `src/particle_types.h:69-70` documents STATIC as *"reserved.
Only the turtle world boundary is truly immovable"*, so the enum value
is a promise the solver never made.

Blast radius today: zero known callers outside
`tests/test_ontology_levers.cpp`. Blast radius the first time a game
sets it: silent, and shaped exactly like a physics bug.

---

## 6. The live question, answered

**Question.** With `WAKE_RESOLVER=1`, `tests/test_light_body_ringing.cpp`
and `tests/test_grass_yields.cpp` go red. Do they depend on the old
pricing quirk, or does the correction break something real?

**Answer: both tests' green baselines are produced by the quirk. The
failures they show under the resolver are real and pre-existing, and are
reproducible with the resolver OFF.**

### Reproduction

```
./build-release/logosphere-tests --test <name> --no-head              -> exit 0
WAKE_RESOLVER=1 ./build-release/logosphere-tests --test <name> --no-head -> exit 1
```

Confirmed for both tests.

### The mechanism, traced

Both tests build their anchor with the same three lines —
`tests/test_light_body_ringing.cpp:96-98` and
`tests/test_grass_yields.cpp:93-95`:

```cpp
v[id].solver_mode = ParticleSolverMode::KINEMATIC;
v[id].owner       = ParticleOwner::DYNAMICS;
v[id].is_at_rest  = true;
```

Nothing writes those anchors. They are bucket C pins.

The chain: E-1 (`update_rest_state`, no KINEMATIC guard) clears the
pinned root's `is_at_rest` as soon as its bond is loaded. The root is now
KINEMATIC-and-not-at-rest. Under the **old** contact-row pricing
(`inv_ma = pi.is_at_rest ? 0 : 1/pi.GetMass()`) that root is priced at
`1 / 0.00238 kg = 420` — a 2.38 gram **feather** — while the apply site
refuses to move it. The contact can support nothing, so the bodies
resting on the anchor sink straight through it.

**Measured**, `test_light_body_ringing` at ratio 1044x, state at frame 0
(after the 60-frame settle, before the push). The chain is built at root
`z=0.10`, light `z=0.16`, heavy `z=0.22`, bond rest 0.060 m:

| Config | light z | heavy z | light-heavy dist |
|---|---|---|---|
| lever OFF | **0.0300** | **0.0337** | **0.0037** |
| lever ON | 0.1105 | 0.1657 | 0.0551 |

With the lever off the chain has already fallen through its own anchor
and is lying in a heap on the turtle floor (`z=0.03` is the box
half-thickness), with the two segments 3.7 mm apart instead of 60 mm.
The physics tracer confirms the sinking directly: **183 `why=turtle`
contact rows with the lever off, zero with it on**, the first at frame 12
— mid-settle:

```
f12  s2  i0  row_created  a=1  b=1  why=turtle  v=0.064499,0.00238,1
```

The test then pushes that heap at 1.2 m/s and measures whether it "rings
down". A heap resting on the floor rings down trivially. That is the
green.

### The control experiment

`SLEEP_LAW_OFF=1` (`physics_system_v4.cpp:4715`) forces `satisfied =
true`, so the `!satisfied` branch never fires and the pinned root is
never woken. Run with the **resolver OFF**, this isolates E-1 alone:

| ratio | lever OFF (baseline) | **OFF + SLEEP_LAW_OFF=1** | **WAKE_RESOLVER=1** |
|---|---|---|---|
| 250x | 1.200 rings down | 1.233 rings down | 1.242 rings down |
| 500x | 1.200 rings down | **1.550 TORE** | **1.548 TORE** |
| 1044x | 1.200 rings down | **1.792 TORE** | **1.794 TORE** |

The control reproduces the resolver's result to three decimal places
**without the resolver**. The entire delta is attributable to whether
the KINEMATIC anchor is woken and thereby priced as a feather.

Same control on the grass test:

| Config | speed retention | verdict |
|---|---|---|
| lever OFF | 0.90 / 1.2 = **75%** | PASS (gate is `>= 75%`) |
| OFF + `SLEEP_LAW_OFF=1` | 0.80 / 1.2 = **67%** | FAIL |
| `WAKE_RESOLVER=1` | 0.81 / 1.2 = **67%** | FAIL |

Note the baseline passes at *exactly* its gate. Caveat, stated:
`SLEEP_LAW_OFF` is a global sleep change, not a surgical one, so for the
grass test it is a weaker isolation than for the ringing ladder (contact
counts differ: 42 vs the resolver's 180 vs the baseline's 48). The
retention numbers matching exactly at 67% is the evidence; the mechanism
match is INFERRED from the ringing result.

### Were the expected numbers baselined against the quirk?

Yes, and demonstrably so for the ringing ladder: `peak_early = 1.200`
across every ratio in the baseline is exactly the injected push speed
(`tests/test_light_body_ringing.cpp:124`, `v[heavy].vy = 1.2f`). A
collapsed heap cannot exceed what it was given. The moment the chain
actually stands, peak reaches 1.794 — 1.49x injected — which is real
amplification and a real INV-17 violation that the collapse was hiding.

The grass test is the more interesting case, because **it already
convicts itself**. Its header at `tests/test_grass_yields.cpp:16-22`
says the KINEMATIC rooting makes blade bases *"an immovable post"* and
sets up a diagnosis column to convict it. Its failure text at
`:308-312` names the honest fix: *"The rooting mechanism needs to yield
(dynamic base with a strong anchor gluon, or contacts that exempt
walker-vs-root)."* That rooting is B3/B4 in §2. The test was written to
detect exactly the disease this audit is cataloguing.

### Recommendation on the flip

**Flip it, but fix E-1 and E-2 first, and re-baseline the two tests
rather than treating them as regressions.**

Sequence, dependency-ordered:

1. **E-1**: add the KINEMATIC guard to `update_rest_state`
   (`physics_system_v4.cpp:4706` region). A body the solver never
   integrates has no business carrying a sleep state. This alone removes
   the quirk's cause and is correct independent of the flip. Expect both
   tests to go red with the lever OFF once it lands — that is the
   quirk's support being removed, not a new break.
2. **E-2**: route the two friction applies (`:3463`, `:3485`) and their
   `v_rel` guards (`:3442`, `:3472`) through `inv_mb`, matching the
   correction already made at `:3324`. This is live the moment the
   resolver flips and destroys momentum in every frictional contact
   against a sleeper.
3. **Re-baseline** `test_light_body_ringing` and `test_grass_yields`
   against a standing chain and a real wade. The 500x/1044x tear and the
   67% retention become genuine open defects at that point — which is
   what they always were.
4. Flip the default.

**Residual risk, named.** The tear at 500x and 1044x is real and
currently unexplained. This audit establishes that the resolver did not
cause it and that the quirk was concealing it; it does **not** establish
why a bond between unequal masses amplifies a 1.2 m/s push to 1.79 m/s.
That is an open INV-17 defect and flipping the lever makes it visible in
CI. Whether to flip before or after diagnosing it is a sequencing call
for the owner, not a technical blocker.

Second residual: the grass gate at exactly 75% means the baseline had no
margin. Any honest change moves it. The 67% figure is what wading
through *unpinned* grass costs — INFERRED, since B3/B4 have not been
switched to pin gluons and measured.

---

## 7. Open questions for the owner

Each is a real fork, with the evidence, not a recommendation in
disguise.

**Q1. Three written sources license KINEMATIC as a permanent immobility
mechanism (§Finding 0). Which moves — the text or the code?**
- *Amend the text* (`INVARIANTS.jsonl` INV-1,
  `schema/logosphere.yaml:1184-1191` and `:56-58`,
  `include/logosphere/kg/entity_physical_state.h:28-32`) to match the
  transient doctrine. Consequence: all 12 bucket-B sites become
  violations at once, and every comment citing the licence needs
  rewriting.
- *Keep the text*: bucket B is compliant, and the transient doctrine has
  no enforceable statement. The hips pin (B7) stays legal, so humanoids
  stay unknockable-over by design rather than by accident.
- Evidence: the three quotations are verbatim; the authors demonstrably
  followed them.
- Note: whichever way this goes, it is the prerequisite. Nothing else in
  bucket B can be called a defect until it is settled.

**Q2. The planet surface (B2): is it a body or is it the world?**
- *Body*: 6000+ stones go DYNAMIC and need the core bonds that
  `planet_generator.cpp:71-73` deliberately deleted for cost. The
  cascade at `:131-137` must be shown survivable; that measurement does
  not exist. Two acceptance assertions
  (`at_logogenesis_creation.cpp:1204-1207`, `:1246-1249`) fail by
  construction and would need rewriting to assert the opposite.
- *World*: it is terrain, and pinning it is the correct mechanism under
  the repo's written doctrine. The turtle is not an available
  substitute — it is a catch-net, and the tree measurements at
  `physics_tree_generator.cpp:900-903` show it does not support a body
  resting on it.
- *Middle path, already designed and unbuilt*: `planet_generator.cpp:73-75`
  proposes flipping a *region* back to DYNAMIC and bonding it on demand.
  The lever for that exists (`entity_physical_state.cpp:34`). This is
  the only option that satisfies both doctrines.

**Q2b. Should the release lever be wired to worldgen at all?**
- The transient mechanism exists, is tested bidirectionally
  (`tests/test_ontology_levers.cpp:140-160`), and has **zero** worldgen
  callers. Wiring generated bodies through `solver_authority` instead of
  baking `solver_mode` into the struct would make every bucket-B pin
  releasable without changing any default behavior.
- Against: it adds a KG property write per generated body, at grass and
  crust density.
- Evidence: `grep -rn "solver_authority" src/worldgen/` → 0 hits. This is
  a cheap structural change with no behavioral consequence until
  something pulls the lever, which is unusual enough to be worth naming
  separately.

**Q3. The humanoid hips (B7): release them, or declare humanoids
non-physical?**
- *Release*: hips become DYNAMIC when no clip drives them. Ragdoll,
  knockback, and falling become possible.
  `tests/test_humanoid_impact.cpp:381-382` starts measuring what it
  claims to. Unknown: whether locomotion's own hips integrator
  (`humanoid_locomotion.cpp:5153-5160`, *"Since physics skips DYNAMICS
  particles, we integrate hips position here"*) can coexist with a
  solver that also writes them. That comment suggests the two would
  fight; this audit did not test it.
- *Declare*: humanoids are permanently externally-owned, and
  `KNOCKBACK` (`src/generated/space_ontology.h:617`, unimplemented) is
  removed from the ontology rather than left as a promise.

**Q4. Bucket C's 30 copy-pasted floor grids: worth consolidating?**
- *Yes*: one shared helper makes the floor mechanism a single decision.
- *No*: they are test fixtures, inert, and touching 30 files has its own
  risk.
- Evidence: nothing writes them (verified globally); the turtle already
  supports bodies in the same scenes. Pure hygiene, no behavior at
  stake.

**Q5. Flip sequencing (§6): before or after diagnosing the 500x/1044x
tear?**
- *Before*: CI shows the real defect immediately; two tests stay red
  until it is fixed.
- *After*: keeps CI green, but the quirk keeps hiding the tear and every
  measurement taken meanwhile is taken against a collapsed chain.
- Evidence: the tear is reproducible today with `SLEEP_LAW_OFF=1` and
  the lever off, so diagnosis does not require the flip.

---

## Method notes

- Build: `cmake --build build-release -j8 --target logosphere-tests`,
  exit 0.
- All test runs headless (`--no-head`). No windows opened.
- Evidence levers used: `WAKE_RESOLVER`, `SLEEP_LAW_OFF`, `RING_RATIO`,
  `RING_TRACE`, `RING_FRAMES`, `LOGOSPHERE_PHYS_TRACE=4`,
  `LOGOSPHERE_PHYS_TRACE_FILE`.
- `src/platform/platform_macos.mm` was not read or touched. It carries a
  pre-existing local modification unrelated to this audit.
- Nothing was fixed. This is a survey.
