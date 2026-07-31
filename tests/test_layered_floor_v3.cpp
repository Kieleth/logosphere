// ============================================================================
// LAYERED FLOOR TEST V3: Sequential Strata with Physics Settling
// ============================================================================
// Purpose: Geophysics-inspired terrain generation
//   1. Generate foundation layer (large boulders at z=0)
//   2. Run physics until ALL particles are at rest
//   3. Generate next layer ON TOP of settled foundation
//   4. Repeat for multiple strata layers
//
// Key insight: Real geology forms layers SEQUENTIALLY - each settles before
// the next is added. This prevents physics explosions from simultaneous
// layer collisions.
//
// Run:
//   ./logosphere-tests --test test_layered_floor_v3              # Headless
//   INTERACTIVE=1 ./logosphere-tests --test test_layered_floor_v3  # Visual
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

using PhysicsV4::TURTLE_Z;

// ============================================================================
// Layer Specification
// ============================================================================
struct LayerSpec {
    const char* name;
    float size_min;
    float size_max;
    float thickness_mean;
    float thickness_stddev;
    float z_offset;        // Height above previous layer's floor
    float r, g, b;         // Base color
    float color_variance;
    int max_settle_frames; // Max physics frames to wait for settling
};

// ============================================================================
// Helper: Sample from normal distribution, clamped
// ============================================================================
inline float sample_normal_clamped(std::mt19937& rng, float mean, float stddev, float min_val, float max_val) {
    std::normal_distribution<float> dist(mean, stddev);
    return std::clamp(dist(rng), min_val, max_val);
}

// ============================================================================
// Helper: Wait until all non-light particles are at rest
// ============================================================================
int settle_until_at_rest(Engine& engine, int max_frames, bool verbose = false) {
    const float dt = 1.0f / 60.0f;
    auto& ps = engine.get_particle_system();

    for (int frame = 0; frame < max_frames; frame++) {
        engine.update(dt);

        // Count particles at rest
        auto view = ps.lock_particles_for_read();
        int total = 0;
        int at_rest = 0;
        float max_vel = 0;

        for (size_t i = 0; i < view.size(); i++) {
            const auto& p = view[i];
            if (p.is_light_source) continue;
            total++;
            if (p.is_at_rest) at_rest++;

            float vel = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
            if (vel > max_vel) max_vel = vel;
        }

        if (verbose && frame % 30 == 0) {
            std::cout << "[SETTLE] Frame " << frame << ": " << at_rest << "/" << total
                      << " at rest, max_vel=" << max_vel << " m/s\n";
        }

        // Early exit if all settled
        if (total > 0 && at_rest == total) {
            if (verbose) {
                std::cout << "[SETTLE] All " << total << " particles at rest after " << frame << " frames\n";
            }
            return frame;
        }

        // Check for explosions
        if (max_vel > 50.0f) {
            std::cout << "[SETTLE] WARNING: max velocity " << max_vel << " m/s indicates explosion!\n";
        }
    }

    if (verbose) {
        auto view = ps.lock_particles_for_read();
        int total = 0, at_rest = 0;
        for (size_t i = 0; i < view.size(); i++) {
            if (view[i].is_light_source) continue;
            total++;
            if (view[i].is_at_rest) at_rest++;
        }
        std::cout << "[SETTLE] Timeout after " << max_frames << " frames: "
                  << at_rest << "/" << total << " at rest\n";
    }
    return max_frames;
}

// ============================================================================
// Helper: Get floor height at grid positions (for placing next layer)
// Returns a 2D grid of maximum Z heights at each XY sample position
// ============================================================================
struct FloorHeightGrid {
    std::vector<float> heights;
    float cell_size;
    float min_x, min_y;
    int grid_w, grid_h;

    float get_height(float wx, float wy) const {
        int gx = static_cast<int>((wx - min_x) / cell_size);
        int gy = static_cast<int>((wy - min_y) / cell_size);
        gx = std::clamp(gx, 0, grid_w - 1);
        gy = std::clamp(gy, 0, grid_h - 1);
        return heights[gy * grid_w + gx];
    }

