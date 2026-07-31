// Pathfinding System - A* Grid Navigation
//
// Simple 2D pathfinding for NPC movement.
// Uses a grid-based representation of walkable space.

#ifndef PATHFINDING_SYSTEM_H
#define PATHFINDING_SYSTEM_H

#include <vector>
#include <cmath>

namespace pathfinding {

// ============================================
// POINT
// 2D position (world coordinates)
// ============================================

struct Point {
    float x = 0, y = 0;

    Point() = default;
    Point(float x_, float y_) : x(x_), y(y_) {}

    float distance_to(const Point& other) const {
        float dx = other.x - x;
        float dy = other.y - y;
        return std::sqrt(dx*dx + dy*dy);
    }

    Point operator+(const Point& other) const { return {x + other.x, y + other.y}; }
    Point operator-(const Point& other) const { return {x - other.x, y - other.y}; }
    Point operator*(float s) const { return {x * s, y * s}; }
};

// ============================================
// PATH
// Sequence of waypoints
// ============================================

struct Path {
    std::vector<Point> waypoints;
    bool valid = false;

    bool empty() const { return waypoints.empty(); }
    size_t size() const { return waypoints.size(); }

    Point current() const {
        if (waypoints.empty()) return {0, 0};
        return waypoints.front();
    }

    void advance() {
        if (!waypoints.empty()) {
            waypoints.erase(waypoints.begin());
        }
    }

    float total_length() const {
        float len = 0;
        for (size_t i = 1; i < waypoints.size(); i++) {
            len += waypoints[i-1].distance_to(waypoints[i]);
        }
        return len;
    }
};

// ============================================
// NAV GRID
// 2D grid representing walkable space
// ============================================

class NavGrid {
public:
    NavGrid() = default;

    // Initialize grid covering world area
    // cell_size = size of each grid cell in world units
    void init(float world_min_x, float world_min_y,
              float world_max_x, float world_max_y,
              float cell_size);

    // Mark cell as blocked/unblocked
    void set_blocked(float world_x, float world_y, bool blocked);
    void set_blocked_rect(float min_x, float min_y, float max_x, float max_y, bool blocked);

    // Check if position is walkable
    bool is_walkable(float world_x, float world_y) const;

    // Clear all blocked cells
    void clear();

    // Get grid dimensions
    int width() const { return width_; }
    int height() const { return height_; }
    float cell_size() const { return cell_size_; }

    // Debug: print grid to console
    void print() const;

private:
    friend class Pathfinder;

    std::vector<bool> blocked_;  // true = blocked
    int width_ = 0, height_ = 0;
    float cell_size_ = 1.0f;
    float min_x_ = 0, min_y_ = 0;

    // Convert world coords to grid coords
    int world_to_grid_x(float world_x) const;
    int world_to_grid_y(float world_y) const;
    float grid_to_world_x(int grid_x) const;
    float grid_to_world_y(int grid_y) const;

    bool is_valid_cell(int gx, int gy) const;
    bool is_blocked_cell(int gx, int gy) const;
};

// ============================================
// PATHFINDER
// A* search on NavGrid
// ============================================

class Pathfinder {
public:
    Pathfinder() = default;

    // Find path from start to goal
    // Returns empty path if no path found
    Path find_path(const NavGrid& grid, Point start, Point goal);

    // Enable diagonal movement (default: true)
    void set_allow_diagonal(bool allow) { allow_diagonal_ = allow; }

    // Debug output
    void set_verbose(bool v) { verbose_ = v; }

private:
    bool allow_diagonal_ = true;
    bool verbose_ = false;
};

} // namespace pathfinding

#endif // PATHFINDING_SYSTEM_H
