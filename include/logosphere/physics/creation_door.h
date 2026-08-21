#ifndef LOGOSPHERE_CREATION_DOOR_H
#define LOGOSPHERE_CREATION_DOOR_H

// ============================================================================
// THE CREATION DOOR — NOTHING IS BORN INSIDE ANYTHING (INV-30, INV-4, G-48)
// ============================================================================
// Owner ruling R8, 2026-08-21: "the generator should NOT put things
// overlapping... I'd not fix for penetration, I'd just avoid it fully...
// enforce nothing that is created, ever, can coexist — particles are always,
// 100% guaranteed not to overlap in 3d, ever. Solve that, and do not care
// about that in our physics engine."
//
// WHY THIS IS NOT A TOLERANCE PROBLEM.
//
// A particle is a body: it occupies space and it collides. Two bodies created
// in the same place is not a small error to be cleaned up on frame one, it is
// an impossible world, and the solver's only correct response is a separating
// impulse — measured at over 3 m/s of ejection on the tree case. A body born
// overlapping is a compressed spring with the energy already stored, and no
// amount of solver tuning gives that energy back.
//
// The ONE penetration this engine may legitimately see is the in-flight
// capture delta: a body moving fast enough that a single tick cannot resolve
// its approach. That overlap is produced BY the integration and bounded by
// v*dt. Creation is not that case. A generator knows where it is putting
// things and has no dt, so creation refuses instead of negotiating.
//
// TOUCHING IS NOT OVERLAPPING.
//
// The threshold is PhysicsV4::SLOP, the engine's existing geometric-error
// constant — not a new number. Adjacent floor tiles share a face; two bonded
// tree segments share a joint point. Those measure zero penetration and are
// legal. Anything deeper than SLOP is a placement the generator got wrong.
//
// THE GEOMETRY IS THE ENGINE'S OWN.
//
// The verdict runs the same narrow phase the solver runs, never a private
// axis-aligned copy, because the two disagree exactly where it matters: the
// world-axis extents of a rotated log are a DIFFERENT SOLID from the log
// (tests/test_collision_bounds_rotation.cpp part 5, and the 1.907 m ramp
// tunnel that produced it).
// ============================================================================

#include <cstddef>
#include <string>
#include <vector>

struct Particle;

namespace logosphere {

// One body as the door saw it, captured by value so the report survives the
// particle store being torn down (or the process aborting).
struct CreationBody {
    int         index = -1;
    const char* shape = "";
    float       x = 0, y = 0, z = 0;
    float       half[3] = {0, 0, 0};   // half extents along the body's OWN axes
    float       rot[3] = {0, 0, 0};    // rotation_x / _y / _z, radians
    float       world_min_z = 0;       // oriented bottom, world Z
    float       mass = 0;
    bool        rotated = false;
};

// A pair the door refuses: two bodies interpenetrating deeper than SLOP.
struct CreationOverlap {
    CreationBody a, b;
    float depth = 0;                   // deepest manifold point, metres
    float nx = 0, ny = 0, nz = 0;      // contact normal, B toward A
    bool  same_structure = false;      // bonded into one body by the gluon graph
};

// What one audit measured. `pairs_tested` is exact narrow-phase calls, which
// is the cost that matters; `candidates` is what the BVH handed back.
//
// TWO BANDS, and the split is not a tolerance — it is scope.
//
//   violations  pairs the solver WOULD build a contact row for. These are the
//               overlaps that become separating impulses on frame one, and
//               they are what the armed door refuses.
//   structural  pairs inside ONE gluon-bonded structure. The contact build
//               refuses to generate a row between them by declared law
//               (PhysicsSystem::bonded_components, and the RCA behind it:
//               when the narrow phase first SAW a tree's own crown crossings
//               it ejected leaves at 6.77 m/s). No row means no impulse and
//               no birth energy, so these cannot be refused today without
//               stopping every world from generating.
//
// R8's words are literal — "particles are always, 100% guaranteed not to
// overlap in 3d, ever" — and that includes the structural band. Reaching it
// is a generator redesign, not a threshold: `structural` is MEASURED and
// PUBLISHED on every audit so the size of that job is a number and not an
// opinion, and LOGOSPHERE_CREATION_STRICT_STRUCTURAL=1 promotes the band to
// refusals the day a generator can pass it.
struct CreationVerdict {
    std::size_t bodies_audited = 0;
    std::size_t candidates = 0;
    std::size_t pairs_tested = 0;
    // How many bonds existed when the verdict was taken. Reported because the
    // one way this door lies is by judging a structure before its gluons are
    // registered: particles are created first, constraints second, and a
    // half-declared body looks like loose parts inside each other. A refusal
    // arriving with 0 bonds on a scene that should have hundreds is an
    // ordering bug in the caller, not a placement bug.
    std::size_t bonds_at_verdict = 0;
    double      micros = 0.0;
    std::vector<CreationOverlap> violations;
    std::vector<CreationOverlap> structural;

    bool clean() const { return violations.empty(); }
    bool clean_including_structural() const {
        return violations.empty() && structural.empty();
    }
    static float deepest_of(const std::vector<CreationOverlap>& v) {
        float d = 0;
        for (const auto& o : v) if (o.depth > d) d = o.depth;
        return d;
    }
    float deepest() const { return deepest_of(violations); }
    float deepest_structural() const { return deepest_of(structural); }
};

// Describe a body the way the door reports it: oriented half-extents, the
// oriented world-Z bottom, shape name.
CreationBody describe_creation_body(int index, const Particle& p);

// How far a body reaches BELOW its own centre in world Z, for whatever
// orientation it carries. To rest a body on a surface: z = surface + this.
//
// Exists because the arithmetic is not the raw half-thickness and pretending
// it is has already cost the engine twice. A log laid flat carries its LENGTH
// on the thickness axis and its DIAMETER in world Z, so a generator that
// offsets by half the thickness hovers the whole log (the four fallen-tree
// presets, 0.21 to 0.29 m each). Ask the geometry instead of assuming it.
float oriented_bottom_offset(const Particle& p);

// Upper bound on that reach over ALL orientations: the half diagonal. No
// quaternion, no matrix, three multiplies and a square root. A body with more
// clearance than this cannot be below a plane whatever pose it is in, so the
// exact extent need not be built — the same cost guard the solver's turtle
// pass uses, stated once instead of twice.
float max_bottom_reach(const Particle& p);

// Exact penetration depth between two bodies through the engine's own narrow
// phase. Returns 0.0 when they are separated or merely touching. Rotated
// boxes go through the oriented path on both sides; everything else uses the
// same dispatcher the solver's broad phase feeds.
float creation_penetration(const Particle& a, const Particle& b,
                           float& out_nx, float& out_ny, float& out_nz);

// The full-detail refusal text for one pair: both bodies, their shapes,
// positions, oriented extents, and the depth. Written once and used by both
// the abort path and the lenient per-pair error line, so the two can never
// say different things about the same geometry.
std::string creation_violation_text(const CreationOverlap& v);

}  // namespace logosphere

#endif  // LOGOSPHERE_CREATION_DOOR_H
