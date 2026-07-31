# Getting Started: Build a Game on Logosphere

This tutorial walks through the path from empty directory to a running
game, using the Eden example (`examples/eden/`) as the reference.

By the end you will have:
- A game-specific ontology YAML with custom types, relations, events
- Generated C++ registry code
- An `IApplication` implementation
- Entities created with KG properties and body-plan capabilities
- Event subscribers reacting to rule fires

## Prerequisites

- macOS arm64 (Apple Silicon), the only supported platform
- CMake 3.20+, C++17 compiler, GLFW 3, pkg-config
- Python 3 + `linkml-runtime` (for the ontology generator)

```bash
brew install glfw pkg-config cmake
conda env create -f environment.yml  # or pip install linkml-runtime pyyaml
```

## Step 1: Write your game ontology

Create `examples/mygame/schema/mygame.yaml`. Extend the engine's
`logosphere` schema by importing it and adding your own types.

```yaml
id: https://mygame.example.com/schema
name: mygame
title: MyGame
description: A tiny demo game on Logosphere.

default_range: string

prefixes:
  linkml: https://w3id.org/linkml/
  mygame: https://mygame.example.com/schema/
  logosphere: https://logosphere.dev/schema/

imports:
  - linkml:types
  - logosphere

classes:
  Player:
    is_a: Humanoid
    description: The player-controlled character.
    slots:
      - score

  Pickup:
    is_a: WorldEntity
    description: A collectible item.

  PickupEvent:
    is_a: WorldEvent
    description: The player picked something up.

slots:
  score:
    range: integer
    description: Accumulated score.
```

**Key points:**
- `is_a:` inherits from an engine type (`Humanoid`, `WorldEntity`, etc.). All engine properties and behavior apply.
- `slots:` declare new fields. The generator turns them into struct members.
- Your schema is additive. You cannot remove or modify engine types, only add.

## Step 2: Generate the C++ registry

The ontology generator emits two files per schema:
- `<game>_ontology.h` — type definitions (structs + enums)
- `<game>_ontology_registry.cpp` — the `registry()` function

The `scripts/generate_ontology.py` script picks up any schema under
`examples/*/schema/*.yaml`:

```bash
python scripts/generate_ontology.py
```

The generator ships with the repository (`scripts/cppgen/`, invoked
through `scripts/gen_cpp_header.py`); its Python dependencies are
declared in `environment.yml` (`linkml`, `pyyaml`). Generated
sources are committed, so you only need this step when you edit
schema YAML; building the engine and examples does not.

Output:
```
examples/mygame/src/generated/mygame_ontology.h
examples/mygame/src/generated/mygame_ontology_registry.cpp
```

Re-run whenever you change the YAML. Never edit generated files by hand.

## Step 3: Implement IApplication

The engine drives your game through the `Logosphere::IApplication`
interface (defined in `include/application.h`). The minimum
implementation:

```cpp
#include "application.h"
#include "core/engine.h"
#include "kg/kg_module.h"
#include "mygame_ontology_registry.h"

class MyGame : public Logosphere::IApplication {
public:
    // --- Lifecycle ---
    bool initialize() override { /* window, resources */ return true; }
    void shutdown() override {}

    // --- Platform (macOS in this repo) ---
    void display_framebuffer(uint8_t* buf, int w, int h) override;
    GLFWwindow* get_window() override { return window_; }

    // --- Engine hook: game init ---
    void initialize_game(void* engine_ptr) override {
        engine_ = static_cast<Engine*>(engine_ptr);

        // Extend the engine's KG ontology with your game's types
        engine_->get_kg().extendOntology(mygame::ontology::registry());

        // Create initial entities
        player_ = engine_->get_kg().createEntity("Player");
        engine_->get_kg().setProperty(player_, "score", "0");
    }

    void update_game(float dt) override { /* per-frame pre-physics */ }

private:
    Engine* engine_ = nullptr;
    GLFWwindow* window_ = nullptr;
    kg::EntityID player_ = kg::INVALID_ENTITY;
};
```

See `examples/eden/src/main.cpp` for a full working implementation.

## Step 4: Wire it into CMake

Copy `examples/eden/CMakeLists.txt` as a starting point. Link against
the `logosphere` library. The root `CMakeLists.txt` adds
`examples/eden` as a subdirectory; do the same for your game.

```cmake
add_executable(mygame
    src/main.cpp
    src/generated/mygame_ontology_registry.cpp
)
target_link_libraries(mygame PRIVATE
    logosphere glfw
    "-framework Cocoa" "-framework IOKit" "-framework CoreVideo"
    "-framework Metal" "-framework MetalKit"
    "-framework MetalPerformanceShaders" "-framework QuartzCore"
)
target_include_directories(mygame PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/generated
)
```

