// ============================================================================
// LAYERED FLOOR TEST V2: Frontier-based organic floor generation
// ============================================================================
// Purpose: BFS expansion algorithm - tiles spawn neighbors on cardinal sides
//
// Run:
//   ./logosphere-tests --test test_layered_floor_v2              # Headless
//   INTERACTIVE=1 ./logosphere-tests --test test_layered_floor_v2  # Visual
//
// ============================================================================

#include "../src/core/engine.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/physics/physics_solver.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <cstdlib>
#include <vector>
#include <queue>
#include <random>
#include <map>
#include <string>

using PhysicsV4::TURTLE_Z;

// ============================================================================
// Frontier Algorithm Data Structures
// ============================================================================

struct FrontierSide {
    float x, y;           // Center of the parent tile's side
    int direction;        // 0=N(+Y), 1=E(+X), 2=S(-Y), 3=W(-X)
    float parent_size;    // Parent's size (for inheritance)
};

struct PlacedTile {
    float cx, cy;         // Center
    float half_w, half_h; // Half dimensions
};

// ============================================================================
// Helper: Sample from normal distribution, clamped
// ============================================================================
inline float sample_normal_clamped(std::mt19937& rng, float mean, float stddev, float min_val, float max_val) {
    std::normal_distribution<float> dist(mean, stddev);
    return std::clamp(dist(rng), min_val, max_val);
}

