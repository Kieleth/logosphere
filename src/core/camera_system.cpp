#include "camera_system.h"
#include "particle.h"
#include "particle_geometry_v2.h"  // For Surface struct
#include "optimization_flags.h"    // For optimization flags
#include "frame_metrics.h"          // For performance counting
#include "simd_edge.h"              // For SIMD edge equation evaluation
#include "logosphere/rendering/rasterization_math.h"  // For 3D math primitives
#include <algorithm>  // For std::min/max
#include <cassert>    // For assert
#include <cmath>      // For std::abs, std::fabs
#include <iostream>   // For std::cout

// Thread-local storage for pixel UV metrics
thread_local CameraSystem::PixelUVMetrics g_camera_pixel_uv_metrics;

CameraSystem::CameraSystem() {
    // Initialize to default camera position and orientation
    position_x_ = 0.0f;
    position_y_ = 0.0f;
    position_z_ = 0.0f;

    // Default orientation: looking down positive Y axis (north)
    forward_x_ = 0.0f;
    forward_y_ = 1.0f;
    forward_z_ = 0.0f;

    // Default up direction: positive Z axis (up)
    up_x_ = 0.0f;
    up_y_ = 0.0f;
    up_z_ = 1.0f;

    // Calculate right vector
    calculate_right_vector();

    // Default projection system: isometric with its natural camera position
    projection_system_ = ProjectionFactory::create(ProjectionFactory::Type::Isometric);

    // Apply projection-specific camera defaults
    auto defaults = projection_system_->get_default_camera();
    position_x_ = defaults.position_x;
    position_y_ = defaults.position_y;
    position_z_ = defaults.position_z;
    look_at(defaults.look_at_x, defaults.look_at_y, defaults.look_at_z);
}

void CameraSystem::set_position(float x, float y, float z) {
    position_x_ = x;
    position_y_ = y;
    position_z_ = z;
}

void CameraSystem::get_position(float& x, float& y, float& z) const {
    x = position_x_;
    y = position_y_;
    z = position_z_;
}

void CameraSystem::set_camera_deadzone(float size) {
    camera_deadzone_size_ = size;
}

void CameraSystem::set_camera_follow_offset(float offset) {
    camera_follow_offset_ = offset;
}

void CameraSystem::update_follow_target(float target_x, float target_y, float target_z) {
    // Camera follows target with deadzone box
    // Target can move freely within deadzone_size box before camera moves
    // When target exits box, camera moves to keep target at box boundary
    //
    // ISOMETRIC OFFSET: In isometric projection, to move target vertically on screen
    // without horizontal shift, camera must be offset DIAGONALLY (equal X and Y).
    // Positive offset = camera southwest of target = target appears higher on screen.
    //
    // Math: iso_x = (view_x - view_y) * 0.866
    //       iso_y = (view_x + view_y) * 0.5 + view_z
    // For iso_x = 0: view_x = view_y (camera on diagonal)
    // For iso_y = 0 with view_z = -30: view_x = view_y = 30

    // Apply diagonal offset for isometric centering. The offset must
    // stay on the VIEW's diagonal, so under an azimuth orbit the
    // world-space direction rotates with the view (rotate the classic
    // (-1,-1) diagonal out of the view frame).
    float off_x = camera_follow_offset_, off_y = camera_follow_offset_;
    if (projection_system_) {
        const float a = projection_system_->get_view_azimuth();
        if (a != 0.0f) {
            const float c = std::cos(a), sn = std::sin(a);
            off_x = camera_follow_offset_ * (c - sn);
            off_y = camera_follow_offset_ * (sn + c);
        }
    }
    float effective_target_x = target_x - off_x;
    float effective_target_y = target_y - off_y;

    // Store for zoom centering (CameraController uses this)
    last_follow_target_x_ = effective_target_x;
    last_follow_target_y_ = effective_target_y;
    has_follow_target_ = true;

    // Store ACTUAL target position for shadow distance culling (no offset)
    look_at_target_x_ = target_x;
    look_at_target_y_ = target_y;

    if (camera_deadzone_size_ <= 0.0f) {
        // No deadzone - camera follows target immediately
        position_x_ = effective_target_x;
        position_y_ = effective_target_y;
        return;
    }

    // Calculate 2D offset from camera to effective target
    float offset_x = effective_target_x - position_x_;
    float offset_y = effective_target_y - position_y_;

    float half_deadzone = camera_deadzone_size_ / 2.0f;

    // Move camera only if target exceeds deadzone boundary
    if (offset_x > half_deadzone) {
        position_x_ = effective_target_x - half_deadzone;
    } else if (offset_x < -half_deadzone) {
        position_x_ = effective_target_x + half_deadzone;
    }

    if (offset_y > half_deadzone) {
        position_y_ = effective_target_y - half_deadzone;
    } else if (offset_y < -half_deadzone) {
        position_y_ = effective_target_y + half_deadzone;
    }
}

