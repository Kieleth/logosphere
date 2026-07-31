#ifndef VISION_SYSTEM_H
#define VISION_SYSTEM_H

#include "particle.h"
#include <vector>
#include <deque>

// EDUCATIONAL NOTE: The Vision System
// 
// This module handles all vision-related calculations for our engine:
// 1. Vision cone - what the character can see based on look direction
// 2. Binocular vision - using two "eyes" for better depth perception
// 3. Line-of-sight - checking if objects block vision
// 4. Spatial memory - remembering what we've seen
//
// Real games often use similar systems for:
// - Stealth mechanics (can enemies see the player?)
// - Fog of war (what areas have been explored?)
// - AI perception (what can NPCs perceive?)

// Vision system constants
const float VISION_CONE_ANGLE_DEG = 90.0f;  // Field of view in degrees
const float VISION_CONE_ANGLE_RAD = VISION_CONE_ANGLE_DEG * (3.14159f / 180.0f);  // Convert to radians
const float VISION_CONE_RANGE = 20.0f;  // How far we can see in world units
const float EYE_SEPARATION = 0.6f;  // Distance between "eyes" for binocular vision

// Forward declarations removed - Character struct eliminated

class VisionSystem {
public:
    // Constructor and destructor
    VisionSystem();
    ~VisionSystem();
    
    // Initialize the vision system
    void initialize();
    
    // Core vision functions
    bool is_particle_in_vision_cone(const Particle& particle, const Particle& viewer, float look_direction) const;
    bool has_line_of_sight(float from_x, float from_y, float to_x, float to_y, class ParticleSystem& particle_system) const;
    bool has_binocular_line_of_sight(float to_x, float to_y, const Particle& viewer, float look_direction, class ParticleSystem& particle_system) const;
    
    // Ray-particle intersection helpers
    bool ray_intersects_edge(float ray_x, float ray_y, float ray_dx, float ray_dy,
                           float edge_x1, float edge_y1, float edge_x2, float edge_y2,
                           float max_distance) const;
    bool particle_blocks_ray(const Particle& particle, 
                           float ray_x, float ray_y, 
                           float ray_dx, float ray_dy,
                           float max_distance) const;
    
    // View plane calculations (for oriented particles)
    ViewPlane calculate_view_plane(const Particle& particle, float viewer_x, float viewer_y) const;
    
    // BUG FIX: The Parameter That Wouldn't Die
    //
    // This method signature kept causing problems because it was taking
    // particles as a parameter while also trying to get them from
    // ParticleSystem internally. Classic case of "half-refactored" code.
    //
    // Rule I learned: When changing data sources, update ALL the interfaces
    // first, then the implementations. Otherwise you get these weird states
    // where half your code uses the old way and half uses the new way.
    
    // Light-based vision
    void update_vision_based_on_light(std::vector<ParticleLighting>& particle_lighting, class ParticleSystem& particle_system);
    
    // Spatial memory management
    void update_spatial_memory(double current_time, const Particle& viewer, float look_direction,
                             const std::vector<Particle>& visible_particles, class ParticleSystem& particle_system);
    const std::vector<MemoryParticle>& get_spatial_memory() const { return spatial_memory; }
    std::vector<MemoryParticle>& get_spatial_memory() { return spatial_memory; }
    
    // Debug rendering - uses IDrawSurface + CoordinateTransformer so it
    // doesn't depend on the full RenderSystem surface.
    void render_vision_cone_debug(const Particle& viewer, float look_direction,
                                  class IDrawSurface& surface,
                                  class CoordinateTransformer& coord,
                                  class ParticleSystem& particle_system) const;
    void render_binocular_vision_debug(const Particle& viewer, float look_direction,
                                       class IDrawSurface& surface,
                                       class CoordinateTransformer& coord,
                                       class ParticleSystem& particle_system) const;
    
    // Settings
    void set_show_vision_cone_debug(bool show) { show_vision_cone_debug = show; }
    void set_show_binocular_debug(bool show) { show_binocular_debug = show; }
    bool get_show_vision_cone_debug() const { return show_vision_cone_debug; }
    bool get_show_binocular_debug() const { return show_binocular_debug; }
    
private:
    // Helper functions
    float normalize_angle(float angle) const;
    float calculate_distance(float x1, float y1, float x2, float y2) const;
    void add_to_spatial_memory(const Particle& particle, double current_time);
    MemoryLayer get_memory_layer(const MemoryParticle& memory, double current_time) const;
    void apply_memory_decay(MemoryParticle& memory, double current_time);
    
    // Find the distance where a ray hits an obstacle
    float find_ray_hit_distance(float from_x, float from_y, float to_x, float to_y, class ParticleSystem& particle_system) const;
    
    // Spatial memory storage
    std::vector<MemoryParticle> spatial_memory;
    
    // Debug flags
    bool show_vision_cone_debug;
    bool show_binocular_debug;
};

#endif // VISION_SYSTEM_H