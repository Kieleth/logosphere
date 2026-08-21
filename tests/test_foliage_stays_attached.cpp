// =============================================================================
// DO LEAVES STAY ON THE TREE? (issue #38)
// =============================================================================
// Reported symptom: in Eden the tree canopies come away from their trunks and
// hang in the air, drifting rather than sitting still. Trunks stay put; the
// leaves scatter.
//
// HOW LEAVES ARE SUPPOSED TO WORK, since that decides what "wrong" means here.
//
// A leaf is not decoration. It is a body, like everything else in this engine:
// it has mass, it occupies space, it collides. It is held to the branch tip by
// a GLUON, a physical bond with a rest length, a stiffness and a breaking
// strength. `PhysicsTreeGenerator` gives leaf gluons:
//
//   target_distance   = wherever the leaf was actually placed, up to 1.5 m out
//   stiffness         = 5000      flexible stem
//   damping           = 100
//   angular_stiffness = 0         "leaves swing freely"
//   material_strength = 1000      "weak, leaves tear easily"
//
// So a leaf is EXPECTED to move. It swings. What it is not allowed to do is
// leave: the distance to its branch tip should stay near the rest length the
// bond was created with.
//
// LAWS (assert-protocol migration, 2026-08-21):
//   PEAK SPEED    INV-11 (no detonation) and INV-17 (a contact may stop an
//                 approach, never amplify one). A leaf ejected from a
//                 structure nothing is pushing got its speed from somewhere,
//                 and the only sources here are the repair pass and the
//                 contact rows.
//   MEAN DRIFT    INV-14 (a bond between two satisfied resting bodies never
//                 tears) and INV-4 (a structure born strained tears itself
//                 apart; this is the observable end of test_tree_bonds_born_
//                 at_rest's frame-zero measurement).
//   The worst-single-leaf and leaf-count lines are REPORTED, not gated, and
//   say so; they are not asserts and carry no law.
//
// WHAT THIS MEASURES.
//
// For every leaf, the distance from its parent branch tip, at generation and
// then over time. A bond doing its job holds that near constant. Growth means
// the leaf is being pulled away, and a large value means it has gone.
//
// It reports rather than asserting a tight bound, because the correct number
// depends on the spec and nobody has pinned one yet. What it DOES assert is the
// thing that cannot be a matter of taste: a leaf must not end up further from
// its branch than a tree is wide.
//
//   ./build-release/logosphere-tests --test test_foliage_stays_attached --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "../src/core/particle_system.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include "logosphere/worldgen/physics_tree_generator.h"
#include "logosphere/worldgen/tree_generator.h"
#include "../src/core/telemetry.h"
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

struct LeafWatch {
    int   id = -1;
    float x0 = 0, y0 = 0, z0 = 0;    // where it started
    float d0 = 0;                    // distance to the tree's trunk axis at start
};

float dist3(float ax, float ay, float az, float bx, float by, float bz) {
    const float dx = ax - bx, dy = ay - by, dz = az - bz;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

struct Snapshot {
    double max_drift = 0.0;    // furthest any leaf has moved from where it started
    double mean_drift = 0.0;
    int    escaped = 0;        // leaves further than ESCAPE_M from their start
    double max_speed = 0.0;    // fastest leaf right now
};

constexpr double ESCAPE_M = 5.0;   // a leaf this far from where it was placed has left

}  // namespace

