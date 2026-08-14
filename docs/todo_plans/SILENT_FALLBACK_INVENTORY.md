# Silent Fallback Inventory — physics layer

Date: 2026-08-13. Mission: owner order (LEDGER 2026-08-13, "SILENT
FALLBACKS: DESTROY ALL in physics"). This document is the map: every
candidate site in scope, its taxonomy class, and the mechanism-vs-mask
judgment. Committed BEFORE any code change so the destruction is
reviewable against it. Breakage inventory follows in the same file
after the slices land.

Scope: `src/core/physics_system_v4.cpp`, `src/core/narrow_phase.cpp`,
`src/core/explosion_detector.cpp`, `include/logosphere/physics/*.h`,
`src/generated/physics_constants.h` consumers. NOT the KG, NOT
animation/dynamics, NOT worldgen (cross-boundary sightings listed at
the end for the engine-wide phase).

Taxonomy (from the mission): (1) class-member defaults standing in for
required-but-never-set; (2) value-or-default expressions masking
absence; (3) if-missing-use-X-and-continue / ignored errors /
catch-and-continue; (4) zero/default-init standing in for never-set;
(5) silent clamps masking invalid input (vs declared mechanism);
(6) default function parameters hiding a required choice.

Verdicts: **DESTROY** (becomes a loud refusal with a `*_LENIENT`
lever), **KEEP-MECHANISM** (judged a declared mechanism; justification
given so review can veto), **CATALOG** (real disease, but destroying it
silently changes physics or API surface — owner decision needed).

Levers: `GLUON_LENIENT` (bond doors, existing precedent),
`PHYSICS_LENIENT` (new, solver-internal refusals), `TURTLE_LENIENT`
(existing, untouched).

---

## A. Gluon class hierarchy — include/logosphere/physics/physics_system.h

| # | Site | Class | Verdict | Reasoning |
|---|------|-------|---------|-----------|
| A1 | `GluonConstraintBase::angular_stiffness = 100.0f` (L151) | 1 | **DESTROY** | THE FLAGSHIP. Any bond whose creator forgets runs on a number nobody chose; test_humanoid_tuning_coverage has been red saying exactly this. Owner ruled destroyed, not derived, not registered. Cure: 0.0f = UNDECLARED sentinel (same pattern as stiffness/damping at L129) + door refusal for consumers (force-bounded bonds with the angular constraint enabled must have a derived/declared angular law). |
| A2 | `GluonConstraintBase::angular_damping = 10.0f` (L152) | 1 | **DESTROY** | Same as A1. Zero damping is legal (bonds may ring); the DEFAULT 10 is not. Sentinel 0.0f; door refuses negative. |
| A3 | `GluonConstraintBase::last_force_magnitude` (L131) | 4 | **DESTROY (delete)** | Uninitialized float, zero writers, zero readers in the entire tree. Dead field carrying indeterminate memory. C-116: delete. |
| A4 | `OrganicGluon::contact_area` (L291) | 4 | **DESTROY (sentinel)** | No initializer. A forgotten area is indeterminate memory; garbage > 0 DERIVES A FORCE LAW FROM GARBAGE and sails past the refuse door. Explicit `= 0.0f` makes "forgot" deterministic → derivation declines → door refuses. |
| A5 | `NailGluon::breaking_force` (L281) | 4 | **DESTROY (sentinel + door)** | No initializer. Garbage breaking force = a weld with an indeterminate contract. `= 0.0f` sentinel + door refuses any bond whose `calculate_breaking_force()` <= 0 (also catches organics whose material strength is zero — the same G6 hollow-bond disease through the breaking factor: budget = min(breaking, spring) so breaking 0 is a zero-force bond). |
| A6 | `ElasticGluon::{breaking_force, stiffness, max_strain}` (L313-315) | 4 | **DESTROY (partial)** | All three uninitialized. `stiffness` SHADOWS the base member: entity_manager writes the shadow, the solver/energy-ledger read the base = a declaration that silently lands nowhere. Delete the shadow (C-116), sentinel `breaking_force = 0.0f` (caught by A5's door). `max_strain` is DEAD (ElasticGluon never overrides max_strain_ratio(), so the KG's declared max_strain is silently ignored) — CATALOG as an uncontrolled flow, breakage class "silently-ignored declaration"; wiring it up changes tear behavior = owner decision. |
| A7 | `compliance = 0.0f` (L118) | 4 | KEEP-MECHANISM | 0 = rigid is the identity element (absence of compliance = strictest constraint), documented at the site. Not invented physics. |
| A8 | `target_relative_rotation = 0.0f` (L153) | 4 | KEEP-MECHANISM | Identity: target = "match parent frame". The universal rest pose, not a magic value. |
| A9 | `enable_angular_constraint = true` (L154) | 1/6 | CATALOG | A default POLICY: every bond is a rotation-coupling joint unless declared otherwise. Not a number nobody chose, but a semantic nobody chose per-bond. Destroying it forces a declaration at every creation site in the tree — engine-wide-scale breakage beyond this mission's numeric-default disease. Owner call. |
| A10 | `max_relative_rotation = ANGULAR_LIMIT_UNLIMITED` (L185) | 1 | KEEP-MECHANISM | Named registry constant (INV-29), semantic "no limit", documented. A declared identity, not a mask. |
| A11 | `rotate_offsets = true` (L139) | 1/6 | KEEP-MECHANISM | Documented semantic default matching every skeleton/organic creator; the false case is the special one and is always declared. Borderline; review may veto. |
| A12 | `plastic_yield_angle = inf`, `force_over_frames = 0`, `warm_i* = 0` (L199-224) | 4 | KEEP-MECHANISM | inf = purely elastic (identity), 0 = no accumulated state. Identity elements of their mechanisms. |
| A13 | `PhysicsConfig::restitution = 0.5f` (L52) | 1 | CATALOG | Read by NOTHING in the tree. A config field that promises restitution and silently does nothing — the lie is the field existing, not the default. Deleting is a public-API change; owner call. Same for `debug_collision`. |
| A14 | `initialize(..., config = PhysicsConfig())` (L333/337) | 6 | KEEP-MECHANISM | The config's live fields (enable_collision) have honest defaults; forcing every headless test to build a config buys nothing while A13 is unresolved. |
| A15 | `OrganicGluon::max_strain = 2.0f` (L306) | 1 | KEEP-MECHANISM | Declared, documented (plant fiber tears at ~2x, deliberately generous), overridable per bond, consumed via the max_strain_ratio() override. A chosen number with its reasoning at the site. INV-9 tension noted: could derive from material elongation-at-break — constant-work, not fallback-work. |

## B. Solver — src/core/physics_system_v4.cpp

| # | Site | Class | Verdict | Reasoning |
|---|------|-------|---------|-----------|
| B1 | L796 `c.effective_mass = pi.GetMass() > 0.0f ? pi.GetMass() : 1.0f` (turtle row build) | 2 | **DESTROY** | The eff-mass phantom, THIRD copy. Killed twice already (S22 contact path L1236, gluon build L1604 — both comment the disease). Massless is skipped at L742, so the `: 1.0f` arm is an unreachable invented value waiting for the guard above it to change. Loud refusal on mass<=0, direct read otherwise. |
| B2 | L1963 `else { continue; } // Zero inertia (shouldn't happen), skip` | 3 | **DESTROY** | Catch-and-continue on a state the comment itself calls impossible. A dynamic endpoint with I<=0 is broken particle data; the angular row silently vanishes and the joint never couples. Loud refusal + PHYSICS_LENIENT. |
| B3 | L4708-4711 `if (I <= 0) I = 0.01f; // Fallback for invalid inertia` | 2/4 | **DESTROY** | Flagship #2. An invented inertia integrates real torque on a body whose inertia is unknown. Split honestly: mass==0 → skip integration (massless bodies carry no angular momentum, same door-predicate as INV-7's linear side, stated at the site); mass>0 && I<=0 → loud refusal + lever. |
| B4 | L4329-4331 `if (!std::isfinite(p.vx)) p.vx = 0.0f;` (NaN swallow) | 3 | **DESTROY** | A NaN velocity is a solver defect; zeroing it silently is how detonation-class bugs stay invisible (the site's own comment: "Root cause should still be investigated"). Loud, names the particle, PHYSICS_LENIENT keeps the zero-reset for sweeps. |
| B5 | L4322-4327 MAX_VELOCITY rescale | 5 | KEEP-MECHANISM | Named registry cap (INV-29), documented safety net; the explosion detector samples after it and its ceiling sits below the cap, so the clamp does not blind the tripwire. Declared mechanism, not a mask. |
| B6 | L1345-1350 contact point `(num_points > 0) ? points[0] : invented-from-normal` | 2 | **DESTROY (simplify)** | `have_contact == true` guarantees num_points >= 1 on every handler (all return `num_points > 0`). The else-arm invents a contact point from center/normal arithmetic — dead fallback. Replace with the direct read. |
| B7 | L1259 `eff_mass_share = split_off ? 1.0f : 1/N` | — | KEEP-MECHANISM | SPLIT_OFF is a declared diagnostic lever, documented; the 1/N is the manifold mass split (INV-8 mechanism). |
| B8 | L1627-1632 force-bounded damping `: damping_factor` when m_light<=0 | 2 | KEEP-MECHANISM (narrow) | A massless endpoint has no momentum for the declared damping model to act on; the fall-through to material-pair damping is the pre-existing model for that case. Real states (lights), not missing data. Review may veto. |
| B9 | L1086 massless sleeper → raw-speed wake gate | 3-lite | KEEP-MECHANISM | Declared branch on a real state with a named registry constant; transfer formula is undefined at ms=0 and the site says what replaces it. |
| B10 | L5091-5097 `index_gluon` duplicate-pair overwrite (cout WARNING, then continue) | 3 | **DESTROY** | Two live bonds for one pair: the deque solves BOTH (double dose, INV-22 violation) while queries see one. Today it's a cout line scrolling past. Loud refusal + GLUON_LENIENT. Known suspect flow: pin-gluon respawn indexes the new bond while the old awaits deferred removal — the breakage inventory will name it. |
| B11 | L5188-5218 `prune_invalid_gluons` repairs stale gluons and continues | 3 | **DESTROY** | The function's own comment: "this should ideally find nothing - if it finds stale gluons, there's a bug". Catch-log-continue on corrupted bookkeeping. Abort after the diagnostic unless GLUON_LENIENT. |
| B12 | L4986/L5053 `add_particle_with_gluon_to`/`add_gluon_between` silent drop on !initialized or null gluon | 3 | **DESTROY** | A requested bond silently vanishing (or -1 returned into a caller that may ignore it) is a world that differs from what the creator declared. Loud refusal + PHYSICS_LENIENT. |
| B13 | L4878 `add_force` accepts forces the solver never applies (`apply_registry_forces` is an empty stub) | 3 | **DESTROY (warn)** | A silently-inert API: callers register gravity and think it acts; only `get_gravity_vector()` queries read the registry. One-time loud stderr on first add_force naming the contract (registry = direction data, NOT applied). Abort would nuke legitimate direction-query users; the honest minimum is the loud contract statement. Full cure (delete or implement) = owner decision, catalogued. |
| B14 | L269 `update()` returns silently when !initialized | 3-lite | KEEP-MECHANISM | Standard lifecycle guard; engine teardown paths may tick after shutdown. Borderline; review may veto. |
| B15 | L5793 `apply_gluon_constraint_for_pair` returns silently when no bond exists | 3 | CATALOG | Zero callers in the tree (dead public API). Destroying inside dead code proves nothing; the API itself is the finding. Owner call: delete (C-116) or keep for the animation-PHYSICS-mode plan. |
| B16 | L1414 `gap_threshold = 0.15f`, L1098 `0.05f`, L1107 `0.3f`, etc. | — | CATALOG (INV-29) | Magic floats, already pinned by the INV-29 gate's KNOWN_RESIDUALS table (ledger Stage E). Constants-work, not fallback-work; separate effort by decree. |
| B17 | L4380-4381 `b = max(5.0f, damping*24)`; ks/damping_factor floors at 0.5 | 5 | CATALOG | The 5.0 floor silently overrides any material declaring lower structural damping — a hidden clamp on an input (INV-19/INV-29 tension, in the gate's residual table). Removing it changes settling behavior everywhere = constant-work + mechanism-work, owner decision. The 0.5 floors are numerical guards with a stated invariant (damping must not reverse velocity) — KEEP-MECHANISM. |
| B18 | L3509 residual pass zeroes bias only for turtle rows (solve zeroes for all non-approach-limit rows) | — | CATALOG | Measurement inconsistency, not a fallback: the residual instrument measures a different constraint than was solved for gluon/box rows. Diagnostic-only skew; noted for the instrumentation lane. |
| B19 | L530 / L1876ff `p.owner` reads | — | out of taxonomy | The 7 known INV-15 reads, pinned by test_inv15_owner_blindness. Not touched. |
| B20 | L5023/L5072 `damping *= sqrt(ma*mb)` mass-scaling for non-force-bounded bonds | — | KEEP-MECHANISM | Declared V4.4 model for welds, documented, skipped for derived organics precisely to avoid double-counting. |
| B21 | L1160-1187 static-neighbor AABB surface merge; L2705-2831 contact composition | — | KEEP-MECHANISM | Both are geometry mechanisms with invariants (seam artifact removal), not value substitutions. |

## C. Narrow phase — src/core/narrow_phase.cpp

| # | Site | Class | Verdict | Reasoning |
|---|------|-------|---------|-----------|
| C1 | L507-511 sphere-sphere coincident centers → normal = +Z | 5-lite | KEEP-MECHANISM | Coincident centers have NO defined normal; some deterministic convention is required. Documented at the site. FLAG: the +Z choice is an axis bias (INV-6 tension) — a gravity-free convention (e.g. derived from body ids) would be cleaner; owner call, listed for the engine-wide phase. |
| C2 | L979-988 ELLIPSOID (and unknown pairs) collide as their enclosing AABB | — | KEEP-MECHANISM (flagged) | Declared, header-documented approximation ("conservative over-reporting"), not a missing-datum mask. FLAG: violates INV-12's spirit (a body colliding as its axis-aligned slab); tracked as invariant debt, not fallback debt. |
| C3 | L897-914 grazing-pose manifold thinned to 0 → deepest-vertex recovery | — | KEEP-MECHANISM | Recovers REAL geometry (the incident box's support vertex), doesn't invent data; documented numerical-robustness path. |
| C4 | L561-581 sphere-center-inside-box → shallowest exit face | — | KEEP-MECHANISM | Analytic degenerate-case geometry, standard and correct. |
| C5 | L51-54 slab_check parallel-axis epsilon 1e-8 | — | CATALOG (INV-29) | Precision guard; within the gate's exemption band. |

## D. Explosion detector — src/core/explosion_detector.cpp

| # | Site | Class | Verdict | Reasoning |
|---|------|-------|---------|-----------|
| D1 | L142 KE ratio printed as `999.0` when previous KE was 0 | 2 | **DESTROY** | An invented number in a safety instrument's output. Print the honest value (inf). |
| D2 | L32-37 `LOGOSPHERE_EXPLOSION_SPEED` unparseable/nonpositive → silently keep default | 3 | **DESTROY** | An operator who mis-sets the tripwire's ceiling flies with a different instrument than they believe armed. Loud refusal (abort with actionable message). No lenient lever: this is operator-input validation at process start. |
| D3 | L26-30 detector on by default, `is_light_source` exemption | — | KEEP-MECHANISM | INV-11 mechanism; the exemption is a declared design statement. |

## E. Solver structs — include/logosphere/physics/physics_solver.h

| # | Site | Class | Verdict | Reasoning |
|---|------|-------|---------|-----------|
| E1 | `Constraint()` default `jz = 1.0f` ("Default to Z axis") | 4 | **DESTROY** | A row nobody finished building silently pushes along +Z (an axis bias on top of a phantom). Every real builder sets the jacobian explicitly (verified: turtle, contact, speculative, gluon, angular). Unset must be INERT (0,0,0), not secretly vertical. |
| E2 | `angular_axis_idx = 2` default | 4 | KEEP-MECHANISM | Load-bearing declared convention: the scalar angular path IS Z-rotation by definition and relies on this default; documented at the field. |
| E3 | `min/max_angular_impulse = ±inf` defaults | 4 | KEEP-MECHANISM | Identity (unbounded); every budgeted row sets its budget explicitly; non-angular rows never read them. |
| E4 | `penetration = 0.0f` default | 4 | KEEP-MECHANISM | Documented: measurement-only field, "defaulted, because rows are built field by field and gluons never set it". The site already tells the truth. |

## F. physics_flags.h, bvh.h, contact_manifold.h, narrow_phase.h

No fallback sites found. physics_flags.h is compile-time diagnostic
switches (all false); bvh.h inverted-AABB default constructor is the
standard expansion identity; contact_manifold.h documents its own
normal-direction trap honestly.

## G. Cross-boundary fallbacks spotted OUT of scope (for the engine-wide phase)

- **H4 (MALLEUS)**: `src/materials.h` switch ladders with `default:`
  arms returning invented property values; `material_strength = 50e6`
  hardcoded at 34 creation sites vs table values 50x apart. The A5
  door (breaking<=0 refusal) will surface the zero-strength side;
  the 50e6 side stays until H4's one-table cure.
- **H1 (MALLEUS)**: `KGCore::setProperty` validates nothing — the KG
  gate's fallback. Out of scope, queued.
- `src/entity_manager.cpp` ELASTIC path writes the SHADOWED stiffness
  (lands nowhere the solver reads) and a max_strain the engine
  ignores (A6). Creator-side fix belongs to the engine-wide phase.
- `src/worldgen/humanoid_generator.cpp` declares twenty per-joint
  angular profiles that `humanoid_locomotion.cpp` overwrites with
  2000/60 before frame one (test_humanoid_tuning_coverage's finding):
  dead declarations, creator-side.
- `src/kg/kg_particle_store.cpp` / `src/core/particle_system.cpp`
  turtle doors already strict (TURTLE_LENIENT) — the precedent, no
  action.
- `Particle::GetMass()/GetMomentOfInertia()` (src/particle.h, not in
  scope): whether degenerate dimensions can yield I<=0 at mass>0 is
  the flow B2/B3's refusals will inventory.

---

## Totals (inventory phase)

- Candidates cataloged: 40
- DESTROY: 13 (A1 A2 A3 A4 A5 A6-part B1 B2 B3 B4 B6 B10 B11 B12 B13-warn D1 D2 E1 — grouped into slices below)
- KEEP-MECHANISM: 19 (each with its justification above; review can veto any)
- CATALOG for owner decision: 8 (A6-max_strain A9 A13 B15 B16 B17 B18 C5 + the G list)

## Slices (one commit each)

1. `docs`: this inventory.
2. `gluon-doors`: A1 A2 A3 A4 A5 A6 + refuse_undeclared_bond extension + B10 B11 B12 (GLUON_LENIENT / PHYSICS_LENIENT).
3. `solver-core`: B1 B2 B3 B4 B6 (PHYSICS_LENIENT).
4. `structs+detector`: E1 D1 D2 B13-warn.
5. Breakage inventory appended below + TEST_AUDIT/LEDGER updates.

## Breakage inventory (post-destruction, 2026-08-13)

Instruments: the 12-gate battery per slice (baseline captured at HEAD
5958ea4 before any change), then the full physics sweep (285 audited
tests, strict doors, run alone, 9.5 min). Every mole reproduced solo
before classification.

### Baseline (before destruction)

All 12 battery gates PASS except test_humanoid_tuning_coverage
(pre-existing red: 8 bonds carrying the 100/10 class default).

### New loud failures, classified by guilty flow

1. **test_spirit_light_artifacts — SIGABRT at the INV-22
   duplicate-pair door (B10). REAL, the campaign's catch.**
   Guilty flow: `ParticleSystem::clear_particles()`
   (src/core/particle_system.cpp:174) clears every particle and resets
   the id counter but never clears physics bonds. The test's Test 6
   respawns a humanoid after a bulk clear; new particles reuse ids
   1, 2, ... and the FIRST scene's bonds re-bind to them — 28 stale
   bonds counted under GLUON_LENIENT. Until today those 28 ghost bonds
   silently acted on an unrelated world (same disease family as the
   deferred-deletion stale-raw-index RCAs). Class: genuinely
   uncontrolled flow — bond lifecycle across bulk particle clear.
   NOT fixed here (a two-system lifecycle mechanism, not a one-line
   declaration); owner decision. GLUON_LENIENT=1 completes the run and
   the test PASSES (rendering was never affected — which is exactly
   why nothing caught it).
   Audit row updated: known_open "fallback-destruction:GLUON_LENIENT".

2. **test_inv29_constants_gate — ratchet drop, by design.**
   Destroying the header's 100.0f/10.0f, the solver's 0.01f invented
   inertia and the detector's 999.0 shrank the pinned residual count
   (35 -> 31 sites, 17 -> 14 token-groups). The gate demands its table
   shrink in the same effort; done (tests/test_inv29_constants_gate.cpp),
   gate green again. Class: sanctioned follow-through.

3. **test_physics_battery — HUNG at the sweep's 240 s deadline.
   NOT reproducible.** Solo: 2/2 PASS, full scenario ladder green,
   0.19 s wall time. The sweep-time stall is the documented
   phantom-mole class (engine/GPU init stall), not a physics refusal —
   no REFUSED line in any battery output. Class: infrastructure flake.

4. **test_async_prep_equivalence — FAIL rc 1 in the sweep, PASS solo.**
   GPU pixel-equivalence test, known contention-sensitive. No physics
   door involved. Class: infrastructure flake.

### Loud lines that fire without failing (contract statements)

- `[PHYSICS CONTRACT] add_force` (B13) fires in test_ancient_oak and
  test_tree_wiggly: those fixtures register a GravityForce the V4
  solver never applies. Guilty flow: force-registry callers believing
  registration means application. Owner decision queued (implement or
  delete the registry).

### The flagship, restated in the new sentinel

test_humanoid_tuning_coverage still FAILS (expected, pre-existing):
8 bonds on the humanoid carry the UNDECLARED sentinel — now 0/0
(absence) instead of 100/10 (a number pretending to be a choice).
All 8 hang off the head particle (face/hair bond creation in
humanoid_generator.cpp gives them no angular opinion). Guilty flow:
missing declaration at a creation site; the fix is per-joint
declarations or derivation, owner's tuning call. The other 20 joint
bonds' generator declarations are all overwritten by
humanoid_locomotion's 2000/60 init pass (dead declarations, listed
in G).

### Doors that did NOT fire anywhere in the fleet

The breaking-force door (A5), the zero-inertia refusals (B2/B3), the
NaN door (B4), the turtle eff-mass refusal (B1), the stale-gluon abort
(B11), the creation-door refusals (B12) and the detector env guard
(D2) fired in ZERO of 285 tests. Those flows are now provably
controlled: the doors exist, armed, and today's fleet passes through
them clean.

### Bottom line

- Provably controlled now: bond force laws (linear + angular +
  breaking) at both creation doors; pair ownership at the index;
  solver masses/inertias (no invented 1.0 kg / 0.01 kg*m^2); NaN
  velocities; unset constraint jacobians; detector calibration input.
- Uncontrolled, named, pending owner decisions: bond lifecycle across
  bulk particle clear (the spirit-light flow); the inert force
  registry (B13); ElasticGluon::max_strain silently ignored (A6);
  enable_angular_constraint's per-bond default policy (A9); dead
  PhysicsConfig::restitution (A13); the 5.0 structural-damping floor
  overriding material declarations (B17); the humanoid 2000/60
  overwrite + 8 opinion-less head bonds (creator side).
