#!/usr/bin/env bash
# Repeatable launcher for the multi-light demo scene.
#
# The same vehicle the perf sweeps use (test_multi_light_progressive), with
# MLP_DEMO=1: dark blue-violet floor, shadow-casting pillars, falling
# DYNAMIC bodies under the solver, and lights on layered counter-rotating
# orbits that dive low to rake long coloured shadows.
#
# The demo dressing is behind MLP_DEMO and does NOT touch the benchmark
# scene — the phase dumps stay byte-identical pixel oracles.
#
#   scripts/demo_lights.sh                 # 16 lights, 2304 particles
#   scripts/demo_lights.sh 24 4096         # lights, particles
#   scripts/demo_lights.sh 16 2304 60      # ... and 60 bodies/sec raining in
#
# Sweep shapes (optional first arg):
#   scripts/demo_lights.sh lights    24 2304    # lights grow, particles fixed
#   scripts/demo_lights.sh particles 8  576     # lights fixed, particles rain
#   scripts/demo_lights.sh both      16 1024    # both grow together
#
# SPACE advances a phase, ESC exits.
set -euo pipefail
cd "$(dirname "$0")/.."

# Optional first arg is the sweep shape; otherwise args stay positional so
# older invocations keep working.
MODE=""
case "${1:-}" in
  lights|particles|both) MODE="$1"; shift ;;
esac

LIGHTS="${1:-16}"
PARTICLES="${2:-2304}"
BIN=build-release/logosphere-tests

if [ ! -x "$BIN" ]; then
  echo "Building $BIN ..."
  cmake --build build-release --target logosphere-tests -j 8 >/dev/null
fi

echo "Demo: mode=${MODE:-default} ${LIGHTS} lights, ${PARTICLES} particles  (SPACE = next phase, ESC = quit)"
# MLP_SPAWN_RATE: bodies dropped per second. Particle count climbs during
# the run, so the HUD shows cost rising with load in real time.
# Record telemetry for every demo run: watching the hit and being able to
# quantify it afterwards should not be two separate runs.
METRICS="${METRICS:-/tmp/mlp_demo_metrics.jsonl}"
echo "Metrics -> $METRICS   (analyse: scripts/bench_report.py $METRICS --ramp)"
LOGOSPHERE_METRICS="$METRICS" MLP_MODE="$MODE" \
  MLP_DEMO=1 INTERACTIVE=1 PARTICLES="$PARTICLES" LIGHTS="$LIGHTS" \
  MLP_SPAWN_RATE="${3:-30}" MLP_MAX_PARTICLES="${4:-20000}" \
  "$BIN" --test test_multi_light_progressive --no-head 2>&1 | tee /tmp/mlp_demo.log
