// =============================================================================
// TORSION TRANSMISSION — G-53's rung ladder, simplest first (KISS)
// =============================================================================
// One rung, one purpose, one derivable expectation (derivations in the
// G-53 record; owner ruling 2026-08-27):
//   R1 one spinning cube on the turtle: the spin dies, nothing created.
//   R2 spinner under a FREE passenger: being dragged is GUARANTEED
//      (no anchor), asserted from below.
//   R3 spinner on a carrier: the turtle anchor WINS, asserted from
//      above. Sub-unity transmission is never demanded from below.
// Every rung also answers INV-4 (stands) and INV-3 (bounded) — the
// per-rung heights localize the spinning-interface grind by pressure.
// =============================================================================
#include "scenes/scene_torsion_rungs.h"

#include <cstdio>
#include <iostream>
#include <cstdlib>
#include <string>
#include <vector>

using namespace scene_torsion_rungs;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("  %s %s\n", ok ? "[PASS]" : "[FAIL]", what.c_str());
    if (!ok) failures++;
}
}  // namespace

int main() {
    std::printf("\n=== TORSION TRANSMISSION: the rung ladder (G-53) ===\n");
    static const bool trace = std::getenv("ARGUS_TRACE") != nullptr;
    // TORSION_RUNG=<n> runs one rung alone (RCA focus).
    const char* only_env = std::getenv("TORSION_RUNG");
    const int only = only_env ? std::atoi(only_env) : -1;
    char buf[176];
    for (int rung = 0; rung < N_RUNGS; ++rung) {
        if (only >= 0 && rung != only) continue;
        ParticleSystem ps;
        PhysicsSystem physics;
        if (!physics.initialize(ps)) { std::printf("  [FAIL] init\n"); return 1; }
        Scene scene;
        const int n = scene.build_rung(ps, rung);

        const float sgn = OMEGA0 > 0 ? 1.0f : -1.0f;
        std::vector<float> peak_signed_wz(n, -1e9f);
        const float L0 = scene.total_Lz(ps);
        float peak_absL = std::fabs(L0);
        float stop_time = -1.0f;   // first frame the spinner reads noise

        for (int f = 0; f < RUN_FRAMES; ++f) {
            scene.step(ps, physics, f);
            for (int i = 0; i < n; ++i)
                peak_signed_wz[i] = std::fmax(peak_signed_wz[i],
                                              sgn * scene.box_omega_z(ps, i));
            peak_absL = std::fmax(peak_absL, std::fabs(scene.total_Lz(ps)));
            if (stop_time < 0.0f &&
                scene.box_spin(ps, SPINNER_OF[rung]) < SPIN_NOISE_MAX)
                stop_time = (float)(f + 1) * DT;
            // The whole episode lives in the first fraction of a second;
            // a 15-frame stride misses it. Per-frame through the episode,
            // sparse after.
            if (trace && (f < 60 || f % 15 == 0)) {
                std::printf("  [f%03d]", f);
                for (int i = 0; i < n; ++i)
                    std::printf("  wz%d %+8.4f", i,
                                scene.box_omega_z(ps, i));
                std::printf("  Lz %8.2f\n", scene.total_Lz(ps));
                if (f % 15 == 0)
                    for (int i = 0; i < n; ++i)
                        scene.argus.narrate(std::cout, scene.boxes[i]);
            }
        }

        std::printf("\n-- %s --\n", RUNG_NAMES[rung]);
        for (int i = 0; i < n; ++i)
            std::printf("  [measure] box%d z %.4f (static %.1f)  spin %.4f  "
                        "peak signed wz %.4f  peak speed %.4f\n",
                        i, scene.box_z(ps, i), scene.static_z(i),
                        scene.box_spin(ps, i), peak_signed_wz[i],
                        scene.argus.peak_speed(scene.boxes[i]));
        std::printf("  [measure] L_z initial %.1f  peak %.1f  final %.1f\n",
                    L0, peak_absL, scene.total_Lz(ps));

        const int sp = SPINNER_OF[rung];
        std::snprintf(buf, sizeof(buf),
                      "G-53: the spinner's spin DIES (final %.4f < %.2f)",
                      scene.box_spin(ps, sp), SPIN_NOISE_MAX);
        check(scene.box_spin(ps, sp) < SPIN_NOISE_MAX, buf);
        if (rung == 0) {
            std::snprintf(buf, sizeof(buf),
                          "G-55: it dies at the GRINDSTONE RATE "
                          "(stop %.3f s in [%.1f, %.1f])",
                          stop_time, STOP_TIME_MIN, STOP_TIME_MAX);
            check(stop_time >= STOP_TIME_MIN && stop_time <= STOP_TIME_MAX,
                  buf);
        }
        std::snprintf(buf, sizeof(buf),
                      "G-53/INV-3: L_z is never created "
                      "(peak %.1f <= initial %.1f x %.2f)",
                      peak_absL, std::fabs(L0), LZ_BAND);
        check(peak_absL <= std::fabs(L0) * LZ_BAND, buf);
        std::snprintf(buf, sizeof(buf),
                      "G-53: the turtle drains the spin "
                      "(final |L| %.1f < initial %.1f)",
                      std::fabs(scene.total_Lz(ps)), std::fabs(L0));
        check(std::fabs(scene.total_Lz(ps)) < std::fabs(L0), buf);
        if (rung == 1) {
            std::snprintf(buf, sizeof(buf),
                          "G-53: the FREE passenger is dragged, same sign "
                          "(peak signed wz %.4f >= %.2f)",
                          peak_signed_wz[PARTNER_OF[rung]], PASSENGER_MIN);
            check(peak_signed_wz[PARTNER_OF[rung]] >= PASSENGER_MIN, buf);
            std::snprintf(buf, sizeof(buf),
                          "G-53: the passenger's spin also dies "
                          "(final %.4f < %.2f)",
                          scene.box_spin(ps, PARTNER_OF[rung]),
                          SPIN_NOISE_MAX);
            check(scene.box_spin(ps, PARTNER_OF[rung]) < SPIN_NOISE_MAX, buf);
        }
        if (rung == 2) {
            std::snprintf(buf, sizeof(buf),
                          "G-53: the ANCHORED carrier is NOT dragged strongly "
                          "(peak signed wz %.4f < %.2f)",
                          peak_signed_wz[PARTNER_OF[rung]], CARRIER_MAX);
            check(peak_signed_wz[PARTNER_OF[rung]] < CARRIER_MAX, buf);
        }
        for (int i = 0; i < n; ++i) {
            // On the pressure rungs (G-56) this height check IS the
            // grind assert: REST_TOL is 12x tighter than the 0.232 m
            // grind measured at the 73.5 kN interface.
            std::snprintf(buf, sizeof(buf),
                          "%s/INV-4: box%d stands at its static height "
                          "(%.4f vs %.1f, tol %.2f)",
                          rung >= 3 ? "G-56" : "G-48", i,
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
        physics.shutdown();
    }
    std::printf("\n  %s (%d failures)\n",
                failures == 0 ? "EVERY RUNG ANSWERS ITS LAW"
                              : "RED (G-53): informative reds are booked, "
                                "not hidden",
                failures);
    return failures == 0 ? 0 : 1;
}
