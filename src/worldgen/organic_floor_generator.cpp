#include "logosphere/worldgen/organic_floor_generator.h"
#include "core/particle_system.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <iomanip>

// ============================================================================
// Config presets
// ============================================================================

OrganicFloorConfig OrganicFloorConfig::rocky() {
    OrganicFloorConfig c;
    c.min_width = 0.5f; c.max_width = 1.2f;
    c.min_length = 0.5f; c.max_length = 1.2f;
    c.min_thickness = 0.12f; c.max_thickness = 0.20f;
    c.gap_mean = 0.03f; c.gap_stddev = 0.01f;
    c.base_r = 0.35f; c.base_g = 0.35f; c.base_b = 0.38f;  // Gray stone
    return c;
}

OrganicFloorConfig OrganicFloorConfig::sandy() {
    OrganicFloorConfig c;
    c.min_width = 0.20f; c.max_width = 0.45f;
    c.min_length = 0.20f; c.max_length = 0.45f;
    c.min_thickness = 0.08f; c.max_thickness = 0.12f;
    c.gap_mean = 0.06f; c.gap_stddev = 0.03f;
    c.base_r = 0.75f; c.base_g = 0.65f; c.base_b = 0.45f;  // Sand color
    return c;
}

OrganicFloorConfig OrganicFloorConfig::earth() {
    OrganicFloorConfig c;
    // Default values are already earth-like
    return c;
}

OrganicFloorConfig OrganicFloorConfig::strata_base() {
    OrganicFloorConfig c;
    c.min_width = 3.0f;  c.max_width = 8.0f;    // Large slabs
    c.min_length = 3.0f; c.max_length = 8.0f;
    c.min_thickness = 0.2f; c.max_thickness = 1.0f;  // Varied strata heights
    c.gap_mean = 0.4f; c.gap_stddev = 0.2f;     // Larger gaps (organic)
    c.base_r = 0.35f; c.base_g = 0.30f; c.base_b = 0.20f;  // Darker earth
    c.color_variance = 0.05f;  // Less variance for uniform strata look
    return c;
}

// ============================================================================
// Helper functions
// ============================================================================

namespace {

float sample_uniform(std::mt19937& rng, float min_val, float max_val) {
    std::uniform_real_distribution<float> dist(min_val, max_val);
    return dist(rng);
}

float sample_normal_clamped(std::mt19937& rng, float mean, float stddev) {
    std::normal_distribution<float> dist(mean, stddev);
    float val = dist(rng);
    return std::max(0.0f, std::min(val, mean * 3.0f));
}

} // anonymous namespace

// ============================================================================
// OrganicFloor namespace implementation
// ============================================================================