void CameraSystem::look_at(float target_x, float target_y, float target_z) {
    // Store look-at target for shadow distance culling
    look_at_target_x_ = target_x;
    look_at_target_y_ = target_y;

    // Calculate direction from camera to target
    forward_x_ = target_x - position_x_;
    forward_y_ = target_y - position_y_;
    forward_z_ = target_z - position_z_;

    normalize_vector(forward_x_, forward_y_, forward_z_);
    orthonormalize_vectors();
}

void CameraSystem::set_forward_direction(float x, float y, float z) {
    forward_x_ = x;
    forward_y_ = y;
    forward_z_ = z;
    
    normalize_vector(forward_x_, forward_y_, forward_z_);
    orthonormalize_vectors();
}

void CameraSystem::set_up_direction(float x, float y, float z) {
    up_x_ = x;
    up_y_ = y;
    up_z_ = z;
    
    normalize_vector(up_x_, up_y_, up_z_);
    orthonormalize_vectors();
}

void CameraSystem::rotate_yaw(float angle) {
    // Rotate around up vector
    float cos_a = cosf(angle);
    float sin_a = sinf(angle);
    
    float new_forward_x = forward_x_ * cos_a - right_x_ * sin_a;
    float new_forward_y = forward_y_ * cos_a - right_y_ * sin_a;
    float new_forward_z = forward_z_ * cos_a - right_z_ * sin_a;
    
    forward_x_ = new_forward_x;
    forward_y_ = new_forward_y;
    forward_z_ = new_forward_z;
    
    normalize_vector(forward_x_, forward_y_, forward_z_);
    calculate_right_vector();
}

void CameraSystem::rotate_pitch(float angle) {
    // Rotate around right vector
    float cos_a = cosf(angle);
    float sin_a = sinf(angle);
    
    float new_forward_x = forward_x_ * cos_a - up_x_ * sin_a;
    float new_forward_y = forward_y_ * cos_a - up_y_ * sin_a;
    float new_forward_z = forward_z_ * cos_a - up_z_ * sin_a;
    
    float new_up_x = forward_x_ * sin_a + up_x_ * cos_a;
    float new_up_y = forward_y_ * sin_a + up_y_ * cos_a;
    float new_up_z = forward_z_ * sin_a + up_z_ * cos_a;
    
    forward_x_ = new_forward_x;
    forward_y_ = new_forward_y;
    forward_z_ = new_forward_z;
    
    up_x_ = new_up_x;
    up_y_ = new_up_y;
    up_z_ = new_up_z;
    
    normalize_vector(forward_x_, forward_y_, forward_z_);
    normalize_vector(up_x_, up_y_, up_z_);
    calculate_right_vector();
}

