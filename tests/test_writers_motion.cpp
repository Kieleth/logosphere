// =============================================================================
// A BODY MOVED FROM OUTSIDE CARRIES ITS MOTION (INV-39) - the headless half
// =============================================================================
// G-77 the moving platform, G-78 the turning post. Scene and stepping in
// tests/scenes/scene_writers_motion.h, shared with the window. BORN RED
// by the law's registration (2026-09-05): the solver keeps no previous
// state for a KINEMATIC body and reads its velocity as zero. The
// prediction is inferred from the code and is what this run measures.
//
//   ./build/test_writers_motion                        numbers
//   INTERACTIVE=1 ./build/test_writers_motion_visual    window
// =============================================================================
// FULL-STATE NARRATION (assert or waive, per DOF).
//   SLAB (KINEMATIC, the writer moves x): x prescribed by the writer, not
//     a claim; y, z, orientation, omega: nothing writes them; WAIVED, watched.
//     velocity ledger: THE QUESTION; read through the cube (below).
//   CUBE (DYNAMIC rider): x ASSERTED as slip against the slab (G-77);
//     z ASSERTED as seat (bottom within SLOP of the slab's top, never
//     inside it); vx ASSERTED in the steady phase against the slab's;
//     y, omega, orientation: no lateral force, no torque claimed; WAIVED,
//     watched; coherence ASSERTED (G-21/G-23).
//   POST (KINEMATIC, the writer turns it): yaw prescribed; position,
//     omega ledger: THE QUESTION; read through the arm.
//   ARM (DYNAMIC, nailed): spin ASSERTED in the steady phase (G-78);
//     yaw ASSERTED against the post's; separation ASSERTED (the nail is
//     rigid); x, y, z otherwise follow the nail; WAIVED, watched;
//     coherence ASSERTED.
#include "scenes/scene_writers_motion.h"
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
using namespace scene_writers_motion;
namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("  %s %s\n", ok ? "[PASS]" : "[FAIL]", what.c_str());
    if (!ok) failures++;
}
}
int main() {
    std::printf("\n=== a body moved from outside carries its motion (INV-39) ===\n");
    ParticleSystem ps;
    PhysicsSystem physics;
    if (!physics.initialize(ps)) { std::printf("  [FAIL] init\n"); return 1; }
    Scene scene;
    scene.build(ps, physics);
    for (int f = 0; f < RUN_FRAMES; ++f) {
        scene.step(ps, physics, f);
        if (f % 30 == 29) {
            const auto* S = scene.argus.latest(scene.slab); const auto* C = scene.argus.latest(scene.cube);
            const auto* P = scene.argus.latest(scene.post); const auto* A = scene.argus.latest(scene.arm);
            std::printf("  [f%3d] slab x %+6.3f vx(ledger) %+6.3f | cube x %+6.3f vx %+6.3f seat %+7.4f"
                        " | post yaw %+5.2f wz(ledger) %+5.2f | arm yaw %+5.2f spin %5.3f sep %.4f\n",
                        f, S->x, S->vx, C->x, C->vx, (C->z - CUBE*0.5f) - (S->z + SLAB_THK*0.5f),
                        P->rz, P->oz, A->rz, scene.argus.spin(scene.arm), scene.argus.separation(scene.post, scene.arm));
        }
    }
    std::printf("\n  [measure] G-77 slip max %.4f m (bar %.4f); seat gap [%+.4f, %+.4f] m; cube steady vx error max %.4f m/s\n",
                scene.slip_max, SLIP_MAX, scene.seat_gap_min, scene.seat_gap_max, scene.cube_speed_err_max);
    std::printf("  [measure] G-78 arm spin steady [%.4f, %.4f] rad/s (post %.2f, tol %.3f); yaw err max %.4f rad; sep drift max %.5f m\n",
                scene.spin_min_steady, scene.spin_max_steady, POST_OMEGA, SPIN_TOL, scene.yaw_err_max, scene.sep_drift_max);
    std::printf("  [argus] the witness's last frame:\n");
    scene.argus.dump(std::cout, 1);
    check(Scene::rides(scene.slip_max), "[INV-39/G-77] the cube RIDES the slab: slip under 10 SLOP over the run");
    check(Scene::seated(scene.seat_gap_min, scene.seat_gap_max), "[INV-2/G-77] the cube stays SEATED: bottom within SLOP of the slab's top, never inside it");
    check(Scene::carried(scene.cube_speed_err_max), "[INV-39/G-77] in the steady phase the cube's velocity is the slab's (within 5 %)");
    check(Scene::turns_with(scene.spin_min_steady, scene.spin_max_steady), "[INV-39/G-78] the arm TURNS WITH the post: spin within 10 % of 0.5 rad/s through the steady phase");
    check(Scene::aligned(scene.yaw_err_max), "[INV-39/G-78] the arm's yaw follows the post's (error under 0.05 rad)");
    check(Scene::rigid(scene.sep_drift_max), "[INV-22/G-78] the nail stays rigid: centre separation constant within 5 SLOP");
    check(Scene::coherent(scene.argus.peak_divergence(scene.cube, false), scene.argus.peak_divergence(scene.cube, true)) &&
          Scene::coherent(scene.argus.peak_divergence(scene.arm, false), scene.argus.peak_divergence(scene.arm, true)),
          "[G-21/G-23] one body, one orientation, for the cube and the arm");
    physics.shutdown();
    std::printf("\n  %s (%d failures)\n", failures == 0 ? "THE SOLVER READS THE WRITERS' MOTION" : "RED: INV-39 (a KINEMATIC body's ledger reads zero)", failures);
    return failures == 0 ? 0 : 1;
}
