#include "vision_system.h"
#include "logosphere/rendering/i_draw_surface.h"
#include "logosphere/rendering/coordinate_transformer.h"
#include "core/particle_system.h"
#include <cmath>
#include <algorithm>
#include <iostream>

// EDUCATIONAL NOTE: Vision System Implementation
//
// This file implements the vision calculations that determine what the player can see.
// The system combines several concepts:
// 1. Field of View (FOV) - angular range the character can see
// 2. Line of Sight (LOS) - whether vision is blocked by obstacles
// 3. Binocular vision - using two viewpoints for better perception
// 4. Spatial memory - remembering previously seen objects

// REFACTORING LOG: Another Global Vector Bites the Dust
//
// Found another place using the global particles vector. VisionSystem
// was checking the old global vector for line-of-sight calculations.
// No wonder vision wasn't working properly after refactoring!
//
// I'm starting to understand why experienced C++ developers hate globals.
// Every time you think you've found them all, another one pops up.
// It's like playing whack-a-mole with technical debt.
//
// Now using ParticleSystem as the single source of truth.
#include "core/particle_system.h"
// Note: framebuffer access now through g_render_system.get_framebuffer()

// EDUCATIONAL NOTE: Getting Window Dimensions
//
// Instead of hardcoding window dimensions, we get them from RenderSystem.
// This centralizes configuration and makes it easier to change window size.

VisionSystem::VisionSystem() : show_vision_cone_debug(false), show_binocular_debug(false) {
    // Reserve space for spatial memory
    spatial_memory.reserve(1000);
}

VisionSystem::~VisionSystem() {
    // Destructor - vectors clean themselves up
}

void VisionSystem::initialize() {
    // Clear spatial memory on initialization
    spatial_memory.clear();
    show_vision_cone_debug = false;
    show_binocular_debug = false;
}

float VisionSystem::normalize_angle(float angle) const {
    // Normalize angle to be between -π and +π
    // This handles angle wrapping for proper cone calculations
    
    const float PI = 3.14159f;
    
    // Wrap angle to [-2π, +2π] range first
    while (angle > PI) angle -= 2 * PI;
    while (angle < -PI) angle += 2 * PI;
    
    return angle;
}

float VisionSystem::calculate_distance(float x1, float y1, float x2, float y2) const {
    // Calculate Euclidean distance between two points
    float dx = x2 - x1;
    float dy = y2 - y1;
    return sqrtf(dx * dx + dy * dy);
}

bool VisionSystem::is_particle_in_vision_cone(const Particle& particle, const Particle& viewer, float look_direction) const {
    // Check if a particle is within the viewer's field of view
    // Returns true if particle should be visible, false if hidden
    
    // Calculate distance from viewer to particle
    float distance = calculate_distance(
        viewer.x, viewer.y,
        particle.x, particle.y
    );
    
    // First check: Is particle within vision range?
    if (distance > VISION_CONE_RANGE) {
        return false;  // Too far away to see
    }
    
    // Second check: Is particle within the vision cone angle?
    
    // Calculate angle from viewer to particle
    float dx = particle.x - viewer.x;
    float dy = particle.y - viewer.y;
    float angle_to_particle = atan2f(dy, dx);
    
    // Calculate the angular difference between look direction and particle direction
    float angle_difference = normalize_angle(angle_to_particle - look_direction);
    
    // Check if this difference is within half of our vision cone angle
    // VISION_CONE_ANGLE_RAD is the total cone angle, so we use half for each side
    float half_cone_angle = VISION_CONE_ANGLE_RAD / 2.0f;
    
    if (fabsf(angle_difference) <= half_cone_angle) {
        return true;  // Particle is within the vision cone
    }
    
    return false;  // Particle is outside the vision cone
}

