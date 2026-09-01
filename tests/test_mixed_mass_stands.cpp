// =============================================================================
// MIXED MASSES — G-54's single purpose: stand, FALL, stand
// =============================================================================
// A stacking law proven only at 1:1 is proven nowhere. Three unequal
// structures whose statics certifies three different verdicts; the
// overhung cube that MUST fall catches phantom support, the inverse
// defect of the G-48 sink. Split from the multi-case instrument by the
// owner's KISS ruling (2026-08-27).
// =============================================================================
#include "scenes/scene_mixed_mass.h"

#include <cstdio>
#include <iostream>
#include <cstdlib>
#include <string>

using namespace scene_mixed_mass;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("  %s %s\n", ok ? "[PASS]" : "[FAIL]", what.c_str());
    if (!ok) failures++;
}
}  // namespace

int main() {
    std::printf("\n=== MIXED MASSES: three verdicts (G-54) ===\n");
    static const bool trace = std::getenv("ARGUS_TRACE") != nullptr;
    char buf[176];
    ParticleSystem ps;
    PhysicsSystem physics;
    if (!physics.initialize(ps)) { std::printf("  [FAIL] init\n"); return 1; }
    Scene scene;
    const int n = scene.build(ps);
    for (int f = 0; f < RUN_FRAMES; ++f) {
        scene.step(ps, physics, f);
        if (trace && f % 15 == 0)
            for (int i = 0; i < n; ++i)
                scene.argus.narrate(std::cout, scene.boxes[i]);
    }
    for (int i = 0; i < n; ++i)
        std::printf("  [measure] box%d z %.4f (static %s)  peak speed %.4f\n",
                    i, scene.box_z(ps, i),
                    scene.expects_static(i)
                        ? (std::snprintf(buf, sizeof(buf), "%.2f",
                                         scene.static_z(i)), buf)
                        : "waived",
                    scene.argus.peak_speed(scene.boxes[i]));

    for (int i : {M1_SMALL, M1_BIG}) {
        std::snprintf(buf, sizeof(buf),
                      "G-54/INV-4: M1 box%d stands (%.4f vs %.2f, tol %.2f)",
                      i, scene.box_z(ps, i), scene.static_z(i), REST_TOL);
        check(std::fabs(scene.box_z(ps, i) - scene.static_z(i)) < REST_TOL,
              buf);
    }
    // M2: the overhung big cube. Its small perch (M2_SMALL) is measured
    // above and WAIVED: the departing load passes over the perch's own
    // base edge, a marginal case whose statics we have not done.
    std::snprintf(buf, sizeof(buf),
                  "G-54: the overhung big cube DEPARTS its perch "
                  "(final z %.4f < %.2f; static would be %.2f)",
                  scene.box_z(ps, M2_BIG), M2_DEPART_Z,
                  M_SMALL + M_BIG * 0.5f);
    check(scene.box_z(ps, M2_BIG) < M2_DEPART_Z, buf);
    for (int i : {M3_WOOD0, M3_WOOD1, M3_STONE}) {
        std::snprintf(buf, sizeof(buf),
                      "G-54/INV-4: M3 box%d stands (%.4f vs %.2f, tol %.2f)",
                      i, scene.box_z(ps, i), scene.static_z(i), REST_TOL);
        check(std::fabs(scene.box_z(ps, i) - scene.static_z(i)) < REST_TOL,
              buf);
    }
    for (int i = 0; i < n; ++i) {
        std::snprintf(buf, sizeof(buf),
                      "INV-3: box%d speeds stayed bounded (peak %.4f)",
                      i, scene.argus.peak_speed(scene.boxes[i]));
        check(scene.argus.peak_speed(scene.boxes[i]) < SPEED_MAX, buf);
    }
    physics.shutdown();
    std::printf("\n  %s (%d failures)\n",
                failures == 0 ? "STAND, FALL, STAND: ALL THREE VERDICTS"
                              : "RED (G-54): informative reds are booked, "
                                "not hidden",
                failures);
    return failures == 0 ? 0 : 1;
}