void CameraSystem::rotate_roll(float angle) {
    // Rotate around forward vector
    float cos_a = cosf(angle);
    float sin_a = sinf(angle);
    
    float new_up_x = up_x_ * cos_a - right_x_ * sin_a;
    float new_up_y = up_y_ * cos_a - right_y_ * sin_a;
    float new_up_z = up_z_ * cos_a - right_z_ * sin_a;
    
    up_x_ = new_up_x;
    up_y_ = new_up_y;
    up_z_ = new_up_z;
    
    normalize_vector(up_x_, up_y_, up_z_);
    calculate_right_vector();
}

void CameraSystem::get_view_direction_to_point(float world_x, float world_y, float world_z,
                                              float view_direction[3]) const {
    // Calculate normalized direction from camera position to target point
    view_direction[0] = world_x - position_x_;
    view_direction[1] = world_y - position_y_;
    view_direction[2] = world_z - position_z_;

    // Normalize the direction vector using primitive
    float length = RasterizationMath::distance_3d(
        view_direction[0], view_direction[1], view_direction[2]);

    if (length > 0.001f) {
        view_direction[0] /= length;
        view_direction[1] /= length;
        view_direction[2] /= length;
    } else {
        // Fallback: use camera's forward direction
        view_direction[0] = forward_x_;
        view_direction[1] = forward_y_;
        view_direction[2] = forward_z_;
    }
}

void CameraSystem::get_forward_vector(float& x, float& y, float& z) const {
    x = forward_x_;
    y = forward_y_;
    z = forward_z_;
}

void CameraSystem::get_up_vector(float& x, float& y, float& z) const {
    x = up_x_;
    y = up_y_;
    z = up_z_;
}

void CameraSystem::get_right_vector(float& x, float& y, float& z) const {
    x = right_x_;
    y = right_y_;
    z = right_z_;
}

void CameraSystem::normalize_vector(float& x, float& y, float& z) {
    float length = sqrtf(x * x + y * y + z * z);
    if (length > 0.001f) {
        x /= length;
        y /= length;
        z /= length;
    }
}

void CameraSystem::calculate_right_vector() {
    // right = forward × up (cross product)
    right_x_ = forward_y_ * up_z_ - forward_z_ * up_y_;
    right_y_ = forward_z_ * up_x_ - forward_x_ * up_z_;
    right_z_ = forward_x_ * up_y_ - forward_y_ * up_x_;
    
    normalize_vector(right_x_, right_y_, right_z_);
}

void CameraSystem::orthonormalize_vectors() {
    // Ensure forward is normalized
    normalize_vector(forward_x_, forward_y_, forward_z_);
    
    // Calculate right = forward × up
    calculate_right_vector();
    
    // Recalculate up = right × forward to ensure orthogonality
    up_x_ = right_y_ * forward_z_ - right_z_ * forward_y_;
    up_y_ = right_z_ * forward_x_ - right_x_ * forward_z_;
    up_z_ = right_x_ * forward_y_ - right_y_ * forward_x_;
    
    normalize_vector(up_x_, up_y_, up_z_);
}

void CameraSystem::set_viewport(int width, int height) {
    viewport_width_ = width;
    viewport_height_ = height;
}

void CameraSystem::set_projection_system(std::unique_ptr<ProjectionSystem> projection) {
    projection_system_ = std::move(projection);

    // Auto-configure camera for this projection type
    auto defaults = projection_system_->get_default_camera();
    set_position(defaults.position_x, defaults.position_y, defaults.position_z);
    look_at(defaults.look_at_x, defaults.look_at_y, defaults.look_at_z);
}

void CameraSystem::set_pixels_per_unit(float pixels) {
    pixels_per_unit_ = pixels;
}

void CameraSystem::adjust_zoom(float delta) {
    // Zoom by adjusting pixels_per_unit
    // Positive delta = zoom in (more pixels per unit = objects appear larger)
    // Negative delta = zoom out (fewer pixels per unit = objects appear smaller)
    pixels_per_unit_ += delta;

    // Clamp to reasonable range (prevent inversion or excessive zoom)
    const float min_zoom = 5.0f;   // Minimum zoom out
    const float max_zoom = 200.0f; // Maximum zoom in
    if (pixels_per_unit_ < min_zoom) pixels_per_unit_ = min_zoom;
    if (pixels_per_unit_ > max_zoom) pixels_per_unit_ = max_zoom;
}

