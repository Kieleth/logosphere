// =============================================================================
// A CUBE AND A SPHERE ON THE SAME RAMP — the headless half
// =============================================================================
// Board front F2, made measurable: "a sphere will not slide a ramp that a
// cube slides". The mechanism was located 2026-08-16 and is two lines
// wide: `narrow_phase_particle_pair` built the box side of a
// SPHERE-vs-BOX pair with `aabb_of_box_particle`, which never reads
// rotation. The sphere met the ramp's upright bounding slab and its
// contact normal came back (0,0,1): a flat shelf with no along-slope
// component. FIXED 2026-08-19 by `narrow_phase_sphere_obb`; the travel
// asserts below are green and stay as the regression.
//
// STILL BORN RED on a different mechanism, D2 1.2: contact rows carry
// jx/jy/jz from the manifold normal and NO LEVER ARM, so nothing in this
// engine can be spun up by a contact. Both bodies launch off the ramp
// edge and land on the turtle with peak |omega| of exactly zero.
//
// Scene and stepping: tests/scenes/scene_ramp_race.h, shared with
// test_ramp_race_visual.cpp. No body, force or threshold lives here.
//
//   ./build/test_ramp_race                        numbers
//   INTERACTIVE=1 ./build/test_ramp_race_visual    window
// =============================================================================

// FULL-STATE NARRATION (assert-or-waive, per DOF, owner directive
// 2026-08-19). Three bodies; every degree of freedom of each is
// asserted below or waived here, by name. Everything is read through
// Argus, so the asserts, the stdout log and the window readout are one
// source.
//
//   RAMP (KINEMATIC fixture, tilted 40 deg about Y)
//     position xyz  — must not move at all: ASSERTED (worst drift over
//                     the run, all three axes at once). It is the datum
//                     every travel number is measured against.
//     velocity      — implied by the drift assert; WAIVED, watched.
//     orientation   — nothing writes it after build; WAIVED, watched.
//                     A turn would show as drift in the bodies riding it.
//     omega         — WAIVED, watched; same argument.
//
//   CUBE and BALL (DYNAMIC racers, one lane each)
//     x  (downhill) — ASSERTED: each must travel (gravity along the
//                     slope moves anything resting on it). Not "equal
//                     distance": a rolling sphere should outrun a
//                     sliding cube.
//     y  (lateral)  — ASSERTED: no lateral force exists in this
//                     experiment, so neither may leave its lane. This
//                     is what makes the two runs independent.
//     z             — ASSERTED at the deadline: each ends resting ON
//                     the turtle (bottom at 0), not sunk through it and
//                     not perched on the shelf a rotation-blind bound
//                     would invent. This is the assert that would have
//                     caught the pre-fix sphere resting 2.7 m up.
//     velocity      — ASSERTED at the deadline (stopped). Peak speed
//                     over the run is printed, WAIVED from a ceiling:
//                     the explosion detector owns velocity ceilings
//                     engine-wide.
//     omega         — ASSERTED, and RED for both: a body that leaves a
//                     ramp edge and lands on the turtle must turn.
//                     D2 1.2.
//     orientation   — the Euler triple follows omega and is not
//                     independently claimed; WAIVED, watched. Its
//                     COHERENCE with the quaternion IS asserted
//                     (G-23): one body, one orientation.
//     separation    — ASSERTED: the two racers never come within reach
//                     of each other, so neither result contaminates the
//                     other. approach_speed is WAIVED: with the gap
//                     assert holding, there is no approach to bound.

#include "scenes/scene_ramp_race.h"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>

using namespace scene_ramp_race;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("  %s %s\n", ok ? "[PASS]" : "[FAIL]", what.c_str());
    if (!ok) failures++;
}
}  // namespace

