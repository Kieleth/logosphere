// =============================================================================
// THE SQUARE STRIKE — G-47 born red, the TDD instrument for INV-32's flip
// =============================================================================
// A mirror-symmetric impact must produce zero net spin at any speed.
// The manifold's points are solved sequentially today; each point sees
// the spin the previous point injected, and a violent strike ends
// contact before the pass can rebalance, so the seed leaves as real
// spin (measured 2.99 rad/s at 9 m/s). Green requires the manifold
// BLOCK SOLVE: the contact's points solved simultaneously.
// =============================================================================
#include "scenes/scene_square_strike.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace scene_square_strike;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("  %s %s\n", ok ? "[PASS]" : "[FAIL]", what.c_str());
    if (!ok) failures++;
}
}  // namespace

int main() {
    std::printf("\n=== THE SQUARE STRIKE: symmetry owes zero spin (G-47) ===\n");
    for (float speed : SPEEDS) {
        ParticleSystem ps;
        PhysicsSystem physics;
        if (!physics.initialize(ps)) { std::printf("  [FAIL] init\n"); return 1; }
        Scene scene;
        scene.build(ps);
        scene.arm(ps, physics, speed);
        float min_sep = 1e9f;
        for (int f = 0; f < RUN_FRAMES; ++f) {
            scene.step(ps, physics, f);
            const float sep = scene.argus.separation(scene.striker,
                                                     scene.target);
            if (sep >= 0.0f && sep < min_sep) min_sep = sep;
        }
        std::printf("\n-- strike at %.0f m/s --\n", speed);
        std::printf("  [measure] roll %.4f  yaw %.4f  pitch %.4f rad/s  "
                    "|vy| %.4f  min sep %.4f (touch %.2f)\n",
                    scene.roll(ps), scene.yaw(ps), scene.pitch(ps),
                    scene.lateral(ps), min_sep, BODY);
        char buf[176];
        std::snprintf(buf, sizeof(buf),
                      "hygiene: the strike HAPPENED (min sep %.4f <= %.3f)",
                      min_sep, BODY + 0.01f);
        check(min_sep <= BODY + 0.01f, buf);
        std::snprintf(buf, sizeof(buf),
                      "G-47/INV-3: no ROLL from a y-symmetric strike at "
                      "%.0f m/s (%.4f < %.2f)", speed, scene.roll(ps),
                      SPIN_NOISE_MAX);
        check(scene.roll(ps) < SPIN_NOISE_MAX, buf);
        std::snprintf(buf, sizeof(buf),
                      "G-47/INV-3: no YAW either (%.4f < %.2f)",
                      scene.yaw(ps), SPIN_NOISE_MAX);
        check(scene.yaw(ps) < SPIN_NOISE_MAX, buf);
        std::snprintf(buf, sizeof(buf),
                      "G-47: nothing comes out SIDEWAYS (|vy| %.4f < %.2f)",
                      scene.lateral(ps), LATERAL_MAX);
        check(scene.lateral(ps) < LATERAL_MAX, buf);
        std::printf("  [waive] pitch (omega_y): floor friction acts below "
                    "the COM, the nose dip is PHYSICS in grounded staging\n");
        physics.shutdown();
    }
    // THE PITCHED RUNG (G-47 refined): rotated-pose manifold, same
    // mirror. Written expecting the ledger tumble's 0.335 rad/s leak;
    // it measured EXACT ZERO — single strikes hold the mirror in every
    // pose, so the leak lives in the chaotic multi-contact tumble and
    // its minimal repro is still owed (board).
    {
        ParticleSystem ps;
        PhysicsSystem physics;
        if (!physics.initialize(ps)) { std::printf("  [FAIL] init\n"); return 1; }
        Scene scene;
        scene.build(ps);
        scene.arm(ps, physics, 9.0f, PITCH_POSE);
        float min_sep = 1e9f;
        for (int f = 0; f < RUN_FRAMES; ++f) {
            scene.step(ps, physics, f);
            const float sep = scene.argus.separation(scene.striker,
                                                     scene.target);
            if (sep >= 0.0f && sep < min_sep) min_sep = sep;
        }
        std::printf("\n-- THE PITCHED STRIKE: 9 m/s, pose 0.3 rad about Y --\n");
        std::printf("  [measure] roll %.4f  yaw %.4f  pitch %.4f rad/s  "
                    "|vy| %.4f  min sep %.4f\n",
                    scene.roll(ps), scene.yaw(ps), scene.pitch(ps),
                    scene.lateral(ps), min_sep);
        check(min_sep <= BODY + 0.05f,
              "hygiene: the pitched strike HAPPENED");
        check(scene.roll(ps) < SPIN_NOISE_MAX &&
              scene.yaw(ps) < SPIN_NOISE_MAX,
              "G-47: a ROTATED-POSE y-symmetric strike holds the mirror "
              "too (measured exact; the ledger tumble's 0.335 roll leak "
              "still lacks a minimal repro)");
        check(scene.lateral(ps) < LATERAL_MAX,
              "G-47: and nothing sideways from the rotated pose either");
        std::printf("  [waive] pitch: a pitched box tumbling on strike is "
                    "physics\n");
        physics.shutdown();
    }
    std::printf("\n  %s (%d failures)\n",
                failures == 0 ? "SYMMETRY HOLDS AT EVERY SPEED"
                              : "BORN RED (G-47): angular momentum from "
                                "iteration order; the block solve is owed",
                failures);
    return failures == 0 ? 0 : 1;
}