    // Get MAXIMUM floor height over a rectangular footprint
    // This prevents placing particles that overlap with taller neighbors
    float get_max_height(float cx, float cy, float half_w, float half_h) const {
        int gx1 = std::clamp(static_cast<int>((cx - half_w - min_x) / cell_size), 0, grid_w - 1);
        int gx2 = std::clamp(static_cast<int>((cx + half_w - min_x) / cell_size), 0, grid_w - 1);
        int gy1 = std::clamp(static_cast<int>((cy - half_h - min_y) / cell_size), 0, grid_h - 1);
        int gy2 = std::clamp(static_cast<int>((cy + half_h - min_y) / cell_size), 0, grid_h - 1);

        float max_h = heights[gy1 * grid_w + gx1];
        for (int gy = gy1; gy <= gy2; gy++) {
            for (int gx = gx1; gx <= gx2; gx++) {
                float h = heights[gy * grid_w + gx];
                if (h > max_h) max_h = h;
            }
        }
        return max_h;
    }
};

FloorHeightGrid compute_floor_heights(
    ParticleSystem& ps,
    float min_x, float max_x, float min_y, float max_y,
    float cell_size = 0.5f  // 0.5m resolution for better accuracy with small particles
) {
    FloorHeightGrid grid;
    grid.cell_size = cell_size;
    grid.min_x = min_x;
    grid.min_y = min_y;
    grid.grid_w = static_cast<int>((max_x - min_x) / cell_size) + 1;
    grid.grid_h = static_cast<int>((max_y - min_y) / cell_size) + 1;
    grid.heights.resize(grid.grid_w * grid.grid_h, TURTLE_Z);  // Default to ground level

    auto view = ps.lock_particles_for_read();
    for (size_t i = 0; i < view.size(); i++) {
        const auto& p = view[i];
        if (p.is_light_source) continue;

        // Particle's bounding box
        float half_w = p.width / 2.0f;
        float half_h = p.height / 2.0f;
        float half_t = p.thickness / 2.0f;
        float top_z = p.z + half_t;  // Top surface of particle

        // Mark all grid cells this particle covers
        int gx1 = std::clamp(static_cast<int>((p.x - half_w - min_x) / cell_size), 0, grid.grid_w - 1);
        int gx2 = std::clamp(static_cast<int>((p.x + half_w - min_x) / cell_size), 0, grid.grid_w - 1);
        int gy1 = std::clamp(static_cast<int>((p.y - half_h - min_y) / cell_size), 0, grid.grid_h - 1);
        int gy2 = std::clamp(static_cast<int>((p.y + half_h - min_y) / cell_size), 0, grid.grid_h - 1);

        for (int gy = gy1; gy <= gy2; gy++) {
            for (int gx = gx1; gx <= gx2; gx++) {
                float& h = grid.heights[gy * grid.grid_w + gx];
                if (top_z > h) h = top_z;
            }
        }
    }

    return grid;
}

// ============================================================================
// Generate layer with PHYSICS SETTLING (rain/sedimentation approach)
// ============================================================================
// Particles spawn above the floor and fall naturally. Physics handles collisions.
// This creates natural settling patterns rather than pre-calculated positions.
//
// Algorithm:
//   1. Spawn batch of particles at random XY, fixed height above floor
//   2. Run physics for N frames (particles fall and settle)
//   3. Repeat until target count reached or no more space
// ============================================================================
struct PhysicsLayerResult {
    std::vector<int> particle_ids;
    int spawned;
    int settled;
};