void CameraSystem::world_to_screen(
    float world_x, float world_y, float world_z,    // INPUT: WORLD SPACE (X=East, Y=North, Z=Up)
    int& screen_x, int& screen_y) const {           // OUTPUT: SCREEN SPACE (pixels, origin top-left)

    // Projection system must always be set (initialized in constructor)
    assert(projection_system_ && "CameraSystem requires a projection system");

    // COORDINATE TRANSFORMATION PIPELINE:
    // World Space → View Space → Projection Space → Screen Space
    // This is handled by the projection system (isometric/perspective/cabinet)
    projection_system_->project(
        world_x, world_y, world_z,                    // World position to project
        position_x_, position_y_, position_z_,        // Camera position in world
        viewport_width_, viewport_height_,            // Screen dimensions
        pixels_per_unit_,                             // World-to-pixel scale
        screen_x, screen_y);                          // Result in screen pixels
}

float CameraSystem::compute_depth(float world_x, float world_y, float world_z) const {
    assert(projection_system_ && "CameraSystem requires a projection system");
    return projection_system_->compute_depth(world_x, world_y, world_z,
                                             position_x_, position_y_, position_z_);
}

CameraSystem::ProjectedTriangle CameraSystem::project_triangle(const float world_vertices[3][3]) const {
    // THREAD SAFETY: Validate inputs before dereferencing
    // Worker threads call this method, so we need defensive checks
    if (!projection_system_) {
        std::cerr << "[CAMERA FATAL] projection_system_ is null! this="
                  << static_cast<const void*>(this) << std::endl;
        std::abort();
    }
    if (!world_vertices) {
        std::cerr << "[CAMERA FATAL] world_vertices is null! this="
                  << static_cast<const void*>(this) << std::endl;
        std::abort();
    }

    ProjectedTriangle tri;
    tri.on_screen = true;

    // Copy world vertices and project to screen
    for (int i = 0; i < 3; ++i) {
        tri.world_corners[i][0] = world_vertices[i][0];
        tri.world_corners[i][1] = world_vertices[i][1];
        tri.world_corners[i][2] = world_vertices[i][2];

        // Project to screen space
        world_to_screen(tri.world_corners[i][0],
                       tri.world_corners[i][1],
                       tri.world_corners[i][2],
                       tri.screen_corners[i][0],
                       tri.screen_corners[i][1]);
    }
    
    // Compute bounding box using primitive
    float verts_2d[3][2];
    for (int i = 0; i < 3; ++i) {
        verts_2d[i][0] = tri.screen_corners[i][0];
        verts_2d[i][1] = tri.screen_corners[i][1];
    }
    RasterizationMath::compute_bbox_2d(verts_2d, tri.min_x, tri.max_x, tri.min_y, tri.max_y);
    
    // Compute edge equations using primitives
    if (Optimizations::USE_INCREMENTAL_EDGES) {
        for (int i = 0; i < 3; ++i) {
            int j = (i + 1) % 3;  // Next vertex (wraps around)
            tri.edges[i].setup(
                tri.screen_corners[i][0], tri.screen_corners[i][1],
                tri.screen_corners[j][0], tri.screen_corners[j][1]
            );
        }
        tri.edges_computed = true;
    } else {
        tri.edges_computed = false;
    }
    
    // Check if on screen (with margin for large triangles extending off-screen)
    // Large margin ensures triangles partially on-screen aren't incorrectly culled
    const int margin = static_cast<int>(pixels_per_unit_ * 5.0f);  // 5 world units margin
    if (tri.max_x < -margin || tri.min_x >= viewport_width_ + margin ||
        tri.max_y < -margin || tri.min_y >= viewport_height_ + margin) {
        tri.on_screen = false;
    }
    
    return tri;
}



