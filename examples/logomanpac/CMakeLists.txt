cmake_minimum_required(VERSION 3.10)

# Logomanpac — maze chase on Logosphere. See LOGOMANPAC.md.

set(LOGOMANPAC_COMMON_SOURCES
    src/maze.cpp
    src/generated/logomanpac_ontology_registry.cpp
)

set(LOGOMANPAC_INCLUDES
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/generated
)

set(LOGOMANPAC_FRAMEWORKS
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

add_executable(logomanpac src/main.cpp ${LOGOMANPAC_COMMON_SOURCES})
target_include_directories(logomanpac PRIVATE ${LOGOMANPAC_INCLUDES})
target_link_libraries(logomanpac PRIVATE ${LOGOMANPAC_FRAMEWORKS})
set_target_properties(logomanpac PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/logomanpac)
target_compile_features(logomanpac PRIVATE cxx_std_17)
if(APPLE)
    target_compile_options(logomanpac PRIVATE -Wall -Wextra)
endif()

# Headless acceptance test: drives the same LogomanpacApplication with no
# window and asserts on measured numbers. Lands in build/ next to the
# other at_* executables.
add_executable(at_logomanpac at_logomanpac.cpp ${LOGOMANPAC_COMMON_SOURCES})
target_include_directories(at_logomanpac PRIVATE ${LOGOMANPAC_INCLUDES})
target_link_libraries(at_logomanpac PRIVATE ${LOGOMANPAC_FRAMEWORKS})
set_target_properties(at_logomanpac PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_compile_features(at_logomanpac PRIVATE cxx_std_17)
if(APPLE)
    target_compile_options(at_logomanpac PRIVATE -Wall -Wextra)
endif()