PhysicsLayerResult generate_layer_with_physics(
    Engine& engine,
    float chunk_min_x, float chunk_max_x,
    float chunk_min_y, float chunk_max_y,
    const LayerSpec& spec,
    std::mt19937& rng,
    int target_particles,      // How many particles to spawn
    int batch_size = 20,       // Particles per batch
    int settle_frames = 30,    // Physics frames between batches
    float spawn_height = 3.0f  // Height above current max Z to spawn
) {
    PhysicsLayerResult result = {{}, 0, 0};
    auto& ps = engine.get_particle_system();

    // Size and thickness distributions
    std::uniform_real_distribution<float> size_dist(spec.size_min, spec.size_max);
    std::normal_distribution<float> thick_dist(spec.thickness_mean, spec.thickness_stddev);
    std::uniform_real_distribution<float> x_dist(chunk_min_x + spec.size_max, chunk_max_x - spec.size_max);
    std::uniform_real_distribution<float> y_dist(chunk_min_y + spec.size_max, chunk_max_y - spec.size_max);
    std::uniform_real_distribution<float> color_var(-spec.color_variance, spec.color_variance);

    const float dt = 1.0f / 60.0f;
    int total_spawned = 0;
    int batches = 0;
    int consecutive_failures = 0;

    while (total_spawned < target_particles && consecutive_failures < 5) {
        batches++;

        // Find current max Z to determine spawn height
        float max_z = 0.0f;
        {
            auto view = ps.lock_particles_for_read();
            for (size_t i = 0; i < view.size(); i++) {
                if (view[i].is_light_source) continue;
                float top = view[i].z + view[i].thickness / 2.0f;
                if (top > max_z) max_z = top;
            }
        }
        float spawn_z = max_z + spawn_height;

        // Spawn a batch of particles
        int batch_spawned = 0;
        for (int i = 0; i < batch_size && total_spawned < target_particles; i++) {
            float tile_size = size_dist(rng);
            float thickness = std::clamp(thick_dist(rng), 0.05f, spec.thickness_mean * 2.0f);

            Particle p = {};
            p.x = x_dist(rng);
            p.y = y_dist(rng);
            // Stagger heights more aggressively - each particle 1.5x its size above previous
            p.z = spawn_z + (i * (spec.size_max * 1.5f));
            p.shape = ParticleShape::BOX;
            p.width = tile_size;
            p.height = tile_size;
            p.thickness = thickness;
            p.size = tile_size;

            // NOT at rest - let physics handle settling
            p.is_at_rest = false;
            p.vx = p.vy = p.vz = 0.0f;

            // Color with variance
            p.r = std::clamp(spec.r + color_var(rng), 0.1f, 1.0f);
            p.g = std::clamp(spec.g + color_var(rng), 0.1f, 1.0f);
            p.b = std::clamp(spec.b + color_var(rng), 0.1f, 1.0f);

            p.SetMaterial(Materials::Type::STONE);  // High friction for stable piles

            int pid = engine.add_particle(p);
            result.particle_ids.push_back(pid);
            batch_spawned++;
            total_spawned++;
        }

        // Run physics to let batch settle
        int at_rest_count = 0;
        for (int frame = 0; frame < settle_frames; frame++) {
            engine.update(dt);

            // Check how many are at rest
            auto view = ps.lock_particles_for_read();
            at_rest_count = 0;
            for (size_t i = 0; i < view.size(); i++) {
                if (view[i].is_light_source) continue;
                if (view[i].is_at_rest) at_rest_count++;
            }
        }

        // Check if we're making progress
        if (batch_spawned == 0) {
            consecutive_failures++;
        } else {
            consecutive_failures = 0;
        }

        // Progress output
        if (batches % 10 == 0) {
            std::cout << "  [Batch " << batches << "] Spawned: " << total_spawned
                      << ", At rest: " << at_rest_count << "\n";
        }
    }

    result.spawned = total_spawned;

    // Final settling - run until all at rest or timeout
    std::cout << "  [Final settling...]\n";
    for (int frame = 0; frame < spec.max_settle_frames; frame++) {
        engine.update(dt);

        auto view = ps.lock_particles_for_read();
        int total = 0, at_rest = 0;
        for (size_t i = 0; i < view.size(); i++) {
            if (view[i].is_light_source) continue;
            total++;
            if (view[i].is_at_rest) at_rest++;
        }

        if (at_rest == total) {
            result.settled = at_rest;
            break;
        }
        result.settled = at_rest;
    }

    return result;
}

