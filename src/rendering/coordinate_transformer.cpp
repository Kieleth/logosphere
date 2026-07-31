#include "logosphere/rendering/coordinate_transformer.h"
#include "../core/projection_mode.h"
#include "../core/camera_system.h"
#include "../projection_system.h"
#include "../lighting_config.h"
#include <cmath>
#include <cassert>
#include <cstring>
#include <string>
#include <iostream>

// Constructor
CoordinateTransformer::CoordinateTransformer()
    : viewport_width_(800)
    , viewport_height_(600)
    , pixels_per_unit_(20.0f)
    , camera_system_(nullptr)
    , projection_mode_(ProjectionMode::Isometric)
    , dpi_scale_(1.0f) {
}

// Set viewport dimensions
void CoordinateTransformer::set_viewport(int width, int height) {
    viewport_width_ = width;
    viewport_height_ = height;
    // Update DPI scale based on viewport size
    // This would be properly calculated from window vs framebuffer size
    // For now, assume 1.0 (will be set properly by RenderSystem)
    dpi_scale_ = 1.0f;
}

// 2D world to screen (delegates to 3D version with z=0)
void CoordinateTransformer::world_to_screen(float world_x, float world_y, 
                                           int& screen_x, int& screen_y) const {
    // For 2D compatibility, just call 3D version with z=0
    world_to_screen_3d(world_x, world_y, 0.0f, screen_x, screen_y);
}

// 3D world to screen transformation
void CoordinateTransformer::world_to_screen_3d(float world_x, float world_y, float world_z,
                                              int& screen_x, int& screen_y) const {
    // COORDINATE TRANSFORMATION: World Space → Screen Space
    // This delegates to CameraSystem which handles the full transformation pipeline
    
    assert(camera_system_ && "CoordinateTransformer requires camera_system_ to be initialized");
    
    // Delegate to CameraSystem which handles projection
    camera_system_->world_to_screen(world_x, world_y, world_z, screen_x, screen_y);
}

// Screen to world - delegates based on current projection mode
void CoordinateTransformer::screen_to_world(int screen_x, int screen_y,
                                           float& world_x, float& world_y) const {
    // Delegate based on projection mode
    switch (projection_mode_) {
        case ProjectionMode::Isometric:
        case ProjectionMode::IsometricWithDepth:
            screen_to_world_isometric(screen_x, screen_y, world_x, world_y);
            break;
        case ProjectionMode::Perspective:
            screen_to_world_perspective(screen_x, screen_y, world_x, world_y);
            break;
        case ProjectionMode::Cabinet:
            screen_to_world_cabinet(screen_x, screen_y, world_x, world_y);
            break;
        default:
            // Fallback to perspective
            screen_to_world_perspective(screen_x, screen_y, world_x, world_y);
            break;
    }
}

// Height-aware inverse: convert screen position at specific world Z height
void CoordinateTransformer::screen_to_world_at_z(int screen_x, int screen_y, float world_z,
                                                  float& world_x, float& world_y) const {
    // Currently only isometric supports height-aware conversion
    // Other projections can be added if needed

    float center_x = viewport_width_ / 2.0f;
    float center_y = viewport_height_ / 2.0f;

    // Get camera position and CURRENT zoom level
    float cam_x = 0, cam_y = 0, cam_z = 0;
    float current_ppu = pixels_per_unit_;  // Fallback
    if (camera_system_) {
        camera_system_->get_position(cam_x, cam_y, cam_z);
        current_ppu = camera_system_->get_pixels_per_unit();  // Use LIVE zoom value
    }

    // Convert screen to iso coordinates
    float iso_x = (static_cast<float>(screen_x) - center_x) / current_ppu;
    float iso_y = (center_y - static_cast<float>(screen_y)) / current_ppu;

    // Use provided world_z instead of assuming Z=0
    // view_z = world_z - cam_z
    float view_z = world_z - cam_z;

    // The forward transform is: iso_y = (view_x + view_y) * 0.5 + view_z
    // So: iso_y_corrected = iso_y - view_z = (view_x + view_y) * 0.5
    float iso_y_corrected = iso_y - view_z;

    // Now solve the system:
    // iso_x = (view_x - view_y) * 0.866
    // iso_y_corrected = (view_x + view_y) * 0.5
    float view_x = 0.5f * (iso_x / 0.866f + iso_y_corrected / 0.5f);
    float view_y = 0.5f * (iso_y_corrected / 0.5f - iso_x / 0.866f);

    // Convert from view space to world space
    world_x = view_x + cam_x;
    world_y = view_y + cam_y;
}

