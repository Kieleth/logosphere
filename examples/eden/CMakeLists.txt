cmake_minimum_required(VERSION 3.10)

# Eden source files
set(EDEN_SOURCES
    src/main.cpp
    src/generated/eden_ontology_registry.cpp
)

# Create Eden executable
add_executable(eden ${EDEN_SOURCES})

# Include directories
target_include_directories(eden PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/kg
    ${CMAKE_SOURCE_DIR}/src/generated
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/generated
)

# Link with Logosphere library
target_link_libraries(eden
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
)

# Set output directory
set_target_properties(eden PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/eden
)

# Compiler flags
if(APPLE)
    target_compile_options(eden PRIVATE -Wall -Wextra)
endif()