// ============================================================================
// Frontier Floor Generator V2 - Tight packing with probe-and-expand
// ============================================================================
std::vector<int> generate_frontier_floor(
    Engine& engine,
    ParticleSystem& ps,
    float chunk_min_x, float chunk_max_x,
    float chunk_min_y, float chunk_max_y,
    float base_z,
    std::mt19937& rng,
    std::vector<PlacedTile>& placed_tiles,  // IN/OUT: existing tiles + new tiles
    float seed_size = 3.0f,          // Starting size (0 = no seed, scan for gaps)
    float size_min = 0.3f,           // Minimum tile size
    float size_max = 15.0f,          // Maximum tile size
    float tile_gap = 0.06f,          // Gap between tiles
    float thickness_mean = 0.3f,
    float thickness_stddev = 0.2f
) {
    std::vector<int> tile_pids;
    std::queue<FrontierSide> frontier;

    float chunk_w = chunk_max_x - chunk_min_x;
    float chunk_h = chunk_max_y - chunk_min_y;
    float chunk_area = chunk_w * chunk_h;

    // Fine occupancy grid for precise collision detection
    float cell_size = 0.05f;  // 5cm cells for very tight packing
    int grid_w = static_cast<int>(chunk_w / cell_size) + 1;
    int grid_h = static_cast<int>(chunk_h / cell_size) + 1;
    std::vector<bool> occupied(grid_w * grid_h, false);

    auto world_to_grid = [&](float wx, float wy) -> std::pair<int, int> {
        int gx = static_cast<int>((wx - chunk_min_x) / cell_size);
        int gy = static_cast<int>((wy - chunk_min_y) / cell_size);
        return {std::clamp(gx, 0, grid_w - 1), std::clamp(gy, 0, grid_h - 1)};
    };

    auto mark_occupied = [&](float cx, float cy, float hw, float hh) {
        auto [gx1, gy1] = world_to_grid(cx - hw, cy - hh);
        auto [gx2, gy2] = world_to_grid(cx + hw, cy + hh);
        for (int gy = gy1; gy <= gy2; gy++) {
            for (int gx = gx1; gx <= gx2; gx++) {
                occupied[gy * grid_w + gx] = true;
            }
        }
    };

    auto check_overlap = [&](float cx, float cy, float hw, float hh) -> bool {
        auto [gx1, gy1] = world_to_grid(cx - hw, cy - hh);
        auto [gx2, gy2] = world_to_grid(cx + hw, cy + hh);
        for (int gy = gy1; gy <= gy2; gy++) {
            for (int gx = gx1; gx <= gx2; gx++) {
                if (occupied[gy * grid_w + gx]) return true;
            }
        }
        return false;
    };

    auto calc_coverage = [&]() -> float {
        float tile_area = 0.0f;
        for (const auto& t : placed_tiles) {
            tile_area += (t.half_w * 2) * (t.half_h * 2);
        }
        return tile_area / chunk_area;
    };

    // Initialize occupancy from existing placed_tiles (for strata layers)
    for (const auto& t : placed_tiles) {
        mark_occupied(t.cx, t.cy, t.half_w, t.half_h);
    }

    // Random size - normal distribution, clamped to min/max
    float size_mean = (seed_size > 0) ? seed_size : (size_min + size_max) / 2.0f;
    auto random_size = [&]() {
        return sample_normal_clamped(rng, size_mean, size_mean * 0.5f, size_min, size_max);
    };
    // Random offset along parent edge to break grid pattern
    std::uniform_real_distribution<float> edge_offset(-0.25f, 0.25f);  // ±25% of parent size (reduced for better coverage)

    // Debug counters and overlap checker (must be before add_tile)
    int placed_count = 0;
    int skipped_count = 0;
    int overlap_detected = 0;

    auto debug_check_overlap = [&](float cx, float cy, float hw, float hh, const char* context) {
        for (size_t i = 0; i < placed_tiles.size(); i++) {
            const auto& t = placed_tiles[i];
            // AABB overlap test
            bool overlap_x = std::abs(cx - t.cx) < (hw + t.half_w);
            bool overlap_y = std::abs(cy - t.cy) < (hh + t.half_h);
            if (overlap_x && overlap_y) {
                float gap_x = std::abs(cx - t.cx) - (hw + t.half_w);
                float gap_y = std::abs(cy - t.cy) - (hh + t.half_h);
                std::cout << "[OVERLAP " << context << " #" << i << "] New(" << cx << "," << cy
                          << " sz " << hw*2 << "x" << hh*2
                          << ") vs Existing(" << t.cx << "," << t.cy
                          << " sz " << t.half_w*2 << "x" << t.half_h*2
                          << ") gaps: " << gap_x << "," << gap_y << "\n";
                overlap_detected++;
                return true;
            }
        }
        return false;
    };

    auto add_tile = [&](float cx, float cy, float tile_size, int from_dir) -> bool {
        float half_size = tile_size / 2.0f;

        // Check bounds
        if (cx - half_size < chunk_min_x || cx + half_size > chunk_max_x ||
            cy - half_size < chunk_min_y || cy + half_size > chunk_max_y) {
            return false;
        }

        // Check overlap via grid
        if (check_overlap(cx, cy, half_size, half_size)) {
            return false;
        }

        // DEBUG: Double-check against placed_tiles list
        if (debug_check_overlap(cx, cy, half_size, half_size, "ADD_TILE")) {
            std::cout << "  BUG: Grid said FREE but placed_tiles shows OVERLAP!\n";
            return false;
        }

        // Create particle with thickness variation
        float thickness = sample_normal_clamped(rng, thickness_mean, thickness_stddev, 0.05f, 0.8f);
        float z = base_z + thickness / 2.0f;

        Particle p = {};
        p.x = cx;
        p.y = cy;
        p.z = z;
        p.shape = ParticleShape::BOX;
        p.width = tile_size;
        p.height = tile_size;
        p.thickness = thickness;
        p.size = tile_size;

        // Earth color with slight variation
        std::uniform_real_distribution<float> color_var(-0.05f, 0.05f);
        p.r = std::clamp(0.45f + color_var(rng), 0.3f, 0.6f);
        p.g = std::clamp(0.38f + color_var(rng), 0.25f, 0.5f);
        p.b = std::clamp(0.28f + color_var(rng), 0.15f, 0.4f);

        p.SetMaterial(Materials::Type::BRICK);

        int pid = engine.add_particle(p);
        tile_pids.push_back(pid);
        placed_tiles.push_back({cx, cy, half_size, half_size});
        mark_occupied(cx, cy, half_size, half_size);

        // DEBUG: Track every particle creation
        std::cout << "[ADD_TILE] pid=" << pid << " at (" << cx << "," << cy << "," << z
                  << ") size=" << tile_size << " total_now=" << ps.count() << "\n";

        // Add frontier sides (exclude the direction we came from) - multiple points per edge
        int back_dir = (from_dir + 2) % 4;
        float gap = tile_gap;
        float point_spacing = 1.0f;  // 1m spacing for good coverage

        for (int d = 0; d < 4; d++) {
            if (from_dir >= 0 && d == back_dir) continue;

            int num_points = std::max(1, (int)(tile_size / point_spacing));
            for (int i = 0; i < num_points; i++) {
                float t = (num_points == 1) ? 0.0f : (i / (float)(num_points - 1) - 0.5f);

                FrontierSide side;
                side.direction = d;
                side.parent_size = std::min(point_spacing * 2.0f, tile_size);

                if (d == 0) { side.x = cx + t * tile_size; side.y = cy + half_size + gap; }
                else if (d == 1) { side.x = cx + half_size + gap; side.y = cy + t * tile_size; }
                else if (d == 2) { side.x = cx + t * tile_size; side.y = cy - half_size - gap; }
                else { side.x = cx - half_size - gap; side.y = cy + t * tile_size; }

                frontier.push(side);
            }
        }

        return true;
    };

    if (seed_size > 0) {
        // First layer: place seed at center
        std::cout << "[FRONTIER] Seed: " << seed_size << "m at center\n";
        if (add_tile(0.0f, 0.0f, seed_size, -1)) {
            placed_count = 1;
        }
    } else {
        // Subsequent layers: add frontier points from edges of existing tiles
        std::cout << "[FRONTIER] No seed - using edges of " << placed_tiles.size() << " existing tiles\n";
        float gap = tile_gap;
        float point_spacing = 1.0f;
        for (const auto& t : placed_tiles) {
            for (int d = 0; d < 4; d++) {
                float edge_len = (d == 0 || d == 2) ? (t.half_w * 2) : (t.half_h * 2);
                int num_points = std::max(1, (int)(edge_len / point_spacing));
                for (int i = 0; i < num_points; i++) {
                    float frac = (num_points == 1) ? 0.0f : (i / (float)(num_points - 1) - 0.5f);
                    FrontierSide side;
                    side.direction = d;
                    side.parent_size = std::min(point_spacing * 2.0f, edge_len);
                    if (d == 0) { side.x = t.cx + frac * t.half_w * 2; side.y = t.cy + t.half_h + gap; }
                    else if (d == 1) { side.x = t.cx + t.half_w + gap; side.y = t.cy + frac * t.half_h * 2; }
                    else if (d == 2) { side.x = t.cx + frac * t.half_w * 2; side.y = t.cy - t.half_h - gap; }
                    else { side.x = t.cx - t.half_w - gap; side.y = t.cy + frac * t.half_h * 2; }
                    frontier.push(side);
                }
            }
        }
        std::cout << "[FRONTIER] Added " << frontier.size() << " frontier points from existing tiles\n";
    }

    // BFS expansion
    while (!frontier.empty()) {
        // No coverage check - run until frontier exhausted (no more tiles fit)

        FrontierSide side = frontier.front();
        frontier.pop();

        // Starting point with random offset along parent edge
        float offset = edge_offset(rng) * side.parent_size;
        float contact_x = side.x;
        float contact_y = side.y;

        // Apply offset perpendicular to spawn direction
        if (side.direction == 0 || side.direction == 2) contact_x += offset;  // N/S: offset in X
        else contact_y += offset;  // E/W: offset in Y

        // === PHASE 1: FIT - try random size, if fails try minimum ===
        float tile_size = random_size();
        float tile_cx = contact_x, tile_cy = contact_y;
        bool fits = false;
        bool needs_expand = false;

        // Helper to check if tile fits at given size
        auto try_fit = [&](float size) -> bool {
            float half = size / 2.0f;
            tile_cx = contact_x;
            tile_cy = contact_y;
            if (side.direction == 0) tile_cy = contact_y + half;
            else if (side.direction == 1) tile_cx = contact_x + half;
            else if (side.direction == 2) tile_cy = contact_y - half;
            else tile_cx = contact_x - half;

            // Check bounds
            if (tile_cx - half < chunk_min_x || tile_cx + half > chunk_max_x ||
                tile_cy - half < chunk_min_y || tile_cy + half > chunk_max_y) {
                return false;
            }
            // Check overlap
            return !check_overlap(tile_cx, tile_cy, half, half);
        };

        // Try random size first
        if (try_fit(tile_size)) {
            fits = true;
        } else {
            // Random didn't fit - try minimum size (1m)
            tile_size = 1.0f;
            if (try_fit(tile_size)) {
                fits = true;
                needs_expand = true;  // Will probe-and-expand
            }
        }

        if (!fits) {
            skipped_count++;
            continue;
        }

        // Final tile dimensions (may be expanded if we had to shrink)
        float half = tile_size / 2.0f;
        float tile_min_x = tile_cx - half;
        float tile_max_x = tile_cx + half;
        float tile_min_y = tile_cy - half;
        float tile_max_y = tile_cy + half;

        int back_dir = (side.direction + 2) % 4;

        // === PHASE 2: EXPAND - only if random didn't fit and we used minimum ===
        if (needs_expand) {
            // Probe distance in one direction - check ENTIRE front strip, not just discrete points
            auto probe_distance = [&](int dir, float edge_min, float edge_max, float edge_pos) -> float {
                float max_probe = size_max;
                for (float dist = cell_size; dist <= max_probe; dist += cell_size) {
                    // Check ALL grid cells along the front strip
                    bool blocked = false;

                    if (dir == 0) {  // North: check strip from edge_min to edge_max at y = edge_pos + dist
                        float test_y = edge_pos + dist;
                        if (test_y > chunk_max_y) return dist - cell_size;
                        auto [gx1, _] = world_to_grid(edge_min, test_y);
                        auto [gx2, gy] = world_to_grid(edge_max, test_y);
                        for (int gx = gx1; gx <= gx2 && !blocked; gx++) {
                            if (occupied[gy * grid_w + gx]) blocked = true;
                        }
                    } else if (dir == 1) {  // East: check strip at x = edge_pos + dist
                        float test_x = edge_pos + dist;
                        if (test_x > chunk_max_x) return dist - cell_size;
                        auto [gx, gy1] = world_to_grid(test_x, edge_min);
                        auto [_, gy2] = world_to_grid(test_x, edge_max);
                        for (int gy = gy1; gy <= gy2 && !blocked; gy++) {
                            if (occupied[gy * grid_w + gx]) blocked = true;
                        }
                    } else if (dir == 2) {  // South: check strip at y = edge_pos - dist
                        float test_y = edge_pos - dist;
                        if (test_y < chunk_min_y) return dist - cell_size;
                        auto [gx1, _] = world_to_grid(edge_min, test_y);
                        auto [gx2, gy] = world_to_grid(edge_max, test_y);
                        for (int gx = gx1; gx <= gx2 && !blocked; gx++) {
                            if (occupied[gy * grid_w + gx]) blocked = true;
                        }
                    } else {  // West: check strip at x = edge_pos - dist
                        float test_x = edge_pos - dist;
                        if (test_x < chunk_min_x) return dist - cell_size;
                        auto [gx, gy1] = world_to_grid(test_x, edge_min);
                        auto [_, gy2] = world_to_grid(test_x, edge_max);
                        for (int gy = gy1; gy <= gy2 && !blocked; gy++) {
                            if (occupied[gy * grid_w + gx]) blocked = true;
                        }
                    }

                    if (blocked) return dist - cell_size;
                }
                return max_probe;
            };

            // Iterative: probe all, pick closest, expand that dimension only
            // Stop when tile reaches size_max in both dimensions
            for (int iter = 0; iter < 100; iter++) {
                float cur_width = tile_max_x - tile_min_x;
                float cur_height = tile_max_y - tile_min_y;

                // Stop if already at max size in both dimensions
                if (cur_width >= size_max && cur_height >= size_max) break;

                // Only probe directions where we haven't reached max
                float dist_n = (back_dir != 0 && cur_height < size_max) ? probe_distance(0, tile_min_x, tile_max_x, tile_max_y) : 0;
                float dist_e = (back_dir != 1 && cur_width < size_max) ? probe_distance(1, tile_min_y, tile_max_y, tile_max_x) : 0;
                float dist_s = (back_dir != 2 && cur_height < size_max) ? probe_distance(2, tile_min_x, tile_max_x, tile_min_y) : 0;
                float dist_w = (back_dir != 3 && cur_width < size_max) ? probe_distance(3, tile_min_y, tile_max_y, tile_min_x) : 0;

                // Clamp distances so we don't exceed size_max
                float max_h_expand = size_max - cur_height;
                float max_w_expand = size_max - cur_width;
                dist_n = std::min(dist_n, max_h_expand);
                dist_s = std::min(dist_s, max_h_expand);
                dist_e = std::min(dist_e, max_w_expand);
                dist_w = std::min(dist_w, max_w_expand);

                float min_dist = size_max + 1;
                int min_dir = -1;
                if (dist_n > 0 && dist_n < min_dist) { min_dist = dist_n; min_dir = 0; }
                if (dist_e > 0 && dist_e < min_dist) { min_dist = dist_e; min_dir = 1; }
                if (dist_s > 0 && dist_s < min_dist) { min_dist = dist_s; min_dir = 2; }
                if (dist_w > 0 && dist_w < min_dist) { min_dist = dist_w; min_dir = 3; }

                if (min_dir < 0) break;

                if (min_dir == 0) tile_max_y += min_dist;
                else if (min_dir == 1) tile_max_x += min_dist;
                else if (min_dir == 2) tile_min_y -= min_dist;
                else tile_min_x -= min_dist;
            }
        }

        // Final dimensions
        float final_width = tile_max_x - tile_min_x;
        float final_height = tile_max_y - tile_min_y;
        tile_cx = (tile_min_x + tile_max_x) / 2.0f;
        tile_cy = (tile_min_y + tile_max_y) / 2.0f;

        // DEBUG: Check for overlap before creating particle
        float final_half_w = final_width / 2.0f;
        float final_half_h = final_height / 2.0f;
        if (debug_check_overlap(tile_cx, tile_cy, final_half_w, final_half_h, "BEFORE_CREATE")) {
            std::cout << "  Grid says: " << (check_overlap(tile_cx, tile_cy, final_half_w, final_half_h) ? "OCCUPIED" : "FREE") << "\n";
            skipped_count++;
            continue;  // Skip this tile - it overlaps!
        }

        // Create the particle
        float thickness = sample_normal_clamped(rng, thickness_mean, thickness_stddev, 0.05f, 0.8f);
        float z = base_z + thickness / 2.0f;

        Particle p = {};
        p.x = tile_cx;
        p.y = tile_cy;
        p.z = z;
        p.shape = ParticleShape::BOX;
        p.width = final_width;
        p.height = final_height;
        p.thickness = thickness;
        p.size = std::max(final_width, final_height);

        std::uniform_real_distribution<float> color_var(-0.05f, 0.05f);
        p.r = std::clamp(0.45f + color_var(rng), 0.3f, 0.6f);
        p.g = std::clamp(0.38f + color_var(rng), 0.25f, 0.5f);
        p.b = std::clamp(0.28f + color_var(rng), 0.15f, 0.4f);

        p.SetMaterial(Materials::Type::BRICK);

        int pid = engine.add_particle(p);
        tile_pids.push_back(pid);
        placed_tiles.push_back({tile_cx, tile_cy, final_width / 2.0f, final_height / 2.0f});
        mark_occupied(tile_cx, tile_cy, final_width / 2.0f, final_height / 2.0f);

        // DEBUG: Track every particle creation
        std::cout << "[BFS_TILE] pid=" << pid << " at (" << tile_cx << "," << tile_cy << "," << z
                  << ") size=" << final_width << "x" << final_height << " total_now=" << ps.count() << "\n";

        // Add frontier sides for new tile - multiple points along long edges
        float gap = tile_gap;
        float half_w = final_width / 2.0f;
        float half_h = final_height / 2.0f;
        float point_spacing = 1.0f;  // Add a frontier point every 1m for good coverage

        for (int d = 0; d < 4; d++) {
            if (d == back_dir) continue;

            // Determine edge length and orientation
            float edge_len = (d == 0 || d == 2) ? final_width : final_height;
            int num_points = std::max(1, (int)(edge_len / point_spacing));

            for (int p = 0; p < num_points; p++) {
                // Distribute points along edge
                float t = (num_points == 1) ? 0.0f : (p / (float)(num_points - 1) - 0.5f);

                FrontierSide new_side;
                new_side.direction = d;
                new_side.parent_size = std::min(point_spacing * 2.0f, edge_len);  // Local parent size

                if (d == 0) {  // North
                    new_side.x = tile_cx + t * final_width;
                    new_side.y = tile_cy + half_h + gap;
                } else if (d == 1) {  // East
                    new_side.x = tile_cx + half_w + gap;
                    new_side.y = tile_cy + t * final_height;
                } else if (d == 2) {  // South
                    new_side.x = tile_cx + t * final_width;
                    new_side.y = tile_cy - half_h - gap;
                } else {  // West
                    new_side.x = tile_cx - half_w - gap;
                    new_side.y = tile_cy + t * final_height;
                }

                frontier.push(new_side);
            }
        }

        placed_count++;
    }

    float final_coverage = calc_coverage();
    std::cout << "[FRONTIER] Frontier exhausted. Placed: " << placed_count
              << ", Skipped: " << skipped_count
              << ", Overlaps detected: " << overlap_detected
              << ", Final coverage: " << (final_coverage * 100.0f) << "%\n";

    if (overlap_detected > 0) {
        std::cout << "[INFO] " << overlap_detected << " overlap attempts PREVENTED (tiles skipped)\n";
    }

    return tile_pids;
}

