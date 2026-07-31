# Game Layer Reference

How to build a game on top of the logosphere engine. This documents the
extension points: `IApplication`, `OntologyRegistry::extend()`, the
event bus, the capability/dynamics system, and KG property schema.

The engine is generic. It does not assume combat, simulation, puzzles,
or any genre. Games provide their own ontology, scene, dynamics
parameters, and event handlers. Everything below is opt-in unless
marked otherwise.

## 1. The IApplication Interface

`include/application.h` defines the contract between engine and game.
A game implements it; the engine drives it.

```cpp
class MyGame : public Logosphere::IApplication {
    bool initialize() override;             // window + resources
    void shutdown() override;
    void display_framebuffer(uint8_t* buf, int w, int h) override;
    GLFWwindow* get_window() override;

    // Optional lifecycle hooks
    void initialize_game(void* engine_ptr) override;  // after engine init
    void update_game(float dt) override;              // before physics
    void update_game_post_physics(float dt) override; // after physics+anim
    void render_game() override;                      // game overlay

    // Optional input (return true to consume)
    bool handle_key(int key, int sc, int action, int mods) override;
    bool handle_mouse_button(int btn, int action, int mods) override;
    bool handle_mouse_move(double x, double y) override;
    bool handle_mouse_scroll(double xo, double yo) override;

    // Optional scene + KG
    unsigned int create_initial_scene(
        std::function<int(const Particle&)> add_particle) override;
    void* getKGModule() override;
};
```

**Frame order:** `update_game` → engine physics + animation →
`update_game_post_physics` → engine render → `render_game` → display.

`update_game_post_physics` is the right hook for game logic that needs
post-animation positions (e.g., punch hit detection that needs the
hand at its animated location, not its rest position).

**Known gap:** `create_initial_scene` only adds particles. TODO[ARCH-003]
in `application.h:134` proposes a full SceneBuilder for lights, sounds,
audio sources. Until then, games create lights/etc. via direct engine
API after `initialize_game`.

## 2. Extending the Ontology

The engine ships with `logosphere::ontology::registry()` —
auto-generated from `logosphere.yaml` by `generate_registry.py`. Games
add their own types via `OntologyRegistry::extend()`.

### How to add custom entity types at runtime

```cpp
// 1. Build your own registry (manually or via your own generated code)
kg::OntologyRegistry game_ontology;
game_ontology.addEntityType("Crop", "Plant", /*is_abstract=*/false);
game_ontology.addAncestors("Crop", {"Plant", "BodyPart"});
game_ontology.addProperty("Crop", "growth_stage", "integer", /*required=*/false);
game_ontology.addRelationType("PLANTED_IN",
    /*sources=*/{"Crop"},
    /*targets=*/{"Soil"});

// 2. Extend the engine's KG with your additions
engine.get_kg().extendOntology(game_ontology);

// 3. Use your types like any built-in type
auto crop = engine.get_kg().createEntity("Crop");
engine.get_kg().setProperty(crop, "growth_stage", "0");
```

`extend()` is **additive only**. You cannot remove or change engine
types. Conflicting type names overwrite (last-write-wins).

### Recommended pattern: pre-generated game registry

Match the engine's pattern — author your game schema in YAML, generate
a `your_game_ontology_registry.cpp` via `generate_registry.py`, and
call your `your_game::ontology::registry()` from `extend()`. This
gives you the same compile-time guarantees the engine has.

See `tests/test_ontology_extension.cpp` for a working runtime example.

### Facets: types carry meaning

Types accept free-form semantic tags in schema YAML — the Knowledge
layer's selection substrate:

```yaml
  TrailSegment:
    is_a: WorldEntity
    annotations:
      facets: world          # comma-separated: "world, llm_visible"
```

The engine defines NO facet names; `world` / `ui` / `debug` are game
conventions, exactly like game entity types. Facets are explicit per
concrete type (no `is_a` inheritance). Runtime:
`reg.addFacets("Crop", {"world"})`, lookup via
`registry.hasFacet(type, facet)` / `typesWithFacet(facet)`.

### Queries: one shape for every consumer

Full consumer guide with worked examples: [KNOWLEDGE_LAYER.md](KNOWLEDGE_LAYER.md).