bool VisionSystem::ray_intersects_edge(float ray_x, float ray_y, float ray_dx, float ray_dy,
                                      float edge_x1, float edge_y1, float edge_x2, float edge_y2,
                                      float max_distance) const {
    // Ray-line-segment intersection
    // Using parametric form: intersection = ray_start + t * ray_direction
    
    // Line segment vector
    float seg_dx = edge_x2 - edge_x1;
    float seg_dy = edge_y2 - edge_y1;
    
    // Ray starting from viewer position
    float start_dx = edge_x1 - ray_x;
    float start_dy = edge_y1 - ray_y;
    
    // Solve for intersection using cross products
    float denom = ray_dx * seg_dy - ray_dy * seg_dx;
    
    // Parallel check - if ray and edge are parallel, no intersection
    if (fabsf(denom) < 0.0001f) return false;
    
    // Calculate intersection parameters
    float t = (start_dx * seg_dy - start_dy * seg_dx) / denom;  // Ray parameter
    float u = (start_dx * ray_dy - start_dy * ray_dx) / denom;  // Segment parameter
    
    // Check if intersection is within bounds
    // t > 0: intersection is in front of ray origin
    // t < max_distance: intersection is within our vision range
    // 0 <= u <= 1: intersection point is on the line segment
    // Add small epsilon to avoid numerical precision issues
    const float epsilon = 0.01f;
    return t > epsilon && t < max_distance && u >= -epsilon && u <= 1.0f + epsilon;
}

bool VisionSystem::particle_blocks_ray(const Particle& particle, 
                                     float ray_x, float ray_y, 
                                     float ray_dx, float ray_dy,
                                     float max_distance) const {
    float half_size = particle.size / 2.0f;
    
    // Quick bounds check - if ray doesn't even go near particle, skip edge checks
    float to_particle_x = particle.x - ray_x;
    float to_particle_y = particle.y - ray_y;
    float dot = to_particle_x * ray_dx + to_particle_y * ray_dy;
    
    // If particle is behind ray origin, skip
    if (dot < 0) return false;
    
    // If particle is beyond max distance, skip
    float dist_sq = to_particle_x * to_particle_x + to_particle_y * to_particle_y;
    if (dist_sq > max_distance * max_distance) return false;
    
    // Check if particle is rotated (has a facing angle)
    if (particle.facing_angle != 0) {
        // For rotated particles, we need to rotate the edges
        float cos_angle = cosf(particle.facing_angle);
        float sin_angle = sinf(particle.facing_angle);
        
        // Define corners relative to center
        float corners[4][2] = {
            {-half_size, +half_size},  // Top-left
            {+half_size, +half_size},  // Top-right
            {+half_size, -half_size},  // Bottom-right
            {-half_size, -half_size}   // Bottom-left
        };
        
        // Rotate and translate corners to world position
        float world_corners[4][2];
        for (int i = 0; i < 4; i++) {
            float local_x = corners[i][0];
            float local_y = corners[i][1];
            
            // Rotate
            float rotated_x = local_x * cos_angle - local_y * sin_angle;
            float rotated_y = local_x * sin_angle + local_y * cos_angle;
            
            // Translate to world position
            world_corners[i][0] = particle.x + rotated_x;
            world_corners[i][1] = particle.y + rotated_y;
        }
        
        // Check each edge (connecting adjacent corners)
        for (int i = 0; i < 4; i++) {
            int next = (i + 1) % 4;
            if (ray_intersects_edge(ray_x, ray_y, ray_dx, ray_dy,
                                   world_corners[i][0], world_corners[i][1],
                                   world_corners[next][0], world_corners[next][1],
                                   max_distance)) {
                return true;  // Ray blocked by this edge
            }
        }
    } else {
        // For axis-aligned particles, use simpler calculation
        // Define the 4 edges of the square
        struct Edge {
            float x1, y1, x2, y2;
        };
        
        Edge edges[4] = {
            // Top edge
            {particle.x - half_size, particle.y + half_size, 
             particle.x + half_size, particle.y + half_size},
            // Right edge  
            {particle.x + half_size, particle.y + half_size,
             particle.x + half_size, particle.y - half_size},
            // Bottom edge
            {particle.x + half_size, particle.y - half_size,
             particle.x - half_size, particle.y - half_size},
            // Left edge
            {particle.x - half_size, particle.y - half_size,
             particle.x - half_size, particle.y + half_size}
        };
        
        // Check intersection with each edge
        for (const auto& edge : edges) {
            if (ray_intersects_edge(ray_x, ray_y, ray_dx, ray_dy, 
                                   edge.x1, edge.y1, edge.x2, edge.y2, 
                                   max_distance)) {
                return true;  // Ray blocked by this edge
            }
        }
    }
    
    return false;  // Ray passes through without hitting any edge
}

