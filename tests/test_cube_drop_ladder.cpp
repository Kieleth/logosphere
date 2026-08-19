// =============================================================================
// THE CUBE DROP LADDER — the headless half
// =============================================================================
// Divide and conquer on rotation, owner method 2026-08-19: G's first
// (GEDANKEN-35/36/37), asserts written FROM the G's, then measure, then
// solutions. Scene, stepping and every threshold live in
// tests/scenes/scene_cube_drop.h; this driver owns none of them.
//
//   R0  a flat cube dropped must settle flat and invent no rotation
//       (the control that keeps every later red honest)
//   R1  a cube dropped tilted 20 deg must TIP FLAT: contact torque,
//       geometry only, no material constant in the prediction
//   R2  a spinning cube's spin must SURVIVE FLIGHT (>90%) and die on
//       the floor's face, not in the air
//   R3  per flight frame, spin retention is 1.0 to noise: a constant
//       leak and an impact loss cannot hide in one end-to-end number
//
//   ./build/test_cube_drop_ladder                          numbers
//   INTERACTIVE=1 ./build/test_cube_drop_ladder_visual      window
// =============================================================================

#include "scenes/scene_cube_drop.h"

#include <cstdio>
#include <string>

using namespace scene_cube_drop;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("  %s %s\n", ok ? "[PASS]" : "[FAIL]", what.c_str());
    if (!ok) failures++;
}
}  // namespace

int main() {
    std::printf("\n=== the cube drop ladder: rotation, one mechanism per rung ===\n");

    for (int r = 0; r < 3; ++r) {
        const RungSpec& spec = RUNGS[r];
        ParticleSystem ps;
        PhysicsSystem physics;
        if (!physics.initialize(ps)) { std::printf("  [FAIL] init\n"); return 1; }
        Scene scene;
        scene.build(ps);
        scene.arm(ps, spec);
        for (int f = 0; f < RUN_FRAMES; ++f)
            scene.step(ps, physics, f, spec.spin_z);

        std::printf("\n-- %s --\n", spec.name);
        std::printf("  [measure] settled: rot_y %.4f rad, spin %.4f rad/s, z %.4f "
                    "(edge-rest would be %.4f, flat 0.2000)\n",
                    scene.settled_rot_y(ps), scene.settled_spin(ps),
                    scene.settled_z(ps), rest_height(spec.tilt_rad));
        std::printf("  [measure] peak |omega_y| %.4f rad/s, touchdown frame %d\n",
                    scene.peak_omega_y, scene.touchdown_frame);

        if (r == 0) {
            check(scene.settled_rot_y(ps) < CONTROL_ROT_MAX &&
                  scene.peak_omega_y < CONTROL_ROT_MAX,
                  "R0 control: a flat drop invents no rotation");
        } else if (r == 1) {
            check(scene.settled_rot_y(ps) < FLAT_ROT_MAX,
                  "R1: the tilted cube settles FLAT (G-35: a cube cannot "
                  "rest on an edge)");
            check(scene.peak_omega_y > TIP_OMEGA_MIN,
                  "R1: and it ROTATED to get there (contact torque exists)");
        } else {
            std::printf("  [measure] spin at touchdown / release: %.4f "
                        "(G-36 predicts today ~0.046 = 0.95^60)\n",
                        scene.keep_at_touchdown);
            std::printf("  [measure] worst per-flight-frame retention: %.4f "
                        "(G-37: 0.95^4 = 0.8145 today)\n", scene.min_frame_keep);
            check(scene.keep_at_touchdown > FLIGHT_KEEP_MIN,
                  "R2: spin SURVIVES flight; it may die only at the contact");
            check(scene.settled_spin(ps) < SETTLED_SPIN_MAX,
                  "R2: and the floor's friction does brake it to rest");
            check(scene.min_frame_keep > FRAME_KEEP_MIN,
                  "R3: torque-free flight conserves angular momentum, "
                  "per frame (G-37)");
        }
        physics.shutdown();
    }

    std::printf("\n  %s (%d failures)\n",
                failures == 0 ? "THE LADDER IS GREEN"
                              : "RED BY DESIGN: each failure names its own "
                                "mechanism (G-35/36/37)",
                failures);
    return failures == 0 ? 0 : 1;
}
