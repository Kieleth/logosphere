// =============================================================================
// THE REFUSAL LEDGER IS COMPLETE, OR IT IS A LIE
// =============================================================================
// When a body cannot take momentum — an external writer owns it — the
// solver stops the striker anyway and the share the braced body should
// have taken goes somewhere. Since 2026-08-14 it is BOOKED
// (PhysicsSystem::record_refused_impulse) so the body's owner can decide
// what a shove means. That ledger is about to be consumed by policy
// (knockback, stagger, ragdoll), which makes its COMPLETENESS the whole
// question: a drain that receives a fraction of the truth is worse than
// no drain at all, because the fraction looks like an answer.
//
// WHAT THIS CAUGHT (F1 RCA, 2026-08-14): the ledger booked 2.5% of the
// truth. A KINEMATIC target refused 1357.8 kg*m/s and the book held
// 33.6. Two doors spent momentum outside the booking loop — the warm
// start (physics_system_v4.cpp, cached impulses applied before the
// iterations) and the entire friction block, which booked nothing at
// all. Both are fixed; this test is what keeps them fixed.
//
// THE MEASUREMENT. One striker, one braced target, airborne so no floor
// row can muddy the accounting. The striker's momentum change IS the
// momentum the contact moved; the target takes none of it. So the book
// must hold what the striker lost, to within the tolerance a converging
// solver leaves. No expected value is invented anywhere: the striker's
// own delta is the truth the ledger is checked against.
//
// Run: ./build/test_refused_momentum_ledger
// =============================================================================

// FULL-STATE NARRATION (assert-or-waive, per DOF, owner directive
// 2026-08-19). Two bodies, observed through Argus so the asserts and the
// log read one source.
//
//   TARGET (KINEMATIC, braced, airborne)
//     vx            — ASSERTED at zero: it is what "refused" means.
//     vy, vz        — ASSERTED at zero too. A braced body that took the
//                     strike sideways would have refused nothing, and
//                     the original test could not have told.
//     position xyz  — ASSERTED unmoved on all three axes. Zero velocity
//                     at the deadline does not prove it never moved.
//     omega         — ASSERTED at zero: the solver may not spin a body
//                     its external writer owns, any more than shove it.
//     orientation   — follows omega; ASSERTED at identity.
//
//   STRIKER (DYNAMIC, airborne, 9 m/s along +X)
//     vx            — ASSERTED: its delta IS the truth the ledger is
//                     checked against. No expected value is invented.
//     vy            — ASSERTED at zero: the strike is square, so nothing
//                     may come out sideways.
//     vz, z         — WAIVED: it is airborne with gravity on, so it
//                     falls, deliberately — the fall is what keeps a
//                     floor row out of the accounting. Printed.
//     y             — ASSERTED unmoved, the position form of the vy
//                     claim.
//     omega         — ASSERTED at zero. Both boxes are centred on the
//                     same y and z, so this strike has no lever arm and
//                     zero is the RIGHT answer, not D2's missing torque.
//     orientation   — ASSERTED at identity; coherence between the two
//                     ledgers ASSERTED (G-23).
//
//   THE LEDGER
//     along +X      — ASSERTED against the striker's own momentum delta.
//     along Y       — ASSERTED at zero: nothing whatever acts on that
//                     axis in this experiment.
//     along Z       — ASSERTED, and NOT at zero, which is what measuring
//                     it taught. Both off-axis columns were already
//                     being summed by this test and then silently
//                     dropped; the first version of this narration
//                     predicted both would be zero and the Z column
//                     measured -132.449 kg*m/s. It is real and it is
//                     the SECOND door of the F1 RCA doing its job: the
//                     striker comes to rest against the braced face and
//                     then slides DOWN it under gravity, and the
//                     friction that resists the slide is refused by the
//                     braced body exactly as the normal load was. The
//                     striker's own numbers confirm it: at the deadline
//                     it is falling at 5.70 m/s where free fall would
//                     give 6.53, and 160 kg times that 0.83 m/s deficit
//                     is 133 kg*m/s against the 132.4 in the book. So
//                     the Z column is checked the same way the X column
//                     is — against the striker's own delta, no invented
//                     expected value — and additionally held inside the
//                     Coulomb cone that bounds any friction refusal.
//
//   RELATIVE (Argus)
//     approach      — ASSERTED: peak closing speed is the release speed,
//                     so the striker really was aimed at the target.
//     separation    — ASSERTED: the closest the two ever came is the sum
//                     of their half-extents, which is what "it struck
//                     the target" means geometrically. The old test
//                     inferred the strike from a momentum number, which
//                     a spurious overlap elsewhere could also produce.

