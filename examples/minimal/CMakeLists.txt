cmake_minimum_required(VERSION 3.10)

# The smallest complete game. One file, no schema, no generated code.
# docs/GETTING_STARTED.md quotes main.cpp, so this target existing and
# building is what keeps that tutorial honest.

add_executable(minimal main.cpp)
target_include_directories(minimal PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(minimal PRIVATE
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
set_target_properties(minimal PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/minimal)
target_compile_features(minimal PRIVATE cxx_std_17)
if(APPLE)
    target_compile_options(minimal PRIVATE -Wall -Wextra)
endif()
