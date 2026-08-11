#include <iostream>
#include <cmath>
#include "../src/test_context.h"
#include "../src/core/particle_system.h"
#include "../src/core/camera_system.h"
#include "../src/projection_system.h"
#include "../src/particle_geometry_v2.h"

// Test cube projections by verifying the actual projected 2D polygons
bool test_basic_cube_projections(TestContext& ctx) {
    std::cout << "\n=== BASIC CUBE PROJECTIONS TEST ===" << std::endl;
    std::cout << "Verifying 3D cube surfaces project correctly to 2D screen space" << std::endl;
    
    // =========================================================================
    // STEP 1: Setup test environment
    // =========================================================================
    std::cout << "\n[STEP 1] Setting up test environment..." << std::endl;
    
    // Clear any existing state
    ctx.clear_state();
    
    // Configure isometric projection
    // According to docs/ARCHITECTURE.md:
    // - Isometric viewing direction: FROM Southwest-below looking Northeast-up
    // - Camera typically at (-distance, -distance, height) looking at origin
    ctx.camera_system.set_projection_system(std::make_unique<IsometricProjection>());
    ctx.camera_system.set_viewport(800, 600);  // Standard test viewport
    
    // Position camera at standard isometric position
    // Camera is SOUTHWEST (-X,-Y) and ABOVE (+Z) the origin
    float cam_x = -10.0f;  // West of origin (negative X = West in world space)
    float cam_y = -10.0f;  // South of origin (negative Y = South in world space)  
    float cam_z = 20.0f;   // Above origin (positive Z = Up in world space)
    ctx.set_camera_position(cam_x, cam_y, cam_z);
    ctx.set_camera_look_at(0.0f, 0.0f, 0.0f);  // Look at world origin
    
    std::cout << "  Camera position (world space): (" << cam_x << ", " << cam_y << ", " << cam_z << ")" << std::endl;
    std::cout << "  Camera looking at (world space): (0, 0, 0)" << std::endl;
    std::cout << "  Viewport: 800x600 pixels" << std::endl;
    
    // =========================================================================
    // STEP 2: Create test cube
    // =========================================================================
    std::cout << "\n[STEP 2] Creating test cube..." << std::endl;
    
    // Create a cube at world origin with size 2
    // This means the cube extends from -1 to +1 on each axis
    // The comment above says the cube spans -1..+1 on each axis, but its Z
    // extent is add_cube_particle's thickness (0.15), not `size`. So at z = 0
    // its underside sat 0.075 m below the turtle — the last violation in the
    // harness. Raising it by exactly half that thickness puts the bottom on
    // the floor and moves the centre 7.5 cm, which the projection checks
    // below tolerate; a full `size/2` lift would not be the real geometry.
    float cube_x = 0.0f, cube_y = 0.0f, cube_z = 0.075f;  // bottom on the turtle
    float cube_size = 2.0f;
    int cube_id = ctx.add_cube_particle(cube_x, cube_y, cube_z, cube_size, 255, 255, 255);
    
    std::cout << "  Cube center (world space): (" << cube_x << ", " << cube_y << ", " << cube_z << ")" << std::endl;
    std::cout << "  Cube size: " << cube_size << " (extends ±1 on each axis)" << std::endl;
    std::cout << "  Cube ID: " << cube_id << std::endl;
    
    // =========================================================================
    // STEP 3: Get cube surfaces in world space
    // =========================================================================
    std::cout << "\n[STEP 3] Getting cube surfaces in world space..." << std::endl;
    
    auto particle = ctx.particle_system.get_particle_copy(cube_id);
    auto surfaces = particle.GetSurfaces();  // Particle knows how to generate its surfaces
    
    std::cout << "  Retrieved " << surfaces.size() << " surfaces" << std::endl;
    
    // With triangles, we now have 12 surfaces (2 triangles per cube face)
    // Expected surface order from TriangleCubeGeometry:
    // 0-1: North (+Y), 2-3: South (-Y), 4-5: Top (+Z)
    // 6-7: Bottom (-Z), 8-9: East (+X), 10-11: West (-X)
    const char* face_names[] = {
        "North(+Y)-T1", "North(+Y)-T2", "South(-Y)-T1", "South(-Y)-T2",
        "Top(+Z)-T1", "Top(+Z)-T2", "Bottom(-Z)-T1", "Bottom(-Z)-T2",
        "East(+X)-T1", "East(+X)-T2", "West(-X)-T1", "West(-X)-T2"
    };
    
    // =========================================================================
    // STEP 4: Determine expected visibility
    // =========================================================================
    std::cout << "\n[STEP 4] Determining which faces should be visible..." << std::endl;
    
    // From camera at (-10, -10, 20) looking at origin:
    // We are SOUTHWEST and ABOVE the cube
    // Therefore we should see:
    // - South face (-Y): We're south of the cube, so we see its south face
    // - West face (-X): We're west of the cube, so we see its west face  
    // - Top face (+Z): We're above the cube, so we see its top face
    // Both triangles of each visible face should be visible
    bool expected_visible[] = {
        false, false,  // North (+Y) triangles - facing away from us
        true, true,    // South (-Y) triangles - facing toward us
        true, true,    // Top (+Z) triangles - facing toward us (we're above)
        false, false,  // Bottom (-Z) triangles - hidden underneath
        false, false,  // East (+X) triangles - facing away from us
        true, true     // West (-X) triangles - facing toward us
    };
    
    for (int i = 0; i < 12; i++) {
        std::cout << "  " << face_names[i] << ": " 
                  << (expected_visible[i] ? "SHOULD BE VISIBLE" : "should be hidden") << std::endl;
    }
    
    // =========================================================================
    // STEP 5: Project each surface and verify
    // =========================================================================
    std::cout << "\n[STEP 5] Projecting surfaces to 2D screen space..." << std::endl;
    
    bool all_correct = true;
    
    // Store projections for later relative position checks
    // Now we have 12 triangles (2 per face)
    CameraSystem::ProjectedTriangle projections[12];
    
    for (size_t i = 0; i < surfaces.size(); i++) {
        const auto& surf = surfaces[i];
        std::cout << "\n  Testing " << face_names[i] << ":" << std::endl;
        std::cout << "    World position: (" << surf.x << ", " << surf.y << ", " << surf.z << ")" << std::endl;
        std::cout << "    World normal: (" << surf.nx << ", " << surf.ny << ", " << surf.nz << ")" << std::endl;
        std::cout << "    Surface dimensions: " << surf.width << "x" << surf.height << std::endl;
        
        // TEST CULLING: Check if surface should be culled
        bool should_render = ctx.camera_system.should_render_surface(
            surf.x, surf.y, surf.z,  // Surface position
            surf.nx, surf.ny, surf.nz,  // Surface normal
            surf.width, surf.height);  // Surface dimensions
        
        std::cout << "    Culling result: " << (should_render ? "RENDER" : "CULLED") << std::endl;
        
        // Verify culling matches expectations
        if (should_render != expected_visible[i]) {
            std::cout << "      ❌ CULLING ERROR: Expected " 
                      << (expected_visible[i] ? "RENDER" : "CULLED") << std::endl;
            all_correct = false;
        } else {
            std::cout << "      ✓ Culling correct" << std::endl;
        }
        
        // Use stored vertices directly from V2 geometry
        std::cout << "    World space corners:" << std::endl;
        for (int c = 0; c < surf.vertex_count; c++) {
            std::cout << "      Corner " << c << ": (" 
                      << surf.vertices[c][0] << ", " << surf.vertices[c][1] << ", " << surf.vertices[c][2] << ")" << std::endl;
        }
        
        // Only project surfaces that should be rendered (not culled)
        if (should_render) {
            // Check if this is actually a triangle (vertex_count == 3)
            if (surf.vertex_count != 3) {
                std::cout << "      ❌ ERROR: Not a triangle (vertex_count=" << surf.vertex_count << ")" << std::endl;
                all_correct = false;
                continue;
            }
            
            // Project the triangle from 3D world space to 2D screen space
            const float (&vertices)[3][3] = (const float (&)[3][3])surf.vertices;
            auto projected = ctx.camera_system.project_triangle(vertices);
            projections[i] = projected;  // Store for later checks
            
            std::cout << "    Projection result:" << std::endl;
            std::cout << "      On screen: " << (projected.on_screen ? "YES" : "NO") << std::endl;
            
            if (projected.on_screen) {
            // Display the 2D bounding box
            std::cout << "      Screen space bounding box: (" 
                      << projected.min_x << "," << projected.min_y << ") to (" 
                      << projected.max_x << "," << projected.max_y << ")" << std::endl;
            
            // Display the actual 2D polygon corners (screen space)
            std::cout << "      Screen space triangle corners:" << std::endl;
            for (int c = 0; c < 3; c++) {
                std::cout << "        Corner " << c << ": (" 
                          << projected.screen_corners[c][0] << ", " 
                          << projected.screen_corners[c][1] << ")" << std::endl;
            }
            
            // Calculate polygon dimensions
            int width = projected.max_x - projected.min_x;
            int height = projected.max_y - projected.min_y;
            std::cout << "      Polygon dimensions: " << width << "x" << height << " pixels" << std::endl;
            
            // Check if visibility matches expectation
            if (!expected_visible[i]) {
                std::cout << "      ❌ ERROR: Face should NOT be visible but is on screen!" << std::endl;
                all_correct = false;
            } else {
                std::cout << "      ✓ Correctly visible" << std::endl;
            }
            
            // Basic sanity checks
            if (width <= 0 || height <= 0) {
                std::cout << "      ❌ ERROR: Invalid polygon dimensions!" << std::endl;
                all_correct = false;
            }
            
            if (width > 400 || height > 400) {
                std::cout << "      ❌ ERROR: Polygon unreasonably large!" << std::endl;
                all_correct = false;
            }
            
            } else {
                // Surface should render but is off-screen
                std::cout << "      ❌ ERROR: Face should be visible but is OFF SCREEN!" << std::endl;
                all_correct = false;
            }
        } else {
            // Surface was culled, no need to project
            std::cout << "    Skipping projection (surface culled)" << std::endl;
        }
    }
    
    // =========================================================================
    // STEP 6: Verify relative positions of visible faces
    // =========================================================================
    std::cout << "\n[STEP 6] Verifying relative positions of projected faces..." << std::endl;
    
    // Get projections for the three visible faces (using triangle indices)
    // South face (-Y): triangles 2,3  Top face (+Z): triangles 4,5  West face (-X): triangles 10,11
    const auto& south_proj = projections[2];  // South (-Y) first triangle
    const auto& top_proj = projections[4];    // Top (+Z) first triangle
    const auto& west_proj = projections[10];  // West (-X) first triangle
    
    std::cout << "  Visible face screen bounds:" << std::endl;
    std::cout << "    South face: (" << south_proj.min_x << "," << south_proj.min_y 
              << ") to (" << south_proj.max_x << "," << south_proj.max_y << ")" << std::endl;
    std::cout << "    Top face: (" << top_proj.min_x << "," << top_proj.min_y 
              << ") to (" << top_proj.max_x << "," << top_proj.max_y << ")" << std::endl;
    std::cout << "    West face: (" << west_proj.min_x << "," << west_proj.min_y 
              << ") to (" << west_proj.max_x << "," << west_proj.max_y << ")" << std::endl;
    
    // In isometric projection with camera above:
    // - Top face should appear higher on screen (smaller Y values)
    // - Remember: screen Y=0 is at top, Y increases downward
    if (top_proj.min_y < south_proj.max_y) {
        std::cout << "    ✓ Top face appears above South face (correct)" << std::endl;
    } else {
        std::cout << "    ❌ Top face NOT above South face (projection error!)" << std::endl;
        all_correct = false;
    }
    
    if (top_proj.min_y < west_proj.max_y) {
        std::cout << "    ✓ Top face appears above West face (correct)" << std::endl;
    } else {
        std::cout << "    ❌ Top face NOT above West face (projection error!)" << std::endl;
        all_correct = false;
    }
    
    // Check that faces meet at shared edges
    // The projections should share some overlapping bounds where faces meet
    bool south_west_adjacent = (south_proj.min_x <= west_proj.max_x && 
                                south_proj.max_x >= west_proj.min_x &&
                                south_proj.min_y <= west_proj.max_y && 
                                south_proj.max_y >= west_proj.min_y);
    
    if (south_west_adjacent) {
        std::cout << "    ✓ South and West faces share an edge (correct)" << std::endl;
    } else {
        std::cout << "    ⚠️  South and West faces don't appear to share an edge" << std::endl;
    }
    
    // =========================================================================
    // STEP 7: Debug Eden's specific cube placement
    // =========================================================================
    std::cout << "\n[STEP 7] Testing Eden's exact cube configuration..." << std::endl;
    std::cout << "Eden places cube at (0, 8, 3) with size 6" << std::endl;
    
    // Create Eden's exact cube
    int eden_cube_id = ctx.add_cube_particle(0.0f, 8.0f, 3.0f, 6.0f, 255, 255, 255);
    auto eden_particle = ctx.particle_system.get_particle_copy(eden_cube_id);
    auto eden_surfaces = eden_particle.GetSurfaces();
    
    std::cout << "\nAnalyzing Eden cube visibility from camera at (-10, -10, 20):" << std::endl;
    
    for (size_t i = 0; i < eden_surfaces.size(); i++) {
        const auto& surf = eden_surfaces[i];
        std::cout << "\n" << face_names[i] << ":" << std::endl;
        std::cout << "  Position: (" << surf.x << ", " << surf.y << ", " << surf.z << ")" << std::endl;
        std::cout << "  Normal: (" << surf.nx << ", " << surf.ny << ", " << surf.nz << ")" << std::endl;
        
        // Calculate view direction
        float view_x = surf.x - cam_x;
        float view_y = surf.y - cam_y; 
        float view_z = surf.z - cam_z;
        float length = sqrt(view_x*view_x + view_y*view_y + view_z*view_z);
        view_x /= length;
        view_y /= length;
        view_z /= length;
        
        // Dot product
        float dot = surf.nx * view_x + surf.ny * view_y + surf.nz * view_z;
        
        // Check culling
        bool should_render = ctx.camera_system.should_render_surface(
            surf.x, surf.y, surf.z, surf.nx, surf.ny, surf.nz, surf.width, surf.height);
        
        std::cout << "  View direction: (" << view_x << ", " << view_y << ", " << view_z << ")" << std::endl;
        std::cout << "  Dot product: " << dot << std::endl;
        std::cout << "  Result: " << (should_render ? "VISIBLE" : "CULLED") << std::endl;
        
        if (should_render) {
            std::cout << "  -> This face WILL be rendered in Eden" << std::endl;
            if (i == 0) std::cout << "     COLOR: RED" << std::endl;
            if (i == 1) std::cout << "     COLOR: GREEN" << std::endl;
            if (i == 2) std::cout << "     COLOR: BLUE" << std::endl;
            if (i == 3) std::cout << "     COLOR: YELLOW" << std::endl;
            if (i == 4) std::cout << "     COLOR: CYAN" << std::endl;
            if (i == 5) std::cout << "     COLOR: MAGENTA" << std::endl;
        }
    }
    
    std::cout << "\nExpected to see: South (GREEN), West (MAGENTA), Top (BLUE)" << std::endl;
    std::cout << "You report seeing: Cyan (EAST?), Blue (TOP?), Yellow (BOTTOM?)" << std::endl;
    
    // =========================================================================
    // STEP 8: Final verdict
    // =========================================================================
    std::cout << "\n[STEP 7] Test results:" << std::endl;
    
    if (all_correct) {
        std::cout << "\n✅ CUBE PROJECTION TEST PASSED!" << std::endl;
        std::cout << "All cube faces project correctly to 2D screen space." << std::endl;
        std::cout << "The 3D->2D projection pipeline is working correctly." << std::endl;
        return true;
    } else {
        std::cout << "\n❌ CUBE PROJECTION TEST FAILED!" << std::endl;
        std::cout << "Some cube faces are not projecting correctly." << std::endl;
        std::cout << "The projection system has errors that need fixing." << std::endl;
        return false;
    }
}