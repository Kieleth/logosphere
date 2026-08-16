// =============================================================================
// WHAT SLOWS A SPINNING BODY DOWN?
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
// This test is the instrument for GEDANKEN-25 and GEDANKEN-27, and it
// is BORN RED on purpose. It measures the disease so the campaign that
// cures it has a before-value, per the ladder discipline: red first,
// green after, and never weaken the assertion to make it pass.
//
// TWO MEASUREMENTS.
//
// 1. A body spinning in vacuum. Nothing touches it, no medium, no
//    bonds, no contacts. Angular momentum is conserved in the absence
//    of an external torque; that is the definition of an isolated
//    body, not a modelling preference. Expect: it keeps spinning.
//
// 2. The same body spinning inside a declared water medium, against a
//    twin spinning in open air. Water is roughly 800x denser and the
//    body presents area to the flow, so the submerged one must slow
//    faster. Expect: they differ.
//
// Both are red today for the same reason, and the reason is one
// constant standing in for two mechanisms the engine does not have:
// a DECLARABLE ambient medium and ANGULAR coupling to one.
//
// CORRECTION (2026-08-16): an ambient medium DOES exist and this file
// first said it did not. Quadratic drag against RHO_AIR = 1.225 runs on
// every moving body every substep (physics_system_v4.cpp:4674-4683).
// It is not declarable, so no scene can be set in vacuum, and it runs
// ALONGSIDE the linear medium drag rather than instead of it. What is
// genuinely absent is any ANGULAR counterpart: `apply_volume_forces`
// writes vx/vy/vz and never touches omega, and the ambient law is
// linear-velocity-only too. So the angular finding below stands
// unchanged; only the story about what surrounds the body was wrong.
// Boarded as D7.
//
// Run: ./build/test_angular_dissipation
// =============================================================================

#include "core/particle_system.h"
#include "logosphere/physics/physics_system.h"
#include "particle.h"
#include "generated/physics_constants.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace {

int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("  %s %s\n", ok ? "[PASS]" : "[FAIL]", what.c_str());
    if (!ok) failures++;
}

constexpr float SPIN0 = 4.0f;      // rad/s, well under MAX_OMEGA
constexpr int   FRAMES = 60;       // one second at 60 Hz

// One cube, spinning, far from the turtle and from everything else.
// Returns its angular speed about Z after `FRAMES` frames.
float spin_alone(float& out_start) {
    ParticleSystem ps;
    PhysicsSystem physics;
    if (!physics.initialize(ps)) { out_start = 0.0f; return 0.0f; }

    Particle p{};
    p.x = 0.0f; p.y = 0.0f; p.z = 40.0f;     // high above the world floor
    p.shape = ParticleShape::BOX;
    p.width = p.height = p.thickness = 0.4f;
    p.size = 0.4f;
    p.SetMaterial(Materials::Type::STONE);
    const int id = ps.queue_particle_addition(p);
    ps.flush_pending_particles();
    {
        auto v = ps.lock_particles_for_write();
        v[id].omega_z = SPIN0;
        v[id].is_at_rest = false;
        // Cancel gravity's effect on the spin question by keeping the
        // body airborne for the whole run: 40 m of fall at 9.81 m/s^2
        // covers ~4.9 m in one second, so it never reaches the turtle.
    }
    out_start = SPIN0;

    for (int f = 0; f < FRAMES; ++f) {
        ps.update_bvh();
        physics.update(1.0 / 60.0);
    }
    const float spin = std::fabs(ps.lock_particles_for_read()[id].omega_z);
    physics.shutdown();
    return spin;
}

}  // namespace

int main() {
    std::printf("\n=== what slows a spinning body down? ===\n");

    float start = 0.0f;
    const float vacuum = spin_alone(start);
    const float retained = (start > 0.0f) ? (vacuum / start) : 0.0f;

    // The arithmetic the constant implies, stated so the measurement can
    // be checked against it rather than merely reported.
    const float predicted = std::pow(PhysicsV4::ANGULAR_DRAG,
                                     static_cast<float>(FRAMES * 4));

    std::printf("\n-- GEDANKEN-25: a body spinning in vacuum --\n");
    std::printf("  [measure] omega_z %.4f -> %.6f rad/s after %d frames\n",
                start, vacuum, FRAMES);
    std::printf("  [measure] retained %.6f of its spin in one second\n",
                retained);
    std::printf("  [measure] ANGULAR_DRAG^(4 substeps x %d frames) = %.6f\n",
                FRAMES, predicted);
    std::printf("  [note] nothing touched this body: no contact, no bond,\n"
                "         no medium, no torque. Only the constant.\n");

    // The claim is conservation, not "less damping". An isolated body
    // has no external torque, so 5%% is as wrong as 95%%; the tolerance
    // is integrator noise, not an allowance for dissipation.
    check(retained > 0.99f,
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

    std::printf("\n  %s (%d failures)\n",
                failures == 0 ? "ANGULAR DISSIPATION IS PHYSICAL"
                              : "ANGULAR DISSIPATION IS A CONSTANT (expected "
                                "red: this is the before-value)",
                failures);
    return failures == 0 ? 0 : 1;
}
