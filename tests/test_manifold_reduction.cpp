// ============================================================================
// MANIFOLD REDUCTION — the support spans the face (G-51, born red)
// ============================================================================
// The stack pump's power supply, witnessed at substep granularity on
// 2026-08-26 (G-48 / G-50 / G-51 in tests/invariants/GEDANKEN.jsonl):
// two stacked unit cubes, laterally offset ~3 mm and micro-tilted
// ~3e-4 rad, clip to a polygon of up to 8 vertices whose depths differ
// by ~0.3 mm across a metre of face. The reduction keeps the 4 DEEPEST
// (narrow_phase.cpp, "Keep the 4 deepest"), and under tilt all four sit
// on the downhill edge: the support centroid lands up to 0.455 m
// off-centre, the warm start pushes its constant 86.9 N*s through it,
// and 22-40 N*m*s of torque is injected per substep with alternating
// sign as the injection itself flips the tilt. A powered rocker.
//
// THE LAW (G-51): a face contact's reduced point set must SPAN the
// contact polygon. For near-uniform depth, the centroid of the reduced
// set stays at the overlap polygon's centre, so cached support
// transmits no net torque to a resting interface.
//
// Mode: the spanning reduction lives behind MANIFOLD_SPAN=1 (default
// off until the owner's flip ruling). This test sets the lever itself,
// so it asserts the claim IN THE CLAIM'S MODE. It was BORN RED
// 2026-08-26 (worst centroid offset 0.3774 m, rotating with the tilt
// phase) and went green the same day when the spanning reduction
// landed behind the lever. The default path's exact current numbers
// are enforced by the audited battery, not here.
//
// ARGUS IS N/A HERE, deliberately (same reasoning as
// test_collision_bounds_rotation): no ParticleSystem, no time, no
// evolving state. The file builds poses, calls the narrow phase once
// per pose, and reads the answer.
//
// FULL-STATE NARRATION of the object this file produces (a manifold):
//   - point positions ......... ASSERTED (centroid vs overlap centre)
//   - point count ............. ASSERTED (a face needs >= 4 supports)
//   - normal .................. ASSERTED (hygiene: -Z-ish, the poses
//                               are near-flat stacks)
//   - penetrations ............ WAIVED with a print: depth magnitudes
//                               are the clip's business (INV-2 tests);
//                               here only their ORDERING matters, and
//                               the law says ordering must not place
//                               the support.
// ============================================================================

#include "logosphere/physics/narrow_phase.h"
#include "test_env_portable.h"
#include "particle.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

int checks_run = 0, checks_failed = 0;

void check(bool ok, const char* what) {
    checks_run++;
    if (!ok) checks_failed++;
    printf("      %-66s %s\n", what, ok ? "[V]" : "*** FAIL ***");
}

Particle unit_box(float x, float y, float z, float rx, float ry, float rz) {
    Particle p = {};
    p.shape = ParticleShape::BOX;
    p.x = x; p.y = y; p.z = z;
    p.width = p.height = p.thickness = 1.0f; p.size = 1.0f;
    p.rotation_x = rx; p.rotation_y = ry; p.rotation_z = rz;
    return p;
}

struct Reduced {
    int n = 0;
    float cx = 0, cy = 0;         // centroid of the point set (world)
    float nx = 0, ny = 0, nz = 0;
    float pen_min = 0, pen_max = 0;
    bool hit = false;
};

Reduced manifold_of(const Particle& lower, const Particle& upper) {
    Reduced r;
    ContactManifold m;
    r.hit = narrow_phase_obb(obb_of_box_particle(upper, upper.z),
                             obb_of_box_particle(lower, lower.z),
                             0, 1, 0.08f, m);
    if (!r.hit || m.num_points == 0) return r;
    r.n = m.num_points;
    r.nx = m.normal_x; r.ny = m.normal_y; r.nz = m.normal_z;
    r.pen_min = r.pen_max = m.points[0].penetration;
    for (int i = 0; i < m.num_points; ++i) {
        r.cx += m.points[i].px;
        r.cy += m.points[i].py;
        r.pen_min = std::fmin(r.pen_min, m.points[i].penetration);
        r.pen_max = std::fmax(r.pen_max, m.points[i].penetration);
    }
    r.cx /= (float)r.n;
    r.cy /= (float)r.n;
    return r;
}

// For two overlapping unit squares the overlap rectangle's centre is the
// mean of the centres, per axis (midpoint of [max(a,b)-0.5, min(a,b)+0.5]).
// Micro-tilts move the projection by ~1.5e-4 m, far under the tolerance.

// Tolerance for the centroid's distance from the overlap centre. The
// defect measures 0.25-0.45 m; a spanning reduction of a near-uniform
// polygon sits within clip noise (~the 3 mm offset) of the centre.
// 0.05 m is 5x the defect-free scale and 5-9x under the defect.
constexpr float CENTROID_TOL = 0.05f;

} // namespace

