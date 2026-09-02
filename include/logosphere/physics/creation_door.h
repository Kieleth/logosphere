#ifndef LOGOSPHERE_CREATION_DOOR_H
#define LOGOSPHERE_CREATION_DOOR_H

// ============================================================================
// THE CREATION DOOR — NOTHING IS BORN INSIDE ANYTHING (INV-37)
// ============================================================================
// Owner decree 2026-09-01: "under no circumstances, any creation of particles
// should be allowed to overlap in space with another", and "we should have an
// assert/except for any creation-overlapping moment". Owner ruling 2026-09-02
// on the shape: "fresh at the choke point, with the incremental BVH cost
// measured on the headless bench before it ships, in TDD please."
//
// WHY THIS IS NOT A TOLERANCE PROBLEM.
//
// A particle is a body: it occupies space and it collides. Two bodies created
// in the same place is not a small error to be cleaned up on frame one, it is
// an impossible world. The solver's only correct response to two solids in one
// place is a separating impulse — measured at over 3 m/s of ejection on the
// tree case — and when the pair cannot separate (a 42 kg stone inside a 12-ton
// strata tile) neither body may ever sleep, which is the frame collapse
// GEDANKEN-67 traced: Eden at 3.4 s of physics per frame from ~900 bodies that
// could not go quiet.
//
// The ONE penetration this engine may legitimately see is the in-flight
// capture delta: a body moving fast enough that a single tick cannot resolve
// its approach. That overlap is produced BY the integration and bounded by
// v*dt. Creation is not that case. A generator knows where it is putting
// things and has no dt, so creation refuses instead of negotiating.
//
// TOUCHING IS NOT OVERLAPPING. The threshold is PhysicsV4::SLOP, the
// engine's existing geometric-error constant — not a new number (INV-29).
// Adjacent floor tiles share a face; two bonded tree segments share a joint
// point. Those measure zero penetration and are legal.
//
// NO CLASS LIST. INV-37 is literal: a bonded structure's own boxes are
// refused exactly like anything else, because "the generator drew it that
// way" is a defect at the generator, not a licence.
//
// THE GEOMETRY IS THE ENGINE'S OWN. The verdict runs the same narrow phase
// the solver runs, never a private axis-aligned copy, because the two
// disagree exactly where it matters: the world-axis extents of a rotated log
// are a DIFFERENT SOLID from the log (INV-12; tests/test_ramp_race.cpp
// measured a sphere falling 1.907 m through a tilted face).
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "logosphere/physics/narrow_phase.h"   // AABB6

struct Particle;

namespace logosphere {

// One body as the door saw it, captured by value so the report survives the
// particle store being torn down.
struct CreationBody {
    int         index = -1;            // live-array index (-1 for a newborn)
    const char* shape = "";
    float       x = 0, y = 0, z = 0;
    float       half[3] = {0, 0, 0};   // half extents along the body's OWN axes
    float       rot[3] = {0, 0, 0};    // rotation_x / _y / _z, radians
    float       world_min_z = 0;       // oriented bottom, world Z
    float       mass = 0;
    bool        rotated = false;
};

// The verdict on one birth. `refused` false means the body was created.
struct CreationRefusal {
    bool  refused = false;
    int   would_be_index = -1;     // the live index the body would have taken
    float depth = 0.0f;            // metres, deepest manifold point
    float nx = 0, ny = 0, nz = 0;  // contact normal, blocker toward newborn
    CreationBody newborn;
    CreationBody blocker;
    std::string  text;             // the full [PHYSICS REFUSED] line
};

// What the door has cost so far, and what it has refused. Reported once per
// ParticleSystem so the price of the law is a number, not an opinion.
struct CreationDoorStats {
    std::size_t births = 0;         // bodies offered to the door
    std::size_t refusals = 0;
    std::size_t candidates = 0;     // index hits
    std::size_t exact_tests = 0;    // narrow-phase calls
    std::size_t rebuilds = 0;       // index rebuilt from scratch (indices moved)
    std::size_t refits = 0;         // leaf bounds re-read (the world moved)
    double      micros = 0.0;       // door time only, index maintenance included
    double      rebuild_micros = 0.0;
    double      refit_micros = 0.0;
};

// ---------------------------------------------------------------------------
// THE INDEX: an incremental AABB tree.
// ---------------------------------------------------------------------------
// The engine's BVH (include/logosphere/physics/bvh.h) can build and refit and
// cannot insert, so a door standing at every birth would pay a full rebuild
// per body: 20.4 ms for 12,440 bodies, measured on the batch-audit branch, and
// Eden's preload creates that many. This is the missing operation and nothing
// else: leaves keyed by live-array index, insert in O(depth) by the
// surface-area heuristic, refit when the world has moved, rebuild only when
// swap-and-pop has changed what an index means.
//
// Bounds are the ORIENTED world bounds of the body (INV-12): a tilted blade's
// length leaves Z and enters Y, and a broad phase that under-covers means the
// narrow phase is never consulted at all.
class CreationIndex {
public:
    void clear();
    void rebuild(const std::vector<Particle>& particles);
    void refit(const std::vector<Particle>& particles);
    void insert(int body, const AABB6& bounds);
    void query(const AABB6& box, std::vector<int>& out) const;

    std::size_t leaves() const { return leaf_count_; }
    bool        empty() const { return root_ < 0; }
    int         depth() const;   // diagnostics only

private:
    struct Node {
        AABB6 box{};
        int   parent = -1;
        int   child1 = -1;
        int   child2 = -1;
        int   body   = -1;    // >= 0 => leaf
        int   next   = -1;    // free list
        bool  leaf() const { return child1 < 0; }
    };

    int  allocate();
    void free_node(int i);
    void insert_leaf(int leaf);
    void refit_up(int i);

    std::vector<Node> nodes_;
    int         root_ = -1;
    int         free_ = -1;
    std::size_t leaf_count_ = 0;
};

// The oriented world bounds a body occupies. One definition, used by the
// index and by the door's query box, so a body cannot be stored under one
// bound and looked up under another.
AABB6 creation_bounds(const Particle& p);

// Describe a body the way the door reports it: oriented half-extents, the
// oriented world-Z bottom, shape name.
CreationBody describe_creation_body(int index, const Particle& p);

// Exact penetration depth between two bodies through the engine's own narrow
// phase. Returns 0.0 when they are separated or merely touching. Rotated box
// pairs go through the oriented path on both sides; everything else uses the
// same dispatcher the solver's broad phase feeds.
float creation_penetration(const Particle& a, const Particle& b,
                           float& out_nx, float& out_ny, float& out_nz);

// The refusal line: both bodies, the depth in millimetres, the law. Written
// once so the log, the stored verdict and any test read the same sentence.
std::string creation_refusal_text(const CreationRefusal& r,
                                  const char* door_name);

// A coarse signature for the census: shape and dimensions in millimetres. Two
// refusals with the same signature came from the same recipe, which is what
// makes the census a list of SITES rather than a list of bodies.
std::string creation_site_signature(const CreationBody& newborn,
                                    const CreationBody& blocker);

// The kill switch (INV-36's pattern). CREATION_DOOR=0 disables the door for
// A/B measurement only — a diagnostic, never a shipping mode. Read once.
bool creation_door_enabled();

}  // namespace logosphere

#endif  // LOGOSPHERE_CREATION_DOOR_H
