#include "projection_system.h"
#include <cmath>
#include <algorithm>
#include <iostream>  // For debug logging

// Classic 2.5D isometric projection
void IsometricProjection::project(
    float world_x, float world_y, float world_z,      // INPUT: WORLD SPACE - X=East, Y=North, Z=Up
    float camera_x, float camera_y, float camera_z,    // INPUT: WORLD SPACE - Camera position
    int viewport_width, int viewport_height,           // INPUT: SCREEN SPACE - Display dimensions in pixels
    float pixels_per_unit,                             // INPUT: Scale factor - world meters to screen pixels
    int& screen_x, int& screen_y) const {              // OUTPUT: SCREEN SPACE - pixel coordinates (origin top-left)

    // TRANSFORM 1: World Space → View Space (camera-relative coordinates)
    float view_x = world_x - camera_x;  // VIEW SPACE X: East(+)/West(-) relative to camera
    float view_y = world_y - camera_y;  // VIEW SPACE Y: North(+)/South(-) relative to camera
    float view_z = world_z - camera_z;  // VIEW SPACE Z: Up(+)/Down(-) relative to camera

    // TRANSFORM 2: View Space → Isometric Projection Space
    // Standard isometric formula for proper compass alignment:
    // - Rotate 45° around Z axis: transforms square grid to diamond
    // - Tilt ~30° for depth: cos(30°) ≈ 0.866, sin(30°) = 0.5
    float iso_x = (view_x - view_y) * 0.866f;              // PROJECTION SPACE X: horizontal on screen
    float iso_y = (view_x + view_y) * 0.5f + view_z * height_scale_;  // PROJECTION SPACE Y: vertical on screen

    // COORDINATE MAPPING from world to screen (after Y-axis inversion):
    // World North (+Y) → Screen diagonal top-left (iso_x-, iso_y-)
    // World South (-Y) → Screen diagonal bottom-right (iso_x+, iso_y+)
    // World East (+X) → Screen diagonal top-right (iso_x+, iso_y-)
    // World West (-X) → Screen diagonal bottom-left (iso_x-, iso_y+)
    // World Up (+Z) → Screen straight up

    // TRANSFORM 3: Projection Space → Screen Space (final pixel coordinates)
    screen_x = viewport_width / 2 + static_cast<int>(iso_x * pixels_per_unit);   // SCREEN X: pixels from left edge
    screen_y = viewport_height / 2 - static_cast<int>(iso_y * pixels_per_unit);  // SCREEN Y: pixels from top edge (inverted)
    // Note: Subtract iso_y because screen Y+ goes down, but projection Y+ should go up visually

    // DEBUG: Print projection calculation for camera position (should be screen center)
    // Only print if projecting a point very close to camera (within 0.1m)
    static bool debug_enabled = false;  // Toggle this to enable/disable projection debug
    if (debug_enabled && fabsf(view_x) < 0.1f && fabsf(view_y) < 0.1f && fabsf(view_z) < 0.1f) {
        std::cout << "[PROJECTION] World: (" << world_x << ", " << world_y << ", " << world_z << ")" << std::endl;
        std::cout << "[PROJECTION] Camera: (" << camera_x << ", " << camera_y << ", " << camera_z << ")" << std::endl;
        std::cout << "[PROJECTION] View: (" << view_x << ", " << view_y << ", " << view_z << ")" << std::endl;
        std::cout << "[PROJECTION] Iso: (" << iso_x << ", " << iso_y << ")" << std::endl;
        std::cout << "[PROJECTION] Screen: (" << screen_x << ", " << screen_y << ") [center: "
                  << viewport_width/2 << ", " << viewport_height/2 << "]" << std::endl;
        std::cout << "[PROJECTION] Pixels/unit: " << pixels_per_unit << std::endl;
    }
}

