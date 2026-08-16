cmake_minimum_required(VERSION 3.10)

# Pacman — maze chase on Logosphere. See PACMAN.md.

set(PACMAN_COMMON_SOURCES
    src/maze.cpp
    src/generated/pacman_ontology_registry.cpp
)

set(PACMAN_INCLUDES
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/generated
)

set(PACMAN_FRAMEWORKS
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

add_executable(pacman src/main.cpp ${PACMAN_COMMON_SOURCES})
target_include_directories(pacman PRIVATE ${PACMAN_INCLUDES})
target_link_libraries(pacman PRIVATE ${PACMAN_FRAMEWORKS})
set_target_properties(pacman PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/pacman)
target_compile_features(pacman PRIVATE cxx_std_17)
if(APPLE)
    target_compile_options(pacman PRIVATE -Wall -Wextra)
endif()

# Headless acceptance test: drives the same PacmanApplication with no
# window and asserts on measured numbers. Lands in build/ next to the
# other at_* executables.
add_executable(at_pacman at_pacman.cpp ${PACMAN_COMMON_SOURCES})
target_include_directories(at_pacman PRIVATE ${PACMAN_INCLUDES})
target_link_libraries(at_pacman PRIVATE ${PACMAN_FRAMEWORKS})
set_target_properties(at_pacman PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_compile_features(at_pacman PRIVATE cxx_std_17)
if(APPLE)
    target_compile_options(at_pacman PRIVATE -Wall -Wextra)
endif()
