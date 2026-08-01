cmake_minimum_required(VERSION 3.10)
project(logosphere)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Generate include/logosphere/build_info.h with the current git SHA,
# tag-describe, and configure timestamp. Needed by the telemetry /
# session-recording layer so saved artifacts can be correlated with
# the code that produced them. See cmake/build_info.cmake.
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(${CMAKE_SOURCE_DIR}/cmake/build_info.cmake)

# ----------------------------------------------------------------------
# Build profiles
# ----------------------------------------------------------------------
# LOGOSPHERE_PROFILE selects how much of the engine to build:
#   full    — everything: rendering, Metal, GLFW, platform, examples,
#             all tests. The macOS dev workflow. (default)
#   physics — logosphere_core + physics + dynamics + worldgen and their
#             smoke tests. No rendering, no GLFW, no Metal, no
#             Objective-C++. Builds on any C++17 toolchain; this is the
#             Linux physics CI lane.
#   core    — logosphere_core and the pure-C++ headless tests only.
#             The cheapest CI smoke.
set(LOGOSPHERE_PROFILE "full" CACHE STRING
    "Build profile: full | physics | core")
set_property(CACHE LOGOSPHERE_PROFILE PROPERTY STRINGS full physics core)

# Back-compat alias: existing CI/docs pass -DLOGOSPHERE_HEADLESS_ONLY=ON,
# which maps to the `core` profile.
option(LOGOSPHERE_HEADLESS_ONLY
    "(deprecated) alias for -DLOGOSPHERE_PROFILE=core"
    OFF)
if(LOGOSPHERE_HEADLESS_ONLY)
    set(LOGOSPHERE_PROFILE "core")
endif()

set(LOGOSPHERE_FULL OFF)
set(LOGOSPHERE_PHYSICS_HEADLESS OFF)
set(LOGOSPHERE_CORE_ONLY OFF)
if(LOGOSPHERE_PROFILE STREQUAL "full")
    set(LOGOSPHERE_FULL ON)
elseif(LOGOSPHERE_PROFILE STREQUAL "physics")
    set(LOGOSPHERE_PHYSICS_HEADLESS ON)
elseif(LOGOSPHERE_PROFILE STREQUAL "core")
    set(LOGOSPHERE_CORE_ONLY ON)
else()
    message(FATAL_ERROR "Unknown LOGOSPHERE_PROFILE '${LOGOSPHERE_PROFILE}' (use full | physics | core)")
endif()
message(STATUS "Logosphere build profile: ${LOGOSPHERE_PROFILE}")

# Find required packages (only the full engine needs a windowing stack)
if(LOGOSPHERE_FULL)
    find_package(glfw3 REQUIRED)
endif()

# ============================================================================
# logosphere_core — headless-buildable subset of the engine
# ============================================================================
# Pure-C++ static lib with the pieces that do NOT depend on rendering,
# physics, GLFW, Metal, Objective-C++, platform code, or worldgen.
# Suitable for Linux and (eventually) Windows CI. The full `logosphere`
# lib below supersets this for macOS dev workflows.
#
# Exclusions (to maintain the invariant): kg_particle_store.cpp
# (particle.h), damage_particle_bridge.cpp (spatial/entity_spatial.h),
# time_system.cpp (physics_flags.h), anything under rendering/, physics,
# platform/, worldgen/, animation/, npc-ai/, scene/, ui/, etc.
set(LOGOSPHERE_CORE_SOURCES
    # Tiered performance instrumentation (counters / phase timers / deep
    # probes). Pure C++, no rendering or physics dependency, so any caller
    # can instrument. Methodology: docs/PERFORMANCE_RESEARCH.md
    src/core/telemetry.cpp
    # Tiny shared utility — global BVH-rebuild tick. Lives in core so any
    # caller (ParticleSystem, lighting, headless tests) can resolve the
    # symbol without dragging the rest of LightingPrimitives.
    src/core/bvh_frame_counter.cpp
    # Perlin noise (logosphere::noise::perlin_noise_2d). Used by
    # ParticleSystem::create_tile_grid and worldgen. Header-light, no
    # Engine deps, lives in core so headless tests link cleanly.
    src/worldgen/noise.cpp
    # Knowledge graph (particle-free since kg_particle_store.cpp split)
    src/kg/kg_module.cpp
    src/kg/kg_core.cpp
    src/kg/ontology_serialize.cpp
    src/kg/kg_query.cpp
    src/kg/kg_ops_parse.cpp
    src/kg/kg_ops_apply.cpp
    src/kg/ontology_validator.cpp
    # Generated ontology registry (pure data)
    src/generated/logosphere_ontology_registry.cpp
    # Capability system (engine aggregation + dynamics params + rules)
    src/capability/capability_profile.cpp
    src/capability/dynamics_params.cpp
    src/capability/trigger_registry.cpp
    src/capability/effect_registry.cpp
    src/capability/body_plan.cpp
    # Damage system (particle-free since damage_particle_bridge.cpp split)
    src/damage/damage_system.cpp
    # Central time authority (no physics coupling)
    src/core/game_time.cpp
    # Particle geometry — pure math, no rendering deps after
    # optimization_flags.h → shadow_sampling_config.h split.
    src/particle_geometry_v2.cpp
    src/flat_particle_geometry.cpp
    src/triangle_cube_geometry.cpp
    src/sphere_geometry.cpp
    src/particle_shape_methods.cpp
    # Narrow-phase collision (AABB SAT + sphere-sphere + sphere-box)
    src/core/narrow_phase.cpp
    # Projection system — pure math, needed by the iso-camera test.
    src/projection_system.cpp
    # Telemetry: Session + Instrument — disk-backed append-only
    # JSONL, tagged with build_info.h. Games register their own
    # Instrument subclasses. Pure C++17; no Metal / GLFW / rendering.
    src/telemetry/session.cpp
    # Vision-cone LOS occlusion mask generator. Pure ray-vs-OBB
    # math; the GPU side (Pass-4 Metal kernel) consumes the output.
    src/rendering/vision_cone_occlusion.cpp
    # Vision memory grid — world-space "what the viewer just saw"
    # buffer. CPU update (decay + mark); the GPU side samples per
    # pixel in the Pass-4 cone kernel and blends with live cone
    # visibility for the fading-fog-of-war effect.
    src/rendering/vision_memory.cpp
    # Per-pixel vision-cone visibility math (CPU spec). The Pass-4
    # Metal kernel implements the same algorithm; this CPU version
    # is what the headless tests + game-level ATs assert against.
    src/rendering/vision_cone_pixel.cpp
    # Particle store + write-tracer + anatomical joint constants +
    # physics telemetry: pure C++, and logosphere_physics / _dynamics /
    # _worldgen genuinely depend on their symbols. They live in core so
    # every downstream lib links them in strict GNU-ld archive order.
    src/core/particle_system.cpp
    src/core/particle_tracer.cpp
    src/core/joint_types.cpp
    src/core/entity_telemetry.cpp
    # Particle interaction model (KG-declared contact filtering + media);
    # headless-core safe, consulted by the solver seam in Phase 2.
    src/interaction/particle_interaction_system.cpp
)

