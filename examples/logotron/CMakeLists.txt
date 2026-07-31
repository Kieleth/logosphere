cmake_minimum_required(VERSION 3.10)

# Logotron — Tron-style light cycle arena with LLM Weirden Director.
# See LOGOTRON.md and DEVLOG.md.

set(LOGOTRON_SOURCES
    src/main.cpp
    src/cycle.cpp
    src/arena.cpp
    src/round.cpp
    src/llm_plan.cpp
    src/ai/head.cpp
    src/ai/perception.cpp
    src/ai/tactics/avoid_imminent_crash.cpp
    src/ai/tactics/maintain_direction.cpp
    src/ai/tactics/chase_seen_opponent.cpp
    src/ai/tactics/avoid_seen_opponent.cpp
    src/ai/tactics/flee_own_recent_trail.cpp
    src/ai/personality.cpp
    src/ai/decide.cpp
    src/ai/ai_instrument.cpp
    src/director/respawn_ai.cpp
    src/director/director_parser.cpp
    src/director/director.cpp
    src/director/random_director.cpp
    src/director/program_actor.cpp
    src/director/cinematic.cpp
    src/director/symbolic_refs.cpp
    src/director/narrative_hint.cpp
    src/director/playback_registrations.cpp
    src/hud/speed_dashboard.cpp
    src/hud/sight_occluders.cpp
    src/occluders.cpp
    src/generated/logotron_ontology_registry.cpp
)

add_executable(logotron ${LOGOTRON_SOURCES})

target_include_directories(logotron PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/generated
)

target_link_libraries(logotron
    PRIVATE
        logosphere
        glfw
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
        "-framework CoreGraphics"
        "-framework AppKit"
        "-framework Foundation"
        "-framework QuartzCore"
        "-framework Metal"
        "-framework MetalKit"
        "-framework MetalPerformanceShaders"
)

set_target_properties(logotron PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/logotron
)

if(APPLE)
    target_compile_options(logotron PRIVATE -Wall -Wextra)
endif()

target_compile_features(logotron PRIVATE cxx_std_17)

# ---------------------------------------------------------------------------
# bike_viewer — standalone visual test rig for the Logotron motorcycle.
# Single bike, rotatable, zoomable, proper lighting. Not a game — just an
# inspection harness so bike-shape iterations don't need a live round.
# ---------------------------------------------------------------------------
add_executable(bike_viewer
    src/bike_viewer.cpp
    src/generated/logotron_ontology_registry.cpp
)
target_include_directories(bike_viewer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/generated
)
target_link_libraries(bike_viewer
    PRIVATE
        logosphere
        glfw
        "-framework Cocoa" "-framework IOKit" "-framework CoreVideo"
        "-framework CoreGraphics" "-framework AppKit" "-framework Foundation"
        "-framework QuartzCore" "-framework Metal" "-framework MetalKit"
        "-framework MetalPerformanceShaders"
)
set_target_properties(bike_viewer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/logotron
)
if(APPLE)
    target_compile_options(bike_viewer PRIVATE -Wall -Wextra)
endif()
target_compile_features(bike_viewer PRIVATE cxx_std_17)

# ---------------------------------------------------------------------------
# logotron_trail_fade_lab — isolated visual test for the crashed-trail fade.
# Reproduces ONLY the fade (no Director/AI/respawn) with a bright control row
# next to the fading row, so the self-emissive-glow-vs-alpha question is a
# direct side-by-side read. Uses engine types only (no logotron ontology).
# ---------------------------------------------------------------------------
add_executable(logotron_trail_fade_lab
    viz/trail_fade_lab.cpp
)
target_include_directories(logotron_trail_fade_lab PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/generated
)
target_link_libraries(logotron_trail_fade_lab
    PRIVATE
        logosphere
        glfw
        "-framework Cocoa" "-framework IOKit" "-framework CoreVideo"
        "-framework CoreGraphics" "-framework AppKit" "-framework Foundation"
        "-framework QuartzCore" "-framework Metal" "-framework MetalKit"
        "-framework MetalPerformanceShaders"
)
set_target_properties(logotron_trail_fade_lab PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/logotron
)
if(APPLE)
    target_compile_options(logotron_trail_fade_lab PRIVATE -Wall -Wextra)
endif()
target_compile_features(logotron_trail_fade_lab PRIVATE cxx_std_17)

# ---------------------------------------------------------------------------
# sphere_lighting_test — minimal one-sphere + one-overhead-light scene used
# to TDD the rendering / shadow / lighting pipeline. Prints the expected
# per-point lighting values so a screenshot can be compared against what the
# engine SHOULD produce. See examples/logotron/src/sphere_lighting_test.cpp.
# ---------------------------------------------------------------------------
add_executable(sphere_lighting_test
    src/sphere_lighting_test.cpp
    src/generated/logotron_ontology_registry.cpp
)
target_include_directories(sphere_lighting_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/generated
)
target_link_libraries(sphere_lighting_test
    PRIVATE
        logosphere
        glfw
        "-framework Cocoa" "-framework IOKit" "-framework CoreVideo"
        "-framework CoreGraphics" "-framework AppKit" "-framework Foundation"
        "-framework QuartzCore" "-framework Metal" "-framework MetalKit"
        "-framework MetalPerformanceShaders"
)
set_target_properties(sphere_lighting_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/logotron
)
if(APPLE)
    target_compile_options(sphere_lighting_test PRIVATE -Wall -Wextra)
endif()
target_compile_features(sphere_lighting_test PRIVATE cxx_std_17)

# ---------------------------------------------------------------------------
# ellipsoid_lighting_test — same scene as sphere_lighting_test, but the test
# body is an ELLIPSOID with the bike-body aspect ratio. Used to isolate
# whether the "lit rim, dark interior" bug lives in the non-uniform scale
# rendering path or in the larger bike_viewer scene.
# ---------------------------------------------------------------------------
add_executable(ellipsoid_lighting_test
    src/ellipsoid_lighting_test.cpp
    src/generated/logotron_ontology_registry.cpp
)
target_include_directories(ellipsoid_lighting_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/generated
)
target_link_libraries(ellipsoid_lighting_test
    PRIVATE
        logosphere
        glfw
        "-framework Cocoa" "-framework IOKit" "-framework CoreVideo"
        "-framework CoreGraphics" "-framework AppKit" "-framework Foundation"
        "-framework QuartzCore" "-framework Metal" "-framework MetalKit"
        "-framework MetalPerformanceShaders"
)
set_target_properties(ellipsoid_lighting_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/logotron
)
if(APPLE)
    target_compile_options(ellipsoid_lighting_test PRIVATE -Wall -Wextra)
endif()
target_compile_features(ellipsoid_lighting_test PRIVATE cxx_std_17)