// ============================================================================
// Main test function
// ============================================================================

bool test_layered_floor_v2() {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  LAYERED FLOOR V2: Frontier Expansion Algorithm              ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";

    // Interactive mode
    const char* interactive_env = std::getenv("INTERACTIVE");
    bool is_interactive = (interactive_env != nullptr && std::string(interactive_env) == "1");

    std::cout << "[MODE] " << (is_interactive ? "INTERACTIVE" : "HEADLESS") << std::endl;
    if (!is_interactive) {
        std::cout << "[MODE] Set INTERACTIVE=1 for visual mode" << std::endl;
    }

    // Engine setup
    Engine engine;
    EngineConfig engine_config;
    engine_config.create_display = is_interactive;
    engine_config.window_width = 1600;
    engine_config.window_height = 1200;
    engine_config.window_title = "Layered Floor V2 - Frontier";
    engine_config.enable_chat_window = false;

    if (engine.initialize(engine_config) != 0) {
        std::cerr << "ERROR: Engine init failed\n";
        return false;
    }

    auto& ps = engine.get_particle_system();
    auto& input = engine.get_input_system();

    // Random seed
    std::random_device rd;
    std::mt19937 rng(rd());

    // Chunk bounds
    float chunk_size = 50.0f;
    float half_chunk = chunk_size / 2.0f;
    float chunk_min_x = -half_chunk, chunk_max_x = half_chunk;
    float chunk_min_y = -half_chunk, chunk_max_y = half_chunk;
    float base_z = TURTLE_Z;

    // Strata parameters
    float size_min = 1.0f;
    float size_max = 12.0f;
    float thickness_mean = 0.3f;
    float thickness_stddev = 0.15f;

    // Scene state
    std::vector<PlacedTile> placed_tiles;
    std::vector<int> tile_pids;
    int current_layer = 0;
    std::vector<int> layer_counts;  // Tiles per layer
    float current_max_size = size_max;
    float size_reduction = 0.7f;  // Each layer is 70% of previous max size

    // Add lights helper
    auto add_lights = [&]() {
        Particle light = {};
        light.size = 2.0f;
        light.SetMaterial(Materials::Type::LIGHT);
        light.is_light_source = true;
        light.emission_strength = 100000.0f;
        light.emission_radius = 500.0f;
        light.r = 1.0f; light.g = 1.0f; light.b = 1.0f;

        // Light 1: center-south
        light.x = 0.0f; light.y = -10.0f; light.z = 25.0f;
        engine.add_particle(light);

        // Light 2: west
        light.x = -15.0f; light.y = 5.0f; light.z = 25.0f;
        engine.add_particle(light);

        // Light 3: east
        light.x = 15.0f; light.y = 5.0f; light.z = 25.0f;
        engine.add_particle(light);
    };

    // Generate base floor (Pass 1 with gaps + Pass 2 fills gaps)
    auto generate_base_floor = [&]() {
        ps.clear_particles();
        placed_tiles.clear();
        tile_pids.clear();
        layer_counts.clear();
        current_layer = 0;
        current_max_size = size_max;

        add_lights();

        // Base layer: Large tiles with gaps (gaps filled by subsequent layers at different Z)
        float pass1_gap = 2.0f;  // Larger gap leaves room for strata layers
        std::cout << "\n[FLOOR] Base layer: seed=4m, max=" << size_max << "m, gap=" << pass1_gap << "m\n";
        auto pass1_pids = generate_frontier_floor(
            engine, ps, chunk_min_x, chunk_max_x, chunk_min_y, chunk_max_y,
            base_z, rng, placed_tiles,
            4.0f, size_min, size_max, pass1_gap,
            thickness_mean, thickness_stddev
        );
        tile_pids.insert(tile_pids.end(), pass1_pids.begin(), pass1_pids.end());
        std::cout << "  Tiles: " << pass1_pids.size() << "\n";

        // NO Pass 2 - leave gaps for strata layers to fill at different Z heights

        layer_counts.push_back(tile_pids.size());
        current_layer = 1;
        current_max_size = size_max * size_reduction;
        std::cout << "  Layer 1 (base): " << layer_counts[0] << " tiles\n";
        std::cout << "  Total particles in system: " << ps.count() << "\n";
    };

    // Add next strata layer (higher Z, full coverage, smaller tiles)
    float layer_z_offset = 1.2f;  // Safe vertical gap (max thickness 0.8 + margin)
    auto add_next_layer = [&]() {
        float layer_z = base_z + current_layer * layer_z_offset;
        float layer_gap = 1.5f;  // Leave gaps for subsequent layers

        // SHARED occupancy: Use placed_tiles from outer scope so layers don't overlap in XY
        // This prevents physics explosions when upper layer tiles fall onto lower layer tiles
        // that occupy the same XY position. With shared occupancy, tiles only fill gaps.

        std::cout << "\n[STRATA] Layer " << (current_layer + 1) << ": z=" << layer_z
                  << "m, max=" << current_max_size << "m (" << (int)(100 * std::pow(size_reduction, current_layer)) << "% of original)\n";

        // Strata layers use SHARED placed_tiles - fill gaps left by previous layers
        // This prevents XY overlap between layers, avoiding physics explosions
        auto layer_pids = generate_frontier_floor(
            engine, ps, chunk_min_x, chunk_max_x, chunk_min_y, chunk_max_y,
            layer_z, rng, placed_tiles,  // SHARED occupancy
            0.0f, size_min, current_max_size, layer_gap,  // No seed - fill existing gaps
            thickness_mean * 0.7f, thickness_stddev * 0.4f
        );
        std::cout << "  Tiles: " << layer_pids.size() << "\n";

        int layer_total = layer_pids.size();
        tile_pids.insert(tile_pids.end(), layer_pids.begin(), layer_pids.end());
        layer_counts.push_back(layer_total);
        current_layer++;
        current_max_size *= size_reduction;

        std::cout << "  Layer " << current_layer << ": " << layer_total << " tiles at z=" << layer_z << "\n";
        std::cout << "  Total tiles: " << tile_pids.size() << "\n";
        std::cout << "  Total particles in system: " << ps.count() << "\n";

        // DEBUG: Check for physics issues - count particles by Z range
        {
            auto view = ps.lock_particles_for_read();
            std::map<int, int> z_buckets;  // z_bucket -> count
            for (size_t i = 0; i < view.size(); i++) {
                int z_bucket = (int)(view[i].z * 10);  // 0.1m buckets
                z_buckets[z_bucket]++;
            }
            std::cout << "  Z distribution: ";
            for (auto& [z, count] : z_buckets) {
                std::cout << (z/10.0f) << "m:" << count << " ";
            }
            std::cout << "\n";
        }
    };

    // Start with base floor
    generate_base_floor();

    // In headless mode, auto-add layers to test stacking
    if (!is_interactive) {
        std::cout << "\n[HEADLESS] Auto-adding layers to test stacking...\n";
        for (int i = 0; i < 4; i++) {
            add_next_layer();
        }
    }

    // Main loop
    const float dt = 1.0f / 60.0f;
    bool should_quit = false;
    bool space_was_pressed = false;

    std::cout << "\n[RUNNING] SPACE=regenerate, ESC=quit\n";

    while (!should_quit) {
        engine.update(dt);

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();

            // Handle SPACE to add layer, R to reset
            bool space_pressed = input.get_input_state().keys[GLFW_KEY_SPACE];
            bool r_pressed = input.get_input_state().keys[GLFW_KEY_R];
            if (space_pressed && !space_was_pressed) {
                add_next_layer();  // Add another strata layer
            }
            if (r_pressed) {
                generate_base_floor();  // Reset to base floor only
            }
            space_was_pressed = space_pressed;

            // Handle ESC to quit
            if (input.get_input_state().keys[GLFW_KEY_ESCAPE]) {
                should_quit = true;
            }

            auto* ui = engine.get_ui_system();
            {
                auto particles = ps.lock_particles_for_read();
                int at_rest_count = 0;
                for (int pid : tile_pids) {
                    if (pid < (int)particles.size() && particles[pid].is_at_rest) at_rest_count++;
                }

                ui->draw_text(10, 10, "Frontier Floor V2 - Strata", 255, 200, 100);

                // Layer info
                ui->draw_text(10, 40, "Layers: " + std::to_string(current_layer), 255, 255, 100);
                int y = 70;
                for (int i = 0; i < (int)layer_counts.size(); i++) {
                    int pct = (int)(100 * std::pow(size_reduction, i));
                    std::string layer_str = "  L" + std::to_string(i + 1) + ": " +
                                            std::to_string(layer_counts[i]) + " tiles (" + std::to_string(pct) + "%)";
                    ui->draw_text(10, y, layer_str, 180, 180, 180);
                    y += 25;
                }

                // Totals
                y += 10;
                ui->draw_text(10, y, "Total particles: " + std::to_string(particles.size()), 200, 255, 200);
                y += 30;
                ui->draw_text(10, y, "Tiles: " + std::to_string(tile_pids.size()), 200, 200, 255);
                y += 30;

                // At rest
                bool all_rest = (at_rest_count == (int)tile_pids.size());
                ui->draw_text(10, y, std::to_string(at_rest_count) + "/" + std::to_string(tile_pids.size()) + " at rest",
                              all_rest ? 100 : 255, all_rest ? 255 : 100, 100);
                y += 40;

                // Next layer info
                int next_pct = (int)(100 * std::pow(size_reduction, current_layer));
                ui->draw_text(10, y, "Next layer: " + std::to_string(next_pct) + "% size", 128, 200, 128);
                y += 30;
                ui->draw_text(10, y, "SPACE: add layer | R: reset | ESC: exit", 128, 128, 128);
            }
            engine.present();
        } else {
            // Headless mode: run longer and check for explosions
            static int headless_frames = 0;
            headless_frames++;

            // Check for explosions every 30 frames
            if (headless_frames % 30 == 0) {
                auto view = ps.lock_particles_for_read();
                int explosion_count = 0;
                float max_velocity = 0;
                int max_vel_pid = -1;

                for (size_t i = 0; i < view.size(); i++) {
                    const auto& p = view[i];
                    if (p.is_light_source) continue;

                    float vel = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
                    if (vel > max_velocity) {
                        max_velocity = vel;
                        max_vel_pid = static_cast<int>(i);
                    }
                    if (vel > 5.0f) {  // 5 m/s threshold for "explosion"
                        explosion_count++;
                    }
                }

                if (headless_frames % 60 == 0) {
                    // Z distribution at this frame
                    std::map<int, int> z_buckets;
                    for (size_t i = 0; i < view.size(); i++) {
                        if (view[i].is_light_source) continue;
                        int z_bucket = (int)(view[i].z * 10);  // 0.1m buckets
                        z_buckets[z_bucket]++;
                    }
                    std::cout << "[PHYSICS] Frame " << headless_frames
                              << ": max_vel=" << max_velocity
                              << " m/s (pid=" << max_vel_pid << ")"
                              << ", explosions=" << explosion_count;
                    // Summarize Z distribution (just show unique Z ranges)
                    std::cout << " | Z_ranges:";
                    int prev_bucket = -1000;
                    int range_count = 0;
                    for (auto& [z, count] : z_buckets) {
                        if (z != prev_bucket + 1) {
                            if (range_count > 0) std::cout << "]";
                            std::cout << " [" << (z/10.0f) << "m";
                            range_count = count;
                        } else {
                            range_count += count;
                        }
                        prev_bucket = z;
                    }
                    if (range_count > 0) std::cout << "]";
                    std::cout << "\n";
                }

                // Check for overlaps at frame 1 (before physics has done much)
                if (headless_frames == 30) {
                    std::cout << "[OVERLAP ANALYSIS] Checking ALL particle pairs for 3D overlap at frame 30:\n";
                    int total_overlaps = 0;
                    for (size_t i = 0; i < view.size() && total_overlaps < 10; i++) {
                        const auto& pi = view[i];
                        if (pi.is_light_source) continue;
                        for (size_t j = i+1; j < view.size(); j++) {
                            const auto& pj = view[j];
                            if (pj.is_light_source) continue;

                            float dx = std::abs(pi.x - pj.x);
                            float dy = std::abs(pi.y - pj.y);
                            float dz = std::abs(pi.z - pj.z);
                            float sum_hw = (pi.width + pj.width) / 2.0f;
                            float sum_hh = (pi.height + pj.height) / 2.0f;
                            float sum_ht = (pi.thickness + pj.thickness) / 2.0f;

                            bool overlap_x = dx < sum_hw;
                            bool overlap_y = dy < sum_hh;
                            bool overlap_z = dz < sum_ht;

                            if (overlap_x && overlap_y && overlap_z) {
                                float pen_x = sum_hw - dx;
                                float pen_y = sum_hh - dy;
                                float pen_z = sum_ht - dz;
                                std::cout << "  OVERLAP #" << total_overlaps << ":\n";
                                std::cout << "    pid " << i << ": pos=(" << pi.x << "," << pi.y << "," << pi.z
                                          << ") size=" << pi.width << "x" << pi.height << "x" << pi.thickness << "\n";
                                std::cout << "    pid " << j << ": pos=(" << pj.x << "," << pj.y << "," << pj.z
                                          << ") size=" << pj.width << "x" << pj.height << "x" << pj.thickness << "\n";
                                std::cout << "    distance: dx=" << dx << " dy=" << dy << " dz=" << dz << "\n";
                                std::cout << "    combined_half: hw=" << sum_hw << " hh=" << sum_hh << " ht=" << sum_ht << "\n";
                                std::cout << "    penetration: x=" << pen_x << " y=" << pen_y << " z=" << pen_z << "\n";
                                total_overlaps++;
                                if (total_overlaps >= 10) break;
                            }
                        }
                    }
                    std::cout << "[OVERLAP ANALYSIS] Found " << total_overlaps << " overlapping pairs\n";
                }

                if (explosion_count > 0 && headless_frames == 60) {
                    std::cout << "[WARNING] Detected " << explosion_count
                              << " particles with velocity > 5 m/s (explosion-like behavior)\n";

                    // Dump exploding particles
                    std::cout << "[EXPLOSION DETAILS] First frame with explosions:\n";
                    int shown = 0;
                    for (size_t i = 0; i < view.size() && shown < 5; i++) {
                        const auto& p = view[i];
                        if (p.is_light_source) continue;
                        float vel = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
                        if (vel > 5.0f) {
                            std::cout << "  pid=" << i << " pos=(" << p.x << "," << p.y << "," << p.z
                                      << ") vel=(" << p.vx << "," << p.vy << "," << p.vz
                                      << ") |v|=" << vel << " m/s"
                                      << " size=" << p.width << "x" << p.height << "x" << p.thickness << "\n";
                            shown++;
                        }
                    }

                    // Check for XY overlaps between exploding particles
                    std::cout << "[OVERLAP CHECK] Checking exploding particles for XY overlaps:\n";
                    for (size_t i = 0; i < view.size(); i++) {
                        const auto& pi = view[i];
                        if (pi.is_light_source) continue;
                        float vel_i = std::sqrt(pi.vx*pi.vx + pi.vy*pi.vy + pi.vz*pi.vz);
                        if (vel_i <= 50.0f) continue;  // Only check fast-moving particles

                        for (size_t j = i+1; j < view.size(); j++) {
                            const auto& pj = view[j];
                            if (pj.is_light_source) continue;

                            // Check if they overlap in XY (using half-widths)
                            float dx = std::abs(pi.x - pj.x);
                            float dy = std::abs(pi.y - pj.y);
                            float dz = std::abs(pi.z - pj.z);
                            float sum_hw = (pi.width + pj.width) / 2.0f;
                            float sum_hh = (pi.height + pj.height) / 2.0f;
                            float sum_ht = (pi.thickness + pj.thickness) / 2.0f;

                            bool overlap_x = dx < sum_hw;
                            bool overlap_y = dy < sum_hh;
                            bool overlap_z = dz < sum_ht;

                            if (overlap_x && overlap_y && overlap_z) {
                                std::cout << "  3D OVERLAP: pid " << i << " (" << pi.x << "," << pi.y << "," << pi.z
                                          << " z±" << pi.thickness/2 << ") vs pid " << j
                                          << " (" << pj.x << "," << pj.y << "," << pj.z
                                          << " z±" << pj.thickness/2 << ")"
                                          << " gaps: x=" << (dx - sum_hw)
                                          << " y=" << (dy - sum_hh)
                                          << " z=" << (dz - sum_ht) << "\n";
                            }
                        }
                    }
                }
            }

            if (headless_frames > 300) {  // 5 seconds of simulation
                should_quit = true;
            }
        }
    }

    std::cout << "\n[DONE] Layers: " << current_layer << ", Total tiles: " << tile_pids.size() << "\n";

    return true;
}
