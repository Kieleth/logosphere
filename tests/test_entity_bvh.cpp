#include "../src/test_context.h"
#include "logosphere/rendering/entity_bvh.h"
#include "logosphere/rendering/gpu/metal_compute_bridge.h"
#include <iostream>
#include <chrono>
#include <cmath>

// Helper: Create a simple cube as 12 triangles
static void create_cube_triangles(float cx, float cy, float cz, float size,
                                   std::vector<Logosphere::ShadowTriangle>& out) {
    float half = size * 0.5f;

    // 6 faces, 2 triangles each = 12 triangles
    // Face +Z (top)
    out.push_back(Logosphere::ShadowTriangle(
        cx-half, cy-half, cz+half,  cx+half, cy-half, cz+half,  cx+half, cy+half, cz+half));
    out.push_back(Logosphere::ShadowTriangle(
        cx-half, cy-half, cz+half,  cx+half, cy+half, cz+half,  cx-half, cy+half, cz+half));

    // Face -Z (bottom)
    out.push_back(Logosphere::ShadowTriangle(
        cx-half, cy-half, cz-half,  cx-half, cy+half, cz-half,  cx+half, cy+half, cz-half));
    out.push_back(Logosphere::ShadowTriangle(
        cx-half, cy-half, cz-half,  cx+half, cy+half, cz-half,  cx+half, cy-half, cz-half));

    // Face +X (east)
    out.push_back(Logosphere::ShadowTriangle(
        cx+half, cy-half, cz-half,  cx+half, cy+half, cz-half,  cx+half, cy+half, cz+half));
    out.push_back(Logosphere::ShadowTriangle(
        cx+half, cy-half, cz-half,  cx+half, cy+half, cz+half,  cx+half, cy-half, cz+half));

    // Face -X (west)
    out.push_back(Logosphere::ShadowTriangle(
        cx-half, cy-half, cz-half,  cx-half, cy-half, cz+half,  cx-half, cy+half, cz+half));
    out.push_back(Logosphere::ShadowTriangle(
        cx-half, cy-half, cz-half,  cx-half, cy+half, cz+half,  cx-half, cy+half, cz-half));

    // Face +Y (north)
    out.push_back(Logosphere::ShadowTriangle(
        cx-half, cy+half, cz-half,  cx-half, cy+half, cz+half,  cx+half, cy+half, cz+half));
    out.push_back(Logosphere::ShadowTriangle(
        cx-half, cy+half, cz-half,  cx+half, cy+half, cz+half,  cx+half, cy+half, cz-half));

    // Face -Y (south)
    out.push_back(Logosphere::ShadowTriangle(
        cx-half, cy-half, cz-half,  cx+half, cy-half, cz-half,  cx+half, cy-half, cz+half));
    out.push_back(Logosphere::ShadowTriangle(
        cx-half, cy-half, cz-half,  cx+half, cy-half, cz+half,  cx-half, cy-half, cz+half));
}

bool test_entity_bvh_basic(TestContext& ctx) {
    (void)ctx;  // Not using test context for this standalone test
    std::cout << "\n=== Testing EntityBVH Basic Functionality ===" << std::endl;

    // Create 5 cube entities along X axis
    std::vector<EntityTriangleData> entities;
    for (int i = 0; i < 5; ++i) {
        EntityTriangleData entity;
        entity.entity_id = static_cast<kg::EntityID>(i + 1);
        entity.is_dynamic = false;

        // Create cube centered at (i*3, 0, 0)
        create_cube_triangles(i * 3.0f, 0.0f, 0.0f, 1.0f, entity.triangles);

        // Compute AABB
        float cx = i * 3.0f;
        entity.bbox = AABB(cx - 0.5f, -0.5f, -0.5f, cx + 0.5f, 0.5f, 0.5f);

        entities.push_back(entity);
    }

    std::cout << "Created " << entities.size() << " entities with "
              << (entities.size() * 12) << " triangles total" << std::endl;

    // Build EntityBVH
    EntityBVH bvh;
    bvh.build(entities);

    if (!bvh.is_ready()) {
        std::cout << "FAIL: EntityBVH not built!" << std::endl;
        return false;
    }

    int entity_count, total_tris, max_depth;
    bvh.get_stats(entity_count, total_tris, max_depth);
    std::cout << "EntityBVH built: " << entity_count << " entities, "
              << total_tris << " triangles, depth " << max_depth << std::endl;
    std::cout << "Nodes: " << bvh.get_node_count() << ", Groups: " << bvh.get_group_count() << std::endl;

    // Test 1: Ray that should be blocked (through cube at x=6)
    bool blocked = bvh.trace_shadow_ray(
        -5.0f, 0.0f, 0.0f,   // From left
        15.0f, 0.0f, 0.0f,   // To right
        kg::INVALID_ENTITY   // No skip
    );

    if (!blocked) {
        std::cout << "FAIL: Ray should be blocked by cubes!" << std::endl;
        return false;
    }
    std::cout << "  Ray blocked: OK" << std::endl;

    // Test 2: Ray that should NOT be blocked (above cubes)
    blocked = bvh.trace_shadow_ray(
        -5.0f, 0.0f, 5.0f,   // From left, above
        15.0f, 0.0f, 5.0f,   // To right, above
        kg::INVALID_ENTITY
    );

    if (blocked) {
        std::cout << "FAIL: Ray above cubes should not be blocked!" << std::endl;
        return false;
    }
    std::cout << "  Ray not blocked: OK" << std::endl;

    // Test 3: Skip entity test
    blocked = bvh.trace_shadow_ray(
        2.9f, 0.0f, 0.0f,    // Just left of cube at x=3 (entity 2)
        3.1f, 0.0f, 0.0f,    // Just right of cube at x=3
        2                     // Skip entity 2
    );

    if (blocked) {
        std::cout << "FAIL: Ray should not be blocked when skipping entity 2!" << std::endl;
        return false;
    }
    std::cout << "  Entity skip: OK" << std::endl;

    std::cout << "  EntityBVH basic test passed!" << std::endl;
    return true;
}

