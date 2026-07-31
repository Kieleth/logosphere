#ifndef PIXEL_TO_LIGHT_STRATEGY_H
#define PIXEL_TO_LIGHT_STRATEGY_H

#include "lighting_config.h"
#include <vector>
#include <unordered_map>
#include <chrono>

// Forward declarations
class ParticleSystem;
class CameraSystem;
struct Particle;

// PixelToLightShadowStrategy - Simplest accurate shadow implementation
//
// Algorithm: For each pixel on screen, cast ONE ray to each light source
// This is the most straightforward approach to shadows:
// 1. Project each surface to screen space
// 2. For each pixel in the projected area
// 3. Cast a ray from that pixel's 3D position to each light
// 4. If ray hits something, pixel is shadowed
// 5. Otherwise, apply inverse square falloff
//
// Pros:
// - Pixel-perfect shadows
// - No precomputation needed
// - Easy to understand and debug
//
// Cons:
// - Expensive: O(pixels * lights * surfaces)
// - ~144ms for typical scene (10 lights, 100 surfaces)
// - No soft shadows (point lights only)

class PixelToLightStrategy : public ILightingStrategy {
public:
    PixelToLightStrategy();
    virtual ~PixelToLightStrategy() = default;
    
    // ILightingStrategy interface
    void initialize(int width, int height) override;
    
    void update_lighting(
        ParticleSystem& particle_system,
        const std::vector<int>& light_indices
    ) override;
    
    SurfaceColor get_surface_color_at(
        int particle_id,
        int surface_index, 
        float u, float v,
        float base_r, float base_g, float base_b
    ) const override;
    
    float get_last_update_ms() const override { return last_update_ms_; }
    std::string get_strategy_name() const override { return "PixelToLightShadow"; }
    
    void clear_cache() override;
    std::string get_debug_info() const override;
    
    // Set camera system for projections
    void set_camera_system(CameraSystem* camera) { camera_system_ = camera; }

private:
    // Cache structure for pixel lighting
    struct PixelLight {
        float r, g, b;        // Accumulated light color
        float intensity;      // Total light intensity
        bool computed;        // Has this pixel been computed?
    };
    
    // Surface lighting cache
    // Key: (particle_id << 16) | (surface_index << 8) | (pixel_index)
    // This allows us to cache per-pixel lighting results
    std::unordered_map<uint32_t, PixelLight> pixel_cache_;
    
    // Render dimensions
    int width_;
    int height_;
    
    // Performance tracking
    float last_update_ms_;
    int rays_cast_;
    int pixels_processed_;
    int shadows_found_;
    
    // System references
    CameraSystem* camera_system_;
    
    // Helper methods
    
    // Cast a ray from a 3D point to a light source
    // Returns true if ray reaches light (not blocked)
    bool cast_ray_to_light(
        float from_x, float from_y, float from_z,
        const Particle& light,
        ParticleSystem& particle_system,
        float& out_distance
    );
    
    // Check if a ray intersects any surface between two points
    bool ray_intersects_any_surface(
        float x1, float y1, float z1,
        float x2, float y2, float z2,
        ParticleSystem& particle_system,
        int exclude_particle_id = -1
    );
    
    // Calculate lighting for a single pixel
    PixelLight calculate_pixel_lighting(
        float world_x, float world_y, float world_z,
        float base_r, float base_g, float base_b,
        ParticleSystem& particle_system,
        const std::vector<int>& light_indices
    );
    
    // Generate cache key for a pixel
    uint32_t make_cache_key(int particle_id, int surface_index, int pixel_x, int pixel_y) const {
        return (particle_id << 20) | (surface_index << 16) | (pixel_y << 8) | pixel_x;
    }
};

#endif // PIXEL_TO_LIGHT_STRATEGY_H