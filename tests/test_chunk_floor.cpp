// ============================================================================
// CHUNK FLOOR STREAMING TEST
// ============================================================================
// Verifies the FloorGenerator creates floor tiles at the correct world
// positions when preloading around a given observer position.
//
// The bug: chunks were loading at world origin instead of around the
// observer position, leaving Eva standing on void.
//
// Run:
//   ./logosphere-tests --test test_chunk_floor
// ============================================================================

#include "../src/test_context.h"
#include "../src/core/engine.h"
#include "../src/particle.h"
#include "logosphere/worldgen/worldgen_system.h"
#include "logosphere/worldgen/floor_generator.h"
#include <cstdio>
#include <cmath>
#include <vector>

bool test_chunk_floor(TestContext& ctx) {
    printf("\n=== Chunk Floor Streaming Test ===\n");

    auto& engine = ctx.get_engine();
    auto& ps = engine.get_particle_system();
    const float dt = 1.0f / 60.0f;

    ctx.clear_particles();

    // Observer at (25, -25) — same as Eden's Eva position
    float obs_x = 25.0f, obs_y = -25.0f;

    // Configure floor generator
    auto& floor_gen = engine.get_worldgen_system().get_floor_generator();
    floor_gen.set_tile_size(2.0f);
    floor_gen.set_tile_thickness(0.1f);
    floor_gen.set_tiles_per_chunk(5);      // 5x5 = 25 tiles per chunk (small for testing)
    floor_gen.set_tiles_per_entity(1);
    floor_gen.set_load_radius(30.0f);
    floor_gen.set_unload_radius(40.0f);
    floor_gen.set_floor_type(FloorType::GRASS);
    floor_gen.set_enabled(true);

    printf("  Observer at (%.0f, %.0f)\n", obs_x, obs_y);
    printf("  Tile size: 2m, tiles/chunk: 5x5, load radius: 30m\n");

    // Preload around observer
    floor_gen.preload_chunks_around(obs_x, obs_y, 2);

    // Run a few frames so chunks fully load
    for (int i = 0; i < 10; i++) {
        engine.update(dt);
        engine.render();
    }

    // Check: floor tiles should exist near the observer position
    auto particles_view = ps.lock_particles_for_read();
    int total = particles_view.size();
    int floor_near = 0;
    int floor_far = 0;
    int floor_at_origin = 0;
    float min_dist_to_obs = 9999.0f;

    for (int i = 0; i < total; i++) {
        const auto& p = particles_view[i];
        // Floor tiles are thin, wide, at z ≈ 0
        if (p.thickness < 0.2f && p.width >= 1.5f && p.z < 0.5f) {
            float dx = p.x - obs_x;
            float dy = p.y - obs_y;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < min_dist_to_obs) min_dist_to_obs = dist;

            if (dist < 35.0f) floor_near++;
            else floor_far++;

            // Check if tiles are at origin instead of observer
            float dist_origin = std::sqrt(p.x * p.x + p.y * p.y);
            if (dist_origin < 15.0f) floor_at_origin++;
        }
    }

    printf("\n  Total particles: %d\n", total);
    printf("  Floor tiles near observer (<35m): %d\n", floor_near);
    printf("  Floor tiles far from observer: %d\n", floor_far);
    printf("  Floor tiles near origin (bug): %d\n", floor_at_origin);
    printf("  Closest tile to observer: %.1fm\n", min_dist_to_obs);

    // Assertions
    bool has_floor = floor_near > 20;  // Should have many tiles near observer
    bool correct_position = min_dist_to_obs < 10.0f;  // Closest tile within 10m
    bool not_at_origin = (obs_x > 10.0f) ? (floor_at_origin < floor_near / 2) : true;

    printf("\n  %s: Floor tiles exist near observer (%d, threshold > 20)\n",
           has_floor ? "PASS" : "FAIL", floor_near);
    printf("  %s: Closest tile within 10m of observer (%.1fm)\n",
           correct_position ? "PASS" : "FAIL", min_dist_to_obs);
    printf("  %s: Tiles NOT clustered at origin (%d at origin vs %d near obs)\n",
           not_at_origin ? "PASS" : "FAIL", floor_at_origin, floor_near);

    bool pass = has_floor && correct_position && not_at_origin;
    printf("\n%s: Chunk floor streaming\n", pass ? "PASS" : "FAIL");

    // Disable floor gen to not affect other tests
    floor_gen.set_enabled(false);

    return pass;
}