ViewPlane VisionSystem::calculate_view_plane(const Particle& particle, float viewer_x, float viewer_y) const {
    ViewPlane plane;
    plane.center_x = particle.x;
    plane.center_y = particle.y;
    
    // All particles are cubes now - use facing angle for orientation
    // TODO[ARCH-038]: When particle geometry system is implemented, handle different shapes
    // For now, all particles face their facing_angle direction
    plane.normal_x = cosf(particle.facing_angle);
    plane.normal_y = sinf(particle.facing_angle);
    
    // Calculate plane endpoints perpendicular to normal
    // Perpendicular vector is (-normal_y, normal_x)
    float perp_x = -plane.normal_y;
    float perp_y = plane.normal_x;
    
    // Always use particle size as the plane width to match world dimensions
    float width = particle.size;
    plane.half_width = width / 2.0f;
    
    // Calculate line segment endpoints
    plane.start_x = plane.center_x + perp_x * plane.half_width;
    plane.start_y = plane.center_y + perp_y * plane.half_width;
    plane.end_x = plane.center_x - perp_x * plane.half_width;
    plane.end_y = plane.center_y - perp_y * plane.half_width;
    
    return plane;
}

float VisionSystem::find_ray_hit_distance(float from_x, float from_y, float to_x, float to_y, ParticleSystem& particle_system) const {
    // Cast a ray and find where it hits an obstacle
    // Returns the distance to the hit point, or full distance if no hit
    
    float dx = to_x - from_x;
    float dy = to_y - from_y;
    float full_distance = sqrtf(dx * dx + dy * dy);
    
    // Normalize direction
    if (full_distance < 0.0001f) return 0.0f;
    float ray_dx = dx / full_distance;
    float ray_dy = dy / full_distance;
    
    float closest_hit = full_distance;  // Start with no obstruction
    
    // DEBUGGING NOTE: The Case of the Missing Obstacles
    //
    // I had a bug where vision rays were going through walls. After hours
    // of debugging, I realized this loop was checking the old empty global
    // particles vector, so it never found any obstacles!
    //
    // Lesson learned: When refactoring, grep for EVERY use of the old data.
    // Don't trust that you found them all. Test visually, not just with
    // unit tests.

    // THREAD SAFETY: Use ReadView for automatic lock management
    auto particles_view = particle_system.lock_particles_for_read();

    // Check all particles for obstruction
    for (size_t i = 1; i < particles_view.size(); i++) {  // Skip character (index 0)
        const auto& particle = particles_view[i];

        // All particles can potentially block vision
        // TODO: Could check particle properties like opacity or size instead

        // Check if this particle blocks the ray
        if (particle_blocks_ray(particle, from_x, from_y, ray_dx, ray_dy, closest_hit)) {
            // Find exact hit distance (simplified - assumes hit at particle center)
            float to_particle = calculate_distance(from_x, from_y, particle.x, particle.y);
            if (to_particle < closest_hit) {
                closest_hit = to_particle - particle.size / 2.0f;  // Hit at particle edge
            }
        }
    }
    
    return closest_hit;
}

bool VisionSystem::has_line_of_sight(float from_x, float from_y, float to_x, float to_y, ParticleSystem& particle_system) const {
    // Check if there's a clear line of sight between two points
    float hit_distance = find_ray_hit_distance(from_x, from_y, to_x, to_y, particle_system);
    float target_distance = calculate_distance(from_x, from_y, to_x, to_y);
    
    // If hit distance is close to target distance, we can see the target
    return hit_distance >= target_distance - 0.1f;
}

bool VisionSystem::has_binocular_line_of_sight(float to_x, float to_y, const Particle& viewer, float look_direction, ParticleSystem& particle_system) const {
    // Check if target is visible from either eye position
    // This mimics real binocular vision where one eye can see around obstacles
    
    // Calculate eye positions based on viewer's look direction
    // Eyes are offset perpendicular to look direction
    float look_perp_x = -sinf(look_direction) * EYE_SEPARATION / 2.0f;
    float look_perp_y = cosf(look_direction) * EYE_SEPARATION / 2.0f;
    
    float left_eye_x = viewer.x - look_perp_x;
    float left_eye_y = viewer.y - look_perp_y;
    float right_eye_x = viewer.x + look_perp_x;
    float right_eye_y = viewer.y + look_perp_y;
    
    // Check line of sight from each eye
    bool left_eye_los = has_line_of_sight(left_eye_x, left_eye_y, to_x, to_y, particle_system);
    bool right_eye_los = has_line_of_sight(right_eye_x, right_eye_y, to_x, to_y, particle_system);
    
    // Particle is visible if EITHER eye can see it (binocular advantage)
    return left_eye_los || right_eye_los;
}

