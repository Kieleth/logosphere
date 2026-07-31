# Logosphere Documentation

## New here?

Read in order:
1. **[README](../README.md)** — what Logosphere is, who it's for, how to build it.
2. **[Getting Started](GETTING_STARTED.md)** — build your first game from empty directory to running executable.
3. **[Game Layer](GAME_LAYER.md)** — canonical reference for the game-facing API.

## Building a game

- **[Getting Started](GETTING_STARTED.md)** — step-by-step tutorial with Eden as reference
- **[Game Layer](GAME_LAYER.md)** — IApplication, ontology extension, event bus, capability rules, DynamicsParams override, particle interaction model
- **[Knowledge Layer](KNOWLEDGE_LAYER.md)** — facets, queries, the event journal, history renderers, and the ops write-back loop: how any consumer (LLM director, AI, inspector, replay) reads meaning from the world

## Engine internals

For contributors working on the engine itself:

- **[Module Architecture](MODULE_ARCHITECTURE.md)** — Core / Modules / Plugins layout, migration plan, headless build profile (`LOGOSPHERE_HEADLESS_ONLY`)
- **[Coordinate Transformation Patterns](COORDINATE_TRANSFORMATION_PATTERNS.md)** — world/local/screen space conventions
- **[Shadow System](SHADOW_SYSTEM.md)** — Metal compute shadow ray pipeline
- **[Metal](METAL.md)** — Metal-specific implementation notes
- **[Platform Reference](PLATFORM_REFERENCE.md)** — macOS platform layer

## Build profiles

Three profiles, all documented in [README.md](../README.md#platform):

- **Full engine** (`full`, default) — macOS arm64. Rendering, physics, GLFW, Metal, examples.
- **Headless physics** (`-DLOGOSPHERE_PROFILE=physics`) — any C++17 toolchain. The full engine minus GPU and windowing: render-free `Engine`, physics, locomotion, the guard suite. Gated by the `physics-linux` CI lane.
- **Headless core** (`-DLOGOSPHERE_HEADLESS_ONLY=ON`) — any C++17 toolchain. KG, capability, damage, events, ontology, game time. No GPU, no GLFW. Linux runs in CI; Windows untested but structurally compatible.

For the rules on where to add new sources/tests so they work in both profiles, see [GETTING_STARTED.md](GETTING_STARTED.md).

## Examples

- **[examples/logogenesis](../examples/logogenesis/README.md)** — conversational world creation: type a wish, an LLM authors validated KG operations, the engine grows the world
- **[examples/eden](../examples/eden/EDEN.md)** — the knowledge-garden tableau exercising the KG and capability stack end to end
- **[examples/logotron](../examples/logotron/LOGOTRON.md)** — light-cycle arena with an LLM Director that redesigns opponents between rounds


## Contributing

- **[CONTRIBUTING.md](../CONTRIBUTING.md)** — repo layout, build, testing, commit conventions
- **[CHANGELOG.md](../CHANGELOG.md)** — user-facing changes; add to `[Unreleased]` when you merge
- **[CODE_OF_CONDUCT.md](../CODE_OF_CONDUCT.md)** — Contributor Covenant v2.1
- **[RELEASING.md](RELEASING.md)** — how to cut a new release (human-facing)
- **[LICENSE.md](../LICENSE.md)** — Logosphere License 1.0 (source-available; 5% royalty above US$100K lifetime gross per product)

## Canonical examples in the test suite

Each pattern is demonstrated by a dedicated test executable.

| Pattern | Test |
|---|---|
| Runtime ontology extension | `tests/test_ontology_extension.cpp` |
| Custom DynamicsParams | `tests/test_dynamics_override.cpp` |
| Body plan helpers | `tests/test_body_plan.cpp` |
| Capability aggregation + response rules | `tests/test_capability_system.cpp` |
| Damage → capability pipeline | `tests/test_damage_pipeline.cpp` |
| Event bus subscription | `tests/test_damage_events.cpp` |
| Event journal (ring, cursors, collect_since) | `tests/test_event_log.cpp` |
| KG query algebra + facets | `tests/test_kg_query.cpp` |
| Journal renderers (compact, state deltas) | `tests/test_journal_render.cpp` |
| KG relation events | `tests/test_relation_events.cpp` |
| KG setProperty events | `tests/test_kg_setproperty_events.cpp` |
| GameTime singleton | `tests/test_game_time.cpp` |