bool test_entity_bvh_directional_culling(TestContext& ctx) {
    (void)ctx;
    std::cout << "\n=== Testing EntityBVH Directional Culling ===" << std::endl;

    // Create a single cube entity
    std::vector<EntityTriangleData> entities;
    EntityTriangleData entity;
    entity.entity_id = 1;
    entity.is_dynamic = false;
    create_cube_triangles(0.0f, 0.0f, 0.0f, 2.0f, entity.triangles);
    entity.bbox = AABB(-1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f);
    entities.push_back(entity);

    EntityBVH bvh;
    bvh.build(entities);

    if (!bvh.is_ready()) {
        std::cout << "FAIL: EntityBVH not built!" << std::endl;
        return false;
    }

    // Verify directional groups were created
    int groups = bvh.get_group_count();
    std::cout << "Created " << groups << " directional groups (expected ~6)" << std::endl;

    if (groups < 4 || groups > 6) {
        std::cout << "WARNING: Expected ~6 directional groups, got " << groups << std::endl;
    }

    // Test ray from +X direction (should hit -X facing triangles first)
    bool blocked = bvh.trace_shadow_ray(
        5.0f, 0.0f, 0.0f,    // From +X
        -5.0f, 0.0f, 0.0f,   // To -X
        kg::INVALID_ENTITY
    );

    if (!blocked) {
        std::cout << "FAIL: Ray from +X should hit cube!" << std::endl;
        return false;
    }
    std::cout << "  Ray from +X blocked: OK" << std::endl;

    // Test ray from -Z direction (should hit +Z facing triangles first)
    blocked = bvh.trace_shadow_ray(
        0.0f, 0.0f, -5.0f,   // From -Z
        0.0f, 0.0f, 5.0f,    // To +Z
        kg::INVALID_ENTITY
    );

    if (!blocked) {
        std::cout << "FAIL: Ray from -Z should hit cube!" << std::endl;
        return false;
    }
    std::cout << "  Ray from -Z blocked: OK" << std::endl;

    std::cout << "  Directional culling test passed!" << std::endl;
    return true;
}

bool test_entity_bvh_performance(TestContext& ctx) {
    (void)ctx;
    std::cout << "\n=== Testing EntityBVH Performance ===" << std::endl;

    // Create 100 cube entities in a grid
    std::vector<EntityTriangleData> entities;
    for (int x = 0; x < 10; ++x) {
        for (int y = 0; y < 10; ++y) {
            EntityTriangleData entity;
            entity.entity_id = static_cast<kg::EntityID>(x * 10 + y + 1);
            entity.is_dynamic = false;

            float cx = x * 3.0f;
            float cy = y * 3.0f;
            create_cube_triangles(cx, cy, 0.0f, 1.0f, entity.triangles);
            entity.bbox = AABB(cx - 0.5f, cy - 0.5f, -0.5f, cx + 0.5f, cy + 0.5f, 0.5f);

            entities.push_back(entity);
        }
    }

    std::cout << "Created " << entities.size() << " entities with "
              << (entities.size() * 12) << " triangles" << std::endl;

    // Build and time
    auto build_start = std::chrono::high_resolution_clock::now();
    EntityBVH bvh;
    bvh.build(entities);
    auto build_end = std::chrono::high_resolution_clock::now();

    double build_ms = std::chrono::duration<double, std::milli>(build_end - build_start).count();
    std::cout << "Build time: " << build_ms << " ms" << std::endl;

    int entity_count, total_tris, max_depth;
    bvh.get_stats(entity_count, total_tris, max_depth);
    std::cout << "Stats: " << entity_count << " entities, "
              << total_tris << " triangles, depth " << max_depth << std::endl;

    // Trace 10,000 random rays
    const int NUM_RAYS = 10000;
    int blocked_count = 0;

    auto trace_start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < NUM_RAYS; ++i) {
        // Random ray through the scene
        float angle = i * 0.0628f;  // Spread rays around
        float from_x = -10.0f + std::cos(angle) * 40.0f;
        float from_y = 15.0f + std::sin(angle) * 40.0f;
        float to_x = 15.0f;
        float to_y = 15.0f;

        if (bvh.trace_shadow_ray(from_x, from_y, 0.0f, to_x, to_y, 0.0f)) {
            blocked_count++;
        }
    }
    auto trace_end = std::chrono::high_resolution_clock::now();

    double trace_ms = std::chrono::duration<double, std::milli>(trace_end - trace_start).count();
    double rays_per_sec = NUM_RAYS / (trace_ms / 1000.0);

    std::cout << "Traced " << NUM_RAYS << " rays in " << trace_ms << " ms" << std::endl;
    std::cout << "  " << blocked_count << " rays blocked ("
              << (100.0 * blocked_count / NUM_RAYS) << "%)" << std::endl;
    std::cout << "  " << (rays_per_sec / 1000.0) << "K rays/sec" << std::endl;

    std::cout << "  EntityBVH performance test passed!" << std::endl;
    return true;
}
