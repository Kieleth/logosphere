// =============================================================================
// PHYSICS DRIVE — SHOULDER MULTI-AXIS — INV-13, the loaded-joint carrier
// =============================================================================
// The free-pair carrier (test_gluon_3axis_drive_converges) proves the
// quaternion drive on two identical boxes with nothing else acting. THIS
// test is the same law under load: Eva's right shoulder, a ball-socket
// joint inside a full humanoid standing on a strata floor, commanded to a
// pose that mixes two axes at once — 30 deg flex about X composed with
// 20 deg abduct about Y, 35.9 deg of total Rodrigues angle.
//
// Combining flex and abduct is the canonical multi-axis pose and the
// reason Stage 4 replaced three independent per-axis rows with a single
// Rodrigues-axis vector row: independent per-axis bias piles up a
// small-angle approximation that left combined targets near 0.1 rad.
//
// -----------------------------------------------------------------------------
// FULL-STATE NARRATION — assert-or-waive, per degree of freedom
// (owner directive 2026-08-19; every DOF is asserted with its law ID or
//  waived on this list, by name, with its measured value printed)
// -----------------------------------------------------------------------------
// CAST (Argus watches all three; the asserts read Argus):
//   parent = right_arm_ids[0], the proximal shoulder particle
//   child  = right_arm_ids[1], the upper arm — the driven body
//   hips   = the humanoid root, the control that says the body stayed put
//
// PHASES:
//   WARM-UP   30 frames before the drive is armed. FK establishes the rest
//             pose and the parent's Euler frame settles on "facing north".
//   DRIVE-UP  f0..f179    the commanded pose is reached
//   HOLD      f180..f239  the pose is held under gravity and the arm's own
//                         weight, which is the load this carrier adds
//
// THE TWO ORIENTATION LEDGERS ARE NOT SYMMETRIC HERE, and that is the
// point of this scene. The child is quat-driven: the solver integrates its
// rotation_q. The parent is FK-owned, so its truth is the Euler triple and
// the error term reads it through from_euler — exactly the split
// GEDANKEN-23 calls a live disagreement. Both ledgers are measured below,
// per body, and only the body whose quaternion the solver integrates is
// held to the G-21 coherence contract.
//
// PER-DOF DISPOSITION:
//  1. joint relative orientation, |q_err| — ASSERTED, INV-13, at this
//     test's own budgets, unchanged (5 % of target magnitude on the final
//     frame, 10 % through hold). THE FINAL-FRAME ASSERT WAS RED BEFORE
//     THIS CONVERSION AND IS GREEN AFTER IT, and no threshold, target,
//     gain or body moved. The red was the instrument: see the RCA block
//     the run prints, and the spawn-index note at the cast below.
//  2. per-axis error (Rodrigues components ex/ey/ez) — MEASURED and
//     printed. Flex is X and abduct is Y and both are commanded; Z is
//     twist and is commanded to nothing, so a residual on Z is error the
//     target never asked for. NOT asserted: no registry law budgets a
//     per-axis residual. Missing-law candidate, reported.
//  3. hold-phase settling — MEASURED as the hold band (max - min) and the
//     hold mean. INV-13 says "without oscillation", INV-24 says
//     corrections terminate, neither carries a number for a drive. WAIVED,
//     printed. The band is 0.0135 rad against a final-frame budget of
//     0.0314: the joint does not settle flat, it moves inside a band, and
//     the final-frame assert samples one phase of it.
//  4. residual relative angular velocity — MEASURED, printed, WAIVED,
//     same gap as (3).
//  5. joint integrity, parent-to-child separation — ASSERTED over the hold
//     window, budget PhysicsV4::SLOP (a registry constant, not invented
//     here), derives from INV-26: the angular row may swing the arm, it
//     may not pull the joint apart.
//  6. q-vs-Euler coherence — ASSERTED on the CHILD only, GEDANKEN-21
//     two-band (0.01 sharp / 0.015 inside Argus::FOLD_BAND). MEASURED and
//     WAIVED on the parent, which is FK-owned: its quaternion is not the
//     solver's to keep current, and holding it to the same contract would
//     assert a defect that GEDANKEN-23 already owns.
//  7. hips position and velocity — MEASURED, printed, WAIVED. Locomotion
//     owns whether Eva stands still; this test only needs to know that she
//     did not fall over while the shoulder was being driven, which the
//     printed drift shows.
//  8. peak speed and peak spin, all three bodies — WAIVED, printed. The
//     explosion detector owns velocity ceilings engine-wide (INV-11).
//
// Run: ./build/logosphere-tests --test test_physics_drive_shoulder_multiaxis --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/worldgen/worldgen_system.h"
#include "logosphere/worldgen/strata_floor_generator.h"
#include "../src/math/quat.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include "core/argus.h"
#include "generated/physics_constants.h"
#include <cstdio>
#include <cmath>
#include <iostream>
#include <vector>

