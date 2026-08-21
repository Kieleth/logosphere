// CONTRADICTS-A-RULING (adjudicated 2026-08-21). This file's stated POLICY and
// an ACTIVE INVARIANT give opposite answers to the same question — may a
// GENERATOR hand the solver a world in which two bodies already interpenetrate
// — and the adjudication pass deliberately took NO side. Both are owner
// decisions, eleven days apart.
//
//   2026-08-02, the policy block at the bottom of this file: "MINIMAL OVERLAP
//   IS ACCEPTED", because driving it to zero cost the tree (rejecting a
//   colliding branch drops its whole subtree, 149 bodies became 59) and the
//   gate moved to test_foliage_stays_attached. That block is the ONLY carrier
//   of the decision: LEDGER.md has no 2026-08-02 entry.
//
//   2026-08-13, INV-30 (external-writers-place-nothing-illegal, status
//   active): a subsystem that owns a body's position from outside the solver
//   — "generators" is in the list, by name — "may not hand the solver a state
//   it would never have produced: no frame begins with an overlap beyond
//   SLOP", enforcement "STRICT-FIRST ... lenient mode only as the inventory
//   lever".
//
// Under 2026-08-02 this file reports and always exits 0, which is what it does
// today. Under INV-30 it gates, and the tree case runs under the inventory
// lever until the generator is fixed. Measured today, so the ruling is made
// against a number: 2 of 11781 pairs overlap deeper than SLOP, the deepest by
// 0.3426 m (branch 43 vs leaf 40, 342x SLOP), 0 pairs coincident.
//
// The full write-up, with both positions verbatim and a table of what each
// would demand, is in tests/invariants/TO_INVESTIGATE.md. NOTHING WAS CHANGED
// HERE and nothing may be until the owner rules: the verdict line below
// already says, out loud, that this file enforces neither law.
//
// =============================================================================
// NOTHING MAY BE CREATED INSIDE SOMETHING ELSE (issue #38)
// =============================================================================
// THE RULE THIS TEST DEFENDS: INV-4 (born-at-rest: no overlap beyond slop at
// frame zero, before a single solver iteration) and INV-30
// (external-writers-place-nothing-illegal: the doors INTO the solver are
// closed, not just the physics inside it).
//
// In this engine a particle is a body. It has mass, it takes up room, it
// collides. There is no such thing as a decorative particle that occupies no
// space. That is an engine invariant, not a preference.
//
// It follows that creating two bodies in the same place is not a small
// inaccuracy to be cleaned up later. It is an impossible world. The solver's
// only correct response to two objects inside each other is to push them apart,
// hard, and it does: measured at over 3 m/s in the tree case. Whatever the
// generator intended is then destroyed within a few frames.
//
// So the check belongs at CREATION, before the first step, where the cost of
// being wrong is a rejected placement instead of an explosion.
//
// WHAT THIS TEST DOES.
//
// Generates a tree and, before a single physics frame runs, compares every pair
// of its bodies. Anything overlapping by more than the solver's 1 mm slop is a
// generator bug, reported with the pair and the depth.
//
// It also reports how many bodies are EXACTLY COINCIDENT, sharing a position to
// within a millimetre, because that pattern points somewhere very specific: not
// a placement that came out slightly tight, but a placement that never happened
// at all.
//
// WHY A TEST AND NOT JUST A FIX.
//
// The engine already has a placement check, `try_place_with_retry`, and the tree
// generator already calls it with a generous 0.8 m gap and 30 attempts. Making
// the world legal is therefore not a matter of writing new geometry code. This
// test exists to say, in numbers, what the existing machinery is actually
// achieving, so that whatever gets changed can be shown to have changed it.
//
//   ./build-release/logosphere-tests --test test_no_overlap_at_creation --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "../src/core/particle_system.h"
#include "../src/particle.h"
#include "logosphere/worldgen/physics_tree_generator.h"
#include "logosphere/worldgen/tree_generator.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

// The solver ignores overlap under 1 mm (SLOP). Anything deeper is real.
constexpr float SLOP_M = 0.001f;
// Two bodies within a millimetre of each other are, for our purposes, in the
// same place. Nothing places a body that precisely by accident.
constexpr float COINCIDENT_M = 0.001f;

struct Body {
    int   id;
    float x, y, z;
    float hw, hh, ht;   // half extents
    const char* kind;
};