void VisionSystem::update_vision_based_on_light(std::vector<ParticleLighting>& particle_lighting, ParticleSystem& particle_system) {
    // Update what particles are visible based on lighting
    // This creates a more realistic vision system where we need light to see

    // THREAD SAFETY: Use ReadView for automatic lock management
    auto particles_view = particle_system.lock_particles_for_read();

    // For each particle that received light, check if we can see it
    for (size_t i = 0; i < particles_view.size() && i < particle_lighting.size(); i++) {
        if (particle_lighting[i].IsLit()) {
            // Particle is lit - reduce its brightness based on distance and angle
            const auto& particle = particles_view[i];
            
            // Skip character particle (always visible)
            if (i == 0) continue;
            
            // Calculate view angle factor (particles look dimmer from the side)
            float view_angle = 1.0f;  // TODO: Calculate based on particle orientation
            
            // TODO[ARCH-038]: Apply view angle to surface lighting
            // For now, this is handled by the observer system
            // View angle should affect which surfaces are visible from observer
        }
    }
}

void VisionSystem::render_vision_cone_debug(const Particle& viewer, float look_direction,
                                             IDrawSurface& surface,
                                             CoordinateTransformer& coord,
                                             ParticleSystem& particle_system) const {
    if (!show_vision_cone_debug) return;

    // Viewer's screen position
    int char_screen_x, char_screen_y;
    coord.world_to_screen(viewer.x, viewer.y, char_screen_x, char_screen_y);

    // Cone boundaries
    const float half_cone_angle = VISION_CONE_ANGLE_RAD / 2.0f;
    const float left_boundary_angle = look_direction + half_cone_angle;
    const float right_boundary_angle = look_direction - half_cone_angle;

    const float cone_visual_range = VISION_CONE_RANGE * coord.get_pixels_per_unit();

    const float left_end_x = char_screen_x + cosf(left_boundary_angle) * cone_visual_range;
    const float left_end_y = char_screen_y - sinf(left_boundary_angle) * cone_visual_range;
    const float right_end_x = char_screen_x + cosf(right_boundary_angle) * cone_visual_range;
    const float right_end_y = char_screen_y - sinf(right_boundary_angle) * cone_visual_range;

    const int vw = coord.get_viewport_width();
    const int vh = coord.get_viewport_height();
    const int line_steps = static_cast<int>(cone_visual_range);

    // Left boundary line, cyan
    for (int i = 0; i <= line_steps; i++) {
        const float t = static_cast<float>(i) / line_steps;
        int pixel_x = static_cast<int>(char_screen_x + (left_end_x - char_screen_x) * t);
        int pixel_y = static_cast<int>(char_screen_y + (left_end_y - char_screen_y) * t);
        if (pixel_x >= 0 && pixel_x < vw && pixel_y >= 0 && pixel_y < vh) {
            surface.set_pixel(pixel_x, pixel_y, 0, 255, 255, 128);
        }
    }

    // Right boundary line, cyan
    for (int i = 0; i <= line_steps; i++) {
        const float t = static_cast<float>(i) / line_steps;
        int pixel_x = static_cast<int>(char_screen_x + (right_end_x - char_screen_x) * t);
        int pixel_y = static_cast<int>(char_screen_y + (right_end_y - char_screen_y) * t);
        if (pixel_x >= 0 && pixel_x < vw && pixel_y >= 0 && pixel_y < vh) {
            surface.set_pixel(pixel_x, pixel_y, 0, 255, 255, 128);
        }
    }

    // Arc at the vision range limit (brighter cyan)
    const int arc_steps = 20;
    for (int i = 0; i <= arc_steps; i++) {
        const float arc_progress = static_cast<float>(i) / arc_steps;
        const float arc_angle = right_boundary_angle + (left_boundary_angle - right_boundary_angle) * arc_progress;
        int arc_x = static_cast<int>(char_screen_x + cosf(arc_angle) * cone_visual_range);
        int arc_y = static_cast<int>(char_screen_y - sinf(arc_angle) * cone_visual_range);
        if (arc_x >= 0 && arc_x < vw && arc_y >= 0 && arc_y < vh) {
            surface.set_pixel(arc_x, arc_y, 0, 255, 255, 200);
        }
    }
}