`include/logosphere/kg/kg_query.h`. Select (types | facets | property
equals, ANDed) → project (property subset) → limit; `count_query` for
aggregation. The same `Query` serves C++ callers and prompt
serialization:

```cpp
kg::Query q;
q.facets = {"world"};                 // never leaks UI/debug types
q.where  = {{"state", "ripe"}};
q.props  = {"kind", "growth_stage"};  // projection bounds prompt cost
q.limit  = 64;                        // budget

auto rows = kg::run_query(kg, q);     // structs for game code
auto json = kg::render_query_json(rows);  // JSON lines for an LLM
```

Determinism is guaranteed (type order as given, ids ascending,
properties sorted) so rendered output is diffable and cacheable.
`serialize_kg_snapshot(type_list)` survives as a deprecated wrapper
over this and dies when consumers migrate.

## 3. The Event Bus

`include/logosphere/events/`. Two-tier system: synchronous signals
(immediate invariant maintenance) + a ring journal (the engine's
Information layer: capacity-retained history with sequence cursors).
Each event type has its own `EventChannel`.

### Subscribing

```cpp
// Synchronous: fired on emit, runs immediately
auto sub_id = engine.get_event_bus().damage().subscribe(
    [](const onto::DamageEvent& e) {
        // game reaction: spawn corpse, play sound, vfx, etc.
    });

// Journal: hold ONE reader per consumer for its lifetime; drain at
// your own pace. The cursor makes each drain "everything since I
// last looked" — seconds or minutes between drains is fine.
auto reader = engine.get_event_bus().damage().create_reader();
for (const auto& e : reader.drain()) {
    // batch process
}
```

### Channels available

| Channel | Type | Use |
|---|---|---|
| `collisions()` | `CollisionEvent` | Physics collisions |
| `damage()` | `DamageEvent` | HP changes |
| `spawns()` | `SpawnEvent` | Entity creation |
| `deaths()` | `DeathEvent` | Entity destruction |
| `perception()` | `PerceptionEvent` | AI sense events |
| `state_changes()` | `WorldEvent` | KG property mutations (via setProperty) |
| `relations()` | `RelationEvent` | Topological changes (HAS_PART created/removed, etc.) |
| `animation_contacts()` | `PhysicsContactEvent` | Animation-physics handoff |

`deaths()` means DEATHS: emit it when something the game considers
alive stops being alive (DamageSystem does this on HP zero; Logotron
does it on derez). `kg.destroyEntity` is bookkeeping and emits
nothing — a trail segment being recycled is not a death.

The engine calls `bus.advance_frame(game_time)` once per frame; it
only timestamps subsequent emits. Retention is capacity-based
(default 1024 events per channel, `set_capacity` to tune).

### The journal (meta-agent history)

Full consumer guide with worked examples: [KNOWLEDGE_LAYER.md](KNOWLEDGE_LAYER.md).

Every emit is stamped `{seq, frame, game_time}` and retained until
capacity evicts it. This is the substrate for consumers that think in
"what happened since": LLM directors, narrators, replay, analytics.

```cpp
// A director that fires every ~8s and wants exactly the deaths
// since its previous fire:
auto reader = bus.deaths().create_reader();   // once, at init
...
for (const auto& ev : reader.drain()) {       // at each fire
    prompt += describe(ev);                    // game voice, game-side
}
reader.dropped_count();  // events lost to capacity eviction — loud

// Stateless history query (no reader):
auto entries = bus.deaths().collect_since(seq, /*max_n=*/32);
// entries[i] = {event, seq, frame, game_time}
```

Readers are subscriptions: `create_reader()` starts at the head
(events already journaled are not re-delivered). History from before
a consumer existed is a `collect_since` query. A reader that falls
behind a full ring skips forward and counts the loss in
`dropped_count()` — never silent, never stuck.

