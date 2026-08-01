#!/usr/bin/env bash
# Builds and runs the threshold example (windowed, full profile: WASD to
# move, Space to jump).
#
# On this machine, the default `cc`/`c++` on PATH can resolve to a
# non-Apple clang (e.g. a Homebrew LLVM install) that fails to compile the
# engine's Metal/Objective-C++ platform layer. If build_full doesn't exist
# yet, this configures it explicitly against Apple's clang to sidestep
# that; if it already exists (e.g. reused from another example), whatever
# compiler it was configured with is left alone.
set -euo pipefail

cd "$(dirname "$0")/../.."

BUILD_DIR=build_full

if [ ! -d "$BUILD_DIR" ]; then
    cmake -B "$BUILD_DIR" -DLOGOSPHERE_PROFILE=full \
        -DCMAKE_C_COMPILER=/usr/bin/clang \
        -DCMAKE_CXX_COMPILER=/usr/bin/clang++
fi

cmake --build "$BUILD_DIR" --target threshold -j"$(sysctl -n hw.ncpu)"

exec "$BUILD_DIR/threshold/threshold"