#include "core/argus.h"
#include "core/particle_system.h"
#include "logosphere/physics/physics_system.h"
#include "particle.h"
#include "generated/physics_constants.h"

#include <cmath>
#include <cstdio>
#include <iostream>

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    printf("  %s %s\n", ok ? "[PASS]" : "[FAIL]", what);
    if (!ok) failures++;
}

}  // namespace

int main() {
    printf("\n=== THE REFUSAL LEDGER: complete, or a lie ===\n");

    ParticleSystem ps;
    PhysicsSystem physics;
    if (!physics.initialize(ps)) { printf("  [FAIL] init\n"); return 1; }

    auto spawn = [&](float x, float z, float size, Materials::Type mat,
                     float vx) {
        Particle p{};
        p.x = x; p.y = 0.0f; p.z = z;
        p.shape = ParticleShape::BOX;
        p.width = p.height = p.thickness = size;
        p.size = size;
        p.vx = vx;
        p.SetMaterial(mat);
        int id = ps.queue_particle_addition(p);
        ps.flush_pending_particles();
        return id;
    };

    // Airborne, well clear of the turtle: the only contact in this world
    // is the one under test.
    const int striker = spawn(-1.2f, 6.0f, 0.4f, Materials::Type::STONE, 9.0f);
    const int target  = spawn( 0.0f, 6.0f, 0.6f, Materials::Type::STONE, 0.0f);
    {
        auto v = ps.lock_particles_for_write();
        v[target].solver_mode = ParticleSolverMode::KINEMATIC;   // braced
        v[target].owner = ParticleOwner::DYNAMICS;
    }

    float m_striker = 0.0f, vx0 = 0.0f;
    {
        auto v = ps.lock_particles_for_read();
        m_striker = v[striker].GetMass();
        vx0 = v[striker].vx;
    }

    // The witness. Read-only over the particles; it cannot perturb the
    // strike it is here to observe.
    logosphere::Argus argus;
    argus.watch(striker, "striker");
    argus.watch(target, "target");
    float tx0 = 0.0f, ty0 = 0.0f, tz0 = 0.0f, sy0 = 0.0f;
    {
        auto v = ps.lock_particles_for_read();
        tx0 = v[target].x; ty0 = v[target].y; tz0 = v[target].z;
        sy0 = v[striker].y;
    }
    // Latched per frame from the witness.
    float min_sep = 1e9f, peak_approach = 0.0f;
    float target_move = 0.0f, striker_lateral = 0.0f;
    float target_spin = 0.0f, striker_spin = 0.0f;
    float target_tilt = 0.0f, striker_tilt = 0.0f;
    float max_div = 0.0f, target_off_axis_v = 0.0f, striker_lateral_v = 0.0f;

    float booked_x = 0.0f, booked_y = 0.0f, booked_z = 0.0f;
    int striker_awake_frames = 0;   // sleep suspends gravity (INV-18):
                                    // the truth integrates over AWAKE time
    for (int f = 0; f < 40; ++f) {
        ps.update_bvh();
        physics.update(1.0 / 60.0);
        argus.observe(ps, f);
        { auto v = ps.lock_particles_for_read();
          if (!v[striker].is_at_rest) striker_awake_frames++; }
        {
            const logosphere::Argus::State* st = argus.latest(target);
            const logosphere::Argus::State* ss = argus.latest(striker);
            if (st && ss) {
                const float sep = argus.separation(striker, target);
                if (sep >= 0.0f && sep < min_sep) min_sep = sep;
                const float app = argus.approach_speed(striker, target);
                if (app > peak_approach) peak_approach = app;
                const float dx = st->x - tx0, dy = st->y - ty0, dz = st->z - tz0;
                const float d = std::sqrt(dx*dx + dy*dy + dz*dz);
                if (d > target_move) target_move = d;
                const float ly = std::fabs(ss->y - sy0);
                if (ly > striker_lateral) striker_lateral = ly;
                const float tv = std::sqrt(st->vx*st->vx + st->vy*st->vy
                                         + st->vz*st->vz);
                if (tv > target_off_axis_v) target_off_axis_v = tv;
                if (std::fabs(ss->vy) > striker_lateral_v)
                    striker_lateral_v = std::fabs(ss->vy);
                const float ts = argus.spin(target), sspin = argus.spin(striker);
                if (ts > target_spin) target_spin = ts;
                if (sspin > striker_spin) striker_spin = sspin;
                auto tilt = [](const logosphere::Argus::State& s) {
                    return std::fmax(std::fabs(s.rx),
                           std::fmax(std::fabs(s.ry), std::fabs(s.rz)));
                };
                if (tilt(*st) > target_tilt) target_tilt = tilt(*st);
                if (tilt(*ss) > striker_tilt) striker_tilt = tilt(*ss);
                const float dv = std::fmax(argus.divergence(striker),
                                           argus.divergence(target));
                if (dv > max_div) max_div = dv;
            }
        }
        float jx, jy, jz;
        const size_t n = ps.lock_particles_for_read().size();
        for (size_t pid = 0; pid < n; ++pid)
            if (physics.take_refused_impulse(pid, jx, jy, jz)) {
                booked_x += jx; booked_y += jy; booked_z += jz;
            }
    }

    float vx1 = 0.0f, target_vx = 0.0f;
    {
        auto v = ps.lock_particles_for_read();
        vx1 = v[striker].vx;
        target_vx = v[target].vx;
    }

    const float lost = m_striker * (vx0 - vx1);   // what the contact moved
    const float ratio = (lost > 0.01f) ? (booked_x / lost) : 0.0f;

    printf("  [measure] striker %.1f kg: vx %.2f -> %.2f, momentum lost "
           "%.1f kg*m/s\n", m_striker, vx0, vx1, lost);
    printf("  [measure] target vx %.4f (braced: must not move)\n", target_vx);
    printf("  [measure] ledger holds %.1f along the strike "
           "(%.1f%% of the truth)\n", booked_x, 100.0f * ratio);

    // --- the witness: the strike as geometry, and the rest of the state
    const float TOUCH = 0.5f * (0.4f + 0.6f);   // half-extent sum, on x
    printf("  [argus] closest approach %.4f m (their faces meet at %.4f)\n",
           min_sep, TOUCH);
    printf("  [argus] peak closing speed %.3f m/s (released at %.3f)\n",
           peak_approach, vx0);
    printf("  [argus] target moved %.2e m, |v| peaked at %.2e m/s, spin "
           "%.2e rad/s, tilt %.2e rad\n",
           target_move, target_off_axis_v, target_spin, target_tilt);
    printf("  [argus] striker lateral drift %.2e m, |vy| %.2e m/s, spin "
           "%.2e rad/s, tilt %.2e rad\n",
           striker_lateral, striker_lateral_v, striker_spin, striker_tilt);
    printf("  [argus] worst q-vs-Euler divergence %.6f rad\n", max_div);
    // The Z column, checked the same way the X column is: against the
    // striker's OWN delta. It rests on the braced face and slides down
    // it, so the friction the target refuses shows up as a shortfall in
    // the striker's fall. No expected value is invented here either.
    // Sleep is a cache over dynamics (INV-18): a sleeping body receives
    // no gravity, so no vertical momentum exists in those frames for the
    // face to refuse. G-44 made this reachable here: the full-Jacobian
    // warm start satisfies the rows so completely that no live impulse
    // resets the rest counter, and the striker legitimately sleeps
    // pressed flat (equilibrium, quiet, non-growing) mid-window. The
    // truth therefore integrates gravity over MEASURED awake time, not
    // wall time. Still no invented value: awake frames are counted, and
    // the striker's own vz supplies the residual as before.
    const float t_run = (float)striker_awake_frames / 60.0f;
    const float vz_freefall = -PhysicsV4::GRAVITY * t_run;
    float vz_now = 0.0f;
    { auto v = ps.lock_particles_for_read(); vz_now = v[striker].vz; }
    const float held_up = m_striker * (vz_now - vz_freefall);   // kg*m/s, +Z
    const float z_ratio = (std::fabs(held_up) > 1.0f)
                        ? (-booked_z / held_up) : 0.0f;
    printf("  [measure] ledger off-axis: y %.3f, z %.3f kg*m/s\n",
           booked_y, booked_z);
    printf("  [measure] striker vz %.3f vs free fall %.3f over %d awake "
           "frames: the braced face held up %.1f kg*m/s (ledger z holds "
           "%.0f%% of it)\n",
           vz_now, vz_freefall, striker_awake_frames, held_up,
           100.0f * z_ratio);
    printf("\n  the witness's last two frames:\n");
    argus.dump(std::cout, 2);
    printf("\n");

    check(min_sep < TOUCH + 0.01f,
          "the striker actually REACHED the target: closest approach is "
          "the sum of their half-extents, which is what a strike is "
          "geometrically. A momentum number alone cannot say that.");
    check(peak_approach > 0.9f * vx0,
          "and it was closing on the target at its release speed, not "
          "grazing something else");
    check(std::fabs(target_vx) < 1e-4f,
          "the braced body did not move (it really refused)");
    check(target_off_axis_v < 1e-4f,
          "and it refused on EVERY axis, not just along the strike");
    check(target_move < 1e-4f,
          "and it never moved at any point, not merely at the deadline");
    check(target_spin < 1e-4f && target_tilt < 1e-4f,
          "and the solver did not SPIN the body its writer owns either");
    check(lost > 1.0f, "the strike really landed");
    check(striker_lateral < 1e-3f && striker_lateral_v < 1e-3f,
          "the strike was square: nothing came out sideways");
    check(striker_spin < 1e-4f && striker_tilt < 1e-4f,
          "and nothing spun the striker: both boxes share a y and a z, so "
          "this contact has no lever arm and zero is the right answer "
          "(not D2's missing torque)");
    check(ratio > 0.90f,
          "the ledger holds what the braced body refused (>90%)");
    check(std::fabs(booked_y) < 0.01f * std::fabs(booked_x),
          "nothing is booked along Y, because nothing acts along Y: the "
          "off-axis columns were summed by this test from the start and "
          "then dropped, so a book with the right total pointing the "
          "wrong way would have read as complete");
    check(z_ratio > 0.90f && z_ratio < 1.10f,
          "the FRICTION refusal is booked too, and in the right amount: "
          "the striker slides down the braced face, and the vertical "
          "momentum the face held up (from the striker's own fall, not an "
          "invented value) is what the Z column holds. This is the second "
          "door of the F1 RCA — the friction block that once booked "
          "nothing at all.");
    check(std::fabs(booked_z) <=
              PhysicsV4::FRICTION_COEFFICIENT * std::fabs(booked_x) + 1.0f,
          "and that friction refusal stays inside the Coulomb cone the "
          "normal refusal defines");
    check(max_div < 0.01f,
          "one body, one orientation, both bodies, every frame (G-23)");

    printf("\n  %s (%d failures)\n",
           failures == 0 ? "LEDGER COMPLETE" : "LEDGER INCOMPLETE", failures);
    return failures == 0 ? 0 : 1;
}