int main() {
    // THE CLAIM'S MODE (fixed-protocol decree): assert under the lever.
    test_env::set("MANIFOLD_SPAN", "1");   // portable shim (merge-policy gate)

    printf("\n=== MANIFOLD REDUCTION: THE SUPPORT SPANS THE FACE (G-51) ===\n");
    printf("    lever MANIFOLD_SPAN=1 (the claim's mode; born red at 0.3774 m,\n"
           "    green since the spanning reduction landed behind the lever)\n");

    // ------------------------------------------------------------------
    // Part 1 — CONTROL: exact stack, zero offset, zero tilt, through the
    // same OBB face path. Clip is the 4 corners, depths identical, no
    // reduction choice to make. Must be green in every world, or the
    // sweep below measures nothing.
    // ------------------------------------------------------------------
    printf("    [1] control: exact stack (no offset, no tilt)\n");
    {
        Particle lo = unit_box(0, 0, 0.5f, 0, 0, 0);
        Particle hi = unit_box(0, 0, 1.48f, 0, 0, 0);   // 20 mm overlap
        Reduced r = manifold_of(lo, hi);
        printf("      [measure] n=%d centroid=(%.4f,%.4f) n_vec=(%.3f,%.3f,%.3f)"
               " pen=[%.4f..%.4f]\n",
               r.n, r.cx, r.cy, r.nx, r.ny, r.nz, r.pen_min, r.pen_max);
        check(r.hit && r.n >= 4, "control: face contact carries >= 4 points (hygiene)");
        check(std::fabs(r.nz) > 0.99f, "control: normal is vertical (hygiene)");
        check(std::hypot(r.cx, r.cy) < CENTROID_TOL,
              "control: centroid at face centre (G-51)");
    }

    // ------------------------------------------------------------------
    // Part 2 — THE ROCKING CLOCK: the measured stack pose, tilt direction
    // swept around the compass. This is the oscillator's cycle unrolled
    // into poses: at every phase of the rock, the reduced set must keep
    // its centroid at the overlap centre. Today the deepest-4 selection
    // clusters downhill and the centroid follows the tilt around the
    // clock — the alternating-anchor witness, reproduced deterministically.
    // ------------------------------------------------------------------
    printf("    [2] the rocking clock: offset 0.1 mm, yaw 3e-4, tilt 3e-4, 12 phases\n");
    {
        // The settled column's regime, read off the canary: the boxes are
        // laterally aligned to well under a millimetre and RELATIVELY
        // YAWED by a few 1e-4 rad. There the clip is an 8-gon whose
        // near-parallel edge-plane crossings land MID-EDGE (the measured
        // vertices at y=-0.28, x=+0.31), and the micro-tilt orders them
        // so the deepest-4 selection clusters downhill. Offset alone
        // (no yaw) clips to a clean rectangle and hides the defect.
        const float TILT = 3e-4f;    // measured tilt scale (canary, G-51)
        const float OFF  = 1e-4f;    // measured lateral alignment scale
        const float YAW  = 3e-4f;    // relative yaw: makes the clip an 8-gon
        float worst = 0.0f; int worst_k = -1;
        int span_fails = 0;
        for (int k = 0; k < 12; ++k) {
            const float th = (float)k * (2.0f * (float)M_PI / 12.0f);
            // tilt about the horizontal axis (cos th, sin th): downhill
            // direction rotates around the clock like the rocking box's.
            const float rx = TILT * std::cos(th);
            const float ry = TILT * std::sin(th);
            Particle lo = unit_box(0, 0, 0.5f, 0, 0, 0);
            Particle hi = unit_box(OFF * std::cos(th + 0.7f),
                                   OFF * std::sin(th + 0.7f),
                                   1.48f, rx, ry, YAW);
            Reduced r = manifold_of(lo, hi);
            const float ecx = hi.x * 0.5f, ecy = hi.y * 0.5f; // overlap centre
            const float d = std::hypot(r.cx - ecx, r.cy - ecy);
            printf("      [measure] phase %2d: n=%d centroid=(%+.4f,%+.4f) "
                   "overlap-centre=(%+.4f,%+.4f) offset=%.4f pen=[%.4f..%.4f]\n",
                   k, r.n, r.cx, r.cy, ecx, ecy, d, r.pen_min, r.pen_max);
            if (!(r.hit && r.n >= 4)) span_fails++;
            if (d > worst) { worst = d; worst_k = k; }
        }
        printf("      [measure] worst centroid offset %.4f m at phase %d "
               "(defect band 0.25-0.45; spanning band < %.3f)\n",
               worst, worst_k, CENTROID_TOL);
        check(span_fails == 0, "every phase: face contact keeps >= 4 points (hygiene)");
        check(worst < CENTROID_TOL,
              "every phase: support centroid at the overlap centre (G-51)");
    }

    // ------------------------------------------------------------------
    // Part 3 — ONE STEP PAST: the half-off stack. The upper box hangs
    // 0.30 m to +X, so the overlap centre is legitimately at x = 0.15,
    // NOT the face centre. A "fix" that clamps the centroid to the face
    // centre instead of spanning the actual overlap polygon fails here.
    // ------------------------------------------------------------------
    printf("    [3] one step past: half-off stack, centre must follow the overlap\n");
    {
        Particle lo = unit_box(0, 0, 0.5f, 0, 0, 0);
        Particle hi = unit_box(0.30f, 0, 1.48f, 2e-4f, 2e-4f, 3e-4f);
        Reduced r = manifold_of(lo, hi);
        const float ecx = 0.15f, ecy = 0.0f;
        const float d = std::hypot(r.cx - ecx, r.cy - ecy);
        printf("      [measure] n=%d centroid=(%.4f,%.4f) overlap-centre=(%.4f,%.4f)"
               " offset=%.4f\n", r.n, r.cx, r.cy, ecx, ecy, d);
        check(r.hit && r.n >= 4, "half-off: face contact keeps >= 4 points (hygiene)");
        check(d < CENTROID_TOL,
              "half-off: centroid at the OVERLAP centre, not the face centre (G-51)");
    }

    printf("\n  %d checks, %d failed\n", checks_run, checks_failed);
    printf("=== %s ===\n\n", checks_failed == 0 ? "PASS" : "FAIL");
    return checks_failed == 0 ? 0 : 1;
}