void VisionSystem::render_binocular_vision_debug(const Particle& viewer, float look_direction,
                                                  IDrawSurface& surface,
                                                  CoordinateTransformer& coord,
                                                  ParticleSystem& particle_system) const {
    if (!show_binocular_debug) return;

    // Eye positions
    const float look_perp_x = -sinf(look_direction) * EYE_SEPARATION / 2.0f;
    const float look_perp_y = cosf(look_direction) * EYE_SEPARATION / 2.0f;
    const float left_eye_x = viewer.x - look_perp_x;
    const float left_eye_y = viewer.y - look_perp_y;
    const float right_eye_x = viewer.x + look_perp_x;
    const float right_eye_y = viewer.y + look_perp_y;

    int left_screen_x, left_screen_y, right_screen_x, right_screen_y;
    coord.world_to_screen(left_eye_x, left_eye_y, left_screen_x, left_screen_y);
    coord.world_to_screen(right_eye_x, right_eye_y, right_screen_x, right_screen_y);

    const int vw = coord.get_viewport_width();
    const int vh = coord.get_viewport_height();

    // Left eye as red dot
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            int px = left_screen_x + dx;
            int py = left_screen_y + dy;
            if (px >= 0 && px < vw && py >= 0 && py < vh) {
                surface.set_pixel(px, py, 255, 0, 0, 200);
            }
        }
    }

    // Right eye as green dot
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            int px = right_screen_x + dx;
            int py = right_screen_y + dy;
            if (px >= 0 && px < vw && py >= 0 && py < vh) {
                surface.set_pixel(px, py, 0, 255, 0, 200);
            }
        }
    }

    // THREAD SAFETY: Use ReadView for automatic lock management
    auto particles_view = particle_system.lock_particles_for_read();

    // Vision rays from each eye to visible particles
    for (size_t i = 1; i < particles_view.size(); i++) {
        const auto& particle = particles_view[i];
        if (!is_particle_in_vision_cone(particle, viewer, look_direction)) continue;

        int particle_screen_x, particle_screen_y;
        coord.world_to_screen(particle.x, particle.y, particle_screen_x, particle_screen_y);

        bool left_los = has_line_of_sight(left_eye_x, left_eye_y, particle.x, particle.y, particle_system);
        bool right_los = has_line_of_sight(right_eye_x, right_eye_y, particle.x, particle.y, particle_system);

        if (left_los) {
            float dx = particle_screen_x - left_screen_x;
            float dy = particle_screen_y - left_screen_y;
            int steps = static_cast<int>(sqrtf(dx*dx + dy*dy));
            for (int step = 0; step <= steps; step += 4) {
                float t = static_cast<float>(step) / steps;
                int px = static_cast<int>(left_screen_x + dx * t);
                int py = static_cast<int>(left_screen_y + dy * t);
                if (px >= 0 && px < vw && py >= 0 && py < vh) {
                    surface.set_pixel(px, py, 200, 0, 0, 50);  // Faint red
                }
            }
        }
        
        if (right_los) {
            float dx = particle_screen_x - right_screen_x;
            float dy = particle_screen_y - right_screen_y;
            int steps = static_cast<int>(sqrtf(dx*dx + dy*dy));
            for (int step = 0; step <= steps; step += 4) {
                float t = static_cast<float>(step) / steps;
                int px = static_cast<int>(right_screen_x + dx * t);
                int py = static_cast<int>(right_screen_y + dy * t);
                if (px >= 0 && px < vw && py >= 0 && py < vh) {
                    surface.set_pixel(px, py, 0, 200, 0, 50);  // Faint green
                }
            }
        }
    }
}

