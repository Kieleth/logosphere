# Effect Composition Inventory

_Read-only survey. What this tree already does when more than one thing
acts on the same subject at the same time._

**Why this exists.** `tests/invariants/LEDGER.md:812-816` records the
owner's question verbatim:

> "effects must be able to INTERACT WITH EACH OTHER. When several are in
> play at once the outcome must be resolved deterministically — 'a
> logical, prolog way to solve when multiple things are in effect at the
> same time'."

This document answers only the prior question: **how does the engine
resolve simultaneous effects today?** It makes no recommendation about
Kamaji's design.

**Notation.** `[E]` established from code read at the cited line. `[I]`
inferred, with the inference named. `[E*]` established by a parallel
sweep and re-verified line-by-line in this pass. `[E†]` established by a
parallel sweep, cited but not independently re-read — treat as strong
but unconfirmed.

**Method.** Determinism is claimed from the declared container type, not
from intent. Every `std::unordered_map` / `unordered_set` whose
iteration feeds a decision is listed, whether or not the consequence is
observable today.

---

## PART 0 — Executive summary

### The shape of the answer

There is **no general arbiter**. Each site carries its own merge law,
written where the merge happens. "Two effects interacting" is not a
concept this engine has; "two effects both applying" is.

Composition patterns already present, by name:

| Pattern | Representative site |
|---|---|
| accumulate-sum | impulse inbox `particle_interaction_system.cpp:265`; damage HP `damage_system.cpp:88` |
| min-merge (most restrictive wins) | `speed_cap` `effect_registry.cpp:22`; cascade `capability_profile.cpp:155`; friction `physics_system_v4.cpp:3514` |
| max-merge | `outcome_executor.cpp:257` |
| arithmetic mean | material damping `materials.h:158`; bond breaking strength `physics_system.h:313` |
| compound multiply | `cap_modifier` `effect_registry.cpp:43` |
| veto / fail-closed | unknown condition `contact_response.cpp:127`; sleep law `physics_system_v4.cpp:4893`; `binary` capability mode `capability_profile.cpp:313` |
| static precedence | `cap_disable` > `cap_modifier` `capability_profile.cpp:332`; own rule > cascade `:231`; locomotion MODE ladder `humanoid_locomotion.cpp:325/342/517` |
| three-tier answer precedence | `outcome_executor.cpp:731-774` |
| consume / stop propagation | UI mouse routing `input_system.cpp:368-386` — **the only one in the tree** |
| last-write-wins by pipeline order | animation after physics `engine.cpp:1266-1268`; journal delta `journal_render.h:85` |
| all-match, unspecified order | contact rules `particle_interaction_system.cpp:364`, `:554` |
| exclusive ownership, enforced | `index_gluon` `physics_system_v4.cpp:5494-5504` (`abort()`) |
| exclusive ownership, skipped | gluon-owns-pair `:1129`; bonded structure `:1140` |
| overlap is a load error | `lookup_table_selector.cpp:137-145`; `rollable_table_runner.cpp:99-105` |
| total-function proof | `rollable_table_runner.cpp:117-134` |
| first-of-list, ambiguity admitted | `particle_interaction_system.cpp:227-230` |
| layered merge: region mask + crossfade + additive channel | `animation_types.h:547-613` |

### The five things that matter most

1. **`rules_` is an `unordered_map` and it decides an OUTCOME, not just
   an order.** Two `SWAP_PROFILE` rules matching one episode both write
   `interaction_profile_id`; the winner is whichever hash order visits
   last (`particle_interaction_system.cpp:554` + `:525`). This is
   sharper than the ordering defect already noted in
   `INTERACTION_ROUTER_DESIGN.md:570-580`.

2. **The `src/rules/` layer resolves ambiguity by refusing it.** Eleven
   distinct "would-be tie" conditions are load-time errors. It is the
   only subsystem in the tree with a coherent doctrine, and it is the
   one the router design already echoes
   (`INTERACTION_ROUTER_DESIGN.md:832-836`).

3. **There is no condition language in `src/rules/`.** The typed
   expression ontology exists in `schema/packs/rule_language.yaml` with
   **no operator classes and no evaluator**. Everything executable is
   C++ handlers. `[E†]`

4. **"May this body move" has eight live formulas**, where the file's own
   comment celebrates reducing three to one on the momentum axis only
   (§3.1).

5. **`RULE_LANGUAGE.md:945-947` bans production-rule semantics by
   decision** — no automatic firing, no retraction, no conflict sets, no
   mutation ordering. Any resolution layer has to be reconciled with
   that ruling or supersede it explicitly. `[E†]`

---

## PART 1 — The composition table

Subject = the field written. Order = what determines the sequence of
contributions.

### 1.1 Interaction / contact rules

| # | Subject | What composes | Semantics | Order source | Determ. | Tie | Site |
|---|---|---|---|---|---|---|---|
| C1 | one contact pair | every `ON_CONTACT` rule whose condition passes | **all-match, all apply** | `std::unordered_map<uint32_t, TransformationRule> rules_` | **NO** | no tie concept; both fire, so two `knockback` rules mean **double impulse** | `src/interaction/particle_interaction_system.cpp:364-369`; member `include/logosphere/interaction/particle_interaction_system.h:335` |
| C2 | `particles[idx].interaction_profile_id` | every matching `SWAP_PROFILE` rule on one episode open | **last-write-wins** | same `rules_` iteration | **NO — the OUTCOME depends on hash order** | last visited wins | `particle_interaction_system.cpp:554` + `:521-526` |
| C3 | `out_delete` | every matching `DELETE_PARTICLE` rule | **append, duplicates allowed** | same | NO | same index pushed twice; dedup left to deferred deletion | `particle_interaction_system.cpp:527-529` |
| C4 | one pair, both sides | rules evaluated once per side | fixed loop `side = 0,1` | source | YES | a symmetric rule fires twice by design | `particle_interaction_system.cpp:356-370` |
| C5 | particle's pending impulse | every `knockback` that fired | **accumulate-sum**; header states the intent: *"two things hitting you from opposite sides should cancel, not race"* | deposit order | order-dependent only in float rounding | opposite deposits cancel to 0 | `particle_interaction_system.cpp:265-272`; header `:221-223`; test `tests/test_contact_bounce.cpp:398-411` |
| C6 | impulse drain | N would-be consumers | **destructive single-consumer**: first `take_impulse` gets everything, second gets `false` | caller order | YES | no arbitration | `particle_interaction_system.cpp:274-283` |
| C7 | condition evaluation | one condition per rule | **fail-closed + stderr**; empty condition = unconditional | n/a | YES | n/a | `src/interaction/contact_response.cpp:112-131` |
| C8 | effect application | one effect per rule | **fail-loud, no-op** on unknown name | n/a | YES | n/a | `contact_response.cpp:196-203` |
| C9 | condition composition | — | **impossible**: one expression per rule, `split_expr` takes the first colon only. No AND/OR over contact conditions | n/a | n/a | n/a | `contact_response.cpp:17-26` |
| C10 | registry entry per name | N registrations of one name | **last-registration-wins, silent**; header says *"Register or replace"* | cross-TU init order | NO in principle | a game silently replaces a built-in | `contact_response.cpp:105`, `:185`; header `contact_response.h:124`, `:135`, `:154` |
| C11 | `rules_[r.id]` | reloading the same rule entity | **last-load-wins, silent** | `findByType` → insertion-ordered vector | YES | n/a | `particle_interaction_system.cpp:483` |
| C12 | contact episode set | substep duplicate pairs | first occurrence wins, rest dropped | `unordered_set` membership (not order) | YES | n/a | `particle_interaction_system.cpp:305-312` |
| C13 | part → entity attribution | several `HAS_PART` owners | **first of list**, ambiguity admitted in the comment | `getRelatedReverse` → `std::vector<RelationID>` insertion order | YES | `.front()` | `particle_interaction_system.cpp:226-230`; index type `include/logosphere/kg/kg_core.h:170` |
| C14 | volume episode close | overlaps per pair | dedup, then set difference | `std::unordered_map<uint64_t,uint32_t> open_episodes_` iterated at `:191` | emission order **NO** | n/a | `particle_interaction_system.cpp:150-200` |
| C15 | intruder velocity | drag + buoyancy + field, per medium | **sequential, all three**; and **each side of every pair applies**, so two overlapping media both act | hardcoded, then overlap vector | YES | no "which medium am I in" question exists | `particle_interaction_system.cpp:650-697` |

### 1.2 Capability / response rules

The densest composition surface, and the only one with a documented
cross-effect precedence rule.

