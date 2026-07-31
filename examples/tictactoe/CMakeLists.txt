cmake_minimum_required(VERSION 3.10)

# Tic-Tac-Toe: headless/console example. Links only logosphere_core (no
# rendering, GLFW, or Metal), so it builds under every LOGOSPHERE_PROFILE
# (core/physics/full) -- unlike examples/eden, examples/logotron, and
# examples/logogenesis, which need the full macOS engine.
add_executable(tictactoe
    src/main.cpp
    src/generated/tictactoe_ontology_registry.cpp
)

target_include_directories(tictactoe PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/generated
)

target_link_libraries(tictactoe PRIVATE logosphere_core)
target_compile_features(tictactoe PRIVATE cxx_std_17)

set_target_properties(tictactoe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tictactoe
)
