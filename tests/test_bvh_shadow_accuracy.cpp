// Test BVH shadow accuracy - is AABB approximation causing issues?
#include "../src/test_context.h"
#include "../src/core/particle_system.h"
#include "logosphere/physics/bvh.h"
#include <iostream>

bool test_bvh_shadow_accuracy(TestContext& ctx) {
    std::cout << "\n=== Testing BVH Shadow Accuracy ===" << std::endl;
    
    ctx.clear_particles();
    
    // Same setup as Eden shadows test
    int light_id = ctx.add_light_particle(0, -2, 0, 0.2f, 1, 1, 1, 10000000, 50);
    int eva_id = ctx.add_cube_particle(0, 1, 0, 1.0f, 0.8f, 0.3f, 0.3f);
    int wall_id = ctx.add_cube_particle(0, 6, 0, 4.0f, 0.9f, 0.9f, 0.9f);
    
    std::cout << "Light at (0,-2,0), Eva at (0,1,0) size 1, Wall at (0,6,0) size 4" << std::endl;

    // Build the BVH directly. TestContext::update_lighting() early-returns
    // in Interactive mode (the harness engine's mode), which used to leave
    // this test tracing a STALE tree from whatever scene a previous module
    // built — the source of its false BLOCKED verdicts.
    ctx.particle_system.update_bvh();
    
    // Get the BVH
    const BVH* bvh = ctx.particle_system.get_shadow_bvh();
    if (!bvh || !bvh->is_ready()) {
        std::cerr << "ERROR: BVH not built!" << std::endl;
        return false;
    }
    
    std::cout << "\nBVH Stats:" << std::endl;
    std::cout << "  Ready: " << (bvh->is_ready() ? "YES" : "NO") << std::endl;
    std::cout << "  Depth: " << bvh->get_depth() << std::endl;
    
    // Test specific rays
    struct TestRay {
        float from_x, from_y, from_z;
        float to_x, to_y, to_z;
        bool should_be_blocked;
        const char* description;
    };
    
    TestRay test_rays[] = {
        // Ray from wall center to light - should be blocked by Eva
        {0, 4, 0,  0, -2, 0,  true, "Wall center to light (through Eva)"},
        
        // Ray from wall edge to light - should NOT be blocked
        {2, 4, 0,  0, -2, 0,  false, "Wall right edge to light (misses Eva)"},
        {-2, 4, 0,  0, -2, 0,  false, "Wall left edge to light (misses Eva)"},
        {0, 4, 2,  0, -2, 0,  false, "Wall front edge to light (misses Eva)"},
        {0, 4, -2,  0, -2, 0,  false, "Wall back edge to light (misses Eva)"},
        
        // Corner rays - should NOT be blocked
        {1.9f, 4, 1.9f,  0, -2, 0,  false, "Wall corner to light (misses Eva)"},
    };
    
    int correct = 0;
    int total = sizeof(test_rays) / sizeof(TestRay);

    auto particles_view = ctx.particle_system.lock_particles_for_read();
    for (const auto& ray : test_rays) {
        bool blocked = bvh->trace_shadow_ray(
            ray.from_x, ray.from_y, ray.from_z,
            ray.to_x, ray.to_y, ray.to_z,
            particles_view.get(),
            wall_id  // Skip the wall when checking
        );
        
        std::cout << ray.description << ": " 
                  << (blocked ? "BLOCKED" : "NOT BLOCKED")
                  << " (expected: " << (ray.should_be_blocked ? "BLOCKED" : "NOT BLOCKED") << ")";
        
        if (blocked == ray.should_be_blocked) {
            std::cout << " ✓" << std::endl;
            correct++;
        } else {
            std::cout << " ✗ ERROR!" << std::endl;
            
            // Debug: Check Eva's AABB
            if (!ray.should_be_blocked && blocked) {
                std::cout << "  Eva's AABB might be too large!" << std::endl;
                auto eva = ctx.particle_system.get_particle_copy(eva_id);
                std::cout << "  Eva: pos=(" << eva.x << "," << eva.y << "," << eva.z 
                          << ") size=" << eva.size << std::endl;
                std::cout << "  Eva AABB: [" << (eva.x - eva.size/2) << " to " << (eva.x + eva.size/2) << "] x ["
                          << (eva.y - eva.size/2) << " to " << (eva.y + eva.size/2) << "] x ["
                          << (eva.z - eva.size/2) << " to " << (eva.z + eva.size/2) << "]" << std::endl;
            }
        }
    }
    
    std::cout << "\nResults: " << correct << "/" << total << " rays correct" << std::endl;
    
    if (correct < total) {
        std::cout << "\n⚠️ trace_shadow_ray returned wrong occlusion verdicts!" << std::endl;
        std::cout << "This API is live for agent vision LOS (sense_system) — investigate," << std::endl;
        std::cout << "the narrow phase is exact ray-triangle, not AABB-conservative." << std::endl;
        return false;
    }
    
    return true;
}