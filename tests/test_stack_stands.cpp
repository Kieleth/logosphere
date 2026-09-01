// =============================================================================
// THE STACK AND THE PILE — G-48's single purpose: statics owes both
// =============================================================================
// No torque exists in either structure's statics; the solver must
// invent none. Torsion lives in test_torsion_transmission (G-53);
// mixed masses in test_mixed_mass_stands (G-54). Owner KISS ruling
// 2026-08-27: single-purposed tests.
// =============================================================================
#include "scenes/scene_stack_stand.h"

#include <cstdio>
#include <iostream>
#include <cstdlib>
#include <string>

using namespace scene_stack_stand;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("  %s %s\n", ok ? "[PASS]" : "[FAIL]", what.c_str());
    if (!ok) failures++;
}
}  // namespace

int main() {
    std::printf("\n=== THE STACK AND THE PILE: statics owes both (G-48) ===\n");
    static const bool trace = std::getenv("ARGUS_TRACE") != nullptr;
    char buf[176];
    for (int which = 0; which < N_CASES; ++which) {
        ParticleSystem ps;
        PhysicsSystem physics;
        if (!physics.initialize(ps)) { std::printf("  [FAIL] init\n"); return 1; }
        Scene scene;
        const int n = scene.build_case(ps, which);
        for (int f = 0; f < RUN_FRAMES; ++f) {
            scene.step(ps, physics, f);
            if (trace && f % 15 == 0)
                for (int i = 0; i < n; ++i)
                    scene.argus.narrate(std::cout, scene.boxes[i]);
        }
        std::printf("\n-- %s --\n", CASE_NAMES[which]);
        for (int i = 0; i < n; ++i)
            std::printf("  [measure] box%d z %.4f (static %.1f)  spin %.4f  "
                        "argus peak spin %.4f  peak speed %.4f\n",
                        i, scene.box_z(ps, i), scene.static_z(i),
                        scene.box_spin(ps, i),
                        scene.argus.peak_spin(scene.boxes[i]),
                        scene.argus.peak_speed(scene.boxes[i]));
        for (int i = 0; i < n; ++i) {
            std::snprintf(buf, sizeof(buf),
                          "G-48/INV-4: box%d stands at its static height "
                          "(%.4f vs %.1f, tol %.2f)", i,
                          scene.box_z(ps, i), scene.static_z(i), REST_TOL);
            check(std::fabs(scene.box_z(ps, i) - scene.static_z(i)) < REST_TOL,
                  buf);
        }
        for (int i = 0; i < n; ++i) {
            std::snprintf(buf, sizeof(buf),
                          "G-48/INV-24: box%d spin settles to noise "
                          "(%.4f < %.2f)", i, scene.box_spin(ps, i),
                          SPIN_NOISE_MAX);
            check(scene.box_spin(ps, i) < SPIN_NOISE_MAX, buf);
        }
        for (int i = 0; i < n; ++i) {
            std::snprintf(buf, sizeof(buf),
                          "INV-3: box%d speeds stayed bounded (peak %.4f)",
                          i, scene.argus.peak_speed(scene.boxes[i]));
            check(scene.argus.peak_speed(scene.boxes[i]) < SPEED_MAX, buf);
        }
        physics.shutdown();
    }
    std::printf("\n  %s (%d failures)\n",
                failures == 0 ? "BOTH STRUCTURES STAND"
                              : "RED (G-48): the sustained-contact loop does "
                                "not hold statics in this world",
                failures);
    return failures == 0 ? 0 : 1;
}