Body make_body(int id, const Particle& p, const char* kind) {
    Body b{id, p.x, p.y, p.z, 0, 0, 0, kind};
    if (p.shape == ParticleShape::BOX) {
        b.hw = p.width * 0.5f; b.hh = p.height * 0.5f; b.ht = p.thickness * 0.5f;
    } else {
        b.hw = b.hh = b.ht = p.size * 0.5f;
    }
    return b;
}

// Axis-aligned overlap depth: how far the two boxes interpenetrate along the
// axis where they overlap LEAST. Zero or negative means they are apart. This
// mirrors what the contact solver will compute, so the number here is the
// number that will be turned into an impulse on frame one.
float overlap_depth(const Body& a, const Body& b) {
    const float ox = (a.hw + b.hw) - std::fabs(a.x - b.x);
    const float oy = (a.hh + b.hh) - std::fabs(a.y - b.y);
    const float oz = (a.ht + b.ht) - std::fabs(a.z - b.z);
    if (ox <= 0.0f || oy <= 0.0f || oz <= 0.0f) return 0.0f;   // separated on some axis
    return std::min({ox, oy, oz});
}

float centre_distance(const Body& a, const Body& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

}  // namespace

bool test_no_overlap_at_creation() {
    printf("\n=== NOTHING MAY BE CREATED INSIDE SOMETHING ELSE (issue #38) ===\n");
    printf("A particle is a body and occupies space. Two bodies created in the same\n");
    printf("place is an impossible world, and the solver's only correct response is\n");
    printf("to throw them apart. This checks the world BEFORE the first frame.\n\n");

    EngineConfig cfg;
    cfg.create_display = false;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) { printf("  ERROR: engine init failed\n"); return false; }

    PhysicsTreeGenerator gen;
    gen.initialize(&engine);
    TreeSpec spec;
    spec.random_seed = 12345;

    PhysicsTreeResult tree = gen.generate_tree_with_roots(0.0f, 0.0f, 0.0f, spec);
    engine.get_particle_system().flush_pending_particles();
    // NO engine.update() ANYWHERE IN THIS TEST. The moment physics runs it
    // starts repairing the damage, and the question here is what was handed to
    // it, not what it managed to salvage.

    std::vector<Body> bodies;
    {
        auto v = engine.get_particle_system().lock_particles_for_write();
        // DEDUPE BY ID. trunk_id also appears in branch_ids, so a naive append
        // compares body 5 against itself and reports its own full width as an
        // overlap. Caught when the deepest "overlap" came back as
        // "trunk 5 vs branch 5", which is one body, not two.
        std::vector<int> seen;
        auto add = [&](int id, const char* kind) {
            if (id < 0 || (size_t)id >= v.size() || v[id].is_light_source) return;
            if (std::find(seen.begin(), seen.end(), id) != seen.end()) return;
            seen.push_back(id);
            bodies.push_back(make_body(id, v[id], kind));
        };
        add(tree.trunk_id, "trunk");
        for (int id : tree.branch_ids) add(id, "branch");
        for (int id : tree.leaf_ids)   add(id, "leaf");
        for (int id : tree.root_ids)   add(id, "root");
        add(tree.root_plate_id, "plate");
    }

    printf("  tree bodies: %zu  (trunk 1, branches %zu, leaves %zu, roots %zu)\n\n",
           bodies.size(), tree.branch_ids.size(), tree.leaf_ids.size(), tree.root_ids.size());

    if (bodies.size() < 2) {
        printf("  INCONCLUSIVE: fewer than two bodies, nothing to compare.\n");
        engine.shutdown();
        return true;
    }

    int   overlapping = 0, coincident = 0;
    float worst = 0.0f;
    int   worst_a = -1, worst_b = -1;
    const char *worst_ka = "", *worst_kb = "";
    // Every leaf that shares a position with at least one other body.
    std::vector<int> coincident_ids;

    for (size_t i = 0; i < bodies.size(); ++i) {
        bool i_coincident = false;
        for (size_t j = i + 1; j < bodies.size(); ++j) {
            const float d = overlap_depth(bodies[i], bodies[j]);
            if (d > SLOP_M) {
                overlapping++;
                if (d > worst) {
                    worst = d;
                    worst_a = bodies[i].id; worst_b = bodies[j].id;
                    worst_ka = bodies[i].kind; worst_kb = bodies[j].kind;
                }
            }
            if (centre_distance(bodies[i], bodies[j]) < COINCIDENT_M) {
                coincident++;
                i_coincident = true;
            }
        }
        if (i_coincident) coincident_ids.push_back(bodies[i].id);
    }

    // Dump every offending pair in full. A count tells you there is a problem;
    // the geometry tells you whether the placement check should have caught it.
    printf("\n  OFFENDING PAIRS (what the creation-time check was looking at):\n");
    printf("  %-16s %-22s %-22s %8s %8s\n", "pair", "A pos / half-extents", "B pos / half-extents", "depth", "gapNeed");
    int shown = 0;
    for (size_t i = 0; i < bodies.size() && shown < 10; ++i) {
        for (size_t j = i + 1; j < bodies.size() && shown < 10; ++j) {
            const float d = overlap_depth(bodies[i], bodies[j]);
            if (d <= SLOP_M) continue;
            const Body& A = bodies[i]; const Body& B = bodies[j];
            char pa[32], pb2[32], ea[32], eb[32];
            snprintf(pa, sizeof(pa), "%s%d(%.2f,%.2f,%.2f)", A.kind, A.id, A.x, A.y, A.z);
            snprintf(pb2, sizeof(pb2), "%s%d(%.2f,%.2f,%.2f)", B.kind, B.id, B.x, B.y, B.z);
            snprintf(ea, sizeof(ea), "%.3f/%.3f/%.3f", A.hw, A.hh, A.ht);
            snprintf(eb, sizeof(eb), "%.3f/%.3f/%.3f", B.hw, B.hh, B.ht);
            printf("  %-16s %-22s %-22s %8.4f\n", "", pa, pb2, d);
            printf("  %-16s   extents %-30s extents %s\n", "", ea, eb);
            shown++;
        }
    }

    const size_t pairs = bodies.size() * (bodies.size() - 1) / 2;
    printf("  %-38s %8d  of %zu pairs\n", "pairs overlapping deeper than 1mm", overlapping, pairs);
    printf("  %-38s %8d\n", "pairs at the SAME POSITION (<1mm apart)", coincident);
    printf("  %-38s %8.4f m   %s %d vs %s %d\n", "deepest overlap", worst,
           worst_ka, worst_a, worst_kb, worst_b);

    printf("\n");
    if (coincident > 0) {
        printf("  INV-4/INV-30: %d pairs of bodies were created AT THE SAME POINT. That is not a tight\n"
               "  placement, it is no placement: something is assigning positions without\n"
               "  regard to what is already there. Bodies affected: ", coincident);
        for (size_t k = 0; k < coincident_ids.size() && k < 12; ++k)
            printf("%d ", coincident_ids[k]);
        if (coincident_ids.size() > 12) printf("... (%zu total)", coincident_ids.size());
        printf("\n\n");
    }

    const bool clean = (overlapping == 0);
    if (clean) {
        printf("  INV-4/INV-30 CLEAN. Every body of the generated tree is clear of every other by at\n"
               "  least the 1 mm slop, so the solver is handed a legal world and has\n"
               "  nothing to repair on frame one.\n");
    } else {
        printf("  INV-4/INV-30 RESIDUAL OVERLAP AT CREATION (accepted, see policy below).\n"
               "  %d of %zu body pairs are already interpenetrating before physics has run\n"
               "  a single frame, the worst by %.4f m. The solver will do the only correct\n"
               "  thing and eject them, which is measured elsewhere at over 3 m/s, and the\n"
               "  canopy ends up scattered metres from where it was drawn.\n\n"
               "  This is not a solver bug and no amount of tuning fixes it. The world\n"
               "  handed to the solver is impossible.\n", overlapping, pairs, worst);
    }

    printf("\n  POLICY (owner decision, 2026-08-02): MINIMAL OVERLAP IS ACCEPTED.\n"
           "  Driving this to zero was tried and it cost the tree: rejecting a colliding\n"
           "  branch drops its whole subtree, and 149 bodies became 59. The complexity\n"
           "  IS the tree, so the gate moved to what actually shows on screen, which is\n"
           "  whether bodies get LAUNCHED. That gate is test_foliage_stays_attached:\n"
           "  peak speed under 2 m/s, mean canopy drift under 0.5 m.\n"
           "  This file REPORTS the overlap that remains, so it cannot creep unnoticed.\n");
    printf("\n  %s\n", "PASS (diagnostic: INV-4 and INV-30 are REPORTED here, "
           "not enforced — this function returns true unconditionally. See the "
           "TO-INVESTIGATE block at the top of this file)");
    engine.shutdown();
    return true;
}
