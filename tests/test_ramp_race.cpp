// =============================================================================
// A CUBE AND A SPHERE ON THE SAME RAMP — the headless half
// =============================================================================
// Board front F2, made measurable: "a sphere will not slide a ramp that a
// cube slides". The mechanism was located 2026-08-16 and is two lines
// wide: `narrow_phase_particle_pair` builds the box side of a
// SPHERE-vs-BOX pair with `aabb_of_box_particle`, which never reads
// rotation (src/core/narrow_phase.cpp:957-976). The sphere therefore
// meets the ramp's upright bounding slab and its contact normal comes
// back (0,0,1): a flat shelf, with no along-slope component to move it.
//
// BORN RED against that. The assertion is not that the two travel the
// same distance — a rolling sphere should outrun a sliding cube — but
// that BOTH travel at all, which is what gravity on a 30 degree slope
// does to anything resting on it.
//
// Scene and stepping: tests/scenes/scene_ramp_race.h, shared with
// test_ramp_race_visual.cpp. No body, force or threshold lives here.
//
//   ./build/test_ramp_race                        numbers
//   INTERACTIVE=1 ./build/test_ramp_race_visual    window
// =============================================================================

#include "scenes/scene_ramp_race.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace scene_ramp_race;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("  %s %s\n", ok ? "[PASS]" : "[FAIL]", what.c_str());
    if (!ok) failures++;
}
}  // namespace

int main() {
    std::printf("\n=== a cube and a sphere on the same %.0f degree ramp ===\n",
                SLOPE_DEG);

    ParticleSystem ps;
    PhysicsSystem physics;
    if (!physics.initialize(ps)) { std::printf("  [FAIL] init\n"); return 1; }

    Scene scene;
    scene.build(ps);
    for (int f = 0; f < RUN_FRAMES; ++f) scene.step(ps, physics);

    const float cube_d = scene.cube_travel(ps);
    const float ball_d = scene.ball_travel(ps);
    // What gravity offers each of them, before friction takes its cut.
    const float along = 9.81f * std::sin(SLOPE_RAD);

    std::printf("  [measure] gravity along a %.0f deg slope: %.2f m/s^2\n",
                SLOPE_DEG, along);
    std::printf("  [measure] cube   travelled %.3f m downhill in %.1f s\n",
                cube_d, RUN_FRAMES / 60.0f);
    std::printf("  [measure] sphere travelled %.3f m downhill in %.1f s\n",
                ball_d, RUN_FRAMES / 60.0f);
    {   // Where did they actually END UP? A zero can mean "did not
        // slide" or "never landed", and those are different bugs.
        float cx, cy, cz, bx, by, bz;
        scene.position(ps, scene.cube, cx, cy, cz);
        scene.position(ps, scene.ball, bx, by, bz);
        float rx, ry, rz;
        scene.position(ps, scene.ramp, rx, ry, rz);
        std::printf("  [measure] ramp  centre (%.2f, %.2f, %.2f), tilt %.0f deg\n",
                    rx, ry, rz, SLOPE_DEG);
        std::printf("  [measure] cube   final (%.2f, %.2f, %.2f)\n", cx, cy, cz);
        std::printf("  [measure] sphere final (%.2f, %.2f, %.2f)\n", bx, by, bz);
        auto v = ps.lock_particles_for_read();
        std::printf("  [measure] ramp solver_mode = %s, moved %.2f m in z\n",
                    v[scene.ramp].solver_mode == ParticleSolverMode::KINEMATIC
                        ? "KINEMATIC" : "DYNAMIC",
                    rz - ramp_centre_z());
    }
    std::printf("  [note] the cube goes through the 15-axis OBB path and\n"
                "         meets a tilted face. The sphere goes through\n"
                "         narrow_phase_sphere_aabb and meets an upright\n"
                "         slab: normal (0,0,1), a flat shelf.\n");

    check(Scene::travelled(cube_d),
          "the cube slides down the slope");
    check(Scene::travelled(ball_d),
          "the sphere ALSO moves (INV-12: contacts come from the body's "
          "actual oriented shape, not its bounding slab)");

    physics.shutdown();
    std::printf("\n  %s (%d failures)\n",
                failures == 0 ? "BOTH BODIES FEEL THE SLOPE"
                              : "F2 CONFIRMED (expected red: the sphere is "
                                "standing on a normal the engine invented)",
                failures);
    return failures == 0 ? 0 : 1;
}
