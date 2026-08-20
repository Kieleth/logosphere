// =============================================================================
// WHAT SLOWS A SPINNING BODY DOWN? — the headless half
// =============================================================================
// The engine's answer today is a constant. `ANGULAR_DRAG = 0.95` is
// multiplied into omega_x, omega_y and omega_z once per substep, four
// times a frame, for every body, unconditionally. Its own declaration
// admits the problem (`src/generated/physics_constants.h:296-301`):
//
//     "Same INV-19 exposure as DAMPING_FACTOR: absolute-motion damping
//      whose dissipation story is thin. Extracted as-is by decree; any
//      retuning is ledger follow-up."
//
// INV-29 was satisfied by that extraction: the constant is named,
// unit-tagged and homed. INV-19 was never asked. Its linear twin
// DAMPING_FACTOR has since been eradicated and has zero readers left.
// This one is still applied.
//
// BORN RED on purpose: it measures the disease so the campaign that
// cures it has a before-value. Red first, green after, and never weaken
// the assertion to make it pass. The claim is CONSERVATION, so a 5% loss
// would be as wrong as 95%.
//
// THE SCENE LIVES IN tests/scenes/scene_spinning_cube.h AND SO DOES THE
// STEPPING. This file holds no body, no force and no threshold, and
// neither does its windowed twin, `test_angular_dissipation_visual.cpp`.
// That is criterion (b) of the logosphere-tests skill and it is not
// decoration: `PHYSICS_TIMESTEP` is 1/30 and `engine.update()`
// accumulates, so a windowed driver stepping the engine and a headless
// one stepping physics directly measure DIFFERENT spin-down for a
// per-substep quantity. Both call `scene.step()` instead. Verified: both
// report retained = 0.000005.
//
//   ./build/test_angular_dissipation                        numbers
//   INTERACTIVE=1 ./build/test_angular_dissipation_visual    window
//
// GEDANKEN-25 is the experiment; GEDANKEN-27's half (water against air)
// cannot be instrumented yet and this file says so rather than pretending.
// =============================================================================

// FULL-STATE NARRATION (assert-or-waive, per DOF, owner directive
// 2026-08-19). ONE body, and "isolated" is a claim about all of its
// state, not about omega_z alone. Read through Argus, so the asserts
// and the log are one source.
//
//   x, y          — nothing pushes sideways: ASSERTED, worst deviation
//                   from the release column over the whole run.
//   z             — the body is in free fall, so z is not held to a
//                   value. What IS asserted is CLEARANCE: it must stay
//                   far above the turtle, the only thing in this world
//                   it could meet. That turns "nothing touched it" from
//                   a claim into a measurement. Its departure from the
//                   analytic vacuum drop is printed (ambient air acts on
//                   linear velocity) and WAIVED from assertion: this
//                   experiment is the ANGULAR side, and the linear
//                   medium is its own front.
//   vx, vy, vz    — implied by the position asserts; peak speed printed.
//                   WAIVED from a ceiling: the explosion detector owns
//                   velocity ceilings engine-wide.
//   omega_x, y    — nothing excites them: ASSERTED at zero.
//   omega_z       — the subject. ASSERTED end-to-end (RED) and now also
//                   PER FRAME (RED): an end-to-end number cannot tell a
//                   constant leak from a single event, and the per-frame
//                   figure names ANGULAR_DRAG^4 to four decimals.
//   orientation   — rot_z follows omega_z and is not independently
//                   claimed; printed, WAIVED. Its COHERENCE with the
//                   quaternion IS asserted (G-23).
//   relative      — there is no second body. Argus separation and
//                   approach_speed are n/a by construction, which is
//                   exactly what makes this experiment isolated.

#include "scenes/scene_spinning_cube.h"
#include "generated/physics_constants.h"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>

using namespace scene_spinning_cube;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("  %s %s\n", ok ? "[PASS]" : "[FAIL]", what.c_str());
    if (!ok) failures++;
}
}  // namespace