# ============================================================================
# logosphere_events — header-only module (INTERFACE target)
# ============================================================================
# EventBus, typed channels, event log, signals. Pure headers under
# include/logosphere/events/. Acts as a named target so other modules can
# express dependencies via target_link_libraries(PUBLIC logosphere_events)
# once they are split out — today it is carried transitively through
# logosphere_core.
add_library(logosphere_events INTERFACE)
target_include_directories(logosphere_events INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(logosphere_events INTERFACE cxx_std_17)

add_library(logosphere_core STATIC ${LOGOSPHERE_CORE_SOURCES})
target_link_libraries(logosphere_core PUBLIC logosphere_events)
target_include_directories(logosphere_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
# nlohmann/json is header-only; only kg_ops_parse.cpp uses it.
target_include_directories(logosphere_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)
target_compile_features(logosphere_core PUBLIC cxx_std_17)
target_compile_definitions(logosphere_core PUBLIC DEBUG_BUILD)

# ============================================================================
# Headless test executables — always built, link only logosphere_core
# ============================================================================
# Each is its own executable with its own main(). They build on any
# standards-compliant C++17 toolchain. No engine state, no GPU, no GLFW.
enable_testing()

function(add_headless_test name)
    add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE logosphere_core)
    target_compile_features(${name} PRIVATE cxx_std_17)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_headless_test(test_event_log)
add_headless_test(test_signal)
add_headless_test(test_event_channel)
add_headless_test(test_event_bus)
add_headless_test(test_damage_events)
add_headless_test(test_capability_system)
add_headless_test(test_ontology_extension)
add_headless_test(test_ontology_serialize)
add_headless_test(test_particle_self_emissive_field)
add_headless_test(test_kg_ops_parse)
add_headless_test(test_ontology_validator)
add_headless_test(test_kg_ops_apply)
# test_mutation_playback links logosphere_core only (no Engine
# dereferencing in the tests; dummy pointers in fixtures).
add_executable(test_mutation_playback
    tests/test_mutation_playback.cpp
    src/effects/mutation_playback.cpp
)
target_link_libraries(test_mutation_playback PRIVATE logosphere_core)
target_compile_features(test_mutation_playback PRIVATE cxx_std_17)
add_headless_test(test_body_plan)
add_headless_test(test_game_time)
add_headless_test(test_kg_setproperty_events)
add_headless_test(test_relation_events)
add_headless_test(test_kg_parse_safety)
add_headless_test(test_particle_shapes)
add_headless_test(test_interaction_ontology)
add_headless_test(test_interaction_events)
add_headless_test(test_interaction_profiles)
add_headless_test(test_iso_camera_centering)
add_headless_test(test_iso_depth_ordering)
add_headless_test(test_shadow_penumbra_softness)
add_headless_test(test_bike_body_shadow)
add_headless_test(test_gbuffer_depth_encoding)
add_headless_test(test_rigid_assembly)
add_headless_test(test_ellipsoid_outward_normals)
add_headless_test(test_shadow_pose_dirty)
add_headless_test(test_vision_cone_occlusion)
add_headless_test(test_vision_memory)
add_headless_test(test_vision_cone_pixel)
add_headless_test(test_kg_query)          # Knowledge layer: query algebra + facet selection
add_headless_test(test_journal_render)    # Knowledge layer: journal history renderers

# ============================================================================
# Tic-Tac-Toe example — headless/console, links only logosphere_core
# ============================================================================
# Unlike examples/eden, examples/logotron, and examples/logogenesis (added
# later, after the profile short-circuits below, so they only build in the
# `full` profile), this example needs nothing but the KG + ontology +
# events slice of the engine, so it's registered here to build under
# core/physics/full alike.
add_subdirectory(examples/tictactoe)

add_executable(test_tictactoe
    tests/test_tictactoe.cpp
    examples/tictactoe/src/generated/tictactoe_ontology_registry.cpp
)
target_link_libraries(test_tictactoe PRIVATE logosphere_core)
target_include_directories(test_tictactoe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/tictactoe/src
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/tictactoe/src/generated
)
target_compile_features(test_tictactoe PRIVATE cxx_std_17)
add_test(NAME test_tictactoe COMMAND test_tictactoe)


# Engine acceptance tests — link against the full engine library so
# they can spin up Engine in headless mode + drive the real GPU
# render pipeline + read pixels back. The mechanism every game's
# GPU-touching ATs use. See memory project_headless_render_path.md.
function(add_engine_at name)
    add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name}
        PRIVATE
            logosphere
            "-framework Cocoa" "-framework IOKit" "-framework CoreVideo"
            "-framework CoreGraphics" "-framework AppKit"
            "-framework Foundation" "-framework QuartzCore"
            "-framework Metal" "-framework MetalKit"
            "-framework MetalPerformanceShaders"
    )
    target_include_directories(${name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_compile_features(${name} PRIVATE cxx_std_17)
    add_test(NAME ${name} COMMAND ${name} --no-head)
endfunction()

if(LOGOSPHERE_FULL)
    add_engine_at(test_engine_headless_render)
    add_engine_at(test_trail_fade_render)
    add_engine_at(test_celestial_moon)   # moon factory preset (pure config)
    add_engine_at(test_strata_earth_impact)  # earth preset + boulder crater contract
    add_engine_at(test_leg_spin_on_rotation) # Eden playtest repro: legs spin when human rotates
    add_engine_at(test_face_tracks_head)     # Eden playtest repro: eyes/ears/nose ride the head's yaw
    add_engine_at(test_leg_geometry_choreography) # Eden playtest repro: legs fold under walk/stop/turn transitions
    add_engine_at(at_ai_bike_invisible_outside_cone)
    add_engine_at(test_cinematic_pause)
    add_engine_at(test_camera_director)
    add_engine_at(test_assembly_visibility)

    # at_program_actor needs program_actor.cpp + the logotron
    # ontology (Program / body parts) registered. Built bespoke
    # rather than via add_engine_at since add_engine_at expects a
    # single source file.
    add_executable(at_program_actor
        tests/at_program_actor.cpp
        examples/logotron/src/director/program_actor.cpp
        examples/logotron/src/generated/logotron_ontology_registry.cpp
    )
    target_link_libraries(at_program_actor
        PRIVATE
            logosphere
            "-framework Cocoa" "-framework IOKit" "-framework CoreVideo"
            "-framework CoreGraphics" "-framework AppKit"
            "-framework Foundation" "-framework QuartzCore"
            "-framework Metal" "-framework MetalKit"
            "-framework MetalPerformanceShaders"
    )
    target_include_directories(at_program_actor PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/src
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/src/generated
    )
    target_compile_features(at_program_actor PRIVATE cxx_std_17)

    # at_cinematic — same pattern as at_program_actor; needs both
    # program_actor.cpp and cinematic.cpp linked in.
    add_executable(at_cinematic
        tests/at_cinematic.cpp
        examples/logotron/src/director/program_actor.cpp
        examples/logotron/src/director/cinematic.cpp
        examples/logotron/src/generated/logotron_ontology_registry.cpp
    )
    target_link_libraries(at_cinematic
        PRIVATE
            logosphere
            "-framework Cocoa" "-framework IOKit" "-framework CoreVideo"
            "-framework CoreGraphics" "-framework AppKit"
            "-framework Foundation" "-framework QuartzCore"
            "-framework Metal" "-framework MetalKit"
            "-framework MetalPerformanceShaders"
    )
    target_include_directories(at_cinematic PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/src
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/src/generated
    )
    target_compile_features(at_cinematic PRIVATE cxx_std_17)

    # at_logotron_full_game_loop — instantiates the actual
    # LogotronApplication + Engine in headless mode and drives ticks
    # against the random offline Director. Asserts post-tick KG state
    # (Arena dimensions, cycles alive, walls landing) end-to-end. The
    # AT executable rebuilds every Logotron .cpp into itself because
    # LogotronApplication's header-only class pulls them in
    # transitively; one extra heavy compile is the price of full-loop
    # ATs without splitting the class header / impl.
    add_executable(at_logotron_full_game_loop
        examples/logotron/tests/at/at_full_game_loop.cpp
        examples/logotron/src/cycle.cpp
        examples/logotron/src/arena.cpp
        examples/logotron/src/round.cpp
        examples/logotron/src/llm_plan.cpp
        examples/logotron/src/ai/head.cpp
        examples/logotron/src/ai/perception.cpp
        examples/logotron/src/ai/tactics/avoid_imminent_crash.cpp
        examples/logotron/src/ai/tactics/maintain_direction.cpp
        examples/logotron/src/ai/tactics/chase_seen_opponent.cpp
        examples/logotron/src/ai/tactics/avoid_seen_opponent.cpp
        examples/logotron/src/ai/tactics/flee_own_recent_trail.cpp
        examples/logotron/src/ai/personality.cpp
        examples/logotron/src/ai/decide.cpp
        examples/logotron/src/ai/ai_instrument.cpp
        examples/logotron/src/director/respawn_ai.cpp
        examples/logotron/src/director/director_parser.cpp
        examples/logotron/src/director/director.cpp
        examples/logotron/src/director/random_director.cpp
        examples/logotron/src/director/program_actor.cpp
        examples/logotron/src/director/cinematic.cpp
        examples/logotron/src/director/symbolic_refs.cpp
        examples/logotron/src/director/narrative_hint.cpp
        examples/logotron/src/director/playback_registrations.cpp
        examples/logotron/src/hud/speed_dashboard.cpp
        examples/logotron/src/hud/sight_occluders.cpp
        examples/logotron/src/occluders.cpp
        examples/logotron/src/generated/logotron_ontology_registry.cpp
    )
    target_link_libraries(at_logotron_full_game_loop
        PRIVATE
            logosphere
            glfw
            "-framework Cocoa" "-framework IOKit" "-framework CoreVideo"
            "-framework CoreGraphics" "-framework AppKit"
            "-framework Foundation" "-framework QuartzCore"
            "-framework Metal" "-framework MetalKit"
            "-framework MetalPerformanceShaders"
    )
    target_include_directories(at_logotron_full_game_loop PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/src
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/src/generated
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/tests/at
    )
    target_compile_features(at_logotron_full_game_loop PRIVATE cxx_std_17)

    # at_logotron_personality_swap — exercises the Director-tunable AI
    # personality contract end-to-end: write the AICycle.ai_personality
    # KG slot, drive a tick, assert the live Personality cache swapped.
    # Same plumbing as at_logotron_full_game_loop; needs every Logotron
    # source because LogotronApplication is header-only.
    add_executable(at_logotron_personality_swap
        examples/logotron/tests/at/at_personality_swap.cpp
        examples/logotron/src/cycle.cpp
        examples/logotron/src/arena.cpp
        examples/logotron/src/round.cpp
        examples/logotron/src/llm_plan.cpp
        examples/logotron/src/ai/head.cpp
        examples/logotron/src/ai/perception.cpp
        examples/logotron/src/ai/tactics/avoid_imminent_crash.cpp
        examples/logotron/src/ai/tactics/maintain_direction.cpp
        examples/logotron/src/ai/tactics/chase_seen_opponent.cpp
        examples/logotron/src/ai/tactics/avoid_seen_opponent.cpp
        examples/logotron/src/ai/tactics/flee_own_recent_trail.cpp
        examples/logotron/src/ai/personality.cpp
        examples/logotron/src/ai/decide.cpp
        examples/logotron/src/ai/ai_instrument.cpp
        examples/logotron/src/director/respawn_ai.cpp
        examples/logotron/src/director/director_parser.cpp
        examples/logotron/src/director/director.cpp
        examples/logotron/src/director/random_director.cpp
        examples/logotron/src/director/program_actor.cpp
        examples/logotron/src/director/cinematic.cpp
        examples/logotron/src/director/symbolic_refs.cpp
        examples/logotron/src/director/narrative_hint.cpp
        examples/logotron/src/director/playback_registrations.cpp
        examples/logotron/src/hud/speed_dashboard.cpp
        examples/logotron/src/hud/sight_occluders.cpp
        examples/logotron/src/occluders.cpp
        examples/logotron/src/generated/logotron_ontology_registry.cpp
    )
    target_link_libraries(at_logotron_personality_swap
        PRIVATE
            logosphere
            glfw
            "-framework Cocoa" "-framework IOKit" "-framework CoreVideo"
            "-framework CoreGraphics" "-framework AppKit"
            "-framework Foundation" "-framework QuartzCore"
            "-framework Metal" "-framework MetalKit"
            "-framework MetalPerformanceShaders"
    )
    target_include_directories(at_logotron_personality_swap PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/src
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/src/generated
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/tests/at
    )
    target_compile_features(at_logotron_personality_swap PRIVATE cxx_std_17)
endif()

# ----------------------------------------------------------------------------
# Example-game headless tests — purely pure-C++ game logic that doesn't
# touch the engine. Compiled straight against the example's sources so
# the test never needs the full game binary (which pulls in GLFW/Metal).
# ----------------------------------------------------------------------------
add_executable(test_logotron_cycle
    examples/logotron/tests/test_cycle_movement.cpp
    examples/logotron/src/cycle.cpp
)
target_include_directories(test_logotron_cycle PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/src
)
target_compile_features(test_logotron_cycle PRIVATE cxx_std_17)

# v0.4 — KG-backed cycles and trails. Needs the KG module + the
# logotron-side ontology registry (an example-local generated file)
# and the core engine ontology registry. Links against logosphere_core
# only; no GLFW, no Metal, no rendering.
add_executable(test_logotron_trails
    examples/logotron/tests/test_logotron_trails.cpp
    examples/logotron/src/cycle.cpp
    examples/logotron/src/arena.cpp
    examples/logotron/src/generated/logotron_ontology_registry.cpp
)
target_link_libraries(test_logotron_trails PRIVATE logosphere_core)
target_include_directories(test_logotron_trails PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/src
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/src/generated
)
target_compile_features(test_logotron_trails PRIVATE cxx_std_17)

# v0.5 — Collision detection. Same source set as test_logotron_trails.
add_executable(test_logotron_collision
    examples/logotron/tests/test_logotron_collision.cpp
    examples/logotron/src/cycle.cpp
    examples/logotron/src/arena.cpp
    examples/logotron/src/generated/logotron_ontology_registry.cpp
)
target_link_libraries(test_logotron_collision PRIVATE logosphere_core)
target_include_directories(test_logotron_collision PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/src
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/src/generated
)
target_compile_features(test_logotron_collision PRIVATE cxx_std_17)

# v0.6 — Round status: pure function of two cycles' KG state.
add_executable(test_logotron_round
    examples/logotron/tests/test_logotron_round.cpp
    examples/logotron/src/cycle.cpp
    examples/logotron/src/arena.cpp
    examples/logotron/src/round.cpp
    examples/logotron/src/generated/logotron_ontology_registry.cpp
)
target_link_libraries(test_logotron_round PRIVATE logosphere_core)
target_include_directories(test_logotron_round PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/src
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/src/generated
)
target_compile_features(test_logotron_round PRIVATE cxx_std_17)

# v0.9 — Weirden Director: KG-side primitives (respawn_ai today,
# mutations + prompt + parser as they land). Headless: links only
# logosphere_core. The director tools never need rendering.
add_executable(test_logotron_director
    examples/logotron/tests/test_logotron_director.cpp
    examples/logotron/src/cycle.cpp
    examples/logotron/src/arena.cpp
    examples/logotron/src/director/respawn_ai.cpp
    examples/logotron/src/director/director_parser.cpp
    examples/logotron/src/director/director.cpp
    examples/logotron/src/director/random_director.cpp
    examples/logotron/src/director/symbolic_refs.cpp
    examples/logotron/src/director/narrative_hint.cpp
    # NOTE: program_actor.cpp is intentionally NOT included here.
    # It depends on the full Engine + worldgen (HumanoidGenerator),
    # which test_logotron_director doesn't link. program_actor lives
    # in the logotron binary build (examples/logotron/CMakeLists.txt)
    # and gets coverage via the engine-AT at_program_actor (linked
    # against the full logosphere lib).
    examples/logotron/src/llm_plan.cpp
    examples/logotron/src/generated/logotron_ontology_registry.cpp
)
target_link_libraries(test_logotron_director PRIVATE logosphere_core)
target_include_directories(test_logotron_director PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/src
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/src/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)
target_compile_features(test_logotron_director PRIVATE cxx_std_17)

# Pure-logic logotron test for the SymbolicRefs helper.
add_executable(test_symbolic_refs
    examples/logotron/tests/test_symbolic_refs.cpp
    examples/logotron/src/director/symbolic_refs.cpp
)
target_link_libraries(test_symbolic_refs PRIVATE logosphere_core)
target_include_directories(test_symbolic_refs PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/src
)
target_compile_features(test_symbolic_refs PRIVATE cxx_std_17)

# Pure-logic logotron test for the narrative_hint formatter.
add_executable(test_narrative_hint
    examples/logotron/tests/test_narrative_hint.cpp
    examples/logotron/src/director/narrative_hint.cpp
)
target_link_libraries(test_narrative_hint PRIVATE logosphere_core)
target_include_directories(test_narrative_hint PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/src
)
target_compile_features(test_narrative_hint PRIVATE cxx_std_17)

# v0.7 — AI driver: tactics + head rotation + perception contract.
# Headless: links only logosphere_core + cycle.cpp / arena.cpp /
# ai/*.cpp. Tactic code has zero dependency on rendering or Metal.
add_executable(test_logotron_ai
    examples/logotron/tests/test_logotron_ai.cpp
    examples/logotron/src/cycle.cpp
    examples/logotron/src/arena.cpp
    examples/logotron/src/ai/head.cpp
    examples/logotron/src/ai/perception.cpp
    examples/logotron/src/ai/tactics/avoid_imminent_crash.cpp
    examples/logotron/src/ai/tactics/chase_seen_opponent.cpp
    examples/logotron/src/ai/tactics/avoid_seen_opponent.cpp
    examples/logotron/src/ai/tactics/flee_own_recent_trail.cpp
    examples/logotron/src/ai/tactics/maintain_direction.cpp
    examples/logotron/src/ai/personality.cpp
    examples/logotron/src/ai/decide.cpp
    examples/logotron/src/occluders.cpp
    examples/logotron/src/generated/logotron_ontology_registry.cpp
)
target_link_libraries(test_logotron_ai PRIVATE logosphere_core)
target_include_directories(test_logotron_ai PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/src
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/src/generated
)
target_compile_features(test_logotron_ai PRIVATE cxx_std_17)

# ============================================================================
# Logotron Acceptance Tests (ATs) — feature-level, headless by default,
# visualization opt-in via LOGOTRON_AT_VISUAL=1.
# ============================================================================
# Each AT is a single .cpp under examples/logotron/tests/at/. Headless
# assertions exercise the same data path the running game uses (engine
# sources compiled in directly — no dependency on logosphere_rendering or
# Metal). Visual mode shells out to ./logotron/logotron so the human
# can confirm what the headless asserts can't (anything purely shader-
# side). Add new ATs by calling `add_logotron_at(<name>)`.
function(add_logotron_at name)
    add_executable(at_logotron_${name}
        examples/logotron/tests/at/at_${name}.cpp
        examples/logotron/src/cycle.cpp
        examples/logotron/src/generated/logotron_ontology_registry.cpp
    )
    target_link_libraries(at_logotron_${name} PRIVATE logosphere_core)
    target_include_directories(at_logotron_${name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/src
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/src/generated
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/tests/at
    )
    target_compile_features(at_logotron_${name} PRIVATE cxx_std_17)
endfunction()

add_logotron_at(vision_memory_excludes_dynamic)

# Director apply-pipeline AT — pure-C++, no Engine, no LLM server.
# Brings the Director sources in directly (they're not in
# logosphere_core because they live under examples/logotron/).
add_executable(at_logotron_director_random_apply
    examples/logotron/tests/at/at_director_random_apply.cpp
    examples/logotron/src/director/director.cpp
    examples/logotron/src/director/director_parser.cpp
    examples/logotron/src/director/random_director.cpp
    examples/logotron/src/director/symbolic_refs.cpp
    examples/logotron/src/generated/logotron_ontology_registry.cpp
)
target_link_libraries(at_logotron_director_random_apply PRIVATE logosphere_core)
target_include_directories(at_logotron_director_random_apply PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/src
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/src/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/tests/at
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)
target_compile_features(at_logotron_director_random_apply PRIVATE cxx_std_17)

# Director ↔ MLX smoke AT — same Director sources as the random AT,
# plus LLMSystemHTTP for the live HTTP round trip. Auto-skips with
# PASS when no server is reachable so it's safe to ship in CI.
# NOTE: links logosphere_llm, defined below the headless short-circuit,
# so this target only exists in the full profile.
if(LOGOSPHERE_FULL)
# LLM JSON extraction unit test — the double-encoding regression
# (Anthropic "text" values must be decoded, not raw slices).
add_executable(test_llm_json_extract tests/test_llm_json_extract.cpp)
target_link_libraries(test_llm_json_extract PRIVATE
    logosphere_core logosphere_llm)
target_include_directories(test_llm_json_extract PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(test_llm_json_extract PRIVATE cxx_std_17)

add_executable(at_logotron_director_mlx_smoke
    examples/logotron/tests/at/at_director_mlx_smoke.cpp
    examples/logotron/src/director/director.cpp
    examples/logotron/src/director/director_parser.cpp
    examples/logotron/src/director/symbolic_refs.cpp
    examples/logotron/src/generated/logotron_ontology_registry.cpp
)
target_link_libraries(at_logotron_director_mlx_smoke PRIVATE
    logosphere_core
    logosphere_llm
)
target_include_directories(at_logotron_director_mlx_smoke PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/src
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/src/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/tests/at
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)
target_compile_features(at_logotron_director_mlx_smoke PRIVATE cxx_std_17)
endif()  # LOGOSPHERE_FULL (llm-linked smoke AT)

# Wall visibility AT — pure-C++ assertion on the Particle properties
# set_wall_color / set_wall_geometry produce. Catches the
# "is_light_source flag missing" class of bug that turns walls dark
# in the live game without a pixel-level RT test.
add_executable(at_logotron_wall_visibility
    examples/logotron/tests/at/at_wall_visibility.cpp
    examples/logotron/src/generated/logotron_ontology_registry.cpp
)
target_link_libraries(at_logotron_wall_visibility PRIVATE logosphere_core)
target_include_directories(at_logotron_wall_visibility PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/src
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/src/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/logotron/tests/at
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)
target_compile_features(at_logotron_wall_visibility PRIVATE cxx_std_17)

# ============================================================================
# Short-circuit for LOGOSPHERE_HEADLESS_ONLY=ON builds (Linux / Windows CI)
# ============================================================================
# Everything below this point is the full macOS engine (rendering, physics,
# platform, worldgen, Metal shaders, examples, and their tests). Skipped
# entirely when HEADLESS is set.
if(LOGOSPHERE_CORE_ONLY)
    return()
endif()

# ============================================================================
# Headless-physics subset — core + physics + dynamics + worldgen
# ============================================================================
# Everything in this section is pure C++17 with no rendering, GLFW, Metal,
# or platform dependency. It builds in BOTH the physics and full profiles;
# the physics profile returns at the end of the section. The umbrella
# `logosphere` target below links these libs in the full profile.

# ---- logosphere_worldgen — generators, chunks, strata ----
set(LOGOSPHERE_WORLDGEN_SOURCES
    src/worldgen/chunk_system.cpp
    src/worldgen/async_chunk_loader.cpp
    src/worldgen/floor_generator.cpp
    src/worldgen/organic_floor_generator.cpp
    src/worldgen/strata_generator.cpp
    src/worldgen/strata_floor_generator.cpp
    # noise.cpp moved to logosphere_core in B26 — pulled in transitively.
    src/worldgen/tree_generator.cpp
    src/worldgen/physics_tree_generator.cpp
    src/worldgen/physics_rock_generator.cpp
    src/worldgen/organic_generator.cpp
    src/worldgen/space_colonization.cpp
    src/worldgen/organic_spec.cpp
    src/worldgen/attractor_shapes.cpp
    src/worldgen/grass_patch_spec.cpp
    src/worldgen/organic_growth_simulator.cpp
    src/worldgen/humanoid_generator.cpp
    src/worldgen/snake_generator.cpp
    src/worldgen/butterfly_generator.cpp
    src/worldgen/totem_generator.cpp
    src/worldgen/rock_generator.cpp
    src/worldgen/fallen_tree_generator.cpp
    src/worldgen/scene_chunk_generator.cpp
    src/worldgen/worldgen_system.cpp
    src/worldgen/entity_generator.cpp
)
add_library(logosphere_worldgen STATIC ${LOGOSPHERE_WORLDGEN_SOURCES})
target_link_libraries(logosphere_worldgen PUBLIC logosphere_core)
target_include_directories(logosphere_worldgen PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(logosphere_worldgen PUBLIC cxx_std_17)
target_compile_definitions(logosphere_worldgen PUBLIC DEBUG_BUILD HAS_LLAMA)

# ---- logosphere_dynamics — animation, FK, locomotion, kinematic root ----
set(LOGOSPHERE_DYNAMICS_SOURCES
    src/core/particle_dynamics_system.cpp
    src/animation/animation_types.cpp
    src/animation/animation_controller.cpp
    src/animation/humanoid_locomotion.cpp  # Refactor B0: scaffolding for humanoid policy split
    src/animation/serpent_locomotion.cpp   # Refactor B19: serpent module split
    src/animation/butterfly_flight.cpp     # Refactor B20: butterfly module split
)
add_library(logosphere_dynamics STATIC ${LOGOSPHERE_DYNAMICS_SOURCES})
target_link_libraries(logosphere_dynamics PUBLIC logosphere_core)
target_include_directories(logosphere_dynamics PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(logosphere_dynamics PUBLIC cxx_std_17)
target_compile_definitions(logosphere_dynamics PUBLIC DEBUG_BUILD HAS_LLAMA)

# ---- logosphere_physics — XPBD solver, gluons, narrow-phase, BVH ----
set(LOGOSPHERE_PHYSICS_SOURCES
    src/core/physics_system_v4.cpp
    src/core/gluon_particle.cpp
    src/bvh.cpp
)
add_library(logosphere_physics STATIC ${LOGOSPHERE_PHYSICS_SOURCES})
target_link_libraries(logosphere_physics PUBLIC logosphere_core)
target_include_directories(logosphere_physics PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(logosphere_physics PUBLIC cxx_std_17)
target_compile_definitions(logosphere_physics PUBLIC DEBUG_BUILD HAS_LLAMA)

# Locomotion smoke test — exercises HumanoidLocomotion / PhysicsSystem /
# ParticleDynamicsSystem via the *_headless initialize variants. No
# Engine, no GLFW window, no Metal device. Validates the B22-B24
# decoupling stays alive.
add_executable(test_humanoid_headless
    tests/test_humanoid_headless.cpp
    # (particle_system / tracer / joint_types / entity_telemetry now
    #  live in logosphere_core — linked, not recompiled here)
)
target_link_libraries(test_humanoid_headless PRIVATE
    logosphere_dynamics logosphere_physics logosphere_core)
target_include_directories(test_humanoid_headless PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(test_humanoid_headless PRIVATE cxx_std_17)

# Phase 4b C2 — pin-gluon lifecycle round-trip. Same headless setup as
# test_humanoid_headless. Spawns a stub humanoid, opts in via
# enable_physics_drive_legs, walks → stops, asserts anchor + pin gluon
# spawn at heel-strike and tear down on walk→idle.
add_executable(test_pin_gluon_lifecycle
    tests/test_pin_gluon_lifecycle.cpp
)
target_link_libraries(test_pin_gluon_lifecycle PRIVATE
    logosphere_dynamics logosphere_physics logosphere_core)
target_include_directories(test_pin_gluon_lifecycle PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(test_pin_gluon_lifecycle PRIVATE cxx_std_17)

# (physics-profile return moved below: the umbrella `logosphere`,
#  llm, rendering, and the guard runner build in BOTH full and physics.)


# Logosphere source files (temporarily includes everything)
set(LOGOSPHERE_SOURCES
    src/effects/assembly_visibility.cpp
    src/effects/mutation_playback.cpp
    # NOTE: particle geometry .cpp files live in logosphere_core (see top of
    # this file). Carrying them here would cause duplicate-symbol link
    # conflicts now that logosphere transitively links logosphere_core.
    src/shadow_geometry_cache.cpp  # Pre-cached geometry templates for shadow triangles
    src/scanline_uv_cache.cpp
    src/surface_cache.cpp      # Scanline UV coherence optimization
    # NOTE: rendering sources live in logosphere_rendering (see below).
    # NOTE: physics_system_v4 / narrow_phase / gluon_particle / bvh live in
    # logosphere_physics (see below). narrow_phase is also in logosphere_core
    # for headless shape tests.
    src/core/light_system.cpp
    # NOTE: particle_dynamics_system.cpp lives in logosphere_dynamics (see below).
    # NOTE: capability/*.cpp live in logosphere_core (see top of file).
    # NOTE: animation/*.cpp lives in logosphere_dynamics (see below).
    src/core/time_system.cpp
    # NOTE: game_time.cpp lives in logosphere_core.
    src/lighting_primitives.cpp    # Reusable lighting calculation primitives
    src/pixel_lighting_strategy.cpp # Per-pixel accurate lighting (the only mode now)
    # NOTE: bvh.cpp lives in logosphere_physics (see below).
    src/sense_system.cpp           # Creature perception via BVH raycasting
    # rendering/triangle_bvh.cpp + rendering/entity_bvh.cpp live in logosphere_rendering.
    src/core/input_system.cpp
    src/vision_system.cpp
    # Engine adapter for SerpentLocomotion — keeps RenderSystem +
    # InputSystem references out of logosphere_dynamics (B26).
    src/animation/serpent_locomotion_engine.cpp
    src/core/engine.cpp
    src/assembly/rigid_assembly.cpp  # Rigid multi-particle entity primitive
    # LLM sources now live in the logosphere_llm STATIC target (see below).
    src/core/telemetry.cpp
    # rendering/resolution_manager.cpp lives in logosphere_rendering.
    src/debug_overlay.cpp
    src/scene_manager.cpp
    src/game_loop.cpp
    src/key_mapper.cpp
    src/main_key_handler.cpp
    # Diagnostics (humanoid integrity, deep probe, particle tracer)
    src/core/humanoid_integrity_monitor.cpp
    src/core/deep_probe.cpp
    # NOTE: worldgen sources live in the logosphere_worldgen STATIC target (see below).
    # NOTE: capability/body_plan.cpp lives in logosphere_core.
    # NPC AI (GOAP, pathfinding, action executors)
    src/npc-ai/goap_system.cpp
    src/npc-ai/pathfinding_system.cpp
    src/npc-ai/executor_registry.cpp
    src/npc-ai/goap_plan_executor.cpp
    src/npc-ai/executors/pursue_executor.cpp
    src/npc-ai/executors/eat_executor.cpp
    src/npc-ai/executors/scan_executor.cpp
    src/npc-ai/executors/investigate_smell_executor.cpp
    src/npc-ai/executors/escape_block_executor.cpp
    src/npc-ai/executors/give_up_executor.cpp
    src/npc-ai/executors/grab_prey_executor.cpp
    src/celestial/celestial_system.cpp
    src/test_projections.cpp
    src/test_overlay.cpp
    src/test_suite_coordinator.cpp
    src/test_context.cpp
    src/unified_test_runner.cpp
    tests/test_stubs.cpp
    tests/test_coordinate_system.cpp
    tests/test_pixel_rasterization.cpp
    tests/test_basic_cube.cpp
    tests/test_basic_cube_projections.cpp
    tests/test_uv_coordinates.cpp
    tests/visual_test_top.cpp
    # Platform abstraction layer (new)
    src/platform/platform_macos.mm
    # Platform layer (old - temporary during migration)
    src/platform/platform_layer.cpp
    src/platform/macos_platform.mm
    # UI System (new)
    src/ui/ui_system.cpp
    src/ui/widget.cpp
    src/ui/widgets.cpp
    src/ui/chat_window.cpp
    src/ui/text_window.cpp
    # Camera System (new)
    src/core/camera_system.cpp
    src/core/camera_director.cpp
    # Camera Controller (new)
    src/camera_controller.cpp
    # NOTE: projection_system.cpp lives in logosphere_core (see top of file).
    # Entity System (new)
    src/entity_system.cpp
    # Player Controller (mouse look, WASD, abilities)
    src/player_controller.cpp
    # Entity Manager (entity type polymorphism)
    src/entity_manager.cpp
    src/entities/tree_entity.cpp
    src/entities/simple_entity.cpp
    src/entities/rock_entity.cpp
    # Knowledge Graph module
    src/kg/kg_module.cpp
    src/kg/kg_core.cpp
    src/kg/kg_particle_store.cpp  # Particle-coupled subset (split for headless-core builds)
    # Generated ontology registry
    src/generated/logosphere_ontology_registry.cpp
    # Spatial query module
    src/spatial/entity_spatial.cpp
    # Damage system
    src/damage/damage_system.cpp
    src/damage/damage_particle_bridge.cpp  # apply_to_particle → EntitySpatialQuery bridge
    # Test framework
    tests/test_framework.cpp
    # Test files (NOT test_main.cpp - that goes in test executable only)
    # Legacy CPU-lighting/tile tests retired 2026-07-15 (dead CPU paths; see CHANGELOG)
    tests/test_shadow_movement_direction.cpp  # Test shadow movement with particle movement
    tests/test_surface_ray_tracing.cpp  # Test surface-based ray tracing vs sphere approximation
    tests/test_eden_shadows.cpp  # Test Eden-like shadow scenario
    tests/test_shadow_diagonal.cpp  # Debug diagonal shadow pattern
    # REMOVED: test_uv_mapping_direction.cpp - old tangent space system
    # REMOVED: test_tangent_space_generation.cpp - old tangent space system
    tests/test_rotated_cube.cpp  # Test 3D rotation of cubes
    tests/test_triangle_shadow_artifacts.cpp  # Test triangle shadow distortions
    tests/test_shadow_ray_blocking.cpp  # Test shadow ray blocking
    tests/test_small_particle_shadow.cpp  # Test Eva-sized particle shadows
    tests/test_bvh_shadow_accuracy.cpp  # Test BVH shadow accuracy
    tests/test_wall_rendering_issue.cpp  # Test wall rendering issue
    tests/test_pixel_shadow_visual.cpp  # Test pixel shadow visual
    tests/test_bvh.cpp             # Test BVH acceleration structure
    # TEMPORARILY DISABLED - use old grid API:
    # tests/test_font_rendering.cpp
    # tests/test_game_integration.cpp
    # tests/test_camera_offset.cpp
    # tests/test_light_vision.cpp
    # tests/test_lighting_physics.cpp
    # tests/test_wall_shadow_scenario.cpp
    # tests/test_lighting_complex_scenarios.cpp
    # tests/test_lighting_reflection.cpp
    # tests/test_basic_scenarios.cpp
    # tests/test_shadow_casting.cpp
    # tests/test_surface_light_grid_integration.cpp
    # tests/test_cube_around_light.cpp
    # tests/test_brightness_rendering.cpp
    # tests/test_brightness_normalization.cpp
    # tests/test_dynamic_lighting.cpp
    # tests/test_eden_headless_rendering.cpp
    # tests/test_light_distance_falloff.cpp
    # tests/test_culling_observer.cpp
    # tests/run_unit_tests.cpp
    # tests/visual_test_scenarios.cpp
    # tests/test_keymapper.cpp
    # tests/test_hexagon_grid.cpp
    # tests/test_wasd_compass_alignment.cpp
    # tests/test_input_target.cpp
    # tests/test_kg_inspector.cpp
    # tests/test_particle_system.cpp
    # tests/test_physics_system.cpp
    # tests/test_render_boundaries.cpp
    # tests/test_vision_fov.cpp
    # tests/test_vision_input_target.cpp
    # tests/test_engine_integration.cpp
    # tests/test_mouse_particle_detection.cpp
    # tests/test_platform_system.cpp
    # tests/register_platform_tests.cpp
    # tests/test_render_fatal_debug.cpp
    # tests/test_engine_neutrality.cpp
    # tests/test_ui_scaling.cpp
    # tests/test_particle_rendering.cpp
    # tests/test_isometric_surface_visibility.cpp
    # tests/test_performance_benchmark.cpp
    # tests/test_camera_culling.cpp
    # KG tests integrated into main library
    # tests/kg/test_kg_basic.cpp
    # Diagnostics tools
    src/diagnostics/shadow_probe.cpp
)

# Logosphere header files  
# Render-free umbrella for the physics profile: drop the Objective-C++
# platform TUs, the legacy GLFW platform layer, and the embedded test
# scaffolding (unified_test_runner externs the whole ~150-function test
# suite; the physics profile uses tests/physics_guard_runner.cpp
# instead). NullPlatform supplies Platform::create_platform_system().
# test_context.cpp STAYS: engine.cpp defines Engine::create_test_context.
if(LOGOSPHERE_PHYSICS_HEADLESS)
    list(REMOVE_ITEM LOGOSPHERE_SOURCES
        src/platform/platform_macos.mm
        src/platform/macos_platform.mm
        src/platform/platform_layer.cpp
        src/test_projections.cpp
        src/test_overlay.cpp
        src/test_suite_coordinator.cpp
        src/unified_test_runner.cpp
        tests/test_stubs.cpp
        tests/test_coordinate_system.cpp
        tests/test_pixel_rasterization.cpp
        tests/test_basic_cube.cpp
        tests/test_basic_cube_projections.cpp
        tests/test_uv_coordinates.cpp
        tests/visual_test_top.cpp
        tests/test_framework.cpp
        tests/test_shadow_diagonal.cpp
    )
    list(APPEND LOGOSPHERE_SOURCES src/platform/null_platform.cpp)
endif()

set(LOGOSPHERE_HEADERS
    src/particle_system.h
    src/particle_types.h
    src/core/projection_mode.h
    src/physics_system.h
    src/light_system.h
    src/input_system.h
    src/vision_system.h
    src/engine.h
    src/metrics_collector.h
    src/rendering/resolution_manager.h
    src/debug_overlay.h
    src/scene_manager.h
    src/system_coordinator.h
    src/system_initializer.h
    src/game_loop.h
    src/key_mapper.h
    src/main_key_handler.h
    src/test_projections.h
    src/test_suite_coordinator.h
    src/test_context.h
    src/unified_test_runner.h
)

# Create Logosphere library
add_library(logosphere STATIC ${LOGOSPHERE_SOURCES})

# Enable debug buffer for tests and debug builds
target_compile_definitions(logosphere PUBLIC DEBUG_BUILD)

# logosphere_core and the headless test suite are defined at the top of
# this file (above the LOGOSPHERE_HEADLESS_ONLY short-circuit).

# Include directories
target_include_directories(logosphere
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/generated
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

# ============================================================================
# logosphere_llm — HTTP LLM + optional llama.cpp backend
# ============================================================================
# HTTP client and HTTP-backed LLMSystem are always built (they have no
# heavyweight deps). The llama.cpp-backed LLMSystem is optional and only
# compiled if llama.cpp is found on disk.
set(LOGOSPHERE_LLM_SOURCES
    src/llm/http_client.cpp
    src/llm/llm_system_http.cpp
    src/tools/tool_registry.cpp
)

# Optional HTTPS backend via libcurl (CurlTransport). Default ON
# when find_package(CURL) succeeds; off otherwise. Off-state
# means cloud LLM APIs (Anthropic / OpenAI) are unreachable from
# this build but localhost vLLM still works fine.
option(LOGOSPHERE_LLM_HTTPS "Enable HTTPS for LLM cloud APIs (libcurl)" ON)
if(LOGOSPHERE_LLM_HTTPS)
    find_package(CURL QUIET)
    if(CURL_FOUND)
        list(APPEND LOGOSPHERE_LLM_SOURCES src/llm/curl_transport.cpp)
        message(STATUS "libcurl found - CurlTransport enabled (HTTPS for cloud LLMs)")
    else()
        message(STATUS "libcurl not found - CurlTransport disabled; cloud LLM APIs unavailable")
        set(LOGOSPHERE_LLM_HTTPS OFF)
    endif()
else()
    message(STATUS "LOGOSPHERE_LLM_HTTPS=OFF; CurlTransport not built")
endif()

# Optional llama.cpp backend
# Optional native llama.cpp backend: opt in with
#   -DLOGOSPHERE_LLAMA_ROOT=/path/to/llama.cpp
# (no implicit sibling-directory discovery; the HTTP backend needs nothing)
set(LOGOSPHERE_LLAMA_ROOT "" CACHE PATH "Path to a built llama.cpp checkout (optional)")
find_library(LLAMA_LIB llama HINTS ${LOGOSPHERE_LLAMA_ROOT}/build/bin)
find_library(GGML_LIB ggml HINTS ${LOGOSPHERE_LLAMA_ROOT}/build/bin)
find_library(GGML_METAL_LIB ggml-metal HINTS ${LOGOSPHERE_LLAMA_ROOT}/build/bin)
find_path(LLAMA_INCLUDE llama.h HINTS ${LOGOSPHERE_LLAMA_ROOT}/include)
if(LLAMA_INCLUDE)
    list(APPEND LOGOSPHERE_LLM_SOURCES src/llm/llm_system.cpp)
    message(STATUS "llama.cpp found - LLMSystem (native) enabled")
else()
    message(STATUS "llama.cpp not found - LLMSystem (native) disabled; HTTP backend still available")
endif()

add_library(logosphere_llm STATIC ${LOGOSPHERE_LLM_SOURCES})
target_link_libraries(logosphere_llm PUBLIC logosphere_core)
target_include_directories(logosphere_llm PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor  # nlohmann/json for tool registry
)
if(LLAMA_INCLUDE)
    target_include_directories(logosphere_llm PUBLIC
        ${LLAMA_INCLUDE}
        ${LOGOSPHERE_LLAMA_ROOT}/ggml/include
    )
endif()
target_compile_features(logosphere_llm PUBLIC cxx_std_17)
target_compile_definitions(logosphere_llm PUBLIC HAS_LLAMA)
if(LOGOSPHERE_LLM_HTTPS AND CURL_FOUND)
    target_compile_definitions(logosphere_llm PUBLIC LOGOSPHERE_HAS_LLM_HTTPS)
    target_link_libraries(logosphere_llm PUBLIC CURL::libcurl)
endif()
message(STATUS "Tool registry enabled (nlohmann/json from vendor/)")

target_link_libraries(logosphere PUBLIC logosphere_llm)

# Link libraries (windowing + Apple frameworks: full profile only)
if(LOGOSPHERE_FULL)
target_link_libraries(logosphere
    PUBLIC
        glfw
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
        "-framework Metal"
        "-framework MetalKit"
        "-framework MetalPerformanceShaders"
        "-framework QuartzCore"
        "-framework Foundation"
)
endif()

# Link llama.cpp libraries to logosphere_llm if found
if(LLAMA_LIB AND GGML_LIB AND GGML_METAL_LIB)
    target_link_libraries(logosphere_llm
        PUBLIC
            ${LLAMA_LIB}
            ${GGML_LIB}
            ${GGML_METAL_LIB}
    )
endif()

# ============================================================================
# logosphere_worldgen — procedural content generation
# ============================================================================
# Generators for humanoids, trees, rocks, butterflies, snakes, strata, etc.
# Plus the chunk streaming system that drives procedural world loading.

target_link_libraries(logosphere PUBLIC logosphere_worldgen)

# ============================================================================
# logosphere_dynamics — animation, FK, locomotion, kinematic root
# ============================================================================
# Mirrors docs/MODULE_ARCHITECTURE.md phase 5. Contains the particle
# dynamics system (6594-line beast driving humanoid locomotion) plus the
# animation clip engine. Headers live under include/logosphere/dynamics/.

target_link_libraries(logosphere PUBLIC logosphere_dynamics)

# ============================================================================
# logosphere_physics — XPBD solver, gluons, narrow-phase, BVH
# ============================================================================
# Mirrors docs/MODULE_ARCHITECTURE.md phase 8. Sequential-impulse solver,
# gluon constraints, SAT narrow-phase (duplicated as a tiny .cpp in
# logosphere_core for the headless particle-shape test), and the BVH
# acceleration structure used by both physics broad-phase and rendering.

target_link_libraries(logosphere PUBLIC logosphere_physics)

# ============================================================================
# logosphere_rendering — software raster, Metal lighting, shadows
# ============================================================================
# Mirrors docs/MODULE_ARCHITECTURE.md phase 7. CPU tile rasterizer, Metal
# GPU compute bridge, deferred lighting, BVH used by shadow rays. Pulls
# Cocoa / Metal frameworks for the .mm sources.
set(LOGOSPHERE_RENDERING_SOURCES
    src/rendering/font_renderer.cpp
    src/rendering/primitive_renderer.cpp
    src/rendering/pixel_buffer.cpp
    src/rendering/coordinate_transformer.cpp
    src/rendering/depth_buffer.cpp
    src/rendering/surface_rasterizer.cpp
    src/rendering/parallel_tile_processor.cpp
    src/rendering/tile_thread_pool.cpp
    src/rendering/shadow_ray_batch.cpp
    src/rendering/surface_rasterizer_single_pass.cpp
    src/rendering/pixel_shader.cpp
    src/rendering/render_pipeline.cpp
    # DrawSurface: concrete IDrawSurface. Engine owns one instance
    # and hands it to UI widgets, game HUDs, and debug-text callers
    # instead of exposing RenderSystem's drawing API directly.
    src/rendering/draw_surface.cpp
    # PixelPicker: mouse hit-testing. Owns the framebuffer→render
    # coord transform and queries the object map. Replaces the
    # RenderSystem::get_object_at_pixel surface.
    src/rendering/pixel_picker.cpp
    # MetalRenderer: macOS IRenderer implementation. Phase 2 of the
    # Renderer/Display split: a thin forwarder over RenderSystem that
    # lets callers depend on the IRenderer interface without a full
    # ownership migration. Phase 4 will invert ownership.
    src/rendering/metal_renderer.cpp
    # MacOSDisplay: macOS IDisplay implementation. Phase 3 of the
    # Renderer/Display split: a thin forwarder over the existing
    # Platform::IPlatformSystem for the present() surface only.
    src/display/macos_display.cpp
    src/rendering/render_spatial_grid.cpp
    src/rendering/sparse_object_map.cpp
    src/rendering/triangle_bvh.cpp
    src/rendering/entity_bvh.cpp
    src/rendering/resolution_manager.cpp
)
# GPU backend: real Metal on the full profile, no-op stub elsewhere.
# The stub's initialize() returns true and fires completion callbacks
# immediately (see src/rendering/gpu/gpu_stub.cpp for the contract).
if(LOGOSPHERE_FULL)
    list(APPEND LOGOSPHERE_RENDERING_SOURCES
        src/rendering/gpu/metal_compute_bridge.mm
        src/rendering/gpu/gpu_rasterizer.mm
    )
else()
    list(APPEND LOGOSPHERE_RENDERING_SOURCES src/rendering/gpu/gpu_stub.cpp)
endif()

add_library(logosphere_rendering STATIC ${LOGOSPHERE_RENDERING_SOURCES})
target_link_libraries(logosphere_rendering PUBLIC logosphere_core)
if(LOGOSPHERE_FULL)
target_link_libraries(logosphere_rendering
    PUBLIC
        glfw
        "-framework Cocoa" "-framework IOKit" "-framework CoreVideo"
        "-framework Metal" "-framework MetalKit"
        "-framework MetalPerformanceShaders" "-framework Foundation"
)
endif()
target_include_directories(logosphere_rendering PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(logosphere_rendering PUBLIC cxx_std_17)
target_compile_definitions(logosphere_rendering PUBLIC DEBUG_BUILD HAS_LLAMA)

target_link_libraries(logosphere PUBLIC logosphere_rendering)

# ============================================================================
# Physics guard runner — the locomotion guard suite on the render-free
# Engine. Built in BOTH profiles (macOS devs run the same binary the
# physics-linux CI job gates on). Dedicated runner: the combined harness
# and unified_test_runner cannot link without the full test suite.
# ============================================================================
add_executable(logosphere-physics-guards
    tests/physics_guard_runner.cpp
    tests/test_stance_foot_invariance.cpp
    tests/test_walk_forward_progress.cpp
    tests/test_strafe_progress.cpp
    tests/test_rotation_cascade_yaw.cpp
    tests/test_turn_in_place_foot_step.cpp
    tests/test_rotation_during_walk.cpp
    tests/test_strafe_sidestep_pattern.cpp
    tests/test_leg_shoot_out_during_rotation.cpp
    tests/test_idle_pose_stability.cpp
    tests/test_humanoid_strata_walk.cpp
    tests/test_spider_eva_shin_crush.cpp
    tests/test_joint_hierarchy_swap_integrity.cpp
    tests/test_reverse_leg_chain.cpp
    tests/test_phase_e_diagnostic.cpp
    tests/test_position_authority.cpp
    tests/test_deferred_deletion_integrity.cpp
    tests/test_pin_anchor_persistence.cpp
    tests/test_interaction_filtering.cpp
    tests/test_interaction_volume_forces.cpp
    tests/test_interaction_transformations.cpp
)
target_link_libraries(logosphere-physics-guards PRIVATE logosphere)
target_include_directories(logosphere-physics-guards PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
target_compile_features(logosphere-physics-guards PRIVATE cxx_std_17)

# ============================================================================
# Standalone animation tests — build in BOTH full and physics profiles.
# The umbrella carries glfw + Apple frameworks PUBLIC in the full profile,
# so linking `logosphere` is sufficient everywhere. Tests default to
# headless and use the glfw_compat shim; on Linux they run against the
# render-free Engine. CI runs the green subset (see physics-linux job);
# the stale FK-era reds are documented in CHANGELOG Known issues.
# ============================================================================
set(STANDALONE_ANIMATION_TESTS
    test_animation_layering
    test_animation_primitives
    test_bilateral_animation
    test_foot_planting
    test_humanoid_movement
    test_idle_run
    test_leg_primitives
    test_punch_evolution_primitives
    test_running
    test_spine_fk_lookat
    test_walk_feel
    test_walk_locomotion
    test_walk_rotation
)

foreach(TEST_NAME ${STANDALONE_ANIMATION_TESTS})
    add_executable(${TEST_NAME} tests/${TEST_NAME}.cpp)
    target_link_libraries(${TEST_NAME} PRIVATE logosphere)
    target_include_directories(${TEST_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_compile_features(${TEST_NAME} PRIVATE cxx_std_17)
endforeach()

# Deep-probe halt-semantics regression (headless Engine; both profiles)
add_executable(test_deep_probe_halt tests/test_deep_probe_halt.cpp)
target_link_libraries(test_deep_probe_halt PRIVATE logosphere)
target_include_directories(test_deep_probe_halt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(test_deep_probe_halt PRIVATE cxx_std_17)

# Physics profile stops here: everything below is full-only (Metal
# shaders, examples, the combined test harness, standalone tools).
if(LOGOSPHERE_PHYSICS_HEADLESS)
    return()
endif()

# Compiler flags
if(APPLE)
    target_compile_options(logosphere PRIVATE -Wall -Wextra)
endif()

# =====================================
# METAL SHADER COMPILATION (macOS only)
# =====================================
# Compile .metal shaders to .metallib for GPU compute
# Two-stage process: .metal → .air → .metallib
# Output: default.metallib in build directory

if(APPLE)
    # Metal shader source files
    set(METAL_SHADOW_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/gpu/shadow_ray.metal)
    set(METAL_LIGHTING_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/gpu/lighting_compute.metal)
    set(METAL_RASTERIZE_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/gpu/rasterize_triangle.metal)
    set(METAL_CLEAR_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/gpu/clear_buffers.metal)
    # Deferred rendering shaders (4-pass architecture)
    set(METAL_GBUFFER_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/gpu/rasterize_gbuffer.metal)
    set(METAL_SHADOWS_DEFERRED_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/gpu/shadow_rays_deferred.metal)
    set(METAL_LIGHTING_DEFERRED_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/gpu/apply_lighting_deferred.metal)
    set(METAL_VISION_CONE_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/gpu/vision_cone_postprocess.metal)
    set(METAL_PENUMBRA_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/gpu/penumbra.metal)
    set(METAL_FORWARD_TRANSPARENT_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/gpu/forward_transparent.metal)
    set(METAL_SSAO_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/gpu/ssao.metal)
    set(METAL_DDGI_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/gpu/ddgi.metal)

    # Shared .metal headers included by the sources above. Every .air rule
    # depends on these: editing one must recompile ALL shaders, or the
    # metallib links objects with mismatched struct layouts (caught
    # 2026-07-24: GBufferPixel 36->32 left stale 36-byte .air files and
    # the frame went dark).
    set(METAL_SHARED_HEADERS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/gpu/gbuffer_types.metal
        ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/gpu/gpu_types.metal
        ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/gpu/gpu_math.metal
        ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/gpu/gpu_constants.metal
        ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/gpu/depth_encoding.metal
        ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/gpu/optimization_flags.metal
        ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/gpu/procedural_patterns.metal
    )

    # Intermediate and output files
    set(METAL_SHADOW_AIR ${CMAKE_CURRENT_BINARY_DIR}/shadow_ray.air)
    set(METAL_LIGHTING_AIR ${CMAKE_CURRENT_BINARY_DIR}/lighting_compute.air)
    set(METAL_RASTERIZE_AIR ${CMAKE_CURRENT_BINARY_DIR}/rasterize_triangle.air)
    set(METAL_CLEAR_AIR ${CMAKE_CURRENT_BINARY_DIR}/clear_buffers.air)
    # Deferred rendering AIR files
    set(METAL_GBUFFER_AIR ${CMAKE_CURRENT_BINARY_DIR}/rasterize_gbuffer.air)
    set(METAL_SHADOWS_DEFERRED_AIR ${CMAKE_CURRENT_BINARY_DIR}/shadow_rays_deferred.air)
    set(METAL_LIGHTING_DEFERRED_AIR ${CMAKE_CURRENT_BINARY_DIR}/apply_lighting_deferred.air)
    set(METAL_VISION_CONE_AIR ${CMAKE_CURRENT_BINARY_DIR}/vision_cone_postprocess.air)
    set(METAL_PENUMBRA_AIR ${CMAKE_CURRENT_BINARY_DIR}/penumbra.air)
    set(METAL_FORWARD_TRANSPARENT_AIR ${CMAKE_CURRENT_BINARY_DIR}/forward_transparent.air)
    set(METAL_SSAO_AIR ${CMAKE_CURRENT_BINARY_DIR}/ssao.air)
    set(METAL_DDGI_AIR ${CMAKE_CURRENT_BINARY_DIR}/ddgi.air)
    set(METAL_LIB_FILE ${CMAKE_CURRENT_BINARY_DIR}/default.metallib)

    # Stage 1: Compile shadow_ray.metal to .air
    add_custom_command(
        OUTPUT ${METAL_SHADOW_AIR}
        COMMAND xcrun -sdk macosx metal -c ${METAL_SHADOW_SOURCE} -o ${METAL_SHADOW_AIR}
        DEPENDS ${METAL_SHADOW_SOURCE} ${METAL_SHARED_HEADERS}
        COMMENT "Compiling Metal shader: shadow_ray.metal → shadow_ray.air"
        VERBATIM
    )

    # Stage 1: Compile lighting_compute.metal to .air
    add_custom_command(
        OUTPUT ${METAL_LIGHTING_AIR}
        COMMAND xcrun -sdk macosx metal -c ${METAL_LIGHTING_SOURCE} -o ${METAL_LIGHTING_AIR}
        DEPENDS ${METAL_LIGHTING_SOURCE} ${METAL_SHARED_HEADERS}
        COMMENT "Compiling Metal shader: lighting_compute.metal → lighting_compute.air"
        VERBATIM
    )

    # Stage 1: Compile rasterize_triangle.metal to .air
    add_custom_command(
        OUTPUT ${METAL_RASTERIZE_AIR}
        COMMAND xcrun -sdk macosx metal -c ${METAL_RASTERIZE_SOURCE} -o ${METAL_RASTERIZE_AIR}
        DEPENDS ${METAL_RASTERIZE_SOURCE} ${METAL_SHARED_HEADERS}
        COMMENT "Compiling Metal shader: rasterize_triangle.metal → rasterize_triangle.air"
        VERBATIM
    )

    # Stage 1: Compile clear_buffers.metal to .air
    add_custom_command(
        OUTPUT ${METAL_CLEAR_AIR}
        COMMAND xcrun -sdk macosx metal -c ${METAL_CLEAR_SOURCE} -o ${METAL_CLEAR_AIR}
        DEPENDS ${METAL_CLEAR_SOURCE} ${METAL_SHARED_HEADERS}
        COMMENT "Compiling Metal shader: clear_buffers.metal → clear_buffers.air"
        VERBATIM
    )

    # Stage 1: Compile rasterize_gbuffer.metal to .air (Deferred Pass 1)
    add_custom_command(
        OUTPUT ${METAL_GBUFFER_AIR}
        COMMAND xcrun -sdk macosx metal -c ${METAL_GBUFFER_SOURCE} -o ${METAL_GBUFFER_AIR}
        DEPENDS ${METAL_GBUFFER_SOURCE} ${METAL_SHARED_HEADERS}
        COMMENT "Compiling Metal shader: rasterize_gbuffer.metal → rasterize_gbuffer.air"
        VERBATIM
    )

    # Stage 1: Compile shadow_rays_deferred.metal to .air (Deferred Pass 2)
    # Pass Phase 2 temporal distribution flags from optimization_flags.h
    # USE_TEMPORAL_LIGHTING: Enable temporal distribution (diagonal stripe pattern)
    # TEMPORAL_FRAME_COUNT: Frame distribution (2, 3, 4, or 5)
    # USE_SOFT_SHADOWS: Enable soft shadow temporal accumulation
    # SOFT_SHADOW_TEMPORAL_FRAMES: Number of frames to accumulate for soft shadows (4-8)
    add_custom_command(
        OUTPUT ${METAL_SHADOWS_DEFERRED_AIR}
        COMMAND xcrun -sdk macosx metal -c ${METAL_SHADOWS_DEFERRED_SOURCE} -o ${METAL_SHADOWS_DEFERRED_AIR}
                -DUSE_TEMPORAL_LIGHTING=1 -DTEMPORAL_FRAME_COUNT=1
                -DUSE_SOFT_SHADOWS=1 -DSOFT_SHADOW_TEMPORAL_FRAMES=4 -DSOFT_SHADOW_MAX_SAMPLES=64
                -DPCSS_BLOCKER_SAMPLES=8 -DPCSS_PENUMBRA_SAMPLES=16
                -DPENUMBRA_MODE=5
        DEPENDS ${METAL_SHADOWS_DEFERRED_SOURCE} ${METAL_SHARED_HEADERS}
        COMMENT "Compiling Metal shader: shadow_rays_deferred.metal → shadow_rays_deferred.air (soft shadows enabled)"
        VERBATIM
    )

    # Stage 1: Compile apply_lighting_deferred.metal to .air (Deferred Pass 3)
    add_custom_command(
        OUTPUT ${METAL_LIGHTING_DEFERRED_AIR}
        COMMAND xcrun -sdk macosx metal -c ${METAL_LIGHTING_DEFERRED_SOURCE} -o ${METAL_LIGHTING_DEFERRED_AIR}
        DEPENDS ${METAL_LIGHTING_DEFERRED_SOURCE} ${METAL_SHARED_HEADERS}
        COMMENT "Compiling Metal shader: apply_lighting_deferred.metal → apply_lighting_deferred.air"
        VERBATIM
    )

    # Stage 1: Compile vision_cone_postprocess.metal to .air (Deferred Pass 4 - optional)
    add_custom_command(
        OUTPUT ${METAL_VISION_CONE_AIR}
        COMMAND xcrun -sdk macosx metal -c ${METAL_VISION_CONE_SOURCE} -o ${METAL_VISION_CONE_AIR}
        DEPENDS ${METAL_VISION_CONE_SOURCE} ${METAL_SHARED_HEADERS}
        COMMENT "Compiling Metal shader: vision_cone_postprocess.metal → vision_cone_postprocess.air"
        VERBATIM
    )

    # Stage 1: Compile penumbra.metal to .air (Pass 2.5 - penumbra post-process)
    add_custom_command(
        OUTPUT ${METAL_PENUMBRA_AIR}
        COMMAND xcrun -sdk macosx metal -c ${METAL_PENUMBRA_SOURCE} -o ${METAL_PENUMBRA_AIR}
        DEPENDS ${METAL_PENUMBRA_SOURCE} ${METAL_SHARED_HEADERS}
        COMMENT "Compiling Metal shader: penumbra.metal → penumbra.air"
        VERBATIM
    )

    # Stage 1: Compile forward_transparent.metal to .air (Pass 3.5 - transparency forward pass)
    add_custom_command(
        OUTPUT ${METAL_FORWARD_TRANSPARENT_AIR}
        COMMAND xcrun -sdk macosx metal -c ${METAL_FORWARD_TRANSPARENT_SOURCE} -o ${METAL_FORWARD_TRANSPARENT_AIR}
        DEPENDS ${METAL_FORWARD_TRANSPARENT_SOURCE} ${METAL_SHARED_HEADERS}
        COMMENT "Compiling Metal shader: forward_transparent.metal → forward_transparent.air"
        VERBATIM
    )

    # Stage 1: Compile ssao.metal to .air (Pass 2.7 - screen-space ambient occlusion)
    add_custom_command(
        OUTPUT ${METAL_SSAO_AIR}
        COMMAND xcrun -sdk macosx metal -c ${METAL_SSAO_SOURCE} -o ${METAL_SSAO_AIR}
        DEPENDS ${METAL_SSAO_SOURCE} ${METAL_SHARED_HEADERS}
        COMMENT "Compiling Metal shader: ssao.metal → ssao.air"
        VERBATIM
    )

    # Stage 1: Compile ddgi.metal to .air (Pass 2.5b/c - probe-based global illumination)
    add_custom_command(
        OUTPUT ${METAL_DDGI_AIR}
        COMMAND xcrun -sdk macosx metal -c ${METAL_DDGI_SOURCE} -o ${METAL_DDGI_AIR}
        DEPENDS ${METAL_DDGI_SOURCE} ${METAL_SHARED_HEADERS}
        COMMENT "Compiling Metal shader: ddgi.metal → ddgi.air"
        VERBATIM
    )

    # Stage 2: Link all .air files to .metallib (Metal library)
    add_custom_command(
        OUTPUT ${METAL_LIB_FILE}
        COMMAND xcrun -sdk macosx metallib ${METAL_SHADOW_AIR} ${METAL_LIGHTING_AIR} ${METAL_RASTERIZE_AIR} ${METAL_CLEAR_AIR} ${METAL_GBUFFER_AIR} ${METAL_SHADOWS_DEFERRED_AIR} ${METAL_LIGHTING_DEFERRED_AIR} ${METAL_VISION_CONE_AIR} ${METAL_PENUMBRA_AIR} ${METAL_FORWARD_TRANSPARENT_AIR} ${METAL_SSAO_AIR} ${METAL_DDGI_AIR} -o ${METAL_LIB_FILE}
        DEPENDS ${METAL_SHADOW_AIR} ${METAL_LIGHTING_AIR} ${METAL_RASTERIZE_AIR} ${METAL_CLEAR_AIR} ${METAL_GBUFFER_AIR} ${METAL_SHADOWS_DEFERRED_AIR} ${METAL_LIGHTING_DEFERRED_AIR} ${METAL_VISION_CONE_AIR} ${METAL_PENUMBRA_AIR} ${METAL_FORWARD_TRANSPARENT_AIR} ${METAL_SSAO_AIR} ${METAL_DDGI_AIR}
        COMMENT "Linking Metal library: 12 shaders → default.metallib"
        VERBATIM
    )

    # Create custom target to ensure Metal shaders are built
    add_custom_target(metal_shaders ALL
        DEPENDS ${METAL_LIB_FILE}
        COMMENT "Building Metal shaders for GPU compute"
    )

    # Make logosphere library depend on Metal shaders being compiled
    add_dependencies(logosphere metal_shaders)

    message(STATUS "Metal shader compilation enabled")
    message(STATUS "  Shadow shader: ${METAL_SHADOW_SOURCE}")
    message(STATUS "  Lighting shader: ${METAL_LIGHTING_SOURCE}")
    message(STATUS "  Rasterization shader: ${METAL_RASTERIZE_SOURCE}")
    message(STATUS "  Output: ${METAL_LIB_FILE}")
endif()

# =====================================
# EDEN EXAMPLE GAME
# =====================================
add_subdirectory(examples/eden)
add_subdirectory(examples/logotron)
add_subdirectory(examples/logogenesis)
add_subdirectory(examples/threshold)

if(LOGOSPHERE_FULL)
    # at_logogenesis_creation — chat text -> LLM ops -> seeds ->
    # worldgen materialization, headless, no network (scripted
    # responder / offline gardener).
    add_executable(at_logogenesis_creation
        examples/logogenesis/tests/at_logogenesis_creation.cpp
        examples/logogenesis/src/generated/logogenesis_ontology_registry.cpp
    )
    target_link_libraries(at_logogenesis_creation
        PRIVATE
            logosphere
            glfw
            "-framework Cocoa" "-framework IOKit" "-framework CoreVideo"
            "-framework CoreGraphics" "-framework AppKit"
            "-framework Foundation" "-framework QuartzCore"
            "-framework Metal" "-framework MetalKit"
            "-framework MetalPerformanceShaders"
    )
    target_include_directories(at_logogenesis_creation PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/logogenesis/src
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/logogenesis/src/generated
    )
    target_compile_features(at_logogenesis_creation PRIVATE cxx_std_17)

    # eval_logogenesis_llm — LIVE-LLM regression evals: canonical
    # requests against the real model, range assertions on engine
    # output. Requires ANTHROPIC_API_KEY; skips cleanly without
    # (CI-safe). Not in the default battery — run on demand.
    add_executable(eval_logogenesis_llm
        examples/logogenesis/tests/eval_logogenesis_llm.cpp
        examples/logogenesis/src/generated/logogenesis_ontology_registry.cpp
    )
    target_link_libraries(eval_logogenesis_llm
        PRIVATE
            logosphere
            glfw
            "-framework Cocoa" "-framework IOKit" "-framework CoreVideo"
            "-framework CoreGraphics" "-framework AppKit"
            "-framework Foundation" "-framework QuartzCore"
            "-framework Metal" "-framework MetalKit"
            "-framework MetalPerformanceShaders"
    )
    target_include_directories(eval_logogenesis_llm PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/logogenesis/src
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/logogenesis/src/generated
    )
    target_compile_features(eval_logogenesis_llm PRIVATE cxx_std_17)
endif()

# =====================================
# TEST HARNESS EXECUTABLE
# =====================================
# Separate test executable that uses Logosphere as a library
# This follows the principle that "Engine CANNOT run standalone"

# Collect test sources
set(TEST_SOURCES
    tests/test_main.cpp
    tests/test_harness.cpp
    src/unified_test_runner.cpp  # Test runner is currently in src
    src/test_context.cpp         # Test context is in src
    tests/test_coordinate_system.cpp
    tests/test_pixel_rasterization.cpp
    tests/test_basic_cube.cpp
    tests/test_basic_cube_projections.cpp
    tests/test_uv_coordinates.cpp
    tests/visual_test_top.cpp    # Visual test with light above cube
    tests/test_stubs.cpp         # Temporary stubs for old tests
    tests/test_occlusion_culling.cpp  # Occlusion culling verification
    tests/test_bvh.cpp                # BVH tests (includes test_bvh_basic and test_bvh_performance)
    tests/test_entity_bvh.cpp         # EntityBVH tests (entity-level culling with directional groups)
    tests/test_bvh_shadow_accuracy.cpp # BVH shadow accuracy test
    tests/test_shadow_movement_direction.cpp # Shadow movement direction test
    tests/test_kg_stable_particle_ids.cpp # KG stable particle ID system test
    tests/test_surface_ray_tracing.cpp # Surface ray tracing test
    tests/test_eden_shadows.cpp       # Eden shadow scenario test
    tests/test_shadow_diagonal.cpp    # Debug diagonal shadow pattern
    tests/test_triangle_shadow_artifacts.cpp # Triangle shadow artifacts test
    tests/test_shadow_ray_blocking.cpp # Shadow ray blocking test
    tests/test_wall_rendering_issue.cpp # Wall rendering issue test
    tests/test_pixel_shadow_visual.cpp # Pixel shadow visual test
    tests/test_simd_edge_equations.cpp # SIMD edge equation evaluation test
    tests/test_parallel_tiles.cpp # Parallel tile rendering test (2025-01-22)
    tests/test_performance_single_light_20.cpp # Performance benchmark: 20 particles, 1 light
    tests/test_multi_light_performance.cpp     # Multi-light performance impact test
    tests/test_multi_light_progressive.cpp     # Progressive multi-light test
    tests/test_sphere_lod_quality.cpp          # Icosphere LOD quality vs cost
    tests/test_gpu_shadow_ray.cpp              # GPU compute shadow ray validation (Phase I MVP)
    # tests/test_gpu_multi_triangle.cpp          # GPU multi-triangle shadow ray (Phase I-B) - FILE MISSING
    tests/test_gpu_parallel_rays.cpp           # GPU parallel rays (Phase I-C)
    tests/test_gpu_ray_batching.cpp            # GPU ray batching (Phase II-A)
    tests/test_gpu_rasterize_minimal.cpp       # GPU rasterization minimal (Phase III STEP 1)
    tests/test_gpu_rasterize_triangle.cpp      # GPU triangle rasterization (Phase III STEP 2)
    tests/test_gpu_barycentric.cpp             # GPU barycentric interpolation (Phase III STEP 3)
    tests/test_gpu_lighting.cpp                # GPU lighting integration (Phase III STEP 7)
    # REMOVED: test_load_bearing_constraints.cpp - dead feature (is_load_bearing_ field removed)
    # REMOVED: test_eva_humanoid.cpp - old test
    tests/test_stiffness_stability.cpp         # XPBD stability investigation - incremental complexity
    # REMOVED: test_eva_floor_interaction.cpp - old test, Eva floor interaction now covered by test_physics_experiment_01 Case 5
    tests/test_physics_experiment_01.cpp       # PHYSICS v2: Newtonian gravity basics (visual)
    tests/test_turtle_single_particle.cpp      # TURTLE EXPERIMENT: Single particle on Turtle
    tests/test_oscillation_diagnostic.cpp     # OSCILLATION DIAGNOSTIC: Gluoned tile on Turtle
    tests/test_physics_minimal.cpp             # MINIMAL PHYSICS: Simplest tile-on-turtle cases
    tests/test_physics_minimal_v2.cpp          # MINIMAL PHYSICS V2: Progressive complexity
    tests/test_physics_tree_roots.cpp          # PHYSICS: Tree with roots on turtle (no floor tiles)
    tests/test_experiment_totem_builder.cpp    # PHYSICS v2: Totem collision stacking test (Eden structure)
    tests/test_totem_gluon_nails.cpp           # PHYSICS v2: Gluon nail constraints test (pre-built totem)
    tests/test_gluon_tree.cpp                  # PHYSICS v2: Tree structure with gluon branches
    tests/test_gluon_tree_v34.cpp              # PHYSICS v3.4: Load propagation through contacts
    tests/test_physics_tree.cpp                 # PHYSICS: PhysicsTreeGenerator test
    tests/test_ancient_oak.cpp                  # PHYSICS: Ancient Oak tree test
    tests/test_sleep_diagnostics.cpp            # PHYSICS: Sleep diagnostics test
    tests/test_tree_wiggly.cpp                  # PHYSICS: Tree wiggly test - same oak as logomancers
    tests/test_tree_shadow_wiggly.cpp           # PHYSICS: Tree shadow stability test - sunset light
    tests/test_falling_cube.cpp                 # PHYSICS: Falling cube collision test
    tests/test_physics_rock.cpp                  # PHYSICS: PhysicsRockGenerator test
    tests/test_contact_manifold.cpp              # PHYSICS: SAT + Sutherland-Hodgman clipping
    tests/test_body_coherence.cpp                # PHYSICS: Body stays intact during walking
    tests/test_humanoid_movement_instrumented.cpp # PHYSICS: Full limb instrumentation idle + walk
    tests/test_layered_floor_v1.cpp              # PHYSICS: Organic layered floor generation
    tests/test_layered_floor_v2.cpp              # PHYSICS: Frontier-based floor generation
    tests/test_layered_floor_v3.cpp              # PHYSICS: Sequential strata with settling
    tests/test_strata_generator.cpp              # WORLDGEN: Strata generator unit test
    tests/test_awake_onto_at_rest.cpp            # PHYSICS: minimal 2-particle stack trace
    tests/test_humanoid_strata_walk.cpp          # PHYSICS: humanoid walks streaming strata w/ obstacles
    tests/test_spider_eva_shin_crush.cpp         # PHYSICS: wedge — l_shin<>l_thigh crush during SW walk
    tests/test_stance_foot_invariance.cpp        # PHYSICS: kinematic-root TDD wedge — stance foot stays on ground
    tests/test_walk_forward_progress.cpp         # PHYSICS: kinematic-root post-shift wedge — Eva walks forward as intended
    tests/test_strafe_progress.cpp                # PHYSICS: strafe moves body in intent direction (not facing)
    tests/test_rotation_cascade_yaw.cpp          # PHYSICS: head→chest→hips yaw cascade on sudden rotation target
    tests/test_turn_in_place_foot_step.cpp       # PHYSICS: feet step under rotating hips (Stage 2 rotation TDD)
    tests/test_rotation_during_walk.cpp          # PHYSICS: walking + yawing traces a curved path (Stage 3 rotation TDD)
    tests/test_strafe_sidestep_pattern.cpp       # PHYSICS: strafe feet don't cross midline (Stage 4 rotation TDD)
    tests/test_leg_shoot_out_during_rotation.cpp # PHYSICS: no leg teleports during yaw cascade + twist-step
    tests/test_idle_pose_stability.cpp           # PHYSICS: standing Eva keeps feet under body
    tests/test_gluon_angular_drive_converges.cpp # PHYSICS-DRIVE: gluon PD drive to target rotation (Phase 1)
    tests/test_gluon_3axis_drive_converges.cpp   # ROTATIONAL-DOF: gluon 3-axis PD drive (Stage 2)
    tests/test_particle_quat_euler_sync.cpp      # ROTATIONAL-DOF: quat-driven Euler derivation (Stage 3 bridge)
    tests/test_physics_drive_neck_yaw.cpp        # PHYSICS-DRIVE: humanoid neck yaw physics-driven (Phase 2)
    tests/test_physics_drive_shoulder_multiaxis.cpp # ROTATIONAL-DOF: humanoid shoulder quat drive (Stage 3)
    tests/test_physics_drive_two_joints.cpp       # PHYSICS-DRIVE: concurrent neck + shoulder drive (Phase 3a)
    tests/test_physics_drive_arm_chain.cpp        # PHYSICS-DRIVE: shoulder+elbow+wrist chain (Phase 3b)
    tests/test_gluon_chain_converges.cpp          # PHYSICS-DRIVE: isolated 3-particle chain (Phase 3b probe)
    tests/test_physics_drive_full_idle.cpp        # PHYSICS-DRIVE: whole-body idle on Eva (Phase 3c)
    tests/test_physics_drive_walk_upper_body.cpp  # PHYSICS-DRIVE: walk with upper body physics-driven (Phase 4a)
    tests/test_gluon_distance_holds_offset.cpp    # GLUON-REHAB: gluon closes position gap from offset (TDD, validates ENABLE_GLUON_JACOBIAN_POSITION_BIAS)
    tests/test_pin_gluon_holds_particle.cpp       # PHYSICS-DRIVE: pin gluon holds DYNAMIC at KINEMATIC anchor (Phase 4b-2 mechanism)
    tests/test_physics_drive_walk_legs.cpp        # PHYSICS-DRIVE: walk with physics-driven legs on Eva (Phase 4b C3 capstone)
    tests/test_physics_drive_walk_legs_fast.cpp   # PHYSICS-DRIVE: 2 m/s fast walk substepping proof (V4.11)
    tests/test_phase_e_diagnostic.cpp             # PHASE-E: headless RCA harness for "shooting stuff"
    tests/test_collision_event_swap_integrity.cpp # PHYSICS: collision events stay valid across particle delete/swap
    tests/test_position_authority.cpp             # PHYSICS: one integrator per particle — speed cap binds ground speed
    tests/test_deferred_deletion_integrity.cpp    # PHYSICS: queued deletions survive swap-and-pop
    tests/test_pin_anchor_persistence.cpp         # PHYSICS: foot-plant anchors moved, never churned
    tests/test_shadow_edge_quantization.cpp       # RENDERING: hard-shadow staircase catcher
    tests/test_ui_text_dpi.cpp                    # UI: HUD glyph scale follows render resolution
    tests/test_gpu_wait_no_fixed_sleep.cpp        # RENDERING: GPU drain primitive carries no fixed sleep (live-stall guard)
    tests/test_ui_overlay_plane.cpp               # UI: overlay plane raster/composite separation (Option B step 1)
    tests/test_ui_overlay_survives_frames.cpp     # UI: HUD survives GPU completions across N presents (flicker lock)
    tests/test_interaction_filtering.cpp     # INTERACTION: profile filter seam in contact broad phase
    tests/test_interaction_volume_forces.cpp     # INTERACTION: medium drag/buoyancy/field forces
    tests/test_interaction_transformations.cpp   # INTERACTION: declarative transformation rules
    tests/test_joint_hierarchy_swap_integrity.cpp # PHYSICS: joint hierarchy ids stay in sync across chunk swaps
    tests/test_reverse_leg_chain.cpp             # ANIMATION: reverse_leg_chain_to_hip roundtrip (kinematic-root Stage 1b)
    tests/test_soft_shadows.cpp                 # RENDERING: Soft shadow visual test
    tests/test_ssgi_visual.cpp                  # RENDERING: SSGI + soft shadows visual test suite
    tests/test_shadow_quality.cpp               # RENDERING: Shadow quality (stability + penumbra)
    tests/test_physics_profile.cpp              # PHYSICS: Physics profiling test (STANDALONE)
    # REMOVED: test_eva_physics.cpp - old test
    tests/test_eva_movement.cpp                 # PHYSICS: Eva movement stress test
    tests/test_capability_profile.cpp           # Engine: CapabilityProfile + DynamicsParams
    tests/test_spirit_light_artifacts.cpp       # RENDERING: Spirit light shadow/box artifacts
    tests/test_shadow_invariant.cpp             # RENDERING: Shadow invariant — can only reduce light
    tests/test_rock_shadow_artifact.cpp         # RENDERING: Rock shadow vertical streak
    tests/test_gi_radial_artifact.cpp            # RENDERING: GI noise radial artifact
    tests/test_gi_speckle.cpp                    # RENDERING: GI speckle on floor
    tests/test_ssao_basic.cpp                    # RENDERING: SSAO contact shadows
    tests/test_gi_bounce.cpp                     # RENDERING: GI colored bounce
    tests/test_ddgi_bounce.cpp                   # RENDERING: DDGI probe-based bounce
    tests/test_bvh_stress.cpp                    # PERF: BVH rebuild stall test
    tests/test_chunk_floor.cpp                   # WORLDGEN: Floor chunk streaming
    tests/test_memory_leak.cpp                   # PERF: Memory leak detection
    tests/test_tile_sticking.cpp                 # PHYSICS: Tile boundary sticking (telemetry)
    tests/test_humanoid_ground.cpp              # PHYSICS: Humanoid ground support test
    tests/test_humanoid_ground_multitile.cpp    # PHYSICS: Humanoid multi-tile ground test
    tests/test_humanoid_rotation.cpp            # DYNAMICS: Humanoid rotation/look-at test
    tests/test_humanoid_lie_down.cpp            # DYNAMICS: Humanoid lie down/stand up test
    tests/test_hunter_rotation.cpp              # PHYSICS: Hunter stability test (Logomancers-style)
    tests/test_physics_experiment_03_eva_constraints.cpp  # PHYSICS v2: Eva body parts + constraints (incremental)
    tests/test_humanoid_impact.cpp              # PHYSICS: Humanoid impact response (projectile knockback)
    # TEMPORARILY DISABLED - use old grid API:
    # tests/test_basic_scenarios.cpp
    # tests/test_brightness_normalization.cpp
    # tests/test_shadow_casting.cpp
    # tests/test_wasd_compass_alignment.cpp
)

# Create test executable
add_executable(logosphere-tests ${TEST_SOURCES})

# Link with Logosphere library
target_link_libraries(logosphere-tests
    PRIVATE
        logosphere
        glfw
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
        "-framework Metal"
        "-framework MetalKit"
        "-framework MetalPerformanceShaders"
        "-framework QuartzCore"  # For CAMetalLayer only
)

# Include directories
target_include_directories(logosphere-tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

# Set compiler flags
target_compile_options(logosphere-tests PRIVATE -Wall -Wextra)

# =====================================
# STANDALONE ENGINE-DEPENDENT TESTS (non-headless)
# =====================================
# Tests that pull in the full engine (GLFW/Metal/physics). Headless
# tests live at the top of this file.

# NOTE: test_particle_shapes is registered above as a headless test
# (add_headless_test on line ~94). The geometry + narrow-phase sources it
# needs now live in logosphere_core after the Tier 1 modularity cleanup.

add_executable(test_dynamics_override tests/test_dynamics_override.cpp)
target_link_libraries(test_dynamics_override PRIVATE
    logosphere glfw
    "-framework Cocoa" "-framework IOKit" "-framework CoreVideo"
    "-framework Metal" "-framework MetalKit"
    "-framework MetalPerformanceShaders" "-framework QuartzCore"
)
target_include_directories(test_dynamics_override PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(test_dynamics_override PRIVATE cxx_std_17)

add_executable(test_damage_visual tests/test_damage_visual.cpp)
target_link_libraries(test_damage_visual PRIVATE
    logosphere glfw
    "-framework Cocoa" "-framework IOKit" "-framework CoreVideo"
    "-framework Metal" "-framework MetalKit"
    "-framework MetalPerformanceShaders" "-framework QuartzCore"
)
target_include_directories(test_damage_visual PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(test_damage_visual PRIVATE cxx_std_17)

add_executable(test_damage_pipeline tests/test_damage_pipeline.cpp)
target_link_libraries(test_damage_pipeline PRIVATE
    logosphere glfw
    "-framework Cocoa" "-framework IOKit" "-framework CoreVideo"
    "-framework Metal" "-framework MetalKit"
    "-framework MetalPerformanceShaders" "-framework QuartzCore"
)
target_include_directories(test_damage_pipeline PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(test_damage_pipeline PRIVATE cxx_std_17)

# Standalone animation test executables (moved from logomancers)
# (standalone animation tests moved above the physics-profile return —
#  they build render-free on Linux too)

# HTTP standalone tests
add_executable(test-http-standalone tests/test-http-standalone.cpp)
target_link_libraries(test-http-standalone PRIVATE logosphere)
target_include_directories(test-http-standalone PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(test-http-standalone PRIVATE cxx_std_17)

add_executable(test-http-after-engine tests/test-http-after-engine.cpp)
target_link_libraries(test-http-after-engine PRIVATE
    logosphere glfw
    "-framework Cocoa" "-framework IOKit" "-framework CoreVideo"
    "-framework Metal" "-framework MetalKit"
    "-framework MetalPerformanceShaders" "-framework QuartzCore"
)
target_include_directories(test-http-after-engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(test-http-after-engine PRIVATE cxx_std_17)
