// ============================================================================
// LAYERED FLOOR TEST V1: Organic, non-uniform tile floor generation
// ============================================================================
// Purpose: Test organic floor generation with varied tile sizes and gaps
//
// Run:
//   ./logosphere-tests --test test_layered_floor_v1              # Headless
//   INTERACTIVE=1 ./logosphere-tests --test test_layered_floor_v1  # Visual
//   PRESET=rocky INTERACTIVE=1 ./logosphere-tests --test test_layered_floor_v1
//   PRESET=sandy INTERACTIVE=1 ./logosphere-tests --test test_layered_floor_v1
//
// ============================================================================

#include "../src/core/engine.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/physics/physics_solver.h"
#include "logosphere/worldgen/organic_floor_generator.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <cstdlib>
#include <vector>
#include <random>
#include <string>

using PhysicsV4::TURTLE_Z;

// Wrappers for engine OrganicFloor:: functions (for backwards compatibility)
inline std::vector<int> generate_organic_floor_layer(
    ParticleSystem& ps, const OrganicFloorConfig& config, float base_z,
    std::mt19937& rng, std::vector<PlacedTile>& placed_tiles
) {
    return OrganicFloor::generate_layer(ps, config, base_z, rng, placed_tiles);
}

inline std::vector<int> fill_floor_gaps(
    ParticleSystem& ps, const OrganicFloorConfig& config, float base_z,
    std::mt19937& rng, std::vector<PlacedTile>& placed_tiles,
    float min_gap_size = -1.0f, float coverage_target = -1.0f
) {
    return OrganicFloor::fill_gaps(ps, config, base_z, rng, placed_tiles, min_gap_size, coverage_target);
}

inline std::vector<int> fill_floor_progressive(
    ParticleSystem& ps, const OrganicFloorConfig& config, float base_z,
    std::mt19937& rng, std::vector<PlacedTile>& placed_tiles,
    float phase1_coverage = 0.80f, float final_coverage = 0.95f, float scale_factor = 0.6f
) {
    return OrganicFloor::fill_gaps_progressive(ps, config, base_z, rng, placed_tiles,
                                                phase1_coverage, final_coverage, scale_factor);
}

inline std::vector<int> generate_strata_coverage(
    ParticleSystem& ps, const OrganicFloorConfig& config, float base_z,
    std::mt19937& rng, std::vector<PlacedTile>& placed_tiles,
    int max_slabs = 10, float coverage_target = 0.90f
) {
    return OrganicFloor::generate_strata_coverage(ps, config, base_z, rng, placed_tiles,
                                                   max_slabs, coverage_target);
}

// ============================================================================
// Main test function
// ============================================================================

