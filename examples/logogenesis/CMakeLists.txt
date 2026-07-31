# Logogenesis — conversational world creation demo.
# You type what should exist; an LLM makes it so through the KG-ops
# grammar and the engine's worldgen generators.
cmake_minimum_required(VERSION 3.10)

set(LOGOGENESIS_SOURCES
    src/main.cpp
    src/generated/logogenesis_ontology_registry.cpp
)

add_executable(logogenesis ${LOGOGENESIS_SOURCES})

target_include_directories(logogenesis PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/kg
    ${CMAKE_SOURCE_DIR}/src/generated
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/generated
)

target_link_libraries(logogenesis PRIVATE
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

set_target_properties(logogenesis PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/logogenesis
)

target_compile_features(logogenesis PRIVATE cxx_std_17)