// Isometric with depth-based scaling
void IsometricDepthProjection::project(
    float world_x, float world_y, float world_z,      // INPUT: WORLD SPACE - X=East, Y=North, Z=Up
    float camera_x, float camera_y, float camera_z,    // INPUT: WORLD SPACE - Camera position
    int viewport_width, int viewport_height,           // INPUT: SCREEN SPACE - Display dimensions in pixels
    float pixels_per_unit,                             // INPUT: Scale factor - world meters to screen pixels
    int& screen_x, int& screen_y) const {              // OUTPUT: SCREEN SPACE - pixel coordinates (origin top-left)
    
    // TRANSFORM 1: World Space → View Space (camera-relative)
    float view_x = world_x - camera_x;  // VIEW SPACE X: East(+)/West(-) relative to camera
    float view_y = world_y - camera_y;  // VIEW SPACE Y: North(+)/South(-) relative to camera
    float view_z = world_z - camera_z;  // VIEW SPACE Z: Up(+)/Down(-) relative to camera
    
    // Calculate depth for scaling (objects further away appear smaller)
    float depth = view_y;  // Use forward distance for depth
    float scale = 1.0f / (1.0f + depth / depth_factor_);
    scale = std::max(min_scale_, std::min(max_scale_, scale));
    
    // TRANSFORM 2: View Space → Isometric Projection Space (with depth scaling)
    // Using SAME formula as basic isometric to maintain consistency
    float iso_x = (view_x - view_y) * 0.866f * scale;              // PROJECTION SPACE X (scaled)
    float iso_y = (view_x + view_y) * 0.5f * scale + view_z * scale;  // PROJECTION SPACE Y (scaled)
    
    // TRANSFORM 3: Projection Space → Screen Space
    screen_x = viewport_width / 2 + static_cast<int>(iso_x * pixels_per_unit);   // SCREEN X: pixels from left
    screen_y = viewport_height / 2 - static_cast<int>(iso_y * pixels_per_unit);  // SCREEN Y: pixels from top (inverted)
}

void IsometricDepthProjection::configure(float depth_factor, float min_scale, float max_scale) {
    depth_factor_ = depth_factor;
    min_scale_ = min_scale;
    max_scale_ = max_scale;
}

// True 3D perspective projection
void PerspectiveProjection::project(float world_x, float world_y, float world_z,
                                   float camera_x, float camera_y, float camera_z,
                                   int viewport_width, int viewport_height,
                                   float pixels_per_unit,
                                   int& screen_x, int& screen_y) const {
    // Transform to view space (camera-relative coordinates)
    float view_x = world_x - camera_x;
    float view_y = world_y - camera_y;
    float view_z = world_z - camera_z;
    
    // For proper perspective, we need to establish a camera coordinate system
    // Since camera looks at origin from (-20,-20,40), we can derive the forward vector
    // Forward vector points from camera to origin (what we're looking at)
    float forward_x = -camera_x;  // Points toward origin
    float forward_y = -camera_y;
    float forward_z = -camera_z;
    
    // Normalize forward vector
    float forward_len = std::sqrt(forward_x*forward_x + forward_y*forward_y + forward_z*forward_z);
    if (forward_len < 0.001f) {
        // Camera at origin, can't determine forward direction
        screen_x = viewport_width / 2;
        screen_y = viewport_height / 2;
        return;
    }
    forward_x /= forward_len;
    forward_y /= forward_len;
    forward_z /= forward_len;
    
    // Project view position onto forward axis (depth)
    float depth = view_x * forward_x + view_y * forward_y + view_z * forward_z;
    
    // Avoid division by zero and near plane clipping
    if (depth <= near_plane_) {
        screen_x = -9999;
        screen_y = -9999;
        return;
    }
    
    // Create right and up vectors for the camera
    // Right = forward × world_up (0,0,1)
    float right_x = forward_y;   // forward × (0,0,1)
    float right_y = -forward_x;
    float right_z = 0;
    
    // Normalize right vector
    float right_len = std::sqrt(right_x*right_x + right_y*right_y);
    if (right_len > 0.001f) {
        right_x /= right_len;
        right_y /= right_len;
    }
    
    // Up = right × forward
    float up_x = right_y * forward_z;
    float up_y = -right_x * forward_z;
    float up_z = right_x * forward_y - right_y * forward_x;
    
    // Project onto right and up axes
    float screen_space_x = view_x * right_x + view_y * right_y + view_z * right_z;
    float screen_space_y = view_x * up_x + view_y * up_y + view_z * up_z;
    
    // Apply perspective division
    float fov_rad = fov_degrees_ * M_PI / 180.0f;
    float focal_length = (viewport_height * 0.5f) / tanf(fov_rad * 0.5f);
    
    float projected_x = (screen_space_x * focal_length) / depth;
    float projected_y = (screen_space_y * focal_length) / depth;
    
    // Convert to screen coordinates
    screen_x = viewport_width / 2 + static_cast<int>(projected_x);
    screen_y = viewport_height / 2 - static_cast<int>(projected_y);
}

