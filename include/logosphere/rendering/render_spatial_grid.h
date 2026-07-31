#ifndef RENDER_SPATIAL_GRID_H
#define RENDER_SPATIAL_GRID_H

#include <vector>
#include <cmath>
#include <unordered_map>
#include <cstdint>

// Forward declarations
class CameraSystem;

/**
 * RenderSpatialGrid - Micro-chunk spatial partitioning for render culling
 *
 * Part of Two-Tier Chunk Streaming:
 * - Tier 1 (Memory): 50m game chunks loaded async
 * - Tier 2 (Render): 10m micro-chunks for camera-driven culling <- THIS
 *
 * Problem solved: pre_cull_entities() iterates ALL entities to frustum test.
 * With 112K entities, this scan costs time even with O(1) AABB tests.
 *
 * Solution: Spatial grid where frustum tests ~100 cells instead of 112K entities.
 *
 * Usage:
 *   grid.insert(particle_idx, x, y);  // On particle add
 *   grid.remove(particle_idx, x, y);  // On particle remove
 *   grid.update(particle_idx, old_x, old_y, new_x, new_y);  // On particle move
 *   auto visible = grid.query_frustum(camera, w, h);  // Each frame
 */
class RenderSpatialGrid {
public:
    static constexpr float CELL_SIZE = 10.0f;  // 10m micro-chunks

    RenderSpatialGrid() = default;
    ~RenderSpatialGrid() = default;

    // Particle management
    void insert(int particle_idx, float x, float y);
    void remove(int particle_idx, float x, float y);
    void update(int particle_idx, float old_x, float old_y, float new_x, float new_y);

    // Clear all cells (call on world reset)
    void clear();

    // Query: Returns particle indices in cells that intersect camera frustum
    // Only returns particles in visible micro-chunks (frustum + distance culling)
    std::vector<int> query_frustum(
        CameraSystem& camera,
        int render_width,
        int render_height,
        float max_render_distance = 100.0f  // Max distance from camera to render
    ) const;

    // Debug info
    size_t get_cell_count() const { return cells_.size(); }
    size_t get_total_particles() const;

private:
    struct Cell {
        std::vector<int> particle_indices;
        // Cell bounds (for quick frustum test)
        float min_x, min_y, max_x, max_y;

        Cell(int cx, int cy) {
            min_x = cx * CELL_SIZE;
            min_y = cy * CELL_SIZE;
            max_x = min_x + CELL_SIZE;
            max_y = min_y + CELL_SIZE;
        }
    };

    // Hash function for cell coordinates
    static uint64_t cell_hash(int cx, int cy) {
        // Combine into single 64-bit key (supports negative coords)
        uint32_t ux = static_cast<uint32_t>(cx);
        uint32_t uy = static_cast<uint32_t>(cy);
        return (static_cast<uint64_t>(ux) << 32) | static_cast<uint64_t>(uy);
    }

    // Get cell coordinates from world position
    static void world_to_cell(float x, float y, int& cx, int& cy) {
        cx = static_cast<int>(std::floor(x / CELL_SIZE));
        cy = static_cast<int>(std::floor(y / CELL_SIZE));
    }

    // Get or create cell at given coordinates
    Cell& get_or_create_cell(int cx, int cy);

    // Check if cell AABB intersects camera frustum
    bool cell_in_frustum(const Cell& cell,
                         CameraSystem& camera,
                         int render_width,
                         int render_height) const;

    // Storage: hash(cx, cy) → Cell
    std::unordered_map<uint64_t, Cell> cells_;
};

#endif // RENDER_SPATIAL_GRID_H