int main() {
    std::printf("\n=== a cube and a sphere on the same %.0f degree ramp ===\n",
                SLOPE_DEG);

    ParticleSystem ps;
    PhysicsSystem physics;
    if (!physics.initialize(ps)) { std::printf("  [FAIL] init\n"); return 1; }

    Scene scene;
    scene.build(ps);
    // ARGUS_TRACE=1: narrate the cube's lane per 15 frames, so a lateral
    // drift names the PHASE it starts in (on-ramp box-box vs turtle).
    static const bool lane_trace = std::getenv("ARGUS_TRACE") != nullptr;
    for (int f = 0; f < RUN_FRAMES; ++f) {
        scene.step(ps, physics, f);
        if (lane_trace && f % 15 == 0) {
            auto v = ps.lock_particles_for_read();
            const Particle& cb = v[scene.cube];
            std::printf("  [trace f%3d] cube x %+7.3f  y %+7.3f  z %6.3f  "
                        "omega(%+6.2f,%+6.2f,%+6.2f)\n",
                        f, cb.x, cb.y, cb.z,
                        cb.omega_x, cb.omega_y, cb.omega_z);
        }
    }

    const float cube_d = scene.cube_travel(ps);
    const float ball_d = scene.ball_travel(ps);
    // What gravity offers each of them, before friction takes its cut.
    const float along = 9.81f * std::sin(SLOPE_RAD);

    std::printf("  [measure] gravity along a %.0f deg slope: %.2f m/s^2\n",
                SLOPE_DEG, along);
    std::printf("  [measure] cube   travelled %.3f m downhill in %.1f s\n",
                cube_d, RUN_FRAMES / 60.0f);
    std::printf("  [measure] sphere travelled %.3f m downhill in %.1f s\n",
                ball_d, RUN_FRAMES / 60.0f);
    {   // Where did they actually END UP? A zero can mean "did not
        // slide" or "never landed", and those are different bugs.
        float cx, cy, cz, bx, by, bz;
        scene.position(ps, scene.cube, cx, cy, cz);
        scene.position(ps, scene.ball, bx, by, bz);
        float rx, ry, rz;
        scene.position(ps, scene.ramp, rx, ry, rz);
        std::printf("  [measure] ramp  centre (%.2f, %.2f, %.2f), tilt %.0f deg\n",
                    rx, ry, rz, SLOPE_DEG);
        std::printf("  [measure] cube   final (%.2f, %.2f, %.2f)\n", cx, cy, cz);
        std::printf("  [measure] sphere final (%.2f, %.2f, %.2f)\n", bx, by, bz);
        auto v = ps.lock_particles_for_read();
        // Does the cube TURN while it slides? Friction acts at the contact
        // face, gravity at the centre of mass, so a body sliding on a slope
        // has a moment arm and should acquire angular velocity. A cube that
        // slides perfectly flat is a cube whose friction has no torque.
        {   auto v = ps.lock_particles_for_read();
            const Particle& cb = v[scene.cube];
            std::printf("  [measure] cube final rotation (%.4f, %.4f, %.4f) rad\n",
                        cb.rotation_x, cb.rotation_y, cb.rotation_z);
        }
        std::printf("  [measure] cube rotation_y %.4f rad, omega (%.4f, %.4f, %.4f)\n",
                    v[scene.cube].rotation_y, v[scene.cube].omega_x,
                    v[scene.cube].omega_y, v[scene.cube].omega_z);
        std::printf("  [measure] sphere rotation_y %.4f rad, omega (%.4f, %.4f, %.4f)\n",
                    v[scene.ball].rotation_y, v[scene.ball].omega_x,
                    v[scene.ball].omega_y, v[scene.ball].omega_z);
        {   // The sharpest statement of F2 available: WHERE it came to rest.
            // Kept as the regression witness now that F2 is fixed — the
            // shelf height is printed so a relapse is visible, not
            // inferred from a travel number going to zero.
            const float b = scene.ball_bottom(ps);
            std::printf("  [measure] sphere rests with its bottom at z = %.3f\n", b);
            std::printf("  [measure]   the ramp's REAL tilted face at that x: %.3f\n",
                        Scene::face_z_at(bx));
            std::printf("  [measure]   the ramp's UNROTATED box top:          %.3f\n",
                        Scene::shelf_z());
            std::printf("  [note] F2 fixed 2026-08-19: it runs the face it can\n"
                        "         see and lands on the turtle. A relapse to the\n"
                        "         invented flat shelf would park it up at %.3f.\n",
                        Scene::shelf_z());
        }
        std::printf("  [measure] peak |omega|: cube %.4f rad/s, sphere %.4f rad/s\n",
                    scene.cube_spin_peak, scene.ball_spin_peak);
        std::printf("  [measure] ramp solver_mode = %s, moved %.2f m in z\n",
                    v[scene.ramp].solver_mode == ParticleSolverMode::KINEMATIC
                        ? "KINEMATIC" : "DYNAMIC",
                    rz - ramp_centre_z());
    }
    std::printf("  [note] the cube goes through the 15-axis OBB path and\n"
                "         meets a tilted face. Since 2026-08-19 the sphere\n"
                "         goes through narrow_phase_sphere_obb and meets the\n"
                "         same tilted face: both normals carry the slope.\n");

    // --- what only a per-frame witness can say ------------------------
    std::printf("\n  [argus] worst lane deviation: cube %.4f m, sphere %.4f m "
                "(bound %.2f%s)\n", scene.cube_lane_dev, scene.ball_lane_dev,
                std::getenv("CONTACT_TORQUE") ? LANE_DEV_MAX_LEVER : LANE_DEV_MAX,
                std::getenv("CONTACT_TORQUE") ? ", lever ratchet" : "");
    std::printf("  [argus] fixture drift over the run: %.6f m (bound %.0e)\n",
                scene.ramp_drift, (double)FIXTURE_DRIFT_MAX);
    std::printf("  [argus] closest the two racers ever came: %.3f m "
                "(their shapes meet below %.2f)\n",
                scene.lane_gap_min, LANE_GAP_MIN);
    std::printf("  [argus] worst q-vs-Euler divergence: cube %.6f sharp / "
                "%.6f fold, sphere %.6f / %.6f (bands: %.3f / %.3f)\n",
                scene.argus.peak_divergence(scene.cube, false),
                scene.argus.peak_divergence(scene.cube, true),
                scene.argus.peak_divergence(scene.ball, false),
                scene.argus.peak_divergence(scene.ball, true),
                DIV_MAX_SHARP, DIV_MAX_FOLD);
    std::printf("  [argus] peak speed: cube %.3f m/s, sphere %.3f m/s\n",
                scene.argus.peak_speed(scene.cube),
                scene.argus.peak_speed(scene.ball));
    std::printf("  [argus] at the deadline: cube bottom %.4f m speed %.4f m/s, "
                "sphere bottom %.4f m speed %.4f m/s\n",
                scene.bottom(scene.cube), scene.speed(scene.cube),
                scene.bottom(scene.ball), scene.speed(scene.ball));
    std::printf("\n  the witness's last two frames:\n");
    scene.argus.dump(std::cout, 2);
    std::printf("\n");

    check(Scene::held(scene.ramp_drift),
          "the ramp never moved: every travel number has a fixed datum");
    static const bool lever = std::getenv("CONTACT_TORQUE") != nullptr;
    if (lever) {
        // The lever-mode CONTRACT (2026-08-20 decree): the torque
        // slices' headline claims, enforced where they are made.
        check(scene.ball_spin_peak > ROLL_MIN_LEVER,
              "LEVER: the sphere ROLLS (friction torque at the contact "
              "point; measured 5.30 rad/s when this was clamped)");
        check(ball_d > cube_d * 0.9f,
              "LEVER: rolling is not slower than sliding by more than "
              "10% (rolling dissipates less at the contact)");
    }
    check(Scene::travelled(cube_d),
          "the cube slides down the slope");
    check(Scene::turned(scene.cube_spin_peak),
          "the cube TURNS as it goes (D2 1.2: contact rows carry jx/jy/jz "
          "from the manifold normal and NO LEVER ARM, so no contact in this "
          "engine can spin a body up. The contact point is computed, carried "
          "into the solver, and used only to fill a CollisionEvent.)");
    check(Scene::travelled(ball_d),
          "the sphere ALSO moves (INV-12: contacts come from the body's "
          "actual oriented shape, not its bounding slab)");
    check(Scene::turned(scene.ball_spin_peak),
          "the sphere TURNS TOO. A sphere on a 40 deg slope cannot slide "
          "without rolling: the friction that resists its motion acts a "
          "full radius from its centre, which is a torque and nothing "
          "else. Same mechanism as the cube's, D2 1.2, and harder to "
          "excuse — the cube at least has a flat face to slide on.");
    check(Scene::in_lane(scene.cube_lane_dev) && Scene::in_lane(scene.ball_lane_dev),
          "neither leaves its lane: no lateral force exists, so the two "
          "runs are independent measurements");
    check(Scene::lanes_kept(scene.lane_gap_min),
          "the racers never touch: neither result is contaminated by the "
          "other body");
    check(Scene::landed_and_stopped(scene.bottom(scene.cube), scene.speed(scene.cube)),
          "the cube ends AT REST ON THE TURTLE, not sunk through it and "
          "not perched on a shelf");
    check(Scene::landed_and_stopped(scene.bottom(scene.ball), scene.speed(scene.ball)),
          "the sphere ends AT REST ON THE TURTLE (the assert that would "
          "have caught it resting 2.7 m up inside the ramp)");
    check(Scene::coherent(scene.argus.peak_divergence(scene.cube, false),
                          scene.argus.peak_divergence(scene.cube, true)) &&
          Scene::coherent(scene.argus.peak_divergence(scene.ball, false),
                          scene.argus.peak_divergence(scene.ball, true)),
          "one body, one orientation, every frame for both racers (G-23, "
          "two-band: sharp away from the gimbal fold, the measured "
          "representational ceiling inside it)");

    // Name only the fronts ACTUALLY failing. A verdict that lists a front
    // already fixed is the same class of lie as a comment that outlived
    // its code, which is what this whole area was cleaning up.
    std::string verdict = "RED: ";
    if (!Scene::travelled(cube_d) || !Scene::travelled(ball_d))
        verdict += "F2 (a body does not feel the slope)";
    if (!Scene::turned(scene.cube_spin_peak) || !Scene::turned(scene.ball_spin_peak)) {
        if (verdict.size() > 5) verdict += " + ";
        verdict += "D2 1.2 (contacts carry no torque, so nothing turns)";
    }
    physics.shutdown();
    std::printf("\n  %s (%d failures)\n",
                failures == 0 ? "BOTH BODIES FEEL THE SLOPE AND BOTH TURN"
                              : verdict.c_str(),
                failures);
    return failures == 0 ? 0 : 1;
}