| # | Subject | What composes | Semantics | Order source | Determ. | Tie | Site |
|---|---|---|---|---|---|---|---|
| K1 | `speed_cap` | every `speed_cap:<v>` | **min-merge** | irrelevant | YES | most restrictive wins | `src/capability/effect_registry.cpp:18-23` |
| K2 | one capability's modifier | every `cap_modifier:<cap>:<f>` | **compound multiply** | parts vector × rule index 0..7 × effect slot 0..3 | YES (float `*=` is reproducible, not associative) | both apply, product | `effect_registry.cpp:33-44` |
| K3 | one capability | `cap_disable` vs `cap_modifier` | **disable wins**, explicit | n/a | YES | absolute | `src/capability/capability_profile.cpp:325-337` |
| K3b | a capability with no contributing part | disable vs modifier on a missing key | **asymmetric**: disable uses `operator[]` and creates the entry; modifier uses `find()` and skips | n/a | YES | two behaviours for one situation | `capability_profile.cpp:329` vs `:333-336` |
| K4 | cascade factor onto a part | every cascade clause reaching it | **min-merge** | irrelevant | YES | most damaging wins | `capability_profile.cpp:152-156` |
| K5 | a part's effective health | own rule vs inherited cascade | **own rule vetoes cascade entirely** (not multiplied, not min'd) | first-triggering rule, `break` | YES | local beats inherited | `capability_profile.cpp:221-236` |
| K6 | a capability factor | contributions from N parts | mode-selected: `average` / `minimum` / `binary` / `weighted_sum`; unknown mode string leaves `factor = 0` **silently** | `parts` vector, then `unordered_map` for the mode pass | accumulation YES, mode-pass order NO (harmless: distinct keys) | default `average` unless named | `capability_profile.cpp:249-266`, `:292-323` |
| K7 | `binary` mode | any part below threshold | **veto**: `any_below_threshold` is a sticky OR across all parts, each of which may declare its own threshold | irrelevant | YES | any-veto | `capability_profile.cpp:255-260`, `:313-314` |
| K8 | rule activation | rule group vs entity's group flags | **fail-closed**: undeclared group → skip | n/a | YES | n/a | `capability_profile.cpp:127-133` |
| K9 | trigger evaluation | one trigger expression | **fail-closed, SILENT** on unknown name | n/a | YES | n/a | `src/capability/trigger_registry.cpp:170-172` |
| K10 | compound trigger | atoms joined by ` AND ` / ` OR ` | short-circuit; **mixed operators REFUSED** rather than resolved by assumed precedence | left to right over a vector | YES | n/a | `trigger_registry.cpp:87-151`, refusal at `:121` |
| K11 | rule slot scan | `rule.0` … `rule.7` | **first-match-wins** with `break`; a **numbering gap silently truncates** the remaining rules | fixed integer order | YES | n/a | `capability_profile.cpp:163-167`, `:223-230` |
| K12 | effect slots on one rule | `.effect`, `.effect_2..4` | **all dispatched, fixed slot order**; gap truncates | fixed | YES | no precedence between effects | `capability_profile.cpp:178-183` |
| K13 | `side_hints[cap.side]` | two parts declaring the same cap+side | **last-write-wins**, no min, no average | `parts` vector order | YES | winner is an accident of HAS_PART creation order | `capability_profile.cpp:262-265`, consumed `:349-352` |
| K14 | `left_leg_factor` etc. | `get_cap()` result vs `side_hints` | **side hint overwrites**, a second last-write layer on the same four fields | fixed | YES | n/a | `capability_profile.cpp:344-352` |
| K15 | KG `capability.*` | five explicit writes, then a generic loop rewriting four of the same keys | **last-write-wins, generic map wins** — except when a capability is absent from the map, where the explicit write survives | `unordered_map` iteration | KG state YES, **event sequence NO** | which writer wins depends on map membership | `src/capability/capability_store.cpp:100-120` |
| K16 | store recompute | the store's own writes | **two-layer echo suppression**: `writing_` flag + `capability.` prefix guard | n/a | YES | suppresses the store's own handler only; the nested emit still reaches the other subscribers | `capability_store.cpp:19`, `:82`, `:103`/`:119` |
| K17 | effect / trigger registry entry | N registrations of one name | **last-registration-wins, silent**; process-wide singletons | cross-TU init order | NO in principle | n/a | `effect_registry.cpp:66-68`; `trigger_registry.cpp:154-156` |

**"What happens when two rules disagree" has no single answer.** Each
effect type carries its own law (K1 min, K2 multiply), there is one
cross-effect precedence rule (K3), one scope precedence rule (K5), and
no priority field, no ordering key, and no conflict diagnostic anywhere.

### 1.3 Damage / tissue / materials

| # | Subject | What composes | Semantics | Order source | Determ. | Tie | Site |
|---|---|---|---|---|---|---|---|
| D1 | `EntityHealth::hp` | N damage events | **sequential subtract, clamp at 0** | caller order | YES (`entities_` is keyed, never iterated) | **first kill wins**: once `dead`, later damage returns 0 at entry and emits **no event** | `src/damage/damage_system.cpp:63-105`, early-out `:68`; test `tests/test_damage_events.cpp:126-135` |
| D2 | `final_damage` | damage type × resistance | **first-match switch, then multiply**; `Pure` is an explicit override | n/a | YES | `Cold` and `Poison` fall to `default:` → **structurally unresistable**, silently | `damage_system.cpp:150-162`; enum `include/logosphere/damage/damage_system.h:35-49` |
| D3 | `pierce_resistance` | `Pierce` and `Bite` writes | **two damage types alias one float, last-write-wins**; unobservable through the API because the getter aliases too. Header comment says Bite is *"pierce + blunt"*; blunt is never consulted | n/a | YES | setting one silently clobbers the other | `damage_system.cpp:120-133`, `:143-144`; comment `damage_system.h:39` |
| D4 | entity HP **and** part `"health"` | one damage event | **two independent pools, resistance applied twice to the raw damage**; the part does not absorb from the entity pool | `getRelated` vector | YES | **first part whose `body_part_name` matches wins**; later duplicates untouched, no diagnostic | `damage_system.cpp:164-208`, resistance at `:172` and `:193` |
| D5 | contact / gluon damping | two materials | **arithmetic mean** | symmetric | YES | n/a | `src/materials.h:155-159` |
| D6 | gluon damping | material pair vs the bond's declared `damping` | **override gated by a veto** (`force_bounded()`), then clamp to [0, 0.95]. The comment names the bug: the material table used to win unconditionally | n/a | YES | declaration beats material pair — **for bonds only** | `src/core/physics_system_v4.cpp:1798-1811` |
| D7 | contact damping | same two materials | **no override exists** | n/a | YES | same physical quantity, two combination rules chosen by code path | `physics_system_v4.cpp:1410` |
| D8 | `material_density`, `material_strength` | `SetMaterial` vs a generator literal | **last-write-wins, made a documented contract** (*"must say so AFTER this call"*). `material_type` is left pointing at the material whose numbers were discarded | call sequence | YES | `material_type` and `material_strength` can disagree | `src/particle_core.h:141-150`; overriders in `src/worldgen/*` |
| D9 | bond breaking force | two `material_strength` values | **average × contact_area** | symmetric | YES | `NailGluon` / `ElasticGluon` ignore materials entirely and return a flat declared force — participation is decided by subclass | `include/logosphere/physics/physics_system.h:311-315`, `:297-299`, `:344-346` |
| D10 | pair friction | two per-particle scalars | **min**, deliberately a veto (`friction=0` on either side kills it) | symmetric | YES | n/a | `physics_system_v4.cpp:3510-3514`; doc `src/particle_core.h:152` |
| D11 | pair friction (dead path) | a material-pair lookup table | exact-pair first-match, hardcoded `0.8f` fallback; symmetric `==` **and** symmetric XOR hash, so they agree | keyed lookup only | safe | **never populated, `get_friction` has zero callers** | `include/logosphere/dynamics/particle_dynamics_system.h:664-676`; `src/core/particle_dynamics_system.cpp:283-316` |
| D12 | restitution | — | **no per-material and no per-pair restitution exists**; `config.restitution` has no readers | n/a | n/a | n/a | `physics_system.h:53` |
| D13 | tissue health | damage type × tissue | **declared in prose only.** `HasTissue` names `CUT` and `CRUSH`, which are **not in the `DamageType` enum**. Nothing decrements a tissue value; nothing reads one | n/a | n/a | n/a | `schema/logosphere.yaml:505-512`; generated e.g. `src/generated/arms_ontology.h:1007-1017`; sole writer `src/worldgen/humanoid_generator.cpp:2299-2302` |

### 1.4 Physics solver

