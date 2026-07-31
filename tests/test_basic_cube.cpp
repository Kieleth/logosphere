// KISS - Simplest possible cube face test
#include <iostream>
#include "../src/test_context.h"
#include "../src/particle_geometry_v2.h"
#include "../src/particle_geometry_v2.h"
#include "../src/core/particle_system.h"

bool test_basic_cube(TestContext& ctx) {
    std::cout << "\n=== BASIC CUBE TEST ===" << std::endl;
    std::cout << "According to docs/ARCHITECTURE.md:" << std::endl;
    std::cout << "  X+ = East,  X- = West" << std::endl;
    std::cout << "  Y+ = North, Y- = South" << std::endl;
    std::cout << "  Z+ = Up,    Z- = Down\n" << std::endl;
    
    // Create a cube using V2 geometry with explicit vertices
    ParticleGeometryV2::CubeGeometryV2 cube(2.0f);  // Size 2 (so faces are at ±1)
    ParticleGeometryV2::Transform transform(ParticleGeometryV2::Vec3(0, 0, 0), 0, 0, 0);
    auto surfaces = ParticleGeometryV2::geometry_to_surfaces(cube, transform);
    
    const char* expected_names[] = {
        "North(+Y)", "South(-Y)", "Top(+Z)", 
        "Bottom(-Z)", "East(+X)", "West(-X)"
    };
    
    std::cout << "Cube surfaces (expecting 6):" << std::endl;
    bool all_correct = true;
    
    for (size_t i = 0; i < surfaces.size(); i++) {
        const auto& s = surfaces[i];
        std::cout << "Surface " << i << " (" << expected_names[i] << "):" << std::endl;
        std::cout << "  Position: (" << s.x << ", " << s.y << ", " << s.z << ")" << std::endl;
        std::cout << "  Normal:   (" << s.nx << ", " << s.ny << ", " << s.nz << ")" << std::endl;
        
        // Verify each face
        bool correct = false;
        switch(i) {
            case 0: // North (+Y)
                correct = (s.y == 1.0f && s.ny == 1.0f);
                if (!correct) std::cout << "  ERROR: North face should be at y=1 with normal=(0,1,0)" << std::endl;
                break;
            case 1: // South (-Y)
                correct = (s.y == -1.0f && s.ny == -1.0f);
                if (!correct) std::cout << "  ERROR: South face should be at y=-1 with normal=(0,-1,0)" << std::endl;
                break;
            case 2: // Top (+Z)
                correct = (s.z == 1.0f && s.nz == 1.0f);
                if (!correct) std::cout << "  ERROR: Top face should be at z=1 with normal=(0,0,1)" << std::endl;
                break;
            case 3: // Bottom (-Z)
                correct = (s.z == -1.0f && s.nz == -1.0f);
                if (!correct) std::cout << "  ERROR: Bottom face should be at z=-1 with normal=(0,0,-1)" << std::endl;
                break;
            case 4: // East (+X)
                correct = (s.x == 1.0f && s.nx == 1.0f);
                if (!correct) std::cout << "  ERROR: East face should be at x=1 with normal=(1,0,0)" << std::endl;
                break;
            case 5: // West (-X)
                correct = (s.x == -1.0f && s.nx == -1.0f);
                if (!correct) std::cout << "  ERROR: West face should be at x=-1 with normal=(-1,0,0)" << std::endl;
                break;
        }
        if (!correct) all_correct = false;
    }
    
    std::cout << "\n=== ISOMETRIC VIEW TEST ===" << std::endl;
    std::cout << "Standard isometric camera at (+1,+1,+1) looking at origin:" << std::endl;
    std::cout << "Should see 3 faces:" << std::endl;
    std::cout << "  - South (-Y) facing camera" << std::endl;
    std::cout << "  - West (-X) facing camera" << std::endl;
    std::cout << "  - Top (+Z) facing camera" << std::endl;
    
    // Check which faces would be visible
    float cam_x = 1, cam_y = 1, cam_z = 1;  // Camera direction
    std::cout << "\nDot products (positive = facing camera):" << std::endl;
    for (size_t i = 0; i < surfaces.size(); i++) {
        const auto& s = surfaces[i];
        float dot = s.nx * cam_x + s.ny * cam_y + s.nz * cam_z;
        std::cout << "  " << expected_names[i] << ": " << dot;
        if (dot > 0) std::cout << " [VISIBLE]";
        std::cout << std::endl;
    }
    
    if (all_correct) {
        std::cout << "\n✅ All cube faces are correct!" << std::endl;
        return true;
    } else {
        std::cout << "\n❌ Cube faces are WRONG!" << std::endl;
        return false;
    }
}