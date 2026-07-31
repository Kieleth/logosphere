# Module Architecture

How the engine is organized into Core, Modules, and Plugins. Based on
patterns from Bevy, Unreal, O3DE, and Godot, adapted for a C++ static-lib
engine.

## Three tiers

### Core (always linked, mandatory)
The minimum viable engine. Everything here is unconditional and
always present in any build.

- `Engine` glue, lifecycle, config
- `ParticleSystem` — the data everything else references
- Math, transforms, geometry primitives
- Platform abstraction (platform layer, window, input)
- `GameTime` (time authority)
- Logging, metrics, telemetry primitives

A game linking only core can create particles, run the main loop, and
render a black screen. Nothing else works yet.

### Modules (first-party, opt-in at build time)
Optional capabilities shipped in-tree. Each module is a self-contained
unit with its own public headers, implementation, and CMake target.
Gated by a CMake option.

Current/planned modules:
- `physics` — XPBD physics, gluons, BVH
- `rendering` — software raster, Metal lighting, shadows
- `kg` — Knowledge Graph + ontology registry
- `events` — EventBus with typed channels
- `capability` — CapabilityProfile, DynamicsParams, trigger/effect registries, body plans
- `dynamics` — ParticleDynamicsSystem (animation, FK, locomotion)
- `damage` — DamageSystem (HP tracking, typed resistance)
- `worldgen` — procedural generators (humanoid, tree, butterfly)
- `llm` — external LLM integration (HTTP, already opt-in)

### Plugins (third-party or game-specific)
Games extend the engine by:
- Extending the ontology via `OntologyRegistry::extend()` (runtime, YAML-driven)
- Registering custom triggers via `TriggerRegistry::instance().register_trigger(...)`
- Registering custom effects via `EffectRegistry::instance().register_effect(...)`
- Subscribing to event channels
- Providing their own `DynamicsParams` derivation

Plugins don't have a formal descriptor file yet. They're just code the
game links against. A future `.logoplugin` JSON descriptor pointing to
a shared library is an option when there's a concrete use case.

## Directory convention

```
include/
  logosphere/
    core/              # Always linked
    capability/        # Each module has its own subdirectory
    events/
    physics/
    rendering/
    kg/
    dynamics/
    damage/
    worldgen/
    llm/

src/
  core/
  capability/
    module.cpp         # register_module(Engine&) entry point (future)
    CMakeLists.txt     # logosphere_capability target (future)
  events/
  physics/
  ...
```

**Public headers** go in `include/logosphere/<module>/`. Games include
them as:
```cpp
#include "logosphere/capability/capability_profile.h"
#include "logosphere/events/event_bus.h"
```

**Implementation** (`.cpp`) and **private headers** stay in
`src/<module>/`. These are invisible to library users.

## CMake target strategy (planned)

Following Bevy's feature-flag model, translated to CMake options:

```cmake
option(LOGOSPHERE_MODULE_PHYSICS    "Include physics module"    ON)
option(LOGOSPHERE_MODULE_CAPABILITY "Include capability module" ON)
option(LOGOSPHERE_MODULE_EVENTS     "Include events module"     ON)
# ... one per module

add_library(logosphere_core STATIC ...)

if(LOGOSPHERE_MODULE_CAPABILITY)
    add_library(logosphere_capability STATIC ...)
    target_link_libraries(logosphere_capability PUBLIC logosphere_core)
endif()

# Umbrella target: links everything the user opted into
add_library(logosphere INTERFACE)
target_link_libraries(logosphere INTERFACE
    logosphere_core
    $<$<BOOL:${LOGOSPHERE_MODULE_CAPABILITY}>:logosphere_capability>
    $<$<BOOL:${LOGOSPHERE_MODULE_PHYSICS}>:logosphere_physics>
    ...
)
```

Games link against `logosphere` (the umbrella). Minimal games can link
directly against `logosphere_core` and omit everything else.

