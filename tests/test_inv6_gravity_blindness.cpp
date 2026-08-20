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

// FULL-STATE NARRATION (assert-or-waive, per DOF, owner directive
// 2026-08-19). Two bodies per case, three cases. Observed through Argus,
// so the asserts and the log read the same values.
//
//   WALL (KINEMATIC fixture)
//     position xyz  — ASSERTED: must not move on any axis. Every gap and
//                     penetration number below is measured against it,
//                     so a wall that yielded would make all three cases
//                     agree about nothing in particular.
//     orientation   — nothing writes it; WAIVED, watched.
//     velocity      — implied by the drift assert; WAIVED.
//
//   BODY (DYNAMIC, pressed square into the wall)
//     along-axis position  — ASSERTED twice, and the second is new: the
//                     SPREAD across axes (the original claim, about
//                     bias) and now the ABSOLUTE peak penetration and
//                     resting gap against the solver's own slop. Three
//                     cases penetrating equally and deeply would have
//                     passed the spread test alone.
//     along-axis velocity  — ASSERTED: it comes to rest on every axis.
//     tangential position  — measured and printed, WAIVED by name. This
//                     fixture cancels gravity once per FRAME while the
//                     engine applies it once per SUBSTEP, so the X and Y
//                     cases carry a drift the wall's normal cannot
//                     touch. Fixture residue, not solver bias; the same
//                     note already governs the speed measurement below.
//     tangential velocity  — same waiver, same reason.
//     omega         — ASSERTED at zero on all three axes. The press is
//                     square and centred, so no torque exists in this
//                     experiment and zero is the RIGHT answer. That is
//                     not a waiver of D2 (contacts carry no lever arm):
//                     D2 shows where a lever arm should exist, and here
//                     none does.
//     orientation   — follows omega; ASSERTED to stay at identity, the
//                     same claim read on the other ledger.
//     coherence     — ASSERTED: one body, one orientation (G-23).
//     separation    — the body-to-wall gap IS the subject and is
//                     asserted along the press axis, which is the axis
//                     the contact normal lives on. Argus's
//                     centre-to-centre separation is printed for the
//                     record; approach_speed is WAIVED because the
//                     press-axis speed already carries that claim.

