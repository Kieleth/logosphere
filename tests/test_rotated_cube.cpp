// Test that cube rotation produces correct surface orientations
#include "../src/test_context.h"
#include "../src/core/particle_system.h"
#include "../src/particle_geometry_v2.h"
#include "../src/particle_geometry_v2.h"
#include <iostream>
#include <cmath>

bool test_rotated_cube(TestContext& ctx) {
    std::cout << "\n=== Testing Rotated Cube Faces ===" << std::endl;
    
    // Clear any existing particles
    ctx.clear_particles();
    
    // Test 1: Unrotated cube
    std::cout << "\n--- Test 1: Unrotated Cube ---" << std::endl;
    {
        Particle cube;
        cube.x = 0; cube.y = 0; cube.z = 0;
        cube.size = 2.0f;
        // No rotation
        cube.rotation_x = 0;
        cube.rotation_y = 0;
        cube.rotation_z = 0;
        
        ParticleGeometryV2::CubeGeometryV2 geom(cube.size);
        ParticleGeometryV2::Transform transform(
            ParticleGeometryV2::Vec3(cube.x, cube.y, cube.z),
            cube.rotation_x, cube.rotation_y, cube.rotation_z
        );
        auto surfaces = ParticleGeometryV2::geometry_to_surfaces(geom, transform);
        
        // Check that we get 6 surfaces
        if (surfaces.size() != 6) {
            std::cout << "ERROR: Expected 6 surfaces, got " << surfaces.size() << std::endl;
            return false;
        }
        
        // Check south face (index 1) normal points south
        const Surface& south = surfaces[1];
        std::cout << "South face normal: (" << south.nx << ", " << south.ny << ", " << south.nz << ")" << std::endl;
        if (std::abs(south.ny + 1.0f) > 0.01f) {
            std::cout << "ERROR: South face normal should be (0, -1, 0)" << std::endl;
            return false;
        }
        std::cout << "✓ Unrotated cube faces are correct" << std::endl;
    }
    
    // Test 2: 90-degree Z rotation
    std::cout << "\n--- Test 2: 90° Z Rotation ---" << std::endl;
    {
        Particle cube;
        cube.x = 0; cube.y = 0; cube.z = 0;
        cube.size = 2.0f;
        cube.rotation_x = 0;
        cube.rotation_y = 0;
        cube.rotation_z = M_PI / 2;  // 90 degrees
        
        ParticleGeometryV2::CubeGeometryV2 geom(cube.size);
        ParticleGeometryV2::Transform transform(
            ParticleGeometryV2::Vec3(cube.x, cube.y, cube.z),
            cube.rotation_x, cube.rotation_y, cube.rotation_z
        );
        auto surfaces = ParticleGeometryV2::geometry_to_surfaces(geom, transform);
        
        // After 90° Z rotation:
        // What was pointing North (+Y) now points West (-X)
        // What was pointing South (-Y) now points East (+X)
        const Surface& rotated_south = surfaces[1];
        std::cout << "Rotated 'south' face normal: (" 
                  << rotated_south.nx << ", " << rotated_south.ny << ", " << rotated_south.nz << ")" << std::endl;
        
        if (std::abs(rotated_south.nx - 1.0f) > 0.01f) {
            std::cout << "ERROR: After 90° Z rotation, south face should point East (+X)" << std::endl;
            return false;
        }
        std::cout << "✓ 90° Z rotation works correctly" << std::endl;
    }
    
    // Test 3: 45-degree X rotation
    std::cout << "\n--- Test 3: 45° X Rotation ---" << std::endl;
    {
        Particle cube;
        cube.x = 0; cube.y = 0; cube.z = 0;
        cube.size = 2.0f;
        cube.rotation_x = M_PI / 4;  // 45 degrees
        cube.rotation_y = 0;
        cube.rotation_z = 0;
        
        ParticleGeometryV2::CubeGeometryV2 geom(cube.size);
        ParticleGeometryV2::Transform transform(
            ParticleGeometryV2::Vec3(cube.x, cube.y, cube.z),
            cube.rotation_x, cube.rotation_y, cube.rotation_z
        );
        auto surfaces = ParticleGeometryV2::geometry_to_surfaces(geom, transform);
        
        // After 45° X rotation, the top face should tilt forward
        const Surface& top = surfaces[2];  // Top face
        std::cout << "Rotated top face normal: (" 
                  << top.nx << ", " << top.ny << ", " << top.nz << ")" << std::endl;
        
        // Should have both Y and Z components
        if (std::abs(top.ny) < 0.5f || std::abs(top.nz) < 0.5f) {
            std::cout << "ERROR: 45° X rotation should tilt top face" << std::endl;
            return false;
        }
        std::cout << "✓ 45° X rotation works correctly" << std::endl;
    }
    
    // Test 4: Combined rotations
    std::cout << "\n--- Test 4: Combined Rotations ---" << std::endl;
    {
        Particle cube;
        cube.x = 0; cube.y = 0; cube.z = 0;
        cube.size = 2.0f;
        cube.rotation_x = M_PI / 6;  // 30 degrees
        cube.rotation_y = M_PI / 4;  // 45 degrees
        cube.rotation_z = M_PI / 3;  // 60 degrees
        
        ParticleGeometryV2::CubeGeometryV2 geom(cube.size);
        ParticleGeometryV2::Transform transform(
            ParticleGeometryV2::Vec3(cube.x, cube.y, cube.z),
            cube.rotation_x, cube.rotation_y, cube.rotation_z
        );
        auto surfaces = ParticleGeometryV2::geometry_to_surfaces(geom, transform);
        
        // Just verify we get 6 surfaces and they have non-trivial normals
        if (surfaces.size() != 6) {
            std::cout << "ERROR: Combined rotations should still produce 6 surfaces" << std::endl;
            return false;
        }
        
        // Check that normals are still unit vectors
        for (size_t i = 0; i < surfaces.size(); i++) {
            float len = std::sqrt(surfaces[i].nx * surfaces[i].nx + 
                                surfaces[i].ny * surfaces[i].ny + 
                                surfaces[i].nz * surfaces[i].nz);
            if (std::abs(len - 1.0f) > 0.01f) {
                std::cout << "ERROR: Surface " << i << " normal not normalized (length=" << len << ")" << std::endl;
                return false;
            }
        }
        std::cout << "✓ Combined rotations maintain valid normals" << std::endl;
    }
    
    std::cout << "\n=== Summary ===" << std::endl;
    std::cout << "✓ All rotation tests passed!" << std::endl;
    std::cout << "Cube geometry correctly applies 3D rotations" << std::endl;
    
    return true;
}