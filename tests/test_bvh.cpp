#include "../src/test_context.h"
#include "logosphere/physics/bvh.h"
#include "../src/core/particle_system.h"
#include "../src/lighting_primitives.h"
#include <iostream>
#include <chrono>

bool test_bvh_basic(TestContext& ctx) {
    std::cout << "\n=== Testing BVH Basic Functionality ===" << std::endl;
    
    // Create a simple scene with particles
    auto& particle_system = ctx.particle_system;
    
    // Add some non-light particles (these will be in BVH)
    for (int i = 0; i < 5; i++) {
        ctx.add_cube_particle(
            i * 3.0f, 0.0f, 0.0f,  // Line of cubes along X axis
            1.0f,                   // size
            0.7f, 0.7f, 0.7f       // gray color
        );
    }
    
    // Build the BVH
    particle_system.update_bvh();
    const BVH* bvh = particle_system.get_shadow_bvh();
    
    if (!bvh || !bvh->is_ready()) {
        std::cout << "FAIL: BVH not built!" << std::endl;
        return false;
    }
    
    std::cout << "BVH built with " << bvh->get_node_count() << " nodes" << std::endl;
    std::cout << "BVH depth: " << bvh->get_depth() << std::endl;

    auto particles_view = particle_system.lock_particles_for_read();
    // Test 1: Ray that should be blocked
    bool blocked = bvh->trace_shadow_ray(
        -5.0f, 0.0f, 0.0f,  // From left of all cubes
        10.0f, 0.0f, 0.0f,  // To right of all cubes
        particles_view.get(),
        -1  // No skip
    );
    
    if (!blocked) {
        std::cout << "FAIL: Ray should be blocked by cubes!" << std::endl;
        return false;
    }
    std::cout << "✓ Ray correctly blocked by BVH" << std::endl;
    
    // Test 2: Ray that should NOT be blocked
    blocked = bvh->trace_shadow_ray(
        -5.0f, 10.0f, 0.0f,  // From above the cubes
        10.0f, 10.0f, 0.0f,  // To above the cubes
        particles_view.get(),
        -1
    );
    
    if (blocked) {
        std::cout << "FAIL: Ray above cubes should not be blocked!" << std::endl;
        return false;
    }
    std::cout << "✓ Ray correctly not blocked by BVH" << std::endl;
    
    std::cout << "✓ BVH basic test passed!" << std::endl;
    return true;
}

bool test_bvh_performance(TestContext& ctx) {
    std::cout << "\n=== Testing BVH Performance ===" << std::endl;
    
    auto& particle_system = ctx.particle_system;
    
    // Create a larger scene with 20 particles
    for (int x = 0; x < 5; x++) {
        for (int y = 0; y < 4; y++) {
            ctx.add_cube_particle(
                x * 3.0f, y * 3.0f, 0.0f,
                1.0f,
                0.7f, 0.7f, 0.7f
            );
        }
    }
    
    auto particles_view = particle_system.lock_particles_for_read();
    std::cout << "Created " << particles_view.size() << " particles" << std::endl;
    
    // Build BVH
    particle_system.update_bvh();
    const BVH* bvh = particle_system.get_shadow_bvh();
    
    if (!bvh || !bvh->is_ready()) {
        std::cout << "FAIL: BVH not built for performance test!" << std::endl;
        return false;
    }
    
    // Test many rays and compare performance
    const int num_rays = 1000;
    
    // Time linear search
    auto start = std::chrono::high_resolution_clock::now();
    int blocked_linear = 0;
    for (int i = 0; i < num_rays; i++) {
        float from_x = -10.0f + (i % 20);
        float from_y = -10.0f + (i / 20) % 20;
        float to_x = from_x + 20.0f;
        float to_y = from_y + 1.0f;
        
        if (LightingPrimitives::is_ray_blocked(
            from_x, from_y, 0.0f,
            to_x, to_y, 0.0f,
            particles_view.get(), -1)) {
            blocked_linear++;
        }
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto linear_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    
    // Time BVH search
    start = std::chrono::high_resolution_clock::now();
    int blocked_bvh = 0;
    for (int i = 0; i < num_rays; i++) {
        float from_x = -10.0f + (i % 20);
        float from_y = -10.0f + (i / 20) % 20;
        float to_x = from_x + 20.0f;
        float to_y = from_y + 1.0f;
        
        if (bvh->trace_shadow_ray(
            from_x, from_y, 0.0f,
            to_x, to_y, 0.0f,
            particles_view.get(), -1)) {
            blocked_bvh++;
        }
    }
    end = std::chrono::high_resolution_clock::now();
    auto bvh_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    
    std::cout << "Linear search: " << linear_time << "μs, blocked " << blocked_linear << " rays" << std::endl;
    std::cout << "BVH search: " << bvh_time << "μs, blocked " << blocked_bvh << " rays" << std::endl;
    
    // Results should match
    if (blocked_linear != blocked_bvh) {
        std::cout << "FAIL: BVH and linear search disagree on results!" << std::endl;
        return false;
    }
    
    // BVH should be faster (or at least not much slower for small scenes)
    float speedup = (float)linear_time / (float)bvh_time;
    std::cout << "BVH speedup: " << speedup << "x" << std::endl;
    
    std::cout << "✓ BVH performance test passed!" << std::endl;
    return true;
}