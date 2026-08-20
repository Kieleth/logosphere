// =============================================================================
// ONE BODY, ONE ORIENTATION — the headless half
// =============================================================================
// Instruments for the quaternion-truth unification, written before the
// engine changes (G-19/20/23; scene and thresholds in
// tests/scenes/scene_orientation_truth.h).
//
//   ./build/test_orientation_truth                        numbers
//   INTERACTIVE=1 ./build/test_orientation_truth_visual    window
// =============================================================================

#include "scenes/scene_orientation_truth.h"

#include <cstdio>
#include <iostream>
#include <cstdlib>
#include <string>

using namespace scene_orientation_truth;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("  %s %s\n", ok ? "[PASS]" : "[FAIL]", what.c_str());
    if (!ok) failures++;
}
}  // namespace

int main() {
    std::printf("\n=== one body, one orientation ===\n");

    std::printf("\n-- O0 (G-20): the compass round trip, swept --\n");
    const float worst = roundtrip_sweep();
    std::printf("  [measure] worst basis-matrix error over the sweep: %.6f\n", worst);
    check(worst < ROUNDTRIP_TOL,
          "from_euler -> to_euler_zyx -> from_euler is identity away from "
          "the gimbal band");

    std::printf("\n-- O1 (G-23): twin cubes, identical spin, two truths --\n");
    // FULL-STATE NARRATION (assert-or-waive, per DOF, via Argus):
    //   x, y        — no lateral force exists: WAIVED, watched by Argus.
    //   z, vz       — must be IDENTICAL between the twins every frame:
    //                 the flag selects a LEDGER, and a ledger choice that
    //                 moved a body would be motion depending on
    //                 bookkeeping. Asserted per frame, max diff printed.
    //   omega       — identical between twins every frame (nothing in
    //                 the angular path branches on the flag before the
    //                 publish), and each frame's spin is 0.8145x the
    //                 last (4 substeps of ANGULAR_DRAG, the audited
    //                 fingerprint). Asserted.
    //   rot_y       — quat twin shows the turn, Euler twin frozen:
    //                 asserted at the end (the headline claim).
    //   coherence   — the MIRROR IDENTITY, asserted every frame: the
    //                 Euler twin's divergence must equal the quat
    //                 twin's visible rot_y, because it is the SAME
    //                 accumulated angle, once shown, once trapped.
    //   rot_x/rot_z — nothing excites them: WAIVED, watched.
    ParticleSystem ps;
    PhysicsSystem physics;
    if (!physics.initialize(ps)) { std::printf("  [FAIL] init\n"); return 1; }
    Scene scene;
    scene.build(ps);
    float max_z_diff = 0.0f, max_omega_diff = 0.0f, max_mirror_err = 0.0f;
    for (int f = 0; f < RUN_FRAMES; ++f) {
        scene.step(ps, physics, f);
        const auto* sq = scene.argus.latest(scene.quat_twin);
        const auto* se = scene.argus.latest(scene.euler_twin);
        if (sq && se) {
            const float zd = std::fabs(sq->z - se->z);
            const float od = std::fabs(sq->oy - se->oy);
            const float me = std::fabs(scene.argus.divergence(scene.euler_twin)
                                       - sq->ry);
            if (zd > max_z_diff) max_z_diff = zd;
            if (od > max_omega_diff) max_omega_diff = od;
            if (me > max_mirror_err) max_mirror_err = me;
        }
    }
    std::printf("  [measure] argus, per frame over %d frames:\n", RUN_FRAMES);
    std::printf("    twins' z differ by at most     %.9f m\n", max_z_diff);
    std::printf("    twins' omega differ by at most %.9f rad/s\n", max_omega_diff);
    std::printf("    mirror identity error at most  %.6f rad\n", max_mirror_err);
    check(max_z_diff == 0.0f,
          "the ledger flag never moves a body: twin trajectories identical");
    check(max_omega_diff == 0.0f,
          "the ledger flag never touches spin: twin omegas identical");
    check(max_mirror_err < 1e-3f,
          "mirror identity: the trapped angle IS the shown angle, every frame");

    const float dq = scene.divergence(ps, scene.quat_twin);
    const float de = scene.divergence(ps, scene.euler_twin);
    std::printf("  [measure] quat-truth twin:  q-vs-Euler divergence %.4f rad, "
                "visible rot_y %.4f\n", dq, scene.visible_rot_y(ps, scene.quat_twin));
    std::printf("  [measure] Euler-truth twin: q-vs-Euler divergence %.4f rad, "
                "visible rot_y %.4f\n", de, scene.visible_rot_y(ps, scene.euler_twin));
    std::printf("  [note] identical bodies, identical spin. The Euler twin's\n"
                "         quaternion turned while the triple every consumer\n"
                "         reads stayed frozen.\n");
    std::printf("\n  the witness's last three frames:\n");
    scene.argus.dump(std::cout, 3);
    check(dq < COHERENCE_MAX,
          "quat-truth twin: its two representations agree (the control)");
    check(de < COHERENCE_MAX,
          "Euler-truth twin: ONE body must have ONE orientation (G-23)");

    physics.shutdown();

    std::printf("\n-- O2 (G-19): the lever changes NOTHING for a body that "
                "never rotates --\n");
    auto baseline_hash = [](bool lever) {
        ParticleSystem bps;
        PhysicsSystem bphys;
        if (!bphys.initialize(bps)) return 0ULL;
        bphys.set_quat_truth(lever);
        BaselineScene bs;
        bs.build(bps);
        unsigned long long h = 1469598103934665603ULL;
        for (int f = 0; f < RUN_FRAMES; ++f) {
            bps.update_bvh();
            bphys.update(DT);
            bs.hash_state(bps, h);
        }
        bphys.shutdown();
        return h;
    };
    const unsigned long long h_off = baseline_hash(false);
    const unsigned long long h_on  = baseline_hash(true);
    std::printf("  [measure] trajectory+orientation hash, lever OFF: %016llx\n", h_off);
    std::printf("  [measure] trajectory+orientation hash, lever ON:  %016llx\n", h_on);
    check(h_off == h_on && h_off != 0ULL,
          "the default path is bit-identical under the lever (G-19)");

    std::printf("\n-- O3: the lever HEALS the split --\n");
    {
        ParticleSystem lps;
        PhysicsSystem lphys;
        if (!lphys.initialize(lps)) { std::printf("  [FAIL] init\n"); return 1; }
        lphys.set_quat_truth(true);
        Scene lscene;
        lscene.build(lps);
        for (int f = 0; f < RUN_FRAMES; ++f) lscene.step(lps, lphys);
        const float de2 = lscene.divergence(lps, lscene.euler_twin);
        const float vy_q = lscene.visible_rot_y(lps, lscene.quat_twin);
        const float vy_e = lscene.visible_rot_y(lps, lscene.euler_twin);
        std::printf("  [measure] lever ON: Euler twin divergence %.4f rad, "
                    "visible rot_y %.4f (quat twin %.4f)\n", de2, vy_e, vy_q);
        check(de2 < COHERENCE_MAX,
              "lever ON: the previously frozen twin has one orientation");
        check(std::fabs(vy_e - vy_q) < 0.01f,
              "lever ON: identical spins are identically visible");
        lphys.shutdown();
    }
    std::printf("\n  %s (%d failures)\n",
                failures == 0 ? "ONE ORIENTATION"
                              : "TWO TRUTHS (expected red until the "
                                "unification lands)",
                failures);
    return failures == 0 ? 0 : 1;
}
