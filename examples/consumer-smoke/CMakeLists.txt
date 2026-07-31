# External-consumer smoke test for the installed logosphere package.
#
# This is a STANDALONE project, deliberately not part of the engine
# build: it proves that `cmake --install` + find_package(logosphere)
# is enough to build a game-shaped program against logosphere::core.
#
#   cmake --install <engine-build> --prefix <prefix>
#   cmake -S examples/consumer-smoke -B build-consumer \
#         -DCMAKE_PREFIX_PATH=<prefix>
#   cmake --build build-consumer
#   ./build-consumer/consumer-smoke
cmake_minimum_required(VERSION 3.10)
project(logosphere-consumer-smoke CXX)

find_package(logosphere 0.2 REQUIRED)

add_executable(consumer-smoke main.cpp)
target_link_libraries(consumer-smoke PRIVATE logosphere::core)