// Inverse isometric projection
void CoordinateTransformer::screen_to_world_isometric(int screen_x, int screen_y,
                                                     float& world_x, float& world_y) const {
    // INFINITE WORLD COORDINATE TRANSFORMATION
    // The viewport defines the zoom level, not the world bounds
    // Any screen position maps to a valid world coordinate
    
    // The forward transformation from projection_system.cpp is:
    // iso_x = (view_x - view_y) * 0.866f
    // iso_y = (view_x + view_y) * 0.5f + view_z
    // screen_x = center_x + iso_x * pixels_per_unit
    // screen_y = center_y - iso_y * pixels_per_unit
    
    float center_x = viewport_width_ / 2.0f;
    float center_y = viewport_height_ / 2.0f;

    // Get camera position and CURRENT zoom level (pixels_per_unit changes with zoom!)
    float cam_x = 0, cam_y = 0, cam_z = 0;
    float current_ppu = pixels_per_unit_;  // Fallback
    if (camera_system_) {
        camera_system_->get_position(cam_x, cam_y, cam_z);
        current_ppu = camera_system_->get_pixels_per_unit();  // Use LIVE zoom value
    }

    // Convert screen to iso coordinates (reverse the screen transform)
    // Simple linear transformation without any acceleration or dynamic scaling
    float iso_x = (static_cast<float>(screen_x) - center_x) / current_ppu;
    float iso_y = (center_y - static_cast<float>(screen_y)) / current_ppu;
    
    // For mouse picking, we assume world_z = 0 (ground plane)
    // So view_z = 0 - cam_z = -cam_z
    // The forward transform adds view_z to iso_y, so we subtract it in the inverse
    float iso_y_corrected = iso_y - (-cam_z);
    
    // Now solve the system of equations:
    // iso_x = (view_x - view_y) * 0.866
    // iso_y_corrected = (view_x + view_y) * 0.5
    //
    // From equation 1: (view_x - view_y) = iso_x / 0.866
    // From equation 2: (view_x + view_y) = iso_y_corrected / 0.5
    //
    // Adding: 2 * view_x = iso_x / 0.866 + iso_y_corrected / 0.5
    // Subtracting: 2 * view_y = iso_y_corrected / 0.5 - iso_x / 0.866
    
    float view_x = 0.5f * (iso_x / 0.866f + iso_y_corrected / 0.5f);
    float view_y = 0.5f * (iso_y_corrected / 0.5f - iso_x / 0.866f);
    
    // Convert from view space to world space - infinite world, no clamping
    world_x = view_x + cam_x;
    world_y = view_y + cam_y;
}