bool test_layered_floor_v1() {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  LAYERED FLOOR V1: Organic Non-Uniform Tiles                 ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";

    // Interactive mode
    const char* interactive_env = std::getenv("INTERACTIVE");
    bool is_interactive = (interactive_env != nullptr && std::string(interactive_env) == "1");

    std::cout << "[MODE] " << (is_interactive ? "INTERACTIVE" : "HEADLESS") << std::endl;
    if (!is_interactive) {
        std::cout << "[MODE] Set INTERACTIVE=1 for visual mode" << std::endl;
    }

    // Preset selection
    OrganicFloorConfig config;
    const char* preset_env = std::getenv("PRESET");
    std::string preset_name = "earth";
    if (preset_env) {
        std::string preset(preset_env);
        if (preset == "rocky") {
            config = OrganicFloorConfig::rocky();
            preset_name = "rocky";
        } else if (preset == "sandy") {
            config = OrganicFloorConfig::sandy();
            preset_name = "sandy";
        } else {
            config = OrganicFloorConfig::earth();
            preset_name = "earth";
        }
    }
    std::cout << "[PRESET] " << preset_name << std::endl;

    // Test cases: SPACE switches between chunk sizes (interactive) or CASE=N (headless)
    // Case 1: 8m chunk (quick test)
    // Case 2: 50m chunk (standard)
    // Case 3: 50m chunk + 2 layers (layer 2 = 80% size)
    // Case 4: 50m chunk + 3 layers (layer 3 = 64% size)
    // Case 5: 50m strata - huge slabs only
    // Case 6: 50m strata - huge slabs + 80% slabs
    // Case 7: 50m strata - above + 64% slabs
    // Case 8: 50m strata - above + normal particles
    // Case 9: 50m strata - above + 80% normal particles
    // Case 10: 50m strata - L0 95% progressive fill
    // Case 11: OPTIMIZED - ~10 huge slabs (90%) + ~20 gap slabs (99%) + 2 cosmetic layers
    const int MAX_CASE = 14;
    int test_case = 1;

    // Environment variable to select case
    const char* case_env = std::getenv("CASE");
    if (case_env) {
        int requested = std::atoi(case_env);
        if (requested >= 1 && requested <= MAX_CASE) {
            test_case = requested;
            std::cout << "[CASE] Selected case " << test_case << " via CASE env var" << std::endl;
        }
    }
    float chunk_sizes[] = {8.0f, 50.0f, 50.0f, 50.0f, 50.0f, 50.0f, 50.0f, 50.0f, 50.0f, 50.0f, 50.0f, 50.0f, 50.0f, 50.0f};

    auto set_chunk_size = [&config](float size) {
        float half = size / 2.0f;
        config.chunk_min_x = -half;
        config.chunk_max_x = half;
        config.chunk_min_y = -half;
        config.chunk_max_y = half;
    };

    // Set initial chunk size
    set_chunk_size(chunk_sizes[test_case - 1]);

    // Engine setup
    Engine engine;
    EngineConfig engine_config;
    engine_config.create_display = is_interactive;
    engine_config.window_width = 1600;
    engine_config.window_height = 1200;
    engine_config.window_title = "Layered Floor V1";
    engine_config.enable_chat_window = false;

    if (engine.initialize(engine_config) != 0) {
        std::cerr << "ERROR: Engine init failed\n";
        return false;
    }

    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();

    physics.add_force(std::make_unique<GravityForce>(0.0f, 0.0f, -9.8f));

    std::cout << "[SETUP] TURTLE_Z = " << TURTLE_Z << "m\n";
    std::cout << "[SETUP] Gravity = -9.8 m/s²\n";
    std::cout << "[SETUP] Chunk: (" << config.chunk_min_x << "," << config.chunk_min_y
              << ") to (" << config.chunk_max_x << "," << config.chunk_max_y << ")\n\n";

    // Random number generator with fixed seed for reproducibility
    std::mt19937 rng(42);

    std::vector<int> tile_pids;
    std::vector<PlacedTile> placed_tiles;
    int light_id = -1;

    // Create scene for current test case
    auto create_scene = [&]() {
        // Reset RNG for reproducibility
        rng.seed(42);

        ps.clear_particles();
        physics.clear_gluons();
        tile_pids.clear();
        placed_tiles.clear();

        float current_chunk = chunk_sizes[test_case - 1];
        set_chunk_size(current_chunk);
        std::cout << "\n[CASE " << test_case << "/" << MAX_CASE << "] Chunk: "
                  << current_chunk << "m x " << current_chunk << "m\n";

        // Add light
        Particle light = {};
        light.x = 0.0f;
        light.y = -10.0f;
        light.z = 15.0f;
        light.size = 2.0f;
        light.SetMaterial(Materials::Type::LIGHT);
        light.is_light_source = true;
        light.emission_strength = 100000.0f;
        light.emission_radius = 500.0f;
        light.r = 1.0f; light.g = 1.0f; light.b = 1.0f;
        light_id = engine.add_particle(light);

        // Generate organic floor layer
        float base_z = 0.0f;
        std::cout << "[GENERATING] Single layer organic floor...\n";

        auto layer_pids = generate_organic_floor_layer(ps, config, base_z, rng, placed_tiles);
        for (int pid : layer_pids) {
            tile_pids.push_back(pid);
        }
        std::cout << "  Primary tiles: " << layer_pids.size() << "\n";

        // Gap filling
        std::cout << "[FILLING] Gaps...\n";
        auto fill_pids = fill_floor_gaps(ps, config, base_z, rng, placed_tiles);
        for (int pid : fill_pids) {
            tile_pids.push_back(pid);
        }
        std::cout << "  Fill tiles: " << fill_pids.size() << "\n";
        std::cout << "  Layer 1 tiles: " << tile_pids.size() << "\n";

        // Case 3 & 4: Add additional layers with progressively smaller tiles
        if (test_case >= 3 && test_case <= 4) {
            // Calculate avg thickness of first layer for Z offset
            float avg_thickness = (config.min_thickness + config.max_thickness) / 2.0f;
            float layer2_z = base_z + avg_thickness + 0.02f;  // Small gap above layer 1

            // Layer 2: 80% size
            OrganicFloorConfig layer2_config = config;
            layer2_config.min_width *= 0.8f;
            layer2_config.max_width *= 0.8f;
            layer2_config.min_length *= 0.8f;
            layer2_config.max_length *= 0.8f;
            layer2_config.min_thickness *= 0.8f;
            layer2_config.max_thickness *= 0.8f;

            std::cout << "[LAYER 2] Generating (80% size)...\n";
            std::vector<PlacedTile> layer2_placed;
            auto layer2_pids = generate_organic_floor_layer(ps, layer2_config, layer2_z, rng, layer2_placed);
            for (int pid : layer2_pids) {
                tile_pids.push_back(pid);
            }
            std::cout << "  Layer 2 tiles: " << layer2_pids.size() << "\n";

            // Gap fill layer 2
            auto layer2_fill = fill_floor_gaps(ps, layer2_config, layer2_z, rng, layer2_placed);
            for (int pid : layer2_fill) {
                tile_pids.push_back(pid);
            }
            std::cout << "  Layer 2 fill: " << layer2_fill.size() << "\n";

            // Case 4: Add third layer
            if (test_case == 4) {
                float avg_thickness2 = (layer2_config.min_thickness + layer2_config.max_thickness) / 2.0f;
                float layer3_z = layer2_z + avg_thickness2 + 0.02f;

                // Layer 3: 64% size (80% of 80%)
                OrganicFloorConfig layer3_config = layer2_config;
                layer3_config.min_width *= 0.8f;
                layer3_config.max_width *= 0.8f;
                layer3_config.min_length *= 0.8f;
                layer3_config.max_length *= 0.8f;
                layer3_config.min_thickness *= 0.8f;
                layer3_config.max_thickness *= 0.8f;

                std::cout << "[LAYER 3] Generating (64% size)...\n";
                std::vector<PlacedTile> layer3_placed;
                auto layer3_pids = generate_organic_floor_layer(ps, layer3_config, layer3_z, rng, layer3_placed);
                for (int pid : layer3_pids) {
                    tile_pids.push_back(pid);
                }
                std::cout << "  Layer 3 tiles: " << layer3_pids.size() << "\n";

                // Gap fill layer 3
                auto layer3_fill = fill_floor_gaps(ps, layer3_config, layer3_z, rng, layer3_placed);
                for (int pid : layer3_fill) {
                    tile_pids.push_back(pid);
                }
                std::cout << "  Layer 3 fill: " << layer3_fill.size() << "\n";
            }
        }

        // Cases 5-8: Strata approach - huge slabs with layered smaller particles
        if (test_case >= 5) {
            // Clear previous particles for strata cases
            ps.clear_particles();
            tile_pids.clear();
            placed_tiles.clear();

            // Re-add light
            Particle light = {};
            light.x = 0.0f;
            light.y = -10.0f;
            light.z = 15.0f;
            light.size = 2.0f;
            light.SetMaterial(Materials::Type::LIGHT);
            light.is_light_source = true;
            light.emission_strength = 100000.0f;
            light.emission_radius = 500.0f;
            light.r = 1.0f; light.g = 1.0f; light.b = 1.0f;
            light_id = engine.add_particle(light);

            // Layer 0: Huge flat slabs (strata base)
            OrganicFloorConfig strata0 = OrganicFloorConfig::strata_base();
            strata0.chunk_min_x = config.chunk_min_x;
            strata0.chunk_max_x = config.chunk_max_x;
            strata0.chunk_min_y = config.chunk_min_y;
            strata0.chunk_max_y = config.chunk_max_y;

            std::cout << "[STRATA L0] Generating huge slabs (3-8m, 0.2-1m thick)...\n";
            std::vector<PlacedTile> strata0_placed;
            auto strata0_pids = generate_organic_floor_layer(ps, strata0, base_z, rng, strata0_placed);
            for (int pid : strata0_pids) tile_pids.push_back(pid);
            std::cout << "  Layer 0 slabs: " << strata0_pids.size() << "\n";

            // Fill gaps larger than average slab size
            auto strata0_fill = fill_floor_gaps(ps, strata0, base_z, rng, strata0_placed);
            for (int pid : strata0_fill) tile_pids.push_back(pid);
            std::cout << "  Layer 0 fill: " << strata0_fill.size() << "\n";

            float current_z = base_z;

            // Case 6+: Add layer 1 (80% strata slabs)
            if (test_case >= 6) {
                current_z += strata0.max_thickness + 0.05f;  // Use max for clearance

                OrganicFloorConfig strata1 = strata0;
                strata1.min_width *= 0.8f;
                strata1.max_width *= 0.8f;
                strata1.min_length *= 0.8f;
                strata1.max_length *= 0.8f;
                strata1.min_thickness *= 0.8f;
                strata1.max_thickness *= 0.8f;
                strata1.base_r += 0.05f;  // Slightly lighter

                std::cout << "[STRATA L1] Generating 80% slabs...\n";
                std::vector<PlacedTile> strata1_placed;
                auto strata1_pids = generate_organic_floor_layer(ps, strata1, current_z, rng, strata1_placed);
                for (int pid : strata1_pids) tile_pids.push_back(pid);
                std::cout << "  Layer 1 slabs: " << strata1_pids.size() << "\n";

                // Fill layer 1 gaps - target 70% coverage, fill gaps > 1m
                auto strata1_fill = fill_floor_gaps(ps, strata1, current_z, rng, strata1_placed, 1.0f, 0.70f);
                for (int pid : strata1_fill) tile_pids.push_back(pid);
                std::cout << "  Layer 1 fill: " << strata1_fill.size() << "\n";

                // Case 7+: Add 64% slabs (80% of 80%)
                if (test_case >= 7) {
                    current_z += strata1.max_thickness + 0.05f;

                    OrganicFloorConfig strata2 = strata1;
                    strata2.min_width *= 0.8f;
                    strata2.max_width *= 0.8f;
                    strata2.min_length *= 0.8f;
                    strata2.max_length *= 0.8f;
                    strata2.min_thickness *= 0.8f;
                    strata2.max_thickness *= 0.8f;
                    strata2.base_r += 0.05f;  // Slightly lighter

                    std::cout << "[STRATA L2] Generating 64% slabs...\n";
                    std::vector<PlacedTile> strata2_placed;
                    auto strata2_pids = generate_organic_floor_layer(ps, strata2, current_z, rng, strata2_placed);
                    for (int pid : strata2_pids) tile_pids.push_back(pid);
                    std::cout << "  Layer 2 slabs: " << strata2_pids.size() << "\n";

                    // Fill layer 2 gaps - target 80% coverage, fill gaps > 0.5m
                    auto strata2_fill = fill_floor_gaps(ps, strata2, current_z, rng, strata2_placed, 0.5f, 0.80f);
                    for (int pid : strata2_fill) tile_pids.push_back(pid);
                    std::cout << "  Layer 2 fill: " << strata2_fill.size() << "\n";

                    // Case 8+: Add normal particles (earth config)
                    if (test_case >= 8) {
                        current_z += strata2.max_thickness + 0.05f;

                        OrganicFloorConfig normal = OrganicFloorConfig::earth();
                        normal.chunk_min_x = config.chunk_min_x;
                        normal.chunk_max_x = config.chunk_max_x;
                        normal.chunk_min_y = config.chunk_min_y;
                        normal.chunk_max_y = config.chunk_max_y;

                        std::cout << "[STRATA L3] Generating normal particles...\n";
                        std::vector<PlacedTile> normal_placed;
                        auto normal_pids = generate_organic_floor_layer(ps, normal, current_z, rng, normal_placed);
                        for (int pid : normal_pids) tile_pids.push_back(pid);
                        std::cout << "  Layer 3 normal: " << normal_pids.size() << "\n";

                        auto normal_fill = fill_floor_gaps(ps, normal, current_z, rng, normal_placed);
                        for (int pid : normal_fill) tile_pids.push_back(pid);
                        std::cout << "  Layer 3 fill: " << normal_fill.size() << "\n";

                        // Case 9: Add 80% normal particles
                        if (test_case >= 9) {
                            current_z += normal.max_thickness + 0.02f;

                            OrganicFloorConfig small = normal;
                            small.min_width *= 0.8f;
                            small.max_width *= 0.8f;
                            small.min_length *= 0.8f;
                            small.max_length *= 0.8f;
                            small.min_thickness *= 0.8f;
                            small.max_thickness *= 0.8f;

                            std::cout << "[STRATA L4] Generating 80% normal particles...\n";
                            std::vector<PlacedTile> small_placed;
                            auto small_pids = generate_organic_floor_layer(ps, small, current_z, rng, small_placed);
                            for (int pid : small_pids) tile_pids.push_back(pid);
                            std::cout << "  Layer 4 small: " << small_pids.size() << "\n";

                            auto small_fill = fill_floor_gaps(ps, small, current_z, rng, small_placed);
                            for (int pid : small_fill) tile_pids.push_back(pid);
                            std::cout << "  Layer 4 fill: " << small_fill.size() << "\n";
                        }
                    }
                }
            }

            std::cout << "  Total strata tiles: " << tile_pids.size() << "\n";
        }

        // Case 10: Progressive fill strata (L0: 95% with diminishing, L1-L2: 90%, L3: 60%)
        if (test_case == 10) {
            // Clear and rebuild with progressive fill
            ps.clear_particles();
            tile_pids.clear();
            placed_tiles.clear();

            // Re-add light
            Particle light = {};
            light.x = 0.0f; light.y = -10.0f; light.z = 15.0f;
            light.size = 2.0f;
            light.SetMaterial(Materials::Type::LIGHT);
            light.is_light_source = true;
            light.emission_strength = 100000.0f;
            light.emission_radius = 500.0f;
            light.r = 1.0f; light.g = 1.0f; light.b = 1.0f;
            light_id = engine.add_particle(light);

            float current_z = base_z;

            // Layer 0: Huge slabs with 95% progressive fill (80% huge + 20% diminishing)
            OrganicFloorConfig strata0 = OrganicFloorConfig::strata_base();
            strata0.chunk_min_x = config.chunk_min_x;
            strata0.chunk_max_x = config.chunk_max_x;
            strata0.chunk_min_y = config.chunk_min_y;
            strata0.chunk_max_y = config.chunk_max_y;

            std::cout << "[CASE 10 L0] Huge slabs + 95% progressive fill (80% huge, 20% diminishing)...\n";
            std::vector<PlacedTile> l0_placed;
            auto l0_pids = generate_organic_floor_layer(ps, strata0, current_z, rng, l0_placed);
            for (int pid : l0_pids) tile_pids.push_back(pid);
            // Progressive fill: 80% with huge slabs, then diminishing to 95%
            auto l0_fill = fill_floor_progressive(ps, strata0, current_z, rng, l0_placed, 0.80f, 0.95f, 0.6f);
            for (int pid : l0_fill) tile_pids.push_back(pid);
            std::cout << "  L0: " << l0_pids.size() << " + " << l0_fill.size() << " fill\n";

            // Layer 1: 80% slabs with 90% fill
            current_z += strata0.max_thickness + 0.05f;
            OrganicFloorConfig strata1 = strata0;
            strata1.min_width *= 0.8f; strata1.max_width *= 0.8f;
            strata1.min_length *= 0.8f; strata1.max_length *= 0.8f;
            strata1.min_thickness *= 0.8f; strata1.max_thickness *= 0.8f;
            strata1.base_r += 0.05f;

            std::cout << "[CASE 10 L1] 80% slabs + 90% fill...\n";
            std::vector<PlacedTile> l1_placed;
            auto l1_pids = generate_organic_floor_layer(ps, strata1, current_z, rng, l1_placed);
            for (int pid : l1_pids) tile_pids.push_back(pid);
            auto l1_fill = fill_floor_gaps(ps, strata1, current_z, rng, l1_placed, 0.5f, 0.90f);
            for (int pid : l1_fill) tile_pids.push_back(pid);
            std::cout << "  L1: " << l1_pids.size() << " + " << l1_fill.size() << " fill\n";

            // Layer 2: 64% slabs with 90% fill
            current_z += strata1.max_thickness + 0.05f;
            OrganicFloorConfig strata2 = strata1;
            strata2.min_width *= 0.8f; strata2.max_width *= 0.8f;
            strata2.min_length *= 0.8f; strata2.max_length *= 0.8f;
            strata2.min_thickness *= 0.8f; strata2.max_thickness *= 0.8f;
            strata2.base_r += 0.05f;

            std::cout << "[CASE 10 L2] 64% slabs + 90% fill...\n";
            std::vector<PlacedTile> l2_placed;
            auto l2_pids = generate_organic_floor_layer(ps, strata2, current_z, rng, l2_placed);
            for (int pid : l2_pids) tile_pids.push_back(pid);
            auto l2_fill = fill_floor_gaps(ps, strata2, current_z, rng, l2_placed, 0.3f, 0.90f);
            for (int pid : l2_fill) tile_pids.push_back(pid);
            std::cout << "  L2: " << l2_pids.size() << " + " << l2_fill.size() << " fill\n";

            // Layer 3: Normal particles with 60% fill (less dense top layer)
            current_z += strata2.max_thickness + 0.05f;
            OrganicFloorConfig normal = OrganicFloorConfig::earth();
            normal.chunk_min_x = config.chunk_min_x;
            normal.chunk_max_x = config.chunk_max_x;
            normal.chunk_min_y = config.chunk_min_y;
            normal.chunk_max_y = config.chunk_max_y;

            std::cout << "[CASE 10 L3] Normal + 60% fill...\n";
            std::vector<PlacedTile> l3_placed;
            auto l3_pids = generate_organic_floor_layer(ps, normal, current_z, rng, l3_placed);
            for (int pid : l3_pids) tile_pids.push_back(pid);
            auto l3_fill = fill_floor_gaps(ps, normal, current_z, rng, l3_placed, 0.2f, 0.60f);
            for (int pid : l3_fill) tile_pids.push_back(pid);
            std::cout << "  L3: " << l3_pids.size() << " + " << l3_fill.size() << " fill\n";

            std::cout << "  Total case 10 tiles: " << tile_pids.size() << "\n";
        }

        // Case 11: OPTIMIZED - Few particles, maximum coverage
        // L0: ~10 huge slabs → 80-90% coverage
        // L1: ~20 slabs in L0 gaps → 99% combined
        // L2-L3: Cosmetic detail layers on top
        if (test_case == 11) {
            ps.clear_particles();
            tile_pids.clear();
            placed_tiles.clear();

            // Re-add light
            Particle light = {};
            light.x = 0.0f; light.y = -10.0f; light.z = 15.0f;
            light.size = 2.0f;
            light.SetMaterial(Materials::Type::LIGHT);
            light.is_light_source = true;
            light.emission_strength = 100000.0f;
            light.emission_radius = 500.0f;
            light.r = 1.0f; light.g = 1.0f; light.b = 1.0f;
            light_id = engine.add_particle(light);

            float current_z = base_z;

            // L0: Huge slabs (12-20m for 50m chunk) → target 80%
            // Math: 80% of 2500m² = 2000m², need ~10 slabs of 200m² = 14m x 14m avg
            OrganicFloorConfig huge = OrganicFloorConfig::strata_base();
            huge.min_width = 12.0f;  huge.max_width = 20.0f;
            huge.min_length = 12.0f; huge.max_length = 20.0f;
            huge.min_thickness = 0.3f; huge.max_thickness = 1.2f;
            huge.chunk_min_x = config.chunk_min_x;
            huge.chunk_max_x = config.chunk_max_x;
            huge.chunk_min_y = config.chunk_min_y;
            huge.chunk_max_y = config.chunk_max_y;

            std::cout << "[CASE 11 L0] Strategic huge slabs (12-20m) → 80%...\n";
            std::vector<PlacedTile> base_placed;
            auto l0_pids = generate_strata_coverage(ps, huge, current_z, rng, base_placed, 10, 0.80f);
            for (int pid : l0_pids) tile_pids.push_back(pid);
            std::cout << "  L0: " << l0_pids.size() << " slabs\n";

            // L1a: Fill gaps with medium slabs (5-12m) - SAME Z as L0
            // Thickness scaled to match L0's avg thickness for visual consistency
            OrganicFloorConfig gap_med = huge;
            gap_med.min_width = 5.0f;  gap_med.max_width = 12.0f;
            gap_med.min_length = 5.0f; gap_med.max_length = 12.0f;
            gap_med.min_thickness = 0.4f; gap_med.max_thickness = 0.9f;  // Mid-range to match L0
            gap_med.base_r += 0.03f;

            std::cout << "[CASE 11 L1a] Medium gap slabs (5-12m) at base Z...\n";
            auto l1a_pids = generate_strata_coverage(ps, gap_med, base_z, rng, base_placed, 20, 0.95f);
            for (int pid : l1a_pids) tile_pids.push_back(pid);
            std::cout << "  L1a: " << l1a_pids.size() << " slabs\n";

            // L1b: Fill remaining gaps with small slabs (2-5m) - SAME Z
            OrganicFloorConfig gap_small = huge;
            gap_small.min_width = 2.0f;  gap_small.max_width = 5.0f;
            gap_small.min_length = 2.0f; gap_small.max_length = 5.0f;
            gap_small.min_thickness = 0.5f; gap_small.max_thickness = 0.8f;  // Consistent with L0
            gap_small.base_r += 0.06f;

            std::cout << "[CASE 11 L1b] Small gap slabs (2-5m) at base Z...\n";
            auto l1b_pids = generate_strata_coverage(ps, gap_small, base_z, rng, base_placed, 50, 0.97f);
            for (int pid : l1b_pids) tile_pids.push_back(pid);
            std::cout << "  L1b: " << l1b_pids.size() << " slabs\n";

            // L1c: Fill remaining gaps with tiny slabs (0.5-2m) - SAME Z
            OrganicFloorConfig gap_tiny = huge;
            gap_tiny.min_width = 0.5f;  gap_tiny.max_width = 2.0f;
            gap_tiny.min_length = 0.5f; gap_tiny.max_length = 2.0f;
            gap_tiny.min_thickness = 0.5f; gap_tiny.max_thickness = 0.7f;  // Consistent with L0
            gap_tiny.base_r += 0.09f;

            std::cout << "[CASE 11 L1c] Tiny gap slabs (0.5-2m) → 99% at base Z...\n";
            auto l1c_pids = generate_strata_coverage(ps, gap_tiny, base_z, rng, base_placed, 300, 0.99f);
            for (int pid : l1c_pids) tile_pids.push_back(pid);
            std::cout << "  L1c: " << l1c_pids.size() << " slabs\n";

            // L1d: Final row-by-row fill with micro tiles (0.2-0.5m) - SAME Z
            OrganicFloorConfig micro = gap_tiny;
            micro.min_width = 0.2f;  micro.max_width = 0.5f;
            micro.min_length = 0.2f; micro.max_length = 0.5f;
            micro.min_thickness = 0.5f; micro.max_thickness = 0.6f;  // Consistent with base
            micro.base_r += 0.03f;

            std::cout << "[CASE 11 L1d] Micro gap fill (0.2-0.5m) → 99%...\n";
            auto l1d_pids = fill_floor_gaps(ps, micro, base_z, rng, base_placed, 0.1f, 0.99f);
            for (int pid : l1d_pids) tile_pids.push_back(pid);
            std::cout << "  L1d: " << l1d_pids.size() << " micro tiles (total base: "
                      << (l0_pids.size() + l1a_pids.size() + l1b_pids.size() + l1c_pids.size() + l1d_pids.size()) << ")\n";

            // L2: Small cosmetic pebbles on top (smaller than base tiles to avoid burying them)
            float cosmetic_z = huge.max_thickness + 0.1f;
            OrganicFloorConfig cosmetic = OrganicFloorConfig::earth();
            cosmetic.min_width = 0.3f;  cosmetic.max_width = 0.8f;   // Smaller than L1d micro
            cosmetic.min_length = 0.3f; cosmetic.max_length = 0.8f;
            cosmetic.min_thickness = 0.1f; cosmetic.max_thickness = 0.2f;
            cosmetic.chunk_min_x = config.chunk_min_x;
            cosmetic.chunk_max_x = config.chunk_max_x;
            cosmetic.chunk_min_y = config.chunk_min_y;
            cosmetic.chunk_max_y = config.chunk_max_y;

            std::cout << "[CASE 11 L2] Cosmetic pebbles (0.3-0.8m) on top...\n";
            std::vector<PlacedTile> l2_placed;  // Independent - can go anywhere on top
            auto l2_pids = generate_strata_coverage(ps, cosmetic, cosmetic_z, rng, l2_placed, 1000, 0.70f);
            for (int pid : l2_pids) tile_pids.push_back(pid);
            std::cout << "  L2: " << l2_pids.size() << " pebbles\n";

            // L3: Fine gravel on top (even smaller)
            float fine_z = cosmetic_z + cosmetic.max_thickness + 0.02f;
            OrganicFloorConfig fine = cosmetic;
            fine.min_width = 0.1f;  fine.max_width = 0.3f;
            fine.min_length = 0.1f; fine.max_length = 0.3f;
            fine.min_thickness = 0.03f; fine.max_thickness = 0.08f;
            fine.base_r -= 0.05f;

            std::cout << "[CASE 11 L3] Fine gravel (0.1-0.3m) on top...\n";
            auto l3_pids = generate_strata_coverage(ps, fine, fine_z, rng, l2_placed, 2000, 0.50f);
            for (int pid : l3_pids) tile_pids.push_back(pid);
            std::cout << "  L3: " << l3_pids.size() << " gravel pieces\n";

            // Update placed_tiles for coverage calc
            placed_tiles = base_placed;

            std::cout << "  Total case 11 tiles: " << tile_pids.size() << "\n";
        }

        // Case 12: Same as Case 10 but with 25% thinner strata (smoother effect)
        if (test_case == 12) {
            ps.clear_particles();
            tile_pids.clear();
            placed_tiles.clear();

            // Re-add light
            Particle light = {};
            light.x = 0.0f; light.y = -10.0f; light.z = 15.0f;
            light.size = 2.0f;
            light.SetMaterial(Materials::Type::LIGHT);
            light.is_light_source = true;
            light.emission_strength = 100000.0f;
            light.emission_radius = 500.0f;
            light.r = 1.0f; light.g = 1.0f; light.b = 1.0f;
            light_id = engine.add_particle(light);

            float current_z = base_z;

            // Layer 0: Huge slabs with 95% progressive fill - 50% thinner
            OrganicFloorConfig strata0 = OrganicFloorConfig::strata_base();
            strata0.min_thickness *= 0.50f;  // 50% thinner
            strata0.max_thickness *= 0.50f;
            strata0.chunk_min_x = config.chunk_min_x;
            strata0.chunk_max_x = config.chunk_max_x;
            strata0.chunk_min_y = config.chunk_min_y;
            strata0.chunk_max_y = config.chunk_max_y;

            std::cout << "[CASE 12 L0] Huge slabs (25% thinner) + 95% progressive fill...\n";
            std::vector<PlacedTile> l0_placed;
            auto l0_pids = generate_organic_floor_layer(ps, strata0, current_z, rng, l0_placed);
            for (int pid : l0_pids) tile_pids.push_back(pid);
            auto l0_fill = fill_floor_progressive(ps, strata0, current_z, rng, l0_placed, 0.80f, 0.95f, 0.6f);
            for (int pid : l0_fill) tile_pids.push_back(pid);
            std::cout << "  L0: " << l0_pids.size() << " + " << l0_fill.size() << " fill\n";

            // Layer 1: 80% slabs with 90% fill - even thinner
            current_z += strata0.max_thickness + 0.05f;
            OrganicFloorConfig strata1 = strata0;
            strata1.min_width *= 0.8f; strata1.max_width *= 0.8f;
            strata1.min_length *= 0.8f; strata1.max_length *= 0.8f;
            strata1.min_thickness *= 0.5f; strata1.max_thickness *= 0.5f;  // Extra thin
            strata1.base_r += 0.05f;

            std::cout << "[CASE 12 L1] 80% slabs + 90% fill...\n";
            std::vector<PlacedTile> l1_placed;
            auto l1_pids = generate_organic_floor_layer(ps, strata1, current_z, rng, l1_placed);
            for (int pid : l1_pids) tile_pids.push_back(pid);
            auto l1_fill = fill_floor_gaps(ps, strata1, current_z, rng, l1_placed, 0.5f, 0.90f);
            for (int pid : l1_fill) tile_pids.push_back(pid);
            std::cout << "  L1: " << l1_pids.size() << " + " << l1_fill.size() << " fill\n";

            // Layer 2: 64% slabs with 90% fill
            current_z += strata1.max_thickness + 0.05f;
            OrganicFloorConfig strata2 = strata1;
            strata2.min_width *= 0.8f; strata2.max_width *= 0.8f;
            strata2.min_length *= 0.8f; strata2.max_length *= 0.8f;
            strata2.min_thickness *= 0.8f; strata2.max_thickness *= 0.8f;
            strata2.base_r += 0.05f;

            std::cout << "[CASE 12 L2] 64% slabs + 90% fill...\n";
            std::vector<PlacedTile> l2_placed;
            auto l2_pids = generate_organic_floor_layer(ps, strata2, current_z, rng, l2_placed);
            for (int pid : l2_pids) tile_pids.push_back(pid);
            auto l2_fill = fill_floor_gaps(ps, strata2, current_z, rng, l2_placed, 0.3f, 0.90f);
            for (int pid : l2_fill) tile_pids.push_back(pid);
            std::cout << "  L2: " << l2_pids.size() << " + " << l2_fill.size() << " fill\n";

            // Layer 3: Normal particles with 60% fill
            current_z += strata2.max_thickness + 0.05f;
            OrganicFloorConfig normal = OrganicFloorConfig::earth();
            normal.chunk_min_x = config.chunk_min_x;
            normal.chunk_max_x = config.chunk_max_x;
            normal.chunk_min_y = config.chunk_min_y;
            normal.chunk_max_y = config.chunk_max_y;

            std::cout << "[CASE 12 L3] Normal + 60% fill...\n";
            std::vector<PlacedTile> l3_placed;
            auto l3_pids = generate_organic_floor_layer(ps, normal, current_z, rng, l3_placed);
            for (int pid : l3_pids) tile_pids.push_back(pid);
            auto l3_fill = fill_floor_gaps(ps, normal, current_z, rng, l3_placed, 0.2f, 0.60f);
            for (int pid : l3_fill) tile_pids.push_back(pid);
            std::cout << "  L3: " << l3_pids.size() << " + " << l3_fill.size() << " fill\n";

            std::cout << "  Total case 12 tiles: " << tile_pids.size() << "\n";
        }

        // Case 13: Even thinner (20%) + extra L1 layer: L0, L1a, L1b, L2, L3
        if (test_case == 13) {
            ps.clear_particles();
            tile_pids.clear();
            placed_tiles.clear();

            // Re-add light
            Particle light = {};
            light.x = 0.0f; light.y = -10.0f; light.z = 15.0f;
            light.size = 2.0f;
            light.SetMaterial(Materials::Type::LIGHT);
            light.is_light_source = true;
            light.emission_strength = 100000.0f;
            light.emission_radius = 500.0f;
            light.r = 1.0f; light.g = 1.0f; light.b = 1.0f;
            light_id = engine.add_particle(light);

            float current_z = base_z;

            // Layer 0: Huge slabs - 20% of original thickness (very thin)
            OrganicFloorConfig strata0 = OrganicFloorConfig::strata_base();
            strata0.min_thickness *= 0.20f;  // 20% = very thin
            strata0.max_thickness *= 0.20f;
            strata0.chunk_min_x = config.chunk_min_x;
            strata0.chunk_max_x = config.chunk_max_x;
            strata0.chunk_min_y = config.chunk_min_y;
            strata0.chunk_max_y = config.chunk_max_y;

            std::cout << "[CASE 13 L0] Huge slabs (20% thin) + 95% progressive fill...\n";
            std::vector<PlacedTile> l0_placed;
            auto l0_pids = generate_organic_floor_layer(ps, strata0, current_z, rng, l0_placed);
            for (int pid : l0_pids) tile_pids.push_back(pid);
            auto l0_fill = fill_floor_progressive(ps, strata0, current_z, rng, l0_placed, 0.80f, 0.95f, 0.6f);
            for (int pid : l0_fill) tile_pids.push_back(pid);
            std::cout << "  L0: " << l0_pids.size() << " + " << l0_fill.size() << " fill\n";

            // Layer 1a: First L1 - 80% width, thin
            current_z += strata0.max_thickness + 0.02f;
            OrganicFloorConfig strata1a = strata0;
            strata1a.min_width *= 0.8f; strata1a.max_width *= 0.8f;
            strata1a.min_length *= 0.8f; strata1a.max_length *= 0.8f;
            strata1a.min_thickness *= 0.8f; strata1a.max_thickness *= 0.8f;
            strata1a.base_r += 0.03f;

            std::cout << "[CASE 13 L1a] First 80% slabs + 90% fill...\n";
            std::vector<PlacedTile> l1a_placed;
            auto l1a_pids = generate_organic_floor_layer(ps, strata1a, current_z, rng, l1a_placed);
            for (int pid : l1a_pids) tile_pids.push_back(pid);
            auto l1a_fill = fill_floor_gaps(ps, strata1a, current_z, rng, l1a_placed, 0.5f, 0.90f);
            for (int pid : l1a_fill) tile_pids.push_back(pid);
            std::cout << "  L1a: " << l1a_pids.size() << " + " << l1a_fill.size() << " fill\n";

            // Layer 1b: Second L1 (extra layer) - same config as L1a
            current_z += strata1a.max_thickness + 0.02f;
            OrganicFloorConfig strata1b = strata1a;
            strata1b.base_r += 0.03f;

            std::cout << "[CASE 13 L1b] Second 80% slabs + 90% fill...\n";
            std::vector<PlacedTile> l1b_placed;
            auto l1b_pids = generate_organic_floor_layer(ps, strata1b, current_z, rng, l1b_placed);
            for (int pid : l1b_pids) tile_pids.push_back(pid);
            auto l1b_fill = fill_floor_gaps(ps, strata1b, current_z, rng, l1b_placed, 0.5f, 0.90f);
            for (int pid : l1b_fill) tile_pids.push_back(pid);
            std::cout << "  L1b: " << l1b_pids.size() << " + " << l1b_fill.size() << " fill\n";

            // Layer 2: 64% slabs with 90% fill
            current_z += strata1b.max_thickness + 0.02f;
            OrganicFloorConfig strata2 = strata1b;
            strata2.min_width *= 0.8f; strata2.max_width *= 0.8f;
            strata2.min_length *= 0.8f; strata2.max_length *= 0.8f;
            strata2.min_thickness *= 0.8f; strata2.max_thickness *= 0.8f;
            strata2.base_r += 0.03f;

            std::cout << "[CASE 13 L2] 64% slabs + 90% fill...\n";
            std::vector<PlacedTile> l2_placed;
            auto l2_pids = generate_organic_floor_layer(ps, strata2, current_z, rng, l2_placed);
            for (int pid : l2_pids) tile_pids.push_back(pid);
            auto l2_fill = fill_floor_gaps(ps, strata2, current_z, rng, l2_placed, 0.3f, 0.90f);
            for (int pid : l2_fill) tile_pids.push_back(pid);
            std::cout << "  L2: " << l2_pids.size() << " + " << l2_fill.size() << " fill\n";

            // Layer 3: Normal particles with 60% fill
            current_z += strata2.max_thickness + 0.02f;
            OrganicFloorConfig normal = OrganicFloorConfig::earth();
            normal.chunk_min_x = config.chunk_min_x;
            normal.chunk_max_x = config.chunk_max_x;
            normal.chunk_min_y = config.chunk_min_y;
            normal.chunk_max_y = config.chunk_max_y;

            std::cout << "[CASE 13 L3] Normal + 60% fill...\n";
            std::vector<PlacedTile> l3_placed;
            auto l3_pids = generate_organic_floor_layer(ps, normal, current_z, rng, l3_placed);
            for (int pid : l3_pids) tile_pids.push_back(pid);
            auto l3_fill = fill_floor_gaps(ps, normal, current_z, rng, l3_placed, 0.2f, 0.60f);
            for (int pid : l3_fill) tile_pids.push_back(pid);
            std::cout << "  L3: " << l3_pids.size() << " + " << l3_fill.size() << " fill\n";

            std::cout << "  Total case 13 tiles: " << tile_pids.size() << "\n";
        }

        // Case 14: Organic (no rows) - uses generate_strata_coverage instead of row-by-row
        if (test_case == 14) {
            ps.clear_particles();
            tile_pids.clear();
            placed_tiles.clear();

            // Re-add light
            Particle light = {};
            light.x = 0.0f; light.y = -10.0f; light.z = 15.0f;
            light.size = 2.0f;
            light.SetMaterial(Materials::Type::LIGHT);
            light.is_light_source = true;
            light.emission_strength = 100000.0f;
            light.emission_radius = 500.0f;
            light.r = 1.0f; light.g = 1.0f; light.b = 1.0f;
            light_id = engine.add_particle(light);

            float current_z = base_z;

            // Layer 0: Huge slabs - 20% thickness, ORGANIC placement
            OrganicFloorConfig strata0 = OrganicFloorConfig::strata_base();
            strata0.min_thickness *= 0.20f;
            strata0.max_thickness *= 0.20f;
            strata0.chunk_min_x = config.chunk_min_x;
            strata0.chunk_max_x = config.chunk_max_x;
            strata0.chunk_min_y = config.chunk_min_y;
            strata0.chunk_max_y = config.chunk_max_y;

            std::cout << "[CASE 14 L0] Huge slabs ORGANIC + 95% progressive fill...\n";
            std::vector<PlacedTile> l0_placed;
            auto l0_pids = generate_strata_coverage(ps, strata0, current_z, rng, l0_placed, 100, 0.95f);
            for (int pid : l0_pids) tile_pids.push_back(pid);
            auto l0_fill = fill_floor_progressive(ps, strata0, current_z, rng, l0_placed, 0.80f, 0.98f, 0.6f);
            for (int pid : l0_fill) tile_pids.push_back(pid);
            std::cout << "  L0: " << l0_pids.size() << " + " << l0_fill.size() << " fill\n";

            // Layer 1a: First 80% - ORGANIC
            current_z += strata0.max_thickness + 0.02f;
            OrganicFloorConfig strata1a = strata0;
            strata1a.min_width *= 0.8f; strata1a.max_width *= 0.8f;
            strata1a.min_length *= 0.8f; strata1a.max_length *= 0.8f;
            strata1a.min_thickness *= 0.8f; strata1a.max_thickness *= 0.8f;
            strata1a.base_r += 0.03f;

            std::cout << "[CASE 14 L1a] 80% slabs ORGANIC + 95% fill...\n";
            std::vector<PlacedTile> l1a_placed;
            auto l1a_pids = generate_strata_coverage(ps, strata1a, current_z, rng, l1a_placed, 200, 0.95f);
            for (int pid : l1a_pids) tile_pids.push_back(pid);
            auto l1a_fill = fill_floor_gaps(ps, strata1a, current_z, rng, l1a_placed, 0.5f, 0.98f);
            for (int pid : l1a_fill) tile_pids.push_back(pid);
            std::cout << "  L1a: " << l1a_pids.size() << " + " << l1a_fill.size() << " fill\n";

            // Layer 1b: Second 80% - ORGANIC
            current_z += strata1a.max_thickness + 0.02f;
            OrganicFloorConfig strata1b = strata1a;
            strata1b.base_r += 0.03f;

            std::cout << "[CASE 14 L1b] 80% slabs ORGANIC + 95% fill...\n";
            std::vector<PlacedTile> l1b_placed;
            auto l1b_pids = generate_strata_coverage(ps, strata1b, current_z, rng, l1b_placed, 200, 0.95f);
            for (int pid : l1b_pids) tile_pids.push_back(pid);
            auto l1b_fill = fill_floor_gaps(ps, strata1b, current_z, rng, l1b_placed, 0.5f, 0.98f);
            for (int pid : l1b_fill) tile_pids.push_back(pid);
            std::cout << "  L1b: " << l1b_pids.size() << " + " << l1b_fill.size() << " fill\n";

            // Layer 2: 64% slabs - ORGANIC
            current_z += strata1b.max_thickness + 0.02f;
            OrganicFloorConfig strata2 = strata1b;
            strata2.min_width *= 0.8f; strata2.max_width *= 0.8f;
            strata2.min_length *= 0.8f; strata2.max_length *= 0.8f;
            strata2.min_thickness *= 0.8f; strata2.max_thickness *= 0.8f;
            strata2.base_r += 0.03f;

            std::cout << "[CASE 14 L2] 64% slabs ORGANIC + 95% fill...\n";
            std::vector<PlacedTile> l2_placed;
            auto l2_pids = generate_strata_coverage(ps, strata2, current_z, rng, l2_placed, 300, 0.95f);
            for (int pid : l2_pids) tile_pids.push_back(pid);
            auto l2_fill = fill_floor_gaps(ps, strata2, current_z, rng, l2_placed, 0.3f, 0.98f);
            for (int pid : l2_fill) tile_pids.push_back(pid);
            std::cout << "  L2: " << l2_pids.size() << " + " << l2_fill.size() << " fill\n";

            // Layer 3: Normal particles - ORGANIC
            current_z += strata2.max_thickness + 0.02f;
            OrganicFloorConfig normal = OrganicFloorConfig::earth();
            normal.chunk_min_x = config.chunk_min_x;
            normal.chunk_max_x = config.chunk_max_x;
            normal.chunk_min_y = config.chunk_min_y;
            normal.chunk_max_y = config.chunk_max_y;

            std::cout << "[CASE 14 L3] Normal ORGANIC + 80% fill...\n";
            std::vector<PlacedTile> l3_placed;
            auto l3_pids = generate_strata_coverage(ps, normal, current_z, rng, l3_placed, 6000, 0.80f);
            for (int pid : l3_pids) tile_pids.push_back(pid);
            auto l3_fill = fill_floor_gaps(ps, normal, current_z, rng, l3_placed, 0.2f, 0.90f);
            for (int pid : l3_fill) tile_pids.push_back(pid);
            std::cout << "  L3: " << l3_pids.size() << " + " << l3_fill.size() << " fill\n";

            std::cout << "  Total case 14 tiles: " << tile_pids.size() << "\n";
        }

        std::cout << "  Total tiles: " << tile_pids.size() << "\n";

        // Calculate coverage
        float chunk_area = (config.chunk_max_x - config.chunk_min_x) *
                           (config.chunk_max_y - config.chunk_min_y);
        float tile_area = 0.0f;
        for (const auto& pt : placed_tiles) {
            tile_area += (pt.half_w * 2) * (pt.half_h * 2);
        }
        float coverage = (tile_area / chunk_area) * 100.0f;
        std::cout << "  Coverage: " << std::fixed << std::setprecision(1) << coverage << "%\n\n";
    };

    create_scene();

    // Run physics
    const float dt = 1.0f / 60.0f;
    const int max_frames = is_interactive ? 36000 : 1800;  // 10 min or 30s timeout

    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  VELOCITY TRACKING                                           ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n";
    std::cout << "Frame | MaxVel  | MinVel  | AvgVel  | AtRest\n";
    std::cout << "------+---------+---------+---------+---------\n";

    bool should_quit = false;
    bool all_at_rest = false;
    int final_frame = 0;

    // In interactive mode, only exit on ESC (ignore all_at_rest)
    // In headless mode, exit when all tiles settle
    for (int frame = 0; frame < max_frames && !should_quit && (is_interactive || !all_at_rest); ++frame) {
        engine.update(dt);
        final_frame = frame;

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();

            auto* ui = engine.get_ui_system();
            {
                auto particles = ps.lock_particles_for_read();

                float max_vel = 0.0f;
                int at_rest_count = 0;
                for (int pid : tile_pids) {
                    const Particle& p = particles[pid];
                    float vel = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
                    max_vel = std::max(max_vel, vel);
                    if (p.is_at_rest) at_rest_count++;
                }

                // Build case description
                std::string case_desc;
                switch (test_case) {
                    case 1: case_desc = "8m chunk"; break;
                    case 2: case_desc = "50m chunk"; break;
                    case 3: case_desc = "50m + 2 layers"; break;
                    case 4: case_desc = "50m + 3 layers"; break;
                    case 5: case_desc = "strata: huge slabs"; break;
                    case 6: case_desc = "strata: +80% slabs"; break;
                    case 7: case_desc = "strata: +64% slabs"; break;
                    case 8: case_desc = "strata: +normal"; break;
                    case 9: case_desc = "strata: +80% normal"; break;
                    case 10: case_desc = "strata: L0 95% progressive"; break;
                    case 11: case_desc = "OPTIMIZED: few slabs + cosmetic"; break;
                    case 12: case_desc = "Case10 + 50% thinner strata"; break;
                    case 13: case_desc = "20% thin + extra L1 layer"; break;
                    case 14: case_desc = "organic (no rows)"; break;
                    default: case_desc = "unknown"; break;
                }

                std::string title = "Case " + std::to_string(test_case) + "/" + std::to_string(MAX_CASE) + ": " + case_desc;
                ui->draw_text(10, 10, title, 255, 200, 100);
                ui->draw_text(10, 40, "Tiles: " + std::to_string(tile_pids.size()), 200, 200, 200);
                ui->draw_text(10, 70, "Frame: " + std::to_string(frame), 200, 200, 200);
                ui->draw_text(10, 100, "Max vel: " + std::to_string(max_vel).substr(0, 8) + " m/s", 200, 200, 200);

                std::string rest_str = std::to_string(at_rest_count) + "/" + std::to_string(tile_pids.size()) + " at rest";
                bool all_rest = (at_rest_count == (int)tile_pids.size());
                if (all_rest) {
                    ui->draw_text(10, 130, rest_str, 100, 255, 100);
                } else {
                    ui->draw_text(10, 130, rest_str, 255, 100, 100);
                }
                ui->draw_text(10, 200, "SPACE: next case | ESC: exit", 128, 128, 128);
            }

            engine.present();

            const auto& input = engine.get_input_system();
            static bool space_was_pressed = false;
            bool space_pressed = input.get_input_state().keys[GLFW_KEY_SPACE];
            if (space_pressed && !space_was_pressed) {
                test_case++;
                if (test_case > MAX_CASE) test_case = 1;
                create_scene();
            }
            space_was_pressed = space_pressed;

            if (input.get_input_state().keys[GLFW_KEY_ESCAPE]) {
                std::cout << "\n  ESC pressed - exiting" << std::endl;
                should_quit = true;
            }
        }

        // Check rest state
        {
            float max_vel = 0.0f, min_vel = 1000.0f, sum_vel = 0.0f;
            int at_rest_count = 0;

            auto particles = ps.lock_particles_for_read();

            for (int pid : tile_pids) {
                const Particle& p = particles[pid];
                float vel = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
                max_vel = std::max(max_vel, vel);
                min_vel = std::min(min_vel, vel);
                sum_vel += vel;
                if (p.is_at_rest) at_rest_count++;
            }

            if (at_rest_count == (int)tile_pids.size()) {
                all_at_rest = true;
            }

            // Log in headless mode
            if (!is_interactive && (frame % 30 == 0 || frame < 10 || all_at_rest)) {
                float avg_vel = sum_vel / tile_pids.size();
                std::cout << std::setw(5) << frame << " | "
                          << std::setw(7) << std::fixed << std::setprecision(4) << max_vel << " | "
                          << std::setw(7) << min_vel << " | "
                          << std::setw(7) << avg_vel << " | "
                          << at_rest_count << "/" << tile_pids.size() << "\n";
            }
        }
    }

    // Final state
    float elapsed_seconds = final_frame / 60.0f;
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  FINAL STATE (frame " << final_frame << " / " << std::fixed << std::setprecision(1) << elapsed_seconds << "s)\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";

    int total_at_rest = 0;
    float max_final_vel = 0.0f;

    {
        auto particles = ps.lock_particles_for_read();

        for (int pid : tile_pids) {
            const Particle& p = particles[pid];
            float vel = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
            max_final_vel = std::max(max_final_vel, vel);
            if (p.is_at_rest) total_at_rest++;
        }
    }

    int not_at_rest = tile_pids.size() - total_at_rest;

    std::cout << "Tiles: " << tile_pids.size() << "\n";
    std::cout << "At rest: " << total_at_rest << "/" << tile_pids.size() << "\n";
    std::cout << "NOT at rest: " << not_at_rest << "\n";
    std::cout << "Max velocity: " << max_final_vel << " m/s\n\n";

    // Diagnostic
    if (not_at_rest > 0 && not_at_rest <= 20) {
        std::cout << "=== PARTICLES NOT AT REST ===\n";
        auto particles = ps.lock_particles_for_read();
        for (int pid : tile_pids) {
            const Particle& p = particles[pid];
            if (!p.is_at_rest) {
                float vel = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
                std::cout << "P" << pid << ": pos=(" << p.x << "," << p.y << "," << p.z << ") "
                          << "vel=(" << p.vx << "," << p.vy << "," << p.vz << ") |v|=" << vel << "\n";
            }
        }
    }

    // Result
    if (not_at_rest == 0) {
        std::cout << "RESULT: PASS - All " << tile_pids.size() << " tiles at rest\n";
        return true;
    } else {
        std::cout << "RESULT: FAIL - " << not_at_rest << " tiles still moving\n";
        return false;
    }
}
