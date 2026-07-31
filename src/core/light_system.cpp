#include "light_system.h"
#include "particle_system.h"
#include "pixel_lighting_strategy.h"
#include "engine.h"
#include "../optimization_flags.h"  // For USE_GPU_RASTERIZATION
#include <iostream>

// Light System Implementation - SIMPLIFIED!
//
// We used to have 1900+ lines of ray tracing code that only achieved 80% coverage
// Now we have ~100 lines that gives 100% accurate shadows
//
// The old approach: Cast 1000 rays from each light in a Fibonacci sphere pattern
// Problem: Gaps between rays meant some surfaces weren't fully lit
//
// The new approach: For each surface, check if it can see the light
// Simple, accurate, and much easier to understand!

LightSystem::LightSystem() : lighting_strategy_(nullptr), engine_(nullptr) {
    // Always use pixel-accurate lighting for proper shadows
    lighting_strategy_ = new PixelLightingStrategy();
}

LightSystem::~LightSystem() {
    // Clean up
    if (lighting_strategy_) {
        delete lighting_strategy_;
        lighting_strategy_ = nullptr;
    }
}

void LightSystem::initialize() {
    // Initialize our lighting strategy if we have an engine
    if (engine_ && lighting_strategy_) {
        lighting_strategy_->initialize(800, 600);  // Default size, will be updated
        std::cout << "LightSystem: Using " << lighting_strategy_->get_strategy_name() << " for lighting" << std::endl;
    }
}

void LightSystem::update_lighting(ParticleSystem& particle_system) {
    // OPTIMIZATION: Skip CPU lighting when GPU rasterization is enabled IN INTERACTIVE MODE
    // GPU shader does lighting+shadows in one pass, making CPU lighting redundant
    // BUT: In Headless mode (tests), always do CPU lighting since GPU rendering doesn't run
    if (Optimizations::USE_GPU_RASTERIZATION && engine_ && engine_->get_mode() == EngineMode::Interactive) {
        static bool logged_skip = false;
        if (!logged_skip) {
            std::cout << "[LIGHT_SYSTEM] Skipping CPU lighting - GPU handles lighting+shadows (Interactive mode)" << std::endl;
            logged_skip = true;
        }
        return;  // GPU does all the work in visual mode!
    }

    // System-level timing (once per frame - acceptable per PERFORMANCE_RESEARCH.md)
    static int frame_count = 0;
    frame_count++;

    auto start = std::chrono::high_resolution_clock::now();

    // Make sure we're initialized
    if (!lighting_strategy_ || !engine_) {
        std::cerr << "ERROR: LightSystem not properly initialized!" << std::endl;
        return;
    }

    // THREAD SAFETY: Use ReadView for automatic shared lock
    // Build list of light source indices
    std::vector<int> light_indices;
    size_t particle_count = 0;
    {
        auto particles_view = particle_system.lock_particles_for_read();
        particle_count = particles_view.size();
        for (size_t i = 0; i < particles_view.size(); i++) {
            // Check if particle is a light source
            if (particles_view[i].is_light_source) {
                light_indices.push_back(i);
            }
        }
    }  // Release lock after collecting light indices

    // Let the lighting strategy calculate all shadows
    lighting_strategy_->update_lighting(particle_system, light_indices);

    auto duration_ms = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - start).count();

    // Log first 10 frames or slow frames (>100ms)
    if (frame_count <= 10 || duration_ms > 100.0) {
        std::cout << "[LIGHT_UPDATE] " << duration_ms << "ms - "
                  << particle_count << " particles, "
                  << light_indices.size() << " lights" << std::endl;
    }
}

LightSystem::SurfaceColor LightSystem::get_surface_color_at(
    int particle_index, int surface_index,
    float u, float v,
    uint8_t base_r, uint8_t base_g, uint8_t base_b) const {
    
    SurfaceColor result;
    
    // Make sure we have our lighting strategy
    if (!lighting_strategy_) {
        // No lighting strategy? Return dark
        result.r = result.g = result.b = 10;  // Very dark
        result.intensity = 0.0f;
        result.in_shadow = true;
        return result;
    }
    
    // Get color from the lighting strategy
    auto strategy_color = lighting_strategy_->get_surface_color_at(
        particle_index, surface_index, u, v,
        base_r / 255.0f, base_g / 255.0f, base_b / 255.0f  // Convert to 0-1 range
    );
    
    // Convert back to our format
    result.r = strategy_color.r;
    result.g = strategy_color.g;
    result.b = strategy_color.b;
    result.intensity = strategy_color.intensity;
    result.in_shadow = (strategy_color.intensity < 0.5f);  // Less than 50% = shadow
    
    return result;
}

// No longer needed - we always use PixelLightingStrategy