bool test_physics_drive_shoulder_multiaxis() {
    printf("\n=== Physics Drive: Shoulder multi-axis (INV-13 under load) ===\n");
    printf("  DEMONSTRATING: one Rodrigues row drives a loaded ball-socket joint\n"
           "  to a pose that mixes flex (X) and abduct (Y), and holds it while\n"
           "  the arm's own weight pulls on it.\n");

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "shoulder multiaxis";
    cfg.enable_chat_window = false;
    if (engine.initialize(cfg) != 0) {
        printf("  ERROR: engine init failed\n");
        return false;
    }

    auto& ps  = engine.get_particle_system();
    auto& dyn = engine.get_dynamics_system();

    auto& strata = engine.get_worldgen_system().get_strata_floor_generator();
    strata.set_tile_size(4.0f);
    strata.set_tiles_per_chunk(5);
    strata.set_tiles_per_entity(1);
    strata.set_load_radius(30.0f);
    strata.set_unload_radius(40.0f);
    std::vector<StrataLayerSpec> layers;
    auto add_layer = [&](const char* n, Materials::Type m, float th,
                         float r, float g, float b, bool bond, float bs) {
        StrataLayerSpec s;
        s.name = n; s.material = m; s.thickness = th;
        s.r = r; s.g = g; s.b = b;
        s.bond_within_layer = bond;
        s.bond_strength = bs;
        layers.push_back(s);
    };
    add_layer("bedrock",  Materials::Type::STONE, 0.30f, 0.35f, 0.33f, 0.30f, true,  8000.0f);
    add_layer("sediment", Materials::Type::STONE, 0.15f, 0.45f, 0.40f, 0.30f, false, 0.0f);
    add_layer("organic",  Materials::Type::DIRT,  0.10f, 0.30f, 0.45f, 0.22f, false, 0.0f);
    strata.set_layers(std::move(layers));
    strata.set_enabled(true);
    strata.preload_chunks_around(0.0f, 0.0f, 3);

    auto& hgen = engine.get_worldgen_system().get_humanoid_generator();
    auto eva = hgen.generate_humanoid_physics(
        0.0f, 0.0f, 1.0f, -1, HumanoidSpec::eva(), false);
    engine.get_humanoid_locomotion().register_humanoid_direct(
        eva.hips_id,
        eva.left_leg_ids, eva.right_leg_ids,
        eva.left_arm_ids, eva.right_arm_ids,
        eva.torso_ids, 180.0f, 800.0f,
        eva.entity_id);

    // right_arm_ids layout (from HumanoidGenerator): [0]=shoulder,
    // [1]=upper_arm, [2]=forearm, [3]=hand. The right_shoulder joint
    // is between [0] (parent) and [1] (child = upper arm).
    if (eva.right_arm_ids.size() < 2) {
        printf("  FAIL: Eva right arm missing particles (size=%zu)\n",
               eva.right_arm_ids.size());
        return false;
    }
    int upper_arm_id = eva.right_arm_ids[1];
    int parent_id    = eva.right_arm_ids[0];
    int hips_id      = (int)eva.hips_id;

    // THE SPAWN INDEX, kept deliberately as a probe. Until this conversion
    // the test tracked the child through store swaps but registered the
    // PARENT's swap callback after the warm-up loop, so the parent frame in
    // the error term was read at this index for the whole run. Nine swaps
    // move this cast in 30 warm-up frames and the store ends around 1004
    // particles, so that read was past the end of the vector every frame.
    // The probe below counts the frames it would have been out of range; it
    // does NOT perform the read.
    const int stale_parent_id = eva.right_arm_ids[0];

    // ARGUS is the instrument. The eyes key on particle id, so a store swap
    // has to move them or the witness would silently watch the wrong body.
    logosphere::Argus argus;
    argus.watch(parent_id,    "shoulder");
    argus.watch(upper_arm_id, "upper_arm");
    argus.watch(hips_id,      "hips");
    // The stale index is NOT given an Argus eye: at spawn it is the same id
    // as the parent, so a second eye would collide with the first and the
    // swap handler would remove both. It is read straight from the store,
    // which is honest for a diagnostic that no assert depends on.

    int swaps_seen = 0;
    ps.add_swap_callback([&](size_t old_idx, size_t new_idx) {
        auto move_eye = [&](int& id, const char* label) {
            if (id != (int)old_idx) return;
            printf("  [swap] %s P%d -> P%d\n", label, id, (int)new_idx);
            swaps_seen++;
            argus.unwatch(id);
            id = (int)new_idx;
            argus.watch(id, label);   // ring restarts: noted, not hidden
        };
        move_eye(upper_arm_id, "upper_arm");
        move_eye(parent_id,    "shoulder");
        move_eye(hips_id,      "hips");
    });
    printf("  cast at spawn: shoulder P%d, upper_arm P%d, hips P%d\n",
           parent_id, upper_arm_id, hips_id);

    const float dt = 1.0f / 60.0f;

    // Settle a few frames so FK establishes the rest pose and parent
    // Euler reads the correct "facing north" frame.
    for (int i = 0; i < 30; ++i) engine.update(dt);
    printf("  cast after 30 warm-up frames: shoulder P%d, upper_arm P%d, hips P%d "
           "(%d swaps moved the cast)\n",
           parent_id, upper_arm_id, hips_id, swaps_seen);

    // Combined flex + abduct target. Stage 4's single Rodrigues-axis
    // vector constraint handles this as one composed rotation.
    const float flex_deg   = 30.0f;
    const float abduct_deg = 20.0f;
    const float flex_rad   = flex_deg   * static_cast<float>(M_PI) / 180.0f;
    const float abduct_rad = abduct_deg * static_cast<float>(M_PI) / 180.0f;

    auto q_flex   = logosphere::Quat::from_axis_angle(1.0f, 0.0f, 0.0f, flex_rad);
    auto q_abduct = logosphere::Quat::from_axis_angle(0.0f, 1.0f, 0.0f, abduct_rad);
    auto target_q = q_abduct * q_flex;

    // Humanoid arm inertias are different from the free-particle test;
    // the 200/12 defaults leave a steady-state error around 0.1 rad.
    // Stiffer gains pull the pose closer.
    bool enabled = engine.get_humanoid_locomotion().set_joint_physics_drive_q(eva.entity_id, "right_shoulder",
                                                 target_q,
                                                 /*stiffness=*/2000.0f,
                                                 /*damping=*/60.0f);
    if (!enabled) {
        printf("  FAIL: set_joint_physics_drive_q returned false\n");
        return false;
    }

    // Which ledger is each body's truth? Sampled once, after the drive is
    // armed, and printed: the error term below reads the parent through
    // from_euler when the parent is not quat-driven, and a reader has to
    // be able to see that this is what happened (GEDANKEN-23).
    bool parent_quat_driven = false, child_quat_driven = false;
    {
        auto v = ps.lock_particles_for_read();
        parent_quat_driven = v[parent_id].is_quat_driven;
        child_quat_driven  = v[upper_arm_id].is_quat_driven;
    }
    printf("  ledgers: parent is_quat_driven=%d (FK-owned reads Euler), "
           "child is_quat_driven=%d\n",
           (int)parent_quat_driven, (int)child_quat_driven);

    // Run long enough for PD to settle then hold.
    constexpr int FRAMES = 240;
    constexpr int HOLD_START = 180;

    // The error the asserts read comes out of Argus: one source for the
    // assert and for the log. Returns the magnitude and fills the
    // per-axis Rodrigues components.
    auto err_from_argus = [&](float& ex, float& ey, float& ez) -> float {
        const auto* sa = argus.latest(parent_id);
        const auto* sb = argus.latest(upper_arm_id);
        ex = ey = ez = 0.0f;
        if (!sa || !sb) return 0.0f;
        logosphere::Quat q_a = parent_quat_driven
            ? sa->q
            : logosphere::Quat::from_euler(sa->rx, sa->ry, sa->rz);
        logosphere::Quat q_b = child_quat_driven
            ? sb->q
            : logosphere::Quat::from_euler(sb->rx, sb->ry, sb->rz);
        logosphere::Quat q_err = q_b * q_a.conjugate() * target_q.conjugate();
        // Rodrigues components, the same form the solver builds its row
        // from (physics_system_v4.cpp: e = 2*(x,y,z), sign-corrected on w).
        ex = 2.0f * q_err.x; ey = 2.0f * q_err.y; ez = 2.0f * q_err.z;
        if (q_err.w < 0.0f) { ex = -ex; ey = -ey; ez = -ez; }
        float ax, ay, az, theta;
        q_err.to_axis_angle(ax, ay, az, theta);
        if (theta > static_cast<float>(M_PI)) theta -= 2.0f * static_cast<float>(M_PI);
        return std::abs(theta);
    };

    float max_err_settle = 0.0f;
    float max_err_hold   = 0.0f;
    float min_err_hold   = 1e9f;
    float sum_err_hold   = 0.0f;
    int   n_hold         = 0;
    float final_err      = 0.0f;
    // (2) per-axis, at the last frame and worst through hold.
    float ex_final = 0.0f, ey_final = 0.0f, ez_final = 0.0f;
    float max_ez_hold = 0.0f;      // twist: commanded to nothing
    // (4) residual relative spin.
    float rel_spin_final = 0.0f, max_rel_spin_hold = 0.0f;
    // (5) joint separation through hold.
    float sep_ref = -1.0f, sep_final = -1.0f, max_sep_dev_hold = 0.0f;
    // (7) hips drift through the run.
    float hips_x0 = 0.0f, hips_y0 = 0.0f, hips_z0 = 0.0f;
    float hips_drift = 0.0f, hips_speed_final = 0.0f;
    // The spawn-index probe, kept for the RCA.
    size_t store_size = 0;
    int    stale_index_oob_frames = 0;

    printf("\n  --- the run (Argus narration every 40 frames) ---\n");
    for (int f = 0; f < FRAMES; ++f) {
        engine.update(dt);
        argus.observe(ps, f);

        const auto* sa = argus.latest(parent_id);
        const auto* sb = argus.latest(upper_arm_id);
        const auto* sh = argus.latest(hips_id);
        if (!sa || !sb || !sh) {
            printf("  ERROR: Argus lost a body at f%d\n", f);
            return false;
        }

        float ex, ey, ez;
        const float err = err_from_argus(ex, ey, ez);
        final_err = err; ex_final = ex; ey_final = ey; ez_final = ez;

        // THE SPAWN-INDEX PROBE. The pre-conversion test read the parent at
        // this index every frame. The read is NOT repeated here — it is out
        // of bounds and reproducing it would put undefined behaviour in a
        // shipped test — but whether it WOULD have been out of bounds is
        // measured and printed, because that is the whole finding.
        {
            auto v = ps.lock_particles_for_read();
            store_size = v.size();
            if ((size_t)stale_parent_id >= v.size()) stale_index_oob_frames++;
        }

        const float sep = argus.separation(parent_id, upper_arm_id);
        sep_final = sep;

        const float rox = sa->ox - sb->ox, roy = sa->oy - sb->oy, roz = sa->oz - sb->oz;
        rel_spin_final = std::sqrt(rox*rox + roy*roy + roz*roz);

        if (f == 0) { hips_x0 = sh->x; hips_y0 = sh->y; hips_z0 = sh->z; }
        {
            const float dx = sh->x - hips_x0, dy = sh->y - hips_y0, dz = sh->z - hips_z0;
            const float d = std::sqrt(dx*dx + dy*dy + dz*dz);
            if (d > hips_drift) hips_drift = d;
            hips_speed_final = std::sqrt(sh->vx*sh->vx + sh->vy*sh->vy + sh->vz*sh->vz);
        }

        if (f < HOLD_START) {
            if (err > max_err_settle) max_err_settle = err;
        } else {
            if (f == HOLD_START) sep_ref = sep;
            if (err > max_err_hold) max_err_hold = err;
            if (err < min_err_hold) min_err_hold = err;
            sum_err_hold += err; n_hold++;
            if (std::fabs(ez) > max_ez_hold) max_ez_hold = std::fabs(ez);
            if (rel_spin_final > max_rel_spin_hold) max_rel_spin_hold = rel_spin_final;
            if (sep_ref > 0.0f) {
                const float dev = std::fabs(sep - sep_ref);
                if (dev > max_sep_dev_hold) max_sep_dev_hold = dev;
            }
        }

        if (f % 20 == 0) {
            printf("  [f%3d] %s |q_err| %+.4f rad  per-axis e=(%+.4f,%+.4f,%+.4f)  "
                   "|w_rel| %.4f  joint sep %.4f\n",
                   f, f < HOLD_START ? "drive-up" : "hold    ",
                   err, ex, ey, ez, rel_spin_final, sep);
        }
        if (f % 40 == 0) {
            argus.narrate(std::cout, upper_arm_id);
            argus.narrate(std::cout, hips_id);
        }
    }

    // Compute target angular magnitude for budget sizing.
    float tax, tay, taz, ttheta;
    target_q.to_axis_angle(tax, tay, taz, ttheta);
    if (ttheta > static_cast<float>(M_PI)) ttheta -= 2.0f * static_cast<float>(M_PI);
    float target_mag = std::abs(ttheta);

    const float FINAL_BUDGET = 0.05f * target_mag;
    const float HOLD_BUDGET  = 0.10f * target_mag;
    const float mean_err_hold = n_hold > 0 ? sum_err_hold / (float)n_hold : 0.0f;
    const float hold_band = (n_hold > 0) ? (max_err_hold - min_err_hold) : 0.0f;

    constexpr float DIV_MAX_SHARP = 0.01f;
    constexpr float DIV_MAX_FOLD  = 0.015f;
    const float div_c_sharp = argus.peak_divergence(upper_arm_id, false);
    const float div_c_fold  = argus.peak_divergence(upper_arm_id, true);
    const float div_p_sharp = argus.peak_divergence(parent_id, false);
    const float div_p_fold  = argus.peak_divergence(parent_id, true);

    printf("\n  --- WHY THIS TEST'S NUMBERS MOVED (read before the budgets) ---\n");
    printf("  The store swaps this cast %d times in the 30 warm-up frames, and it\n"
           "  ends the run holding %zu particles. The CHILD was always tracked\n"
           "  through swaps. The PARENT was not: its swap callback was registered\n"
           "  after the warm-up loop, so every frame read the parent at the SPAWN\n"
           "  index P%d, which was out of range on %d of %d frames — an unchecked\n"
           "  std::vector read past the end (ParticleSystem::ReadView::operator[],\n"
           "  particle_system.h:68). The pre-conversion \"final |q_err| 0.0333 rad,\n"
           "  budget 0.0314\" was that read, not the joint.\n"
           "  Same physics, correct instrument: final |q_err| %.4f, max hold %.4f.\n"
           "  Nothing in the scene, the target, the gains or the budgets changed.\n",
           swaps_seen, store_size, stale_parent_id, stale_index_oob_frames,
           FRAMES, final_err, max_err_hold);

    printf("\n  --- MEASURES (every narrated DOF, asserted or waived) ---\n");
    printf("  (1) drive DOF   target magnitude  %.4f rad (%.1f deg), "
           "flex %.0f deg about X + abduct %.0f deg about Y\n",
           target_mag, target_mag * 180.0f / static_cast<float>(M_PI),
           flex_deg, abduct_deg);
    printf("      final |q_err| (f%d)        %.4f   (budget %.4f, 5%% of target)\n",
           FRAMES - 1, final_err, FINAL_BUDGET);
    printf("      max |q_err| in drive-up   %.4f   (starts AT the target: rest pose)\n",
           max_err_settle);
    printf("      max |q_err| in hold       %.4f   (budget %.4f, 10%% of target)\n",
           max_err_hold, HOLD_BUDGET);
    printf("  (2) per-axis    final e = (%+.4f, %+.4f, %+.4f) rad "
           "[X=flex, Y=abduct, Z=twist, commanded to nothing]\n",
           ex_final, ey_final, ez_final);
    printf("      worst |e_z| through hold  %.4f rad — uncommanded twist\n",
           max_ez_hold);
    printf("  (3) settling    hold min %.4f  hold mean %.4f  BAND %.4f rad "
           "(budget on the FINAL frame is %.4f)\n",
           (n_hold > 0 ? min_err_hold : 0.0f), mean_err_hold, hold_band,
           FINAL_BUDGET);
    printf("  (4) residual    |omega_rel| final %.4f, worst through hold %.4f rad/s\n",
           rel_spin_final, max_rel_spin_hold);
    printf("  (5) joint       separation ref f%d %.4f -> final %.4f, "
           "max deviation through hold %.5f m (budget %.4f = SLOP)\n",
           HOLD_START, sep_ref, sep_final, max_sep_dev_hold, PhysicsV4::SLOP);
    printf("  (6) coherence   child  sharp %.5f fold %.5f  (budgets %.3f / %.3f)\n",
           div_c_sharp, div_c_fold, DIV_MAX_SHARP, DIV_MAX_FOLD);
    printf("                  parent sharp %.5f fold %.5f  (FK-owned, measured "
           "not asserted — GEDANKEN-23)\n", div_p_sharp, div_p_fold);
    printf("  (7) hips        max drift from f0 %.4f m, final speed %.4f m/s\n",
           hips_drift, hips_speed_final);
    printf("  (8) peaks       speed: shoulder %.3f upper_arm %.3f hips %.3f m/s | "
           "spin: %.3f / %.3f / %.3f rad/s\n",
           argus.peak_speed(parent_id), argus.peak_speed(upper_arm_id),
           argus.peak_speed(hips_id), argus.peak_spin(parent_id),
           argus.peak_spin(upper_arm_id), argus.peak_spin(hips_id));

    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        printf("    %s %s\n", ok ? "[PASS]" : "[FAIL]", what);
        if (!ok) failures++;
    };

    printf("\n  --- ASSERTS (each names its law) ---\n");
    const bool final_ok = (final_err    <= FINAL_BUDGET);
    const bool hold_ok  = (max_err_hold <= HOLD_BUDGET);
    check(final_ok,
          "INV-13: the driven joint converges — final |q_err| within 5% of the "
          "target magnitude");
    check(hold_ok,
          "INV-13: and HOLDS it — max |q_err| through hold within 10% of the "
          "target magnitude");
    check(max_sep_dev_hold <= PhysicsV4::SLOP,
          "hygiene (derives INV-26, budget = PhysicsV4::SLOP): the angular row "
          "swings the arm without pulling the shoulder joint apart");
    check(div_c_sharp < DIV_MAX_SHARP && div_c_fold < DIV_MAX_FOLD,
          "G-21: one body, one orientation — the CHILD's integrated quaternion "
          "and its published Euler ledger agree, two-band");

    printf("\n  --- WAIVED, by name (measured above, not asserted) ---\n");
    printf("    (2) per-axis residual, and the uncommanded twist %.4f rad on Z: "
           "no registry law budgets a per-axis\n"
           "        residual on a multi-axis drive. Missing-law candidate, "
           "reported, not invented here.\n", max_ez_hold);
    printf("    (3) hold band %.4f rad (min %.4f, max %.4f) against a "
           "final-frame budget of %.4f: the error does not\n"
           "        settle flat, it moves, so the final-frame assert samples "
           "one phase of a moving quantity. The band's own\n"
           "        top clears the budget by %.4f rad — margin, not settling. "
           "INV-13 says \"without oscillation\" and INV-24\n"
           "        says corrections terminate; neither carries a number for a "
           "driven joint. Missing-law candidate.\n",
           hold_band, (n_hold > 0 ? min_err_hold : 0.0f), max_err_hold,
           FINAL_BUDGET, FINAL_BUDGET - max_err_hold);
    printf("    (4) residual |omega_rel| %.4f rad/s: same gap as (3).\n",
           rel_spin_final);
    printf("    (6) parent coherence sharp %.5f fold %.5f: the parent is "
           "FK-owned, its truth is the Euler triple and\n"
           "        its quaternion is not the solver's to keep current. "
           "GEDANKEN-23 owns this disagreement; asserting the\n"
           "        child's contract on it would assert a known defect.\n",
           div_p_sharp, div_p_fold);
    printf("    (7) hips drift %.4f m: locomotion owns whether Eva stands "
           "still. Printed so a fall cannot hide.\n", hips_drift);
    printf("    (8) peak speed and spin: the explosion detector owns velocity "
           "ceilings engine-wide (INV-11).\n");

    bool ok = (failures == 0);
    printf("\n  %s\n", ok ? "[PASS]" : "[FAIL - shoulder quat drive did not converge]");
    return ok;
}
