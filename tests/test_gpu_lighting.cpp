// test_gpu_lighting.cpp
// STEP 7: Test GPU lighting integration
//
// Goal: Verify GPU can rasterize triangles with inline Lambertian lighting
// Test: Create 3 cubes, 1 light, render with GPU lighting
// Expected: Pixels are lit (not black), lighting varies with distance

#include "test_context.h"
#include "core/camera_system.h"
#include "projection_system.h"
#include "logosphere/rendering/pixel_buffer.h"
#include "logosphere/rendering/gpu/gpu_rasterizer.h"
#include "particle_geometry_v2.h"
#include <iostream>
#include <vector>
#include <limits>
#include <memory>
#include <cmath>
#include <cstdlib>

bool test_gpu_lighting(TestContext& ctx) {
    if (std::getenv("CI")) {
        std::cout << "SKIP: GPU pixel test on CI (paravirtual GPU)" << std::endl;
        return true;
    }

    // Test resolution
    const int width = 400;
    const int height = 300;

    // Create PixelBuffer and depth buffer
    PixelBuffer pixel_buffer(width, height);
    pixel_buffer.clear(0, 0, 0, 255);  // Black background
    std::vector<uint32_t> depth_buffer(width * height, UINT32_MAX);

    // Initialize GPU rasterizer
    Logosphere::GPURasterizer gpu_rasterizer;
    if (!gpu_rasterizer.initialize(width, height)) {
        std::cout << "[FAIL] GPU rasterizer failed to initialize" << std::endl;
        return false;
    }

    // Create camera system
    CameraSystem camera_system;
    camera_system.set_viewport(width, height);
    camera_system.set_projection_system(std::make_unique<IsometricDepthProjection>());
    camera_system.set_position(-10.0f, -10.0f, 20.0f);  // Standard isometric position
    camera_system.set_pixels_per_unit(40.0f);  // Standard zoom

    // Create 3 cubes at different positions
    struct CubeData {
        float x, y, z;
        uint8_t r, g, b, a;
    };

    CubeData cubes[] = {
        {0.0f, 0.0f, 5.0f, 255, 0, 0, 255},    // Red cube at origin
        {3.0f, 0.0f, 5.0f, 0, 255, 0, 255},    // Green cube to east (farther from light)
        {0.0f, 3.0f, 5.0f, 0, 0, 255, 255}     // Blue cube to north (farther from light)
    };

    // Collect all GPU triangles with lighting data
    std::vector<Logosphere::GPURasterizer::TriangleLit> gpu_triangles;

    for (int i = 0; i < 3; i++) {
        const auto& cube = cubes[i];
        // Create cube geometry
        ParticleGeometryV2::CubeGeometryV2 geom(1.0f);
        ParticleGeometryV2::Transform transform(
            ParticleGeometryV2::Vec3(cube.x, cube.y, cube.z), 0, 0, 0);
        auto surfaces = ParticleGeometryV2::geometry_to_surfaces(geom, transform);

        // Convert each surface to GPU triangles with lighting data
        for (const auto& surface : surfaces) {
            Logosphere::GPURasterizer::convert_surface_to_lit_triangles(
                surface,
                camera_system,
                cube.r, cube.g, cube.b, cube.a,
                gpu_triangles
            );
        }
    }

    // convert_surface_to_lit_triangles back-face culls against the camera
    // (production behavior, gpu_rasterizer.mm ~:1758): under this isometric
    // camera each cube shows 3 of its 6 faces, so the expected count is
    // exactly 3 cubes × 3 visible faces × 2 triangles = 18.
    if (gpu_triangles.size() != 18) {
        std::cout << "[FAIL] Expected 18 back-face-culled triangles, got "
                  << gpu_triangles.size() << std::endl;
        return false;
    }

    // Create light data (matches gpu_types.metal::LightData)
    struct LightData {
        float position[3];           // 12 bytes
        float emission_strength;     // 4 bytes
        float emission_radius;       // 4 bytes
        float light_size;            // 4 bytes (physical size for soft shadows)
        float _padding[2];           // 8 bytes (total 32 bytes)
    };

    // Single stadium light at origin (100,000 lumens, 30m radius)
    LightData lights[1];
    lights[0].position[0] = 0.0f;
    lights[0].position[1] = 0.0f;
    lights[0].position[2] = 6.0f;  // Above the cubes (z=5)
    lights[0].emission_strength = 100000.0f;  // Stadium light
    lights[0].emission_radius = 30.0f;
    lights[0]._padding[0] = 0.0f;
    lights[0]._padding[1] = 0.0f;
    lights[0]._padding[2] = 0.0f;

    // Get native buffer
    auto& native_buffer = pixel_buffer.get_native_buffer();
    if (native_buffer.empty()) {
        std::cout << "[FAIL] PixelBuffer not using native format" << std::endl;
        return false;
    }

    // Rasterize all triangles with GPU lighting
    gpu_rasterizer.rasterize_triangles_lit(
        native_buffer.data(),
        depth_buffer.data(),
        gpu_triangles.data(),
        (uint32_t)gpu_triangles.size(),
        lights,  // Light data
        1,       // Light count
        nullptr, // BVH nodes (not used yet)
        0,       // BVH count
        nullptr, // BVH triangles (not used yet)
        0        // BVH triangle count
    );

    // Test: Count non-black pixels (should have rendered something LIT)
    int non_black_count = 0;
    int red_pixels = 0;
    int green_pixels = 0;
    int blue_pixels = 0;
    int bright_pixels = 0;  // Pixels with any channel > 50

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            EnhancedPixel p = pixel_buffer.get_pixel(x, y);
            if (p.r > 10 || p.g > 10 || p.b > 10) {
                non_black_count++;

                // Count dominant color
                if (p.r > p.g && p.r > p.b && p.r > 20) red_pixels++;
                else if (p.g > p.r && p.g > p.b && p.g > 20) green_pixels++;
                else if (p.b > p.r && p.b > p.g && p.b > 20) blue_pixels++;

                // Count bright pixels (well-lit)
                if (p.r > 50 || p.g > 50 || p.b > 50) bright_pixels++;
            }
        }
    }

    // Test 1: Should have rendered significant number of pixels
    bool test1 = (non_black_count > 1000);
    if (!test1) {
        std::cout << "[FAIL] Too few pixels rendered: " << non_black_count
                  << " (expected >1000)" << std::endl;
        return false;
    }

    // Test 2: Should have all 3 colors (red, green, blue cubes)
    bool test2 = (red_pixels > 50 && green_pixels > 50 && blue_pixels > 50);
    if (!test2) {
        std::cout << "[FAIL] Not all colors rendered. Red: " << red_pixels
                  << ", Green: " << green_pixels << ", Blue: " << blue_pixels
                  << " (expected >50 each)" << std::endl;
        return false;
    }

    // Test 3: Should have well-lit pixels (lighting working)
    bool test3 = (bright_pixels > 500);
    if (!test3) {
        std::cout << "[FAIL] Too few bright pixels: " << bright_pixels
                  << " (expected >500, indicates lighting may not be working)" << std::endl;
        return false;
    }

    std::cout << "[PASS] GPU lighting: " << gpu_triangles.size()
              << " triangles from 3 cubes + 1 light, " << non_black_count << " pixels rendered"
              << " (R:" << red_pixels << " G:" << green_pixels << " B:" << blue_pixels << ")"
              << ", bright:" << bright_pixels
              << std::endl;
    return true;
}
