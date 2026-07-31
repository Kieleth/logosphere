# build_info.cmake — generate include/logosphere/build_info.h from git
# state at configure time.
#
# Every binary that includes this header can report the exact commit
# it was built from, whether the working tree was dirty, and when
# CMake configured it. That's the anchor for session recording /
# telemetry (see include/logosphere/telemetry/session.h) — without
# the SHA, a saved gameplay session is a floating artifact we can't
# correlate with code.
#
# The generated header is intentionally gitignored — it re-generates
# on every CMake configure and inlining it in repo history would
# create pointless diff churn on every commit.

find_package(Git QUIET)

if(Git_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short=10 HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE LOGOSPHERE_BUILD_SHA
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --dirty --always
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE LOGOSPHERE_BUILD_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()

# Fallback values if git isn't available or this isn't a checkout.
# "unknown" rather than empty so telemetry readers can still parse the
# record without special-casing a null field.
if(NOT LOGOSPHERE_BUILD_SHA)
    set(LOGOSPHERE_BUILD_SHA "unknown")
endif()
if(NOT LOGOSPHERE_BUILD_DESCRIBE)
    set(LOGOSPHERE_BUILD_DESCRIBE "unknown")
endif()

string(TIMESTAMP LOGOSPHERE_BUILD_TIMESTAMP "%Y-%m-%dT%H:%M:%SZ" UTC)

# The header lives INSIDE the source include tree so `#include
# "logosphere/build_info.h"` just works from every target that links
# logosphere_core (which already adds include/ to its public search
# path). Gitignored — see .gitignore entry.
configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/build_info.h.in
    ${CMAKE_SOURCE_DIR}/include/logosphere/build_info.h
    @ONLY
)

message(STATUS "build_info: SHA=${LOGOSPHERE_BUILD_SHA} "
               "describe=${LOGOSPHERE_BUILD_DESCRIBE} "
               "ts=${LOGOSPHERE_BUILD_TIMESTAMP}")
