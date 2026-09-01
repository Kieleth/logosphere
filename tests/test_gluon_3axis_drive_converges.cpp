// =============================================================================
// GLUON 3-AXIS ANGULAR DRIVE CONVERGENCE — INV-13, the free-pair carrier
// =============================================================================
// Two identical STONE boxes, nailed together, no other body in the world.
// The nail carries a quaternion angular drive commanded to pi/4 about +Y.
// Nothing else acts on the pair except gravity, which is a COMMON force and
// cannot change the relative pose the drive owns. So the relative
// orientation is the one degree of freedom the experiment is about, and
// every other degree of freedom is a control: it says whether the drive
// reached its target by driving, or by dragging the pair somewhere.
//
// This is the quaternion (Rodrigues) row path:
//   - gluon->use_quat_target + target_relative_q
//   - one Rodrigues-axis vector row rebuilt per substep from q_err
//   - omega_x/y integration + rotation_q exponential-map integration
//   - the split-impulse position pass (angular half) repairing the row
//
// -----------------------------------------------------------------------------
// FULL-STATE NARRATION — assert-or-waive, per degree of freedom
// (owner directive 2026-08-19; every DOF below is asserted with its law ID
//  or waived on this list, by name, with its measured value printed)
// -----------------------------------------------------------------------------
// CAST (Argus watches both, and the asserts read Argus, not the particles):
//   A = parent box, STONE, 0.1 m cube, spawned at (0,0,5.0)
//   B = child  box, STONE, 0.1 m cube, spawned at (0,0,5.3)
//   nail: offsets +-0.05 m on local Z, angular drive ON, target pi/4 about +Y
//
// PHASES, as the test declares them:
//   DRIVE-UP  f0..f59    the commanded pose is reached from rest
//   HOLD      f60..f119  the pose is "held"
//
// PHASES, AS MEASURED (2026-09-01, this tree, default levers). The pair is
// NOT supported and gravity acts the whole run. Argus puts the fall at
// 5.000 m -> 0.058 m and the turtle strike at FRAME 61, at 9.68 m/s —
// one frame after the nominal hold window opens. So:
//
//   f0..f60    FREE FLIGHT. The drive converges at f20 and then holds
//              exactly: |q_err| 0.0021 rad flat, |omega_rel| 0.0000,
//              |omega_A + omega_B| 0.00000, rotY split -0.3915/+0.3915
//              (pi/8 each way, to 1.2 mrad).
//   f61..f119  CRASH AND RING-DOWN. |q_err| wanders 0.0021..0.0452,
//              |omega_rel| peaks near 3.1 rad/s, the nail stretches 68 mm.
//
// THE HOLD ASSERT THEREFORE DOES NOT MEASURE A HOLD. It measures how the
// drive survives a 9.7 m/s ground impact, which is why this test moves
// when the CONTACT solver changes and not when the drive does. Recorded
// here rather than fixed: the scene is not this conversion's to change.
//
// PER-DOF DISPOSITION:
//  1. relative orientation (the drive DOF, |q_err| about the Rodrigues
//     axis) — ASSERTED, INV-13, both phases, at the budgets this test has
//     always used (5 % of target final, 10 % of target through hold).
//  2. relative-orientation SETTLING (does the error stop moving, or does
//     it limit-cycle?) — MEASURED and printed as the hold band
//     (max - min) and the hold mean. INV-13 says "without oscillation" and
//     INV-24 says a correction that fires forever is a defect, but neither
//     names a numeric band for a driven joint. NOT asserted: no registry
//     law sets the budget. Reported as a missing-law candidate.
//  3. relative angular velocity at the end of hold — MEASURED, printed.
//     Same gap as (2): INV-24's "zero corrective work at steady state" has
//     no numeric carrier for a drive. NOT asserted, waived by name.
//  4. pair TOTAL angular momentum, free-flight window only — ASSERTED
//     as |omega_A + omega_B| (the two bodies are identical, so the sum is
//     proportional to L). An internal drive is an equal-and-opposite
//     torque pair: it may split the rotation, never spin the pair. Tagged
//     "hygiene" — see the report: this is a missing-law candidate, the
//     angular twin of INV-20's momentum-bookkeeping argument.
//  5. relative position / nail separation — ASSERTED over the FREE-FLIGHT
//     window only, budget PhysicsV4::SLOP (1 mm, a registry constant, not
//     a number invented here); derives from INV-26, one correction law for
//     the linear row and the angular row alike. The reference is the
//     DRIVEN pose's separation (0.2968 m at f30), not the undriven 0.3000:
//     the attachment offsets rotate with the child, so the driven pose has
//     a different and geometrically correct separation. The post-impact
//     excursion (68 mm) is measured and WAIVED: no law prices a nail's
//     stretch through a 9.7 m/s strike.
//  6. common-mode position and velocity (both bodies falling together) —
//     WAIVED from assertion, MEASURED and printed. Gravity is external and
//     common; the drive owns nothing here. A divergence between the two
//     bodies' fall WOULD show as (5).
//  7. q-vs-Euler coherence, both bodies — ASSERTED, GEDANKEN-21 two-band
//     contract (0.01 rad sharp, 0.015 rad inside Argus::FOLD_BAND). One
//     body, one orientation: a drive that integrates the quaternion while
//     the Euler ledger says something else is GEDANKEN-23's live defect.
//  8. peak linear speed — WAIVED from assertion, printed. The explosion
//     detector owns velocity ceilings engine-wide (INV-11).
//
// Run: ./build/logosphere-tests --test test_gluon_3axis_drive_converges --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "logosphere/physics/physics_system.h"
#include "../src/particle.h"
#include "../src/math/quat.h"
#include "core/argus.h"
#include "generated/physics_constants.h"
#include <cstdio>
#include <cmath>
#include <iostream>
#include <vector>

