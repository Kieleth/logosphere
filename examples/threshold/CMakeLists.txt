cmake_minimum_required(VERSION 3.10)

# Threshold source files
set(THRESHOLD_SOURCES
    src/main.cpp
    src/threshold_app.cpp
    src/scene.cpp
)

add_executable(threshold ${THRESHOLD_SOURCES})

target_include_directories(threshold PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/kg
    ${CMAKE_SOURCE_DIR}/src/generated
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(threshold
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

set_target_properties(threshold PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/threshold
)

target_compile_features(threshold PRIVATE cxx_std_17)

if(APPLE)
    target_compile_options(threshold PRIVATE -Wall -Wextra)
endif()

# threshold_verify: headless acceptance checks for the jump mechanic,
# following tests/test_humanoid_headless.cpp's pattern -- raw
# ParticleSystem/PhysicsSystem/ParticleDynamicsSystem/HumanoidLocomotion
# via their *_headless initializers. No Engine, no GLFW, no Metal: this
# machine's windowed rendering pipeline renders solid black (see the
# project's "windowed rendering" issue), and it turns out even Engine's
# own headless GPU path is unreliable here (intermittent multi-second
# stalls/hangs waiting on async GPU completion), so this sidesteps
# rendering entirely rather than working around it.
add_executable(threshold_verify
    tools/verify.cpp
)

target_include_directories(threshold_verify PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(threshold_verify PRIVATE
    logosphere_dynamics logosphere_physics logosphere_core)

set_target_properties(threshold_verify PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/threshold
)

target_compile_features(threshold_verify PRIVATE cxx_std_17)
