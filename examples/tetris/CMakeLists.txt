cmake_minimum_required(VERSION 3.10)

add_executable(tetris
    src/main.cpp
    src/generated/tetris_ontology_registry.cpp
)

target_include_directories(tetris PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/generated
)

target_link_libraries(tetris
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

set_target_properties(tetris PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tetris
)

if(APPLE)
    target_compile_options(tetris PRIVATE -Wall -Wextra)
endif()

# Well rules, pure C++17: no engine, no window, no GPU. Builds and runs
# in every profile because it depends on nothing but the two headers.
add_executable(test_tetris_well tests/test_tetris_well.cpp)
target_include_directories(test_tetris_well PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
)
set_target_properties(test_tetris_well PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
