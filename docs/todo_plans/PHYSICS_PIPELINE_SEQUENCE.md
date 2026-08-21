# The physics pipeline, as it actually executes

Slice 1 of the order-capture build (task #52). This document supersedes the
two contradicting order comments inside `physics_system_v4.cpp` (the file
header and the "ORDER (per substep)" block) — neither matches the code, and
until now no correct written order existed anywhere in the tree. Node
identity is by NAME; line numbers drift (the silent-fallback destruction is
editing the same file as this is written) and are deliberately omitted.
This is the source from which `edges.jsonl` gets formalized (slice 3), and
what the phase-entry replay test (slice 4) must reproduce.

Scope key: F = once per frame, S = once per substep (4 per frame),
I = per velocity iteration (≤32 per substep). Live: yes unless noted.

## Frame level (`PhysicsSystem::update`)

| # | node | scope | notes |
|---|---|---|---|
| 1 | `prune_invalid_gluons` | F | removes stale-index gluons before anything reads them |
| 2 | `save_pre_constraint_omega` | F | fills `pre_constraint_omega_z_` — **DEAD CARRIER**: its only documented consumer is behind `ENABLE_GLUON_POSITION_PROJECTION = false`; O(n) paid every frame |
| 3 | substep loop ×4 | F | nodes 4-20 below |
| 21 | `update_rest_state` (sleep law) | F | consumes `constraint_dissatisfied_` — **STALENESS WINDOW**: that carrier is rewritten every substep by node 10, so the sleep law sees only the LAST substep's dissatisfaction; nothing documents this. **G-44 (2026-08-20)**: quietness is one currency (v^2 + omega^2 * r_ext^2) and entry requires it NON-GROWING (REST_GROWTH_RUN consecutive growing frames block sleep; alternating jitter sleeps). Entry zeroes BOTH velocity halves. A cache may only cache a fixed point |
| 22 | `remove_marked_gluons` | F | wake-on-break lives here (a sleeping segment whose bond tore must fall) |
| 23 | explosion detector sample+judge | F | one pass over final velocities; runs AFTER rest-update zeroes sleeping velocities — an `invalidates` edge worth capturing |

## Substep level (inside node 3)

| # | node | scope | notes |
|---|---|---|---|
| 4 | energy ledger snapshot (pre) | S | `ENERGY_LEDGER=1` only |
| 5 | `apply_all_forces` → gravity | S | one predicate (`inv_mass_momentum`) decides receivers; quat-driven non-PHYSICS exemption follows it |
| 6 | `solve_contacts_v3` entry | S | nodes 7-17 |
| 7 | gluon pair set + bonded components (union-find) | S | feeds the internal-contact skip (INV-22) |
| 8 | turtle contact detect + rows | S | oriented down-reach since OBB; single-body rows |
| 9 | box-box broad phase (BVH) → narrow phase (OBB SAT / AABB / sphere) → contact rows | S | manifold split (eff/N); eff 0 fallback rules |
| 10 | gluon row build | S | in ONE loop per bond: attachment points (rotated), **wake-on-strain fires here**, `constraint_dissatisfied_` written here, material damping as reduced-mass impulse, 3 axis rows + budgets, quat-drive angular rows, immovable-pair build skip (after wake), warm impulse-memory apply |
| 11 | constraint shuffle (lever) | S | `LOGOSPHERE_PHYS_SHUFFLE` — measurement lever, off by default |
| 12 | row mass refresh (shrink-only) | S | contact AND gluon rows shrink to the live predicate — the compensator for the build-before-wake edges (`repaired_by`, INV-8) |
| 13 | warm start apply (contacts) | S | **rebuilt 2026-08-20 (G-43/G-45)**: contact rows GROUP by full ContactKey (hash dedup retired with its documented collision defect); cached impulse distributes across the group by eff_mass_share and applies through the FULL Jacobian (linear + angular, same gates as the iterations — warm start is iteration zero, not a second solver). Speculative rows (bias < 0) receive nothing; turtle rows with no penetration receive nothing. Store side sums the group. Gluons excluded by design |
| 14 | velocity iterations | I | per row: angular rows, linear impulse via the door, friction (2 tangents); exits: converged (impulse threshold — known INV-10 debt), plateau (rate), exhausted. **G-45 (2026-08-20)**: friction acts only through a TOUCHING contact — rows with bias < 0 (gap open, the row's own classification) have friction_limit forced to 0; their normal half keeps its approach-limiting job. **G-43**: contact v_rel measures omega-cross-r at the anchor in BOTH branches (turtle included), measure-gate == apply-gate (solver_mode DYNAMIC for contacts) |
| 15 | position pass (split impulse) | S | own predicate (`inv_mass_positional`: sleep IS movable), SLOP tolerance, pseudo-impulse discarded (INV-21); turtle rows excluded (boundary owns them — INV-22) |
| 16 | warm cache write-back + impulse memory store (organic) | S | momentum-unit caps (INV-10) |
| 17 | tear / breaking check | S | force AND strain laws; TEAR_DEBUG frame-stamped |
| 18 | `integrate_angular_velocities` | S | quat integrate + Euler publish (publish skips sleepers — the known orientation-truth gap, ruling deferred) |
| 19 | `project_angular_limits` | S | hard bounds, multiple sweeps |
| 19b | `project_gluon_positions` | S | **DISABLED** (`ENABLE_GLUON_POSITION_PROJECTION=false`); the would-be consumer of carriers 2 and `active_contacts_` |
| 20 | `integrate_positions` | S | velocity → position; contains quadratic air drag AND cluster-relative structural damping (the schema's `ForceApplication` concept wrongly co-locates these with gravity, node 5 — recut pending per ruling D2) |
| 20b | `enforce_turtle_boundary` | S | geometric lift with SLOP; the one sanctioned non-row repair |
| 4b | energy ledger snapshots (post-solve/angular/positions/turtle) | S | ledger mode only |

## Seed edges for `edges.jsonl` (slice 3), each with its RCA

- `must_see_live_state_of(contact_row_build → wake)`: contacts (9) build
  before wake-on-strain (10) fires. Unsatisfiable by reorder (contacts must
  precede bonds for manifold reasons); `repaired_by` row-mass-refresh (12).
  Evidence: the 245 m/s blade; the ±110 m/s heel strike (gluon-row twin).
- `must_see_live_state_of(gravity → wake)`: gravity (5) runs before
  wake-on-strain (10); a body woken mid-frame missed its gravity that frame.
  Evidence: the P7 trunk investigation (gravity-probe placement RCA).
- `requires_before(integrate_angular → project_gluon_positions)`: rotation_z
  must be current before position projection (the door-hinge comment).
- `invalidates(update_rest_state → detector)`: rest update (21) zeroes
  sleeping velocities before the detector (23) samples.
- `produces/consumes(gluon_row_build → update_rest_state)` over
  `constraint_dissatisfied_`: per-substep producer, per-frame consumer,
  one-substep staleness window, undocumented.
- `requires_before(row_mass_refresh → warm_start_apply)`: caps must be
  live-scaled before warm impulses are clamped against them.

## Known hazards already on the books

1. `pre_constraint_omega_z_` — dead carrier, O(n)/frame (node 2).
2. `active_contacts_` — dead carrier; only reader is behind the disabled 19b.
3. `constraint_dissatisfied_` staleness window (nodes 10→21).

Next slices per the approved plan: reads/writes per node + the
producer/consumer hazard scan (the load-bearing deliverable), then
edges.jsonl + validator topo-sort, then the 27 phystrace level-3 emits and
the replay diff test, then the schema process-concept recut (D2), then the
layer-2 experiment (D3).