int main() {
    std::printf("\n=== what slows a spinning body down? ===\n");

    ParticleSystem ps;
    PhysicsSystem physics;
    if (!physics.initialize(ps)) { std::printf("  [FAIL] init\n"); return 1; }

    Scene scene;
    scene.build(ps);
    for (int f = 0; f < RUN_FRAMES; ++f) scene.step(ps, physics, f);
    const float retained = scene.retained(ps);

    // The arithmetic the constant implies, so the measurement can be
    // checked against it rather than merely reported.
    const float predicted = std::pow(PhysicsV4::ANGULAR_DRAG,
                                     static_cast<float>(RUN_FRAMES * 4));
    const float per_frame_predicted = std::pow(PhysicsV4::ANGULAR_DRAG, 4.0f);

    std::printf("\n-- GEDANKEN-25: a body spinning in vacuum --\n");
    std::printf("  [measure] omega_z %.4f -> %.6f rad/s after %d frames\n",
                SPIN0, scene.spin_now(ps), RUN_FRAMES);
    std::printf("  [measure] retained %.6f of its spin in one second\n",
                retained);
    std::printf("  [measure] ANGULAR_DRAG^(4 substeps x %d frames) = %.6f\n",
                RUN_FRAMES, predicted);
    std::printf("  [note] nothing touched this body: no contact, no bond,\n"
                "         no medium, no torque. Only the constant.\n");

    // --- the witness: the rest of the state, per frame ----------------
    std::printf("\n  [argus] worst PER-FRAME retention %.4f "
                "(ANGULAR_DRAG^4 = %.4f)\n",
                scene.worst_frame_keep, per_frame_predicted);
    std::printf("  [argus] worst lateral drift %.2e m, worst off-axis spin "
                "%.2e rad/s\n", scene.max_lateral, scene.max_off_axis);
    std::printf("  [argus] lowest z reached %.3f m (turtle at 0; clearance "
                "bound %.0f)\n", scene.min_z, CLEARANCE_MIN);
    std::printf("  [argus] peak spin %.4f rad/s (release %.4f), peak speed "
                "%.3f m/s\n", scene.argus.peak_spin(scene.id), SPIN0,
                scene.argus.peak_speed(scene.id));
    std::printf("  [argus] worst q-vs-Euler divergence %.6f rad, visible "
                "turn %.4f rad\n", scene.max_div, scene.visible_turn(ps));
    std::printf("  [measure] fell %.4f m SHORT of the analytic vacuum drop "
                "(ambient air is linear-only; not asserted here)\n",
                -scene.fall_shortfall(RUN_FRAMES));
    std::printf("\n  the witness's last two frames:\n");
    scene.argus.dump(std::cout, 2);
    std::printf("\n");

    check(Scene::untouched(scene.min_z),
          "the body really was isolated: it never came near the turtle, "
          "the only thing in this world it could have met");
    check(Scene::stayed_in_column(scene.max_lateral),
          "nothing pushed it sideways (so the spin-down cannot be blamed "
          "on a contact this test failed to notice)");
    check(Scene::axis_pure(scene.max_off_axis),
          "no torque appeared on an axis nothing excited");
    check(Scene::coherent(scene.max_div),
          "one body, one orientation, every frame (G-23)");
    check(Scene::passes(retained),
          "vacuum: an isolated body keeps its angular momentum");
    check(Scene::conserves_per_frame(scene.worst_frame_keep),
          "and it keeps it EVERY FRAME, not merely on average. This is "
          "the assert that names the culprit: the worst frame retains "
          "exactly ANGULAR_DRAG^4, so the loss is a constant applied per "
          "substep and not an event, an integrator artefact or a contact "
          "(G-37's method, borrowed from the cube-drop ladder).");

    std::printf("\n-- GEDANKEN-27: water against air --\n");
    std::printf("  [measure] not yet instrumented: the medium path is\n"
                "            LINEAR ONLY (apply_volume_forces writes\n"
                "            vx/vy/vz and never omega), so a submerged\n"
                "            body and an airborne one cannot differ.\n");
    std::printf("  [note] ambient air DOES exist (quadratic, RHO_AIR),\n"
                "         but it is linear-velocity-only and not\n"
                "         declarable, so it damps no spin and no scene\n"
                "         can opt out of it. D7.\n");

    physics.shutdown();
    std::printf("\n  %s (%d failures)\n",
                failures == 0 ? "ANGULAR DISSIPATION IS PHYSICAL"
                              : "ANGULAR DISSIPATION IS A CONSTANT (expected "
                                "red: this is the before-value)",
                failures);
    return failures == 0 ? 0 : 1;
}
