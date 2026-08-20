// =============================================================================
// INV-6: THE SOLVER DOES NOT KNOW WHICH WAY IS DOWN
// =============================================================================
// "Every correction derives from contact geometry, never from a world-up
// axis. Every mechanism must work on walls, ceilings and in zero-g."
//
// This invariant has been load-bearing since the engine's first month —
// it is the reason floor-Z clamps, "push up" heuristics and axis biases
// are rejected on sight — and until 2026-08-15 NOTHING TESTED IT. The
// sweep listed it under COVERAGE HOLES beside INV-5. A law nobody
// measures is a law the next patch can break silently, which is exactly
// how the axis-biased fixes it forbids used to get written.
//
// THE INSTRUMENT: the same collision, three times, in three orientations.
// A body is pressed into a wall by a constant force along each world
// axis in turn — down (-Z, which is gravity's own direction), east (+X),
// and north (+Y) — against a wall whose normal opposes it. Nothing about
// the situation differs except which way the world is pointing.
//
// If the solver reads geometry, all three resolve identically: same
// penetration, same rest, same time to settle. If it reads a world-up
// axis anywhere, the -Z case behaves differently from the other two and
// the difference IS the bias, in metres.
//
// Gravity itself is left OUT of the comparison on purpose. It genuinely
// points along -Z (a modelling choice, not a bias, and one the owner
// intends to replace with a substrate field), so including it would
// measure the choice rather than the solver. Bodies here are driven by
// an explicit force so the three cases are true mirrors of each other.
//
// Run: ./build/test_inv6_gravity_blindness
// =============================================================================

#include "core/particle_system.h"
#include "logosphere/physics/physics_system.h"
#include "particle.h"
#include "generated/physics_constants.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace {

int failures = 0;
void check(bool ok, const std::string& what) {
    printf("  %s %s\n", ok ? "[PASS]" : "[FAIL]", what.c_str());
    if (!ok) failures++;
}

struct Outcome {
    float penetration = 0.0f;   // deepest overlap the solve left
    float final_gap = 0.0f;     // separation along the press axis at rest
    float speed_at_rest = 0.0f;
    bool  ok = false;
};

// One body pressed into one wall along `axis` (0=X, 1=Y, 2=Z), in the
// negative direction. The wall sits on that side; nothing else exists.
Outcome press(int axis, float press_accel) {
    Outcome out;
    ParticleSystem ps;
    PhysicsSystem physics;
    if (!physics.initialize(ps)) return out;

    const float WALL_T = 0.4f;      // wall thickness along the press axis
    const float BODY   = 0.5f;
    const float CENTRE = 6.0f;      // well clear of the turtle at z=0

    auto place = [&](float a, float b, float c, float sa, float sb, float sc,
                     Materials::Type mat) {
        Particle p{};
        // a is the press axis; b, c are the other two, in world order.
        float xyz[3], size[3];
        xyz[axis] = a; size[axis] = sa;
        int j = (axis + 1) % 3, k = (axis + 2) % 3;
        xyz[j] = b; size[j] = sb;
        xyz[k] = c; size[k] = sc;
        p.x = xyz[0]; p.y = xyz[1]; p.z = xyz[2];
        p.shape = ParticleShape::BOX;
        p.width = size[0]; p.height = size[1]; p.thickness = size[2];
        p.size = std::max(size[0], std::max(size[1], size[2]));
        p.SetMaterial(mat);
        return ps.queue_particle_addition(p);
    };

    // Wall: a wide slab, immovable, its face at CENTRE - WALL_T/2.
    const int wall = place(CENTRE - 1.0f, CENTRE, CENTRE,
                           WALL_T, 3.0f, 3.0f, Materials::Type::STONE);
    // Body: resting just off the wall face, pressed toward it.
    const int body = place(CENTRE - 1.0f + WALL_T / 2 + BODY / 2 + 0.02f,
                           CENTRE, CENTRE, BODY, BODY, BODY,
                           Materials::Type::STONE);
    ps.flush_pending_particles();
    {
        auto v = ps.lock_particles_for_write();
        v[wall].solver_mode = ParticleSolverMode::KINEMATIC;
        v[wall].owner = ParticleOwner::DYNAMICS;
        v[wall].is_at_rest = true;
    }

    float peak_pen = 0.0f;
    for (int f = 0; f < 240; ++f) {
        {   // the press: a constant acceleration toward the wall, applied
            // by hand so all three axes are driven identically.
            auto v = ps.lock_particles_for_write();
            Particle& p = v[body];
            float* vel[3] = { &p.vx, &p.vy, &p.vz };
            *vel[axis] -= press_accel * (1.0f / 60.0f);
            // CANCEL GRAVITY, BEFORE the solve. It points along -Z by
            // construction, so leaving it on makes the -Z case "press
            // plus gravity" and the other two "press across gravity" —
            // not mirrors at all (measured: the X and Y bodies fell out
            // of the wall's span and ran away to 13.27 m/s).
            //
            // And it must be cancelled BEFORE update(), not after: the
            // first attempt cancelled afterwards and left the -Z body
            // holding exactly 9.8/60 = 0.16333 m/s of uneaten
            // correction, which read as a 4x axis bias and was purely
            // an artifact of where the line sat.
            p.vz += PhysicsV4::GRAVITY * (1.0f / 60.0f);
            p.is_at_rest = false;
        }
        ps.update_bvh();
        physics.update(1.0 / 60.0);
        auto v = ps.lock_particles_for_read();
        const Particle& b = v[body];
        const Particle& w = v[wall];
        const float bpos[3] = { b.x, b.y, b.z };
        const float wpos[3] = { w.x, w.y, w.z };
        const float gap = (bpos[axis] - BODY / 2) - (wpos[axis] + WALL_T / 2);
        if (gap < 0.0f && -gap > peak_pen) peak_pen = -gap;
    }

    auto v = ps.lock_particles_for_read();
    const Particle& b = v[body];
    const Particle& w = v[wall];
    const float bpos[3] = { b.x, b.y, b.z };
    const float wpos[3] = { w.x, w.y, w.z };
    out.penetration = peak_pen;
    out.final_gap = (bpos[axis] - BODY / 2) - (wpos[axis] + WALL_T / 2);
    // ALONG THE CONTACT NORMAL ONLY. INV-6 is about the correction a
    // contact produces, and that lives on the normal. The tangential
    // residue here is the fixture's own: gravity is applied once per
    // SUBSTEP (four per frame, each followed by a solve) while this test
    // can only cancel it once per frame, so the X and Y cases carry a
    // small upward drift that a vertical wall's normal cannot touch.
    // Measuring the full speed vector would report that artifact as a
    // bias — it did, at 0.1224 m/s, before this line.
    const float vel[3] = { b.vx, b.vy, b.vz };
    out.speed_at_rest = std::fabs(vel[axis]);
    out.ok = true;
    physics.shutdown();
    return out;
}

}  // namespace

