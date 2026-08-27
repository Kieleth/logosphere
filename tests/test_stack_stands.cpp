// =============================================================================
// THE STACK AND THE PILE — G-48 born red, plus the contrast controls
// =============================================================================
// Cases 0/1 prove the null case: no torque exists in the statics, the
// solver must invent none (G-48). Cases 2/3 are the owner-ordered
// controls (2026-08-26) that keep the null case honest: a spinner
// inside the column must brake AND drag its neighbours (G-53 — a
// solver that suppresses real torque would pass the null case), and a
// mixed-mass trio must earn three different verdicts — stand, FALL,
// stand (G-54 — the overhung cube statics certifies to fall catches
// phantom support, the inverse defect of the sink). "Ok to be red if
// its informative."
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
    char buf[200];
    for (int which = 0; which < N_CASES; ++which) {
        ParticleSystem ps;
        PhysicsSystem physics;
        if (!physics.initialize(ps)) { std::printf("  [FAIL] init\n"); return 1; }
        Scene scene;
        const int n = scene.build_case(ps, which);

        // Test-side latches (Argus peaks are magnitudes; the torsion
        // witnesses need SIGNED omega_z excursions and the L_z ledger).
        const float sgn = TORSION_OMEGA0 > 0 ? 1.0f : -1.0f;
        std::vector<float> peak_signed_wz(n, -1e9f);
        const float L0 = scene.total_Lz(ps);
        float peak_absL = std::fabs(L0);

        for (int f = 0; f < RUN_FRAMES; ++f) {
            scene.step(ps, physics, f);
            for (int i = 0; i < n; ++i)
                peak_signed_wz[i] = std::fmax(peak_signed_wz[i],
                                              sgn * scene.box_omega_z(ps, i));
            peak_absL = std::fmax(peak_absL, std::fabs(scene.total_Lz(ps)));
            if (trace && f % 15 == 0)
                for (int i = 0; i < n; ++i)
                    scene.argus.narrate(std::cout, scene.boxes[i]);
        }

        std::printf("\n-- %s --\n", CASE_NAMES[which]);
        for (int i = 0; i < n; ++i)
            std::printf("  [measure] box%d z %.4f (static %s)  spin %.4f  "
                        "wz %.4f  argus peak spin %.4f  peak speed %.4f\n",
                        i, scene.box_z(ps, i),
                        scene.expects_static(i)
                            ? (std::snprintf(buf, sizeof(buf), "%.2f",
                                             scene.static_z(i)), buf)
                            : "waived",
                        scene.box_spin(ps, i), scene.box_omega_z(ps, i),
                        scene.argus.peak_spin(scene.boxes[i]),
                        scene.argus.peak_speed(scene.boxes[i]));

        // ---- the laws every case answers to -----------------------------
        for (int i = 0; i < n; ++i) {
            if (!scene.expects_static(i)) continue;
            std::snprintf(buf, sizeof(buf),
                          "%s/INV-4: box%d stands at its static height "
                          "(%.4f vs %.2f, tol %.2f)",
                          which == 3 ? "G-54" : "G-48", i,
                          scene.box_z(ps, i), scene.static_z(i), REST_TOL);
            check(std::fabs(scene.box_z(ps, i) - scene.static_z(i)) < REST_TOL,
                  buf);
        }
        for (int i = 0; i < n; ++i) {
            std::snprintf(buf, sizeof(buf),
                          "INV-3: box%d speeds stayed bounded (peak %.4f)",
                          i, scene.argus.peak_speed(scene.boxes[i]));
            check(scene.argus.peak_speed(scene.boxes[i]) < SPEED_MAX, buf);
        }

        // ---- per-case contracts ----------------------------------------
        if (which == 0 || which == 1) {
            for (int i = 0; i < n; ++i) {
                std::snprintf(buf, sizeof(buf),
                              "G-48/INV-24: box%d spin settles to noise "
                              "(%.4f < %.2f)", i, scene.box_spin(ps, i),
                              SPIN_NOISE_MAX);
                check(scene.box_spin(ps, i) < SPIN_NOISE_MAX, buf);
            }
        }
        if (which == 2) {
            std::snprintf(buf, sizeof(buf),
                          "G-53: the spinner's spin DIES under load "
                          "(final %.4f < %.2f)",
                          scene.box_spin(ps, SPINNER_IDX), SPIN_DEAD_MAX);
            check(scene.box_spin(ps, SPINNER_IDX) < SPIN_DEAD_MAX, buf);
            std::snprintf(buf, sizeof(buf),
                          "G-53: torsion TRANSMITS below, same sign "
                          "(peak signed wz %.4f >= %.2f, sub-unity anchor)",
                          peak_signed_wz[SPINNER_IDX - 1], TRANSMIT_MIN_BELOW);
            check(peak_signed_wz[SPINNER_IDX - 1] >= TRANSMIT_MIN_BELOW, buf);
            std::snprintf(buf, sizeof(buf),
                          "G-53: torsion TRANSMITS above, same sign "
                          "(peak signed wz %.4f >= %.2f, super-unity drag)",
                          peak_signed_wz[SPINNER_IDX + 1], TRANSMIT_MIN_ABOVE);
            check(peak_signed_wz[SPINNER_IDX + 1] >= TRANSMIT_MIN_ABOVE, buf);
            std::snprintf(buf, sizeof(buf),
                          "G-53/INV-3: L_z is never created "
                          "(peak |L| %.1f <= initial %.1f x %.2f)",
                          peak_absL, std::fabs(L0), LZ_BAND);
            check(peak_absL <= std::fabs(L0) * LZ_BAND, buf);
            std::snprintf(buf, sizeof(buf),
                          "G-53: the turtle drains the spin "
                          "(final |L| %.1f < initial %.1f)",
                          std::fabs(scene.total_Lz(ps)), std::fabs(L0));
            check(std::fabs(scene.total_Lz(ps)) < std::fabs(L0), buf);
        }
        if (which == 3) {
            // M2: index 3 is the overhung big cube; statics certifies
            // the fall. Index 2 (its small perch) is measured above and
            // WAIVED: the departing load passes over the small cube's
            // own base edge, a marginal case whose statics we have not
            // done.
            std::snprintf(buf, sizeof(buf),
                          "G-54: the overhung big cube DEPARTS its perch "
                          "(final z %.4f < %.2f; static would be %.2f)",
                          scene.box_z(ps, 3), M2_DEPART_Z,
                          M_SMALL + M_BIG * 0.5f);
            check(scene.box_z(ps, 3) < M2_DEPART_Z, buf);
        }
        physics.shutdown();
    }
    std::printf("\n  %s (%d failures)\n",
                failures == 0 ? "ALL CASES ANSWER THEIR LAWS"
                              : "RED (G-48/G-53/G-54): informative reds are "
                                "booked, not hidden",
                failures);
    return failures == 0 ? 0 : 1;
}
