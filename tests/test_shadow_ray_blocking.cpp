// =============================================================================
// SHADOW RAY BLOCKING — occlusion booleans on the live ray API
// =============================================================================
// Ported 2026-07-15 from CPU-lighting lux assertions. The old form sampled
// PixelLightingStrategy intensities, a path compiled out of the shipping
// engine (USE_GPU_RASTERIZATION constexpr; TestContext::update_lighting
// early-returns in Interactive mode), so every sample read 0 lux. The
// semantic this test always wanted — a blocker between a surface and a
// light occludes the ray, removing it un-occludes — lives in
// BVH::trace_shadow_ray, the same API agent vision LOS uses
// (src/sense_system.cpp), so the contract is asserted there directly.
// Bonus coverage: BVH rebuild after a swap-and-pop removal.
// =============================================================================

#include "../src/test_context.h"
#include "../src/core/particle_system.h"
#include "logosphere/physics/bvh.h"
#include <iostream>

bool test_shadow_ray_blocking(TestContext& ctx) {
    std::cout << "\n=== Testing Shadow Ray Blocking ===" << std::endl;

    ctx.clear_particles();

    // Light at origin, blocker between it and the target surface.
    ctx.add_light_particle(0, 0, 0, 0.1f, 1.0f, 1.0f, 1.0f, 100000.0f, 50.0f);
    int blocker_id = ctx.add_cube_particle(0, 2, 0, 2.0f, 1.0f, 0.0f, 0.0f);
    int target_id  = ctx.add_cube_particle(0, 5, 0, 2.0f, 0.5f, 0.5f, 0.5f);
    std::cout << "  Light (0,0,0); blocker spans y:[1,3]; target south face at y=4"
              << std::endl;

    // TestContext::update_lighting() is gated off in Interactive mode;
    // build the BVH directly.
    ctx.particle_system.update_bvh();
    const BVH* bvh = ctx.particle_system.get_shadow_bvh();
    if (!bvh || !bvh->is_ready()) {
        std::cerr << "ERROR: BVH not built!" << std::endl;
        return false;
    }

    // Ray from the target's south-face center (0,4,0) to the light (0,0,0)
    // passes through the blocker's span.
    bool blocked_with;
    {
        auto view = ctx.particle_system.lock_particles_for_read();
        blocked_with = bvh->trace_shadow_ray(
            0.0f, 4.0f, 0.0f, 0.0f, 0.0f, 0.0f, view.get(), target_id);
    }
    std::cout << "  With blocker:    center ray "
              << (blocked_with ? "BLOCKED" : "NOT BLOCKED")
              << " (expected BLOCKED)" << std::endl;

    // Remove the blocker (swap-and-pop) and rebuild; the same ray must clear.
    ctx.particle_system.remove_particle(static_cast<size_t>(blocker_id));
    if (target_id == static_cast<int>(ctx.particle_system.count())) {
        target_id = blocker_id;  // target was the tail; it moved into the slot
    }
    ctx.particle_system.update_bvh();
    bvh = ctx.particle_system.get_shadow_bvh();

    bool blocked_without;
    {
        auto view = ctx.particle_system.lock_particles_for_read();
        blocked_without = bvh->trace_shadow_ray(
            0.0f, 4.0f, 0.0f, 0.0f, 0.0f, 0.0f, view.get(), target_id);
    }
    std::cout << "  Without blocker: center ray "
              << (blocked_without ? "BLOCKED" : "NOT BLOCKED")
              << " (expected NOT BLOCKED)" << std::endl;

    bool ok = blocked_with && !blocked_without;
    std::cout << (ok ? "✅ Shadow ray blocking behaves correctly"
                     : "✗ Shadow ray blocking FAILED") << std::endl;
    return ok;
}