| # | Subject | What composes | Semantics | Order source | Determ. | Tie | Site |
|---|---|---|---|---|---|---|---|
| P1 | one body's velocity | every row touching it | **sequential impulse (Gauss-Seidel)**: each row applied against velocities earlier rows changed | `std::vector<Constraint>` build order | YES, deliberately permutable by seed | n/a | solve from `physics_system_v4.cpp:2570`; lever `:2630-2641` |
| P2 | one manifold's rows | N contact points on one pair | effective mass **divided by point count** | manifold point order | YES | `SPLIT_OFF=1` disables the share | `physics_system_v4.cpp:1419-1431` |
| P3 | a pair with a gluon and an overlap | bond row vs contact row | **gluon owns the pair; the contact row is not built** | n/a | YES | n/a | `physics_system_v4.cpp:1121-1133` |
| P4 | two bodies in one bonded structure | bonds vs internal contacts | **bonds own it, transitively**, union-find over the gluon graph through DYNAMIC bodies only, rebuilt every step | `std::vector<uint32_t> bond_root` | YES | n/a | `physics_system_v4.cpp:810-852`, test `:1140` |
| P5 | one pair, two bonds | second `index_gluon` for one key | **refuse loud, `abort()`** (INV-22); `GLUON_LENIENT` degrades to overwrite | n/a | YES | n/a | `physics_system_v4.cpp:5487-5506` |
| P6 | contact impulse | last frame's cache + this frame's solve | **warm start replaces, does not add** | key-hash dedup — see N3 | see N3 | `stored_keys` skips the second row for a key | `physics_system_v4.cpp:2730-2900`, `:3902-3997` |
| P7 | position vs velocity | Baumgarte bias | **split**: velocity solve bias-free, position repair into a discarded pseudo-velocity | fixed phase order | YES | turtle rows excluded — the boundary owns them | `physics_system_v4.cpp:4000-4189` |
| P8 | conflicting SAT normals | N contacts against adjacent statics | **sum the normals, normalise, overwrite all of them** | `std::unordered_map<size_t, std::vector<size_t>>` | order NO, result YES (groups disjoint) | needs 2+ contacts, an adjacent AABB pair, and sign opposition | `physics_system_v4.cpp:2914-3041` |
| P9 | a sleeping body's fate | solved velocity vs sleep bound | **three-way**: wake / cache absorbs / no-op | fixed | YES | boundary at `REST_VELOCITY_THRESHOLD` | `physics_system_v4.cpp:570-605` |
| P10 | may a body sleep | every row touching it | **any-dissatisfied vetoes rest** (`std::vector<uint8_t>` OR) | irrelevant | YES | n/a | `physics_system_v4.cpp:1734`, `:1918-1921`, `:4885-4896` |
| P11 | row effective mass | build-time mass vs post-wake mass | **shrink only, never grow** | constraint vector | YES | n/a | `physics_system_v4.cpp:2691-2728` |
| P12 | structural damping | cluster shared motion vs internal oscillation | damp **only the deviation from the mass-weighted cluster mean** | `unordered_set` accumulation | float-sum order NO | n/a | `physics_system_v4.cpp:4544-4590` |
| P13 | a KINEMATIC body's z | external writer vs turtle boundary | **turtle wins, unconditionally** — §3.2 | fixed phase order | YES | n/a | `physics_system_v4.cpp:4794-4851` |

### 1.5 Animation / pose

| # | Subject | What composes | Semantics | Determ. | Site |
|---|---|---|---|---|---|
| A1 | which clip plays | turn-in-place / walk-run / idle | **static priority ladder** (`if / else if / else if`) | YES | `src/animation/humanoid_locomotion.cpp:325`, `:342`, `:517` |
| A2 | walk vs strafe vs run | two `RotationPose`s | **weighted lerp per joint+channel key**; a joint in only one side is scaled by its own weight | listing order NO, applied state YES | `include/logosphere/dynamics/animation_types.h:456-511` |
| A3 | locomotion vs one-shot | two poses + a body region | **region mask + crossfade**; overlay owns its region, base keeps the rest | YES | `animation_types.h:547-613` |
| A4 | run lean | lean angle vs existing FLEX target | **accumulate-sum** onto the first matching joint+FLEX, else push new | YES (keys unique) | `humanoid_locomotion.cpp:405-419`, `:495-513` |
| A5 | clip twist vs look-at twist | two writers of one channel | **save, clear all, apply clip, then add look-at back additively** | YES | `humanoid_locomotion.cpp:604-631` |
| A6 | a bone's position, whole frame | dynamics pre-physics, solver, dynamics post-physics | **last-write-wins by pipeline order**, stated in a comment: *"Apply kinematic animations AFTER physics to prevent gluon solver from undoing them"* | YES | `src/core/engine.cpp:1176-1181`, `:1196-1208`, `:1266-1268` |

### 1.6 Events

| # | Subject | What composes | Semantics | Determ. | Site |
|---|---|---|---|---|---|
| E1 | one channel's subscribers | N subscribers | **all run, registration order, no veto, no consume**; handler is `std::function<void(const T&)>` — returns `void`, so a veto is not expressible | YES (`std::vector<Slot>`) | `include/logosphere/events/signal.h:29`, `:32`, `:62-69` |
| E2 | which channel | — | channels are **compile-time members, dispatched by overload**, not runtime keys — there is no channel map to hash | YES | `include/logosphere/events/event_bus.h:24-35` |
| E3 | emit | synchronous dispatch + journal append | **both, signal first**; a subscriber cannot prevent the event reaching tier 2 | see §3.5 | `include/logosphere/events/event_channel.h:29-37` |
| E4 | journal retention | N events past capacity | **ring buffer, oldest overwritten**, default 1024; readers pull oldest-first by seq, a lagging reader is clamped forward with the loss counted | YES | `include/logosphere/events/event_log.h:54`, `:112-120`, `:216-222` |
| E5 | unsubscribe during emit | slot nulled, compaction deferred | **deferred delete** | YES | `signal.h:52-59` |
| E6 | subscribe during emit, nested emit | — | **unguarded — see §3.5** | — | `signal.h:62-69` |
| E7 | N `STATE_CHANGE` on one property | several deltas | **latest value wins, first `prev` kept**, net no-ops dropped | YES (`std::map` + explicit comparator) | `include/logosphere/events/journal_render.h:68-96` |
| E8 | mouse button | UI vs camera controller | **consume / stop propagation** — handler returns `bool`, `true` returns early | YES | `src/core/input_system.cpp:368-386` |
| E9 | `state_changes()` subscriber order | locomotion, capability store, physical state, recorder | all run, registration order — but **registration order is an accident of `Engine::initialize()` line order**, never declared as precedence | YES, fragile | `src/core/engine.cpp:418`, `:437`, `:453`; subscribers at `humanoid_locomotion.cpp:122`, `capability_store.cpp:15`, `entity_physical_state.cpp:153` `[E†]` |

E8 is the only place in the tree where a handler can stop something
downstream from running. The event bus cannot.

### 1.7 KG mutation / transaction

| # | Subject | What composes | Semantics | Determ. | Site |
|---|---|---|---|---|---|
| G1 | a batch of KG ops | N ops | **applied strictly by index, authored order, no sort, no grouping** | YES (`std::vector<KGOp>`) | `src/kg/kg_ops_transaction.cpp:221`, `:271` `[E†]` |
| G2 | properties inside one `create_entity` | N properties | **lexicographic key order, not authored order** — the JSON object is `nlohmann::json` whose default `ObjectType` is `std::map`; `ordered_json` is never used | YES but **wrong order** | `src/kg/kg_ops_parse.cpp:88-90`; `vendor/nlohmann/json.hpp:3399-3422` `[E*]` |
| G3 | duplicate keys in one `properties` object | two writes of one key | **collapse silently at parse**, last text occurrence wins, no warning | YES | same as G2 `[E†]` |
| G4 | two ops writing one entity+property | two `set_property` | **silent last-write-wins**, no detection, no report field | YES (index order) | `kg_ops_apply.cpp:49` → `kg_core.cpp:326` `[E†]` |
| G5 | duplicate alias binder in one batch | two `as` bindings | **hard fail** | YES | `kg_ops_transaction.cpp:235-239` `[E†]` |
| G6 | duplicate relation in one batch | two `set_relation` | **hard fail**, "relation already exists" | YES | `kg_ops_transaction.cpp:264-269` `[E†]` |
| G7 | partial failure | undo journal | **LIFO reverse application**, events buffered and flushed only on success, bus detached during the batch, report wiped on failure | YES | `kg_ops_transaction.cpp:190-218`, `:304-317` `[E†]` |
| G8 | inherited property override | direct type vs N ancestors | **first-wins into an `unordered_map`**; direct type added first (correct), then ancestors **in alphabetical order** — so between two ancestors declaring one name, the **alphabetically-first ancestor wins**. Output list itself is sorted | YES, but precedence is a name sort, not inheritance depth | `include/logosphere/kg/ontology_registry.h:669-696` `[E*]` |
| G9 | property write gate | a batch mid-flight | `std::abort()` on an undeclared/ill-typed property unless `KG_GATE_LENIENT` — **no rollback, no flush** | YES | `src/kg/kg_core.cpp:285`, `:297` `[E†]` |

### 1.8 The `src/rules/` layer, planners, and generated registries