bool CameraSystem::pixel_to_triangle_uv(int pixel_x, int pixel_y,
                                        const ProjectedTriangle& tri,
                                        float& out_u, float& out_v) const {
    // NO COUNTING IN HOT PATH - this is called for every pixel!
    
    
    // Early rejection - bounding box test
    if (pixel_x < tri.min_x || pixel_x > tri.max_x || 
        pixel_y < tri.min_y || pixel_y > tri.max_y) {
        return false;
    }

    // Extract screen coordinates
    float x0 = tri.screen_corners[0][0];
    float y0 = tri.screen_corners[0][1];
    float x1 = tri.screen_corners[1][0];
    float y1 = tri.screen_corners[1][1];
    float x2 = tri.screen_corners[2][0];
    float y2 = tri.screen_corners[2][1];

    // Point-in-triangle test using primitive
    if (!RasterizationMath::point_in_triangle_2d(
            pixel_x, pixel_y, x0, y0, x1, y1, x2, y2)) {
        return false;
    }

    // Compute barycentric coordinates using primitive
    float w0, w1, w2;
    RasterizationMath::barycentric_coords_2d(
        pixel_x, pixel_y, x0, y0, x1, y1, x2, y2, w0, w1, w2);

    // Convert barycentric to UV (standard mapping)
    out_u = w1;
    out_v = w2;

    return true;
}

// =========================================================================
// UNIFIED CULLING SYSTEM IMPLEMENTATION
// =========================================================================
// This system determines which surfaces should be rendered based on their
// position, orientation, and relationship to the camera.
//
// CULLING TYPES EXPLAINED:
//
// 1. BACKFACE CULLING (Implemented)
//    - Principle: Closed convex objects (like cubes) have surfaces that face
//      either toward or away from the viewer
//    - Logic: If surface normal points away from camera, it's on the back
//    - Math: dot(normal, view_direction) > 0 means facing away (cull it)
//    - Assumptions: 
//      * Particles are convex closed volumes
//      * Normals point outward from volume center
//      * Camera is outside the volume
//
// 2. FRUSTUM CULLING (TODO)
//    - Principle: Only render what's in the camera's view pyramid
//    - Logic: Test if surface bounding box intersects view frustum
//    - Benefit: Skip surfaces that are off-screen
//
// 3. OCCLUSION CULLING (TODO)
//    - Principle: Don't render surfaces completely hidden by others
//    - Logic: Use z-buffer or hierarchical occlusion maps
//    - Benefit: Skip interior surfaces in complex scenes
//
// 4. DISTANCE/LOD CULLING (TODO)
//    - Principle: Simplify or skip very distant surfaces
//    - Logic: Use distance from camera to choose detail level
//    - Benefit: Better performance for large worlds
// =========================================================================

bool CameraSystem::should_render_surface(float surface_x, float surface_y, float surface_z,
                                         float normal_x, float normal_y, float normal_z,
                                         float width, float height) const {
    // Run through all culling tests
    // Early exit on first cull (all tests must pass to render)
    
    // 1. BACKFACE CULLING - Most common cull, check first
    if (!passes_backface_culling(surface_x, surface_y, surface_z, 
                                 normal_x, normal_y, normal_z)) {
        return false;  // Surface faces away, cull it
    }
    
    // 2. FRUSTUM CULLING - TODO
    // if (!passes_frustum_culling(surface_x, surface_y, surface_z, width, height)) {
    //     return false;  // Surface outside view, cull it
    // }
    
    // 3. DISTANCE CULLING - TODO
    // if (!passes_distance_culling(surface_x, surface_y, surface_z)) {
    //     return false;  // Surface too far, cull it
    // }
    
    // Surface passed all culling tests, render it
    return true;
}

