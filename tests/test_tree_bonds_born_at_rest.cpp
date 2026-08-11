// ============================================================================
// A TREE IS BORN AT REST (issue #38)
// ============================================================================
// THE LAW. A bond's rest geometry is where the generator PUT the two bodies.
// At frame zero, before gravity, before a single solver iteration, every bond
// must read a strain of 1.0. A structure born strained is a structure that
// tears itself apart, and no solver fix can save it.
//
// WHY. test_foliage_stays_attached fails with mean canopy drift 27.5 m against
// a 0.50 limit. TEAR_DEBUG showed 51 bonds tearing at exactly the 2.0x ratio
// with BOTH BODIES STATIONARY — nothing pushing them, nothing shaking. Three
// separate causes were proposed and each turned out to be a real bug that was
// NOT the cause:
//   - unrotated offsets in the tear check's rest term (real, fixed)
//   - rotating those offsets instead (wrong, kept as 1fc74be for its numbers)
//   - mixed-frame branch offsets, local vs world (real, fixed, two sites)
// After all three the canopy still leaves and bonds still tear at rest.
//
// So the remaining possibility, never tested: the generator PLACES the bodies
// at a separation its own bond does not agree with. This test asks that
// question directly, at creation, with physics never having run.
//
// It reports the distribution rather than one number, because "how many bonds
// are born wrong and by how much" is what decides whether this is a stray edge
// case or the whole structure.
//
//   ./build-release/logosphere-tests --test test_tree_bonds_born_at_rest --no-head
// ============================================================================

#include "../src/core/engine.h"
#include "../src/core/particle_system.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/worldgen/physics_tree_generator.h"
#include "logosphere/worldgen/tree_generator.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

bool test_tree_bonds_born_at_rest() {
    printf("\n=== A TREE IS BORN AT REST (issue #38) ===\n\n");

    EngineConfig cfg;
    cfg.create_display = false;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) { printf("  engine init failed\n  FAIL\n"); return false; }

    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();

    PhysicsTreeGenerator gen;
    gen.initialize(&engine);
    TreeSpec spec;
    spec.random_seed = 12345;   // same fixed seed test_foliage_stays_attached uses
    PhysicsTreeResult tree = gen.generate_tree_with_roots(0.0f, 0.0f, 0.0f, spec);
    (void)tree;
    ps.flush_pending_particles();

    const size_t n_gluons = physics.get_total_gluon_count();
    if (n_gluons == 0) {
        printf("  INCONCLUSIVE: the generator produced no bonds.\n  FAIL\n");
        engine.shutdown();
        return false;
    }

    // NO engine.update() ANYWHERE ABOVE. Everything below is the world as the
    // generator left it.
    struct Bad { size_t a, b; float dist, rest, ratio; };
    std::vector<Bad> worst;
    size_t born_taut = 0, born_torn = 0, counted = 0;
    float ratio_sum = 0.0f, ratio_max = 0.0f;

    {
        auto v = ps.lock_particles_for_write();
        std::map<std::pair<size_t,size_t>, bool> seen;
        for (size_t i = 0; i < v.size(); ++i) {
            for (const GluonConstraintBase* g : physics.get_gluons_for_particle(i)) {
                if (!g) continue;
                const size_t a = std::min(g->particle_a, g->particle_b);
                const size_t b = std::max(g->particle_a, g->particle_b);
                if (a >= v.size() || b >= v.size()) continue;
                if (seen.count({a,b})) continue;
                seen[{a,b}] = true;

                const float dx = v[b].x - v[a].x, dy = v[b].y - v[a].y,
                            dz = v[b].z - v[a].z;
                const float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
                // The SAME rest the tear law uses. If this test and the tear
                // law disagree the test is worthless, so it must not invent
                // its own definition.
                const float rest = std::max({g->target_distance,
                                             g->get_segment_length(), 0.01f});
                const float ratio = dist / rest;

                counted++;
                ratio_sum += ratio;
                ratio_max = std::fmax(ratio_max, ratio);
                if (ratio > 1.10f) born_taut++;
                if (ratio >= 2.00f) born_torn++;
                if (ratio > 1.10f && worst.size() < 6)
                    worst.push_back({a, b, dist, rest, ratio});
            }
        }
    }

    printf("  bonds inspected at frame ZERO (no physics has run)  %6zu\n", counted);
    printf("  mean strain at birth                               %8.4f  (want 1.0000)\n",
           counted ? ratio_sum / (float)counted : 0.0f);
    printf("  worst strain at birth                              %8.4f\n", ratio_max);
    printf("  bonds born TAUT   (> 1.10x)                        %6zu\n", born_taut);
    printf("  bonds born TORN   (>= 2.00x, the tear ratio)       %6zu\n", born_torn);

    if (!worst.empty()) {
        printf("\n  the worst offenders, as the generator left them:\n");
        for (const Bad& w : worst)
            printf("    P%-4zu<->P%-4zu  placed %7.4f m apart, bond rest %7.4f  -> %6.3fx\n",
                   w.a, w.b, w.dist, w.rest, w.ratio);
    }

    const bool pass = (born_torn == 0 && ratio_max <= 1.10f);
    printf("\n");
    if (!pass) {
        printf("  *** THE TREE IS BORN STRAINED. ***\n"
               "  %zu bonds are already at or past their tear ratio before a single\n"
               "  solver iteration has run, and %zu are taut. The generator places two\n"
               "  bodies at one separation and gives their bond a different one. No\n"
               "  solver change can hold a structure that arrives already broken, which\n"
               "  is why three separate real fixes to the tear path moved nothing.\n",
               born_torn, born_taut);
    } else {
        printf("  BORN AT REST. Every bond agrees with where its bodies were placed.\n");
    }
    printf("\n  %s\n", pass ? "PASS" : "FAIL (a structure must be born at rest)");
    engine.shutdown();
    return pass;
}