This exists today: per-module STATIC targets (see "Current state vs
target state" below), with `logosphere` as the umbrella games link
against.

## Module registration pattern (planned)

Each module will expose a single entry point:

```cpp
// include/logosphere/capability/module.h
namespace logosphere::capability {
    void register_module(Engine& engine);
}
```

Called from the game's `main()` or a generated init function:

```cpp
int main() {
    Engine engine;
    engine.initialize();
    logosphere::capability::register_module(engine);
    logosphere::damage::register_module(engine);
    // ...
}
```

The pattern is borrowed from Bevy's `App::add_plugins` and Godot's
`register_types`. It's compile-time (static linking), no runtime
loading. Modules declare their dependencies via simple checks:

```cpp
void logosphere::capability::register_module(Engine& engine) {
    assert(engine.has_event_bus() && "capability requires events module");
    // ... wire subscribers, register built-in triggers/effects
}
```

This mirrors O3DE's `GetRequiredServices` without the reflection
machinery. Aligns with the "crash loud with actionable error" rule.

Not implemented yet. Today modules are linked into the engine binary
automatically and wired inside `Engine::initialize()`. The migration
to explicit `register_module` calls comes later.

## Type extension via the ontology

Games add their own entity types, relations, events, and properties by:

1. Writing a LinkML YAML schema (see `examples/eden/schema/eden.yaml`)
2. Running `python scripts/generate_ontology.py` to emit C++ registry
3. Calling `kg.extendOntology(game::ontology::registry())` at startup

This is the primary extension mechanism. It replaces what Unreal's
`UCLASS` macros or Unity's `MonoBehaviour` auto-discovery do in those
engines, but using declarative YAML + code generation instead of
macros or reflection.

See `docs/GETTING_STARTED.md` for the full workflow.

## Current state vs target state

**Current (post-phase-8 + per-module targets):** Public headers live
under `include/logosphere/<module>/` for capability, damage, events,
kg, llm, dynamics, worldgen, rendering, physics, core. Each of those
modules has its own CMake target:

```
logosphere_core        STATIC   — kg + capability + damage + game_time +
                                  particle geometry + narrow_phase
logosphere_events      INTERFACE — header-only event bus
logosphere_llm         STATIC   — HTTP client + HTTP LLMSystem + (optional) llama.cpp
logosphere_worldgen    STATIC   — chunk streaming + all generators
logosphere_dynamics    STATIC   — particle_dynamics_system + animation
logosphere_rendering   STATIC   — CPU raster + Metal GPU + BVHs for shadow rays
logosphere_physics     STATIC   — sequential-impulse solver + gluons + BVH
logosphere             STATIC   — umbrella target: engine.cpp + particle_system.cpp +
                                  vision / sense / input / lighting glue + app framework;
                                  links all the above PUBLIC
```

Games still link `logosphere` (unchanged link surface); the per-module
structure is transparent until someone wants a slim build.

**Remaining target state:** `register_module(Engine&)` entry points and
converting `logosphere` from STATIC into a pure INTERFACE (after
extracting vision, sense, npc-ai, celestial into their own modules).

## Migration order

The refactor happens in phases to minimize conflict with parallel
work and keep each PR reviewable:

1. **Phase 0** — this document ✅
2. **Phase 1** — `capability` module header move ✅
3. **Phase 2** — `events` ✅
4. **Phase 3** — `damage` ✅
5. **Phase 4** — `kg` ✅
6. **Phase 5** — `dynamics` header move ✅
7. **Phase 6** — `worldgen` header move ✅
8. **Phase 7** — `rendering` header move ✅
9. **Phase 8** — `physics` header move ✅
10. **Phase 9** — `llm` ✅
11. **Phase N** — per-module CMake targets ✅ (split into separate STATIC libs for llm, worldgen, dynamics, rendering, physics; INTERFACE for events). `register_module` entry points + umbrella INTERFACE target are still TODO (awaits further module extraction from the current `logosphere` STATIC: vision, sense, npc-ai, celestial, input, app).

Each phase is one or more commits. Tests must pass at every commit
boundary; no broken intermediate states on `main`.

## Why not runtime plugins (yet)

Runtime DLL loading (Unreal's module manager, Godot's GDExtension)
adds significant complexity: ABI stability, symbol visibility,
platform-specific loaders, plugin descriptors. None of this pays off
until there's a concrete need — typically a plugin ecosystem outside
the engine team.

Logosphere is pre-1.0, has three games in-tree, and benefits more
from API ergonomics than from runtime extensibility. Static linking +
compile-time gates gives the same "opt-in to what you need" benefit
without the machinery.

When runtime plugins become useful (e.g., editor tooling, community
mods), we can layer `.logoplugin` JSON descriptors on top of the
existing static module structure without breaking users who only need
static builds. Precedent: Godot's GDExtension is layered on top of
the compile-time module system; they coexist.

## References

- Bevy `Plugin` trait and `add_plugins` — https://bevyengine.org/learn/book/getting-started/plugins/
- Unreal Modules — https://docs.unrealengine.com/modules/
- Unreal Plugins (.uplugin) — https://docs.unrealengine.com/plugins/
- Godot Modules — https://docs.godotengine.org/en/stable/contributing/development/custom_modules_in_cpp.html
- Godot GDExtension — https://docs.godotengine.org/en/stable/tutorials/scripting/gdextension/
- O3DE Gems — https://www.docs.o3de.org/docs/user-guide/gems/
- Flecs Modules — https://www.flecs.dev/flecs/md_docs_2Modules.html