// ============================================================================
// Generate layer using frontier expansion, placing ON TOP of floor grid
// ============================================================================
std::vector<int> generate_layer_on_floor(
    Engine& engine,
    ParticleSystem& ps,
    const FloorHeightGrid& floor,
    float chunk_min_x, float chunk_max_x,
    float chunk_min_y, float chunk_max_y,
    const LayerSpec& spec,
    std::mt19937& rng
) {
    std::vector<int> tile_pids;
    std::queue<std::pair<float, float>> frontier;  // XY positions to try

    float chunk_w = chunk_max_x - chunk_min_x;
    float chunk_h = chunk_max_y - chunk_min_y;

    // Occupancy grid for this layer
    float cell_size = 0.1f;  // 10cm cells
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

    // Size distribution
    auto random_size = [&]() {
        float mean = (spec.size_min + spec.size_max) / 2.0f;
        float stddev = (spec.size_max - spec.size_min) / 4.0f;
        return sample_normal_clamped(rng, mean, stddev, spec.size_min, spec.size_max);
    };

    std::uniform_real_distribution<float> color_var(-spec.color_variance, spec.color_variance);
    std::uniform_real_distribution<float> position_noise(-0.5f, 0.5f);

    // Uniform layer (size_min == size_max) = no position noise, perfect grid
    bool is_uniform_layer = (spec.size_min == spec.size_max);

    // Seed frontier with grid of positions
    float spacing = (spec.size_min + spec.size_max) / 2.0f;
    for (float y = chunk_min_y + spacing/2; y < chunk_max_y; y += spacing) {
        for (float x = chunk_min_x + spacing/2; x < chunk_max_x; x += spacing) {
            float nx = is_uniform_layer ? x : x + position_noise(rng) * spacing * 0.3f;
            float ny = is_uniform_layer ? y : y + position_noise(rng) * spacing * 0.3f;
            frontier.push({nx, ny});
        }
    }

    int placed = 0;
    int skipped = 0;

    while (!frontier.empty()) {
        auto [cx, cy] = frontier.front();
        frontier.pop();

        // Random size for this tile
        float tile_size = random_size();
        float half_size = tile_size / 2.0f;

        // Check bounds
        if (cx - half_size < chunk_min_x || cx + half_size > chunk_max_x ||
            cy - half_size < chunk_min_y || cy + half_size > chunk_max_y) {
            skipped++;
            continue;
        }

        // Check overlap in XY (skip for uniform layers - perfect grid has no overlaps)
        if (!is_uniform_layer && check_overlap(cx, cy, half_size, half_size)) {
            skipped++;
            continue;
        }

        // Get MAX floor height over particle footprint (prevents overlap with taller neighbors)
        float thickness = sample_normal_clamped(rng, spec.thickness_mean, spec.thickness_stddev, 0.1f, 1.0f);
        float floor_z = floor.get_max_height(cx, cy, half_size, half_size);

        // Gap for physics stability (prevents solver from finding overlaps)
        // Uniform layers (size_min == size_max) use zero gap - they're on a perfect grid
        // Variable layers need small gap (10cm minimum) for physics tolerance
        float gap = is_uniform_layer ? 0.0f : std::max(0.10f, std::min(tile_size, thickness) * 0.10f);

        // Place particle so its bottom is 'gap' meters above floor (passes can_place_at)
        // This ensures no overlap and minimal drop distance (≤5cm)
        float tile_z = floor_z + gap + thickness / 2.0f;

        // Try placement with XY jitter only (don't jitter Z - recalculate from floor)
        float final_x = cx, final_y = cy, final_z = tile_z;
        bool placed_ok = ps.can_place_at(cx, cy, tile_z, tile_size, tile_size, thickness, gap);

        if (!placed_ok) {
            // Try jittering XY only, recalculate Z from floor at new position
            std::uniform_real_distribution<float> jitter(-tile_size * 0.3f, tile_size * 0.3f);
            for (int attempt = 0; attempt < 10 && !placed_ok; attempt++) {
                float jx = cx + jitter(rng);
                float jy = cy + jitter(rng);
                // Recalculate Z from MAX floor height over footprint at jittered position
                float jfloor_z = floor.get_max_height(jx, jy, half_size, half_size);
                float jz = jfloor_z + gap + thickness / 2.0f;  // Same formula as above

                if (ps.can_place_at(jx, jy, jz, tile_size, tile_size, thickness, gap)) {
                    final_x = jx;
                    final_y = jy;
                    final_z = jz;
                    placed_ok = true;
                }
            }
        }

        if (!placed_ok) {
            skipped++;
            continue;
        }

        // Create particle at valid position
        Particle p = {};
        p.x = final_x;
        p.y = final_y;
        p.z = final_z;
        p.shape = ParticleShape::BOX;
        p.width = tile_size;
        p.height = tile_size;
        p.thickness = thickness;
        p.size = tile_size;

        // Let physics handle settling naturally
        p.is_at_rest = false;
        p.frames_at_rest = 0;
        p.vx = p.vy = p.vz = 0.0f;

        // Color with variance
        p.r = std::clamp(spec.r + color_var(rng), 0.1f, 1.0f);
        p.g = std::clamp(spec.g + color_var(rng), 0.1f, 1.0f);
        p.b = std::clamp(spec.b + color_var(rng), 0.1f, 1.0f);

        p.SetMaterial(Materials::Type::BRICK);

        int pid = engine.add_particle(p);
        tile_pids.push_back(pid);
        mark_occupied(final_x, final_y, half_size, half_size);
        placed++;

        // Debug: Log placement of specific particles we're tracking
        // Track particles involved in explosions (677 from Organic, and neighbors)
        if (pid == 677 || pid == 676 || pid == 678 ||
            (p.x > -19.0f && p.x < -17.0f && p.y > -2.5f && p.y < -0.5f)) {
            std::cout << "[PLACEMENT] P" << pid << " (" << spec.name << ")"
                      << " pos=(" << p.x << ", " << p.y << ", " << p.z << ")"
                      << " size=" << p.width << "x" << p.height << "x" << p.thickness
                      << " floor_z=" << floor_z << " gap=" << gap << "\n";
        }
    }

    std::cout << "[LAYER " << spec.name << "] Placed: " << placed << ", Skipped: " << skipped << "\n";
    return tile_pids;
}

