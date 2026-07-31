// =============================================================================
// SHADOW MOVEMENT DIRECTION — occlusion geometry tracks the blocker
// =============================================================================
// Ported 2026-07-15 from CPU-lighting lux assertions (dead path: pixel
// lighting strategy compiled out behind USE_GPU_RASTERIZATION, TestContext
// helpers gated off in Interactive mode — every sample read 0 lux). The
// physical contract stands on the live ray API instead: with a central
// light, a blocker displaced west shadows the wall's west samples and
// leaves the east lit, and mirrored; a raised blocker shadows high
// samples and leaves low ones lit. Asserted with BVH::trace_shadow_ray,
// the same occlusion primitive agent vision LOS uses (src/sense_system.cpp).
// No gravity assumptions: pure ray geometry, works on any axis.
// =============================================================================

#include "../src/test_context.h"
#include "../src/core/particle_system.h"
#include "logosphere/physics/bvh.h"
#include <iostream>

namespace {

bool ray_blocked(TestContext& ctx, float fx, float fy, float fz,
                 float tx, float ty, float tz, int skip_id) {
    const BVH* bvh = ctx.particle_system.get_shadow_bvh();
    if (!bvh || !bvh->is_ready()) return false;
    auto view = ctx.particle_system.lock_particles_for_read();
    return bvh->trace_shadow_ray(fx, fy, fz, tx, ty, tz, view.get(), skip_id);
}

void move_particle(TestContext& ctx, int id, float x, float y, float z) {
    {
        auto view = ctx.particle_system.lock_particles_for_write();
        view[id].x = x;
        view[id].y = y;
        view[id].z = z;
    }
    // Writer marks the BVH (hand-moved particle), then rebuild for tracing.
    ctx.particle_system.mark_bvh_dirty();
    ctx.particle_system.update_bvh();
}

} // namespace

bool test_shadow_movement_direction(TestContext& ctx) {
    std::cout << "\n=== Testing Shadow Movement Direction ===" << std::endl;
    std::cout << "Central light; the shadow lands on the wall behind the blocker"
              << std::endl;

    ctx.clear_particles();

    // Light at (0,0,3). Wall 8 m at (0,10,3): south face y=6, x in [-4,4],
    // z in [-1,7]. Blocker 2 m at y=3 (midway light->wall).
    //
    // Ray placement: every BLOCKED assertion is a ray straight through the
    // blocker's CENTER (occludes for any narrow-phase extent convention);
    // every NOT-BLOCKED assertion clears the blocker's full size-2 AABB.
    ctx.add_light_particle(0.0f, 0.0f, 3.0f, 0.3f, 1.0f, 1.0f, 1.0f,
                           100000.0f, 50.0f);
    int wall_id    = ctx.add_cube_particle(0.0f, 10.0f, 3.0f, 8.0f, 0.9f, 0.9f, 0.9f);
    int blocker_id = ctx.add_cube_particle(-1.5f, 3.0f, 3.0f, 2.0f, 0.8f, 0.3f, 0.3f);
    ctx.particle_system.update_bvh();

    const float LX = 0.0f, LY = 0.0f, LZ = 3.0f;
    // Sample points on the wall's south face. WEST/EAST/HIGH are the exact
    // light->blocker-center projections onto the face.
    const float WEST[3]   = {-3.0f, 6.0f, 3.0f};
    const float CENTER[3] = { 0.0f, 6.0f, 3.0f};
    const float EAST[3]   = { 3.0f, 6.0f, 3.0f};
    const float LOW[3]    = { 0.0f, 6.0f, 1.0f};
    const float HIGH[3]   = { 0.0f, 6.0f, 6.0f};

    auto check = [&](const char* label, const float p[3], bool expect_blocked) {
        bool blocked = ray_blocked(ctx, p[0], p[1], p[2], LX, LY, LZ, wall_id);
        std::cout << "  " << label << ": "
                  << (blocked ? "BLOCKED" : "NOT BLOCKED")
                  << " (expected " << (expect_blocked ? "BLOCKED" : "NOT BLOCKED")
                  << ")" << (blocked == expect_blocked ? " ✓" : " ✗ ERROR") << std::endl;
        return blocked == expect_blocked;
    };

    bool ok = true;

    std::cout << "\n  Blocker WEST (-1.5,3,3):" << std::endl;
    ok &= check("west sample", WEST, true);
    ok &= check("center sample", CENTER, false);
    ok &= check("east sample", EAST, false);

    std::cout << "\n  Blocker EAST (+1.5,3,3):" << std::endl;
    move_particle(ctx, blocker_id, 1.5f, 3.0f, 3.0f);
    ok &= check("west sample", WEST, false);
    ok &= check("center sample", CENTER, false);
    ok &= check("east sample", EAST, true);

    std::cout << "\n  Blocker HIGH (0,3,4.5):" << std::endl;
    move_particle(ctx, blocker_id, 0.0f, 3.0f, 4.5f);
    ok &= check("high sample", HIGH, true);
    ok &= check("low sample", LOW, false);

    std::cout << (ok ? "\n✅ Shadow tracks the blocker on every axis"
                     : "\n✗ Shadow movement FAILED") << std::endl;
    return ok;
}
