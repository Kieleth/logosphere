cmake_minimum_required(VERSION 3.10)
project(logosphere VERSION 0.4.5)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# A bare `cmake -S . -B build` (the README line) must not produce an
# unoptimized engine: an empty build type compiles with no -O flags
# and runs the renderer ~8x slower, which reads as an engine
# regression. Default to Release; explicit -DCMAKE_BUILD_TYPE and
# multi-config generators are untouched.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    message(STATUS "No CMAKE_BUILD_TYPE given; defaulting to Release")
endif()

# MSVC: expose the M_PI constant family and keep <windows.h> from
# defining min/max macros that collide with std::min / std::max.
if(MSVC)
    add_compile_definitions(_USE_MATH_DEFINES NOMINMAX)
endif()

# Generate include/logosphere/build_info.h with the current git SHA,
# tag-describe, and configure timestamp. Needed by the telemetry /
# session-recording layer so saved artifacts can be correlated with
# the code that produced them. See cmake/build_info.cmake.
# CMAKE_CURRENT_SOURCE_DIR, not CMAKE_SOURCE_DIR: the latter is the
# TOP-LEVEL project, which is the consuming game whenever this engine
# is pulled in with FetchContent — the documented way to use it. That
# made every consumer look for <game>/cmake/build_info.cmake and fail
# to configure. The two are identical when the engine is built
# standalone, so this changes nothing for the engine's own builds.
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_info.cmake)

# Point this clone's git at the tracked .githooks/ directory (never
# rebase, never force-push, never push to main). Configure time is the
# install step because .git/hooks does not survive a clone and a
# documented "run this once" step does not survive an agent session.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/git_hooks.cmake)

# Vendored source corpora, and the function a game calls to DECLARE the
# one it reads. Included this early because targets defined below and
# targets defined in examples/*/CMakeLists.txt both need it.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/corpora.cmake)
logosphere_corpus_path(LOGOSPHERE_CEPHEUS_SRD cepheus-srd)

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
    src/core/physics_trace.cpp
    src/core/explosion_detector.cpp
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
    src/kg/kg_ops_transaction.cpp
    src/kg/finite_float.cpp
    src/kg/qualified_reference.cpp
    src/kg/ontology_meta_graph.cpp
    src/kg/ontology_validator.cpp
    # Source evidence: address exact fragments and seal corpus editions
    src/text/source_document.cpp
    src/text/source_locator.cpp
    src/text/source_target.cpp
    src/text/source_manifest.cpp
    src/text/source_corpus.cpp
    src/kg/ingestion_ledger.cpp
    # Seed files: envelope + aliased KG-ops loaded through the
    # validated write path, and the ingestion verifier (verbatim /
    # schema / value / semantic / invariants). docs/RPG_MODULE.md.
    src/kg/seed_loader.cpp
    src/kg/seed_verifier.cpp
    src/rules/outcome_executor.cpp
    src/rules/planned_world.cpp
    src/replay/run_recorder.cpp
    src/replay/run_tape.cpp
    src/rules/procedure_runner.cpp
    src/rules/rollable_table_runner.cpp
    src/rules/lookup_table_selector.cpp
    src/rules/task_check_runner.cpp
    src/rules/rule_fork.cpp
    src/rules/rule_static_type.cpp
    src/rules/rule_program_validator.cpp
    # Generated ontology registry (pure data)
    src/generated/logosphere_ontology_registry.cpp
    # Setting packs (schema/packs/*.yaml). Compiling a pack is NOT
    # loading it: the registry function has to be called before its
    # vocabulary exists in a world. Pure generated data, headless-safe.
    src/generated/space_ontology_registry.cpp
    src/generated/earth_ontology_registry.cpp
    src/generated/rule_language_ontology_registry.cpp
    src/generated/rulebook_ontology_registry.cpp
    # Capability system (engine aggregation + dynamics params + rules)
    src/capability/capability_profile.cpp
    src/capability/capability_store.cpp
    src/core/dice_service.cpp
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
    # Ground locator: where a body can be, asked of the world rather
    # than assumed. No z constant lives here or anywhere downstream.
    src/core/ground_locator.cpp
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
    # Contact response: the condition/effect registries an ON_CONTACT
    # rule dispatches through (issue #36). Pure KG + std.
    src/interaction/contact_response.cpp
    # NPC decision layer. Both are pure std with no physics, rendering
    # or platform dependency, so they belong in the headless core and
    # their guards run on the Linux CI profile.
    src/npc-ai/goap_system.cpp
    src/npc-ai/pathfinding_system.cpp
    # The execution layer: a plan of action NAMES becomes movement.
    # Pure std like the planner above (cmath/iostream/random/sstream), so
    # it belongs here rather than only in the full engine build, where the
    # Linux CI lane never saw it.
    src/npc-ai/executor_registry.cpp
    src/npc-ai/goap_plan_executor.cpp
    src/npc-ai/executors/pursue_executor.cpp
    src/npc-ai/executors/scan_executor.cpp
    src/npc-ai/executors/investigate_smell_executor.cpp
    src/npc-ai/executors/escape_block_executor.cpp
    src/npc-ai/executors/give_up_executor.cpp
    # EAT, GRAB_PREY and FoodState left the engine (#37 item 3): they are
    # creature-diet policy, not mechanism, and one of them named a
    # specific game monster in its header. They live in
    # examples/predator/ai/ now and register through the same
    # ExecutorRegistry as any game behaviour.
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
add_library(logosphere::events ALIAS logosphere_events)
set_target_properties(logosphere_events PROPERTIES EXPORT_NAME events)
target_include_directories(logosphere_events INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(logosphere_events INTERFACE cxx_std_17)

add_library(logosphere_core STATIC ${LOGOSPHERE_CORE_SOURCES})
add_library(logosphere::core ALIAS logosphere_core)
set_target_properties(logosphere_core PROPERTIES EXPORT_NAME core)
target_link_libraries(logosphere_core PUBLIC logosphere_events)
target_include_directories(logosphere_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    $<INSTALL_INTERFACE:include/logosphere/internal>
)
# nlohmann/json is header-only; only kg_ops_parse.cpp and
# seed_loader.cpp use it.
target_include_directories(logosphere_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)
target_compile_features(logosphere_core PUBLIC cxx_std_17)

# ============================================================================
# Install / export — find_package(logosphere) consumption
# ============================================================================
# Installs the headless core: logosphere::core (KG, ontology, events,
# capability, damage, game time, particle store, geometry) and the
# header-only logosphere::events. The rendering / physics / platform
# stack is not installable yet; games needing it build in-tree.
#
# Layout: the curated API goes to include/logosphere/. The public
# headers additionally depend on a small closure of engine-internal
# headers (particle family, materials, math/quat, generated ontology);
# those install under include/logosphere/internal/, which is on the
# exported include path so in-tree include spellings keep working.
# Shrinking that internal surface is ongoing; nothing in it is a
# stability promise.
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS logosphere_core logosphere_events
    EXPORT logosphereTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/logosphere
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(FILES
    src/materials.h src/particle.h src/particle_core.h
    src/particle_geometry_v2.h src/particle_lighting.h
    src/particle_memory.h src/particle_types.h src/particle_vision.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/logosphere/internal
)
install(FILES src/core/particle_system.h src/core/game_time.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/logosphere/internal/core
)
install(FILES src/math/quat.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/logosphere/internal/math
)
install(FILES
    src/generated/logosphere_ontology.h
    src/generated/logosphere_ontology_registry.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/logosphere/internal/generated
)
install(EXPORT logosphereTargets
    NAMESPACE logosphere::
    FILE logosphereTargets.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/logosphere
)
configure_package_config_file(
    cmake/logosphereConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/logosphereConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/logosphere
)
# 0.x line: minor versions may break API, so only the same minor is
# compatible (find_package(logosphere 0.2) accepts 0.2.x, not 0.3).
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/logosphereConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/logosphereConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/logosphereConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/logosphere
)

