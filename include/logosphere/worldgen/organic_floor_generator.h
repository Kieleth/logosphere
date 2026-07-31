#ifndef ORGANIC_FLOOR_GENERATOR_H
#define ORGANIC_FLOOR_GENERATOR_H

#include <vector>
#include <random>

class ParticleSystem;

// Placement tracking for overlap detection
struct PlacedTile {
    float cx, cy;          // Center position
    float half_w, half_h;  // Half-extents

    float min_x() const { return cx - half_w; }
    float max_x() const { return cx + half_w; }
    float min_y() const { return cy - half_h; }
    float max_y() const { return cy + half_h; }
};

// Configuration for organic floor generation
struct OrganicFloorConfig {
    // Area bounds (meters)
    float chunk_min_x = -4.0f;
    float chunk_max_x = 4.0f;
    float chunk_min_y = -4.0f;
    float chunk_max_y = 4.0f;

    // Tile size distribution (uniform) - meters
    float min_width = 0.3f;
    float max_width = 0.8f;
    float min_length = 0.3f;
    float max_length = 0.8f;
    float min_thickness = 0.10f;
    float max_thickness = 0.15f;

    // Gap distribution (normal) - meters
    float gap_mean = 0.05f;
    float gap_stddev = 0.02f;

    // Colors
    float base_r = 0.4f;
    float base_g = 0.35f;
    float base_b = 0.25f;
    float color_variance = 0.08f;

    // Gap-filling threshold (fraction of avg tile size)
    float gap_fill_threshold = 1.0f;

    // Terrain presets
    static OrganicFloorConfig rocky();
    static OrganicFloorConfig sandy();
    static OrganicFloorConfig earth();
    static OrganicFloorConfig strata_base();  // Huge flat slabs
};

// Organic floor generator utilities
namespace OrganicFloor {
    // Generate a layer of organic tiles with varied sizes
    // Returns particle IDs of created tiles
    // placed_tiles: output - AABBs for overlap detection
    std::vector<int> generate_layer(
        ParticleSystem& ps,
        const OrganicFloorConfig& config,
        float base_z,
        std::mt19937& rng,
        std::vector<PlacedTile>& placed_tiles
    );

    // Fill gaps larger than threshold with smaller tiles
    // Returns particle IDs of fill tiles
    // min_gap_size: minimum gap to fill (-1 = avg_tile_size * threshold)
    // coverage_target: stop when coverage >= target (0.0-1.0, -1 = no target)
    std::vector<int> fill_gaps(
        ParticleSystem& ps,
        const OrganicFloorConfig& config,
        float base_z,
        std::mt19937& rng,
        std::vector<PlacedTile>& placed_tiles,
        float min_gap_size = -1.0f,
        float coverage_target = -1.0f
    );

    // Progressive gap filling for high coverage targets
    // Uses multiple passes with diminishing tile sizes (width/length only, thickness preserved):
    //   Phase 1: Fill with config-sized tiles to phase1_coverage
    //   Phase 2+: Scale down width/length by scale_factor each pass until final_coverage
    // Returns particle IDs of all fill tiles
    std::vector<int> fill_gaps_progressive(
        ParticleSystem& ps,
        const OrganicFloorConfig& config,
        float base_z,
        std::mt19937& rng,
        std::vector<PlacedTile>& placed_tiles,
        float phase1_coverage = 0.80f,   // Target for huge slabs
        float final_coverage = 0.95f,    // Total target
        float scale_factor = 0.6f        // Width/length reduction per pass
    );

    // Strategic strata generation - few large slabs for maximum coverage
    // Greedy algorithm: places slabs in largest gaps until coverage target or max_slabs
    // L0: ~10 slabs → 80-90% coverage
    // L1: Use same function with L0's placed_tiles → 99%+ combined coverage
    // Returns particle IDs of created slabs
    std::vector<int> generate_strata_coverage(
        ParticleSystem& ps,
        const OrganicFloorConfig& config,
        float base_z,
        std::mt19937& rng,
        std::vector<PlacedTile>& placed_tiles,  // Input: existing tiles, Output: adds new tiles
        int max_slabs = 10,                     // Maximum slabs to place this layer
        float coverage_target = 0.90f           // Stop at this coverage
    );

    // Helper: check if two tiles overlap
    bool overlaps(const PlacedTile& a, const PlacedTile& b);

    // Helper: check if tile overlaps any in list
    bool overlaps_any(const PlacedTile& tile, const std::vector<PlacedTile>& placed);
}

#endif // ORGANIC_FLOOR_GENERATOR_H