Build:
```bash
cmake -S . -B build
cmake --build build --target mygame
./build/mygame/mygame
```

## Step 5: Declare a body plan + capabilities

Game entities can use the engine's capability system to model
locomotion, manipulation, perception, etc. from body part health.

```cpp
#include "core/body_plan.h"

auto& kg = engine_->get_kg();
auto player = kg.createEntity("Player");
body_plan::declare_biped(kg, player);

body_plan::create_capability_part(kg, player, "Leg", "left_leg",
                                   /*max_hp=*/100.0f,
                                   /*capability=*/"locomotion",
                                   /*weight=*/1.0f, "left");
body_plan::create_capability_part(kg, player, "Leg", "right_leg",
                                   100.0f, "locomotion", 1.0f, "right");
```

At any point you can query the derived capability:

```cpp
auto cap = CapabilityProfile::compute_from_kg(kg, player, 75.0f, 0.9f, 1.8f);
float speed = DynamicsParams::from_capability(cap).max_walk_speed;
```

Damage a leg, recompute, and the speed drops. See `docs/GAME_LAYER.md`
for the full capability schema.

## Step 6: Add response rules

Rules declare "when X happens to this part, do Y" via KG properties.

```cpp
// On the left leg entity:
kg.setProperty(left_leg, "rule.0.trigger", "destroyed");
kg.setProperty(left_leg, "rule.0.effect",  "emit_event:limb_lost");
kg.setProperty(left_leg, "rule.0.payload.limb_name", "left_leg");
```

When the leg's health reaches 0, the engine emits a `WorldEvent` with
`event_type="limb_lost"` and a payload carrying `limb_name=left_leg`.

Built-in triggers: `health_below:<pct>`, `health_above:<pct>`,
`destroyed`, `relation_missing:<rel>`, `relation_present:<rel>`.

Built-in effects: `speed_cap:<v>`, `cap_disable:<cap>`,
`cap_modifier:<cap>:<factor>`, `emit_event:<type>`.

Custom triggers/effects: register at startup with
`TriggerRegistry::instance().register_trigger(...)` or
`EffectRegistry::instance().register_effect(...)`. See
`docs/GAME_LAYER.md`.

## Step 7: Subscribe to events

Your game reacts to engine events via the `EventBus`.

```cpp
void initialize_game(void* engine_ptr) override {
    engine_ = static_cast<Engine*>(engine_ptr);
    auto& bus = engine_->get_event_bus();

    // Subscribe to custom rule-fired events
    bus.state_changes().subscribe([this](const WorldEvent& e) {
        if (e.event_type == "limb_lost") {
            for (size_t i = 0; i < e.payload_keys.size(); i++) {
                if (e.payload_keys[i] == "limb_name") {
                    handle_limb_loss(e.payload_values[i]);
                }
            }
        }
    });

    // Subscribe to topological events (limb severed, etc.)
    bus.relations().subscribe([](const RelationEvent& e) {
        if (e.event_type == "RELATION_REMOVED" &&
            e.relation_type && *e.relation_type == "HAS_PART") {
            // Handle structural removal
        }
    });

    // Subscribe to deaths
    bus.deaths().subscribe([](const DeathEvent& e) {
        // Handle entity death
    });
}
```

All seven event channels: `collisions`, `damage`, `spawns`, `deaths`,
`perception`, `state_changes`, `relations`. See `docs/GAME_LAYER.md`.

## Step 8: Run it

```bash
cmake --build build
./build/mygame/mygame           # windowed
./build/mygame/mygame --no-head # headless (for tests/CI)
```

## Where to go next

- `docs/GAME_LAYER.md` — full reference for IApplication, ontology
  extension, event bus, capability schema, DynamicsParams override
- `examples/eden/` — full working example game (Eden: Knowledge Garden)
- `tests/test_ontology_extension.cpp` — runtime extend() example
- `tests/test_dynamics_override.cpp` — custom DynamicsParams example

## Common pitfalls

- **Forgot to re-run the generator.** Change YAML, run `python scripts/generate_ontology.py` before rebuilding.
- **Custom type used before `extendOntology()` call.** Extend the KG first in `initialize_game()`, then create entities.
- **`rule.N.trigger`/`effect` on the entity root, not a body part.** Rules evaluate per body part — attach them to the part, not the root.
- **Ontology `is_a` chain broken.** Every class must inherit (directly or transitively) from an engine base. Usually `WorldEntity`, `LivingEntity`, or `BodyPart`.
- **Unknown trigger/effect name silently no-ops.** Typos don't crash — they just don't fire. Verify with a test subscriber.
