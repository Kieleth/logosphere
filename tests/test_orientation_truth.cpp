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
    ParticleSystem ps;
    PhysicsSystem physics;
    if (!physics.initialize(ps)) { std::printf("  [FAIL] init\n"); return 1; }
    Scene scene;
    scene.build(ps);
    for (int f = 0; f < RUN_FRAMES; ++f) scene.step(ps, physics);

    const float dq = scene.divergence(ps, scene.quat_twin);
    const float de = scene.divergence(ps, scene.euler_twin);
    std::printf("  [measure] quat-truth twin:  q-vs-Euler divergence %.4f rad, "
                "visible rot_y %.4f\n", dq, scene.visible_rot_y(ps, scene.quat_twin));
    std::printf("  [measure] Euler-truth twin: q-vs-Euler divergence %.4f rad, "
                "visible rot_y %.4f\n", de, scene.visible_rot_y(ps, scene.euler_twin));
    std::printf("  [note] identical bodies, identical spin. The Euler twin's\n"
                "         quaternion turned while the triple every consumer\n"
                "         reads stayed frozen.\n");
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