| # | Subject | What composes | Semantics | Determ. | Site |
|---|---|---|---|---|---|
| R1 | which lookup row applies | N numeric bands | `find_if` first-match — but **overlap is rejected at load**, so first-match is provably only-match | YES (`std::vector<Row>`) | `src/rules/lookup_table_selector.cpp:137-145`, `:192-195` `[E†]` |
| R2 | a miss | no row covers the key | **error**, unless the table declares `miss_is_nothing` — a third outcome distinct from match and error | YES | `lookup_table_selector.cpp:196-208` `[E†]` |
| R3 | which rollable row applies | N bands over dice totals | `lower_bound`, overlap rejected, **plus a totality proof that every reachable total has a row, checked before rolling** | YES | `src/rules/rollable_table_runner.cpp:94-134` `[E†]` |
| R4 | which outcome node runs | a typed graph | **no matching at all** — recursive descent dispatching on `getType`, handlers keyed by exact type, unknown type is a hard failure | YES | `src/rules/outcome_executor.cpp:665-799` `[E†]` |
| R5 | sequence / choice ordering | N parts | **declared integer index, required contiguous from 0**; a gap is an error | YES | `outcome_executor.cpp:628-663` `[E†]` |
| R6 | who answers a choice | caller selection, resolver, nobody | **three-tier precedence**: explicit selection > installed resolver > suspend and ask. **Deliberately no default** — *"picking the first option silently collapses every fork the book prints into one branch"* | YES | `outcome_executor.cpp:731-774`; `include/logosphere/rules/outcome_executor.h:186-189` `[E†]` |
| R7 | binding evaluation order | N ready bindings | **total order**: bytewise binding key, then entity id — cannot itself tie | YES (`std::set` with explicit comparator) | `src/rules/rule_program_validator.cpp:295-301`; spec `docs/RULE_LANGUAGE.md:706-708` `[E†]` |
| R8 | reading a plan in flight | committed graph + pending ops | **`PlannedWorld` overlay**: reverse plan scan, newest write wins, then committed value. **Additive only** — no tombstones, so absence is never ambiguous | YES; relation order is contractual | `src/rules/planned_world.cpp:28-97`; invariant `include/logosphere/rules/planned_world.h:27-33` `[E†]` |
| R9 | procedure step transitions | labelled routes vs fall-through | **labelled goto, else next index**, bounded by `transition_limit_` (1024), overrun is a hard error | YES | `src/rules/procedure_runner.cpp:390-407` `[E†]` |
| R10 | a rule fork | source rule vs overrides | **copy-on-write commit**, not alternative worlds: the fork lands in the live graph immediately, both copies exist, nothing selects between them | YES (sorted relation copy) | `src/rules/rule_fork.cpp:30-137` `[E†]` |
| R11 | GOAP plan selection | N action sequences | A* over lowest f-cost; **no tie-break** — equal cost resolved by binary-heap sift order | **NO in principle** | `src/npc-ai/goap_system.cpp:48-51` `[E†]` |
| R12 | GOAP visited set | N world states | key is a string built by **iterating `WorldState`, a `std::unordered_map<std::string,int>`** — two logically identical states reached by different paths can hash differently and fail to collapse | **NO** | `src/npc-ai/goap_system.h:132`; `goap_system.cpp:61-67` `[E†]` |
| R13 | goal selection | N goals | `Goal::priority` is **declared and never read**; multi-goal arbitration is an open `TODO[ARCH]` | n/a | `src/npc-ai/goap_system.h:196`, `:95-113` `[E†]` |
| R14 | randomness | a roll | **seeded named streams**, xorshift64*, no entropy fallback anywhere, journaled, rule-attributed, transactional | YES | `src/core/dice_service.cpp:153-165`; `include/logosphere/core/dice_service.h:88-99` `[E†]` |

---

## PART 2 — Nondeterministic sites, ranked by blast radius

A deterministic resolution layer cannot be built on top of any of these
without fixing them or routing around them. Ranked by how much of an
observable outcome depends on the order.

### N1 — `rules_` iteration decides an OUTCOME, not just an order

`for (const auto& [rid, r] : rules_)` over
`std::unordered_map<uint32_t, TransformationRule>`
(`include/logosphere/interaction/particle_interaction_system.h:335`),
iterated for firing decisions at
`src/interaction/particle_interaction_system.cpp:364` (contacts) and
`:554` (episode opens).

The `:554` loop calls `apply_effect`, whose `SWAP_PROFILE` case is a
plain assignment:

```
case Effect::SWAP_PROFILE:
    particles[idx].interaction_profile_id = r.target_profile;
```
`particle_interaction_system.cpp:524-526`.

**Two matching `SWAP_PROFILE` rules on one particle resolve by whichever
the hash order visits last.** That is a last-write-wins whose winner is
the hash of a KG entity id. `[E]`

At `:364` the same iteration decides:
- the emission order of `TransformationEvent`s on the bus (`emit_event`,
  `contact_response.cpp:172-180`) — observable to any journal consumer;
- float rounding in the summed impulse;
- the application order of **any game-registered effect**, and the
  registry is explicitly open to games (`contact_response.h:143-155`).

Two rules with the same condition and effect both run: for `knockback`
that is **double impulse**, not "one wins".

`unordered_map<uint32_t,...>` under libc++ hashes integers with
identity, so iteration follows bucket layout — which depends on bucket
count and rehash history. Adding one unrelated rule can reorder every
other rule. `[I]`, from the container's specification; not measured.

The safe use is `:490`, which ORs into a bool and breaks. `[E]`

### N2 — "which entity does this particle belong to" is decided by hash order

`getEntitiesByRenderIndex` iterates `kg_to_render_`, declared
`std::unordered_map<KGParticleID, RenderIndex>`
(`include/logosphere/kg/kg_core.h:177`), collecting matches into a vector
(`src/kg/kg_core.cpp:610-631`). Consumers take element `[0]`:

- `src/entity_system.cpp:198` —
  `return entities.empty() ? kg::INVALID_ENTITY : entities[0];`
- `src/ui/ui_system.cpp:668` —
  `inspector_.anchored_entity_id = entities[0];  // Primary entity for backward compat`
- also `ui_system.cpp:693`, `:813`, `:839`

Multi-entity binding is explicitly supported — the comment at
`ui_system.cpp:665` says *"supports multi-entity binding"* — so **which
entity is "primary" is decided by `unordered_map` iteration order.**
`[E*]`

Blast radius: entity identity, which is upstream of every rule that
tests a type.

### N3 — warm-start dedup compares HASHES, not keys

`std::unordered_set<size_t> applied_keys` holds
`ContactKeyHash{}(key)` and the test is `applied_keys.count(key_hash)`
(`physics_system_v4.cpp:2666`, `:2780`, `:2789-2798`). Store side is the
same shape (`stored_keys`, `:3908`, `:3947-3949`). The in-tree comment
states the defect verbatim:

> "this set holds HASHES, and the dedup below compares hashes, not keys.
> Two DIFFERENT contact pairs that collide in the hash therefore silently
> lose one constraint — and which pairs collide depends on particle
> INDICES, which is exactly what adding one unrelated body shifts."
> (`:2660-2665`)

The hash is a three-term xor mix
(`include/logosphere/physics/physics_solver.h:274-281`). This is not
iteration-order nondeterminism — it is content-dependent silent
constraint loss: deterministic for a fixed scene, and unreasonable
scene-to-scene. `DEDUP_DEBUG=1` instruments it (`:2668`, `:2782-2788`);
no count is recorded here — not measured.

### N4 — `getPropertiesWithPrefix` returns properties in hash order

`for (const auto& [key, value] : entity->properties)` where
`PropertyMap = std::unordered_map<std::string, PropertyValue>`
(`include/logosphere/kg/kg_types.h:83`), at `src/kg/kg_module.cpp:230`.

Consumed directly by the `emit_event` capability effect to build a
`WorldEvent`'s payload as two parallel vectors:

```
auto payload_props = ctx.kg.getPropertiesWithPrefix(ctx.part_id, payload_prefix);
for (const auto& [key, value] : payload_props) {
    evt.payload_keys.push_back(key.substr(payload_prefix.size()));
    evt.payload_values.push_back(value);
}
```
`src/capability/effect_registry.cpp:57-61`. `[E]`

Any consumer reading `payload_keys[0]` rather than searching by name
gets a different answer on a different standard library. `[I]` —
`std::hash<std::string>` differs between libc++ and libstdc++; not
measured.

**Contrast:** `src/kg/kg_query.cpp:65-67` sorts `row.props` explicitly
and `:49` sorts `findByType` results. The query layer knows about this;
the effect layer does not.

### N5 — `create_entity` property order is lexicographic, not authored

`src/kg/kg_ops_parse.cpp:88-90` fills an ordered
`std::vector<std::pair<std::string,std::string>>` from a
`nlohmann::json` object, whose default `ObjectType` is `std::map`
(`vendor/nlohmann/json.hpp:3399-3422`; `nlohmann::ordered_json` is never
used in this tree). `[E*]`

Two consequences: property application order inside one op is
alphabetical rather than authored, and **duplicate keys in one
`properties` object collapse silently at parse** with no warning pushed
to `result.warnings`. Same hazard for `play_cinematic` params
(`kg_ops_parse.cpp:175-177`). `[E†]`

Deterministic — but deterministically the wrong order, which is worse
for an author than an obvious nondeterminism.

### N6 — GOAP: two defects

**R12.** `WorldState` is `std::unordered_map<std::string,int>`
(`src/npc-ai/goap_system.h:132`) and the visited-set key is built by
iterating it (`goap_system.cpp:61-67`). Two logically identical states
reached by different action paths produce different hash strings, so the
dedup at `:106-111` fails to collapse them. `[E†]`

**R11.** `priority_queue` comparator is f-cost only
(`goap_system.cpp:48-51`); equal cost resolves by heap sift order —
repeatable for one insertion sequence, not a defined rule, not stable
under any edit to the action list. `[E†]`

