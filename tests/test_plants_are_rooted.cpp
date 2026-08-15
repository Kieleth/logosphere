// =============================================================================
// A PLANT IS ATTACHED TO THE GROUND (issue #47, the half that was missed)
// =============================================================================
// THE INVARIANT, in one sentence: every body in a plant must be connected,
// through bonds, to something immobile. A blade may bend, lean, tear and fall.
// It may not be born already free of the earth.
//
// WHY THIS TEST EXISTS. 07ea3c3 bonded organic entities and rooted them:
//
//     if (!trunk_particles.empty()) {
//         trunk_particles.front().solver_mode = KINEMATIC;   // "ROOT THE PLANT"
//     }
//     ...
//     } else if (!trunk_kg.empty()) {
//         bond(trunk_kg.back(), crown_kg[cidx]);             // fallback bond
//     }
//     // No trunk and no parent: the plant's own base, nothing to bond to.
//
// Both mechanisms hang off a non-empty trunk. generate_trunk() returns an
// EMPTY vector when height * trunk_ratio < 0.05 m, and grass_blade() sets
// trunk_ratio = 0.1, so:
//
//     short grass  0.15 m x 0.1 = 0.015 m   -> always trunkless
//     tall grass   0.80 m x 0.1 = 0.080 m   -> trunked, but the +/-50% height
//                                              jitter drops some blades under
//                                              0.05 and they lose it too
//
// So grass — the entire subject of issue #47 — is NEVER rooted, and a blade
// that grows a single segment gets no bond at all. Measured in Eden at that
// commit: 26 Grass entities activating with 1 particle and 0 gluons, free
// bodies with nothing holding them, and the frame rate collapsing from 155 ms
// to 482 ms with the worst body at 331 km/s.
//
// The existing gate, test_grass_holds_together, uses tall_grass(). It passes
// because it exercises the case that works. This one runs BOTH.
//
// WHAT COUNTS AS ROOTED. solver_mode == KINEMATIC, and nothing else.
// is_at_rest is a solver optimisation, not immobility (CLAUDE.md), and the
// vegetation activator sets it on every particle it loads — so believing it
// would make every plant look anchored while it sails away.
//
// TWO ASSERTIONS, in dependency order:
//   1  BODIES EXIST after activation, or everything below is vacuous.
//   2  EVERY CONNECTED COMPONENT OF THE BOND GRAPH CONTAINS A ROOT. One
//      statement covering both defects: an unrooted chain is a component with
//      no KINEMATIC member, and a free singleton is a component of size one
//      with no KINEMATIC member.
//
//   ./build-release/logosphere-tests --test test_plants_are_rooted --no-head
// =============================================================================

#include "generated/earth_ontology_registry.h"
#include "../src/core/engine.h"
#include "../src/core/particle_system.h"
#include "../src/particle.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/worldgen/organic_generator.h"
#include "logosphere/worldgen/grass_patch_spec.h"
#include "logosphere/worldgen/scene_chunk_generator.h"
#include "logosphere/worldgen/worldgen_system.h"
#include "logosphere/kg/ontology_registry.h"

#include <cstdio>
#include <map>
#include <set>
#include <vector>

namespace {

struct Verdict {
    size_t bodies = 0;
    size_t gluons = 0;
    size_t components = 0;
    size_t rooted_components = 0;
    size_t unrooted_components = 0;
    size_t free_singletons = 0;      // unrooted components of exactly one body
    size_t bodies_adrift = 0;        // bodies in any unrooted component
};

// Union-find over the materialised bodies, joined by their bonds.
struct DisjointSet {
    std::vector<size_t> parent;
    explicit DisjointSet(size_t n) : parent(n) {
        for (size_t i = 0; i < n; ++i) parent[i] = i;
    }
    size_t find(size_t a) {
        while (parent[a] != a) { parent[a] = parent[parent[a]]; a = parent[a]; }
        return a;
    }
    void join(size_t a, size_t b) {
        a = find(a); b = find(b);
        if (a != b) parent[b] = a;
    }
};

Verdict probe_species(Engine& engine, const char* label, const GrassPatchSpec& spec) {
    Verdict v;
    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();

    const size_t first = [&] { auto w = ps.lock_particles_for_write(); return w.size(); }();

    auto& ogen  = engine.get_worldgen_system().get_organic_generator();
    auto& scene = engine.get_worldgen_system().get_scene_generator();
    kg::EntityID patch = ogen.generate_grass_patch(0.0f, 0.0f, 0.0f, spec);
    scene.activate_entity_now(patch);
    ps.flush_pending_particles();

    // Collect what this species materialised, and who is immobile.
    std::vector<size_t> ids;
    std::vector<bool>   rooted;
    {
        auto w = ps.lock_particles_for_write();
        for (size_t i = first; i < w.size(); ++i) {
            ids.push_back(i);
            rooted.push_back(w[i].solver_mode == ParticleSolverMode::KINEMATIC);
        }
    }
    v.bodies = ids.size();
    if (v.bodies == 0) return v;

    std::map<size_t, size_t> slot;                 // particle index -> local slot
    for (size_t k = 0; k < ids.size(); ++k) slot[ids[k]] = k;

    // Join every pair the bonds connect.
    DisjointSet ds(ids.size());
    std::set<std::pair<size_t, size_t>> seen_bonds;
    for (size_t k = 0; k < ids.size(); ++k) {
        for (const GluonConstraintBase* g : physics.get_gluons_for_particle(ids[k])) {
            if (!g) continue;
            auto ia = slot.find(g->particle_a);
            auto ib = slot.find(g->particle_b);
            if (ia == slot.end() || ib == slot.end()) continue;   // bond leaves the patch
            seen_bonds.insert({std::min(g->particle_a, g->particle_b),
                               std::max(g->particle_a, g->particle_b)});
            ds.join(ia->second, ib->second);
        }
    }
    v.gluons = seen_bonds.size();

    // A component is rooted if ANY of its members is immobile.
    std::map<size_t, bool>   comp_rooted;
    std::map<size_t, size_t> comp_size;
    for (size_t k = 0; k < ids.size(); ++k) {
        const size_t r = ds.find(k);
        comp_size[r]++;
        comp_rooted[r] = comp_rooted.count(r) ? (comp_rooted[r] || rooted[k]) : rooted[k];
    }
    for (const auto& [root, is_rooted] : comp_rooted) {
        v.components++;
        if (is_rooted) { v.rooted_components++; continue; }
        v.unrooted_components++;
        v.bodies_adrift += comp_size[root];
        if (comp_size[root] == 1) v.free_singletons++;
    }

    printf("  %-12s bodies %-5zu bonds %-5zu | components %-4zu rooted %-4zu "
           "UNROOTED %-4zu (of which free singletons %zu)\n",
           label, v.bodies, v.gluons, v.components, v.rooted_components,
           v.unrooted_components, v.free_singletons);

    // NAME THE ADRIFT. A count says something is wrong; this says what it is.
    // Print the first few bodies that sit alone in a component of one, with
    // enough of their identity to trace them back to the code that made them.
    size_t shown = 0;
    auto w = ps.lock_particles_for_write();
    for (size_t k = 0; k < ids.size() && shown < 4; ++k) {
        if (ds.find(k) != k) continue;                     // not a component root
        size_t members = 0;
        for (size_t m = 0; m < ids.size(); ++m) if (ds.find(m) == k) members++;
        if (members != 1 || rooted[k]) continue;
        const Particle& p = w[ids[k]];
        printf("        adrift P%-6zu  %.3f x %.3f x %.3f m  mass %.6f kg  "
               "pos (%.2f, %.2f, %.2f)  light=%d  owner=%d\n",
               ids[k], p.width, p.height, p.thickness, p.GetMass(),
               p.x, p.y, p.z, (int)p.is_light_source, (int)p.owner);
        shown++;
    }
    return v;
}

} // namespace

