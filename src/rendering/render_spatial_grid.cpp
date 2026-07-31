#include "logosphere/rendering/render_spatial_grid.h"
#include "../core/camera_system.h"
#include <cmath>
#include <algorithm>

void RenderSpatialGrid::insert(int particle_idx, float x, float y) {
    int cx, cy;
    world_to_cell(x, y, cx, cy);
    Cell& cell = get_or_create_cell(cx, cy);
    cell.particle_indices.push_back(particle_idx);
}

void RenderSpatialGrid::remove(int particle_idx, float x, float y) {
    int cx, cy;
    world_to_cell(x, y, cx, cy);

    uint64_t key = cell_hash(cx, cy);
    auto it = cells_.find(key);
    if (it == cells_.end()) return;

    auto& indices = it->second.particle_indices;
    auto pos = std::find(indices.begin(), indices.end(), particle_idx);
    if (pos != indices.end()) {
        // Swap-and-pop for O(1) removal
        *pos = indices.back();
        indices.pop_back();
    }

    // Remove empty cells to save memory
    if (indices.empty()) {
        cells_.erase(it);
    }
}

void RenderSpatialGrid::update(int particle_idx, float old_x, float old_y, float new_x, float new_y) {
    int old_cx, old_cy, new_cx, new_cy;
    world_to_cell(old_x, old_y, old_cx, old_cy);
    world_to_cell(new_x, new_y, new_cx, new_cy);

    // Only update if cell changed
    if (old_cx != new_cx || old_cy != new_cy) {
        remove(particle_idx, old_x, old_y);
        insert(particle_idx, new_x, new_y);
    }
}

void RenderSpatialGrid::clear() {
    cells_.clear();
}

size_t RenderSpatialGrid::get_total_particles() const {
    size_t total = 0;
    for (const auto& pair : cells_) {
        total += pair.second.particle_indices.size();
    }
    return total;
}

RenderSpatialGrid::Cell& RenderSpatialGrid::get_or_create_cell(int cx, int cy) {
    uint64_t key = cell_hash(cx, cy);
    auto it = cells_.find(key);
    if (it != cells_.end()) {
        return it->second;
    }
    // Create new cell
    auto result = cells_.emplace(key, Cell(cx, cy));
    return result.first->second;
}

bool RenderSpatialGrid::cell_in_frustum(const Cell& cell,
                                         CameraSystem& camera,
                                         int render_width,
                                         int render_height) const {
    // Project cell corners to screen space and check if any part visible
    // Using simplified AABB-frustum test: project cell center and check distance

    float cell_center_x = (cell.min_x + cell.max_x) * 0.5f;
    float cell_center_y = (cell.min_y + cell.max_y) * 0.5f;
    float cell_z = 0.0f;  // Ground level for culling

    // Project to screen
    int screen_x, screen_y;
    camera.world_to_screen(cell_center_x, cell_center_y, cell_z,
                          screen_x, screen_y);

    // Cell radius in screen space (rough approximation)
    // CELL_SIZE is 10m, need to convert to screen pixels
    float ppu = camera.get_pixels_per_unit();
    float cell_radius_screen = CELL_SIZE * ppu * 1.5f;  // 1.5x for safety margin

    // Check if cell's screen bounds intersect viewport
    bool visible = (static_cast<float>(screen_x) + cell_radius_screen >= 0) &&
                   (static_cast<float>(screen_x) - cell_radius_screen <= render_width) &&
                   (static_cast<float>(screen_y) + cell_radius_screen >= 0) &&
                   (static_cast<float>(screen_y) - cell_radius_screen <= render_height);

    return visible;
}

std::vector<int> RenderSpatialGrid::query_frustum(
    CameraSystem& camera,
    int render_width,
    int render_height,
    float max_render_distance
) const {
    std::vector<int> result;

    // Get camera position for distance culling
    float cam_x, cam_y, cam_z;
    camera.get_position(cam_x, cam_y, cam_z);

    // Pre-reserve based on typical visible count
    result.reserve(cells_.size() * 10);  // Estimate ~10 particles per visible cell

    for (const auto& pair : cells_) {
        const Cell& cell = pair.second;

        // Distance culling first (cheaper than frustum test)
        float cell_center_x = (cell.min_x + cell.max_x) * 0.5f;
        float cell_center_y = (cell.min_y + cell.max_y) * 0.5f;
        float dx = cell_center_x - cam_x;
        float dy = cell_center_y - cam_y;
        float dist_sq = dx * dx + dy * dy;

        // Skip cells too far from camera
        float max_dist_with_margin = max_render_distance + CELL_SIZE;
        if (dist_sq > max_dist_with_margin * max_dist_with_margin) {
            continue;
        }

        // Frustum culling
        if (!cell_in_frustum(cell, camera, render_width, render_height)) {
            continue;
        }

        // Add all particles in visible cell
        result.insert(result.end(),
                     cell.particle_indices.begin(),
                     cell.particle_indices.end());
    }

    return result;
}
