// Performance test: 20 particles around single light
#include "../src/test_context.h"
#include "../src/core/particle_system.h"
#include "../src/particle.h"
#include "../src/core/light_system.h"
#include "../src/object_id.h"
#include <iostream>
#include <cmath>
#include <chrono>
#include <random>

/**
 * Performance test: 20 particles around single light
 * 
 * PURPOSE:
 * - Consistent performance benchmark for threading optimizations
 * - Fixed particle count and positions for reproducible results
 * - Tracks FPS improvements as we optimize
 * 
 * SETUP:
 * - 1 light source at origin (0, 0, 0)
 * - 20 particles scattered in 10m radius
 * - Various sizes and colors for realistic workload
 * - Camera positioned to see all particles
 */

// BASELINE FPS (record improvements here)
constexpr float BASELINE_FPS = 21.0f;  // With spin-wait optimization
// Previous: 23.0f with 4 threads, condition variables

// Test that takes TestContext
bool test_performance_single_light_20(TestContext& ctx) {
    std::cout << "\n=== PERFORMANCE TEST: 20 Particles, Single Light ===\n";
    std::cout << "Baseline FPS: " << BASELINE_FPS << "\n";
    std::cout << "Creating 20 particles around single light source\n\n";
    
    // Clear any existing particles
    ctx.clear_particles();
    
    // Add single light at origin
    ctx.add_light_particle(0, 0, 0, 0.1f, 1.0f, 1.0f, 0.8f, 50000.0f, 50.0f);
    
    // Seed for reproducible random positions
    std::mt19937 gen(42);  // Fixed seed for consistency
    std::uniform_real_distribution<float> angle_dist(0.0f, 2.0f * M_PI);
    std::uniform_real_distribution<float> radius_dist(2.0f, 10.0f);
    std::uniform_real_distribution<float> height_dist(-2.0f, 2.0f);
    std::uniform_real_distribution<float> size_dist(0.3f, 0.8f);
    
    // Color palette for variety
    float colors[][3] = {
        {1.0f, 0.2f, 0.2f},  // Red
        {0.2f, 1.0f, 0.2f},  // Green
        {0.2f, 0.2f, 1.0f},  // Blue
        {1.0f, 1.0f, 0.2f},  // Yellow
        {1.0f, 0.2f, 1.0f},  // Magenta
        {0.2f, 1.0f, 1.0f},  // Cyan
        {1.0f, 0.6f, 0.2f},  // Orange
        {0.6f, 0.2f, 1.0f},  // Purple
    };
    int num_colors = sizeof(colors) / sizeof(colors[0]);
    
    // Add 20 particles in a circle around the light
    for (int i = 0; i < 20; i++) {
        float angle = angle_dist(gen);
        float radius = radius_dist(gen);
        float x = radius * cos(angle);
        float z = radius * sin(angle);
        float y = height_dist(gen);
        
        float size = size_dist(gen);
        float* color = colors[i % num_colors];
        
        ctx.add_cube_particle(x, y, z, size, color[0], color[1], color[2]);
    }
    
    // Position camera to see all particles
    ctx.set_camera_position(15, 10, 15);
    ctx.set_camera_look_at(0, 0, 0);
    
    std::cout << "Scene created: 1 light + 20 particles\n";
    std::cout << "Check FPS counter to measure performance\n";
    std::cout << "Current baseline: " << BASELINE_FPS << " FPS\n";
    
    return true;  // Always pass - this is a benchmark
}