Deterministic by contrast: the action list is `std::vector<Action>`
iterated in registration order (`goap_system.h:249`, `goap_system.cpp:114`).

### N7 — `capability.*` write-out order, hence `STATE_CHANGE` event order

`for (const auto& [name, value] : p.capabilities)` over
`std::unordered_map<std::string,float>`
(`src/capability/capability_store.cpp:116`; type at
`include/logosphere/capability/capability_profile.h:48`). KG end state is
identical (distinct keys); the **sequence** of `STATE_CHANGE` events a
subscriber observes is hash-ordered. `[E]`

### N8 — `unloadEntity` returns render indices in hash order

`std::unordered_set<RenderIndex> unique_indices` →
`result.render_indices.assign(...)` (`src/kg/kg_core.cpp:691`, `:711`).
Deletion uses swap-and-pop, so the resulting particle-array layout
differs with the iteration order. `[E†]`

### N9 — volume episode close events

`for (const auto& [k, medium] : open_episodes_)` over
`std::unordered_map<uint64_t, uint32_t>`
(`particle_interaction_system.cpp:191`; member at `.h:313`). Order of
`VolumeEvent{entered=false}` emissions in a multi-exit frame. `[E]`

### N10 — capability aggregation and modifier passes

`contributions` (`capability_profile.cpp:110`, iterated `:293`),
`disabled_caps` (`:142`, iterated `:328`), `cap_modifiers` (`:143`,
iterated `:331`). All three are unordered and all three are iterated.
Results are order-insensitive today: distinct keys, and the K3 veto is
re-checked per key rather than relying on loop sequencing. `[E]` Listed
because the moment anything in those loops gains a side effect — a log,
an emit, a write to a shared key — the nondeterminism becomes
observable.

### N11 — registry name collisions (all singletons)

`effects_[name] = ...` / `triggers_[name] = ...` /
`conditions_[name] = ...` are last-registration-wins, silently
(`effect_registry.cpp:66-68`, `trigger_registry.cpp:154-156`,
`contact_response.cpp:105`, `:185`). The maps are never iterated, so
this is not hash-order nondeterminism — it is **cross-translation-unit
static-initialisation order** deciding which implementation of a name
survives. `[E]`

### N12 — pose blend result ordering

`std::unordered_map<std::string, BlendEntry> entries` iterated at
`animation_types.h:501` to build `result.targets`. **No observable
effect today**: keys are unique per pose and `apply_fk_pose_targets` uses
setters, not accumulators (`humanoid_locomotion.cpp:4692-4736`). `[E]`
Latent: any additive apply makes the pose toolchain-dependent.

### N13 — float-sum order in the cluster mean

`for (size_t idx : particles_with_gluons)` over an `unordered_set`,
accumulating a mass-weighted velocity sum
(`physics_system_v4.cpp:4580-4590`). Last-bits rounding only — real
under INV-27's bit-identical requirement, negligible physically. `[E]`

### Checked and found deterministic (recorded so it is not re-litigated)

- Constraint solving: `std::vector<Constraint>`, permuted only under an
  explicit seed whose default (0) does not touch the array
  (`physics_system_v4.cpp:2630-2641`). The shuffle sits *before* warm
  starting on purpose, reason written at `:2604-2612`.
- Event dispatch: `std::vector<Slot>`, tail-append, forward iteration,
  order-preserving compaction (`signal.h:32`, `:37-43`, `:48`, `:64`).
- Subscription ids: monotonic, never recycled, so a stale id can never
  alias a live handler (`signal.h:33`). `[E†]`
- Gluon iteration: `std::deque<std::unique_ptr<GluonConstraintBase>>`
  (`include/logosphere/physics/physics_system.h:596`).
- `getRelated` / `getRelatedReverse`: outer `unordered_map` is
  key-looked-up; the iterated value is a `std::vector<RelationID>` in
  insertion order (`src/kg/kg_core.cpp:227-265`; types at
  `kg_core.h:167`, `:170`).
- `findByType`: reads `type_index`,
  `std::unordered_map<std::string, std::vector<EntityID>>`
  (`kg_core.h:164`) — per-type order is insertion order.
- KG transaction: ops applied strictly by index, no sort
  (`kg_ops_transaction.cpp:221`). `[E†]`
- Every `unordered` container under `src/rules/` — eleven of them — is
  membership-only or keyed-lookup-only; the two that *are* iterated
  (`rule_fork.cpp:112-116`, `rule_program_validator.cpp:208-211`) copy
  to a vector and `std::sort` first. `[E†]`
- `DiceService::streams_` is keyed lookup only; streams are
  independently seeded and there is no entropy fallback
  (`dice_service.h:88-94`, `:140`).
- Per-part rule scan: `rule.0` … `rule.7` in numeric order
  (`capability_profile.cpp:163-165`) over a `parts` vector.
- Journal delta merge: `std::map` with an explicit comparator
  (`journal_render.h:68`).
- Frame system order: a hardcoded call sequence in
  `src/core/engine.cpp:1170-1268`.
- `findByProperty` **is** hash-ordered (`kg_core.cpp:510-522`, over
  `std::unordered_map<EntityID, Entity>`, `kg_core.h:156`) but is
  currently defused by caller discipline: all three KG callers require
  exactly one match (`seed_loader.cpp:149`,
  `qualified_reference.cpp:311`, `ontology_validator.cpp:302`) and
  `scene_chunk_generator.cpp:169-174` sorts before intersecting. Defused
  by callers, not by the producer. `[E†]`

---

## PART 3 — Two mechanisms, one subject, no arbitration

### 3.1 "May this body move" — eight live formulas

The `inv_mass_momentum` comment block
(`src/core/physics_system_v4.cpp:498-530`) is the honest record of the
last cleanup: the predicate *"was re-derived inline at 8 division sites
with three different formulas"*. That cleanup unified the **momentum**
sites. It did not unify the others.

| # | Site | Formula | Sleeping body | Massless body |
|---|---|---|---|---|
| 1 | `inv_mass_momentum` `:532-537` | KINEMATIC → 0; at_rest → 0 (unless `WAKE_RESOLVER`); m<=0 → 0 | immovable | immovable |
| 2 | `inv_mass_positional` `:4037-4041` | KINEMATIC → 0; m<=0 → 0 | **movable** | immovable |
| 3 | `integrate_positions` `:4604` | KINEMATIC → skip | **movable** | **movable** |
| 4 | `enforce_turtle_boundary` `:4801` | `GetMass()==0` → skip | **movable** | immovable |
| 5 | `update_rest_state` `:4881` | `GetMass()==0` → skip | n/a | immovable |
| 6 | angular row build `:2134-2135` | KINEMATIC → no inv-inertia term; I<=0 → no term | **movable** | refuse loud if massive |
| 7 | `integrate_angular_velocities` `:5016`, `:5021`, `:5032-5041` | KINEMATIC → skip; at_rest → skip; massless+I<=0 → skip; massive+I<=0 → `abort()` | immovable | immovable |
| 8 | `project_angular_limits` `:5190`, `:5193` | KINEMATIC → skip | **movable** | **movable** (only `total_I > 1e-4`) |

Plus one **dead** formula with no guard at all:
`apply_angular_constraints` `:4940-4986` writes `torque_z` onto both
bodies with no KINEMATIC, sleep or mass test. Its header declares it
deprecated (`:4921-4939`) and a tree-wide call-site search found no
caller. `[E]`

**Difference 1 vs 2 is deliberate and documented** (`:509-513`,
`:4022-4036`): momentum and geometry are different questions. That is
arbitration, written down.

**Differences 3, 4, 5, 8 carry no rationale anywhere.** `[I]` — absence
of a comment is not proof of accident, but every deliberate split in
this file carries a paragraph explaining itself, and these do not.

### 3.2 The turtle boundary overrides KINEMATIC authority

`enforce_turtle_boundary` (`:4794-4851`) writes `p.z` and zeroes `p.vz`
for **any** body with mass, including KINEMATIC ones. `[E]`

`src/particle_types.h:80` defines KINEMATIC as *"An external writer OWNS
this body's position RIGHT NOW"*, and the generated ontology repeats it
(`src/generated/logosphere_ontology.h:694`). The turtle pass never
consults `solver_mode`.

What makes it well-defined today: the turtle is the one thing INV-1
allows to be immovable, and phase order puts the boundary last in the
substep (`:412-416`), so it is the final writer. `[I]` — that is a
justification, not an arbitration. No code states that the turtle
outranks a position owner, and the position owner is never told.

### 3.3 FK and the solver both write one bone's orientation