#include "core/argus.h"
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
    // --- what the witness added -------------------------------------
    float wall_drift = 0.0f;    // worst movement of the KINEMATIC datum
    float peak_spin = 0.0f;     // |omega| the body ever reached
    float max_tilt = 0.0f;      // worst |rotation| off identity
    float max_div = 0.0f;       // worst q-vs-Euler divergence
    float tangential = 0.0f;    // worst drift across the press axis
    float separation = 0.0f;    // centre to centre at the deadline
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

    // The witness. It reads only; it cannot perturb the press.
    logosphere::Argus argus;
    argus.watch(body, "body");
    argus.watch(wall, "wall");
    float wall_x0 = 0.0f, wall_y0 = 0.0f, wall_z0 = 0.0f;
    float body_t1_0 = 0.0f, body_t2_0 = 0.0f;
    const int j = (axis + 1) % 3, k = (axis + 2) % 3;
    {
        auto v = ps.lock_particles_for_read();
        wall_x0 = v[wall].x; wall_y0 = v[wall].y; wall_z0 = v[wall].z;
        const float b0[3] = { v[body].x, v[body].y, v[body].z };
        body_t1_0 = b0[j]; body_t2_0 = b0[k];
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
        argus.observe(ps, f);
        {   // Everything the witness sees, latched per frame.
            const logosphere::Argus::State* sb = argus.latest(body);
            const logosphere::Argus::State* sw = argus.latest(wall);
            if (sb && sw) {
                const float dx = sw->x - wall_x0, dy = sw->y - wall_y0,
                            dz = sw->z - wall_z0;
                const float d = std::sqrt(dx*dx + dy*dy + dz*dz);
                if (d > out.wall_drift) out.wall_drift = d;
                const float sp = argus.spin(body);
                if (sp > out.peak_spin) out.peak_spin = sp;
                const float tilt = std::fmax(std::fabs(sb->rx),
                                   std::fmax(std::fabs(sb->ry),
                                             std::fabs(sb->rz)));
                if (tilt > out.max_tilt) out.max_tilt = tilt;
                const float dv = argus.divergence(body);
                if (dv > out.max_div) out.max_div = dv;
                const float bp[3] = { sb->x, sb->y, sb->z };
                const float t1 = bp[j] - body_t1_0, t2 = bp[k] - body_t2_0;
                const float tan_d = std::sqrt(t1*t1 + t2*t2);
                if (tan_d > out.tangential) out.tangential = tan_d;
            }
        }
        auto v = ps.lock_particles_for_read();
        const Particle& b = v[body];
        const Particle& w = v[wall];
        const float bpos[3] = { b.x, b.y, b.z };
        const float wpos[3] = { w.x, w.y, w.z };
        const float gap = (bpos[axis] - BODY / 2) - (wpos[axis] + WALL_T / 2);
        if (gap < 0.0f && -gap > peak_pen) peak_pen = -gap;
    }
    out.separation = argus.separation(body, wall);

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

    // --- the witness: the state the spread numbers cannot see ---------
    printf("\n  [argus] %-6s %10s %10s %10s %10s %10s\n", "axis",
           "wall move", "peak spin", "max tilt", "coherence", "tangent");
    auto row = [](const char* n, const Outcome& o) {
        printf("  [argus] %-6s %10.6f %10.6f %10.6f %10.6f %10.6f\n",
               n, o.wall_drift, o.peak_spin, o.max_tilt, o.max_div, o.tangential);
    };
    row("+X", x); row("+Y", y); row("-Z", z);
    printf("  [argus] centre-to-centre at rest: +X %.6f  +Y %.6f  -Z %.6f\n",
           x.separation, y.separation, z.separation);
    printf("  [note] the tangential column is the FIXTURE's residue, not the\n"
           "         solver's: gravity is cancelled once per frame here and\n"
           "         applied once per substep by the engine. Waived by name\n"
           "         in the narration, printed so it stays visible.\n\n");

    check(x.wall_drift < TOL && y.wall_drift < TOL && z.wall_drift < TOL,
          "the wall never moved: all three cases measure against a fixed "
          "datum");
    check(pen_spread < TOL,
          "the same press penetrates the same on every axis");
    check(gap_spread < TOL,
          "the same press rests at the same distance on every axis");
    // The spread asserts are about BIAS and say nothing about magnitude:
    // three cases penetrating equally and deeply would pass both. These
    // two bound the absolute answer against the solver's own slop.
    check(x.penetration < TOL && y.penetration < TOL && z.penetration < TOL,
          "and the press does not sink into the wall on ANY axis "
          "(absolute, not merely equal)");
    check(std::fabs(x.final_gap) < TOL && std::fabs(y.final_gap) < TOL &&
          std::fabs(z.final_gap) < TOL,
          "and it rests ON the wall face on every axis, not floating off "
          "it (absolute, not merely equal)");
    check(x.speed_at_rest < 0.05f && y.speed_at_rest < 0.05f &&
          z.speed_at_rest < 0.05f,
          "and comes to rest on every axis (no axis keeps it alive)");
    check(x.peak_spin < 1e-4f && y.peak_spin < 1e-4f && z.peak_spin < 1e-4f,
          "a square centred press produces NO torque on any axis (zero is "
          "the right answer here, unlike the ramp cases: there is no lever "
          "arm in this geometry for D2 to lose)");
    check(x.max_tilt < 1e-4f && y.max_tilt < 1e-4f && z.max_tilt < 1e-4f,
          "and the body's orientation never leaves identity, on either "
          "ledger");
    check(x.max_div < 0.01f && y.max_div < 0.01f && z.max_div < 0.01f,
          "one body, one orientation, every frame, on every axis (G-23)");

    printf("\n  %s (%d failures)\n",
           failures == 0 ? "INV-6 WITNESSED" : "INV-6 VIOLATED", failures);
    return failures == 0 ? 0 : 1;
}
