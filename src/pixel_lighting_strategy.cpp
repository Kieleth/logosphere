#include "pixel_lighting_strategy.h"
#include "lighting_primitives.h"
#include "lighting_config.h"
#include "lighting_metrics.h"
#include "surface_cache.h"
#include "core/particle_system.h"
#include "debug_control.h"  // Centralized debug control
#include "optimization_flags.h"
#include "frame_metrics.h"  // Frame-based counters
#include <chrono>
#include <iostream>

// PixelLightingStrategy - Calculate lighting PER PIXEL instead of per surface
// This is the evolution of SimplePixelToLight that gives us accurate shadows!
//
// Key difference: We calculate lighting for each u,v coordinate individually
// This means soft shadow edges, gradients, and pixel-perfect accuracy

void PixelLightingStrategy::update_lighting(
    ParticleSystem& particle_system,
    const std::vector<int>& light_indices) {
    
    PERF_DEBUG_LOG("[PixelLightingStrategy] update_lighting called with "
                   << light_indices.size() << " lights, "
                   << particle_system.count() << " particles");
    
    
    // Time this for performance tracking
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Just cache the data we'll need for per-pixel calculations
    // The actual work happens in get_surface_color_at()
    light_indices_ = light_indices;
    cached_particle_system_ = &particle_system;
    
    
    // =========================================================================
    // OPTIMIZATION: SURFACE CACHING - Clear caches on new frame
    // Part of the surface caching optimization (see header for details)
    // Now using shared global cache for both lighting and shadows
    // =========================================================================
    cache_valid_ = false;
    if (Optimizations::USE_SURFACE_CACHE) {
        SurfaceCache::get().clear();  // Clear shared cache for new frame
    }
    
    // That's it! No pre-calculation. All work is done on-demand per pixel.
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    last_update_ms_ = duration.count() / 1000.0f;
    
    // This will be nearly instant since we're not doing calculations here
    PERF_DEBUG_LOG("[PixelLighting] Update took " << last_update_ms_ << "ms, cached " 
                   << light_indices_.size() << " light sources");
}

// THIS is where per-pixel magic happens - called for EVERY pixel during rendering
SurfaceColor PixelLightingStrategy::get_surface_color_at(
    int particle_id,
    int surface_index, 
    float u, float v,  // NOW we use these! Each pixel gets unique u,v
    float base_r, float base_g, float base_b) const {
    
    // Per-frame metrics - just count operations, no timing in hot path
    LightingMetrics::get().lighting_calls++;
    FRAME_COUNT_LIGHTING;
    
    
    SurfaceColor result;

    // Safety checks
    if (!cached_particle_system_) {
        result.r = result.g = result.b = 0;
        result.intensity = 0.0f;
        return result;
    }

    // THREAD SAFETY: Acquire ReadView for safe particle access
    // NOTE: This is called per-pixel (hot path), but shared_lock is lightweight
    // when already held by the same thread (render already has the lock)
    auto particles_view = cached_particle_system_->lock_particles_for_read();
    if (particle_id >= static_cast<int>(particles_view.size())) {
        result.r = result.g = result.b = 0;
        result.intensity = 0.0f;
        return result;
    }

    const Particle& particle = particles_view[particle_id];
    
    // =========================================================================
    // OPTIMIZATION: SURFACE CACHING - Use shared global cache
    // WHY: GetSurfaces() was taking 23ms for lighting + 30ms for shadows!
    // HOW: Use shared cache between lighting and shadow rays
    // IMPACT: From 55ms to hopefully ~25ms (>50% improvement)
    // ========================================================================="
    
    // TEMPORARILY DISABLE CACHE TO DEBUG
    // Use cache if enabled, otherwise get surfaces directly
    const auto& surfaces = particle.GetSurfaces();
    // const auto& surfaces = Optimizations::USE_SURFACE_CACHE 
    //     ? SurfaceCache::get().get_surfaces(particle_id, particle)
    //     : particle.GetSurfaces();
    
    if (surface_index >= surfaces.size()) {
        result.r = result.g = result.b = 0;
        result.intensity = 0.0f;
        return result;
    }
    
    const Surface& surface = surfaces[surface_index];
    
    // Step 1: Convert this pixel's u,v to exact 3D world position
    // This is the KEY difference from SimplePixelToLight!
    float point_x, point_y, point_z;
    LightingPrimitives::surface_uv_to_world(surface, u, v, point_x, point_y, point_z);
    
    // Step 2: Calculate lighting for THIS SPECIFIC PIXEL
    // Reuse our primitives - they do all the heavy lifting!
    // Use BVH-accelerated version (falls back to linear if BVH disabled or unavailable)
    const BVH* bvh = cached_particle_system_ ? cached_particle_system_->get_shadow_bvh() : nullptr;
    float total_intensity = LightingPrimitives::calculate_point_lighting_bvh(
        point_x, point_y, point_z,
        surface.nx, surface.ny, surface.nz,  // Surface normal (same for all pixels on surface)
        particles_view.get(),
        light_indices_,
        bvh,
        particle_id  // Skip self when checking for shadows
    );
    
    
    // Step 3: Convert intensity to RGB using tone mapping
    LightingConfig& config = LightingConfig::get();
    int brightness_rgb = config.tone_map_to_rgb(total_intensity);
    
    // TODO[CLEAN-001]: Debug code for shadow rendering - remove after verification
    
    
    // Apply brightness to base color
    // FIXED: base_r/g/b are already normalized (0-1) from light_system
    // brightness_rgb is 0-255 (from tone mapping)
    // Simply multiply them together
    
    result.r = static_cast<uint8_t>(base_r * brightness_rgb);
    result.g = static_cast<uint8_t>(base_g * brightness_rgb);
    result.b = static_cast<uint8_t>(base_b * brightness_rgb);
    result.intensity = total_intensity;
    
    // Fixed: render_system was passing wrong range (0-255 instead of 0-1)
    
    // Per-frame metrics: Just increment counters, no timing
    // Actual performance measurement happens at frame level
    
    
    
    return result;
}