Engine renderers over journal entries live in
`include/logosphere/events/journal_render.h`:
`render_journal_compact` (one stamped line per entry, any channel)
and `render_state_deltas` (state_changes folded per entity+property,
first prev → last value, net no-ops dropped — "what changed since
your cursor" for meta-agents). `setProperty` emissions carry
`{property, value, prev}` payloads to make this foldable.

Reference consumer: Logotron's Director drains `deaths()` at each
fire; its prompt's "Derezzes since your last intervention" block is
the drain, formatted game-side
(`examples/logotron/src/logotron_app.h`, `emit_death_event` /
`fire_director`).

### Automatic KG event emission

When an `EventBus` is wired into the `KGModule` via `set_event_bus()`,
the following mutations automatically emit events:
- `setProperty()` → `state_changes` (skipped if value is unchanged)
- `createRelation()` → `relations` with `event_type="RELATION_CREATED"`, `relation_type=<name>`
- `destroyRelation()` → `relations` with `event_type="RELATION_REMOVED"`, `relation_type=<name>`

Subscribers get structured streams — the `relations` channel carries
`RelationEvent` with `source_entity_id`, `target_entity_id`,
`relation_type` (the relation name string, extensible beyond the
engine's built-in enum values), and `event_type` (the action).

**Limb-severed example:** a game detects limb loss without needing a
health-based rule:
```cpp
bus.relations().subscribe([&kg](const RelationEvent& e) {
    if (e.event_type == "RELATION_REMOVED" &&
        e.relation_type && *e.relation_type == "HAS_PART") {
        // e.target_entity_id is the part that was severed
        auto part_id = static_cast<kg::EntityID>(std::stoul(*e.target_entity_id));
        auto name = kg.getProperty(part_id, "body_part_name");
        // game reaction: vfx, AI state, cutscene
    }
});
```

### Wiring pattern

System constructors take `EventBus*` (nullable). Null-safe everywhere.
This keeps the bus opt-in. See `src/core/damage_system.h` for the
canonical pattern.

## 4. Capability + Dynamics System

The engine derives capability factors (locomotion, manipulation,
rotation, perception) from KG body graph health. Games provide
the dynamics formulas.

### The split

- `CapabilityProfile` — engine. Aggregates body part health into
  capability factors via declared `cap_list` bindings + aggregation
  modes. Evaluates response rules (e.g., `speed_cap`). Pure KG-driven.
- `DynamicsParams` — game-overridable. 30+ derived params (walk speed,
  stride, gaze zones, animation timing). `from_capability()` is the
  default derivation; games can replace it.

### KG schema for capabilities

On the entity:
```
cap.<name>.expected_count    "2"       (for "average" mode)
cap.<name>.default_mode      "average" (or "minimum"/"binary"/"weighted_sum")
cap.<name>.normalize         "1.0"     (weighted_sum only)
```

On each body part:
```
cap_list                "locomotion,balance"  (CSV of capabilities this part contributes to)
cap.<name>.weight       "1.0"
cap.<name>.side         "left"               (optional, for asymmetric animation)
cap.<name>.binary_threshold  "0.5"           (binary mode only)
```

Response rules (per body part, up to 8 rules × 4 effects each):
```
rule.N.trigger    "<trigger_name>" or "<trigger_name>:<args>"
rule.N.effect     "<effect>"
rule.N.effect_2   "<effect>"            (up to effect_4)
rule.N.cascade    "children:<factor>"   (multiplies child parts' effective health)
```

Built-in trigger names (from `capability::TriggerRegistry`):
| Trigger | Fires when |
|---|---|
| `health_below:<pct>` | `(health_ratio * 100) < pct` |
| `health_above:<pct>` | `(health_ratio * 100) > pct` |
| `destroyed` | `health_ratio < 0.01` |
| `relation_missing:<relation>` | entity lacks `<relation>` to this part |
| `relation_present:<relation>` | entity has `<relation>` to this part |
| `age_above:<seconds>` | `(GameTime::now() - part.created_at) > seconds` |
| `age_below:<seconds>` | `(GameTime::now() - part.created_at) < seconds` |

Age triggers require a `created_at` KG property on the body part (a
`double` value, seconds). Games set this when creating the entity;
the engine does not auto-populate it. If `created_at` is missing, age
triggers are silent no-ops.

Games register custom triggers at startup:
```cpp
capability::TriggerRegistry::instance().register_trigger(
    "idle_longer_than",
    [](const capability::TriggerContext& ctx, const std::string& args) {
        // game logic — access ctx.kg, ctx.part_id, ctx.entity_id, ctx.health_ratio
        return false;
    });
```
Then reference by name in KG: `rule.0.trigger = "idle_longer_than:5.0"`.

Unknown trigger names are silent no-ops.

Built-in effects (from `capability::EffectRegistry`):
| Effect | Meaning |
|---|---|
| `speed_cap:<value>` | Cap the entity's speed_cap field at `value` (min of all active caps) |
| `cap_disable:<capability>` | Force `cap.<capability>` to 0 |
| `cap_modifier:<capability>:<factor>` | Multiply `cap.<capability>` by `factor` (compound across parts) |
| `emit_event:<type>` | Emit a `WorldEvent` on `state_changes` with `event_type=<type>` |

`cap_disable` beats `cap_modifier` when both target the same capability.

Games register custom effects at startup:
```cpp
capability::EffectRegistry::instance().register_effect(
    "heal",
    [](capability::EffectContext& ctx, const std::string& args) {
        float amount = std::stof(args);
        float cur = std::stof(ctx.kg.getProperty(ctx.part_id, "health"));
        ctx.kg.setProperty(ctx.part_id, "health",
                           std::to_string(cur + amount));
    });
```
Then reference by name in KG: `rule.0.effect = "heal:20"`.

The `EffectContext` provides read/write access to the KG, the part and
entity IDs, the rule prefix (for payload lookup), and mutable references
to the collectors (`speed_cap`, `disabled_caps`, `cap_modifiers`) so
custom effects can feed into the same aggregation pipeline as built-ins.

Unknown effect names are silent no-ops.

### `emit_event` semantics

The emitted event carries:
- `event_type` = the `<type>` string after the colon
- `source_entity_id` = the body part id that triggered the rule
- `target_entity_id` = the root entity id (the one experiencing it)

Games subscribe to `bus.state_changes()` and filter by `event_type`:
```cpp
bus.state_changes().subscribe([](const WorldEvent& e) {
    if (e.event_type == "limb_lost") {
        // game reaction — play vfx, update AI, trigger cutscene
    }
});
```

**Stateless firing:** the event fires every time `compute_from_kg()`
runs with the rule's condition true. There is no transition tracking
in the engine — that's game responsibility. A subscriber that only
wants "on transition" behavior tracks last-seen state itself.

**Rich payloads:** attach arbitrary metadata by setting
`rule.N.payload.<key>` KG properties on the body part. When the rule
fires, those become parallel `payload_keys` and `payload_values` vectors
on the emitted `WorldEvent`. Declared in the ontology, so subscribers
can rely on the shape.

```
rule.0.trigger = "destroyed"
rule.0.effect  = "emit_event:limb_lost"
rule.0.payload.limb_name = "left_leg"
rule.0.payload.severity  = "critical"
rule.0.payload.hp_lost   = "100"
```

Subscriber:
```cpp
bus.state_changes().subscribe([](const WorldEvent& e) {
    if (e.event_type != "limb_lost") return;
    for (size_t i = 0; i < e.payload_keys.size(); i++) {
        const auto& k = e.payload_keys[i];
        const auto& v = e.payload_values[i];
        // k="limb_name", v="left_leg", etc.
    }
});
```

Payloads are per-rule: `rule.0.payload.*` and `rule.1.payload.*` are
independent.

**Requires EventBus:** if no `EventBus` is wired into the KG module
(`kg.set_event_bus(&bus)`), `emit_event` is a silent no-op.

**Example use case:** on a limb body part, set
`rule.0.trigger="destroyed"` + `rule.0.effect="emit_event:limb_lost"`.
When `DamageSystem::apply_to_body_part` drives health to 0, the rule
fires and the game receives a typed event it can act on. Same pattern
works for monsters, humanoids, or any entity with body parts.

### Body plan templates

`src/core/body_plan.h` provides convenience setters:
```cpp
body_plan::declare_biped(kg, my_humanoid);
body_plan::declare_serpent(kg, my_snake, /*segment_count=*/20);
body_plan::declare_winged(kg, my_butterfly, /*wing_pairs=*/2);
body_plan::declare_quadruped(kg, my_wolf);
```

These set `cap.<name>.expected_count` and `default_mode` on the entity.
You still create the body part entities and set their `cap_list`
yourself (or use `body_plan::create_capability_part()` helper).

### Overriding DynamicsParams

```cpp
// Default: engine derives dynamics from capability factors
dynamics.register_humanoid_direct(hips, lleg, rleg, larm, rarm, torso,
                                  reflexes_ms, grit_W, entity_id);

// Override: game provides its own DynamicsParams
DynamicsParams custom = DynamicsParams::from_capability(my_cap);
custom.max_walk_speed = 0.3f;  // slow-motion puzzle game
custom.walk_turn_rate = 1.0f;
dynamics.register_humanoid_direct(hips, lleg, rleg, larm, rarm, torso,
                                  reflexes_ms, grit_W, entity_id,
                                  &custom);
```

`reflexes_ms` and `grit_W` remain physical inputs — they're used by
`CapabilityProfile` and stored as KG properties. The `custom_dynamics`
override only replaces the dynamics derivation step.

## 5. Damage System (optional)

`src/core/damage_system.h`. Generic HP tracking with type-based
resistance. Never instantiated by the engine — games create it if
needed.

```cpp
DamageSystem damage(&kg, &spatial, &event_bus);
damage.register_entity(entity_id, /*max_hp=*/100.0f);
damage.apply(entity_id, DamageType::Slash, 30.0f);
float hp = damage.get_hp(entity_id);
```

`DamageType` includes non-combat values (Fire, Cold, Poison, Pure).
For non-combat use cases (plant withering, fatigue), reuse the same
infrastructure with names that fit your domain.

When wired to the event bus, `apply()` emits `DamageEvent` and (on
death) `DeathEvent`. Body part damage (`apply_to_body_part`) updates
KG health, which triggers `recompute_capability()` via the
`state_changes` subscription in `ParticleDynamicsSystem`.

## 6. Particle Interaction Model (optional)

`include/logosphere/interaction/particle_interaction_system.h`. Games
declare how classes of particles interact — what rigid-contacts what,
what is a passable medium, what transforms on contact — as KG data.
The engine executes the policy. Fully opt-in: with no profiles
registered, every particle rigid-contacts every other exactly as
before.

### Profiles and contact masks

`ParticleInteractionProfile` KG entities carry a category bit and a
collides-with mask (Box2D-style symmetric masking); particles opt in
through `Particle::interaction_profile_id` (the profile's KG
EntityID, 0 = engine default). Two particles rigid-contact iff each
one's category is in the other's mask:

```cpp
// Declare in the KG (or register_profile() directly in C++):
auto water = kg.createEntity("ParticleInteractionProfile");
kg.setProperty(water, "category_bit", "3");
kg.setProperty(water, "collides_with_mask", "0");   // passable by all
kg.setProperty(water, "drag_coefficient", "30.0");  // declares a medium

auto& isys = engine.get_interaction_system();
isys.load_profiles_from_kg(kg);      // once at setup, after declaring

// Tag particles:
view[column_idx].interaction_profile_id = water;
```

Unknown or stale profile ids degrade to the default profile —
physics-as-usual, never mutual tunneling.

### Passable media

A filtered overlap is not decoration. When one side of a filtered
pair declares a medium (nonzero drag, buoyancy, or field), the engine
applies its forces to the intruding particle every frame it overlaps:

| Slot | Effect |
|---|---|
| `drag_coefficient` | `dv -= (c/m)·(v − v_medium)·dt` per axis — a river carries you |
| `buoyancy_factor` | gross lift `B·|g|` against the solver's gravity; net `(B−1)·g` |
| `field_fx/fy/fz` | directional force in Newtons, `dv += F/m·dt` |

Buoyancy opposes `PhysicsSystem::get_solver_gravity()` — the gravity
the solver actually integrates. (`get_gravity_vector()` reads the
force registry, which the solver never applies; in a default world it
returns zero.) A medium keeps its intruders awake, so nothing freezes
mid-water. v1 scope: solver-DYNAMIC intruders — locomotion-owned
bodies re-write their own velocities and couple at their own layer.

### Episode events

Filtered pairs and medium volumes emit once per contact EPISODE (the
frame a pair starts overlapping, not every frame it stays):

| Channel | Type | Fires |
|---|---|---|
| `contact_filtered()` | `ContactFilteredEvent` | pair declined rigid contact (profile ids) |
| `volume()` | `VolumeEvent` | medium entered (`entered=true`) / left (`entered=false`) |

```cpp
auto reader = engine.get_event_bus().volume().create_reader();
for (const auto* e : reader.drain()) {
    if (e->entered.value_or(false)) {
        // splash vfx, swim state, etc.
    }
}
```

### Declarative transformations

`TransformationRule` KG entities fire effects on particles when a
trigger condition holds:

| Slot | Values |
|---|---|
| `trigger` | `on_volume_enter`, `on_contact_filtered`, `on_timer` |
| `effect` | `swap_profile`, `fade_out`, `delete`, `emit_event` |
| `target_profile` | profile id written by `swap_profile` |
| `duration_s` | fade length / timer deadline (0 = next tick) |
| `trigger_profile` | volume/contact binding (0 = any profile) |

`load_rules_from_kg(kg)` mirrors profile loading. Volume and contact
triggers arm themselves off episode opens; `on_timer` rules are armed
by the game:

```cpp
isys.arm_transformation(rule_id, kg.getEntityKGParticles(entity));
```

Long-running effects track particles by stable `KGParticleID` and
drop out fail-safe if the particle dies mid-effect. Rules therefore
only track KG-backed particles (created via `add_particle_to_entity`)
— a particle outside the KG is outside the ontology. `emit_event`
emits `TransformationEvent{rule_name}` on the `transformations()`
channel; other effects act silently — declare a second rule on the
same trigger when you want the action AND an event.

### Worked example: Logotron's crashed-trail fade

Before: a 47-line per-frame function walking the KG, hand-animating
alpha, and calling `delete_particles_immediate` mid-session. After:
one rule declared at setup, armed at the crash seal.

```cpp
// initialize_game():
trail_fade_rule_ = kg.createEntity("TransformationRule");
kg.setProperty(trail_fade_rule_, "trigger", "on_timer");
kg.setProperty(trail_fade_rule_, "effect", "fade_out");
kg.setProperty(trail_fade_rule_, "duration_s", "2.0");
engine_->get_interaction_system().load_rules_from_kg(kg);

// the frame the AI crashes:
std::vector<kg::KGParticleID> ids;
for (auto t : kg.findByType("TrailSegment")) {
    if (kg.getProperty(t, "owner_cycle_id") != ai_id_str) continue;
    for (auto kgid : kg.getEntityKGParticles(t)) ids.push_back(kgid);
}
engine_->get_interaction_system().arm_transformation(trail_fade_rule_, ids);
```

The alpha ramps to zero over 2 s, then the particles are deleted
through the engine's deferred queue (triple-buffer safe — the old
immediate path raced in-flight GPU frames). The `TrailSegment` KG
entities survive the fade, so gameplay logic and tests still see the
trail.

## 7. Examples

| Pattern | Reference |
|---|---|
| Minimal IApplication | `include/application.h` interface, `examples/eden/src/main.cpp` example |
| Ontology extension | `tests/test_ontology_extension.cpp` |
| Event subscription | `tests/test_damage_events.cpp`, `src/core/damage_system.cpp` |
| Custom body plan | `src/core/body_plan.cpp`, `src/worldgen/butterfly_generator.cpp` |
| Custom DynamicsParams | `tests/test_dynamics_override.cpp` |
| Capability response rules | `tests/test_capability_system.cpp` |
| Interaction profiles + masks | `tests/test_interaction_profiles.cpp`, `tests/test_interaction_filtering.cpp` |
| Passable media + volume events | `tests/test_interaction_volume_forces.cpp` |
| Transformation rules | `tests/test_interaction_transformations.cpp` |

## 8. Engine Boundaries

What the engine provides:
- ParticleSystem, PhysicsSystem (motion, gluons, BVH)
- ParticleDynamicsSystem (animation pipeline, look-at, locomotion)
- KGModule (typed graph, validates against ontology)
- EventBus (typed channels, signals + log)
- DamageSystem (opt-in HP tracking)
- CapabilityProfile + DynamicsParams (capability aggregation, derived dynamics)
- ParticleInteractionSystem (contact masks, passable media, declarative transformations)
- RenderSystem, LightSystem (rendering, shadows)
- ChunkSystem, WorldGenSystem (streaming, generation)

What the engine does NOT provide:
- AI, behavior trees, planners (game responsibility)
- Game-specific entity types (use OntologyRegistry::extend())
- Combat rules, weapons, armor, status effects (game logic)
- Save/load (game responsibility)
- Networking, audio mixing, scripting layer

When in doubt: if it varies by genre, it's game. If every game needs
it, it's engine.