// Inverse perspective projection (approximation)
void CoordinateTransformer::screen_to_world_perspective(int screen_x, int screen_y,
                                                       float& world_x, float& world_y) const {
    // INVERSE PERSPECTIVE PROJECTION (APPROXIMATION)
    // True perspective projection cannot be perfectly inverted for mouse picking
    // because perspective division loses depth information.
    // This is a best-effort approximation assuming world_z=0.
    
    float center_x = viewport_width_ / 2.0f;
    float center_y = viewport_height_ / 2.0f;

    // Get camera position and CURRENT zoom level
    float cam_x = 0, cam_y = 0, cam_z = 0;
    float scale = pixels_per_unit_;  // Fallback
    if (camera_system_) {
        camera_system_->get_position(cam_x, cam_y, cam_z);
        scale = camera_system_->get_pixels_per_unit();  // Use LIVE zoom value
    }

    float rel_screen_x = screen_x - center_x;
    float rel_screen_y = screen_y - center_y;

    // Approximate inverse assuming z=0 and average depth
    float camera_distance = 50.0f;
    float focal_length = 35.0f;
    float avg_depth = camera_distance;

    // Inverse perspective division
    float persp_scale = focal_length / avg_depth;
    float view_x = rel_screen_x / (persp_scale * scale);
    float view_z = -rel_screen_y / (persp_scale * scale);
    
    // Approximation: assume view_y = 0 for simplicity at z=0
    float rotated_x = view_x;
    float rotated_y = 0.0f;
    
    // Undo the 45° rotation from forward transform
    float relative_x = rotated_x * 0.707f - rotated_y * 0.707f;
    float relative_y = rotated_x * 0.707f + rotated_y * 0.707f;
    
    // Add camera position
    world_x = relative_x + cam_x;
    world_y = relative_y + cam_y;
}

// Inverse cabinet projection
void CoordinateTransformer::screen_to_world_cabinet(int screen_x, int screen_y,
                                                   float& world_x, float& world_y) const {
    // INVERSE CABINET PROJECTION
    // Cabinet projection forward transformation (at z=0):
    //   screen_x = center_x + (relative_x + relative_y) * scale * 0.707
    //   screen_y = center_y + (relative_x - relative_y) * scale * 0.5
    
    float center_x = viewport_width_ / 2.0f;
    float center_y = viewport_height_ / 2.0f;

    // Get camera position and CURRENT zoom level
    float cam_x = 0, cam_y = 0, cam_z = 0;
    float scale = pixels_per_unit_;  // Fallback
    if (camera_system_) {
        camera_system_->get_position(cam_x, cam_y, cam_z);
        scale = camera_system_->get_pixels_per_unit();  // Use LIVE zoom value
    }

    float rel_screen_x = screen_x - center_x;
    float rel_screen_y = screen_y - center_y;

    // Account for Z component
    float relative_z_at_world_0 = -cam_z;
    float rel_screen_y_corrected = rel_screen_y + relative_z_at_world_0 * scale;
    
    // Invert the cabinet transformation
    float A = rel_screen_x / (scale * 0.707f);
    float B = rel_screen_y_corrected / (scale * 0.5f);
    
    float relative_x = (A + B) / 2.0f;
    float relative_y = (A - B) / 2.0f;
    
    // Add camera position
    world_x = relative_x + cam_x;
    world_y = relative_y + cam_y;
}

// Mouse to framebuffer coordinate conversion (handles DPI scaling)
void CoordinateTransformer::mouse_to_framebuffer(double mouse_x, double mouse_y,
                                                int& fb_x, int& fb_y) const {
    // MOUSE COORDINATE HANDLING WITH DPI SCALING
    // On high-DPI displays (like Retina), the framebuffer resolution may be
    // different from the window coordinates. We need to scale accordingly.

    // For now, assume DPI scale is determined by viewport vs window size ratio
    // In a real implementation, this would come from the window system
    // Since we don't have access to window size here, we'll use dpi_scale_ member

    // Scale mouse coordinates to framebuffer space
    fb_x = static_cast<int>(mouse_x * dpi_scale_);
    fb_y = static_cast<int>(mouse_y * dpi_scale_);

    // BUG FIX: Clamp to FRAMEBUFFER bounds (viewport * DPI), not viewport bounds
    // The viewport is render resolution (e.g., 1600x1051)
    // After DPI scaling, we're in framebuffer space (e.g., 3200x2102)
    int fb_width = static_cast<int>(viewport_width_ * dpi_scale_);
    int fb_height = static_cast<int>(viewport_height_ * dpi_scale_);

    if (fb_x < 0) fb_x = 0;
    if (fb_y < 0) fb_y = 0;
    if (fb_x >= fb_width) fb_x = fb_width - 1;
    if (fb_y >= fb_height) fb_y = fb_height - 1;
}