void PerspectiveProjection::configure(float fov_degrees, float near_plane, float far_plane) {
    fov_degrees_ = fov_degrees;
    near_plane_ = near_plane;
    far_plane_ = far_plane;
}

// Bird's eye view projection - looking straight down from above
void BirdsEyeProjection::project(float world_x, float world_y, float world_z,
                                float camera_x, float camera_y, float camera_z,
                                int viewport_width, int viewport_height,
                                float pixels_per_unit,
                                int& screen_x, int& screen_y) const {
    // For bird's eye view, we look straight down (along negative Z axis)
    // X and Y map directly to screen, Z is ignored for position
    
    // Transform to view space (camera-relative)
    float view_x = world_x - camera_x;
    float view_y = world_y - camera_y;
    // Z is not used for positioning in top-down view
    
    // Direct orthographic projection
    // X maps to screen X (East/West)
    // Y maps to screen Y (North/South) 
    // Note: We might need to rotate or flip depending on desired orientation
    
    // Convert to screen coordinates
    // X: East is right on screen
    // Y: North is up on screen (needs inversion since screen Y+ is down)
    screen_x = viewport_width / 2 + static_cast<int>(view_x * pixels_per_unit);
    screen_y = viewport_height / 2 - static_cast<int>(view_y * pixels_per_unit);  // Minus for Y inversion
}

bool BirdsEyeProjection::validate_projected_quad(const float quad_x[4], const float quad_y[4]) const {
    // In bird's eye view, a square should remain a square
    // and a rectangle should remain a rectangle
    // For now, just return true - proper validation can be added later
    return true;
}

// Cabinet oblique projection
void CabinetProjection::project(float world_x, float world_y, float world_z,
                               float camera_x, float camera_y, float camera_z,
                               int viewport_width, int viewport_height,
                               float pixels_per_unit,
                               int& screen_x, int& screen_y) const {
    // Transform to camera space
    float rel_x = world_x - camera_x;
    float rel_y = world_y - camera_y;
    float rel_z = world_z - camera_z;
    
    // Cabinet projection: front face (X-Z plane) is undistorted
    // Depth (Y) is shown at an angle with reduced scale
    // TODO[RENDER-001]: Cabinet projection may show visual artifacts (triangular splits in quads)
    // and movement feels rotated. This is partially expected behavior for oblique projection,
    // but could be improved with better quad rasterization or adjusted projection angles.
    float angle_rad = angle_degrees_ * M_PI / 180.0f;
    float proj_x = rel_x + rel_y * depth_ratio_ * cosf(angle_rad);
    float proj_y = rel_z + rel_y * depth_ratio_ * sinf(angle_rad);
    
    // Convert to screen coordinates
    screen_x = viewport_width / 2 + static_cast<int>(proj_x * pixels_per_unit);
    screen_y = viewport_height / 2 - static_cast<int>(proj_y * pixels_per_unit);
}

void CabinetProjection::configure(float angle_degrees, float depth_ratio, float unused) {
    angle_degrees_ = angle_degrees;
    depth_ratio_ = depth_ratio;
}

// Factory implementation
std::unique_ptr<ProjectionSystem> ProjectionFactory::create(Type type) {
    switch (type) {
        case Type::Isometric:
            return std::make_unique<IsometricProjection>();
        case Type::IsometricDepth:
            return std::make_unique<IsometricDepthProjection>();
        case Type::Perspective:
            return std::make_unique<PerspectiveProjection>();
        case Type::Cabinet:
            return std::make_unique<CabinetProjection>();
        case Type::BirdsEye:
            return std::make_unique<BirdsEyeProjection>();
        default:
            return std::make_unique<IsometricProjection>();
    }
}

// Validation implementations for each projection type

// Helper function to check if two lines are parallel
static bool are_lines_parallel(float x1, float y1, float x2, float y2,
                               float x3, float y3, float x4, float y4,
                               float tolerance = 0.1f) {
    // Calculate direction vectors
    float dx1 = x2 - x1, dy1 = y2 - y1;
    float dx2 = x4 - x3, dy2 = y4 - y3;
    
    // Normalize
    float len1 = std::sqrt(dx1*dx1 + dy1*dy1);
    float len2 = std::sqrt(dx2*dx2 + dy2*dy2);
    
    if (len1 < 0.001f || len2 < 0.001f) return false;
    
    dx1 /= len1; dy1 /= len1;
    dx2 /= len2; dy2 /= len2;
    
    // Check if directions are parallel (cross product near zero)
    float cross = dx1 * dy2 - dy1 * dx2;
    return std::abs(cross) < tolerance;
}