int main() {
    printf("\n=== INV-6: the solver does not know which way is down ===\n");
    printf("  the same press, three orientations, driven by an explicit\n"
           "  force so gravity's own -Z choice is not what is measured\n\n");

    const float ACCEL = 9.81f;   // a familiar magnitude, applied per axis
    const Outcome x = press(0, ACCEL);
    const Outcome y = press(1, ACCEL);
    const Outcome z = press(2, ACCEL);
    if (!x.ok || !y.ok || !z.ok) { printf("  [FAIL] init\n"); return 1; }

    printf("  %-6s %12s %12s %12s\n", "axis", "peak pen", "final gap", "speed");
    printf("  %-6s %12.6f %12.6f %12.6f\n", "+X", x.penetration, x.final_gap,
           x.speed_at_rest);
    printf("  %-6s %12.6f %12.6f %12.6f\n", "+Y", y.penetration, y.final_gap,
           y.speed_at_rest);
    printf("  %-6s %12.6f %12.6f %12.6f\n", "-Z", z.penetration, z.final_gap,
           z.speed_at_rest);

    // The tolerance is the engine's own contact slop: differences below
    // it are the solve's granularity, not a bias.
    const float TOL = 0.001f;
    auto spread = [](float a, float b, float c) {
        const float hi = std::fmax(a, std::fmax(b, c));
        const float lo = std::fmin(a, std::fmin(b, c));
        return hi - lo;
    };
    const float pen_spread = spread(x.penetration, y.penetration, z.penetration);
    const float gap_spread = spread(x.final_gap, y.final_gap, z.final_gap);
    printf("\n  [measure] penetration spread across axes: %.6f m\n", pen_spread);
    printf("  [measure] resting-gap spread across axes:  %.6f m\n", gap_spread);

    check(pen_spread < TOL,
          "the same press penetrates the same on every axis");
    check(gap_spread < TOL,
          "the same press rests at the same distance on every axis");
    check(x.speed_at_rest < 0.05f && y.speed_at_rest < 0.05f &&
          z.speed_at_rest < 0.05f,
          "and comes to rest on every axis (no axis keeps it alive)");

    printf("\n  %s (%d failures)\n",
           failures == 0 ? "INV-6 WITNESSED" : "INV-6 VIOLATED", failures);
    return failures == 0 ? 0 : 1;
}
