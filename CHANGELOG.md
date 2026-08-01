# Changelog

All notable changes to Logosphere are documented here. The format
follows [Keep a Changelog](https://keepachangelog.com/); versions
follow [Semantic Versioning](https://semver.org) on a 0.x line
(minor versions may break public API until 1.0).

## [Unreleased]

### Added
- Shadow acceleration backend seam: `GPURasterizer::shadow_accel_backend()`
  reports whether shadow rays trace a driver-owned structure (`HardwareRT`) or
  engine-built CPU trees (`SoftwareBVH`). Anything building acceleration data
  consults it instead of assuming. Override with
  `LOGOSPHERE_SHADOW_ACCEL=hardware|software`. Ports that add DXR or Vulkan RT
  implement `HardwareRT` and the CPU trees go dormant with no render-pipeline
  changes. See `docs/PORTING_SHADOWS.md`.
- `test_shadow_accel_backend`: guards the seam by contract rather than by pixel
  diff, asserting the CPU trees are dormant under `HardwareRT`, built under
  `SoftwareBVH`, and that the scene is actually lit. Includes an A-vs-A noise
  floor, without which this engine's equal-depth nondeterminism reads as signal.

### Changed
- The CPU shadow BVHs (`TriangleBVH`, `EntityBVH`) are no longer built when the
  platform traces a hardware acceleration structure. They were rebuilt and
  uploaded every frame while `trace_shadows_deterministic` traced the driver's
  structure and never bound them: 2.16 ms of a 21.7 ms Eden frame, and up to
  97 ms on a single frame in a spawning scene. `prep_bvh` is now 0.00 ms under
  `HardwareRT` and unchanged under `SoftwareBVH`. Unaffected:
  `ParticleSystem::shadow_bvh_`, a different BVH over particles that physics
  and animation query.

### Fixed
- Documented (not yet repaired) that the `SoftwareBVH` shadow fallback renders
  no lighting: its output is byte-identical to the same scene unlit. No
  supported target reaches it, which is why it went unnoticed. A port to
  hardware without ray tracing must fix it first.

### Added
- Logogenesis menagerie: `SerpentSeed` (garden snake / python /
  coral, length and scale colors), `FallenTreeSeed` (trunk / log /
  branch / twigs), and `TotemSeed` (stacked carved wood) join the
  wish grammar, materializing through the engine's existing snake,
  fallen-tree, and totem generators.

### Changed
- Logogenesis persona: the creator is now playful and theatrical.
  Wishes beyond the vocabulary get a decline with flourish plus the
  two or three nearest things it CAN do; questions get thoughts-only
  answers that read the world snapshot. Declines and answers send
  zero ops by contract (locked by AT).

### Fixed
- A bare `cmake -S . -B build` (the README configure line) produced
  an unoptimized engine: no build type meant no `-O` flags, and the
  renderer ran ~8x slower (Eden at 6 FPS instead of ~50). The build
  now defaults to Release when no `CMAKE_BUILD_TYPE` is given;
  explicit build types and multi-config generators are untouched.

### Added
- Isometric camera orbit: `CameraSystem::set_view_azimuth(radians)`
  rotates the isometric view around world +Z (clockwise-positive,
  compass convention; 0 is the classic view, bit-identical to
  before). The whole chain honors the angle: projection and depth,
  mouse picking (both inverse transforms), frustum-culling probe
  directions, the compass widget needle, camera follow offset, and
  GPU temporal shadow reprojection (history invalidates while
  orbiting). Games drive the animation; the engine provides the
  parameter.
- Logogenesis: `OrbitSeed` joins the creative vocabulary. "Orbit
  around the scene" swings the camera through a smoothstep-eased
  revolution (revolutions 0.25-3, duration 2-60 s) and lands exactly
  on its final bearing; the world never moves, only the eye.
- CMake install/export: `cmake --install` now ships the headless
  core, and external projects consume it with
  `find_package(logosphere 0.2)` + `target_link_libraries(app
  PRIVATE logosphere::core)`. Curated headers install to
  `include/logosphere/`; the internal closure they still depend on
  installs under `include/logosphere/internal/` (no stability
  promise there). `examples/consumer-smoke/` is the reference
  external consumer and runs in CI against a fresh install. The
  rendering / physics stack is not installable yet; games needing
  it keep building in-tree.
- High-energy impact energy-budget AT (`test_strata_earth_impact`):
  drops an 8x-mass boulder and audits total mechanical energy
  (kinetic + potential) of boulder + ground every frame. Measured
  today: the deep-penetration impact frame creates 1.19 MJ of
  solver energy (position correction + capped bias velocity across
  many heavy contacts) against a 29 MJ budget; free fall and the
  ballistic ejecta phase conserve energy cleanly. The AT ratchets
  the current scale so escalation fails loudly while the
  dissipation-only contract lands (tracked in issue #5).
- The ontology regeneration toolchain is now vendored:
  `scripts/cppgen/` carries the maintainer-authored LinkML C++
  generator, invoked via `scripts/gen_cpp_header.py`, with
  dependencies declared in `environment.yml`. Contributors can edit
  schema YAML and regenerate the committed sources reproducibly
  (verified byte-identical from a clean environment).
- `PixelBuffer::sync_debug_from_native()`: pulls the native BGRA
  framebuffer into the `EnhancedPixel` debug buffer so tests can
  inspect rendered pixels after a GPU render pass.

### Changed
- The `DEBUG_BUILD` compile definition is gone from every target
  (it was defined `PUBLIC` in all configurations, so release builds
  and downstream consumers carried debug paths). The pixel debug
  buffer is now a runtime opt-in via
  `PixelBuffer::set_debug_mode(true)` with lazy allocation; builds
  no longer pay for it unless a test enables it.

## [0.2.0] - 2026-07-30

First public release. Everything below describes the engine as it
ships today.

### The engine

- **Particle-first world model.** Walls, creatures, trees, terrain,
  fire: all particles with mass, friction, contacts, and
  constraints. Every particle is a node in a queryable knowledge
  graph. The world turtle (an absolute floor at z = 0) is the only
  immovable thing.
- **Software rasterization + Metal compute.** No OpenGL, Vulkan, or
  DirectX: a software rasterizer writes a direct framebuffer, with
  Metal compute shaders for shadow rays, soft shadows, SSAO, SSGI,
  and DDGI probes.
- **Physics V4.** Sequential-impulse solver with SAT face-clipping
  manifolds, speculative contacts, momentum-based sleep/wake, a
  speed-capped Baumgarte push-out, gluon constraint family
  (nail / organic / angular drives) with cluster-aware structural
  damping, and an absolute turtle boundary.
- **Knowledge graph + ontology.** LinkML-defined type system with
  generated C++ registries, runtime extension, schema-validated
  KG operations (the LLM-facing creation grammar), typed event
  journal with reader cursors, and a query algebra with prompt-ready
  renderers.
- **Humanoid locomotion.** Kinematic-root gait (stance foot pinned,
  hips derived), two-bone IK in the committed yaw frame, an
  eyes-head-torso-hips yaw cascade with per-segment time constants,
  twist-step replanting, and a permanent particle write-tracer for
  causal debugging.
- **Worldgen.** Space-colonization trees with species presets and
  growth time-lapse, grass with painterly clustered distribution,
  rocks (scenery and gluon-bonded physics boulders), layered
  strata ground with a settle-based earth preset, streamed chunked
  terrain, butterflies, and full humanoid rigs.
- **Celestial system.** Sun, moons, and stars as real orbiting
  particles far enough that only their light enters the frame;
  color and emission curves keyed to the day fraction; time
  acceleration with exact-hour arrival.
- **LLM integration.** Engine-side HTTP client (Anthropic, OpenAI,
  local servers) with prompt caching support, plus the KG-ops
  grammar that lets a model create and mutate world state under
  schema validation.
- **Three example games.** Logogenesis (conversational world
  creation), Eden (knowledge-garden tableau), Logotron (light-cycle
  arena with an LLM director).

### Known issues

- Small blockers close to the ground produce a sub-pixel penumbra
  kernel, so their shadow edges render hard instead of soft
  (`test_shadow_penumbra_softness` documents the collapse and
  ratchets it from regressing further).
- No CMake install/export surface yet: Logosphere builds in-tree and
  games live in `examples/`; `find_package(logosphere)` consumption
  is the first post-release packaging milestone.

- A heavy boulder impact on layered ground can ripple outward into
  an oversized explosion of tiles. Physics work in progress; the
  crater contract (local splash, far field still, bedrock intact)
  is enforced by test at moderate energies.
- GPU frame stalls during BVH rebuilds on chunk streaming under
  very high particle counts.
- The ontology regeneration toolchain (LinkML C++ generator) is not
  yet published; generated sources are committed, so regeneration
  is only needed when editing schemas.
- Windows builds of the headless core are structurally supported
  but untested.