void VisionSystem::update_spatial_memory(double current_time, const Particle& viewer, float look_direction,
                                       const std::vector<Particle>& visible_particles, ParticleSystem& particle_system) {
    // Update spatial memory by checking which particles are currently visible
    // and managing the transition between active vision and memory
    
    int visible_count = 0;
    
    // First, check all particles to see which ones are currently in vision
    for (size_t i = 1; i < visible_particles.size(); i++) {  // Skip character particle (index 0)
        const auto& particle = visible_particles[i];
        
        // Check if particle is currently visible (in vision cone + line of sight)
        bool in_cone = is_particle_in_vision_cone(particle, viewer, look_direction);
        bool has_los = has_binocular_line_of_sight(particle.x, particle.y, viewer, look_direction, particle_system);
        bool currently_visible = in_cone && has_los;
        
        if (currently_visible) {
            visible_count++;
            // Particle is visible - update or add to spatial memory
            add_to_spatial_memory(particle, current_time);
        }
    }
    
    // Apply memory decay to all memory particles
    for (auto& memory : spatial_memory) {
        apply_memory_decay(memory, current_time);
    }
    
    // Remove memories that have completely faded (confidence near zero)
    spatial_memory.erase(
        std::remove_if(spatial_memory.begin(), spatial_memory.end(),
            [](const MemoryParticle& memory) { return memory.confidence < 0.01f; }),
        spatial_memory.end()
    );
}

void VisionSystem::add_to_spatial_memory(const Particle& particle, double current_time) {
    // Add or update a particle in spatial memory
    
    // Look for existing memory of this particle (simple position-based matching for now)
    bool found_existing = false;
    for (auto& memory : spatial_memory) {
        // If memory particle is very close to current particle, assume it's the same
        float memory_distance = calculate_distance(memory.original_x, memory.original_y,
                                                  particle.x, particle.y);
        if (memory_distance < 1.0f) {  // Match by position proximity
            // Update existing memory
            memory.x = particle.x;  // Update position
            memory.y = particle.y;
            memory.r = particle.r;  // Update color (in case it changed)
            memory.g = particle.g;
            memory.b = particle.b;
            memory.size = particle.size;
            memory.last_seen_time = current_time;
            memory.observation_count++;
            memory.confidence = std::min(1.0f, memory.confidence + 0.1f);  // Strengthen memory
            found_existing = true;
            break;
        }
    }
    
    if (!found_existing) {
        // Create new memory particle
        MemoryParticle new_memory;
        new_memory.x = particle.x;
        new_memory.y = particle.y;
        new_memory.original_x = particle.x;
        new_memory.original_y = particle.y;
        new_memory.r = particle.r;
        new_memory.g = particle.g;
        new_memory.b = particle.b;
        new_memory.a = particle.a;
        new_memory.size = particle.size;
        new_memory.confidence = 1.0f;  // Start with perfect memory
        new_memory.last_seen_time = current_time;
        new_memory.distance_when_seen = 0.0f;  // Will be set by vision system
        new_memory.observation_count = 1;
        
        spatial_memory.push_back(new_memory);
    }
}

MemoryLayer VisionSystem::get_memory_layer(const MemoryParticle& memory, double current_time) const {
    double time_since_seen = current_time - memory.last_seen_time;
    
    if (time_since_seen < 10.0) {
        return MemoryLayer::RECENT_MEMORY;    // Last 10 seconds
    } else if (time_since_seen < 60.0) {
        return MemoryLayer::SHORT_MEMORY;     // 10-60 seconds ago
    } else {
        return MemoryLayer::LONG_MEMORY;      // Over 60 seconds ago
    }
}

void VisionSystem::apply_memory_decay(MemoryParticle& memory, double current_time) {
    double time_since_seen = current_time - memory.last_seen_time;
    float decay_rate = 0.01f;
    
    memory.confidence -= decay_rate * time_since_seen;
    if (memory.confidence < 0.0f) {
        memory.confidence = 0.0f;
    }
    
    // Add position drift for uncertainty
    float drift_amount = (1.0f - memory.confidence) * 0.5f;
    memory.x = memory.original_x + (rand() / (float)RAND_MAX - 0.5f) * drift_amount;
    memory.y = memory.original_y + (rand() / (float)RAND_MAX - 0.5f) * drift_amount;
}