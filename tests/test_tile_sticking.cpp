// ============================================================================
// TILE BOUNDARY STICKING TEST
// ============================================================================
// Uses entity telemetry to detect horizontal contact normals at tile
// boundaries. Eva walks across a row of floor tiles. If corner contact
// detection produces horizontal normals at tile edges, the telemetry
// records them and this test fails.
//
// TDD red: should FAIL with current physics, proving the tile sticking bug.
//
// Run:
//   ./logosphere-tests --test test_tile_sticking
// ============================================================================

#include "../src/core/engine.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "../src/core/entity_telemetry.h"
#include "logosphere/physics/narrow_phase.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "../src/test_context.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <unordered_set>

bool test_tile_sticking(TestContext& /* ctx */) {
    printf("\n=== Tile Boundary Sticking Test (Telemetry) ===\n");

    Engine engine;
    EngineConfig config;
    config.create_display = false;
    config.window_width = 800;
    config.window_height = 600;
    config.window_title = "Tile Sticking Test";
    config.enable_chat_window = false;

    if (engine.initialize(config) != 0) {
        printf("  ERROR: Engine init failed\n");
        return false;
    }

    auto& ps = engine.get_particle_system();
    const float dt = 1.0f / 60.0f;

    // ================================================================
    // Scene: 10 floor tiles in a row along Y axis (2m each, total 20m)
    // ================================================================
    std::vector<int> tile_ids;
    for (int i = 0; i < 10; i++) {
        Particle tile = {};
        tile.shape = ParticleShape::BOX;
        tile.x = 0.0f;
        tile.y = i * 2.0f;  // Tiles at y=0,2,4,...,18
        tile.z = 0.05f;
        tile.width = 4.0f;  // Wide enough so Eva stays on
        tile.height = 2.0f; // 2m along Y
        tile.thickness = 0.1f;
        tile.r = 0.3f; tile.g = 0.5f; tile.b = 0.2f; tile.a = 1.0f;
        tile.SetMaterial(Materials::Type::HEAVY_STATIC);
        tile.is_at_rest = true;
        int id = engine.add_particle(tile);
        tile_ids.push_back(id);
    }

    // Light
    ps.queue_light(0.0f, 10.0f, 8.0f, 300000.0f, 50.0f, 1.0f, 1.0f, 1.0f);

    printf("  Scene: 10 floor tiles (2m x 4m) along Y axis\n");

    // ================================================================
    // Create Eva at y=1 (center of first tile)
    // ================================================================
    auto& humanoid_gen = engine.get_worldgen_system().get_humanoid_generator();
    auto eva = humanoid_gen.generate_humanoid_physics(
        0.0f, 1.0f, 0.5f, -1, HumanoidSpec::eva(), false);

    engine.get_humanoid_locomotion().register_humanoid_direct(
        eva.hips_id,
        eva.left_leg_ids, eva.right_leg_ids,
        eva.left_arm_ids, eva.right_arm_ids,
        eva.torso_ids, 180.0f, 800.0f);

    printf("  Eva at (0, 1) on first tile, hips_id=%d\n", eva.hips_id);

    // ================================================================
    // Enable physics telemetry: track ALL Eva's particles
    // ================================================================
    auto& telemetry = engine.get_physics_system().get_telemetry();
    telemetry.set_enabled(true);
    for (unsigned int pid : eva.body_ids) {
        telemetry.track_particle(pid);
    }
    printf("  Tracking %zu particles for telemetry\n", eva.body_ids.size());
    printf("  Eva body IDs: hips=%d\n", eva.hips_id);
    printf("  Left leg: ");
    for (int id : eva.left_leg_ids) printf("%d ", id);
    printf("\n  Right leg: ");
    for (int id : eva.right_leg_ids) printf("%d ", id);
    printf("\n  Left arm: ");
    for (int id : eva.left_arm_ids) printf("%d ", id);
    printf("\n  Right arm: ");
    for (int id : eva.right_arm_ids) printf("%d ", id);
    printf("\n  Torso: ");
    for (int id : eva.torso_ids) printf("%d ", id);
    printf("\n");

    // Let physics settle (5 frames)
    for (int i = 0; i < 5; i++) { engine.update(dt); engine.render(); }

    // ================================================================
    // Walk Eva north (positive Y) across tile boundaries
    // ================================================================
    engine.get_humanoid_locomotion().set_target_velocity(eva.hips_id, 0.0f, 2.0f);
    engine.get_humanoid_locomotion().set_volitional(eva.hips_id, true);

    printf("  Walking north for 180 frames (3 seconds)...\n");

    for (int frame = 0; frame < 180; frame++) {
        engine.update(dt);
        engine.render();
    }

    // ================================================================
    // Query telemetry: aggregate contacts across ALL Eva's particles
    // ================================================================
    // Check hips buffer for frame count
    const auto* hips_buf = telemetry.query(eva.hips_id);
    if (!hips_buf || hips_buf->empty()) {
        printf("  ERROR: No telemetry for hips (id=%d)\n", eva.hips_id);
        return false;
    }
    printf("  Telemetry: %zu frames recorded per particle\n", hips_buf->size());

    // Check: scan ALL tracked particles for horizontal contacts with floor tiles
    int floor_horizontal = 0;
    int total_horizontal = 0;
    int sticking_frames = 0;

    std::unordered_set<unsigned int> tile_set(tile_ids.begin(), tile_ids.end());

    // Trace Z depth of P10 and P14 across time
    for (unsigned int trace_pid : {10u, 14u}) {
        const auto* tbuf = telemetry.query(trace_pid);
        if (!tbuf || tbuf->empty()) continue;
        printf("  Z-trace P%u (tile_top=0.1):\n", trace_pid);
        for (size_t fi = 0; fi < tbuf->size(); fi++) {
            const auto& f = tbuf->at(fi);
            float bottom = f.z - f.thickness * 0.5f;
            float pen = 0.1f - bottom;
            if (pen > 0.005f) printf("    f%u: z=%.4f bottom=%.4f pen=%.1fmm vz=%.3f\n",
                                     f.frame_number, f.z, bottom, pen*1000, f.vz);
        }
        printf("\n");
    }

    for (unsigned int pid : eva.body_ids) {
        const auto* buf = telemetry.query(pid);
        if (!buf) continue;

        auto h_frames = buf->find_horizontal_contact_frames();
        for (const auto* frame : h_frames) {
            for (const auto& c : frame->contacts) {
                if (c.is_horizontal) {
                    total_horizontal++;
                    // Check if either particle in the contact is a tile
                    if (tile_set.count(c.particle_a) || tile_set.count(c.particle_b)) {
                        floor_horizontal++;
                        if (floor_horizontal <= 5) {
                            printf("    HORIZONTAL FLOOR CONTACT f%d: P%u↔P%u normal=(%.2f,%.2f,%.2f) corner=%d pen=%.3f\n",
                                   frame->frame_number, c.particle_a, c.particle_b,
                                   c.normal_x, c.normal_y, c.normal_z,
                                   c.is_corner_contact, c.penetration);
                            // Diagnostic: show particle state at this frame
                            printf("      Eva particle P%u: pos=(%.4f,%.4f,%.4f) vel=(%.4f,%.4f,%.4f) dims=(%.4f,%.4f,%.4f)\n",
                                   pid, frame->x, frame->y, frame->z,
                                   frame->vx, frame->vy, frame->vz,
                                   frame->width, frame->height, frame->thickness);
                            // Show predicted Z (what the solver actually used)
                            float dt = 1.0f / 60.0f;
                            float z_pred = frame->z + frame->vz * dt;
                            float pred_bottom = z_pred - frame->thickness * 0.5f;
                            printf("      z_predicted=%.4f, pred_bottom=%.4f, tile_top=0.1000, pred_overlap_z=%.4f\n",
                                   z_pred, pred_bottom, 0.1f - pred_bottom);
                            // Find tile ID and show its position
                            unsigned int tile_id = tile_set.count(c.particle_a) ? c.particle_a : c.particle_b;
                            unsigned int eva_id = (tile_id == c.particle_a) ? c.particle_b : c.particle_a;
                            // Reconstruct AABBs from frame data
                            float hw = frame->width * 0.5f;
                            float hh = frame->height * 0.5f;
                            float ht = frame->thickness * 0.5f;
                            printf("      Eva AABB: x=[%.4f,%.4f] y=[%.4f,%.4f] z=[%.4f,%.4f]\n",
                                   frame->x - hw, frame->x + hw,
                                   frame->y - hh, frame->y + hh,
                                   frame->z - ht, frame->z + ht);
                            // Show tile info
                            auto particles = ps.lock_particles_for_read();
                            float tx = particles[tile_id].x;
                            float ty = particles[tile_id].y;
                            float tz = particles[tile_id].z;
                            float tw = particles[tile_id].width;
                            float th = particles[tile_id].height;
                            float tt = particles[tile_id].thickness;
                            printf("      Tile P%u: pos=(%.2f,%.2f,%.2f) dims=(%.2f,%.2f,%.2f)\n",
                                   tile_id, tx, ty, tz, tw, th, tt);
                            printf("      Tile AABB: x=[%.4f,%.4f] y=[%.4f,%.4f] z=[%.4f,%.4f]\n",
                                   tx - tw/2, tx + tw/2, ty - th/2, ty + th/2,
                                   tz - tt/2, tz + tt/2);
                            // Rerun narrow phase for this pair to see why Y was picked
                            AABB6 aabb_eva = {frame->x - hw, frame->x + hw,
                                              frame->y - hh, frame->y + hh,
                                              frame->z - ht, frame->z + ht};
                            AABB6 aabb_tile = {tx - tw/2, tx + tw/2,
                                               ty - th/2, ty + th/2,
                                               tz - tt/2, tz + tt/2};
                            float overlap_x = std::min(aabb_eva.max_x, aabb_tile.max_x) - std::max(aabb_eva.min_x, aabb_tile.min_x);
                            float overlap_y = std::min(aabb_eva.max_y, aabb_tile.max_y) - std::max(aabb_eva.min_y, aabb_tile.min_y);
                            float overlap_z = std::min(aabb_eva.max_z, aabb_tile.max_z) - std::max(aabb_eva.min_z, aabb_tile.min_z);
                            bool x_cont = (aabb_eva.min_x >= aabb_tile.min_x && aabb_eva.max_x <= aabb_tile.max_x) ||
                                          (aabb_tile.min_x >= aabb_eva.min_x && aabb_tile.max_x <= aabb_eva.max_x);
                            bool y_cont = (aabb_eva.min_y >= aabb_tile.min_y && aabb_eva.max_y <= aabb_tile.max_y) ||
                                          (aabb_tile.min_y >= aabb_eva.min_y && aabb_tile.max_y <= aabb_eva.max_y);
                            bool z_cont = (aabb_eva.min_z >= aabb_tile.min_z && aabb_eva.max_z <= aabb_tile.max_z) ||
                                          (aabb_tile.min_z >= aabb_eva.min_z && aabb_tile.max_z <= aabb_eva.max_z);
                            printf("      Overlaps: x=%.4f y=%.4f z=%.4f\n", overlap_x, overlap_y, overlap_z);
                            printf("      Contained: x=%d y=%d z=%d\n", x_cont, y_cont, z_cont);
                        }
                    }
                }
            }
        }

        // Check velocity: hips particle only
        if (pid == (unsigned)eva.hips_id) {
            auto slow = buf->find_slow_frames(0.3f);
            sticking_frames = (int)slow.size();
        }
    }

    printf("\n  Results:\n");
    printf("    Total horizontal contacts: %d\n", total_horizontal);
    printf("    Horizontal contacts with FLOOR TILES: %d\n", floor_horizontal);
    printf("    Sticking frames (hips v < 0.3): %d\n", sticking_frames);

    // Check final position from hips buffer
    float final_y = hips_buf->newest().y;
    float start_y = hips_buf->oldest().y;
    float distance = final_y - start_y;
    printf("    Distance walked: %.1fm (start_y=%.1f final_y=%.1f)\n",
           distance, start_y, final_y);

    // ================================================================
    // Assertions
    // ================================================================
    printf("\n  Assertions:\n");

    // 1. No horizontal contacts with floor tiles
    bool no_floor_horizontal = (floor_horizontal == 0);
    printf("    %s: No horizontal floor contacts (%d found)\n",
           no_floor_horizontal ? "PASS" : "FAIL", floor_horizontal);

    // 2. Eva walked at least 3m (didn't get stuck)
    bool moved = distance > 3.0f;
    printf("    %s: Eva walked > 3m (%.1fm)\n",
           moved ? "PASS" : "FAIL", distance);

    // 3. Few sticking frames
    bool few_sticks = sticking_frames < 30;
    printf("    %s: < 30 sticking frames (%d)\n",
           few_sticks ? "PASS" : "FAIL", sticking_frames);

    bool pass = no_floor_horizontal && moved && few_sticks;
    printf("\n%s: Tile boundary sticking\n", pass ? "PASS" : "FAIL");
    return pass;
}
