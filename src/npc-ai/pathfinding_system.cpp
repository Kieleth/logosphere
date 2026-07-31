// Pathfinding System Implementation
//
// A* search on 2D grid

#include "pathfinding_system.h"
#include <queue>
#include <unordered_map>
#include <algorithm>
#include <iostream>

namespace pathfinding {

// ============================================
// NavGrid Implementation
// ============================================

void NavGrid::init(float world_min_x, float world_min_y,
                   float world_max_x, float world_max_y,
                   float cell_size) {
    min_x_ = world_min_x;
    min_y_ = world_min_y;
    cell_size_ = cell_size;

    width_ = static_cast<int>((world_max_x - world_min_x) / cell_size) + 1;
    height_ = static_cast<int>((world_max_y - world_min_y) / cell_size) + 1;

    blocked_.resize(width_ * height_, false);
}

void NavGrid::clear() {
    std::fill(blocked_.begin(), blocked_.end(), false);
}

int NavGrid::world_to_grid_x(float world_x) const {
    return static_cast<int>((world_x - min_x_) / cell_size_);
}

int NavGrid::world_to_grid_y(float world_y) const {
    return static_cast<int>((world_y - min_y_) / cell_size_);
}

float NavGrid::grid_to_world_x(int grid_x) const {
    return min_x_ + (grid_x + 0.5f) * cell_size_;
}

float NavGrid::grid_to_world_y(int grid_y) const {
    return min_y_ + (grid_y + 0.5f) * cell_size_;
}

bool NavGrid::is_valid_cell(int gx, int gy) const {
    return gx >= 0 && gx < width_ && gy >= 0 && gy < height_;
}

bool NavGrid::is_blocked_cell(int gx, int gy) const {
    if (!is_valid_cell(gx, gy)) return true;
    return blocked_[gy * width_ + gx];
}

void NavGrid::set_blocked(float world_x, float world_y, bool blocked) {
    int gx = world_to_grid_x(world_x);
    int gy = world_to_grid_y(world_y);
    if (is_valid_cell(gx, gy)) {
        blocked_[gy * width_ + gx] = blocked;
    }
}

void NavGrid::set_blocked_rect(float min_x, float min_y, float max_x, float max_y, bool blocked) {
    int gx1 = world_to_grid_x(min_x);
    int gy1 = world_to_grid_y(min_y);
    int gx2 = world_to_grid_x(max_x);
    int gy2 = world_to_grid_y(max_y);

    for (int gy = gy1; gy <= gy2; gy++) {
        for (int gx = gx1; gx <= gx2; gx++) {
            if (is_valid_cell(gx, gy)) {
                blocked_[gy * width_ + gx] = blocked;
            }
        }
    }
}

bool NavGrid::is_walkable(float world_x, float world_y) const {
    int gx = world_to_grid_x(world_x);
    int gy = world_to_grid_y(world_y);
    return !is_blocked_cell(gx, gy);
}

void NavGrid::print() const {
    std::cout << "NavGrid " << width_ << "x" << height_ << ":" << std::endl;
    for (int y = height_ - 1; y >= 0; y--) {
        for (int x = 0; x < width_; x++) {
            std::cout << (is_blocked_cell(x, y) ? '#' : '.');
        }
        std::cout << std::endl;
    }
}

// ============================================
// Pathfinder Implementation
// ============================================

Path Pathfinder::find_path(const NavGrid& grid, Point start, Point goal) {
    Path result;
    result.valid = false;

    // Convert to grid coordinates
    int start_gx = grid.world_to_grid_x(start.x);
    int start_gy = grid.world_to_grid_y(start.y);
    int goal_gx = grid.world_to_grid_x(goal.x);
    int goal_gy = grid.world_to_grid_y(goal.y);

    // Validate start and goal
    if (!grid.is_valid_cell(start_gx, start_gy) ||
        !grid.is_valid_cell(goal_gx, goal_gy)) {
        if (verbose_) std::cout << "[PATH] Invalid start or goal" << std::endl;
        return result;
    }

    if (grid.is_blocked_cell(start_gx, start_gy)) {
        if (verbose_) std::cout << "[PATH] Start is blocked" << std::endl;
        return result;
    }

    if (grid.is_blocked_cell(goal_gx, goal_gy)) {
        if (verbose_) std::cout << "[PATH] Goal is blocked" << std::endl;
        return result;
    }

    // A* structures
    struct Node {
        int gx, gy;
        float g_cost = 0;
        float h_cost = 0;
        float f_cost() const { return g_cost + h_cost; }
    };

    auto heuristic = [](int x1, int y1, int x2, int y2) {
        // Euclidean distance
        float dx = static_cast<float>(x2 - x1);
        float dy = static_cast<float>(y2 - y1);
        return std::sqrt(dx*dx + dy*dy);
    };

    auto compare = [](const Node& a, const Node& b) {
        return a.f_cost() > b.f_cost();
    };

    std::priority_queue<Node, std::vector<Node>, decltype(compare)> open(compare);
    std::unordered_map<int, float> g_costs;  // Best g_cost to reach cell
    std::unordered_map<int, int> came_from;  // Parent cell for reconstruction

    auto cell_id = [&grid](int gx, int gy) { return gy * grid.width() + gx; };

    // Start
    Node start_node{start_gx, start_gy, 0, heuristic(start_gx, start_gy, goal_gx, goal_gy)};
    open.push(start_node);
    g_costs[cell_id(start_gx, start_gy)] = 0;

    // Neighbors (8-directional if diagonal allowed)
    std::vector<std::pair<int, int>> directions;
    directions.push_back({1, 0});
    directions.push_back({-1, 0});
    directions.push_back({0, 1});
    directions.push_back({0, -1});
    if (allow_diagonal_) {
        directions.push_back({1, 1});
        directions.push_back({1, -1});
        directions.push_back({-1, 1});
        directions.push_back({-1, -1});
    }

    int iterations = 0;
    const int MAX_ITERATIONS = 10000;

    while (!open.empty() && iterations < MAX_ITERATIONS) {
        iterations++;

        Node current = open.top();
        open.pop();

        // Goal reached?
        if (current.gx == goal_gx && current.gy == goal_gy) {
            // Reconstruct path
            std::vector<std::pair<int, int>> grid_path;
            int cx = goal_gx, cy = goal_gy;
            while (cx != start_gx || cy != start_gy) {
                grid_path.push_back({cx, cy});
                int parent = came_from[cell_id(cx, cy)];
                cx = parent % grid.width();
                cy = parent / grid.width();
            }
            grid_path.push_back({start_gx, start_gy});
            std::reverse(grid_path.begin(), grid_path.end());

            // Convert to world coordinates
            for (const auto& [gx, gy] : grid_path) {
                result.waypoints.push_back({
                    grid.grid_to_world_x(gx),
                    grid.grid_to_world_y(gy)
                });
            }
            result.valid = true;

            if (verbose_) {
                std::cout << "[PATH] Found in " << iterations << " iterations, "
                          << result.waypoints.size() << " waypoints" << std::endl;
            }
            return result;
        }

        // Skip if we've found a better path to this cell
        int cid = cell_id(current.gx, current.gy);
        if (g_costs.count(cid) && g_costs[cid] < current.g_cost) {
            continue;
        }

        // Explore neighbors
        for (const auto& [dx, dy] : directions) {
            int nx = current.gx + dx;
            int ny = current.gy + dy;

            if (!grid.is_valid_cell(nx, ny) || grid.is_blocked_cell(nx, ny)) {
                continue;
            }

            // Diagonal movement cost
            float move_cost = (dx != 0 && dy != 0) ? 1.414f : 1.0f;
            float new_g = current.g_cost + move_cost;

            int nid = cell_id(nx, ny);
            if (!g_costs.count(nid) || new_g < g_costs[nid]) {
                g_costs[nid] = new_g;
                came_from[nid] = cid;

                Node neighbor{nx, ny, new_g, heuristic(nx, ny, goal_gx, goal_gy)};
                open.push(neighbor);
            }
        }
    }

    if (verbose_) {
        std::cout << "[PATH] No path found after " << iterations << " iterations" << std::endl;
    }
    return result;
}

} // namespace pathfinding