# ============================================================================
# Headless test executables — always built, link only logosphere_core
# ============================================================================
# Each is its own executable with its own main(). They build on any
# standards-compliant C++17 toolchain. No engine state, no GPU, no GLFW.
enable_testing()

function(register_headless_test name)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

function(add_headless_test name)
    add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE logosphere_core)
    target_compile_features(${name} PRIVATE cxx_std_17)
    register_headless_test(${name})
endfunction()

# The predator example's AT. Not add_headless_test because the example's
# diet executors are GAME sources compiled into the AT itself, which is
# exactly how a game consumes the engine (#37 item 3): the engine core
# does not contain them.
add_executable(at_predator_hunger
    examples/predator/at_predator_hunger.cpp
    examples/predator/ai/eat_executor.cpp
    examples/predator/ai/grab_prey_executor.cpp
)
target_link_libraries(at_predator_hunger PRIVATE logosphere_core)
target_compile_features(at_predator_hunger PRIVATE cxx_std_17)
target_include_directories(at_predator_hunger PRIVATE examples/predator)
add_test(NAME at_predator_hunger COMMAND at_predator_hunger)

add_headless_test(test_goap_planner)  # the NPC planner, previously untested
add_headless_test(test_navgrid_pathfinding)  # the NPC A* grid, previously untested
add_headless_test(test_event_log)
add_headless_test(test_planned_world)  # reading a plan before it commits
add_headless_test(test_run_recorder)   # the run, as a diffable trace
add_headless_test(test_run_tape)       # recorded, replayed, or invented
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
add_headless_test(test_kg_property_gate)  # Malleus H1: setProperty ontology gate
add_headless_test(test_kg_ops_transaction)
add_headless_test(test_qualified_reference)
add_headless_test(test_ontology_meta_graph)
add_headless_test(test_rule_static_type)
add_headless_test(test_rule_bindings)
add_headless_test(test_outcome_executor)
add_headless_test(test_procedure_runner)
add_headless_test(test_rollable_table_runner)
add_headless_test(test_lookup_table_selector)
add_headless_test(test_task_check_runner)
# test_mutation_playback links logosphere_core only (no Engine
# dereferencing in the tests; dummy pointers in fixtures).
add_executable(test_mutation_playback
    tests/test_mutation_playback.cpp
    src/effects/mutation_playback.cpp
)
target_link_libraries(test_mutation_playback PRIVATE logosphere_core)
target_compile_features(test_mutation_playback PRIVATE cxx_std_17)
register_headless_test(test_mutation_playback)
add_headless_test(test_body_plan)
add_headless_test(test_game_time)
add_headless_test(test_material_properties)  # mechanical constants must be physically possible
add_headless_test(test_inv29_constants_gate)  # INV-29 static gate: magic-float ratchet over physics TUs
add_headless_test(test_inv15_owner_blindness)  # INV-15 static prover: counts the task-#43 owner reads
add_headless_test(test_kg_setproperty_events)
add_headless_test(test_relation_events)
add_headless_test(test_kg_parse_safety)
add_headless_test(test_particle_shapes)
add_headless_test(test_collision_bounds_rotation)  # which shapes get oriented bounds, and which do not
add_headless_test(test_manifold_reduction)  # G-51: the reduced point set spans the face (born red)
add_headless_test(test_iso_azimuth)
add_headless_test(test_ontology_packs)
add_headless_test(test_entity_type_strings)
# This one reads the source tree, so it needs to know where that is:
# ctest runs from the build directory, not the repo root.
target_compile_definitions(test_entity_type_strings
    PRIVATE LOGOSPHERE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_headless_test(test_source_locator)
# Resolves locators against the vendored SRD in the tree.
target_compile_definitions(test_source_locator
    PRIVATE LOGOSPHERE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_headless_test(test_source_target)
add_headless_test(test_source_manifest)
add_headless_test(test_source_corpus)
add_headless_test(test_ingestion_ledger)
add_headless_test(test_rulebook_pack)
add_headless_test(test_rule_fork)
# The pointer lands where the widget is drawn: window points into the
# UI's render grid, pinned after a game rendering smaller than its
# window put every click short by the scale (2026-09-02).
add_headless_test(test_ui_space)
# A widget holding focus does not swallow the keyboard: it gets first
# refusal and the game hears the rest. A click focuses whatever it
# lands on, so the old rule went deaf, escape included, on the first
# click on a list (2026-09-01).
add_headless_test(test_key_routing)
# Verbatim-checks its citations against the vendored SRD in the tree.
target_compile_definitions(test_rulebook_pack
    PRIVATE LOGOSPHERE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_executable(test_seed_verifier
    tests/test_seed_verifier.cpp
    examples/logovger/src/generated/cepheus_book1_skills_ontology_registry.cpp
    examples/logovger/src/generated/cepheus_book1_character_creation_ontology_registry.cpp
)
target_link_libraries(test_seed_verifier PRIVATE logosphere_core)
target_compile_features(test_seed_verifier PRIVATE cxx_std_17)
target_include_directories(test_seed_verifier PRIVATE examples/logovger/src)
register_headless_test(test_seed_verifier)
# Reads its seed fixtures + the vendored SRD from the source tree.
target_compile_definitions(test_seed_verifier
    PRIVATE LOGOSPHERE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# The ingestion verifier CLI accepts an ordered prerequisite chain before the
# target seed. Game seed files still verify through game-side registries.
# Links the ENGINE registries only (core + rulebook + earth + space,
# merged); game seed files with game types verify through game-side
# targets calling kg::verify_seed with their own registry.
add_executable(logosphere-verify tools/logosphere_verify.cpp)
target_link_libraries(logosphere-verify PRIVATE logosphere_core)
target_compile_features(logosphere-verify PRIVATE cxx_std_17)
# The CLI under ctest: a standalone seed passes, a misquote fails, and a
# cross-seed reference passes only when its prerequisite is supplied.
add_test(NAME logosphere_verify_positive
    COMMAND logosphere-verify
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/seed/rulebook_smoke.json
        ${LOGOSPHERE_CEPHEUS_SRD})
add_test(NAME logosphere_verify_negative
    COMMAND logosphere-verify
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/seed/negative_misquote.json
        ${LOGOSPHERE_CEPHEUS_SRD})
set_tests_properties(logosphere_verify_negative PROPERTIES WILL_FAIL TRUE)
add_test(NAME logosphere_verify_prerequisite
    COMMAND logosphere-verify
        --prerequisite
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/seed/prerequisite_base.json
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/seed/prerequisite_dependent.json
        ${LOGOSPHERE_CEPHEUS_SRD})
add_test(NAME logosphere_verify_missing_prerequisite
    COMMAND logosphere-verify
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/seed/prerequisite_dependent.json
        ${LOGOSPHERE_CEPHEUS_SRD})
set_tests_properties(logosphere_verify_missing_prerequisite
    PROPERTIES WILL_FAIL TRUE)

# Logovger with no window: a life played by an InputSource. Proves the
# replay harness on a real game, and gives a fuzzing entry point that
# needs no API key.
add_executable(logovger-headless
    examples/logovger/logovger_headless.cpp
    examples/logovger/chargen/chargen.cpp
    examples/logovger/src/generated/cepheus_book1_skills_ontology_registry.cpp
    examples/logovger/src/generated/cepheus_book1_character_creation_ontology_registry.cpp
)
target_link_libraries(logovger-headless PRIVATE logosphere_core)
target_compile_features(logovger-headless PRIVATE cxx_std_17)
target_include_directories(logovger-headless PRIVATE
    examples/logovger examples/logovger/src)
target_compile_definitions(logovger-headless PRIVATE
    LOGOVGER_GAME_DIR="${CMAKE_CURRENT_SOURCE_DIR}/examples/logovger")
# Declared, not derived: the book it cites is vendored once, outside
# every game, and read by name.
logosphere_game_corpus(logovger-headless cepheus-srd LOGOVGER)

# ---------------------------------------------------------------------
# Voyager: character creation up to the first door, and no further.
# ---------------------------------------------------------------------

# Voyager with no window. Shares every line of logic with the windowed
# game; only the answer source differs. --random needs no key, --record
# tapes what a model answered, --replay reproduces it offline.
set(VOYAGER_SHARED_SOURCES
    examples/voyager/src/session.cpp
    examples/voyager/src/sheet.cpp
    examples/voyager/src/generated/voyager_chargen_ontology_registry.cpp
)
add_executable(voyager-headless
    examples/voyager/voyager_headless.cpp
    ${VOYAGER_SHARED_SOURCES}
)
target_link_libraries(voyager-headless PRIVATE logosphere_core)
target_compile_features(voyager-headless PRIVATE cxx_std_17)
target_include_directories(voyager-headless PRIVATE
    examples/voyager examples/voyager/src)
target_compile_definitions(voyager-headless PRIVATE
    VOYAGER_GAME_DIR="${CMAKE_CURRENT_SOURCE_DIR}/examples/voyager")
# Declared, not derived: the book it cites is vendored once, outside
# every game, and read by name. A second game naming the same corpus
# reads the same bytes and adds nothing.
logosphere_game_corpus(voyager-headless cepheus-srd VOYAGER)
logosphere_game_corpus(voyager-headless voyager-book VOYAGER_BOOK)

# The claim this whole game is built to keep, measured rather than
# reviewed: the screen is assembled from the characteristics the GRAPH
# holds. It adds a seventh at run time and fails if the sheet does not
# grow. No network, no model, no window.
add_executable(test_characteristics_from_graph
    examples/voyager/test_characteristics_from_graph.cpp
    ${VOYAGER_SHARED_SOURCES}
)
target_link_libraries(test_characteristics_from_graph PRIVATE logosphere_core)
target_compile_features(test_characteristics_from_graph PRIVATE cxx_std_17)
target_include_directories(test_characteristics_from_graph PRIVATE
    examples/voyager examples/voyager/src)
target_compile_definitions(test_characteristics_from_graph PRIVATE
    VOYAGER_GAME_DIR="${CMAKE_CURRENT_SOURCE_DIR}/examples/voyager")
logosphere_game_corpus(test_characteristics_from_graph cepheus-srd VOYAGER)
logosphere_game_corpus(test_characteristics_from_graph voyager-book VOYAGER_BOOK)
register_headless_test(test_characteristics_from_graph)

# Voyager's rules, verified on the way in and then asked the questions
# the game asks them. Catches an extraction that has moved, a citation
# that no longer resolves, and a constant nothing reads.
add_executable(test_voyager_rules
    examples/voyager/test_voyager_rules.cpp
    ${VOYAGER_SHARED_SOURCES}
)
target_link_libraries(test_voyager_rules PRIVATE logosphere_core)
target_compile_features(test_voyager_rules PRIVATE cxx_std_17)
target_include_directories(test_voyager_rules PRIVATE
    examples/voyager examples/voyager/src)
target_compile_definitions(test_voyager_rules PRIVATE
    VOYAGER_GAME_DIR="${CMAKE_CURRENT_SOURCE_DIR}/examples/voyager")
logosphere_game_corpus(test_voyager_rules cepheus-srd VOYAGER)
logosphere_game_corpus(test_voyager_rules voyager-book VOYAGER_BOOK)
register_headless_test(test_voyager_rules)

# Voyager's own book, held to the schema and the graph: the closed
# vocabularies instantiated from both directions, the Unsettled section
# loaded as records, the book's words absent from shipping C++, and one
# edition per corpus so a rewritten chapter changes what a tape pins.
add_executable(test_voyager_book
    examples/voyager/test_voyager_book.cpp
    ${VOYAGER_SHARED_SOURCES}
)
target_link_libraries(test_voyager_book PRIVATE logosphere_core)
target_compile_features(test_voyager_book PRIVATE cxx_std_17)
target_include_directories(test_voyager_book PRIVATE
    examples/voyager examples/voyager/src)
target_compile_definitions(test_voyager_book PRIVATE
    VOYAGER_GAME_DIR="${CMAKE_CURRENT_SOURCE_DIR}/examples/voyager")
logosphere_game_corpus(test_voyager_book cepheus-srd VOYAGER)
logosphere_game_corpus(test_voyager_book voyager-book VOYAGER_BOOK)
register_headless_test(test_voyager_book)

# Voyager's screen, measured rather than looked at. Three headless
# executables over the three pure pieces the windowed game is built on:
# where every rectangle lands at every type size, how text is broken
# into lines, and how a list that outgrows its panel scrolls. None of
# them links a widget, a draw surface or a window; that is the point,
# and it is why a layout defect fails in CI instead of in a screenshot.
foreach(voyager_ui_test test_voyager_layout test_voyager_text
                        test_voyager_doors)
    add_executable(${voyager_ui_test} examples/voyager/${voyager_ui_test}.cpp)
    target_link_libraries(${voyager_ui_test} PRIVATE logosphere_core)
    target_compile_features(${voyager_ui_test} PRIVATE cxx_std_17)
    target_include_directories(${voyager_ui_test} PRIVATE
        examples/voyager examples/voyager/src)
    register_headless_test(${voyager_ui_test})
endforeach()

# Logovger: the referee's answer, held to the rule that asked for it.
# Deliberately free of any LLM dependency (logosphere_llm is defined
# below the headless short-circuit), because this is the check that
# stops a loose reply widening a rule, and it has to run everywhere.
add_executable(test_judgment_answer
    examples/logovger/test_judgment_answer.cpp
    examples/logovger/src/judgment_answer.cpp
)
target_compile_features(test_judgment_answer PRIVATE cxx_std_17)
target_include_directories(test_judgment_answer PRIVATE examples/logovger)
register_headless_test(test_judgment_answer)

# Logovger: absorbed rules proven against the book's own numbers.
# Game sources compiled into the test, engine as a library.
add_executable(test_logovger_rules_book1
    examples/logovger/test_rules_book1.cpp
)
target_link_libraries(test_logovger_rules_book1 PRIVATE logosphere_core)
target_compile_features(test_logovger_rules_book1 PRIVATE cxx_std_17)
target_include_directories(test_logovger_rules_book1 PRIVATE examples/logovger)
add_test(NAME test_logovger_rules_book1 COMMAND test_logovger_rules_book1)
# Skills pack contract: game registries compiled into the test, engine
# as a library; reads the vendored SRD for citation checks.
add_executable(test_skills_pack
    examples/logovger/test_skills_pack.cpp
    examples/logovger/src/generated/cepheus_book1_skills_ontology_registry.cpp
    examples/logovger/src/generated/cepheus_book1_character_creation_ontology_registry.cpp
)
target_link_libraries(test_skills_pack PRIVATE logosphere_core)
target_compile_features(test_skills_pack PRIVATE cxx_std_17)
target_include_directories(test_skills_pack PRIVATE examples/logovger
    examples/logovger/src)
target_compile_definitions(test_skills_pack
    PRIVATE LOGOSPHERE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
logosphere_game_corpus(test_skills_pack cepheus-srd LOGOVGER)
add_test(NAME test_skills_pack COMMAND test_skills_pack)
# Typed lookup rows and rollable outcomes: game-declared result columns,
# verified through the same merged registry Logovger loads.
add_executable(test_logovger_table_results
    examples/logovger/test_table_results.cpp
    examples/logovger/src/generated/cepheus_book1_skills_ontology_registry.cpp
    examples/logovger/src/generated/cepheus_book1_character_creation_ontology_registry.cpp
)
target_link_libraries(test_logovger_table_results PRIVATE logosphere_core)
target_compile_features(test_logovger_table_results PRIVATE cxx_std_17)
target_include_directories(test_logovger_table_results PRIVATE
    examples/logovger examples/logovger/src)
target_compile_definitions(test_logovger_table_results
    PRIVATE LOGOSPHERE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
logosphere_game_corpus(test_logovger_table_results cepheus-srd LOGOVGER)
add_test(NAME test_logovger_table_results COMMAND test_logovger_table_results)
# The first playable slice: a life generated from seeded rules.
add_executable(test_chargen
    examples/logovger/test_chargen.cpp
    examples/logovger/chargen/chargen.cpp
    examples/logovger/src/generated/cepheus_book1_skills_ontology_registry.cpp
    examples/logovger/src/generated/cepheus_book1_character_creation_ontology_registry.cpp
)
target_link_libraries(test_chargen PRIVATE logosphere_core)
target_compile_features(test_chargen PRIVATE cxx_std_17)
target_include_directories(test_chargen PRIVATE examples/logovger
    examples/logovger/src)
target_compile_definitions(test_chargen
    PRIVATE LOGOSPHERE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
logosphere_game_corpus(test_chargen cepheus-srd LOGOVGER)
add_test(NAME test_chargen COMMAND test_chargen)

add_executable(test_logovger_rule_seed_identity
    examples/logovger/test_rule_seed_identity.cpp
    examples/logovger/src/generated/cepheus_book1_skills_ontology_registry.cpp
    examples/logovger/src/generated/cepheus_book1_character_creation_ontology_registry.cpp
)
target_link_libraries(test_logovger_rule_seed_identity PRIVATE logosphere_core)
target_compile_features(test_logovger_rule_seed_identity PRIVATE cxx_std_17)
target_include_directories(test_logovger_rule_seed_identity PRIVATE
    examples/logovger examples/logovger/src)
target_compile_definitions(test_logovger_rule_seed_identity
    PRIVATE LOGOSPHERE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
logosphere_game_corpus(test_logovger_rule_seed_identity cepheus-srd LOGOVGER)
register_headless_test(test_logovger_rule_seed_identity)

# Pure arithmetic: where the panels sit. No engine, no GPU, no window.
# It catches the failures a screenshot cannot, starting with a window
# too wide for the display to show at all.
add_executable(test_screen_layout examples/logovger/test_screen_layout.cpp)
target_compile_features(test_screen_layout PRIVATE cxx_std_17)
target_include_directories(test_screen_layout PRIVATE examples/logovger)
add_test(NAME test_screen_layout COMMAND test_screen_layout)

# The classification audit cannot be quietly skipped: this compares the
# shipped seed against the shipped audit and fails when extraction has
# moved and the audit has not been re-run. No network; it reads two
# files that are both in the repository.
add_executable(test_classification_audit
    examples/logovger/test_classification_audit.cpp)
target_compile_features(test_classification_audit PRIVATE cxx_std_17)
target_compile_definitions(test_classification_audit
    PRIVATE LOGOSPHERE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME test_classification_audit COMMAND test_classification_audit)

add_headless_test(test_dice_service)
add_headless_test(test_capability_store)
add_headless_test(test_kg_reverse_relations)
add_headless_test(test_interaction_ontology)
add_headless_test(test_interaction_rule_loading)
add_headless_test(test_contact_producer)
add_headless_test(test_contact_bounce)
add_headless_test(test_rule_order_determinism)  # two rules, one contact
add_headless_test(test_goap_executors)
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
    # Parallel-safety guard: a headless Engine must never call
    # glfwInit(), because on Cocoa that runs [NSApp run] and blocks on
    # the session-global window-server launch handshake. That is what
    # made `ctest -j 8` deadlock on a different test each run.
    add_engine_at(test_headless_no_window_server)
    add_engine_at(test_trail_fade_render)
    add_engine_at(test_celestial_moon)   # moon factory preset (pure config)
    add_engine_at(test_strata_earth_impact)  # earth preset + boulder crater contract
    add_engine_at(test_iso_azimuth_roundtrip)  # orbit picking round-trip (issue #9)
    add_engine_at(test_orbit_performance)  # orbit frame-cost study + post-orbit ratchet
    add_engine_at(test_ontology_levers)  # KG levers reach the bodies (issue #8)
    add_engine_at(test_ground_locator)  # placement asks the world, never assumes z
    add_engine_at(test_humanoid_terrain_scenarios)  # one walker, one feature at a time
    add_engine_at(test_activation_queue_stale_ids)  # queued ids can die before processing (#21)
    add_engine_at(test_predator_senses)  # sense_system: what a creature can and cannot know
    add_engine_at(test_predator_hunt)  # senses + pathfinding over time: it loses the prey
    # The hunger loop ON SCREEN (visual-verifiability rule): same GOAP
    # loop as at_predator_hunger, in a real scene, carcass shrinking
    # bite by bite, AI panel asserted as pixels. Game sources compiled
    # in, engine consumed as a library — the boundary, watchable.
    add_executable(at_predator_hunger_visual
        examples/predator/at_predator_hunger_visual.cpp
        examples/predator/ai/eat_executor.cpp
        examples/predator/ai/grab_prey_executor.cpp
    )
    target_link_libraries(at_predator_hunger_visual
        PRIVATE
            logosphere
            "-framework Cocoa" "-framework IOKit" "-framework CoreVideo"
            "-framework CoreGraphics" "-framework AppKit"
            "-framework Foundation" "-framework QuartzCore"
            "-framework Metal" "-framework MetalKit"
            "-framework MetalPerformanceShaders"
    )
    target_include_directories(at_predator_hunger_visual PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/predator
    )
    target_compile_features(at_predator_hunger_visual PRIVATE cxx_std_17)
    add_test(NAME at_predator_hunger_visual COMMAND at_predator_hunger_visual)

    # The search: walls, meander, smell, getting lost — the full NPC
    # loop (senses -> facts -> plan -> executors -> replan) on screen.
    add_executable(at_predator_search
        examples/predator/at_predator_search.cpp
        examples/predator/ai/eat_executor.cpp
        examples/predator/ai/grab_prey_executor.cpp
        examples/predator/ai/meander_executor.cpp
        examples/predator/src/generated/predator_ontology_registry.cpp
    )
    target_link_libraries(at_predator_search
        PRIVATE
            logosphere
            "-framework Cocoa" "-framework IOKit" "-framework CoreVideo"
            "-framework CoreGraphics" "-framework AppKit"
            "-framework Foundation" "-framework QuartzCore"
            "-framework Metal" "-framework MetalKit"
            "-framework MetalPerformanceShaders"
    )
    target_include_directories(at_predator_search PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/predator
    )
    target_compile_features(at_predator_search PRIVATE cxx_std_17)
    add_test(NAME at_predator_search COMMAND at_predator_search)
    add_engine_at(test_knockback_scene)  # contact rule -> knockback against the REAL solver (#36)
    add_engine_at(test_tree_collapse_threshold)  # small trees must not be bare poles (#21)
    add_engine_at(test_tree_collapse_demo)  # shows WHY small trees collapsed (#21)
    add_engine_at(test_butterfly_flight_liveliness)  # flight is alive and above real ground
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

    # at_headless_run — the fixed-step, no-window driver every recorded
    # or repeatable run uses.
    add_executable(at_headless_run tests/at_headless_run.cpp)
    target_link_libraries(at_headless_run PRIVATE logosphere)
    target_compile_features(at_headless_run PRIVATE cxx_std_17)
    add_test(NAME at_headless_run COMMAND at_headless_run)

    # test_determinism_guards — three ways the engine used to answer the
    # same question differently. Full engine: Engine + worldgen.
    add_executable(test_determinism_guards tests/test_determinism_guards.cpp)
    target_link_libraries(test_determinism_guards PRIVATE logosphere)
    target_compile_features(test_determinism_guards PRIVATE cxx_std_17)
    add_test(NAME test_determinism_guards COMMAND test_determinism_guards)

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
register_headless_test(test_logotron_cycle)

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
register_headless_test(test_logotron_trails)

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
register_headless_test(test_logotron_collision)

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
register_headless_test(test_logotron_round)

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
register_headless_test(test_logotron_director)

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
register_headless_test(test_symbolic_refs)

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
register_headless_test(test_narrative_hint)

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
register_headless_test(test_logotron_ai)

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
    register_headless_test(at_logotron_${name})
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
register_headless_test(at_logotron_director_random_apply)

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
register_headless_test(at_logotron_wall_visibility)

# ============================================================================
# Short-circuit for LOGOSPHERE_HEADLESS_ONLY=ON builds (Linux / Windows CI)
# ============================================================================
# Everything below this point is the full macOS engine (rendering, physics,
# platform, worldgen, Metal shaders, examples, and their tests). Skipped
# entirely when HEADLESS is set.
if(LOGOSPHERE_CORE_ONLY)
    get_property(_headless_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    get_property(_headless_tests DIRECTORY PROPERTY TESTS)
    foreach(_target IN LISTS _headless_targets)
        if(_target MATCHES "^(test_|at_)" AND
           NOT _target IN_LIST _headless_tests)
            message(FATAL_ERROR
                "Headless test target '${_target}' is not registered with CTest")
        endif()
    endforeach()
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
    src/worldgen/planet_generator.cpp
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
target_compile_definitions(logosphere_worldgen PUBLIC HAS_LLAMA)

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
target_compile_definitions(logosphere_dynamics PUBLIC HAS_LLAMA)

# ---- logosphere_physics — XPBD solver, gluons, narrow-phase, BVH ----
set(LOGOSPHERE_PHYSICS_SOURCES
    src/core/physics_system_v4.cpp
    src/bvh.cpp
)
add_library(logosphere_physics STATIC ${LOGOSPHERE_PHYSICS_SOURCES})
target_link_libraries(logosphere_physics PUBLIC logosphere_core)
target_include_directories(logosphere_physics PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(logosphere_physics PUBLIC cxx_std_17)
target_compile_definitions(logosphere_physics PUBLIC HAS_LLAMA)

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

# KINEMATIC is a transient authority: when locomotion lets go of a
# humanoid, physics must get it back. Proves the corpse falls.
add_executable(test_humanoid_ragdoll tests/test_humanoid_ragdoll.cpp)
target_link_libraries(test_humanoid_ragdoll PRIVATE
    logosphere_dynamics logosphere_physics logosphere_core)
target_include_directories(test_humanoid_ragdoll PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(test_humanoid_ragdoll PRIVATE cxx_std_17)

# KNOCKBACK: momentum a braced (KINEMATIC) body refuses is booked and
# handed to whoever owns that body, not dropped.
add_executable(test_humanoid_knockback tests/test_humanoid_knockback.cpp)
target_link_libraries(test_humanoid_knockback PRIVATE
    logosphere_dynamics logosphere_physics logosphere_core)
target_include_directories(test_humanoid_knockback PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(test_humanoid_knockback PRIVATE cxx_std_17)

# The refusal ledger must hold what a braced body refused. It booked
# 2.5% of it until 2026-08-14; this is what keeps it honest.
add_executable(test_refused_momentum_ledger tests/test_refused_momentum_ledger.cpp)
# logosphere_core and logosphere_dynamics reference each other, and
# this test names neither directly, so GNU ld (left-to-right, one pass
# per archive) drops dynamics before core asks for
# notify_particle_swap. Apple's linker resolves it either way, which is
# why it built here and failed the Linux precheck. Repeat the archive.
target_link_libraries(test_refused_momentum_ledger PRIVATE
    logosphere_dynamics logosphere_physics logosphere_core
    logosphere_dynamics)
target_include_directories(test_refused_momentum_ledger PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(test_refused_momentum_ledger PRIVATE cxx_std_17)

# G-47, born red: a mirror-symmetric strike owes exactly zero spin at
# any speed; sequential per-point manifold solving seeds spin that a
# violent strike carries away (2.99 rad/s at 9 m/s). Green requires
# the manifold block solve. TDD instrument for INV-32's default flip.
add_executable(test_square_strike tests/test_square_strike.cpp)
target_link_libraries(test_square_strike PRIVATE
    logosphere_dynamics logosphere_physics logosphere_core
    logosphere_dynamics)
target_include_directories(test_square_strike PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(test_square_strike PRIVATE cxx_std_17)

# G-48, born red: a touching column and a statically-stable zigzag pile
# must STAND under default torque; the sequentially-seeded wobble pumps
# through stacked contacts instead of decaying (column bottom sinks
# 9.5 cm, pile collapses). Step-1 instrument of the 1-2-3 plan.
add_executable(test_stack_stands tests/test_stack_stands.cpp)
target_link_libraries(test_stack_stands PRIVATE
    logosphere_dynamics logosphere_physics logosphere_core
    logosphere_dynamics)
target_include_directories(test_stack_stands PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(test_stack_stands PRIVATE cxx_std_17)

# G-53's rung ladder, simplest first (owner KISS ruling 2026-08-27):
# one spinning cube, then the free passenger (transmission guaranteed),
# then the anchored carrier (the anchor wins). Single-purposed.
add_executable(test_torsion_transmission tests/test_torsion_transmission.cpp)
target_link_libraries(test_torsion_transmission PRIVATE
    logosphere_dynamics logosphere_physics logosphere_core
    logosphere_dynamics)
target_include_directories(test_torsion_transmission PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(test_torsion_transmission PRIVATE cxx_std_17)

# G-54's three verdicts: stand / FALL / stand. The overhung cube that
# must fall catches phantom support. Single-purposed.
add_executable(test_mixed_mass_stands tests/test_mixed_mass_stands.cpp)
target_link_libraries(test_mixed_mass_stands PRIVATE
    logosphere_dynamics logosphere_physics logosphere_core
    logosphere_dynamics)
target_include_directories(test_mixed_mass_stands PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(test_mixed_mass_stands PRIVATE cxx_std_17)

# THE LIMITS CAMPAIGN (G-57..G-62, owner ruling 2026-08-28): six
# single-purpose probes of the trio world's envelope - masses,
# frictions, sizes, footprints, a slip joint, a particle floor.
# Shared scene + evaluator in tests/scenes/scene_limits.h.
foreach(limits_probe ice anvil size footprint slipjoint floor)
    add_executable(test_limits_${limits_probe}
        tests/test_limits_${limits_probe}.cpp)
    target_link_libraries(test_limits_${limits_probe} PRIVATE
        logosphere_dynamics logosphere_physics logosphere_core
        logosphere_dynamics)
    target_include_directories(test_limits_${limits_probe} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_compile_features(test_limits_${limits_probe} PRIVATE cxx_std_17)
endforeach()

# D7, born red: what slows a spinning body down? Today, a constant.
# ANGULAR_DRAG is multiplied into omega four times a frame for every
# body in every surrounding, so a cube spinning in vacuum stops in a
# second. Measures GEDANKEN-25 and names GEDANKEN-27's blocker.
add_executable(test_angular_dissipation tests/test_angular_dissipation.cpp)
target_link_libraries(test_angular_dissipation PRIVATE
    logosphere_dynamics logosphere_physics logosphere_core
    logosphere_dynamics)
target_include_directories(test_angular_dissipation PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(test_angular_dissipation PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_compile_features(test_angular_dissipation PRIVATE cxx_std_17)

# F2 made measurable: a cube and a sphere released on one ramp. Born red.
add_executable(test_ramp_race tests/test_ramp_race.cpp)
target_link_libraries(test_ramp_race PRIVATE
    logosphere_dynamics logosphere_physics logosphere_core logosphere_dynamics)
target_include_directories(test_ramp_race PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(test_ramp_race PRIVATE cxx_std_17)

# The rotation drop ladder: G-35/36/37 made executable. Born red.
add_executable(test_cube_drop_ladder tests/test_cube_drop_ladder.cpp)
target_link_libraries(test_cube_drop_ladder PRIVATE
    logosphere_dynamics logosphere_physics logosphere_core logosphere_dynamics)
target_include_directories(test_cube_drop_ladder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(test_cube_drop_ladder PRIVATE cxx_std_17)

# Orientation-truth instruments (G-19/20/23), written before the engine
# changes. Born red on the Euler twin.
add_executable(test_orientation_truth tests/test_orientation_truth.cpp)
target_link_libraries(test_orientation_truth PRIVATE
    logosphere_dynamics logosphere_physics logosphere_core logosphere_dynamics)
target_include_directories(test_orientation_truth PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(test_orientation_truth PRIVATE cxx_std_17)

# G-38's KINEMATIC half: a walking humanoid's bones keep both ledgers.
add_executable(test_humanoid_orientation_coherence tests/test_humanoid_orientation_coherence.cpp)
target_link_libraries(test_humanoid_orientation_coherence PRIVATE
    logosphere_dynamics logosphere_physics logosphere_core logosphere_dynamics)
target_include_directories(test_humanoid_orientation_coherence PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(test_humanoid_orientation_coherence PRIVATE cxx_std_17)

if(LOGOSPHERE_FULL)   # full profile only; see the visual-driver gate note
    add_executable(test_orientation_truth_visual tests/test_orientation_truth_visual.cpp)
    target_link_libraries(test_orientation_truth_visual PRIVATE
        logosphere glfw
        "-framework Cocoa" "-framework IOKit" "-framework CoreVideo"
        "-framework CoreGraphics" "-framework AppKit"
        "-framework Foundation" "-framework QuartzCore"
        "-framework Metal" "-framework MetalKit"
        "-framework MetalPerformanceShaders")
    target_include_directories(test_orientation_truth_visual PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(test_orientation_truth_visual PRIVATE cxx_std_17)
endif()

if(LOGOSPHERE_FULL)   # full profile only; see the visual-driver gate note
    add_executable(test_cube_drop_ladder_visual tests/test_cube_drop_ladder_visual.cpp)
    target_link_libraries(test_cube_drop_ladder_visual PRIVATE
        logosphere glfw
        "-framework Cocoa" "-framework IOKit" "-framework CoreVideo"
        "-framework CoreGraphics" "-framework AppKit"
        "-framework Foundation" "-framework QuartzCore"
        "-framework Metal" "-framework MetalKit"
        "-framework MetalPerformanceShaders")
    target_include_directories(test_cube_drop_ladder_visual PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(test_cube_drop_ladder_visual PRIVATE cxx_std_17)
endif()

if(LOGOSPHERE_FULL)   # full profile only; see the gate note above
    add_executable(test_torsion_visual tests/test_torsion_visual.cpp)
    target_link_libraries(test_torsion_visual PRIVATE
        logosphere glfw
        "-framework Cocoa" "-framework IOKit" "-framework CoreVideo"
        "-framework CoreGraphics" "-framework AppKit"
        "-framework Foundation" "-framework QuartzCore"
        "-framework Metal" "-framework MetalKit"
        "-framework MetalPerformanceShaders")
    target_include_directories(test_torsion_visual PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(test_torsion_visual PRIVATE cxx_std_17)

    # The limits campaign's window (G-57..62): all cases, SPACE advances.
    add_executable(test_limits_visual tests/test_limits_visual.cpp)
    target_link_libraries(test_limits_visual PRIVATE
        logosphere glfw
        "-framework Cocoa" "-framework IOKit" "-framework CoreVideo"
        "-framework CoreGraphics" "-framework AppKit"
        "-framework Foundation" "-framework QuartzCore"
        "-framework Metal" "-framework MetalKit"
        "-framework MetalPerformanceShaders")
    target_include_directories(test_limits_visual PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(test_limits_visual PRIVATE cxx_std_17)
endif()

if(LOGOSPHERE_FULL)   # full profile only; see the gate note above
    add_executable(test_stack_stands_visual tests/test_stack_stands_visual.cpp)
    target_link_libraries(test_stack_stands_visual PRIVATE
        logosphere glfw
        "-framework Cocoa" "-framework IOKit" "-framework CoreVideo"
        "-framework CoreGraphics" "-framework AppKit"
        "-framework Foundation" "-framework QuartzCore"
        "-framework Metal" "-framework MetalKit"
        "-framework MetalPerformanceShaders")
    target_include_directories(test_stack_stands_visual PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(test_stack_stands_visual PRIVATE cxx_std_17)
endif()

if(LOGOSPHERE_FULL)   # full profile only; see the gate note above
    add_executable(test_ramp_race_visual tests/test_ramp_race_visual.cpp)
    target_link_libraries(test_ramp_race_visual PRIVATE
        logosphere glfw
        "-framework Cocoa" "-framework IOKit" "-framework CoreVideo"
        "-framework CoreGraphics" "-framework AppKit"
        "-framework Foundation" "-framework QuartzCore"
        "-framework Metal" "-framework MetalKit"
        "-framework MetalPerformanceShaders")
    target_include_directories(test_ramp_race_visual PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(test_ramp_race_visual PRIVATE cxx_std_17)
endif()

# Visual driver: SEPARATE FILE, SAME SCENE. tests/scenes/scene_spinning_cube.h
# owns the bodies and the stepping; neither driver holds a body or a
# threshold, so the two cannot drift apart. FULL PROFILE ONLY, and the
# gate must be LOGOSPHERE_FULL: NOT LOGOSPHERE_HEADLESS_ONLY only
# excludes the deprecated core alias, and the Linux physics profile
# walked through it and tried to compile a GLFW + Metal target
# (precheck caught it, 2026-08-19).
if(LOGOSPHERE_FULL)
    add_executable(test_angular_dissipation_visual tests/test_angular_dissipation_visual.cpp)
    target_link_libraries(test_angular_dissipation_visual PRIVATE
        logosphere glfw
        "-framework Cocoa" "-framework IOKit" "-framework CoreVideo"
        "-framework CoreGraphics" "-framework AppKit"
        "-framework Foundation" "-framework QuartzCore"
        "-framework Metal" "-framework MetalKit"
        "-framework MetalPerformanceShaders")
    target_include_directories(test_angular_dissipation_visual PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(test_angular_dissipation_visual PRIVATE cxx_std_17)
endif()

# INV-6's first witness: the same press on three axes must resolve the
# same, or the solver knows which way is down.
add_executable(test_inv6_gravity_blindness tests/test_inv6_gravity_blindness.cpp)
target_link_libraries(test_inv6_gravity_blindness PRIVATE
    logosphere_dynamics logosphere_physics logosphere_core
    logosphere_dynamics)
target_include_directories(test_inv6_gravity_blindness PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(test_inv6_gravity_blindness PRIVATE cxx_std_17)

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
    # NOTE: physics_system_v4 / narrow_phase / bvh live in
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
    src/replay/headless_run.cpp
    src/assembly/rigid_assembly.cpp  # Rigid multi-particle entity primitive
    # LLM sources now live in the logosphere_llm STATIC target (see below).
    src/core/telemetry.cpp
    src/core/physics_trace.cpp
    src/core/explosion_detector.cpp
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
    src/npc-ai/executors/scan_executor.cpp
    src/npc-ai/executors/investigate_smell_executor.cpp
    src/npc-ai/executors/escape_block_executor.cpp
    src/npc-ai/executors/give_up_executor.cpp
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
    # Setting packs (schema/packs/*.yaml). Compiling a pack is NOT
    # loading it: the registry function has to be called before its
    # vocabulary exists in a world. Pure generated data, headless-safe.
    src/generated/space_ontology_registry.cpp
    src/generated/earth_ontology_registry.cpp
    src/generated/rulebook_ontology_registry.cpp
    # Spatial query module
    src/spatial/entity_spatial.cpp
    # Damage system
    src/damage/damage_system.cpp
    src/damage/damage_particle_bridge.cpp  # apply_to_particle → EntitySpatialQuery bridge
    # Ontology levers → particles. Needs ParticleSystem + PhysicsSystem,
    # so it is a full-engine bridge rather than headless core, for the
    # same reason as damage_particle_bridge.cpp above.
    src/kg/entity_physical_state.cpp
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

# The headless Logovger gains an LLM PLAYER where the LLM library
# exists. Added to the existing target rather than duplicating it, so
# the core profile keeps a full life that plays with no API key at all
# (--random) and the full profile also gets --record, where the model
# plays the character and every answer is taped.
target_sources(logovger-headless PRIVATE
    examples/logovger/src/adjudicator.cpp
    examples/logovger/src/judgment_answer.cpp
)
target_compile_definitions(logovger-headless PRIVATE LOGOVGER_WITH_LLM)
target_link_libraries(logovger-headless PRIVATE logosphere_llm)

# The headless Voyager gains its referee where the LLM library exists.
# Without it the target still builds and --random still exercises the
# rules with no key; --record says plainly that this build has no model
# rather than quietly playing without one.
target_sources(voyager-headless PRIVATE
    examples/voyager/src/model_referee.cpp
)
target_compile_definitions(voyager-headless PRIVATE VOYAGER_WITH_LLM)
target_link_libraries(voyager-headless PRIVATE logosphere_llm)

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
target_compile_definitions(logosphere_rendering PUBLIC HAS_LLAMA)

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
    tests/test_humanoid_strata_integrity.cpp
    tests/test_spider_eva_shin_crush.cpp
    tests/test_joint_hierarchy_swap_integrity.cpp
    tests/test_reverse_leg_chain.cpp
    tests/test_phase_e_diagnostic.cpp
    tests/test_position_authority.cpp
    tests/test_deferred_deletion_integrity.cpp
    tests/test_pin_anchor_persistence.cpp
    tests/test_turtle_ground_support.cpp
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
add_subdirectory(examples/logovger)
add_subdirectory(examples/voyager)
add_subdirectory(examples/minimal)
add_subdirectory(examples/logomanpac)
add_subdirectory(examples/logotriste)

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
    tests/test_shadow_bvh_lever.cpp            # Shadow-BVH rebuild interval, visible
    tests/test_shadow_flat_bvh_necessity.cpp   # Is the flat shadow BVH ever read?
    tests/test_shadow_accel_backend.cpp        # Shadow accel backend seam: dormancy + fallback
    tests/test_gpu_occupancy_sanity.cpp        # GPU occupancy: union not sum, cannot exceed 100%
    tests/test_async_prep_equivalence.cpp      # Async GPU prep must not change a single pixel
    tests/test_shadow_lod_wall.cpp             # LOD judged on the SHADOW, not the object
    tests/test_physics_characterization.cpp    # refactor net: pins solver output on a pile
    tests/test_humanoid_tuning_coverage.cpp   # refactor net: which declared joint values survive to frame one
    tests/test_solver_convergence_ladder.cpp   # where does the solve first give up?
    tests/test_solver_residual.cpp             # how wrong is the answer the solve stopped at?
    tests/test_constraint_order_matters.cpp    # S19 re-run: does constraint order change it?
    tests/test_physics_battery.cpp             # scenario ladder: one mechanism per rung
    tests/test_rotation_ladder.cpp             # rotation TDD: red first, one rung at a time
    tests/test_rube_goldberg_machine.cpp       # one chain reaction, every stage an invariant
    tests/test_sleep_wake_resolver.cpp         # INV-31 red-first: twin-scene invisibility
    tests/test_grass_natures.cpp               # the palette on real grass + human over a blade
    tests/test_settling.cpp                    # focused TDD: wiggle-free rest, and resting ON the ground
    tests/test_async_prep_churn.cpp            # issue #31: KG lookup must stay on the frame thread
    tests/test_foliage_stays_attached.cpp      # issue #38: do leaves stay bonded to their branch?
    tests/test_no_overlap_at_creation.cpp      # issue #38 RED: nothing created inside something else
    tests/test_tree_repair_visual.cpp          # issue #38: broken vs fixed tree side by side, INTERACTIVE=1
    tests/test_explosion_detector.cpp          # issue #42: silent when quiet, fires on detonation, names the body
    tests/test_butterfly_detonation.cpp        # issue #47: solver divergence on gram-scale bodies, ladder
    tests/test_grass_holds_together.cpp        # issue #47 fix 1: bonded grass, no detonation, bounded drift
    tests/test_plants_are_rooted.cpp          # ISSUE #47: every body in a plant reaches something immobile
    tests/test_grass_bends_not_tears.cpp     # ISSUE #47: a pass must bend grass, not sever it
    tests/test_light_body_ringing.cpp        # SOLVER: a bond between unequal masses must not amplify
    tests/test_tree_bonds_born_at_rest.cpp   # ISSUE #38: a structure must be born at rest
    tests/test_branch_placement_ladder.cpp  # ISSUE #57: branch placement vs bond rest, rung by rung
    tests/test_baumgarte_ratchet.cpp          # SOLVER: position repair must not inject momentum
    tests/test_blockers_visual.cpp             # issue #47: both merge blockers on screen, INTERACTIVE=1
    tests/test_walk_through_grass.cpp          # issue #47: a human walks through grasses, all asserted good
    tests/test_divergence_microscope.cpp       # issue #47: freeze on the crime scene, single-step ticks
    tests/test_grass_yields.cpp                # owner: grass must bend gracefully; stuck-ness instrumented
    tests/test_single_blade_contact.cpp        # one blade, one human: the contact, studied end to end
    tests/test_immovable_pair_phantom_impulse.cpp # TDD RED: immovable pairs must not produce work
    tests/test_rotated_box_contact.cpp         # OBB narrow phase: contact matches the rotated geometry
    tests/test_ui_label_actually_renders.cpp   # does a registered widget put pixels on screen?
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
    tests/test_humanoid_strata_integrity.cpp     # PHYSICS: body stays assembled while strata stream (NOT locomotion)
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
    tests/test_drive_gravity_ff.cpp # PHYSICS-DRIVE: gravity compensation ladder (task #41)
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
    tests/test_turtle_ground_support.cpp          # PHYSICS: bare turtle plane supports a walker (task #42 CLASS-1)
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

# ---------------------------------------------------------------------------
# A hung test must FAIL, not wait forever.
#
# Written from an actual incident: three ctest runs sat wedged on engine
# acceptance tests for up to three hours, and a fourth wedged on a
# different one. The whole suite finishes in about twelve seconds, so any
# single test still running after two minutes is not slow, it is stuck.
#
# Nothing here had a TIMEOUT property and ctest's Linux lane has no step
# timeout either, so in CI the same hang would hold a runner until
# GitHub's six-hour ceiling and report nothing useful. Two minutes is
# roughly ten times the entire suite: generous for a slow machine,
# decisive against a deadlock.
#
# Applied last, so it covers every test registered above however it was
# added, and it never overrides a timeout a test set deliberately.
# ---------------------------------------------------------------------------
get_property(logosphere_all_tests DIRECTORY PROPERTY TESTS)
foreach(logosphere_test IN LISTS logosphere_all_tests)
    get_test_property(${logosphere_test} TIMEOUT logosphere_existing_timeout)
    if(NOT logosphere_existing_timeout)
        set_tests_properties(${logosphere_test} PROPERTIES TIMEOUT 120)
    endif()
endforeach()
