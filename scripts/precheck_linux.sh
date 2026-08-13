#!/usr/bin/env bash
# =============================================================================
# Linux pre-check: run the merge-blocking CI jobs locally, before pushing.
# =============================================================================
# macOS never builds the headless or physics profiles, so a change that is green
# on this laptop can still break `main`. This script builds and runs both Linux
# jobs from .github/workflows/ci.yml inside a container.
#
#   ./scripts/precheck_linux.sh
#
# WHY IT MIRRORS CI TEST-BY-TEST. An earlier version BUILT the headless profile
# and never RAN its suite. `test_particle_shapes` asserted a literal 320
# triangles per sphere, the sphere-LOD default moved to 80, and this script
# reported green while CI stayed red for three commits. A pre-check weaker than
# the job it stands in for is worse than no pre-check, because it is trusted.
# If you edit the `headless-linux` or `physics-linux` job, edit this too.
#
# READ THE MARKERS, NOT THE EXIT CODE, when running this by hand: BUILD_OK,
# GUARDS_OK, HEADLESS_SUITE_OK, CONSUMER_OK, and the final PRECHECK verdict.
# The script does exit non-zero on failure, but every wrapper that has ever
# reported on it got that wrong at least once (ledger learnings 3 and 16).
#
# Requires a Docker daemon. Docker Desktop's socket flickers on this machine,
# so colima is the default; override with DOCKER_HOST for anything else.
#   colima start
# =============================================================================
set -uo pipefail

REPO="$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)"
IMAGE="${PRECHECK_IMAGE:-ubuntu:24.04}"

if [ -z "${DOCKER_HOST:-}" ] && [ -S "$HOME/.colima/default/docker.sock" ]; then
    export DOCKER_HOST="unix://$HOME/.colima/default/docker.sock"
fi

if ! docker version >/dev/null 2>&1; then
    echo "PRECHECK FAIL: no reachable Docker daemon (DOCKER_HOST=${DOCKER_HOST:-unset})"
    echo "               try: colima start"
    exit 1
fi

echo "repo:   $REPO"
echo "image:  $IMAGE"
echo "docker: ${DOCKER_HOST:-default}"

# The source tree is mounted read-only and copied inside, so a container build
# can never touch the working tree's build/ directories.
docker run --rm -v "$REPO":/src:ro -w /work "$IMAGE" bash -c '
set -e
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq && apt-get install -y -qq cmake g++ ninja-build >/dev/null
cp -r /src /work/repo && cd /work/repo && rm -rf build build-release build-consumer _install

# --- job: physics-linux ------------------------------------------------------
echo "=== profile: physics ==="
cmake -S . -B build -G Ninja -DLOGOSPHERE_PROFILE=physics -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build -j "$(nproc)" >/dev/null
echo "BUILD_OK physics"
for t in test_humanoid_headless test_pin_gluon_lifecycle; do
  if ./build/$t >/dev/null; then echo "PASS $t"; else echo "FAIL $t"; PHYS_FAILED=1; fi
done
if ./build/logosphere-physics-guards >/dev/null; then echo "GUARDS_OK"; else echo "FAIL logosphere-physics-guards"; PHYS_FAILED=1; fi
# A swallowed abort is how this script printed GREEN over three dead
# markers (2026-08-13): cmd && echo under a for-loop defeats set -e.
[ -z "${PHYS_FAILED:-}" ] || { echo "PRECHECK FAIL: physics-profile tests failed"; exit 1; }
./build/test_deep_probe_halt >/dev/null && echo "INFRA_OK"

# --- job: headless-linux -----------------------------------------------------
echo "=== profile: headless-only ==="
rm -rf build
cmake -S . -B build -G Ninja -DLOGOSPHERE_HEADLESS_ONLY=ON -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build -j "$(nproc)" >/dev/null
echo "BUILD_OK headless"

# KEEP IN SYNC with the headless-linux job test list.
fail=0
for t in \
  test_event_log test_signal test_event_channel test_event_bus \
  test_damage_events test_capability_system test_ontology_extension \
  test_body_plan test_game_time test_kg_setproperty_events \
  test_relation_events test_kg_parse_safety \
  test_logotron_cycle test_logotron_trails test_logotron_collision \
  test_logotron_round test_logotron_director \
  test_particle_shapes \
  test_interaction_ontology test_interaction_events test_interaction_profiles
do
  ./build/$t >/dev/null 2>&1 || { echo "HEADLESS_SUITE_FAIL: $t"; fail=1; }
done
[ "$fail" = "0" ] && echo "HEADLESS_SUITE_OK" || exit 1

# The install + external-consumer half of the same job. A change can pass every
# test and still break the exported CMake package for anyone consuming it.
cmake --install build --prefix "$PWD/_install" >/dev/null
cmake -S examples/consumer-smoke -B build-consumer -G Ninja \
      -DCMAKE_PREFIX_PATH="$PWD/_install" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build-consumer >/dev/null
./build-consumer/consumer-smoke >/dev/null && echo "CONSUMER_OK"
'
status=$?

if [ "$status" -eq 0 ]; then
    echo "PRECHECK GREEN: both Linux CI jobs pass"
else
    echo "PRECHECK FAIL (exit $status): do not push"
fi
exit "$status"