Recorded in-tree as an existing INV-22 debt, not discovered here:
`docs/todo_plans/MOTION_AUTHORITY_DESIGN.md:563` and `:813` — *"an INV-22
debt (FK and the solver both writing one bone's orientation)"* — patched
today by the gravity exemption at `physics_system_v4.cpp:656`:

```
if (p.is_quat_driven && p.owner != ParticleOwner::PHYSICS) continue;
```

The arbitration in force is A6: pipeline order, animation writes last
(`engine.cpp:1266-1268`). No claim, no refusal, no book.
`MOTION_AUTHORITY_DESIGN.md` proposes INV-32 to fix this; it is marked
*"candidate, aspirational"* (`INTERACTION_ROUTER_DESIGN.md:675-677`), and
a tree-wide search for `MotionAuthority` / `claim_motion` returns only
the design doc. `[E]`

### 3.4 One damage event writes two health pools, resistance applied twice

`apply_to_body_part` calls `apply(entity, damage, type)` — which runs
`apply_resistance` on the raw damage and subtracts from
`EntityHealth::hp` — and then independently runs `apply_resistance` on
the **same raw damage** again and subtracts from the KG part's `health`
property (`src/damage/damage_system.cpp:172`, `:193`, `:196`). `[E*]`

There is no armour / soak / pass-through relationship: the part does not
absorb from the entity pool and the entity pool does not shield the
part. Two subjects, one event, no shared budget, and no code states
which is authoritative.

### 3.5 `Signal::emit` — a journal inversion and two reentrancy hazards

```
void emit(const T& event) {
    emitting_ = true;
    for (auto& slot : slots_) { if (slot.fn) slot.fn(event); }
    emitting_ = false;
    compact();
}
```
`include/logosphere/events/signal.h:62-69`.

**(a) Journal ordering inversion.** `EventChannel::emit` fires the signal
**before** journaling (`event_channel.h:29-32`). If a subscriber emits on
the same channel, the inner (effect) event is appended with a **lower
seq** than the outer (cause) event. Every tier-2 consumer —
`render_state_deltas`, `RunRecorder`, replay, the LLM director — reads
the effect before the cause. `[E]` from the code; **not reproduced**.

**(b) subscribe-during-emit is unguarded.** `subscribe` does
`slots_.push_back` (`:48`) and never consults `emitting_`; a
reallocation invalidates the range-for's iterators. `[E]`

**(c) `emitting_` is a bool, not a depth counter.** A nested `emit` sets
it `false` on exit and calls `compact()` — `erase`ing from `slots_` while
the outer loop iterates. After the nested emit returns, the outer
dispatch continues with `emitting_ == false`, so any later `unsubscribe`
in that same dispatch compacts immediately. `[E]`

**This path is live, not theoretical.** `capability_store.cpp:15`
subscribes to `state_changes()`; the handler reaches `write_back`, which
calls `kg_->setProperty` (`:104-117`), which emits `state_changes()`
(`src/kg/kg_module.cpp:193`). The `writing_` bool suppresses **the
store's own handler only** — the nested emit still runs the full
dispatch to the other subscribers. `[E†]`

`tests/test_event_bus.cpp` has nine tests; none covers nested emit,
subscribe-during-emit, or unsubscribe-during-emit. No mutex exists in
any of the four event headers. `[E†]`

### 3.6 The impulse inbox has no consumer in the engine

`deposit_impulse` accumulates
(`particle_interaction_system.cpp:265-272`). `take_impulse` (`:274-283`)
has **no caller anywhere in `src/`** — only
`tests/test_knockback_scene.cpp:223`, `tests/test_contact_bounce.cpp:92`,
`:389-407`. `[E]`

The header states this as intended: *"Nothing drains the inbox by
default. An unclaimed impulse is a game that has not wired its mover yet,
not an engine failure."* (`contact_response.cpp:155-156`). So the
engine's one shipped physical contact effect terminates in a mailbox
that nothing reads and nothing bounds. The drain is also destructive and
single-consumer (C6): two systems both draining means the first takes
everything.

### 3.7 Two friction rules, one live and one dead

`min(pa.friction, pb.friction)` is live
(`physics_system_v4.cpp:3510-3514`). A material-pair `friction_table_`
with a symmetric key, a symmetric hash and a `0.8f` fallback exists,
is never populated, and its `get_friction` has zero callers
(`include/logosphere/dynamics/particle_dynamics_system.h:664-676`;
`src/core/particle_dynamics_system.cpp:283-316`, comment at `:303`: *"not
yet implemented"*). Nothing reconciles the two and nothing documents
which is intended to win. `[E*]` for the live rule, `[E†]` for the dead
table.

### 3.8 Two damping numbers per material, and two combination rules

`materials.h:164-199` splits the file explicitly: everything above
describes *how a material LOOKS to the solver* (`GetDamping`, tuned —
*"STONE 0.40 — increased for stacked settling"*), everything below
describes *what it IS* (`GetLossFactor`, measured). `GetCombinedDamping`
averages the **tuned** number; nothing consumes `GetLossFactor` in the
damping path. `[E]`

And the combination rule differs by code path: force-bounded gluons let
the bond's declared damping **override** the material pair
(`physics_system_v4.cpp:1798-1811`), while contacts between the same two
bodies use the raw material average (`:1410`). Same physical quantity,
two rules, selected by which row type you are in. `[E*]`

### 3.9 Two overlapping media both apply their forces

`apply_volume_forces` calls `apply_medium` for each side of every deduped
overlap pair (`particle_interaction_system.cpp:695-696`). A body inside
two media receives both drags, both buoyancies and both field forces,
summed by velocity increment. There is no "which medium am I in"
question and no arbitration. `[E]`

---

## PART 4 — What already exists that a resolution engine could be built FROM

Judged on what each does today, not on what it could be extended to do.

### 4.1 `src/rules/` — ambiguity designed out rather than resolved

**The strongest doctrine in the tree.** Eleven distinct would-be ties are
**load-time errors**, each naming the offending entities: `[E†]`

| Would-be tie | Rejected at | Message |
|---|---|---|
| two lookup rows covering one key | `lookup_table_selector.cpp:137-145` | "LookupTable row bands overlap" |
| two table rows covering one total | `rollable_table_runner.cpp:99-105` | "RollableTable row bands overlap at N" |
| two steps with one index | `outcome_executor.cpp:654-659` | "step_index values must be contiguous from 0" |
| two procedure steps, one index | `procedure_runner.cpp:119-123` | same shape |
| two routes with one label | `procedure_runner.cpp:161-165` | "ProcedureStep has duplicate route_label" |
| two handlers for one type | `outcome_executor.cpp:974-977` | "handler for 'X' is already registered" |
| two SkillRatings for one skill | `outcome_executor.cpp:207-211` | "target has duplicate SkillRating entities" |
| two params with one key | `rule_program_validator.cpp:149-156` | "duplicate parameter key" |
| two bindings for one param | `rule_program_validator.cpp:545-549` | "duplicate argument binding" |
| an expression in two families | `rule_static_type.cpp:341-346` | "inherits multiple result families" |
| a rule with fixed **and** rolled delta | `outcome_executor.cpp:475-479` | *"a rule carrying both is ambiguous rather than generous"* |

Plus a **totality proof**: `RollableTableRunner` verifies every reachable
dice total has exactly one row **before rolling**
(`rollable_table_runner.cpp:117-134`). Match and no-match are both
structurally impossible at select time.

Directly reusable:
- **The band-selector shape**: sort into a `std::vector`, reject overlap
  at load, then first-match is provably only-match. This is exactly the
  discipline `INTERACTION_ROUTER_DESIGN.md:1041-1045` (G5) prescribes.
- **The tie-break comparator idiom** (`rule_program_validator.cpp:295-301`):
  a total order — key, then entity id — that cannot itself tie. Spec at
  `docs/RULE_LANGUAGE.md:706-708`.
- **The three-tier answer precedence** (`outcome_executor.cpp:731-774`):
  explicit answer > installed resolver > suspend and ask, with
  **deliberately no default**.
- **`PlannedWorld`** (`src/rules/planned_world.{h,cpp}`) as a read
  surface over a candidate effect batch — subject to §4.5's constraints.
- **`apply_kg_ops_atomically` + `DiceService::begin_transaction`** as a
  commit boundary (`outcome_executor.cpp:1001-1023`).

### 4.2 The two registry pairs

`capability::TriggerRegistry` + `EffectRegistry`
(`src/capability/trigger_registry.cpp`, `effect_registry.cpp`) and
`interaction::ContactConditionRegistry` + `ContactEffectRegistry`
(`src/interaction/contact_response.cpp`).

Working today:
- One `"<name>:<args>"` grammar, split on the **first** colon so args may
  contain colons (`contact_response.cpp:17-26`,
  `trigger_registry.cpp:166-168`, `effect_registry.cpp:76-78`).
- Open extension: a game registers its own name and it is as legal as a
  built-in. Tested (`tests/test_contact_bounce.cpp:418+`).
- Fail-closed on unknown names, both sides.
- Load-time effect validation for contact rules: an unregistered effect
  name refuses the whole rule at load, with a message
  (`particle_interaction_system.cpp:436-444`).
- A working compound predicate with AND / OR, parenthesis balancing,
  recursive re-dispatch, and explicit refusal of mixed precedence
  (`trigger_registry.cpp:87-151`).

Not provided: which rule wins, priority, ordering, variable binding,
conflict detection. `evaluate` answers yes/no about one expression;
nothing composes two of them.

`contact_response.h:12-18` explains, in-tree, why there are two registry
pairs rather than one.

### 4.3 `TransformationRule` load + validation

`ParticleInteractionSystem::load_rules_from_kg`
(`particle_interaction_system.cpp:378-495`): KG-driven loading; trigger
vocabulary parsed through the **generated** `onto::from_string` so schema
and loader cannot drift (`:403-410`); case normalisation at the single
parse point (`:21-26`); per-slot optional tuning with `kg_parse`
diagnostics; skip-with-reason on any malformed rule.

Missing for resolution: no priority, no specificity, no rule identity
beyond the KG entity id, and the storage container is N1.

### 4.4 The bonded-pair ownership mechanism (P3, P4, P5)

The tree's only **working** "exactly one mechanism owns this subject"
implementation, at three strengths:

- **Skip:** direct gluon pair → no contact row (`:1129-1133`).
- **Skip, transitive:** union-find over the gluon graph through DYNAMIC
  bodies only, rebuilt every step so a torn bond dissolves the component
  with no stale state (`:810-852`, test at `:1140`).
- **Refuse:** a second bond for one pair `abort()`s, naming both offsets
  (`:5487-5506`), with an escape hatch (`GLUON_LENIENT`).

### 4.5 `PlannedWorld` as a resolution sandbox — and its four constraints

`include/logosphere/rules/planned_world.h`, `src/rules/planned_world.cpp`.
A read view projecting an in-flight `OutcomePlan` over the committed
graph, committed graph untouched, bus detached until the batch commits.
Five deterministic accessors; `related()` returns committed targets then
pending ones **in plan order, contractually** (`planned_world.h:82-84`);
`was_written()` is deliberately not derivable from `property()` because
writing the same value back is invisible to a value read (`:89-94`).
`[E†]`

Constraints to design around:
1. **Additive only.** `planned_world.h:27-33`: *"a plan is ADDITIVE …
   there are no tombstones, and absence is never ambiguous. If destroy
   ever becomes plannable, every method below needs a deletion case."*
   "The route removed the grip" is not expressible today.
2. **O(n) per read** — every accessor linearly scans `plan_.ops`.
3. **Non-owning, non-storable** (`:41-46`), rebuilt per handler call
   (`outcome_executor.cpp:785`).
4. **One plan, not N alternatives.** Nothing forbids building two views
   over two plans; nothing supports comparing them either. The header
   (`:20-25`) is explicit that this is deliberately *not* an overlay
   installed under `KGModule` — citing PhysX 5 removing exactly that
   design.

### 4.6 The sleep law as a veto aggregator (P10)

`constraint_dissatisfied_`, a `std::vector<uint8_t>` any row may set to 1
(`:1918-1921`, `:2376-2378`), consumed as an absolute veto on rest
(`:4885-4896`), with an env lever. Commutative, order-independent, cheap,
per-subject. A working many-writers / one-verdict aggregator.

### 4.7 The split-phase pattern (P7)

Velocity solve runs bias-free; position repair runs the same rows again
into a pseudo-velocity spent on positions and discarded (`:4000-4189`).
The engine's existing answer to "two things want the same field for
different reasons": separate accumulators, one commit point.

### 4.8 The journal + delta merge (E7)

`render_state_deltas` (`journal_render.h:60-96`) collapses a stream of
`STATE_CHANGE` events into a net delta per (entity, property) with
latest-wins, first-prev-kept and net-no-op suppression, ordered by a
`std::map` with an explicit comparator. A deterministic event-fold over
an ordered container, working today.

### 4.9 The pose layering system (A2, A3, A5)

Keyed matching (joint + channel), weighted blend, region masking,
crossfade, and a separate additive channel that survives a full clear.
Animation-specific, but the most complete worked example in the tree of
"several sources write one subject and the result is defined".

### 4.10 The KG transaction (G1, G5, G6, G7)

Authored order preserved, LIFO undo journal, events buffered and flushed
only on success, report wiped on failure, and **two in-batch conflicts
already detected as hard failures** (duplicate alias binder, duplicate
relation). The commit boundary a resolution layer would want, with three
holes named in §5. `[E†]`

### 4.11 The seeded-randomness discipline

`DiceService` — *"the only place randomness becomes fact"*, named streams
independently seeded, journaled, citable by monotonic id, rule-attributed,
with a documented rule that an unseeded stream gets seed 0
deterministically and **no entropy fallback anywhere in the class**
(`include/logosphere/core/dice_service.h:88-99`), plus its own bus channel
(`event_bus.h:35`) and transactional checkpointing so an abandoned plan
consumes no randomness. If resolution ever needs a tiebreak that is not
an authoring error, this is the declared seeded channel INV-27 requires.

### 4.12 What was NOT found

- **No condition/predicate language in `src/rules/`.** The typed
  expression ontology exists (`schema/packs/rule_language.yaml`) with
  the abstract expression families, reads, lets and type descriptors —
  and **zero operator classes**: no `And`, no `Not`, no `Equals`. The
  concrete-family table at `src/rules/rule_static_type.cpp:311-324`
  lists eleven families and no operators. There is **no evaluator**.
  `docs/todo_plans/LOGOVGER_RULE_CONTEXT_HANDOVER.md:287-300`: *"Runtime
  drafts remain non-executable."* `[E†]`
- **No unification, no backtracking, no resolution.** Bindings are
  direct entity pointers, never name resolution
  (`docs/RULE_LANGUAGE.md:700-701`). Every traversal is single-pass
  forward with a cycle stack. `[E†]`
- **No negation, no quantifiers.** `docs/RULE_LANGUAGE.md:1069-1070`
  lists negation as undecided. `[E†]`
- **No forward chaining, no fixpoint.** The only iterative loop is
  Kahn's algorithm over a DAG, which terminates in one pass. `[E†]`
- **No priority, weight or precedence field on any rule type in any
  schema** reachable from the interaction, capability or rules paths.
  `TransformationRule` (`schema/logosphere.yaml:975-994`) has `trigger`,
  `condition`, `effect`, `target_profile`, `duration_s`,
  `trigger_profile` — nothing else. `[E†]`
- **No conflict detection between rules.** `rule_program_validator.cpp`
  validates well-formedness — cycles, scope, duplicate keys, static
  types — and never subsumption, mutual exclusivity, precondition
  overlap or unsatisfiability. `docs/RULE_LANGUAGE.md:1071-1072` lists
  *"modifier stacking, precedence, suppression"* as still undecided.
  `[E†]`
- **`GEDANKEN.jsonl` appeared mid-survey.** At the start of this pass
  `tests/invariants/` held only `INVARIANTS.jsonl`, `LEDGER.md` and
  `TEST_AUDIT.jsonl`, so the registry `LEDGER.md:836-838` names was
  absent. It exists now (10 entries, `GEDANKEN-1` onward), written by
  the parallel Gedankenexperiment study while this document was being
  drafted. Recorded because the two studies are concurrent and this
  inventory does not read that file. `[E*]`

### 4.13 A doctrine collision to surface, not to resolve here

`docs/RULE_LANGUAGE.md:945-947` is a standing decision:

> "This boundary avoids an implicit production-rule loop. **Automatic
> firing, retraction, conflict sets, and mutation ordering are not part
> of the selected language model.**"

and `:940-943`: *"A matched predicate does not fire hidden mutations
merely because it exists in the graph."* `[E†]`

Any accumulate-all effect resolution is, structurally, a conflict set.
The two doctrines are reconcilable only on the grounds that a router is
*pushed* by physics occurrences rather than *pulled* by a working-memory
scan. That reconciliation is not written down anywhere today, and it is
a design question, not an inventory finding.

---

## PART 5 — Contradictions the tree already contains

1. **`SWAP_PROFILE`'s winner is a hash.** Two matching rules, one
   particle, last-write-wins by `unordered_map` order.
   `particle_interaction_system.cpp:554` + `:524-526`.

2. **Warm-start dedup on hashes, not keys.** Documented in-tree as a
   defect that silently drops constraints, still live.
   `physics_system_v4.cpp:2660-2665`, `:2789`, `:3947-3949`.

3. **`enforce_turtle_boundary` moves KINEMATIC bodies** whose position
   the ontology says an external writer owns.
   `physics_system_v4.cpp:4794-4851` vs `src/particle_types.h:80`.

4. **Eight live movability formulas** where the file's own comment
   celebrates reducing three to one, on the momentum axis only.
   `physics_system_v4.cpp:498-530` vs §3.1.

5. **A dead angular function with no guards at all**, still compiled.
   `physics_system_v4.cpp:4940-4986`, header `:4921-4939`. It is also a
   literal spring-damper (`:4980-4981`) in a codebase whose INV-5
   forbids spring-damper models as patches — legal only because it is
   never called.

6. **`Bite` and `Pierce` alias one float.** Setting one silently
   clobbers the other, and the getter aliases too so the clobber is
   invisible through the API. The header calls Bite *"pierce + blunt"*;
   blunt is never consulted. `damage_system.cpp:120-133`, `:143-144`;
   `damage_system.h:39`.

7. **`Cold` and `Poison` are structurally unresistable.** The enum and
   the schema declare them; `EntityHealth` has no field for them and
   both fall into `default: break;`. `damage_system.cpp:150-162` vs
   `damage_system.h:44-46`.

8. **The tissue table names damage types that do not exist.**
   `HasTissue` routes `CUT` and `CRUSH`; the `DamageType` enum is
   `BLUNT, SLASH, PIERCE, BITE, FIRE, COLD, POISON, PURE`, and
   `from_string` rejects both. Nothing decrements a tissue value and
   nothing reads one. `schema/logosphere.yaml:505-512` vs `:104-122`.

9. **One damage event, two health pools, resistance charged twice**, with
   no absorption relationship between them.
   `damage_system.cpp:172`, `:193`, `:196`.

10. **Two friction combination rules**, one live (`min` of two scalars)
    and one dead (a material-pair table with zero callers). Nothing
    reconciles them. `physics_system_v4.cpp:3514` vs
    `particle_dynamics_system.cpp:283-316`.

11. **Two damping numbers per material and two combination rules.** The
    tuned `GetDamping` is used, the measured `GetLossFactor` is not; and
    a bond may override the material pair while a contact between the
    same bodies may not. `materials.h:164-199`;
    `physics_system_v4.cpp:1798-1811` vs `:1410`.

12. **Two sibling registries, two behaviours on the same failure.**
    Unknown contact condition prints to stderr
    (`contact_response.cpp:125-128`); unknown capability trigger returns
    false in silence (`trigger_registry.cpp:171`). Same for the effect
    registries: `contact_response.cpp:199` prints,
    `effect_registry.cpp:80-81` does not.

13. **`create_entity` applies properties alphabetically, not as
    authored, and silently collapses duplicate keys.**
    `kg_ops_parse.cpp:88-90` + `vendor/nlohmann/json.hpp:3399-3422`.

14. **A transaction detects duplicate relations and duplicate aliases,
    but not duplicate property writes.** Two `set_property` ops on one
    entity+key both apply, silently. `kg_ops_transaction.cpp:235-239`,
    `:264-269` vs `kg_core.cpp:326`. `[E†]`

15. **`apply_kg_ops_atomically` is not exception-safe.** It nulls the
    event bus at `kg_ops_transaction.cpp:192` and restores it only at
    `:213` / `:304`; `KGCore::setProperty` and `removeProperty` throw
    (`kg_core.cpp:311-313`, `:354-356`), and `removeProperty` is on the
    rollback path. An escaping throw leaves the module with a
    permanently null bus and no undo. `[E†]`

16. **Inherited property precedence is decided by an alphabetical sort
    of ancestor names**, not by inheritance depth.
    `ontology_registry.h:669-696`.

17. **Contact episodes have no close event; volume episodes do.**
    `open_contacts_ = std::move(current)` discards the previous set with
    no diff (`particle_interaction_system.cpp:374`) while the volume path
    diffs and emits `entered=false` (`:190-199`). Already noted in
    `INTERACTION_ROUTER_DESIGN.md:581-587`.

18. **The engine's only physical contact effect has no consumer.** §3.6.

19. **`kg_query` sorts its results; the effect layer does not.**
    `kg_query.cpp:49`, `:65-67` vs `effect_registry.cpp:57-61`. The same
    codebase knows about hash-order nondeterminism in one file and not
    in the other. `count_query` (`kg_query.cpp:77-83`) also does not sort
    while `run_query` does.

20. **`Signal::emit` is unsafe for subscribe-during-emit and for nested
    emit**, and journals the effect before the cause, while the class
    comment advertises deferred-unsubscribe safety.
    `signal.h:9-10` vs `:62-69`; `event_channel.h:29-32`.

21. **INV-22 is enforced for gluon pairs and hoped for everywhere
    else.** Hard refusal at `physics_system_v4.cpp:5494-5504`; nothing
    equivalent for contact rules, capability effects, damage pools, or
    motion writers.

22. **A rule-numbering gap silently truncates the rest.** `rule.0` and
    `rule.2` with no `rule.1` means `rule.2` never runs; same for effect
    slots. `capability_profile.cpp:166`, `:181`, `:226`.

23. _(withdrawn during this pass.)_ `GEDANKEN.jsonl` was absent when the
    survey began and was created by the concurrent Gedankenexperiment
    study before it ended. Not a contradiction. See §4.12.

---

## PART 6 — Tests that encode a conflict

- `tests/test_capability_system.cpp:421-445` —
  `test_cascade_multiple_clauses_min_wins`: two cascade clauses hitting
  one part, asserts min (0.2) wins, not 0.7, not their product. `[E†]`
- `tests/test_capability_system.cpp:576-591` —
  `test_cap_disable_beats_modifier`: `cap_disable:locomotion` vs
  `cap_modifier:locomotion:2.0` on one part, asserts 0.0,
  *"disable wins over modifier"*. `[E†]`
- `tests/test_capability_system.cpp:680-707` —
  `test_multiple_effects_all_dispatched`: four heterogeneous effects on
  one rule, all asserted to land. `[E†]`
- `tests/test_contact_bounce.cpp:398-411` —
  `test_impulses_accumulate_and_can_cancel`: two opposite deposits sum to
  one entry and cancel to zero. `[E]`
- `tests/test_contact_bounce.cpp:375-396` — a taken impulse cannot be
  taken twice ("a push cannot be applied twice"). `[E]`
- `tests/test_damage_events.cpp:126-135` — kill, then damage again;
  asserts the post-death damage emits **nothing**. Encodes D1's
  first-kill-wins. `[E†]`
- `tests/test_damage_events.cpp:112-125` — three sequential damages of
  three types on one entity, three separate events. `[E†]`
- `tests/test_constraint_order_matters.cpp` — the whole file is the
  question "is constraint order load-bearing", with an A-vs-A control
  built first because a chaotic pile would otherwise "prove" order
  matters no matter what the answer is (`:30-42`). Its header records
  that the earlier study (S19) reached the wrong conclusion twice. `[E]`
- `tests/test_determinism_guards.cpp` — three ways the engine used to
  give different answers to the same question, each now pinned: a reset
  that did not reset, a counter shared between supposedly independent
  engines, and a generator seeded from the clock. `[E]`
- `tests/test_position_authority.cpp:1-29` — the
  one-integrator-per-particle contract, written after a relic loop was
  found integrating every particle a second time. `[E]`
- `tests/test_immovable_pair_phantom_impulse.cpp` (referenced at
  `physics_system_v4.cpp:1406`) — a row no impulse can change must
  contribute zero, or it holds the global convergence test hostage.
- `tests/test_inv15_owner_blindness.cpp` — the physics/game-category
  boundary, with a `KNOWN_OWNER_READS` allowance list.
- `tests/test_material_properties.cpp:130-176` — cross-material
  **ordering** invariants only; **never exercises a two-material
  combination**. `[E†]`

**Not tested anywhere:** `GetCombinedDamping`, the force-bounded damping
override, `MaterialPair`/`friction_table_`, `min(pa.friction,
pb.friction)`, `set_resistance` / Bite-Pierce aliasing, the double
resistance in `apply_to_body_part`, own-rule-vetoes-cascade, any ordering
property of C1/C2/C3, and every reentrancy path in §3.5. `[E†]`

---

## PART 7 — Established vs inferred

**Established and personally verified in this pass (`[E]`, `[E*]`):**
every entry in the Part 1 interaction, capability, physics, animation and
event rows; every declared container type quoted in Part 2 for those
areas; every formula in the §3.1 table; the `SWAP_PROFILE` last-write
path; the `entities[0]` primary-entity pick and its `kg_to_render_`
type; the nlohmann default `ObjectType`; the `effective_properties`
ancestor sort; the `Bite`/`Pierce` alias and the `Cold`/`Poison` gap; the
`LEDGER.md` quotation; the absence of
a `take_impulse` caller in `src/`; the absence of a `MotionAuthority`
implementation; the absence of any caller for
`apply_angular_constraints`.

**Established by the parallel sweeps, cited but not independently
re-read (`[E†]`):** the `src/rules/` load-time rejection table and the
absence of operators/evaluator in the rule language; the GOAP defects;
the KG transaction rollback holes and the `set_event_bus` exception
hazard; the `state_changes()` subscriber registration order; the
`unloadEntity` hash order; the tissue writer/reader survey; the
`friction_table_` dead-path claim; the capability test citations. These
are direct code reads with quoted line numbers and should be treated as
strong, but a second pair of eyes has not been on them.

**Inferred, with the inference named (`[I]`):**

- N1's "adding one unrelated rule can reorder the others" — from the
  `unordered_map` specification, not measured.
- N4's cross-toolchain divergence — `std::hash<std::string>` differs
  between standard libraries; not measured here.
- §3.1's "differences 3, 4, 5, 8 carry no rationale" — from the absence
  of a comment in a file where every deliberate split carries one.
- §3.2's "phase order is a justification, not an arbitration" — no code
  states the turtle outranks a position owner.
- §3.5's hazards — read from the code, **not reproduced**.

**Not measured, and therefore not claimed:** how often N3's hash
collision actually fires; whether N1's iteration order has ever changed
between two builds of this tree; whether any live scene currently loads
two `SWAP_PROFILE` rules that can match one episode; whether the §3.5
reentrancy paths have ever produced observable corruption.