namespace OrganicFloor {

bool overlaps(const PlacedTile& a, const PlacedTile& b) {
    if (a.max_x() <= b.min_x() || b.max_x() <= a.min_x()) return false;
    if (a.max_y() <= b.min_y() || b.max_y() <= a.min_y()) return false;
    return true;
}

bool overlaps_any(const PlacedTile& tile, const std::vector<PlacedTile>& placed) {
    for (const auto& p : placed) {
        if (overlaps(tile, p)) return true;
    }
    return false;
}

std::vector<int> generate_layer(
    ParticleSystem& ps,
    const OrganicFloorConfig& config,
    float base_z,
    std::mt19937& rng,
    std::vector<PlacedTile>& placed_tiles
) {
    std::vector<int> tile_pids;
    placed_tiles.clear();

    float avg_tile_width = (config.min_width + config.max_width) / 2.0f;

    // Row-by-row greedy placement
    float current_y = config.chunk_min_y;

    while (current_y < config.chunk_max_y) {
        float row_tile_length = sample_uniform(rng, config.min_length, config.max_length);
        float x_offset = sample_uniform(rng, 0.0f, avg_tile_width * 0.5f);
        float current_x = config.chunk_min_x + x_offset;

        while (current_x < config.chunk_max_x) {
            float tile_width = sample_uniform(rng, config.min_width, config.max_width);
            float tile_length = sample_uniform(rng, config.min_length, config.max_length);
            float tile_thick = sample_uniform(rng, config.min_thickness, config.max_thickness);

            float tile_cx = current_x + tile_width / 2.0f;
            float tile_cy = current_y + tile_length / 2.0f;

            // Clamp to bounds
            if (tile_cx + tile_width / 2.0f > config.chunk_max_x ||
                tile_cy + tile_length / 2.0f > config.chunk_max_y) {
                tile_width = std::min(tile_width, config.chunk_max_x - current_x);
                tile_length = std::min(tile_length, config.chunk_max_y - current_y);
                if (tile_width < config.min_width || tile_length < config.min_length) {
                    current_x += tile_width + sample_normal_clamped(rng, config.gap_mean, config.gap_stddev);
                    continue;
                }
                tile_cx = current_x + tile_width / 2.0f;
                tile_cy = current_y + tile_length / 2.0f;
            }

            PlacedTile pt;
            pt.cx = tile_cx;
            pt.cy = tile_cy;
            pt.half_w = tile_width / 2.0f;
            pt.half_h = tile_length / 2.0f;

            if (!overlaps_any(pt, placed_tiles)) {
                Particle tile = {};
                tile.x = tile_cx;
                tile.y = tile_cy;
                tile.z = base_z + tile_thick / 2.0f;
                tile.shape = ParticleShape::BOX;
                tile.width = tile_width;
                tile.height = tile_length;
                tile.thickness = tile_thick;
                tile.SetMaterial(Materials::Type::BRICK);

                float r_var = sample_uniform(rng, -config.color_variance, config.color_variance);
                float g_var = sample_uniform(rng, -config.color_variance, config.color_variance);
                float b_var = sample_uniform(rng, -config.color_variance, config.color_variance);
                tile.r = std::max(0.0f, std::min(1.0f, config.base_r + r_var));
                tile.g = std::max(0.0f, std::min(1.0f, config.base_g + g_var));
                tile.b = std::max(0.0f, std::min(1.0f, config.base_b + b_var));
                tile.friction = 0.5f;

                tile_pids.push_back(ps.queue_particle_addition(tile));
                placed_tiles.push_back(pt);
            }

            float gap = sample_normal_clamped(rng, config.gap_mean, config.gap_stddev);
            current_x += tile_width + gap;
        }

        float row_gap = sample_normal_clamped(rng, config.gap_mean, config.gap_stddev);
        current_y += row_tile_length + row_gap;
    }

    return tile_pids;
}

std::vector<int> fill_gaps(
    ParticleSystem& ps,
    const OrganicFloorConfig& config,
    float base_z,
    std::mt19937& rng,
    std::vector<PlacedTile>& placed_tiles,
    float min_gap_size,
    float coverage_target
) {
    std::vector<int> fill_pids;

    if (placed_tiles.empty()) return fill_pids;

    // Calculate chunk area and current coverage
    float chunk_area = (config.chunk_max_x - config.chunk_min_x) *
                       (config.chunk_max_y - config.chunk_min_y);

    auto calc_coverage = [&]() {
        float tile_area = 0.0f;
        for (const auto& pt : placed_tiles) {
            tile_area += (pt.half_w * 2) * (pt.half_h * 2);
        }
        return tile_area / chunk_area;
    };

    // Check if already at target
    if (coverage_target > 0 && calc_coverage() >= coverage_target) {
        return fill_pids;
    }

    // Calculate average tile size for default min_gap_size
    float total_area = 0.0f;
    for (const auto& pt : placed_tiles) {
        total_area += (pt.half_w * 2) * (pt.half_h * 2);
    }
    float avg_tile_area = total_area / placed_tiles.size();
    float avg_tile_side = std::sqrt(avg_tile_area);

    if (min_gap_size < 0) {
        min_gap_size = avg_tile_side * config.gap_fill_threshold;
    }

    // Create occupancy grid with resolution based on tile size
    // Use min tile size / 2 as cell resolution (at least 10cm)
    float cell_size = std::max(0.1f, config.min_width / 2.0f);
    int grid_width = static_cast<int>((config.chunk_max_x - config.chunk_min_x) / cell_size) + 1;
    int grid_height = static_cast<int>((config.chunk_max_y - config.chunk_min_y) / cell_size) + 1;
    std::vector<bool> occupied(grid_width * grid_height, false);

    // Mark occupied cells
    for (const auto& pt : placed_tiles) {
        int min_gx = static_cast<int>((pt.min_x() - config.chunk_min_x) / cell_size);
        int max_gx = static_cast<int>((pt.max_x() - config.chunk_min_x) / cell_size);
        int min_gy = static_cast<int>((pt.min_y() - config.chunk_min_y) / cell_size);
        int max_gy = static_cast<int>((pt.max_y() - config.chunk_min_y) / cell_size);

        for (int gy = std::max(0, min_gy); gy <= std::min(grid_height - 1, max_gy); ++gy) {
            for (int gx = std::max(0, min_gx); gx <= std::min(grid_width - 1, max_gx); ++gx) {
                occupied[gy * grid_width + gx] = true;
            }
        }
    }

    // Find empty cells and fill them (full grid scan until coverage target)
    for (int gy = 0; gy < grid_height; ++gy) {
        for (int gx = 0; gx < grid_width; ++gx) {
            // Check coverage target
            if (coverage_target > 0 && calc_coverage() >= coverage_target) {
                return fill_pids;
            }

            if (occupied[gy * grid_width + gx]) continue;

            float world_x = config.chunk_min_x + (gx + 0.5f) * cell_size;
            float world_y = config.chunk_min_y + (gy + 0.5f) * cell_size;

            // Sample tile - use min_gap_size to constrain max tile size
            float max_w = std::min((config.min_width + config.max_width) / 2.0f, min_gap_size * 2.0f);
            float max_l = std::min((config.min_length + config.max_length) / 2.0f, min_gap_size * 2.0f);
            max_w = std::max(max_w, config.min_width);
            max_l = std::max(max_l, config.min_length);

            float tile_width = sample_uniform(rng, config.min_width, max_w);
            float tile_length = sample_uniform(rng, config.min_length, max_l);
            float tile_thick = sample_uniform(rng, config.min_thickness, config.max_thickness);

            PlacedTile pt;
            pt.cx = world_x;
            pt.cy = world_y;
            pt.half_w = tile_width / 2.0f;
            pt.half_h = tile_length / 2.0f;

            // Check bounds
            if (pt.min_x() < config.chunk_min_x || pt.max_x() > config.chunk_max_x ||
                pt.min_y() < config.chunk_min_y || pt.max_y() > config.chunk_max_y) {
                continue;
            }

            // Check overlap
            if (!overlaps_any(pt, placed_tiles)) {
                Particle tile = {};
                tile.x = world_x;
                tile.y = world_y;
                tile.z = base_z + tile_thick / 2.0f;
                tile.shape = ParticleShape::BOX;
                tile.width = tile_width;
                tile.height = tile_length;
                tile.thickness = tile_thick;
                tile.SetMaterial(Materials::Type::BRICK);

                float r_var = sample_uniform(rng, -config.color_variance, config.color_variance);
                float g_var = sample_uniform(rng, -config.color_variance, config.color_variance);
                float b_var = sample_uniform(rng, -config.color_variance, config.color_variance);
                tile.r = std::max(0.0f, std::min(1.0f, config.base_r + r_var));
                tile.g = std::max(0.0f, std::min(1.0f, config.base_g + g_var));
                tile.b = std::max(0.0f, std::min(1.0f, config.base_b + b_var));
                tile.friction = 0.5f;

                fill_pids.push_back(ps.queue_particle_addition(tile));
                placed_tiles.push_back(pt);

                // Mark new cells as occupied
                int min_gx2 = static_cast<int>((pt.min_x() - config.chunk_min_x) / cell_size);
                int max_gx2 = static_cast<int>((pt.max_x() - config.chunk_min_x) / cell_size);
                int min_gy2 = static_cast<int>((pt.min_y() - config.chunk_min_y) / cell_size);
                int max_gy2 = static_cast<int>((pt.max_y() - config.chunk_min_y) / cell_size);

                for (int gy2 = std::max(0, min_gy2); gy2 <= std::min(grid_height - 1, max_gy2); ++gy2) {
                    for (int gx2 = std::max(0, min_gx2); gx2 <= std::min(grid_width - 1, max_gx2); ++gx2) {
                        occupied[gy2 * grid_width + gx2] = true;
                    }
                }
            }
        }
    }

    return fill_pids;
}

std::vector<int> fill_gaps_progressive(
    ParticleSystem& ps,
    const OrganicFloorConfig& config,
    float base_z,
    std::mt19937& rng,
    std::vector<PlacedTile>& placed_tiles,
    float phase1_coverage,
    float final_coverage,
    float scale_factor
) {
    std::vector<int> all_pids;

    float chunk_area = (config.chunk_max_x - config.chunk_min_x) *
                       (config.chunk_max_y - config.chunk_min_y);

    auto calc_coverage = [&]() {
        float tile_area = 0.0f;
        for (const auto& pt : placed_tiles) {
            tile_area += (pt.half_w * 2) * (pt.half_h * 2);
        }
        return tile_area / chunk_area;
    };

    float initial_coverage = calc_coverage();

    // Already at target?
    if (initial_coverage >= final_coverage) return all_pids;

    // Phase 1: Fill with config-sized tiles to phase1_coverage
    auto phase1_pids = fill_gaps(ps, config, base_z, rng, placed_tiles, -1.0f, phase1_coverage);
    all_pids.insert(all_pids.end(), phase1_pids.begin(), phase1_pids.end());

    float phase1_final = calc_coverage();

    // Phase 2+: Progressively smaller tiles until final_coverage
    OrganicFloorConfig scaled = config;
    float current_scale = scale_factor;
    int max_passes = 10;  // Safety limit
    int pass_count = 0;

    for (int pass = 0; pass < max_passes && calc_coverage() < final_coverage; ++pass) {
        // Scale width/length only, preserve thickness
        scaled.min_width = config.min_width * current_scale;
        scaled.max_width = config.max_width * current_scale;
        scaled.min_length = config.min_length * current_scale;
        scaled.max_length = config.max_length * current_scale;
        // Thickness stays same: scaled.min_thickness, scaled.max_thickness unchanged

        // Minimum tile size sanity check (10cm)
        if (scaled.min_width < 0.1f || scaled.min_length < 0.1f) break;

        auto pass_pids = fill_gaps(ps, scaled, base_z, rng, placed_tiles, -1.0f, final_coverage);
        all_pids.insert(all_pids.end(), pass_pids.begin(), pass_pids.end());

        current_scale *= scale_factor;
        pass_count++;
    }

    float final_cov = calc_coverage();
    std::cout << "    [progressive] " << std::fixed << std::setprecision(1)
              << (initial_coverage * 100) << "% → " << (phase1_final * 100)
              << "% (phase1: " << phase1_pids.size() << " tiles) → "
              << (final_cov * 100) << "% (" << pass_count << " passes, "
              << (all_pids.size() - phase1_pids.size()) << " diminishing tiles)\n";

    return all_pids;
}

std::vector<int> generate_strata_coverage(
    ParticleSystem& ps,
    const OrganicFloorConfig& config,
    float base_z,
    std::mt19937& rng,
    std::vector<PlacedTile>& placed_tiles,
    int max_slabs,
    float coverage_target
) {
    std::vector<int> slab_pids;

    float chunk_w = config.chunk_max_x - config.chunk_min_x;
    float chunk_h = config.chunk_max_y - config.chunk_min_y;
    float chunk_area = chunk_w * chunk_h;

    auto calc_coverage = [&]() {
        float tile_area = 0.0f;
        for (const auto& pt : placed_tiles) {
            tile_area += (pt.half_w * 2) * (pt.half_h * 2);
        }
        return tile_area / chunk_area;
    };

    float initial_coverage = calc_coverage();
    if (initial_coverage >= coverage_target) return slab_pids;

    // Build occupancy grid to find gaps efficiently
    float cell_size = config.min_width / 2.0f;
    int grid_w = static_cast<int>(chunk_w / cell_size) + 1;
    int grid_h = static_cast<int>(chunk_h / cell_size) + 1;
    std::vector<bool> occupied(grid_w * grid_h, false);

    // Mark occupied cells from existing tiles
    for (const auto& pt : placed_tiles) {
        int min_gx = static_cast<int>((pt.min_x() - config.chunk_min_x) / cell_size);
        int max_gx = static_cast<int>((pt.max_x() - config.chunk_min_x) / cell_size);
        int min_gy = static_cast<int>((pt.min_y() - config.chunk_min_y) / cell_size);
        int max_gy = static_cast<int>((pt.max_y() - config.chunk_min_y) / cell_size);
        for (int gy = std::max(0, min_gy); gy <= std::min(grid_h - 1, max_gy); ++gy) {
            for (int gx = std::max(0, min_gx); gx <= std::min(grid_w - 1, max_gx); ++gx) {
                occupied[gy * grid_w + gx] = true;
            }
        }
    }

    // Collect unoccupied cells as candidate positions
    std::vector<std::pair<int, int>> empty_cells;
    for (int gy = 0; gy < grid_h; ++gy) {
        for (int gx = 0; gx < grid_w; ++gx) {
            if (!occupied[gy * grid_w + gx]) {
                empty_cells.push_back({gx, gy});
            }
        }
    }

    std::cout << "    [strata-debug] grid=" << grid_w << "x" << grid_h
              << " cell=" << cell_size << "m empty=" << empty_cells.size() << "\n";

    for (int slab_idx = 0; slab_idx < max_slabs; ++slab_idx) {
        float current_cov = calc_coverage();
        if (current_cov >= coverage_target) {
            std::cout << "    [strata-debug] slab " << slab_idx << ": coverage "
                      << (current_cov*100) << "% >= target, stopping\n";
            break;
        }
        if (empty_cells.empty()) {
            std::cout << "    [strata-debug] slab " << slab_idx << ": no empty cells, stopping\n";
            break;
        }

        float best_area = 0.0f;
        PlacedTile best_tile;
        float best_thickness = 0.0f;
        int valid_samples = 0;

        // Sample from empty cells (more efficient than random chunk positions)
        int samples = std::min(100, static_cast<int>(empty_cells.size()));
        for (int sample = 0; sample < samples; ++sample) {
            // Pick random empty cell
            std::uniform_int_distribution<int> dist(0, empty_cells.size() - 1);
            auto [gx, gy] = empty_cells[dist(rng)];

            float cx = config.chunk_min_x + (gx + 0.5f) * cell_size;
            float cy = config.chunk_min_y + (gy + 0.5f) * cell_size;

            // Try maximum size first, shrink if overlaps
            float half_w = config.max_width / 2.0f;
            float half_h = config.max_length / 2.0f;

            PlacedTile candidate;
            candidate.cx = cx;
            candidate.cy = cy;
            candidate.half_w = half_w;
            candidate.half_h = half_h;

            // Shrink until no overlap or too small
            while (overlaps_any(candidate, placed_tiles) &&
                   candidate.half_w > config.min_width / 2.0f &&
                   candidate.half_h > config.min_length / 2.0f) {
                candidate.half_w *= 0.9f;
                candidate.half_h *= 0.9f;
            }

            // Check bounds
            if (candidate.min_x() < config.chunk_min_x) candidate.cx = config.chunk_min_x + candidate.half_w;
            if (candidate.max_x() > config.chunk_max_x) candidate.cx = config.chunk_max_x - candidate.half_w;
            if (candidate.min_y() < config.chunk_min_y) candidate.cy = config.chunk_min_y + candidate.half_h;
            if (candidate.max_y() > config.chunk_max_y) candidate.cy = config.chunk_max_y - candidate.half_h;

            // Skip if still overlapping or too small
            if (overlaps_any(candidate, placed_tiles)) continue;
            if (candidate.half_w < config.min_width / 2.0f) continue;
            if (candidate.half_h < config.min_length / 2.0f) continue;

            valid_samples++;
            float area = (candidate.half_w * 2) * (candidate.half_h * 2);
            if (area > best_area) {
                best_area = area;
                best_tile = candidate;
                best_thickness = sample_uniform(rng, config.min_thickness, config.max_thickness);
            }
        }

        // Place best slab found
        if (best_area > 0) {
            std::cout << "    [strata-debug] slab " << slab_idx << ": placed "
                      << (best_tile.half_w*2) << "x" << (best_tile.half_h*2)
                      << "m at (" << best_tile.cx << "," << best_tile.cy
                      << ") valid_samples=" << valid_samples << "/" << samples << "\n";
            Particle slab = {};
            slab.x = best_tile.cx;
            slab.y = best_tile.cy;
            slab.z = base_z + best_thickness / 2.0f;
            slab.shape = ParticleShape::BOX;
            slab.width = best_tile.half_w * 2;
            slab.height = best_tile.half_h * 2;
            slab.thickness = best_thickness;
            slab.SetMaterial(Materials::Type::BRICK);

            float r_var = sample_uniform(rng, -config.color_variance, config.color_variance);
            float g_var = sample_uniform(rng, -config.color_variance, config.color_variance);
            float b_var = sample_uniform(rng, -config.color_variance, config.color_variance);
            slab.r = std::max(0.0f, std::min(1.0f, config.base_r + r_var));
            slab.g = std::max(0.0f, std::min(1.0f, config.base_g + g_var));
            slab.b = std::max(0.0f, std::min(1.0f, config.base_b + b_var));
            slab.friction = 0.5f;

            slab_pids.push_back(ps.queue_particle_addition(slab));
            placed_tiles.push_back(best_tile);

            // Update occupancy grid and remove now-occupied cells
            int min_gx = static_cast<int>((best_tile.min_x() - config.chunk_min_x) / cell_size);
            int max_gx = static_cast<int>((best_tile.max_x() - config.chunk_min_x) / cell_size);
            int min_gy = static_cast<int>((best_tile.min_y() - config.chunk_min_y) / cell_size);
            int max_gy = static_cast<int>((best_tile.max_y() - config.chunk_min_y) / cell_size);
            for (int gy = std::max(0, min_gy); gy <= std::min(grid_h - 1, max_gy); ++gy) {
                for (int gx = std::max(0, min_gx); gx <= std::min(grid_w - 1, max_gx); ++gx) {
                    occupied[gy * grid_w + gx] = true;
                }
            }

            // Rebuild empty_cells (could optimize but this is fine for ~10 slabs)
            empty_cells.clear();
            for (int gy = 0; gy < grid_h; ++gy) {
                for (int gx = 0; gx < grid_w; ++gx) {
                    if (!occupied[gy * grid_w + gx]) {
                        empty_cells.push_back({gx, gy});
                    }
                }
            }
        } else {
            // No valid position found, stop
            std::cout << "    [strata-debug] slab " << slab_idx
                      << ": no valid position found (valid_samples=" << valid_samples
                      << " empty_cells=" << empty_cells.size() << "), stopping\n";
            break;
        }
    }

    float final_cov = calc_coverage();
    std::cout << "    [strata] " << slab_pids.size() << " slabs: "
              << std::fixed << std::setprecision(1)
              << (initial_coverage * 100) << "% → " << (final_cov * 100) << "%\n";

    return slab_pids;
}

} // namespace OrganicFloor
