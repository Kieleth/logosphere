cmake_minimum_required(VERSION 3.10)

add_executable(logotriste
    src/main.cpp
    src/generated/logotriste_ontology_registry.cpp
)

target_include_directories(logotriste PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/generated
)

target_link_libraries(logotriste
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

set_target_properties(logotriste PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/logotriste
)

if(APPLE)
    target_compile_options(logotriste PRIVATE -Wall -Wextra)
endif()

# Well rules, pure C++17: no engine, no window, no GPU. Builds and runs
# in every profile because it depends on nothing but the two headers.
add_executable(test_logotriste_well tests/test_logotriste_well.cpp)
target_include_directories(test_logotriste_well PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
)
set_target_properties(test_logotriste_well PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
