# The interaction router

Date: 2026-08-15. Status: design only, no code. Read-only excavation of
`/Users/luis/Projects/logomancers` plus a survey of this tree at `373c132`.

Every claim about either repo carries `file:line`. Claims are marked
**[E]** established (read from code or a doc in the tree) or **[I]**
inferred. No time estimates anywhere.

---

## 0. What is being designed, in the owner's words

> "This is about interactions between volitional kinematic and
> non[-volitional] [...]. My idea was always to have a ROUTER between
> these, like a networking router actually, where we can load /
> pre-load / dynamically load ROUTING between interactions and
> entities, even HIERARCHICALLY, humanoid->torso vs
> humanoid->arm->hand->finger, and any other entity, and being able to
> CATCH the event/interaction between particles, and allow the routing
> to be MODIFIED BY ANY OTHER SYSTEM, like a combat system: i.e. if I
> want a combat system that subscribes when weapons are used, I want to
> be able to influence what my silver-bullet does against a werewolf vs
> a hippo."

> "This is where ontology-powered semantics for these interactions and
> COMPOSABLE interactions come into play, which would allow rules in
> the KG to control these interactions with minimal code implementation
> for them. Once those building blocks exist: 'hand is burning, entity
> retracts in panic' -> engine understands that animation, hand flies
> and hits a cabinet, hand bounces [...] a chain of semantically rich,
> physics-aware, physics-controlled, animation-capable sequences that
> look and feel realistic, CREATED FROM SEMANTICS."

The router decides **meaning**. Physics decides **motion**. That split
is the whole design and everything below is a consequence of it.

---

# PART 1 — What logomancers actually built

## 1.1 The dispatch shape: a router with nothing routable in it

Logomancers' combat system calls itself a router in three separate
places, and means it:

- `src/combat_system.h:195` "Combat is JUST a router. Entity owns
  damage calculation and HP." **[E]**
- `src/combat_system.cpp:665-677` header block over
  `process_damage_event`, titled "Pure routing to entity damage
  controller". **[E]**
- `design/GAME_DESIGN_COMBAT_ENGINE.md:1104` "Combat system is a
  **router**, not a calculator". **[E]**

The actual dispatch is a **two-branch if over two member variables**:

```cpp
// src/combat_system.cpp:690-698
CombatantState* defender_state = nullptr;
if (event.defender == combatant_entity_) {
    defender_state = &player_state_;
} else if (event.defender == opponent_state_.entity) {
    defender_state = &opponent_state_;
}
```