bool test_plants_are_rooted() {
    printf("\n=== A PLANT IS ATTACHED TO THE GROUND (issue #47) ===\n\n");

    EngineConfig cfg;
    cfg.create_display = false;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) { printf("  ERROR: engine init failed\n"); return false; }

    // Same sanctioned ontology extension the existing grass gate uses: the
    // bare engine ontology has no Grass, and without this every createEntity
    // returns 0 and the test fails on plumbing instead of on physics.
    {
        kg::OntologyRegistry reg;
        // The union-merged registry validates parents (malleus H2 made real):
        // a runtime extension must declare the chain it claims.
        // The CANONICAL grass vocabulary: the generated earth-pack
        // registry. Hand-rolled fixture registries drifted from the
        // generator's real writes 16 keys deep (2026-08-14).
        (void)reg;
        engine.get_kg().extendOntology(earth::ontology::registry());
    }

    // Eden plants both, 26 short to 14 tall. The existing gate only ever ran
    // the tall one.
    const Verdict tall  = probe_species(engine, "tall_grass",  GrassPatchSpec::tall_grass());
    const Verdict shrt  = probe_species(engine, "short_grass", GrassPatchSpec::short_grass());

    printf("\n");
    if (tall.bodies == 0 || shrt.bodies == 0) {
        printf("  *** NOTHING MATERIALISED. ***\n"
               "  A species produced zero bodies, so this test cannot say anything about\n"
               "  rooting. The activation path is broken or has moved.\n\n  FAIL\n");
        engine.shutdown();
        return false;
    }

    const size_t adrift    = tall.bodies_adrift + shrt.bodies_adrift;
    const size_t singles   = tall.free_singletons + shrt.free_singletons;
    const size_t unrooted  = tall.unrooted_components + shrt.unrooted_components;
    const size_t total     = tall.bodies + shrt.bodies;

    printf("  %-46s %6zu\n", "bodies materialised, both species", total);
    printf("  %-46s %6zu\n", "UNROOTED components (want 0)", unrooted);
    printf("  %-46s %6zu\n", "bodies adrift in them (want 0)", adrift);
    printf("  %-46s %6zu\n", "of those, free singletons (want 0)", singles);

    const bool pass = (unrooted == 0);
    printf("\n");
    if (pass) {
        printf("  ROOTED. Every body in every plant reaches something immobile through\n"
               "  its bonds. A blade can bend and tear; it cannot be born adrift.\n");
    } else {
        printf("  *** %zu PLANTS ARE NOT ATTACHED TO ANYTHING. ***\n"
               "  %zu bodies float in %zu components that contain no KINEMATIC particle,\n"
               "  %zu of them single bodies bonded to nothing at all. generate_trunk()\n"
               "  returns empty below 0.05 m of trunk, grass_blade() asks for\n"
               "  trunk_ratio 0.1, and BOTH the rooting and the fallback bond in\n"
               "  OrganicGenerator::generate are written as `if (!trunk.empty())`. So the\n"
               "  one plant issue #47 is about is the one plant that never gets a root.\n",
               unrooted, adrift, unrooted, singles);
    }

    printf("\n  %s\n", pass ? "PASS" : "FAIL");
    engine.shutdown();
    return pass;
}