bool CameraSystem::passes_backface_culling(float surface_x, float surface_y, float surface_z,
                                           float normal_x, float normal_y, float normal_z) const {
    // PROJECTION-AWARE BACKFACE CULLING
    // Different projection types need different view direction calculations:
    //
    // PARALLEL PROJECTIONS (Isometric, Cabinet, Bird's Eye):
    //   - All view rays are parallel (infinite distance viewer)
    //   - View direction is FIXED (the camera's forward vector)
    //   - Camera position only controls which part of world is centered on screen
    //   - Use forward_x_, forward_y_, forward_z_ for culling
    //
    // POINT PROJECTIONS (Perspective):
    //   - View rays emanate from camera point
    //   - View direction is DIFFERENT for each surface
    //   - Calculate view direction as: surface_pos - camera_pos

    float view_x, view_y, view_z;

    if (projection_system_ && projection_system_->is_parallel_projection()) {
        // Parallel projection: use fixed viewing direction
        view_x = forward_x_;
        view_y = forward_y_;
        view_z = forward_z_;
        // Forward vector is already normalized
    } else {
        // Point projection: calculate per-surface view direction
        view_x = surface_x - position_x_;
        view_y = surface_y - position_y_;
        view_z = surface_z - position_z_;

        // Normalize view direction
        float length = RasterizationMath::distance_3d(view_x, view_y, view_z);
        if (length < 0.001f) {
            return true;  // Surface at camera, render to be safe
        }
        view_x /= length;
        view_y /= length;
        view_z /= length;
    }

    // Calculate dot product between surface normal and view direction
    // Both vectors are normalized (normals should already be unit length)
    float dot = normal_x * view_x + normal_y * view_y + normal_z * view_z;

    // BACKFACE CULLING LOGIC:
    // - If dot < 0: Normal points toward camera (angle < 90°) → RENDER
    // - If dot > 0: Normal points away from camera (angle > 90°) → CULL
    // - If dot = 0: Normal perpendicular to view (edge-on) → RENDER (to be safe)

    return dot <= 0.0f;  // True if should render, false if should cull
}

bool CameraSystem::passes_frustum_culling(float surface_x, float surface_y, float surface_z,
                                          float width, float height) const {
    // TODO: Implement frustum culling
    // For now, always pass (no frustum culling)
    return true;
}

bool CameraSystem::passes_distance_culling(float surface_x, float surface_y, float surface_z) const {
    // TODO: Implement distance-based LOD culling
    // For now, always pass (no distance culling)
    return true;
}

CameraSystem::PixelUVMetrics CameraSystem::get_pixel_uv_metrics() const {
    return g_camera_pixel_uv_metrics;
}

void CameraSystem::reset_frame_metrics() {
    g_camera_pixel_uv_metrics.reset();
}

// === Velocity Tracking (Phase 3: Camera Prediction for Chunk Pre-loading) ===

void CameraSystem::update_velocity(float dt) {
    if (!velocity_initialized_) {
        // First frame: just store position, no velocity yet
        prev_position_x_ = position_x_;
        prev_position_y_ = position_y_;
        velocity_initialized_ = true;
        return;
    }

    if (dt <= 0.0f) return;  // Invalid dt

    // Compute velocity from position change
    float dx = position_x_ - prev_position_x_;
    float dy = position_y_ - prev_position_y_;

    // Smooth velocity with exponential moving average (avoid jitter)
    const float smoothing = 0.3f;  // Lower = smoother, higher = more responsive
    velocity_x_ = velocity_x_ * (1.0f - smoothing) + (dx / dt) * smoothing;
    velocity_y_ = velocity_y_ * (1.0f - smoothing) + (dy / dt) * smoothing;

    // Store current position for next frame
    prev_position_x_ = position_x_;
    prev_position_y_ = position_y_;
}

float CameraSystem::get_velocity_magnitude() const {
    return std::sqrt(velocity_x_ * velocity_x_ + velocity_y_ * velocity_y_);
}