bool test_gluon_3axis_drive_converges() {
    printf("\n=== Gluon 3-axis Angular Drive Convergence (INV-13) ===\n");
    printf("  DEMONSTRATING: a nail's quaternion drive reaches pi/4 about +Y\n"
           "  between two free identical boxes, and holds it, without the pair\n"
           "  itself acquiring rotation and without the nail letting go.\n");

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "3-axis drive";
    cfg.enable_chat_window = false;
    if (engine.initialize(cfg) != 0) {
        printf("  ERROR: engine init failed\n");
        return false;
    }

    auto& ps      = engine.get_particle_system();
    auto& physics = engine.get_physics_system();

    // Two free-floating particles, stacked on Z.
    Particle a = {};
    a.shape = ParticleShape::BOX;
    a.x = 0.0f; a.y = 0.0f; a.z = 5.0f;
    a.width = 0.1f; a.height = 0.1f; a.thickness = 0.1f;
    a.r = 0.7f; a.g = 0.4f; a.b = 0.4f; a.a = 1.0f;
    a.SetMaterial(Materials::Type::STONE);
    a.is_at_rest = false;
    int a_id = engine.add_particle(a);

    Particle b = {};
    b.shape = ParticleShape::BOX;
    b.x = 0.0f; b.y = 0.0f; b.z = 5.3f;
    b.width = 0.1f; b.height = 0.1f; b.thickness = 0.1f;
    b.r = 0.4f; b.g = 0.7f; b.b = 0.4f; b.a = 1.0f;
    b.SetMaterial(Materials::Type::STONE);
    b.is_at_rest = false;
    int b_id = engine.add_particle(b);

    // Flip both particles into quat-driven mode so the solver reads
    // rotation_q as the authoritative orientation source (Stage 3
    // added an is_quat_driven gate that defaults to false).
    {
        auto view = ps.lock_particles_for_write();
        view[a_id].is_quat_driven = true;
        view[b_id].is_quat_driven = true;
    }

    // Target: rotate child pi/4 around the Y axis relative to parent.
    const float TARGET_THETA = static_cast<float>(M_PI) / 4.0f;
    auto target_q = logosphere::Quat::from_axis_angle(0.0f, 1.0f, 0.0f, TARGET_THETA);

    auto gluon = std::make_unique<NailGluon>();
    gluon->offset_a = Vec3(0.0f, 0.0f, +0.05f);
    gluon->offset_b = Vec3(0.0f, 0.0f, -0.05f);
    gluon->target_distance = 0.2f;
    gluon->breaking_force = 100000.0f;
    gluon->angular_stiffness = 200.0f;
    gluon->angular_damping   = 12.0f;
    gluon->max_relative_rotation = 3.14f;
    gluon->angular_drive_enabled = true;
    gluon->use_quat_target = true;
    gluon->target_relative_q = target_q;

    physics.add_gluon_between(a_id, b_id, std::move(gluon));

    // ARGUS is the instrument: the asserts below read these eyes, and the
    // narration prints the same values, so what is checked and what is seen
    // cannot drift apart.
    logosphere::Argus argus;
    argus.watch(a_id, "A/parent");
    argus.watch(b_id, "B/child");

    const float dt = 1.0f / 60.0f;
    constexpr int FRAMES = 120;
    constexpr int HOLD_START = 60;

    float max_err_settle = 0.0f;
    float max_err_hold   = 0.0f;
    float min_err_hold   = 1e9f;
    float sum_err_hold   = 0.0f;
    int   n_hold         = 0;
    float final_err      = 0.0f;

    // (4) pair total angular momentum, free-flight only.
    float max_pair_spin_free = 0.0f;
    // (5) nail separation. sep0 is the UNDRIVEN pose; the driven pose has a
    // different, geometrically correct separation because the attachment
    // offsets rotate with the child. sep_ref is taken after convergence.
    float sep0 = -1.0f, sep_ref = -1.0f, sep_final = -1.0f;
    float max_sep_dev_free = 0.0f, max_sep_dev_all = 0.0f;
    constexpr int SEP_REF_FRAME = 30;   // well after convergence (f20), well
                                        // before touchdown (measured f61)
    // (6) common-mode fall, and the touchdown probe.
    float z_a0 = 0.0f, z_a_final = 0.0f, min_vz = 0.0f;
    int   first_contact_frame = -1;
    float prev_vz_a = 0.0f;
    float impact_speed = 0.0f;
    // (3) residual relative spin: last frame overall, and last FREE frame.
    float rel_spin_final = 0.0f;
    float rel_spin_last_free = 0.0f;
    float err_last_free = 0.0f;
    int   last_free_frame = -1;

    // The error the asserts read comes out of Argus, not out of the
    // particle store: one source for the assert and for the log.
    auto err_from_argus = [&](void) -> float {
        const auto* sa = argus.latest(a_id);
        const auto* sb = argus.latest(b_id);
        if (!sa || !sb) return 0.0f;
        logosphere::Quat q_err = sb->q * sa->q.conjugate() * target_q.conjugate();
        float ax, ay, az, theta;
        q_err.to_axis_angle(ax, ay, az, theta);
        if (theta > static_cast<float>(M_PI)) theta -= 2.0f * static_cast<float>(M_PI);
        return std::abs(theta);
    };

    printf("\n  --- the run (Argus narration every 10 frames) ---\n");
    for (int f = 0; f < FRAMES; ++f) {
        engine.update(dt);
        argus.observe(ps, f);

        const auto* sa = argus.latest(a_id);
        const auto* sb = argus.latest(b_id);
        if (!sa || !sb) { printf("  ERROR: Argus lost a body at f%d\n", f); return false; }

        const float err = err_from_argus();
        final_err = err;

        if (f < HOLD_START) {
            if (err > max_err_settle) max_err_settle = err;
        } else {
            if (err > max_err_hold) max_err_hold = err;
            if (err < min_err_hold) min_err_hold = err;
            sum_err_hold += err; n_hold++;
        }

        // (6) common-mode fall + touchdown probe. FIRST in the frame body,
        // because every other free-flight measure below is gated on it: the
        // strike happens INSIDE the update that this frame reports, so the
        // striking frame must already be outside the free window.
        if (f == 0) z_a0 = sa->z;
        z_a_final = sa->z;
        if (sa->vz < min_vz) min_vz = sa->vz;
        if (f > 2 && first_contact_frame < 0 && sa->vz > prev_vz_a + 0.01f) {
            first_contact_frame = f;
            impact_speed = std::fabs(prev_vz_a);
        }
        prev_vz_a = sa->vz;
        const bool free_flight = (first_contact_frame < 0);

        // (5) separation — Argus answers the relative question directly.
        const float sep = argus.separation(a_id, b_id);
        if (f == 0) sep0 = sep;
        if (f == SEP_REF_FRAME) sep_ref = sep;
        sep_final = sep;
        if (sep_ref > 0.0f) {
            const float dev = std::fabs(sep - sep_ref);
            if (dev > max_sep_dev_all) max_sep_dev_all = dev;
            if (free_flight && dev > max_sep_dev_free) max_sep_dev_free = dev;
        }

        // (4) pair total angular momentum proxy, free-flight window only.
        const float pair_ox = sa->ox + sb->ox;
        const float pair_oy = sa->oy + sb->oy;
        const float pair_oz = sa->oz + sb->oz;
        const float pair_spin = std::sqrt(pair_ox*pair_ox + pair_oy*pair_oy + pair_oz*pair_oz);
        if (free_flight && pair_spin > max_pair_spin_free)
            max_pair_spin_free = pair_spin;

        // (3) relative spin, latest.
        const float rox = sa->ox - sb->ox, roy = sa->oy - sb->oy, roz = sa->oz - sb->oz;
        rel_spin_final = std::sqrt(rox*rox + roy*roy + roz*roz);
        if (free_flight) {
            rel_spin_last_free = rel_spin_final;
            err_last_free      = err;
            last_free_frame    = f;
        }

        if (f % 10 == 0) {
            printf("  [f%3d] %s  |q_err| %+.4f rad  sep %.4f  |w_rel| %.4f  "
                   "|w_A+w_B| %.4f  zA %+.3f  vzA %+.3f\n",
                   f, f < HOLD_START ? "drive-up" : "hold    ",
                   err, sep, rel_spin_final, pair_spin, sa->z, sa->vz);
            argus.narrate(std::cout, a_id);
            argus.narrate(std::cout, b_id);
        }
    }

    const float FINAL_BUDGET = 0.05f * TARGET_THETA;
    const float HOLD_BUDGET  = 0.10f * TARGET_THETA;
    const float mean_err_hold = n_hold > 0 ? sum_err_hold / (float)n_hold : 0.0f;
    const float hold_band = (n_hold > 0) ? (max_err_hold - min_err_hold) : 0.0f;

    // Two-band orientation coherence, the same contract the cube-drop and
    // ramp ladders enforce (GEDANKEN-21, closed 2026-08-21).
    constexpr float DIV_MAX_SHARP = 0.01f;
    constexpr float DIV_MAX_FOLD  = 0.015f;
    const float div_a_sharp = argus.peak_divergence(a_id, false);
    const float div_a_fold  = argus.peak_divergence(a_id, true);
    const float div_b_sharp = argus.peak_divergence(b_id, false);
    const float div_b_fold  = argus.peak_divergence(b_id, true);

    printf("\n  --- WHAT THE RUN ACTUALLY WAS (read this before the budgets) ---\n");
    printf("  The pair is unsupported and falls the whole run. Argus puts the\n"
           "  turtle strike at frame %d, at %.3f m/s, and the nominal HOLD window\n"
           "  opens at frame %d. So frames %d..%d are FREE FLIGHT and frames\n"
           "  %d..%d are the crash and its ring-down: this test's \"hold phase\"\n"
           "  is a 9.7 m/s ground impact, not a quiet hold. Both regimes are\n"
           "  measured separately below.\n",
           first_contact_frame, impact_speed, HOLD_START,
           0, last_free_frame, first_contact_frame, FRAMES - 1);

    printf("\n  --- MEASURES (every narrated DOF, asserted or waived) ---\n");
    printf("  (1) drive DOF   target        %.4f rad about +Y\n", TARGET_THETA);
    printf("      FREE FLIGHT: |q_err| at f%d (last free frame)  %.4f rad\n",
           last_free_frame, err_last_free);
    printf("      final |q_err| (f%d, post-impact)  %.4f   (budget %.4f, 5%% of target)\n",
           FRAMES - 1, final_err, FINAL_BUDGET);
    printf("      max |q_err| in drive-up   %.4f   (starts AT the target: rest pose)\n",
           max_err_settle);
    printf("      max |q_err| in hold       %.4f   (budget %.4f, 10%% of target)\n",
           max_err_hold, HOLD_BUDGET);
    printf("  (2) settling    hold min      %.4f   hold mean %.4f   BAND %.4f rad\n",
           (n_hold > 0 ? min_err_hold : 0.0f), mean_err_hold, hold_band);
    printf("  (3) residual    |omega_rel| last free frame %.4f  |  last frame %.4f rad/s\n",
           rel_spin_last_free, rel_spin_final);
    printf("  (4) pair spin   max |omega_A + omega_B| in free flight  %.5f rad/s\n",
           max_pair_spin_free);
    printf("  (5) nail        sep undriven f0 %.4f -> driven ref f%d %.4f -> final %.4f m\n",
           sep0, SEP_REF_FRAME, sep_ref, sep_final);
    printf("      max |sep - ref|  free flight %.5f m   |   whole run %.5f m\n",
           max_sep_dev_free, max_sep_dev_all);
    printf("  (6) common fall zA %.3f -> %.3f m, min vzA %.3f m/s, "
           "turtle strike at frame %d\n",
           z_a0, z_a_final, min_vz, first_contact_frame);
    printf("  (7) coherence   A sharp %.5f fold %.5f | B sharp %.5f fold %.5f "
           "(budgets %.3f / %.3f)\n",
           div_a_sharp, div_a_fold, div_b_sharp, div_b_fold,
           DIV_MAX_SHARP, DIV_MAX_FOLD);
    printf("  (8) peak speed  A %.3f  B %.3f m/s   peak |omega| A %.3f  B %.3f rad/s\n",
           argus.peak_speed(a_id), argus.peak_speed(b_id),
           argus.peak_spin(a_id), argus.peak_spin(b_id));

    // ---- ASSERTS. Every line carries the registry ID of the law it enforces.
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        printf("    %s %s\n", ok ? "[PASS]" : "[FAIL]", what);
        if (!ok) failures++;
    };

    printf("\n  --- ASSERTS (each names its law) ---\n");
    const bool final_ok = (final_err    <= FINAL_BUDGET);
    const bool hold_ok  = (max_err_hold <= HOLD_BUDGET);
    check(final_ok,
          "INV-13: the driven joint converges — final |q_err| within 5% of pi/4");
    check(hold_ok,
          "INV-13: and HOLDS it — max |q_err| through hold within 10% of pi/4");
    check(max_sep_dev_free <= PhysicsV4::SLOP,
          "hygiene (derives INV-26, budget = PhysicsV4::SLOP): through free "
          "flight the nail holds the driven pose's separation to 1 mm — the "
          "angular row swings the pair without the linear row letting go");
    check(max_pair_spin_free <= 1e-4f,
          "hygiene (missing-law candidate, angular twin of INV-20): an internal "
          "drive splits rotation, it never spins the free pair — "
          "|omega_A + omega_B| stays at zero in free flight");
    check(div_a_sharp < DIV_MAX_SHARP && div_a_fold < DIV_MAX_FOLD &&
          div_b_sharp < DIV_MAX_SHARP && div_b_fold < DIV_MAX_FOLD,
          "G-21: one body, one orientation — the quaternion the drive "
          "integrates and the published Euler ledger agree, two-band");

    printf("\n  --- WAIVED, by name (measured above, not asserted) ---\n");
    printf("    (2) hold-phase oscillation band %.4f rad (min %.4f, max %.4f) "
           "against a final-frame budget of %.4f:\n"
           "        the band's TOP %s the budget by %.4f rad, so which value "
           "the last frame lands on decides the\n"
           "        first assert. The quantity being judged has not settled; "
           "the frame number has. INV-13 says\n"
           "        \"without oscillation\" and INV-24 says corrections "
           "terminate, but neither carries a numeric band\n"
           "        for a driven joint. Missing-law candidate, reported, not "
           "invented here.\n",
           hold_band, (n_hold > 0 ? min_err_hold : 0.0f), max_err_hold,
           FINAL_BUDGET,
           (max_err_hold > FINAL_BUDGET) ? "EXCEEDS" : "clears",
           std::fabs(max_err_hold - FINAL_BUDGET));
    printf("    (3) residual |omega_rel| %.4f rad/s at the last frame: same gap "
           "— INV-24 has no numeric steady-state carrier\n"
           "        for a drive. Note the free-flight value is %.4f: the "
           "residual is the crash ringing, not the drive.\n",
           rel_spin_final, rel_spin_last_free);
    printf("    (5) post-impact separation excursion %.5f m: no law prices a "
           "nail's stretch through a 9.7 m/s strike.\n", max_sep_dev_all);
    printf("    (6) common-mode fall: gravity is external and common to both "
           "bodies; the drive owns nothing here. Any\n"
           "        divergence between the two falls would appear in (5).\n");
    printf("    (8) peak speed: the explosion detector owns velocity ceilings "
           "engine-wide (INV-11).\n");

    // The verdict is the same two asserts this test has always returned,
    // at the same budgets. The added law-tagged lines are instrumentation
    // of DOFs that were previously unwatched; they report into `failures`
    // so a regression in them is visible, but they were measured green
    // before they were written (see the commit body).
    bool ok = (failures == 0);
    printf("\n  %s\n", ok ? "[PASS]" : "[FAIL - 3-axis PD drive did not converge]");
    return ok;
}