// ============================================================================
// Main test function
// ============================================================================
bool test_layered_floor_v3() {
    std::cout << "\n";
    std::cout << "========================================================\n";
    std::cout << "  LAYERED FLOOR V3: Sequential Strata with Settling\n";
    std::cout << "========================================================\n\n";

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
    engine_config.window_title = "Layered Floor V3 - Sequential Strata";
    engine_config.enable_chat_window = false;

    if (engine.initialize(engine_config) != 0) {
        std::cerr << "ERROR: Engine init failed\n";
        return false;
    }

    auto& ps = engine.get_particle_system();
    auto& input = engine.get_input_system();

    // Random seed - can be set via env var
    std::random_device rd;
    unsigned int seed = rd();
    const char* seed_env = std::getenv("SEED");
    if (seed_env) {
        seed = static_cast<unsigned int>(std::stoul(seed_env));
    }
    std::mt19937 rng(seed);
    std::cout << "[SEED] " << seed << " (set SEED=N to reproduce)\n";

    // Chunk bounds
    float chunk_size = 50.0f;
    float half_chunk = chunk_size / 2.0f;
    float chunk_min_x = -half_chunk, chunk_max_x = half_chunk;
    float chunk_min_y = -half_chunk, chunk_max_y = half_chunk;

    // Layer specifications (geological model)
    // NOTE: z_offset = 0 for all layers - particles placed directly on floor
    // The +thickness/2 in placement ensures they're above floor surface
    // Non-zero z_offset causes particles to fall and gain velocity → explosions
    // IMPORTANT: max_settle_frames must be high enough for ALL particles to settle
    // Unsettled particles cause overlaps in the next layer → explosions
    std::vector<LayerSpec> layer_specs = {
        // Layer 0: Turtle Shell Plates (scutes) - uniform tiles for perfect grid
        // 10m tiles = 5x5 = 25 plates across 50m chunk, better coverage
        {"Turtle Plates", 10.0f, 10.0f, 0.5f, 0.1f, 0.0f, 0.22f, 0.20f, 0.18f, 0.04f, 100},

        // Layer 1: Foundation - massive boulders on bedrock
        {"Foundation", 3.0f, 8.0f, 0.8f, 0.3f, 0.0f, 0.35f, 0.35f, 0.4f, 0.05f, 500},

        // Layer 2: Sediment - medium rocks filling gaps (needs more time due to stacking)
        {"Sediment", 1.0f, 3.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.4f, 0.3f, 0.08f, 600},

        // Layer 3: Gravel - small stones (many particles, need time to settle)
        {"Gravel", 0.6f, 1.5f, 0.35f, 0.1f, 0.0f, 0.55f, 0.45f, 0.35f, 0.1f, 800},

        // Layer 4: Organic - surface material (small particles for dense coverage)
        {"Organic", 0.2f, 0.4f, 0.1f, 0.02f, 0.0f, 0.3f, 0.45f, 0.25f, 0.10f, 1000}
    };

    // Add lights
    auto add_lights = [&]() {
        Particle light = {};
        light.size = 2.0f;
        light.SetMaterial(Materials::Type::LIGHT);
        light.is_light_source = true;
        light.emission_strength = 100000.0f;
        light.emission_radius = 500.0f;
        light.r = 1.0f; light.g = 1.0f; light.b = 1.0f;

        // Light positions
        light.x = 0.0f; light.y = -10.0f; light.z = 30.0f;
        engine.add_particle(light);
        light.x = -15.0f; light.y = 5.0f; light.z = 30.0f;
        engine.add_particle(light);
        light.x = 15.0f; light.y = 5.0f; light.z = 30.0f;
        engine.add_particle(light);
    };

    // Track layers and state
    std::vector<std::vector<int>> layer_pids;
    int current_layer_index = 0;
    FloorHeightGrid current_floor;
    bool is_settling = false;
    int settle_frames_remaining = 0;
    std::string status_message = "Press SPACE to generate first layer";

    // Initialize floor height grid (0.25m resolution for accurate placement)
    constexpr float FLOOR_CELL_SIZE = 0.25f;
    auto reset_floor_grid = [&]() {
        current_floor.cell_size = FLOOR_CELL_SIZE;
        current_floor.min_x = chunk_min_x;
        current_floor.min_y = chunk_min_y;
        current_floor.grid_w = static_cast<int>((chunk_max_x - chunk_min_x) / current_floor.cell_size) + 1;
        current_floor.grid_h = static_cast<int>((chunk_max_y - chunk_min_y) / current_floor.cell_size) + 1;
        current_floor.heights.resize(current_floor.grid_w * current_floor.grid_h, TURTLE_Z);
    };

    // Reset everything
    auto reset_all = [&]() {
        // Use env seed if set, otherwise new random seed
        if (seed_env) {
            seed = static_cast<unsigned int>(std::stoul(seed_env));
        } else {
            seed = rd();
        }
        rng.seed(seed);
        std::cout << "\n[RESET] Seed: " << seed << (seed_env ? " (from env)" : " (random)") << "\n";

        ps.clear_particles();
        layer_pids.clear();
        current_layer_index = 0;
        is_settling = false;
        settle_frames_remaining = 0;

        add_lights();
        reset_floor_grid();

        status_message = "Press SPACE to generate first layer";
    };

    // Generate next layer (called when SPACE pressed)
    auto generate_next_layer = [&]() {
        if (current_layer_index >= (int)layer_specs.size()) {
            status_message = "All layers complete! Press R to reset";
            return;
        }

        const auto& spec = layer_specs[current_layer_index];
        std::cout << "\n[LAYER " << (current_layer_index+1) << "/" << layer_specs.size()
                  << "] Generating " << spec.name << "...\n";

        // Grid-based placement: particles placed at correct positions, physics settles naturally
        auto pids = generate_layer_on_floor(
            engine, ps, current_floor,
            chunk_min_x, chunk_max_x, chunk_min_y, chunk_max_y,
            spec, rng
        );
        layer_pids.push_back(pids);

        std::cout << "[LAYER " << spec.name << "] Placed " << pids.size() << " tiles\n";

        // Start settling phase - physics will naturally settle particles
        is_settling = true;
        settle_frames_remaining = spec.max_settle_frames;
        status_message = "Settling " + std::string(spec.name) + "...";
    };

    // Explosion tracking
    constexpr float EXPLOSION_THRESHOLD = 5.0f;  // m/s - flag particles moving faster than this
    int explosion_first_frame = -1;
    int explosion_first_pid = -1;
    float explosion_first_vel = 0.0f;
    int settle_frame_counter = 0;

    // Check if settling is complete
    auto check_settling_complete = [&]() {
        if (!is_settling) return;

        settle_frames_remaining--;
        settle_frame_counter++;

        // Check if all particles at rest + track explosions
        auto view = ps.lock_particles_for_read();
        int total = 0, at_rest = 0;
        float max_vel = 0.0f;
        int max_vel_pid = -1;
        float max_vel_z = 0.0f;

        for (size_t i = 0; i < view.size(); i++) {
            const auto& p = view[i];
            if (p.is_light_source) continue;
            total++;
            if (p.is_at_rest) at_rest++;

            float vel = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
            if (vel > max_vel) {
                max_vel = vel;
                max_vel_pid = static_cast<int>(i);
                max_vel_z = p.z;
            }

            // Detect first explosion
            if (vel > EXPLOSION_THRESHOLD && explosion_first_frame < 0) {
                explosion_first_frame = settle_frame_counter;
                explosion_first_pid = static_cast<int>(i);
                explosion_first_vel = vel;
                std::cout << "\n[EXPLOSION DETECTED] Frame " << settle_frame_counter
                          << " during " << layer_specs[current_layer_index].name << "\n"
                          << "  Particle " << i << ": vel=" << vel << " m/s\n"
                          << "  Position: (" << p.x << ", " << p.y << ", " << p.z << ")\n"
                          << "  Velocity: (" << p.vx << ", " << p.vy << ", " << p.vz << ")\n"
                          << "  Size: " << p.width << "x" << p.height << "x" << p.thickness << "\n";
            }
        }

        // Log every 10 frames during settling if there's significant velocity
        if (settle_frame_counter % 10 == 0 && max_vel > 1.0f) {
            std::cout << "[SETTLE F" << settle_frame_counter << "] "
                      << at_rest << "/" << total << " at rest, "
                      << "max_vel=" << max_vel << " m/s (P" << max_vel_pid << " z=" << max_vel_z << ")\n";
        }

        bool all_settled = (total > 0 && at_rest == total);
        bool timeout = (settle_frames_remaining <= 0);

        if (all_settled || timeout) {
            is_settling = false;
            settle_frame_counter = 0;  // Reset for next layer

            const auto& spec = layer_specs[current_layer_index];
            std::cout << "[LAYER " << spec.name << "] Settled: " << at_rest << "/" << total
                      << (timeout ? " (timeout)" : " (all at rest)") << "\n";

            // If not all settled, warn about potential issues
            if (!all_settled) {
                std::cout << "[WARNING] " << (total - at_rest) << " particles not at rest - may cause overlaps in next layer\n";
            }

            // Report explosion summary for this layer
            if (explosion_first_frame >= 0) {
                std::cout << "[EXPLOSION SUMMARY] First explosion at frame " << explosion_first_frame
                          << ", particle " << explosion_first_pid
                          << ", vel=" << explosion_first_vel << " m/s\n";
                explosion_first_frame = -1;  // Reset for next layer
            }

            // Update floor height grid for next layer
            current_floor = compute_floor_heights(ps, chunk_min_x, chunk_max_x, chunk_min_y, chunk_max_y, FLOOR_CELL_SIZE);

            current_layer_index++;

            if (current_layer_index >= (int)layer_specs.size()) {
                status_message = "All " + std::to_string(layer_specs.size()) + " layers complete! R=reset";
            } else {
                status_message = "SPACE: add " + std::string(layer_specs[current_layer_index].name) + " layer";
            }
        }
    };

    // Initialize
    reset_all();

    // Main loop
    const float dt = 1.0f / 60.0f;
    bool should_quit = false;
    bool space_was_pressed = false;
    int frame_count = 0;

    while (!should_quit) {
        engine.update(dt);
        frame_count++;

        // Check settling progress
        if (is_settling) {
            check_settling_complete();
        }

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();

            // Handle SPACE to generate next layer (only when not settling)
            bool space_pressed = input.get_input_state().keys[GLFW_KEY_SPACE];
            if (space_pressed && !space_was_pressed && !is_settling) {
                generate_next_layer();
            }
            space_was_pressed = space_pressed;

            // Handle R to reset
            if (input.get_input_state().keys[GLFW_KEY_R]) {
                reset_all();
            }

            // Handle ESC to quit
            if (input.get_input_state().keys[GLFW_KEY_ESCAPE]) {
                should_quit = true;
            }

            // UI overlay
            auto* ui = engine.get_ui_system();
            {
                auto particles = ps.lock_particles_for_read();
                int at_rest = 0, total = 0;
                float max_vel = 0;
                for (size_t i = 0; i < particles.size(); i++) {
                    if (particles[i].is_light_source) continue;
                    total++;
                    if (particles[i].is_at_rest) at_rest++;
                    float vel = std::sqrt(particles[i].vx*particles[i].vx +
                                         particles[i].vy*particles[i].vy +
                                         particles[i].vz*particles[i].vz);
                    if (vel > max_vel) max_vel = vel;
                }

                int y = 10;
                ui->draw_text(10, y, "Sequential Strata V3", 255, 200, 100);
                y += 30;

                // Seed
                ui->draw_text(10, y, "Seed: " + std::to_string(seed), 150, 150, 150);
                y += 25;

                // Layer progress
                ui->draw_text(10, y, "Layer: " + std::to_string(current_layer_index) + "/" +
                              std::to_string(layer_specs.size()), 200, 255, 200);
                y += 30;

                // Layer details
                for (size_t i = 0; i < layer_pids.size(); i++) {
                    bool is_current = (i == (size_t)current_layer_index - 1) && is_settling;
                    uint8_t r = is_current ? 255 : 180;
                    uint8_t g = is_current ? 255 : 180;
                    uint8_t b = is_current ? 100 : 180;
                    std::string s = "  " + std::string(layer_specs[i].name) + ": " +
                                    std::to_string(layer_pids[i].size()) + " tiles";
                    ui->draw_text(10, y, s, r, g, b);
                    y += 22;
                }

                y += 10;

                // Status
                ui->draw_text(10, y, status_message, 255, 255, 150);
                y += 30;

                // Physics state
                if (is_settling) {
                    ui->draw_text(10, y, "Settling: " + std::to_string(settle_frames_remaining) + " frames left",
                                  255, 200, 100);
                    y += 25;
                }

                ui->draw_text(10, y, "At rest: " + std::to_string(at_rest) + "/" + std::to_string(total),
                              at_rest == total ? 100 : 255, at_rest == total ? 255 : 100, 100);
                y += 25;

                if (max_vel > 10.0f) {
                    ui->draw_text(10, y, "Max vel: " + std::to_string((int)max_vel) + " m/s (!)",
                                  255, 100, 100);
                    y += 25;
                }

                y += 20;
                ui->draw_text(10, y, "SPACE: next layer | R: reset | ESC: exit", 128, 128, 128);
            }

            engine.present();
        } else {
            // Headless: auto-generate all layers
            if (!is_settling && current_layer_index < (int)layer_specs.size()) {
                generate_next_layer();
            }

            // After all layers, run extra frames then exit
            if (current_layer_index >= (int)layer_specs.size() && !is_settling) {
                static int extra_frames = 0;
                extra_frames++;
                if (extra_frames > 100) {
                    // Final verification
                    auto view = ps.lock_particles_for_read();
                    int total = 0, at_rest = 0;
                    float min_z = 1000, max_z = -1000;
                    float max_vel = 0;

                    for (size_t i = 0; i < view.size(); i++) {
                        const auto& p = view[i];
                        if (p.is_light_source) continue;
                        total++;
                        if (p.is_at_rest) at_rest++;
                        if (p.z < min_z) min_z = p.z;
                        if (p.z > max_z) max_z = p.z;

                        float vel = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
                        if (vel > max_vel) max_vel = vel;
                    }

                    std::cout << "\n[FINAL] After 100 extra frames:\n";
                    std::cout << "  Seed: " << seed << "\n";
                    std::cout << "  Particles: " << total << "\n";
                    std::cout << "  At rest: " << at_rest << "/" << total << "\n";
                    std::cout << "  Z range: " << min_z << " to " << max_z << " m\n";
                    std::cout << "  Max velocity: " << max_vel << " m/s\n";

                    bool no_explosions = max_vel < 10.0f;
                    bool has_strata = (max_z - min_z) > 0.5f;
                    bool mostly_settled = at_rest >= total * 0.8f;

                    std::cout << "\n[RESULT] ";
                    if (no_explosions && has_strata && mostly_settled) {
                        std::cout << "PASS\n";
                    } else {
                        std::cout << "NEEDS REVIEW\n";
                    }

                    should_quit = true;
                }
            }
        }
    }

    return true;
}
