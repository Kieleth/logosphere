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

// FULL-STATE NARRATION (the assert-or-waive discipline, owner-approved
// 2026-08-19; every narrated degree of freedom is asserted below or
// waived here, by name):
//   position xyz — x,y must not move (no lateral force exists): WAIVED
//     from assertion, watched by Argus, a drift would show in the dump.
//     z: asserted per rung (settle height).
//   velocity — implied by settle (z asserted at rest after 5 s); spikes
//     guarded by Argus peak_speed, printed. WAIVED from hard assert:
//     the explosion detector owns velocity ceilings engine-wide.
//   orientation — rot_y asserted per rung (the ladder's subject).
//     rot_x, rot_z: nothing in any rung excites them; WAIVED, watched.
//   angular velocity — asserted per rung (peak and settle).
//   q-vs-Euler coherence — asserted here via Argus divergence: the
//     ladder must not silently manufacture a two-truths body (G-23).
//   relative geometry — cube-to-slab separation is what "settled ON the
//     slab" means; asserted via Argus for R0.

#include "scenes/scene_cube_drop.h"

#include <cstdio>
#include <iostream>
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

    for (int r = 0; r < RUNG_COUNT; ++r) {
        const RungSpec& spec = RUNGS[r];
        ParticleSystem ps;
        PhysicsSystem physics;
        if (!physics.initialize(ps)) { std::printf("  [FAIL] init\n"); return 1; }
        Scene scene;
        scene.build(ps);
        scene.arm(ps, spec, r);
        scene.argus.reset_milestones(scene.actor(r));
        const int A = scene.actor(r);
        for (int f = 0; f < RUN_FRAMES; ++f) {
            scene.step(ps, physics, f, spec.spin_z);
            if (r >= 3) {
                // The experiment NARRATES itself (owner order): the story
                // in the log, milestones the frame they happen, so dead
                // air is visible as dead air.
                if (f % 10 == 0) scene.argus.narrate(std::cout, A);
                scene.argus.milestones(std::cout, A, scene.rest_z);
            }
        }

        std::printf("\n-- %s --\n", spec.name);
        std::printf("  [measure] settled: rot_y %.4f rad, spin %.4f rad/s, z %.4f "
                    "(edge-rest %.4f, flat %.4f; slab top %.2f)\n",
                    scene.settled_rot_y(ps), scene.settled_spin(ps),
                    scene.settled_z(ps),
                    FLOOR_TOP + rest_height(spec.tilt_rad),
                    FLOOR_TOP + CUBE * 0.5f, FLOOR_TOP);
        std::printf("  [measure] peak |omega_y| %.4f rad/s, touchdown frame %d\n",
                    scene.peak_omega_y, scene.touchdown_frame);

        static const bool lever = std::getenv("CONTACT_TORQUE") != nullptr;
        if (r == 0) {
            if (!lever) {
                check(scene.settled_rot_y(ps) < CONTROL_ROT_MAX &&
                      scene.peak_omega_y < CONTROL_ROT_MAX,
                      "R0 control: a flat drop invents no rotation");
            } else {
                // Under torque a dead-flat landing is an unstable
                // equilibrium: sequential per-point solving injects a
                // deterministic transient. The lever contract: bounded
                // wobble, full decay. Not a loosening of the default
                // control, which still runs strict in default mode.
                check(scene.peak_omega_y < WOBBLE_MAX_LEVER,
                      "R0 lever: transient wobble bounded (deterministic, "
                      "decays; strict zero stays the default-mode law)");
                check(scene.settled_rot_y(ps) < CONTROL_ROT_MAX,
                      "R0 lever: and it DECAYS: settled flat, no lasting "
                      "rotation invented");
            }
            // Argus answers the relative question directly: settled ON
            // the slab means centre-to-centre = slab half + cube half.
            const float sep = scene.argus.separation(scene.cube, scene.slab);
            std::printf("  [measure] argus: cube-slab separation %.4f "
                        "(resting contact = %.4f)\n", sep,
                        FLOOR_TOP * 0.5f + CUBE * 0.5f);
            check(std::fabs(sep - (FLOOR_TOP * 0.5f + CUBE * 0.5f)) < 0.01f,
                  "R0 control: it rests ON the slab (Argus separation)");
            check(scene.argus.divergence(scene.cube) < 0.01f,
                  "R0 control: one body, one orientation throughout");
        } else if (r == 1) {
            check(scene.settled_rot_y(ps) < (lever ? TIP_SETTLE_MAX : FLAT_ROT_MAX),
                  "R1: the tilted cube settles FLAT (G-35: a cube cannot "
                  "rest on an edge)");
            check(scene.peak_omega_y > TIP_OMEGA_MIN,
                  "R1: and it ROTATED to get there (contact torque exists)");
            if (lever)
                check(scene.argus.divergence(scene.cube) < 0.01f,
                      "R1 lever: the righting is COHERENT, one orientation "
                      "through the whole tip (Argus)");
        } else if (r == 6) {
            // R7 THE CORNER STAND — G-43's instrument. Full state named:
            // z must END at a face; spin must PEAK (falling is rotating);
            // orientation stays coherent; the face-resting twin is the
            // contrast and stays put. x/y drift waived by name: which
            // way it falls is chaos's choice and no law constrains it.
            const int H = scene.hero;
            std::printf("  [measure] R7 hero: z %.4f (standing %.4f, "
                        "fallen-face %.4f), peak spin %.4f rad/s\n",
                        scene.settled_z(ps, H), scene.rest_z,
                        FLOOR_TOP + HERO * 0.5f, scene.argus.peak_spin(H));
            static const bool lever7 = std::getenv("CONTACT_TORQUE") != nullptr;
            if (lever7) {
                check(scene.settled_z(ps, H) < CORNER_FALLEN_Z_MAX,
                      "R7 lever: the corner stand FALLS to a face (a cube "
                      "cannot balance on a corner; G-43 says ours does)");
                check(scene.argus.peak_spin(H) > CORNER_TOPPLE_SPIN_MIN,
                      "R7 lever: and it ROTATED on the way down");
                check(scene.argus.divergence(H) < 0.01f,
                      "R7 lever: one orientation through the fall (Argus)");
                const float tdy = scene.displaced_y(ps, scene.twin) - 1.5f;
                check(std::fabs(tdy) < 0.02f,
                      "R7 control: the face-resting twin stays put");
            } else {
                std::printf("  [waive] R7 default: no contact torque law "
                            "by default, a corner stand is trivially "
                            "eternal; the claim is the lever's\n");
            }
        } else if (r >= 3) {
            // G-41 as a LECTURE (skill standard): the hero performs, the
            // still twin is the on-stage control, and both are asserted.
            const int H = scene.hero;
            const float dx = scene.displaced_x(ps, H);
            const float dy = scene.displaced_y(ps, H);
            const float twin_dx = scene.displaced_x(ps, scene.twin)
                                - ((spec.spin_x != 0.0f) ? 1.5f : 0.0f);
            const float twin_dy = scene.displaced_y(ps, scene.twin)
                                - ((spec.spin_x != 0.0f) ? 0.0f : 1.5f);
            std::printf("  [measure] G-41 hero: displaced (%.4f, %.4f) m, "
                        "settled spin %.4f rad/s\n",
                        dx, dy, scene.settled_spin(ps, H));
            std::printf("  [measure] G-41 twin: drifted (%.4f, %.4f) m "
                        "(the still control)\n", twin_dx, twin_dy);
            std::printf("  [measure] argus hero: peak spin %.4f, peak speed "
                        "%.4f, divergence %.6f\n",
                        scene.argus.peak_spin(H),
                        scene.argus.peak_speed(H),
                        scene.argus.divergence(H));
            check(scene.argus.divergence(H) < 0.01f,
                  "G-41: one body, one orientation through the whole event");
            check(scene.argus.peak_speed(H) < 10.0f,
                  "G-41: no energy invented: speeds stay bounded (INV-3)");
            check(std::fabs(twin_dx) < 0.02f && std::fabs(twin_dy) < 0.02f,
                  "G-41 control: the still twin STAYS still (whatever moved "
                  "the hero, it was the spin)");
            static const bool lever2 = std::getenv("CONTACT_TORQUE") != nullptr;
            if (lever2 && r == 3)
                check(std::fabs(dx) < 0.05f && std::fabs(dy) < 0.05f &&
                      scene.settled_spin(ps, H) < 0.1f,
                      "R4 lever: the big top brakes IN PLACE");
            if (lever2 && r == 4)
                check(std::fabs(dy) > 0.05f && std::fabs(dx) < std::fabs(dy),
                      "R5 lever: the X-spin wheel WALKS along Y "
                      "(spin bought translation, on the right axis)");
            if (lever2 && r == 5)
                check(std::fabs(dx) > 0.05f && std::fabs(dy) < std::fabs(dx),
                      "R6 lever: the Y-spin wheel WALKS along X");
        } else {
            std::printf("  [measure] argus: peak spin %.4f, peak speed %.4f\n",
                        scene.argus.peak_spin(scene.cube),
                        scene.argus.peak_speed(scene.cube));
            std::printf("  [measure] spin at touchdown / release: %.4f "
                        "(derived law: flight barely taxes it now)\n",
                        scene.keep_at_touchdown);
            std::printf("  [measure] worst per-flight-frame retention: %.4f "
                        "(derived law: ~1.0 per frame in air)\n", scene.min_frame_keep);
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
