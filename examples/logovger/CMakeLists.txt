# Logovger — character creation at a table, the Cepheus Engine rules
# read out of the knowledge graph. You answer the book's questions in
# the chat window; the engine rolls and records every die.
cmake_minimum_required(VERSION 3.10)

set(LOGOVGER_SOURCES
    src/main.cpp
    chargen/chargen.cpp
    src/sheet_screen.cpp
    src/career_list.cpp
    src/narrator.cpp
    src/generated/voyager_ontology_registry.cpp
    src/generated/cepheus_book1_skills_ontology_registry.cpp
    src/generated/cepheus_book1_character_creation_ontology_registry.cpp
)

add_executable(logovger ${LOGOVGER_SOURCES})

target_include_directories(logovger PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/kg
    ${CMAKE_SOURCE_DIR}/src/generated
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/generated
)

# The rules and the vendored source text are data the app reads at
# run time, so it needs to know where its own directory is.
target_compile_definitions(logovger PRIVATE
    LOGOVGER_GAME_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(logovger PRIVATE
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

set_target_properties(logovger PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/logovger
)

target_compile_features(logovger PRIVATE cxx_std_17)

# How long a player waits between a decision and the story about it.
# Headless, so it can be run against a local model or the API without
# a window, and compared.
add_executable(logovger-bench-narrator
    bench_narrator.cpp
    src/narrator.cpp
)
target_include_directories(logovger-bench-narrator PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(logovger-bench-narrator PRIVATE
    LOGOSPHERE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_features(logovger-bench-narrator PRIVATE cxx_std_17)
target_link_libraries(logovger-bench-narrator PRIVATE logosphere)
