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

#include "scenes/scene_spinning_cube.h"
#include "generated/physics_constants.h"

#include <cmath>
#include <cstdio>
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
    for (int f = 0; f < RUN_FRAMES; ++f) scene.step(ps, physics);
    const float retained = scene.retained(ps);

    // The arithmetic the constant implies, so the measurement can be
    // checked against it rather than merely reported.
    const float predicted = std::pow(PhysicsV4::ANGULAR_DRAG,
                                     static_cast<float>(RUN_FRAMES * 4));

    std::printf("\n-- GEDANKEN-25: a body spinning in vacuum --\n");
    std::printf("  [measure] omega_z %.4f -> %.6f rad/s after %d frames\n",
                SPIN0, scene.spin_now(ps), RUN_FRAMES);
    std::printf("  [measure] retained %.6f of its spin in one second\n",
                retained);
    std::printf("  [measure] ANGULAR_DRAG^(4 substeps x %d frames) = %.6f\n",
                RUN_FRAMES, predicted);
    std::printf("  [note] nothing touched this body: no contact, no bond,\n"
                "         no medium, no torque. Only the constant.\n");

    check(Scene::passes(retained),
          "vacuum: an isolated body keeps its angular momentum");

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