There is no table, no registry, no KG rule. The routing target is one
of two structs, `player_state_` and `opponent_state_`
(`src/combat_system.h:624-625`). **[E]** The N-combatant design in
`design/GAME_DESIGN_COMBAT_ENGINE.md:451` ("N-Combatant Agnostic: No
hardcoded player/enemy") never landed, and the code says so:

```cpp
// src/combat_system.cpp:430-433
// For N-combatant support, this would need a map of entity->state
// For MVP: first threat becomes the opponent
```

Once inside the entity, dispatch is a **string compare on damage type**
into two hand-written functions:

```cpp
// src/combat_system.cpp:121-128
if (event.damage_type == "blunt")      base_damage = resolve_blunt_damage(...);
else if (event.damage_type == "sharp") base_damage = resolve_sharp_damage(...);
else                                   base_damage = event.impact_velocity_ms * 10.0f;
```

and each of those is an if-chain over a material name string
(`src/combat_system.cpp:52-58` for blunt toughness, `:76-81` for cut
resistance). **[E]** Five materials, hardcoded, in C++.

**Verdict:** logomancers has the *shape* of a router (a system that
receives an occurrence, resolves participants, and delegates the
outcome to the participant) and none of the *mechanism* (no table, no
data, no extension point that does not require a recompile).

## 1.2 What is already router-like, and worth taking

Six things. All six deserve to survive into logosphere.

**(a) The same physical contact routes differently depending on
registered context.** This is the single best idea in the file.

```cpp
// src/combat_system.cpp:850-853
// Check if this is an intentional attack
// During an active attack, ANY body part of the attacker hitting the defender counts
// (the punch animation may cause torso/shoulder to hit before the hand reaches)
bool is_intentional = attack_active_ && attacker == attack_entity_;
```

`attack_active_` is opened by `register_attack` (`:486-514`) and closed
by `end_attack` (`:516-656`). **[E]** Inside that window, contacts
aggregate; outside it, they resolve immediately as incidental
(`:869-885`). One contact, two meanings, decided by a window a third
party opened. That is routing.

**(b) N physics contacts collapse to one semantic event.**
`PendingAttackHit` (`src/combat_system.h:144-185`) accumulates every
`ParticleImpact` against one defender and exposes `max_velocity()`,
`total_force()`, `average_direction()`. **[E]** Rationale at
`design/GAME_DESIGN_COMBAT_ENGINE.md:1220-1223`: "One punch = one game
event (even if 8 particles touch) [...] Prevents spam (8 knockbacks
would be chaos)."

**(c) A pluggable outcome authority.**

```cpp
// src/combat_system.h:520-523
// Connect to engine-level DamageSystem for unified HP tracking.
// When set, process_damage_event() forwards final damage to DamageSystem
// after body-part resolution. DamageSystem becomes the death authority.
void set_damage_system(DamageSystem* ds) { damage_system_ = ds; }
```

Applied at `src/combat_system.cpp:766-779`: if a `DamageSystem` is
attached and knows the entity, it overwrites `hp_remaining`, `max_hp`
and `is_dead` in the result. **[E]** A second system takes over the
outcome, by injection, at runtime. That is the closest thing in the
file to "a third party modifies routing".

**(d) Feedback into the body graph, closing a loop through the KG.**

```cpp
// src/combat_system.cpp:754-761
if (forge_ && result.damage_dealt > 0.0f && !result.body_part_name.empty()) {
    forge_->apply_tissue_damage(event.defender, result.body_part_name,
                                event.damage_type, result.damage_dealt);
}
```

`apply_tissue_damage` walks `HAS_PART`, finds the part by
`body_part_name`, and degrades `tendon_health` / `muscle_health` /
`nerve_health` per damage type (`src/forge_system.cpp:173-218`). **[E]**
The next frame's `sync_stats` (`src/combat_system.cpp:204-216`) reads
the degraded graph, and the attack gets slower. Verified end to end by
`tests/test_combat_forge_loop.cpp:198-205` ("Wounded hunter attacks
slower") and `tests/test_combat_engine_integration.cpp:325-332`. **[E]**

**(e) Notification hooks.** Seven `std::function` callbacks on the
system (`src/combat_system.h:595-608`): `on_stance_changed`,
`on_combat_mode_changed`, `on_collision_hit`, `on_entity_died`,
`on_log`, `on_grapple`, `on_release`. **[E]** Used by the game to
ragdoll and to freeze a grappled victim
(`tests/test_shambler_combat_death.cpp:147-177`). **[E]**

**Limit worth naming:** these are *notification only*. Every one
returns `void`. No hook can change an outcome, veto it, or reorder
anything. A third party can watch; it cannot route. **[E]**

**(f) A physics-first stance, stated and argued.**
`design/GAME_DESIGN_COMBAT_ENGINE.md:548-574` rejects hitbox polling
outright: "Game logic decides what's a 'hit'" is listed as the problem,
"Physics detects collision [...] Combat system receives, looks up
entities" as the fix, with four named benefits. **[E]** logosphere's
`process_contacts` is the same stance, better executed.

## 1.3 What is hierarchical in it

Two levels, and they do not connect.

**KG side (real, hierarchical).** `getParticleBodyPart(entity,
particle)` returns a body-part name string
(`src/combat_system.cpp:109`), and `apply_tissue_damage` addresses
parts through `HAS_PART` (`src/forge_system.cpp:182-189`). **[E]** The
graph carries per-part tissue channels.

**C++ side (flat, four buckets).** The returned name is immediately
collapsed into a four-value enum:

```cpp
// src/combat_system.cpp:139-152
BodyPart hit_part = BodyPart::Torso;
if (body_part_name == "head") hit_part = BodyPart::Head;
else if (body_part_name == "torso" || ... ) hit_part = BodyPart::Torso;
else if (body_part_name == "left_arm" || "right_arm" ||
         "left_hand" || "right_hand" || "arms") hit_part = BodyPart::Arms;
else if (... legs ...) hit_part = BodyPart::Legs;
```

`left_hand` and `right_arm` become the same bucket, `Arms`, and HP is
tracked in four floats (`BodyPartHealth`, `src/combat_system.h:311-361`).
**[E]** So `humanoid->arm->hand->finger` is expressible in the graph and
unrepresentable in the outcome. Nothing in the file addresses a
sub-part.

Note the two paths also disagree: `apply_tissue_damage` degrades the
*named* part in the KG while `BodyPartHealth` degrades the *bucket* in
C++, and only the KG path feeds anything back. **[E]**

## 1.4 KG-driven versus hardcoded: where the boundary sat

KG-driven **[E]**:

| Thing | Site |
|---|---|
| particle to body-part name | `combat_system.cpp:109` |
| body graph to FORGE stats (force, reflexes, grit, vigor) | `combat_system.cpp:178-186`, `:204-216` |
| tissue degradation targets | `forge_system.cpp:182-215` |
| entity names, `in_combat`, `stance`, `dead` flags | `combat_system.cpp:384-411`, `:658-663`, `:592` |

Hardcoded in C++ **[E]**:

| Thing | Site |
|---|---|
| material toughness / cut resistance tables | `combat_system.cpp:52-58`, `:76-81` |
| tissue routing by damage type | `forge_system.cpp:201-215` |
| stance modifiers | `combat_system.h:80-94` |
| attack timing coefficients | `combat_system.h:458-511` |
| body-part damage multipliers | `combat_system.h:221-229` |
| knockback magnitude | `combat_system.cpp:618-620` |

The boundary is stark: **the KG describes the body; C++ decides every
consequence.** Nothing in the KG can change what a hit means.

**The sharpest finding in the excavation.** The ontology already
carried the weapon vocabulary. `schema/logomancers.yaml:162-170`:

```yaml
  Weapon:
    is_a: Item
    mixins:
      - HasMaterial
    slots:
      - damage_type            # range: DamageType (schema:379-381)
      - weapon_force_modifier  # (schema:382-384)
      - weapon_reach           # (schema:385-387)
```

**[E]** And the code never reads any of it. Both `DamageEvent`
construction sites hardcode the answer:

```cpp
// src/combat_system.cpp:553-559 (aggregated attack path)
.weapon_type = "blunt",
.weapon_property = 0.5f,
.material_hit = "flesh",

// src/combat_system.cpp:898-902 (incidental path)
.weapon_type = "blunt",       // TODO: query from KG
.weapon_property = 0.5f,      // TODO: query hardness from KG
.material_hit = "flesh",      // TODO: query from KG
```

**[E]** The data-driven surface existed and was bypassed. This is the
exact failure mode the router must be built to prevent: an ontology
that describes the world while the code answers from constants.

## 1.5 What it got wrong, or found painful

Eight items, each with the citation. The failures are the reason this
document exists.

**W1. Ordering bug at the animation/physics seam, admitted and
unfixed.**

```cpp
// src/combat_system.cpp:950-954
// TODO[COMBAT-001]: Replace physics-based hit detection with Hitbox/Hurtbox system.
// Physics runs before FK animation, so animated attacks (punches, kicks) are
// invisible to the physics collision system. The proper solution is a separate
// HitDetection system that runs AFTER animation and uses swept-volume overlap
// queries between attack hitboxes and defender hurtboxes.
```

**[E]** The whole physics-first design (§1.2f) is defeated by frame
order: the punch happens after the contact pass looked. The proposed
remedy abandons physics-first entirely and returns to hitbox polling,
which the design document rejected at
`design/GAME_DESIGN_COMBAT_ENGINE.md:552-560`. **[E]** Two documents in
one repo arguing opposite sides, with the code following neither.

**Do not import the remedy.** Import the diagnosis: *a router that
consumes contacts must sit after every writer that moves a body that
frame, or it will not see the strike.* logosphere's ordering is
different and is addressed in §3.7.

**W2. Velocity had to be taken from the contact, not the particle.**

```cpp
// src/combat_system.cpp:855-858
// Animation moves particles via position constraints, so particle.vx/vy/vz may be 0
// but collision.relative_velocity reflects actual movement from constraint solving
float impact_velocity = std::abs(collision.relative_velocity);
```

**[E]** An externally-written body has no meaningful velocity field.
Every quantity the router uses must come from the occurrence, never
from the participant's state. This holds in logosphere too and is why
`ContactContext::approach_speed` exists (`contact_response.h:92`).

**W3. Magic constants fabricating motion.**

```cpp
// src/combat_system.cpp:618-621
constexpr float KNOCKBACK_RESISTANCE = 100.0f;
constexpr float HUMANOID_MASS = 12.0f;
float knockback_speed = total_force / (HUMANOID_MASS * KNOCKBACK_RESISTANCE);
engine_->get_dynamics_system().apply_impulse(defender_hips, dir_x * knockback_speed, ...);
```

**[E]** The game layer invents a mass, invents a resistance, computes a
speed physics never produced, and writes it onto the hips. This
violates logosphere INV-3 (energy is never created), INV-7 (one door
for momentum) and INV-10 (mass-uniform limits) simultaneously. It is
the single behaviour the router must make unwritable.

**W4. Silent fallbacks on every failure path.** Unknown body part falls
back to `"torso"` inside a bare `catch (...)`
(`src/combat_system.cpp:108-116`). **[E]** An untracked defender returns
a zeroed `HitResult` with `body_part_name = "unknown"` and no
diagnostic (`:700-709`). **[E]** Unknown damage type falls back to
`velocity * 10.0f` (`:126-127`). **[E]** Three places where a wiring
mistake looks like a working system.

**W5. Nondeterminism where the design demanded determinism.**
`design/GAME_DESIGN_COMBAT_ENGINE.md:453` states "Deterministic: Same
inputs -> same outputs (FORGE aligned)". `get_random_body_part` uses
`std::random_device` seeding `std::mt19937`
(`src/combat_system.cpp:26-36`). **[E]**

**W6. Dead code from a superseded model, never removed.**
`get_random_body_part`, `get_body_part_damage_mult`
(`src/combat_system.h:221-229`) and `get_attack_damage_mult`
(`:301-308`) have no callers anywhere in `src/` or `tests/`. **[E]** The
weighted-random hit-location model was replaced by the KG particle
lookup and left in place, so the file documents two incompatible
answers to "where was it hit".

**W7. Two damage paths, one bypassing the router.**
`src/creatures/shambler.cpp:148-157` branches: if a combat system is
attached and in combat, call `initiate_attack`; otherwise apply damage
directly to `DamageSystem`. **[E]** A whole class of damage never
reaches the router at all.
`tests/test_combat_engine_integration.cpp:356-359` is a **red test
encoding exactly this**: "TDD RED: Shambler's brain produces a damage
event which is routed directly to DamageSystem. The combat state
machine never sees it, so opponent_state stays Idle forever." **[E]**

**W8. "Universal damage" is gated on combat mode.**
`design/GAME_DESIGN_COMBAT_ENGINE.md:862-870` declares "DamageEvent is a
UNIVERSAL damage system [...] ANY high-force collision emits
DamageEvent" and lists falling trees and rockslides. The
implementation's first line is:

```cpp
// src/combat_system.cpp:785
if (!in_combat_ || !engine_) return;
```

**[E]** and the participant filter accepts only `combatant_entity_`
versus a member of `active_threats_` (`:811-829`). **[E]** A tree
falling on someone routes nowhere.

## 1.6 Weapon-versus-material specificity: the silver-bullet shape

Logomancers has **no** vulnerability, resistance or immunity mechanism.
A repo-wide search for `silver|werewolf|vulnerab|immune` over `src`,
`schema`, `design` returns only the material tables of §1.1, one design
sentence about a fire-vulnerable creature
(`design/GAME_DESIGN_CREATURES.md:105`, "Vulnerable to fire (takes 3x
damage)"), and one faction note. **[E]**

The nearest thing is a 5x5 implicit matrix: two damage types crossed
with five material names, expressed as ten `if` branches over two
functions (`src/combat_system.cpp:51-90`). **[E]** Adding "silver
against werewolf" to that structure means editing engine C++ and
recompiling. The design document knew this and filed it as a materials
integration TODO (`design/GAME_DESIGN_COMBAT_ENGINE.md:27-36`). **[E]**

**So the silver-bullet requirement is not retrieved from logomancers.
It is new work.** What logomancers contributes is the negative result:
a type-versus-material matrix expressed as C++ branches does not scale
past five materials and two damage types, and its authors said so.

---

# PART 2 — What logosphere has today

Honest classification. Usable / half-built / absent.

## 2.1 USABLE: the contact rule seam

`include/logosphere/interaction/contact_response.h` is already 80% of
the router's condition/effect half, and its header comment already
states the discipline the router must inherit (`:20-23`):

> "Engine effects stay mechanism, never policy. KNOCKBACK delivers an
> impulse; deciding what a push does to a walking creature is the
> game's."

**`ContactContext`** (`contact_response.h:69-95`) carries both entities,
both parts, both stable particle ids, the normal **re-oriented per
side** so `+normal` always means "away from other", contact point,
penetration and `approach_speed` (negative while closing). **[E]**

**Two registries**, condition and effect, `<name>` or `<name>:<args>`,
first colon separating (`contact_response.h:120-155`,
`contact_response.cpp:17-26`). **[E]**

**Fail-closed conditions, loud effects.** An unregistered condition
returns false with a diagnostic (`contact_response.cpp:120-129`); an
unregistered effect on an `ON_CONTACT` rule is refused at *load*, where
the name is still visible (`particle_interaction_system.cpp:436-444`).
**[E]** Both are the right defaults and the router keeps them.

**Shipped conditions: three.** `with_type:<EntityType>`
(`contact_response.cpp:70-73`), `impact_above:<m/s>` (`:82-88`, sign
handled: a separating pair never satisfies it),
`with_part_type:<EntityType>` (`:95-100`). **[E]**

**Shipped effects: two.** `knockback:<speed>` (`:157-166`) which
deposits into the impulse inbox and **moves nothing**, and
`emit_event:<name>` (`:172-180`) which emits a `TransformationEvent`.
**[E]**

**The self/other asymmetry is documented and load-bearing**
(`contact_response.h:47-63`): conditions ask about `other`, effects act
on `self`, so "an entity with NO rule of its own is unaffected by
everything". **[E]** The router inherits this. It is the reason routing
is safe: no rule can mutate a party that did not opt in.

## 2.2 USABLE: the contact producer and its gate

`ParticleInteractionSystem::process_contacts`
(`particle_interaction_system.h:201-202`, implemented at
`particle_interaction_system.cpp:291-376`). **[E]**

- **Gate:** nothing runs unless a contact rule is loaded or something
  subscribes to `bus.collisions()` (`:300-303`, `wants_contacts` at
  `:285-289`). Measured cost when open: 150 ns per fully-resolvable
  contact, 29.88 us/frame for 199 contacts
  (`docs/todo_plans/CONTACT_RESPONSE_DESIGN.md:316-331`). **[E]**
- **Episode throttling:** one event per contact *episode*, the frame a
  pair first touches (`:309-312`). Without it a resting humanoid emits
  for every foot-floor pair forever. **[E]**
- **Both sides evaluated**, with the normal flipped per side so no
  author reasons about which particle the solver called A (`:356-370`,
  `view_from` at `:238-262`). **[E]**

## 2.3 HALF-BUILT: hierarchy is one hop, and it stops at the wrong node

This is the most important gap.

```cpp
// src/interaction/particle_interaction_system.cpp:218-232
ContactParty resolve_party(const kg::KGModule& kg, uint32_t render_idx) {
    ...
    p.part = kg.getEntityByKGParticle(kgid);
    // One hop up. A part claimed by several owners is legal but
    // ambiguous for attribution, so take the first and stay
    // deterministic rather than guessing.
    auto owners = kg.getRelatedReverse(p.part, "HAS_PART");
    p.entity = owners.empty() ? p.part : owners.front();
```

**[E]** Exactly one reverse hop. The KG exposes `getRelated` and
`getRelatedReverse` (`include/logosphere/kg/kg_module.h:98`, `:104`) and
**no transitive traversal at all**. **[E]**

Today's live humanoid is flat, so one hop happens to be enough:
`createChildEntityWithParticles(humanoid_entity, "Arm", parts)`
(`src/worldgen/humanoid_generator.cpp:944-961`) builds
`Humanoid -HAS_PART-> Arm -> particles`, depth 2. **[E]**

A deeper tree exists in the same file and is **dead code**:
`create_arm` (`humanoid_generator.cpp:356-432`) builds
`Arm -HAS_PART-> {upper_arm, forearm, hand}`, and `create_leg`
(`:435-504`) the same for legs. Both are declared
(`include/logosphere/worldgen/humanoid_generator.h:189`, `:191`) and
**never called** anywhere in `src`, `tests` or `examples`. **[E]**

**Consequence [I], following directly from the code above:** the moment
anyone builds `Humanoid -> Arm -> Hand -> Finger`, a finger contact
resolves `self_entity` to `Hand`, `self_type` to `"Hand"`, and
`with_type:Humanoid` matches nothing. The router cannot be built on a
single hop.

Related and already recorded: four of six humanoid parts once silently
failed to exist as entities because their *side* was passed as their
*type* (`humanoid_generator.cpp:937-943`, the comment explaining the
fix). **[E]** Side is a property (`body_part_name`, `:954`), not a type.
Any address language must handle that.

## 2.4 USABLE, WITH A HOLE: the impulse inbox

`deposit_impulse` / `take_impulse` / `pending_impulse_count`
(`particle_interaction_system.h:208-234`, implemented
`particle_interaction_system.cpp:265-283`). **[E]** Sparse, keyed by
stable `KGParticleID`, accumulating within a frame so opposite hits
cancel rather than race. **[E]** Nothing drains it by default, and the
header says an unclaimed impulse is a game that has not wired its
mover, not an engine failure (`contact_response.cpp:154-156`). **[E]**

**The hole:** `knockback:<speed>` takes an author-declared speed
(`contact_response.cpp:159-165`) with no relation to the momentum the
contact actually carried. A route can declare `knockback:50` on a
brush. That is energy creation by authoring, and INV-3 says "a solver
operation that adds energy is a bug wherever it hides"
(`tests/invariants/INVARIANTS.jsonl` INV-3). **[E]** §3.6 fixes it.

## 2.5 USABLE: the refused-momentum ledger, added 2026-08-14/15

```
// include/logosphere/physics/physics_system.h:401-423
// A KINEMATIC body has inverse mass 0, so a contact against it
// computes an impulse and then delivers HALF of it: the striker is
// stopped, and the momentum the pinned body should have taken goes
// nowhere. [...] So the solver books it instead of discarding it.
void record_refused_impulse(size_t particle_id, float jx, float jy, float jz);
bool take_refused_impulse(size_t particle_id, float& jx, float& jy, float& jz);
```

**[E]** Storage at `physics_system.h:552`. This is the volitional /
non-volitional seam made into data, and it is exactly what the owner's
question needs.

**Half-built, per the tree's own account.** Only one site books today
(the normal-impulse apply); friction, gluon rows, warm start and every
angular apply drop the external half in silence
(`docs/todo_plans/MOTION_AUTHORITY_DESIGN.md:502-512`). **[E]** The
structural fix is booking inside `apply_pair_impulse`
(`MOTION_AUTHORITY_DESIGN.md:513-535`) and is aspirational under
candidate INV-32 (`:360`). **[E]**

**And nothing turns it into an occurrence.** It is a ledger a writer
polls, not an event a router can route. That gap is slice S1.

## 2.6 HALF-BUILT: `TransformationRule`, and its deliberate asymmetry

`schema/logosphere.yaml:975-994`, struct at
`particle_interaction_system.h:116-140`. Three separable slots **[E]**:

| slot | openness | why |
|---|---|---|
| `trigger` | **closed** enum (`schema:263-298`) | every value is a queue the engine owns; a game cannot add one without engine code |
| `condition` | **open** registry | evaluated when the event fires; not validated at load "by design" (`particle_interaction_system.cpp:459-463`) |
| `effect` | **floor**, not closed (`schema:300-332`) | the slot is typed `string` precisely so a game-registered name is as legal as a built-in |

The `TransformationEffect` description states the floor rule verbatim
(`schema:302-307`) and then the boundary (`:313-316`): "Engine effects
stay mechanism, never policy. [...] Bleeding, armour absorption and
their kin are game effects and deliberately absent here." **[E]**

**This asymmetry is the correct model and the router adopts it
unchanged.**

Two defects in the current rule firing:

- **All matching rules fire, in unspecified order.**
  `for (const auto& [rid, r] : rules_)` over an
  `std::unordered_map<uint32_t, TransformationRule>`
  (`particle_interaction_system.cpp:364-369`, member at
  `particle_interaction_system.h:335`). **[E]** Iteration order of an
  `unordered_map` is unspecified. INV-27 requires bit-identical repeat
  runs given identical seeds
  (`tests/invariants/INVARIANTS.jsonl` INV-27). **[E]** Today no rule
  produces an *outcome name*, so nothing observable depends on order,
  and the defect is latent. **[I]** The router introduces outcomes, so
  it must fix this before it can be correct.
- **No close event for contacts.** `open_contacts_ = std::move(current)`
  (`particle_interaction_system.cpp:374`) discards the previous set with
  no diff. **[E]** The volume path *does* the diff and emits
  `VolumeEvent{entered=false}` (`:190-199`). **[E]** So "they stopped
  touching" is unrouteable, while "it left the water" is routeable. The
  data for the fix is already there.

## 2.7 USABLE, SEPARATE BY DESIGN: the capability registries

`src/capability/effect_registry.cpp` ships four effects: `speed_cap`,
`cap_disable`, `cap_modifier`, `emit_event` (`:16-64`). **[E]** Same
`<name>:<args>` syntax (`:74-83`). **[E]**

`contact_response.h:12-18` explains why there are two registries and
not one:

> "The two registries here mirror capability::TriggerRegistry and
> capability::EffectRegistry in shape, deliberately, so there is one
> pattern to learn. They are NOT the same registries, because the
> contexts differ in kind: a capability rule is a state predicate over
> the KG and has no occurrence to look at, while everything here is
> about one specific touch."

**[E]** The push/pull split is argued at length in
`CONTACT_RESPONSE_DESIGN.md:30-46` and tabulated in
`docs/GAME_LAYER.md:609-626`. **[E]** The router is on the push side and
does not touch capability rules.

**Two capability mechanisms the router should steal, not reinvent:**

- **Rule groups.** `rule_group.M.name` / `rule_group.M.enabled` on the
  entity, with rules naming a group and a fail-closed lookup
  (`src/capability/capability_profile.cpp:117-133`). **[E]** A third
  party flips one flag and a whole bundle of rules goes on or off. That
  is dynamic routing load/unload, already built, already tested.
- **Relation-scoped cascade.** `relation:<TYPE>:<factor>` propagates a
  rule's consequence along any named KG relation, with `children:` as
  an alias for `relation:HAS_PART:` (`capability_profile.cpp:187-212`).
  **[E]** Hierarchical consequence propagation, already built.

Both are per-part and one hop
(`capability_profile.cpp:113`, `getRelated(entity_id, "HAS_PART")`).
**[E]**

## 2.8 The frame, as it actually executes

From `src/core/engine.cpp` **[E]**:

```
  1180  butterfly_flight_.update            (dynamics/animation writers)
  1188  particle_system_.update_bvh()
  1196  physics_system_.update(...)         (the whole substep loop)
  1219  interaction_system_.apply_volume_forces(...)
  1223  interaction_system_.process_filtered_overlaps(...)
  1248  interaction_system_.process_contacts(contacts, kg_module_, &event_bus_)
  1257  interaction_system_.tick_transformations(...)
  1267  dynamics_system_.update_post_physics(...)
  1268  humanoid_locomotion_.update_post_physics(...)
  1287  application_->update_game_post_physics(...)
```

Inside `physics_system_.update`, the real order is
`docs/todo_plans/PHYSICS_PIPELINE_SEQUENCE.md:15-49`: 23 frame/substep
nodes, contact rows built at node 9, velocity iterations at node 14,
position pass at 15, tear check at 17, rest state at 21, explosion
detector at 23. **[E]** That document supersedes two contradicting order
comments inside `physics_system_v4.cpp`, neither of which matched the
code (`PHYSICS_PIPELINE_SEQUENCE.md:1-10`). **[E]**

**The seam the router lives in is `engine.cpp:1248` to `:1257`: after
physics has finished the frame, before the post-physics writers at
`:1267-1287` drain anything.** That ordering is why a routed stagger
can land on the frame of the hit rather than one frame later. **[I]**

## 2.9 The binding invariants

From `tests/invariants/INVARIANTS.jsonl` **[E]**, the ones this design
must satisfy and how they constrain it:

- **INV-3** energy transforms, never created. Bounds what an effect may
  synthesize.
- **INV-7** one door for momentum, one predicate for "can this body
  receive it". The router may not open a second door.
- **INV-10** every threshold compared across bodies is a velocity or a
  mass-scaled momentum. Bounds what a condition may test.
- **INV-15** physics reads `solver_mode` and physical state only; no
  game category inside `src/core/physics_*.{h,cpp}`. The router adds
  zero lines to those translation units.
- **INV-22** exactly one mechanism owns a pair at a time.
- **INV-25** one contact-normal sign convention, A toward B, approach
  negative while closing.
- **INV-27** bit-identical repeat runs; randomness only through
  declared seeded channels.
- **INV-32** (candidate, aspirational,
  `MOTION_AUTHORITY_DESIGN.md:360`) one motion authority, held and
  released by a named writer, refusal booked at every door.

---

# PART 3 — The design

## 3.1 The one-sentence shape

> An **occurrence** produced by physics is resolved into two
> **hierarchical addresses**, matched against a **route table in the
> KG**, producing exactly one **outcome name** plus a set of
> **effects**, none of which may write a position or a velocity.

Networking analogy, held honestly: the occurrence is the packet, the
address chains are source and destination, the route table is a longest
-prefix match, the interceptors are middleboxes with declared stages,
and the effects are the forwarding action. The engine is the fabric.
It never decides where anything should go; it only guarantees that
exactly one route wins, deterministically, and that the decision is
data.

## 3.2 Vocabulary

### 3.2.1 Occurrences: what the engine produces

These are **facts from physics**. Each is a queue the engine owns, so
the set is **closed**: a game cannot add one without engine code. Same
rule as `TransformationTrigger` (`schema/logosphere.yaml:263-275`).

| Occurrence | Meaning | Carrier today | Status |
|---|---|---|---|
| `CONTACT_OPEN` | a pair began exchanging rigid contact | `process_contacts` episode open, `particle_interaction_system.cpp:309-319` | **EXISTS** |
| `CONTACT_CLOSE` | that episode ended | set diff over `open_contacts_` (`:374` discards it today; `:190-199` shows the pattern) | **GAP, S1** |
| `CONTACT_SUSTAIN` | episode persisting this frame | derivable from the same set, gated | **GAP, S1** |
| `MOMENTUM_REFUSED` | an authority refused momentum physics computed; it was booked | `record_refused_impulse` (`physics_system.h:416`) | **LEDGER EXISTS, no event, S1** |
| `MEDIUM_ENTER` / `MEDIUM_EXIT` | a body entered/left a declared medium | `VolumeEvent` via `process_filtered_overlaps` | **EXISTS** |
| `BOND_SEVERED` | a gluon tore | tear check, pipeline node 17 | **GAP, deferred** |

Nothing here is a game concept. `MOMENTUM_REFUSED` in particular is
pure physics bookkeeping: "I computed this impulse and could not
deliver it." **[I]**

### 3.2.2 Interactions: what a route names

An **interaction** is the *meaning* a route assigns to an occurrence.
It is a string on the route, and the ontology declares a **floor
enum**, not a closed one, exactly as `TransformationEffect` does
(`schema/logosphere.yaml:300-311`).

Engine floor, chosen because each is decidable from physics alone:

| Name | Decidable from |
|---|---|
| `touch` | `CONTACT_OPEN`, closing speed below the route's stated bound |
| `impact` | `CONTACT_OPEN`, closing speed above it |
| `rest` | `CONTACT_SUSTAIN`, relative speed at zero within tolerance |
| `slide` | `CONTACT_SUSTAIN`, tangential relative speed non-zero |
| `separate` | `CONTACT_CLOSE` |
| `refused` | `MOMENTUM_REFUSED` |
| `immersed` / `emerged` | `MEDIUM_ENTER` / `MEDIUM_EXIT` |

Everything the owner named as a game verb, `hit`, `bounce`, `catch`,
`parry`, `block`, `bite`, `burn`, is a **game name** written into the
`outcome` slot of a route. The engine never learns what `parry` means.
It only guarantees that when a route claims `parry`, exactly that route
won, and that any system subscribing to interactions sees the name.

`slide` and `rest` need tangential relative speed, which the solver
computes for friction (pipeline node 14) and does not export. That is
data flowing *out* of physics, not policy flowing *in*, so it is
INV-15-safe, but it is still an edit to a physics TU and is therefore
scheduled late (S10, optional).

## 3.3 Hierarchical addressing

### 3.3.1 The chain

The router replaces `resolve_party`'s single hop
(`particle_interaction_system.cpp:229-230`) with a full walk:

```
render index -> getKGParticleByRenderIndex -> KGParticleID
             -> getEntityByKGParticle      -> the leaf part entity
             -> getRelatedReverse(HAS_PART) repeatedly, until no owner
```

producing a **chain, root first**:

```
[Humanoid, Arm, Hand, Finger]
```

Rules, all mechanical:

- **Depth cap** (declared constant, INV-29): a walk longer than the cap
  stops and reports, naming the entity. Cycles in `HAS_PART` are
  illegal but must not hang the frame.
- **Multiple owners:** today's code takes `owners.front()` and
  documents the choice as "deterministic rather than guessing"
  (`:226-230`). The router keeps that, and additionally **reports** the
  ambiguity once per entity, because a part with two owners is an
  authoring error the current code hides.
- **Chains are cached per `KGParticleID`**, invalidated on
  `relations()` events touching `HAS_PART` (the bus already emits them,
  `docs/GAME_LAYER.md:240-246`). Contacts are not rare and the walk
  must not run per contact per frame. **[I]**

### 3.3.2 The address pattern language

A route's `self` and `other` slots hold a pattern matched against a
chain. Segments are **ontology type names** (`kg.getType`), which is
decision D2 of `CONTACT_RESPONSE_DESIGN.md:278-297`: types are inert
with respect to physics filtering, category bits are not.

```
Humanoid                      the whole entity, and only the root
Humanoid/Arm                  an Arm that is a DIRECT part of a Humanoid
Humanoid/**/Hand              a Hand anywhere under a Humanoid
**/Finger                     any Finger, any owner
*                             anything (matches an unowned scenery part too)
**/Bullet[material=silver]    a Bullet whose `material` property is "silver"
Humanoid/Arm[body_part_name=right_arm]
```

- `/` separates segments, matched against the chain **root first**.
- `*` matches exactly one segment. `**` matches zero or more.
- A pattern must match a **prefix ending at the leaf**: the last
  segment binds to the contacting part. `Humanoid` alone therefore
  matches only a particle owned directly by the humanoid root, which is
  the honest reading and avoids `Humanoid` silently meaning "any part
  of a humanoid". Use `Humanoid/**` for that.
- `[prop=value]` is a `getProperty` equality filter, allowed on **any**
  segment. This is what answers `left_arm` versus `right_arm` without
  inventing types, and it is the direct lesson of
  `humanoid_generator.cpp:937-955`, where passing side as type made four
  of six parts silently not exist.
- Patterns are **compiled at load**, never parsed per contact.

### 3.3.3 Resolution order, stated unambiguously

Two phases. This is the answer to "what happens when two systems claim
the same interaction".

**Phase A, the claim. Exactly one route wins.** Among matching routes
with `claim: CLAIM`, order by this key, computed **at load**, compared
lexicographically, highest first:

1. `other` address specificity: count of literal (non-wildcard)
   segments, then count of property filters, then chain depth of the
   deepest literal segment.
2. `self` address specificity, same measure.
3. Number of condition clauses.
4. **Ontology subsumption depth** (owner ruling 2026-08-15, §3.3.4
   rung 2): with the syntactic keys tied, the route whose address names
   a SUBTYPE of the other route's outranks it. Measured as the size of
   the type's transitive ancestor set, `other` before `self`.
5. Declared `precedence` (integer, default 0). The author's explicit
   override, and the only knob.

Key 4 is placed after the syntactic keys deliberately: every pair of
routes that resolves under keys 1 to 3 today resolves identically, and
the only behaviour that changes is a case that is a load error now.
**Owner question, boarded rather than assumed:** whether subsumption
depth should instead sit inside the measure, ahead of property-filter
count. That ordering decides which wins between a route naming a
subtype and a route naming its supertype with a property filter.

A full tie on all five between two `CLAIM` routes is a **load-time
error naming both routes; neither is installed.** Not "first wins":
`rules_` is an `unordered_map` (`particle_interaction_system.h:335`) and
"first" is not a defined concept. An unresolvable tie is an authoring
bug, and INV-27 forbids resolving it by coin flip. It is also the only
input to the escalation rung (§3.3.4); nothing else may reach it.

**Phase B, the observers. All of them fire.** Every matching route with
`claim: OBSERVE` runs its effects, in the same sorted order, after the
winning claim. Observers cannot change the outcome name. They exist so
"log it", "bleed", "wake the herd" compose onto whatever the meaning
turned out to be.

**Most-specific-wins for MEANING. Accumulate for CONSEQUENCES.** This
is INV-22 ("exactly one mechanism owns a pair") applied one layer up,
and it is why the silver bullet works:

```yaml
# route S: the specific one
on: CONTACT_OPEN
self:  Werewolf/**
other: "**/Bullet[material=silver]"
when:  impact_above:5
outcome: seared
do:    "emit_event:silver_burn, damage.apply:3.0"
claim: CLAIM

# route G: the general one
on: CONTACT_OPEN
self:  Werewolf/**
other: "**/Bullet"
outcome: deflected
do:    "emit_event:bullet_ricochet"
claim: CLAIM
```

Route S has two literal segments plus one property filter on `other`
against route G's one literal segment, so S outranks G on key 1. Fire a
silver bullet at the werewolf: `seared`. Fire a lead one: `deflected`.
Fire either at a hippo: neither route's `self` matches, no claim, no
outcome, and physics has already done the only thing that was going to
happen. **No engine change for any of it.**

### 3.3.4 The escalation ladder (owner ruling, 2026-08-15)

Resolution is not one mechanism. It is four rungs, fast and automatic
at the bottom, slow and rare at the top, in the owner's DIKW frame.
Recorded in full in `tests/invariants/LEDGER.md`; the operative parts:

**The ontology is read at LOAD, never in a frame** (owner correction,
2026-08-15). Rungs 1 and 2 are both compile steps: they compute each
route's key and sort the table once. At runtime a frame walks an
already-sorted vector and takes the first match. Nothing consults the
type lattice per contact, and nothing may.

| Rung | Reads | When | Per-frame cost |
|---|---|---|---|
| 0 DATA | the occurrence physics produced | every contact, in frame | producing it |
| 1 INFORMATION | route addresses, keys 1-3 | **at load**, to compute keys and sort | none |
| 2 KNOWLEDGE | the type lattice, key 4 | **at load**, on a syntactic tie | none |
| 3 WISDOM | the conflict, the corpus, a model | offline, never in a frame | none |
| (runtime) | the sorted vector | every match | one walk, first match wins |

Rung 2 is the rung this ruling adds, and it repairs a real defect: the
key was purely syntactic, so two routes with the same address SHAPE
naming a subtype and its supertype tied on every key and produced a
load-time error, when one of them is strictly more specific by
inheritance. The machinery is already in the tree
(`OntologyRegistry::isSubtypeOf`, `ontology_registry.h:197`;
`ancestorsOf`, `:314`, transitive sets built at load). **[E]**

**One gap to state plainly:** `facets` do NOT inherit. They are
explicit per type by decision (`EntityTypeDef`, `ontology_registry.h:
50-51`, pointing at `docs/KNOWLEDGE_LAYER.md`). **[E]** So rung 2
orders routes that address entity TYPES and buys nothing for routes
that filter on facets. A subtype does not acquire its parent's facets,
and any authoring guidance that assumes it will is wrong.

**Rung 3's contract.** Four rules, and they are what keep INV-27:

1. **Never inside the frame.** A frame that reaches an unordered tie
   uses the fail-closed answer: refuse, report, name both routes.
2. **Its output is never an outcome.** It emits a route, a
   `precedence`, or an ontology edit. It writes rules, not results.
3. **So the same conflict escalates once.** The investment mutates the
   table; the table is what runs. That is the owner's feedback loop,
   and the reason runtime stays a compiled lookup.
4. **Provenance is mandatory** on anything it emits: the triggering
   conflict, the corpus, the producing version. Readable, revocable.

**Sequencing: design rung 3 now, build it last.** On the current
corpus it has nothing to do. Six of the ten Gedankenexperimente are
settled; the four open ones (GEDANKEN-3, 4, 5, 7) are open for reasons
ordering cannot touch. Two need engine physics that does not exist
(GEDANKEN-4 is F3, GEDANKEN-7 needs F4's absent restitution) and two
need an owner ruling on how far a route's authority reaches. The
deterministic rungs missed zero of ten. Ten hand-built cases are a
small, self-selected corpus, and that number is the reason to build the
rung last, not the reason to skip it.

## 3.4 The routing table: schema, location, authoring

**Location: the KG.** A new ontology class, added to
`schema/logosphere.yaml` and regenerated per `CLAUDE.md`'s ontology
workflow (edit YAML, run `scripts/generate_ontology.py`, commit both).

```yaml
  InteractionRoute:
    is_a: Entity
    description: >-
      One row of the interaction routing table. An occurrence produced
      by physics is matched against `self` and `other` address
      patterns plus `when`; the winning route names the interaction in
      `outcome` and applies `do`. Effects are mechanism; the outcome
      name is meaning; neither may write a position or a velocity.
    slots:
      - on            # InteractionOccurrence, CLOSED enum
      - self          # address pattern, matched against the reacting side
      - other         # address pattern, matched against the other side
      - when          # condition expression, OPEN registry, may be empty
      - outcome       # interaction name, FLOOR enum (games extend by name)
      - do            # comma-separated effect expressions, OPEN registry
      - claim         # RouteClaim: CLAIM | OBSERVE
      - precedence    # integer, default 0, tiebreak of last resort
      - route_group   # optional group name, gated like rule_group
```

Openness, deliberately mirroring `TransformationRule`
(`schema/logosphere.yaml:975-994`) **[E]**:

| slot | closed / open / floor | reason |
|---|---|---|
| `on` | **closed** enum | each value is a queue the engine owns |
| `self`, `other` | **open** grammar, closed syntax | any type name from any game's ontology |
| `when` | **open** registry | evaluated at fire time; unknown name fails **closed** with a diagnostic (`contact_response.cpp:120-129`) |
| `outcome` | **floor** enum | validator sees the engine's set; a game name is equally legal |
| `do` | **open** registry | unknown name refused at **load** (`particle_interaction_system.cpp:436-444`) |
| `claim` | **closed** enum | two values, and the resolution order depends on it |

**Authoring:** a route is a KG entity, so it is created the same way a
`TransformationRule` is today
(`docs/GAME_LAYER.md:716-744`, the Logotron trail-fade example), by a
game at setup, by a generator, by a save file, or **by a meta-agent at
runtime**. That last one is the point of putting it in the KG: a
narrative director standing outside the world can add, remove or
reprioritise routes, which is the "transmutable medium" the project
`CLAUDE.md` describes for meta-agents.

**Dynamic load/unload** is `route_group`, lifted verbatim from
capability rule groups (`capability_profile.cpp:117-133`): a group name
on the route, an enabled flag on the entity, fail-closed when the group
is undeclared. Flip one flag, a whole routing bundle goes live. That is
the "load / pre-load / dynamically load" requirement, and the mechanism
already exists and is tested.

## 3.5 The subscription contract for third-party systems

Three seams, in increasing order of power. A combat system uses all
three.

**(1) Register a condition.** `ContactConditionRegistry::register_condition`
(`contact_response.h:125`). The system contributes a named predicate;
the KG decides where it is asked. Combat registers `wielded_as_weapon`,
`in_attack_window`, `refused_speed_above`.

**(2) Register an effect.** `ContactEffectRegistry::register_effect`
(`contact_response.h:147`). The system contributes a named verb; the KG
decides when it runs. Combat registers `combat.resolve`,
`combat.apply_wound`, `motion.absorb`.

This pair is already how the engine says a game extends it
(`docs/GAME_LAYER.md:656-672`) and it covers most of "a combat system
subscribes when weapons are used": the route table says *when*, the
registered code says *what*.

**(3) Register an interceptor.** New, and the only genuinely new
contract. This is for the case the owner named: a system must change
the **outcome**, not merely add an effect.

```cpp
// Proposed shape. `stage` is required and orders interceptors.
struct InteractionDecision {
    const InteractionContext& ctx;   // both chains, the occurrence, the winning route
    std::string outcome;             // the current meaning
    // exactly one of these may be called, by at most one interceptor:
    void retarget(std::string new_outcome);  // change the meaning
    void veto();                             // cancel the claim entirely
    // any number of interceptors may call:
    void observe();                          // no-op marker, for symmetry
};

InteractionRouter::register_interceptor(
    InteractionOccurrence on,
    int stage,                       // unique among retarget-capable interceptors
    std::function<void(InteractionDecision&)> fn);
```

The contract, and the answer to "what happens when TWO systems claim
the same interaction":

- Interceptors run in ascending `stage`. Stages are **declared, not
  derived from registration order**, so the order is a property of the
  configuration and not of the link order.
- **A retarget-capable interceptor must declare a unique stage.
  Registering a second at the same stage is a registration error.**
  Observers may share stages freely.
- `retarget` and `veto` are **exclusive per interaction**: the first
  one to fire takes it. A second attempt in the same interaction emits
  a `RouteConflictEvent` naming both systems and the contested outcome,
  and is **ignored**. Loud, deterministic, and never last-writer-wins.
- `veto` falls through to the **next** most-specific `CLAIM` route. With
  none left, the outcome is `none`, no effects run, and physics keeps
  whatever it already did.
- An interceptor may **not** write a position, a velocity, or an
  impulse. It changes meaning. Effects, which the route names, do the
  acting. This keeps the "who may move a body" question out of the
  interception path entirely.

**Why not just let systems register more routes?** They can, and should,
for anything expressible as data. The interceptor exists for the case
where the discriminator is *not* in the KG: a combat system that must
consult its own frame-window state, an LLM director that must decide
this one narratively. Forcing those through the route table would mean
mirroring private state into the KG every frame.

## 3.6 Physics stays sovereign

Five mechanical guarantees, each with the invariant it serves.

**G1. The router adds zero lines to `src/core/physics_*.{h,cpp}`.**
It lives in `src/interaction/`, consumes physics output, and is
headless-core safe like its neighbours (`particle_interaction_system.h:20-23`).
Serves **INV-15**, and the existing static ratchet
(`test_inv15_owner_blindness`, cited in INV-15's mechanism) keeps it
honest without new machinery.

**G2. No effect writes `p.x/y/z` or `p.vx/vy/vz`.** The only
motion-adjacent output is `deposit_impulse`
(`particle_interaction_system.h:223`), which moves nothing by
construction (`contact_response.cpp:143-153`). Writers drain. Serves
**INV-7**: the router does not open a second door, it hands a request
to the door's owner.

**G3. Effect magnitudes are sourced from the occurrence, never
declared.** This is a **correction to today's engine**.
`knockback:<speed>` currently accepts a free speed
(`contact_response.cpp:157-165`) unrelated to the momentum the contact
carried. It becomes:

```
knockback:<fraction>        # 0 <= fraction <= 1
```

delivering `fraction` of the momentum the occurrence actually
carries: the booked refused impulse for `MOMENTUM_REFUSED`, or the
contact's closing momentum for `CONTACT_OPEN`. A fraction above 1 is
refused **at load**. Serves **INV-3** (energy never created) and kills
logomancers' W3 pattern by construction: there is no slot in which to
write a magic constant.

**G4. Conditions may only test mass-uniform quantities.** Velocities,
or momenta scaled by the mass they act on. `impact_above:<m/s>`
complies (`contact_response.cpp:82-88`). A raw force or impulse
threshold is refused at load. Serves **INV-10** verbatim: "A raw
impulse threshold means a shrug to a branch and a shove to a leaf, and
is forbidden."

**G5. Matching is deterministic.** Routes are held in a **sorted
vector**, keyed as in §3.3.3, never an `unordered_map`. Ties are load
errors. Serves **INV-27**, and repairs the latent defect at
`particle_interaction_system.cpp:364` before an outcome name makes it
observable.

Two more, inherited rather than added: the per-side normal flip
(`view_from`, `particle_interaction_system.cpp:251-254`) means no route
ever reads a world-up axis (**INV-6**), and the A-toward-B convention
with approach negative while closing is already the documented one
(**INV-25**).

## 3.7 Where the router sits in the frame

**One point: `src/core/engine.cpp:1248`**, where `process_contacts` runs
today. Immediately after `physics_system_.update` has finished the
frame (`:1196`), and before the post-physics writers at `:1267`,
`:1268` and `:1287`.

```
1196  physics_system_.update(...)              physics owns the frame
      ...                                      substep nodes 1-23 (PHYSICS_PIPELINE_SEQUENCE.md)
1219  apply_volume_forces                      medium forces
1223  process_filtered_overlaps                medium episodes
      ---- ROUTER ----------------------------------------------------
      R-in      harvest occurrences:
                  get_collision_events()       -> CONTACT_OPEN / SUSTAIN
                  set diff over open_contacts_ -> CONTACT_CLOSE
                  drain refused ledger         -> MOMENTUM_REFUSED
                  volume episodes              -> MEDIUM_ENTER / EXIT
      R-match   resolve chains, match routes, run interceptors
      R-out     emit InteractionEvent; apply effects
                  (KG writes, bus emits, impulse deposits ONLY)
      ----------------------------------------------------------------
1257  tick_transformations                     particle lifecycle (unchanged)
1267  dynamics_system_.update_post_physics     drains inboxes, decides motion
1268  humanoid_locomotion_.update_post_physics drains inboxes, decides motion
1287  application_->update_game_post_physics   the game's turn
```

Three properties of this position:

- **The router is downstream of every physics decision that frame.** It
  cannot influence the solve, which is why it cannot violate anything
  the solver guarantees. **[I]**
- **The router is upstream of every writer.** A routed stagger reaches
  locomotion on the frame of the hit, not the next one. This is the
  direct answer to logomancers' W1: there, hit detection ran before the
  animation that produced the strike
  (`src/combat_system.cpp:950-954`), so the strike was invisible. Here
  animation runs at `:1180`, physics sees the animated positions at
  `:1196`, and the router sees the resulting contacts at `:1248`.
- **The router runs once per frame, not per substep.** Occurrences are
  already deduplicated across substeps
  (`particle_interaction_system.cpp:311`).

**One thing this ordering cannot do:** it cannot turn a contact into a
constraint before the solver runs. A "catch", a hand closing on a
thrown ball, is arguably that. See open question Q7.

## 3.8 Composability: the burning-hand chain, link by link

The owner's chain, in the proposed vocabulary, with the engine carrier
for each link and an honest status. **[E]** for existing carriers,
**GAP** where the mechanism is named but absent.

**Link 0. The hand enters fire.**
Occurrence `MEDIUM_ENTER`. Carrier: `VolumeEvent` from
`process_filtered_overlaps` (`particle_interaction_system.cpp:190-199`).
**[E]** Fire is an interaction profile declaring a medium
(`particle_interaction_system.h:79-94`).

```yaml
on: MEDIUM_ENTER
self:  "Humanoid/**/Hand"
other: Fire
outcome: immersed
do:    "state.set:burning, emit_event:hand_ignited"
claim: CLAIM
```

**Link 1. The entity learns its hand is burning.**
Carrier: `state_changes()` `WorldEvent`, emitted automatically by
`setProperty` when a bus is wired (`docs/GAME_LAYER.md:234-240`).
**[E]** `state.set` is a **game** effect. The engine ships neither the
property nor its meaning.

**Link 2. The entity retracts in panic.**
**This is not a physics occurrence and the router does not produce it.**
It is the game's behaviour layer, subscribed to `state_changes()`,
commanding a retract clip. **[E]** as a seam
(`docs/GAME_LAYER.md:148-165`), game policy as content. Naming this
honestly matters: the chain is not "physics all the way down", it is
physics, meaning, decision, drive, physics.

**Link 3. The hand flies.**
The retract clip drives the arm. Motion authority for those particles
is EXTERNAL (a writer owns position). No event; a drive. **[E]** as a
mechanism, `humanoid_locomotion_.update_post_physics`, `engine.cpp:1268`.

**Link 4. The hand hits the cabinet.**
Occurrence `CONTACT_OPEN`. Carrier: physics `CollisionEvent`
(`physics_system.h:66-91`) translated to `RigidContact`
(`engine.cpp:1237-1248`) and resolved to entities by `process_contacts`.
**[E]** Route:

```yaml
on: CONTACT_OPEN
self:  "Humanoid/**/Hand"
other: "Cabinet/**"
when:  impact_above:2.0
outcome: impact
do:    "emit_event:hand_struck_object"
claim: CLAIM
```

**Link 5. The cabinet does not absorb the hand's momentum.**
Occurrence `MOMENTUM_REFUSED`. The hand's authority is EXTERNAL, so the
solver computes an impulse and delivers only half; the other half is
**booked** (`physics_system.h:401-423`). **[E] as a ledger, GAP as an
occurrence** (slice S1), and **half-built as a mechanism**: only one
site books today (`MOTION_AUTHORITY_DESIGN.md:502-512`). **[E]**

**Link 6. The hand bounces.**
Route on `MOMENTUM_REFUSED`:

```yaml
on: MOMENTUM_REFUSED
self:  "Humanoid/**/Hand"
other: "Cabinet/**"
when:  refused_speed_above:3.0
outcome: rebuffed
do:    "motion.release_authority"     # game effect
claim: CLAIM
precedence: 10
```

`motion.release_authority` is a **game** effect calling the engine's
release path; the hand becomes solver-owned and **physics produces the
bounce**. The router never computed a trajectory. Below the bound, a
lower-precedence route selects `motion.absorb`, and the drive drains
the booked impulse and shifts the whole arm. **GAP**: the release path
is INV-32 work (`MOTION_AUTHORITY_DESIGN.md:360`, status aspirational),
and this is exactly its Q1.

**Link 7. The bounce contact ends.**
Occurrence `CONTACT_CLOSE`, outcome `separate`. **GAP, S1**, and the
data for it is already in `open_contacts_`
(`particle_interaction_system.cpp:374`).

**Score: links 0, 1, 2, 3, 4 are carried by mechanisms in the tree
today. Links 5, 6, 7 need S1 (occurrences) and the motion-authority
ruling.** Every link is either a physics fact or an explicit game
decision. Nothing in the chain is the router inventing motion.

## 3.9 The volitional-versus-physical case

### Eva walks into a tree

Physics: Eva's driven torso particle and the trunk exchange contact. The
solver prices the row, delivers what each side can take, and **books
what Eva's authority refused** (`physics_system.h:401-423`).

Route:

```yaml
on: MOMENTUM_REFUSED
self:  "Humanoid/**"
other: "Tree/**"
outcome: blocked
do:    "motion.absorb"
claim: CLAIM
```

What happens, precisely:

- **The trunk gets exactly what physics delivered and not one newton
  more.** The router deposits nothing on the trunk. So "the trunk
  absorbs her momentum" never occurs, because her momentum was never
  transferred: it was refused and booked. This is the whole reason
  `MOMENTUM_REFUSED` is the right occurrence for this case rather than
  `CONTACT_OPEN`.
- **Eva stops because her own writer spends the booked momentum
  cancelling her own drive.** `motion.absorb` is a game effect that
  drains via `take_refused_impulse` and tells locomotion to stop
  advancing the walk target. Locomotion stops her. The router named the
  situation `blocked`; it did not stop anyone.
- **The invariants hold:** no energy created (the momentum spent is the
  momentum booked, INV-3), one door (the router never wrote a velocity,
  INV-7), one owner of the pair (one CLAIM route, INV-22).

Today this route would fire and do nothing visible, because the
humanoid is inert for a different reason: the torso is DYNAMIC but
exempted from response by `is_quat_driven && owner`
(`tests/test_humanoid_knockback.cpp:26-45`, measuring a boulder passing
through a 15.625 kg chest at a constant 7.99 m/s). **[E]** That is
INV-32's problem, not the router's, and the router is the consumer that
makes fixing it worth something.

### A boulder hits Eva, and the outcome depends on the boulder

Same occurrence, same `self`, different `other`, two routes:

```yaml
- on: MOMENTUM_REFUSED
  self:  "Humanoid/**"
  other: "Boulder"
  when:  refused_speed_above:1.5
  outcome: staggered
  do:    "motion.absorb"
  claim: CLAIM
  precedence: 0

- on: MOMENTUM_REFUSED
  self:  "Humanoid/**"
  other: "Boulder"
  when:  refused_speed_above:6.0
  outcome: felled
  do:    "motion.release_authority"
  claim: CLAIM
  precedence: 10
```

Both match a fast boulder; `precedence` breaks it and `felled` wins.
Only the first matches a slow one.

`refused_speed_above` is the **booked impulse divided by the struck
body's true mass**, so it is a velocity. INV-10 is satisfied in the
letter: it forbids "a raw impulse threshold", not a velocity one, and
its mechanism list names velocities explicitly.

**But it is not satisfied in spirit, and that must be said.**
`MOTION_AUTHORITY_DESIGN.md:917-925` argues against the threshold
option on the grounds that any such number "would have to be [...]
compared against something derived rather than declared (INV-9,
INV-29). No such derivation exists today, and inventing one is a design
decision, not a tuning knob." **[E]** The router does not resolve that.
What it does is move the number **out of engine C++ and into the KG**,
where a derivation can later replace the literal without any engine
change. Whether `1.5` and `6.0` are authored or derived from mass,
stance and support polygon is open question Q5, and it is the owner's
Q1 in different clothes.

---

## 3.10 The TDD ladder, red first

Every rung states what it measures. Each prints values, not PASS. Each
has a control that proves the check can fail, per
`CONTACT_RESPONSE_DESIGN.md:389-403`.

| Rung | Claim under test | Red today because |
|---|---|---|
| **R0** | A contact between entities no route mentions performs **zero** chain resolution and zero effects. Counter asserts the skip, not merely the absence of effects. | new counter |
| **R1** | `Humanoid -> Arm -> Hand -> Finger` resolves to the chain `[Humanoid, Arm, Hand, Finger]`. | `resolve_party` walks one hop, `particle_interaction_system.cpp:229-230` |
| **R2** | `Humanoid/**/Hand` matches that chain; `Humanoid/Hand` does not; `**/Hand` does; `Tree/**` does not. Control: the negative cases must fail. | no matcher |
| **R3** | Two `CLAIM` routes match; the more specific outcome is emitted and the loser fires **nothing**. Control: swap specificity, assert the outcome flips. | no claim phase |
| **R4** | Two `CLAIM` routes tied on all four key components produce a **load error naming both**, and neither installs. | no tie detection |
| **R5** | One `CLAIM` plus two `OBSERVE` routes yields one outcome and three effect applications, in specificity order, byte-identical across two in-process runs (INV-27). | order is `unordered_map` today, `:364` |
| **R6** | `**/Bullet[material=silver]` selects the werewolf's `seared` route; a lead bullet selects `deflected`; a hippo with no route gets neither. | no property filter |
| **R7** | An interceptor at stage 10 retargets `graze` to `wound`; the emitted event carries `wound` and names the retargeting system. | no interceptors |
| **R8** | Two retarget-capable interceptors at the same stage produce a **registration error**, not last-writer-wins. | no interceptors |
| **R9** | A veto falls through to the next `CLAIM` route; with none, outcome is `none` and no effects run. | no interceptors |
| **R10** | Striking a body whose authority refuses emits `MOMENTUM_REFUSED`, and the reported magnitude equals the booked total within 1%. Mirrors INV-32's R4. | ledger exists, no occurrence |
| **R11** | Two bodies touch then separate: exactly one `CONTACT_OPEN` and one `CONTACT_CLOSE` per episode, never two of either. | `open_contacts_` discarded, `:374` |
| **R12** | **The router writes no motion.** Two identical scenes, one with a `knockback` route and one with the same route whose effect is `emit_event`, are **bit-identical** in every position and velocity for N frames until a writer drains the inbox. | new |
| **R13** | `knockback:<fraction>` can never exceed the occurrence's momentum; `ENERGY_LEDGER=1` shows `d_TOTAL` does not rise across a routed contact (INV-3). Control: a fraction above 1 is refused at load. | today's `knockback:<speed>` is unbounded, `contact_response.cpp:157-165` |
| **R14** | Eva walks into a trunk: her forward progress goes to zero, the trunk's velocity change equals exactly what physics delivered, and `sum(booked) == sum(drained)`. | needs S1 + INV-32 |
| **R15** | Same route table, two boulders: slow gives `staggered` with Eva displaced and still driven; fast gives `felled` with authority released and physics owning the fall. | needs the authority ruling (Q5) |

---

## 3.11 Slices, dependency ordered

No estimates. Dependencies only.

**S1. Occurrence completion.** `CONTACT_CLOSE` and `CONTACT_SUSTAIN`
from the existing `open_contacts_` set diff (the pattern is already at
`particle_interaction_system.cpp:190-199`). `MOMENTUM_REFUSED` by
draining `take_refused_impulse` at the router's frame point into an
occurrence queue. No physics TU edits. **Blocks everything: a route
cannot name an occurrence that is not produced.**

**S2. Hierarchical resolution.** `resolve_chain` replacing the one-hop
`owners.front()`, depth-capped, cycle-guarded, ambiguity-reported,
cached per `KGParticleID` and invalidated on `HAS_PART` relation events.
`InteractionContext` carrying both chains, with `ContactContext` kept as
a projection so every existing rule and test keeps working. Depends on
nothing.

**S3. Address language.** Parser, load-time compiler, matcher,
specificity scorer. Depends on S2 for something to match against.

**S4. Ontology.** `InteractionRoute` class, `InteractionOccurrence`
closed enum, `RouteClaim` closed enum, `Interaction` floor enum for
outcome names. Regenerate per the ontology workflow. Loader with
load-time validation: unknown occurrence skips with a diagnostic,
unregistered effect skips (mirroring
`particle_interaction_system.cpp:436-444`), `CLAIM` tie is an error,
`knockback` fraction above 1 is an error, non-mass-uniform condition is
an error. Depends on S3 for the address grammar to validate.

**S5. The resolver.** Sorted route vector, phase A claim, phase B
observe, `InteractionEvent` emitted on a new bus channel. Depends on
S1, S3, S4.

**S6. Interceptors.** Stage registry, `retarget` / `veto` / `observe`,
duplicate-stage refusal, `RouteConflictEvent`. Depends on S5.

**S7. Effect corrections.** `knockback` re-sourced from the occurrence
as a fraction. `refused_speed_above` condition. The **engine** ships
only the drain and release APIs that game effects like `motion.absorb`
and `motion.release_authority` need; it ships neither of those effects,
per `schema/logosphere.yaml:313-316`. **Gated on the motion-authority
ruling (Q5), because `release_authority` has no engine mechanism until
INV-32 lands.**

**S8. Migration, not coexistence.** The router takes `ON_CONTACT`,
`ON_CONTACT_FILTERED` and `ON_VOLUME_ENTER` from `TransformationRule`.
`ON_TIMER` **stays** on `TransformationRule`, because a timer is a
particle lifecycle event and not an interaction between two things.
Every existing contact rule, every test (`test_contact_bounce`,
`test_interaction_rule_loading`, `test_knockback_scene`,
`test_interaction_transformations`) and `docs/GAME_LAYER.md:628-714`
migrate in this slice. **The old contact path is deleted, not left as a
fallback** (project `CLAUDE.md` on replacing a mechanism). See Q1: this
is a fork, not a settled call.

**S9. Documentation and worked examples.** `GAME_LAYER.md` section, the
werewolf table as a shipped test fixture, both Eva cases, and the
burning-hand chain end to end with its gaps marked.

**S10. Optional, deferred.** `slide` and `rest` outcomes, requiring
tangential relative speed exported from the friction pass (physics TU
edit, data out only). `BOND_SEVERED` from the tear check. Both are
additive and neither blocks anything above.

---

## 4. Open questions for the owner

Each is a real fork with the evidence for both sides. None is a
recommendation wearing a disguise.

**Q1. Does the router subsume `TransformationRule`'s contact triggers,
or sit beside it?**

- *Subsume (S8 as written).* One place to look for "what happens when
  things touch". Honours the project rule that replacing a mechanism
  means deleting the old one. Cost: four test files, one docs section
  and every existing contact rule migrate in the same slice, and any
  game outside this tree breaks.
- *Sit beside.* No migration, no breakage. Cost: two mechanisms answer
  the same question, which is precisely the confusion
  `contact_response.h:12-18` and issue #36 exist to end, and a reader
  would have to know which one won.

**Q2. Most-specific-single-claim, or accumulate-everything?**

- *Single claim (§3.3.3).* An interaction has one meaning. Echoes INV-22
  one layer up. Cost: "silver bullet AND the werewolf is on fire" needs
  either one route naming the compound case, or an interceptor.
- *Accumulate.* Composable by construction. Cost: "what IS this
  interaction" has no answer, and two effects can contradict with
  nothing to arbitrate.
- Evidence both ways: capability rules already accumulate
  (`capability_profile.cpp:154-215`, min-merge and compound-multiply)
  and it is fine there **because they produce modifiers, not
  meanings**. Contact rules also accumulate today
  (`particle_interaction_system.cpp:364-369`) and it is fine **because
  there is no outcome name to conflict**. Introducing outcomes is what
  forces the choice.

**Q3. Where does the interaction name live?**

- *Floor enum* (§3.2.2), as `TransformationEffect` already is
  (`schema/logosphere.yaml:300-311`). Validator sees the engine's set;
  a game name is equally legal and invisible to validation.
- *Free string.* No validation at all, no schema churn, and a typo in
  `outcome` is a silent new meaning nobody subscribes to.
- *Closed enum.* Every game verb is an engine change. Rejected by the
  brief, listed for completeness.

**Q4. How specific may an address segment be?**

Today a humanoid's parts carry type `Arm` and property
`body_part_name="left_arm"` (`humanoid_generator.cpp:947-955`), so
`Humanoid/Arm` cannot tell left from right on types alone.

- *Property filters on any segment* (§3.3.2). Most expressive. Cost:
  a `getProperty` per filtered segment per match, and the filters are
  string equality only.
- *Last segment only.* Cheaper, and covers the silver-bullet and
  handedness cases. Cost: cannot say "a Hand of a Humanoid **whose
  faction is X**".
- *Distinct types instead* (`LeftArm`, `RightArm`). No filters needed.
  Cost: type explosion, and the tree already recorded what happens when
  side is smuggled into type (`humanoid_generator.cpp:937-943`: four of
  six parts silently did not exist).

**Q5. Absorb, ragdoll, or a threshold, and is the number derived or
declared?**

This is `MOTION_AUTHORITY_DESIGN.md` Q1 (`:901-925`) restated, because
the router cannot ship `motion.release_authority` without an answer.
The router's contribution is only that all three become expressible as
KG data rather than engine C++. The residual, unresolved: INV-9 and
INV-29 want the number **derived** from mass, stance and support, and
`MOTION_AUTHORITY_DESIGN.md:917-925` states plainly that no such
derivation exists today and inventing one is a design decision. A
declared literal in a KG route is more honest than a declared literal
in engine C++, and it is still declared.

**Q6. What is the unit of an interaction: a contact pair, an entity
pair per frame, or a game-declared session?**

- *Pair episode (today).* `process_contacts` throttles per particle
  pair (`particle_interaction_system.cpp:309-312`). Cheap and already
  correct. Cost: one punch produces N interactions, one per contacting
  particle pair.
- *Entity pair per frame.* One interaction per (attacker, defender) per
  frame. This is logomancers' aggregation
  (`combat_system.h:144-185`) and its stated reason
  (`GAME_DESIGN_COMBAT_ENGINE.md:1220-1223`: "8 knockbacks would be
  chaos"). Cost: a frame-level accumulator and a rule for combining
  normals, which logomancers did by averaging (`combat_system.h:173-184`).
- *Game-declared session.* `register_attack` / `end_attack`
  (`combat_system.cpp:486-514`, `:516-656`). Cost: the game must open
  and close windows. Benefit, and it is the real one: it is the **only**
  reason logomancers could tell a punch from a stumble
  (`combat_system.cpp:850-853`), and no purely physical signal
  distinguishes them.

**Q7. May the router influence a contact before the solver resolves
it?**

Everything in §3.7 is post-hoc: physics has already solved the frame.

- *Post-hoc only (proposal).* Physics sovereignty is trivially
  guaranteed; the router literally cannot perturb the solve.
- *A pre-solve advisory pass.* Would allow a "catch" to become a
  constraint rather than a bounce, and a "parry" to redirect rather
  than absorb. Cost, and it is decisive: it puts a game-visible hook
  inside the substep loop (`PHYSICS_PIPELINE_SEQUENCE.md` nodes 6-17),
  which INV-15 forbids in physics translation units. Any pre-solve
  influence would have to be expressed as declared constraints written
  **before** the frame, not as a callback during it.

**Q8. Is `MOMENTUM_REFUSED` routable before INV-32 lands?**

- *Yes, partially.* One site books today
  (`MOTION_AUTHORITY_DESIGN.md:502-512`), so routes fire for
  normal-impulse refusals and silently miss friction, gluon, warm-start
  and angular ones. Ships sooner; the missing half is invisible, which
  is the failure mode this repo dislikes most.
- *No, wait for the door.* The occurrence is complete or it is a lie.
  Cost: S1 is blocked on INV-32, and the whole volitional case with it,
  which is the case that motivated the router.

---

## 5. What this design does NOT do

Stated so a later reader can tell a boundary from an omission.

- It ships no combat model, no damage type, no material matrix. Those
  are game content in the route table and game code behind registered
  effect names. `schema/logosphere.yaml:313-316` already says so.
- It does not decide what a push does to a walking creature. It names
  the push and hands it to the writer.
- It does not make a body movable. Motion authority is INV-32's
  question and the router is its consumer, not its answer.
- It does not aggregate contacts into attacks. See Q6.
- It does not touch capability response rules. Different kind of rule,
  argued at `CONTACT_RESPONSE_DESIGN.md:30-46`.