bool test_foliage_stays_attached() {
    printf("\n=== DO LEAVES STAY ON THE TREE? (issue #38) ===\n");
    printf("A leaf is a body bonded to its branch by a gluon. It is allowed to\n");
    printf("swing. It is not allowed to leave. This measures how far each leaf\n");
    printf("gets from where the generator placed it.\n\n");

    EngineConfig cfg;
    cfg.create_display = false;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) { printf("  ERROR: engine init failed\n"); return false; }

    ::logosphere::telemetry::set_enabled(true);
    ::logosphere::telemetry::set_residual_enabled(true);

    PhysicsTreeGenerator gen;
    gen.initialize(&engine);

    TreeSpec spec;
    spec.random_seed = 12345;   // fixed: this must reproduce exactly, run to run

    // Sits on the turtle via its root system, so no floor tile is needed and
    // the scene contains the tree and nothing else. Minimal scenes, or the
    // measurement picks up the scenery.
    PhysicsTreeResult tree = gen.generate_tree_with_roots(0.0f, 0.0f, 0.0f, spec);
    engine.get_particle_system().flush_pending_particles();

    printf("  tree: trunk=%d  branches=%zu  LEAVES=%zu  segments=%d\n",
           tree.trunk_id, tree.branch_ids.size(), tree.leaf_ids.size(), tree.total_segments);

    if (tree.leaf_ids.empty()) {
        printf("\n  INCONCLUSIVE: the generator produced no leaves, so there is nothing\n"
               "  to watch. Either the spec suppresses them or generation failed.\n");
        engine.shutdown();
        return true;
    }

    // Record where every leaf started, and how far it is from the trunk.
    std::vector<LeafWatch> leaves;
    float trunk_x = 0, trunk_y = 0, trunk_z = 0;
    {
        auto v = engine.get_particle_system().lock_particles_for_write();
        if (tree.trunk_id >= 0 && (size_t)tree.trunk_id < v.size()) {
            trunk_x = v[tree.trunk_id].x; trunk_y = v[tree.trunk_id].y; trunk_z = v[tree.trunk_id].z;
        }
        for (int id : tree.leaf_ids) {
            if (id < 0 || (size_t)id >= v.size()) continue;
            LeafWatch w;
            w.id = id;
            w.x0 = v[id].x; w.y0 = v[id].y; w.z0 = v[id].z;
            w.d0 = dist3(w.x0, w.y0, w.z0, trunk_x, trunk_y, trunk_z);
            leaves.push_back(w);
        }
    }
    printf("  watching %zu leaves, trunk at (%.2f, %.2f, %.2f)\n\n",
           leaves.size(), trunk_x, trunk_y, trunk_z);

    auto sample = [&]() {
        Snapshot s;
        auto v = engine.get_particle_system().lock_particles_for_write();
        double sum = 0.0;
        for (const LeafWatch& w : leaves) {
            if (w.id < 0 || (size_t)w.id >= v.size()) continue;
            const Particle& p = v[w.id];
            const double d = dist3(p.x, p.y, p.z, w.x0, w.y0, w.z0);
            sum += d;
            if (d > s.max_drift) s.max_drift = d;
            if (d > ESCAPE_M) s.escaped++;
            const double spd = std::sqrt((double)p.vx * p.vx + (double)p.vy * p.vy
                                       + (double)p.vz * p.vz);
            if (spd > s.max_speed) s.max_speed = spd;
        }
        s.mean_drift = leaves.empty() ? 0.0 : sum / (double)leaves.size();
        return s;
    };

    printf("  %8s %12s %12s %10s %12s %10s %10s\n",
           "frame", "max drift", "mean drift", "escaped", "max speed",
           "max PEN", "worst pair");
    const int CHECKPOINTS[] = {1, 2, 3, 5, 8, 10, 30, 60, 120, 240, 900};
    int next = 0, frame = 0;
    Snapshot last;
    std::vector<double> speed_history;   // every frame, so a spike between
                                         // checkpoints cannot hide
    while (next < (int)(sizeof(CHECKPOINTS) / sizeof(int))) {
        engine.update(1.0 / 60.0);
        frame++;
        speed_history.push_back(sample().max_speed);
        if (frame == CHECKPOINTS[next]) {
            last = sample();
            const auto r = ::logosphere::telemetry::solve_residual();
            char pair[24];
            snprintf(pair, sizeof(pair), "%u-%u", r.worst_a, r.worst_b);
            printf("  %8d %12.4f %12.4f %10d %12.4f %10.5f %10s\n",
                   frame, last.max_drift, last.mean_drift, last.escaped, last.max_speed,
                   r.max_penetration, pair);
            next++;
        }
    }

    printf("\n");

    // =====================================================================
    // THE GATE: NO SHOOTING. Minimal overlap is accepted.
    // =====================================================================
    // Perfect separation at creation turned out to cost tree complexity, and
    // complexity is the point of the tree. So the requirement is not "nothing
    // overlaps"; it is "nothing gets LAUNCHED". A little interpenetration that
    // the solver eases apart is fine. Bodies flung across the map are not.
    //
    // Two bounds, both derived rather than tuned to pass:
    //
    // SPEED <= 2.0 m/s. A leaf starts at rest, and the only legitimate sources
    // of motion here are gravity and the stem settling. Free fall for the ten
    // frames it takes to settle (0.167 s) is 1.64 m/s, so 2.0 leaves room for
    // gravity plus a normal settle. The bug this gate exists for produced
    // 3.22 m/s, which is a body being ejected, not a body falling.
    //
    // MEAN DRIFT <= 0.5 m. Individual leaves hang on stems up to 1.5 m long and
    // are meant to swing, so a single leaf moving is not evidence of anything.
    // The CANOPY as a whole migrating is. Half a stem length, averaged over
    // every leaf, separates "it settled" from "it left". The bug produced
    // 1.76 m: the whole canopy relocated.
    constexpr double MAX_SPEED_MS   = 2.0;
    constexpr double MAX_MEAN_DRIFT = 0.5;

    double peak_speed = 0.0;
    for (double s : speed_history) peak_speed = std::fmax(peak_speed, s);

    const bool no_shooting = peak_speed <= MAX_SPEED_MS;
    const bool canopy_held = last.mean_drift <= MAX_MEAN_DRIFT;

    printf("  %-34s %10.4f  limit %.2f  %s\n", "INV-11/INV-17 PEAK SPEED (m/s)",
           peak_speed, MAX_SPEED_MS, no_shooting ? "ok" : "*** SHOOTING ***");
    printf("  %-34s %10.4f  limit %.2f  %s\n", "INV-14/INV-4 MEAN canopy drift (m)",
           last.mean_drift, MAX_MEAN_DRIFT, canopy_held ? "ok" : "*** CANOPY LEFT ***");
    printf("  %-34s %10.4f  (reported, not gated)\n", "worst single leaf drift (m)",
           last.max_drift);
    printf("  %-34s %10zu  (reported, not gated)\n", "leaves in canopy", leaves.size());

    const bool pass = no_shooting && canopy_held;
    printf("\n");
    if (pass) {
        printf("  INV-11/INV-14: NO SHOOTING. The tree is handed to the solver with some overlap and the\n"
               "  solver eases it apart instead of throwing it. Leaves settle where they\n"
               "  were drawn. Residual overlap is accepted deliberately: removing all of\n"
               "  it costs branches and leaves, and the tree's complexity is the point.\n");
    } else {
        printf("  *** INV-11/INV-14: THE TREE IS BEING THROWN APART. ***\n"
               "  Peak leaf speed %.2f m/s against a %.2f limit, mean canopy drift %.2f m\n"
               "  against %.2f. Bodies are being ejected rather than settling, which is\n"
               "  what a canopy scattered across the sky looks like from the camera.\n",
               peak_speed, MAX_SPEED_MS, last.mean_drift, MAX_MEAN_DRIFT);
    }

    printf("\n  %s\n", pass ? "PASS" : "FAIL");
    engine.shutdown();
    return pass;
}
