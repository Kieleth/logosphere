#ifndef COORDINATE_TRANSFORMER_H
#define COORDINATE_TRANSFORMER_H

#include <cstdint>

// Forward declarations
class CameraSystem;
class ProjectionSystem;
enum class ProjectionMode;

/**
 * CoordinateTransformer - Handle all coordinate space conversions
 * 
 * Following KISS & UNIX philosophy: This class does ONE thing well - coordinate transformations.
 * It encapsulates all the math for converting between world, screen, and framebuffer coordinates.
 * 
 * Part of the rendering system refactoring to break down the monolithic RenderSystem.
 * Each coordinate system transformation is a small, focused method that can be composed.
 * 
 * Historical note: Coordinate transformation is fundamental to 3D graphics.
 * Early games like Elite (1984) had to do these calculations in assembly for speed.
 */
class CoordinateTransformer {
public:
    CoordinateTransformer();
    ~CoordinateTransformer() = default;
    
    // Configuration
    void set_viewport(int width, int height);
    void set_camera_system(CameraSystem* camera) { camera_system_ = camera; }
    void set_pixels_per_unit(float ppu) { pixels_per_unit_ = ppu; }
    void set_dpi_scale(float scale) { dpi_scale_ = scale; }
    void set_projection_mode(ProjectionMode mode) { projection_mode_ = mode; }
    
    // Forward transformations (3D world to 2D screen)
    void world_to_screen(float world_x, float world_y, 
                        int& screen_x, int& screen_y) const;
    void world_to_screen_3d(float world_x, float world_y, float world_z, 
                           int& screen_x, int& screen_y) const;
    
    // Inverse transformations (2D screen to 3D world)
    void screen_to_world(int screen_x, int screen_y,
                        float& world_x, float& world_y) const;

    // Height-aware inverse: convert screen position at specific world Z height
    // Use this when picking objects at known heights (e.g., head tracking at head.z)
    void screen_to_world_at_z(int screen_x, int screen_y, float world_z,
                              float& world_x, float& world_y) const;
    
    // Projection-specific inverse transformations
    // These handle the math for each projection type's inverse
    void screen_to_world_isometric(int screen_x, int screen_y, 
                                   float& world_x, float& world_y) const;
    void screen_to_world_perspective(int screen_x, int screen_y, 
                                     float& world_x, float& world_y) const;
    void screen_to_world_cabinet(int screen_x, int screen_y, 
                                 float& world_x, float& world_y) const;
    
    // Mouse/window coordinate handling
    // Handles DPI scaling and window-to-framebuffer conversion
    void mouse_to_framebuffer(double mouse_x, double mouse_y, 
                             int& fb_x, int& fb_y) const;
    
    // Utility methods
    int get_viewport_width() const { return viewport_width_; }
    int get_viewport_height() const { return viewport_height_; }
    float get_pixels_per_unit() const { return pixels_per_unit_; }
    
private:
    // Viewport dimensions
    int viewport_width_;
    int viewport_height_;
    
    // Scale factor for world-to-pixel conversion
    float pixels_per_unit_;
    
    // Camera system for projection calculations
    CameraSystem* camera_system_;
    
    // Current projection mode
    ProjectionMode projection_mode_;
    
    // DPI scale for high-resolution displays
    float dpi_scale_;
};

#endif // COORDINATE_TRANSFORMER_H