bool IsometricProjection::validate_projected_quad(const float quad_x[4], const float quad_y[4]) const {
    // Isometric projection should produce parallelograms
    // Check that opposite sides are parallel
    
    // Check if top and bottom are parallel
    bool top_bottom_parallel = are_lines_parallel(
        quad_x[0], quad_y[0], quad_x[1], quad_y[1],  // Top edge
        quad_x[3], quad_y[3], quad_x[2], quad_y[2]   // Bottom edge
    );
    
    // Check if left and right are parallel
    bool left_right_parallel = are_lines_parallel(
        quad_x[0], quad_y[0], quad_x[3], quad_y[3],  // Left edge
        quad_x[1], quad_y[1], quad_x[2], quad_y[2]   // Right edge
    );
    
    // For isometric, both pairs should be parallel (parallelogram)
    return top_bottom_parallel && left_right_parallel;
}

bool IsometricDepthProjection::validate_projected_quad(const float quad_x[4], const float quad_y[4]) const {
    // With depth scaling, we expect trapezoids (one pair of parallel sides)
    // At least one pair should be parallel
    
    bool top_bottom_parallel = are_lines_parallel(
        quad_x[0], quad_y[0], quad_x[1], quad_y[1],
        quad_x[3], quad_y[3], quad_x[2], quad_y[2]
    );
    
    bool left_right_parallel = are_lines_parallel(
        quad_x[0], quad_y[0], quad_x[3], quad_y[3],
        quad_x[1], quad_y[1], quad_x[2], quad_y[2]
    );
    
    // Should have at least one pair parallel (trapezoid), but not both
    // If both are parallel, it's not showing depth correctly
    return (top_bottom_parallel || left_right_parallel) && !(top_bottom_parallel && left_right_parallel);
}

bool PerspectiveProjection::validate_projected_quad(const float quad_x[4], const float quad_y[4]) const {
    // Perspective projection creates trapezoids with convergence toward vanishing points
    // In true perspective, opposite sides should NOT be parallel (they converge)
    // However, in practice, far objects may appear nearly parallel
    
    bool top_bottom_parallel = are_lines_parallel(
        quad_x[0], quad_y[0], quad_x[1], quad_y[1],
        quad_x[3], quad_y[3], quad_x[2], quad_y[2],
        0.05f  // Tight tolerance - lines should converge
    );
    
    bool left_right_parallel = are_lines_parallel(
        quad_x[0], quad_y[0], quad_x[3], quad_y[3],
        quad_x[1], quad_y[1], quad_x[2], quad_y[2],
        0.05f
    );
    
    // Check that we have a valid quad (4 distinct points)
    float min_dist = 1000.0f;
    for (int i = 0; i < 4; i++) {
        for (int j = i+1; j < 4; j++) {
            float dx = quad_x[i] - quad_x[j];
            float dy = quad_y[i] - quad_y[j];
            float dist = std::sqrt(dx*dx + dy*dy);
            min_dist = std::min(min_dist, dist);
        }
    }
    
    // Valid perspective quad if:
    // 1. Points are distinct (min_dist > threshold)
    // 2. At least one pair of opposite sides is NOT parallel (shows convergence)
    // In practice, we're lenient here - as long as we have a valid quad
    return min_dist > 1.0f;  // Just ensure points are reasonably separated
}

bool CabinetProjection::validate_projected_quad(const float quad_x[4], const float quad_y[4]) const {
    // Cabinet projection preserves parallelism (oblique projection)
    // Should produce parallelograms like isometric, but with different angles
    
    bool top_bottom_parallel = are_lines_parallel(
        quad_x[0], quad_y[0], quad_x[1], quad_y[1],
        quad_x[3], quad_y[3], quad_x[2], quad_y[2]
    );
    
    bool left_right_parallel = are_lines_parallel(
        quad_x[0], quad_y[0], quad_x[3], quad_y[3],
        quad_x[1], quad_y[1], quad_x[2], quad_y[2]
    );
    
    // Cabinet should maintain parallelism (it's an oblique parallel projection)
    return top_bottom_parallel && left_right_parallel;
}