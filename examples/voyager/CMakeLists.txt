# Voyager — one screen, one character, and it stops at the first door.
# The characteristics, the careers and their throws all come out of the
# knowledge graph; the dice come from the engine; the two things the
# rules leave open go to a referee. Nothing in this target's C++ knows
# what a characteristic is called.
cmake_minimum_required(VERSION 3.10)

add_executable(voyager
    src/main.cpp
    src/session.cpp
    src/sheet.cpp
    src/screen.cpp
    src/model_referee.cpp
    src/generated/voyager_chargen_ontology_registry.cpp
)

target_include_directories(voyager PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/generated
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/generated
)

# The rules and the referee's brief are the game's own data, so it needs
# to know where its own directory is.
target_compile_definitions(voyager PRIVATE
    VOYAGER_GAME_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# The vendored book is NOT the game's. It is bytes as published,
# vendored once under corpora/ and read by whoever cites them, so the
# game DECLARES the corpus it reads BY NAME rather than deriving a path
# from its own directory.
logosphere_game_corpus(voyager cepheus-srd VOYAGER)

target_link_libraries(voyager PRIVATE
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

set_target_properties(voyager PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/voyager
)

target_compile_features(voyager PRIVATE cxx_std_17)
