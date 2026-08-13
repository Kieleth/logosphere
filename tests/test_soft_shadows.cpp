// test_soft_shadows.cpp
// Visual test for soft shadow implementation
// Compares different light sizes and their effect on shadow penumbra

#include "../src/test_context.h"
#include "../src/particle.h"
#include "../src/materials.h"
#include <iostream>

bool test_soft_shadows(TestContext& ctx) {
    std::cout << "\n=== SOFT SHADOW VISUAL TEST ===" << std::endl;
    std::cout << "This test demonstrates soft shadows with different light sizes.\n";
    std::cout << "Larger lights should produce softer shadow edges.\n\n";

    // Clear any existing particles
    ctx.clear_particles();

    // Set up camera to view the scene from above-ish (SW isometric view)
    ctx.set_camera_position(-15.0f, -15.0f, 20.0f);
    ctx.set_camera_look_at(0.0f, 0.0f, 0.0f);

    // Create a floor using sleeping particles (is_sleeping = true skips physics)
    // Floor spans -10 to +10 in X and Y, at z=0
    for (int x = -10; x <= 10; x++) {
        for (int y = -10; y <= 10; y++) {
            Particle tile = {};
            tile.x = x * 1.0f;
            tile.y = y * 1.0f;
            // Rest the tile ON the turtle: bottom at z=0. The old z=-0.05
            // sank it 0.125 m below the world floor (default thickness).
            tile.z = tile.thickness * 0.5f;
            tile.size = 0.5f;
            tile.r = 0.8f;
            tile.g = 0.8f;
            tile.b = 0.8f;
            tile.a = 1.0f;
            tile.is_at_rest = true;    // Infinite mass, skip physics
            ctx.add_test_particle(tile);
        }
    }

    // Create three occluders (pillars) to cast shadows
    float pillar_positions[] = {-5.0f, 0.0f, 5.0f};
    for (int i = 0; i < 3; i++) {
        // Pillars are tall boxes (0.5x0.5x3m)
        Particle pillar = {};
        pillar.x = pillar_positions[i];
        pillar.y = 0.0f;
        pillar.z = 0.15f + 1.5f;  // rests on the tile tops (tile top at z=0.15)
        pillar.shape = ParticleShape::BOX;
        pillar.width = 0.5f;
        pillar.height = 0.5f;
        pillar.thickness = 3.0f;  // Tall pillar
        pillar.size = 3.0f;       // BVH bounding
        pillar.r = 0.6f;
        pillar.g = 0.4f;
        pillar.b = 0.3f;
        pillar.a = 1.0f;
        pillar.is_at_rest = true;    // Infinite mass, skip physics
        ctx.add_test_particle(pillar);
    }

    // Create three lights with DIFFERENT sizes
    // Light 1: Point light (size = 0.1) - hard shadows
    {
        Particle light = {};
        light.x = -5.0f;
        light.y = -5.0f;
        light.z = 8.0f;
        light.shape = ParticleShape::BOX;
        light.width = 0.1f;   // Tiny - should be nearly point light
        light.height = 0.1f;
        light.thickness = 0.1f;
        light.size = 0.1f;
        light.r = 1.0f;
        light.g = 0.9f;
        light.b = 0.8f;
        light.a = 1.0f;
        light.is_light_source = true;
        light.emission_strength = 500000.0f;
        light.emission_radius = 30.0f;
        ctx.add_test_particle(light);
        std::cout << "Light 1: size=0.1m (point-like, hard shadows)\n";
    }

    // Light 2: Medium area light (size = 1.0) - soft shadows
    {
        Particle light = {};
        light.x = 0.0f;
        light.y = -5.0f;
        light.z = 8.0f;
        light.shape = ParticleShape::BOX;
        light.width = 1.0f;   // Medium - soft shadows
        light.height = 1.0f;
        light.thickness = 0.5f;
        light.size = 1.0f;
        light.r = 0.9f;
        light.g = 1.0f;
        light.b = 0.8f;
        light.a = 1.0f;
        light.is_light_source = true;
        light.emission_strength = 500000.0f;
        light.emission_radius = 30.0f;
        ctx.add_test_particle(light);
        std::cout << "Light 2: size=1.0m (medium area, soft shadows)\n";
    }

    // Light 3: Large area light (size = 3.0) - very soft shadows
    {
        Particle light = {};
        light.x = 5.0f;
        light.y = -5.0f;
        light.z = 8.0f;
        light.shape = ParticleShape::BOX;
        light.width = 3.0f;   // Large - very soft shadows
        light.height = 3.0f;
        light.thickness = 1.0f;
        light.size = 3.0f;
        light.r = 0.8f;
        light.g = 0.9f;
        light.b = 1.0f;
        light.a = 1.0f;
        light.is_light_source = true;
        light.emission_strength = 500000.0f;
        light.emission_radius = 30.0f;
        ctx.add_test_particle(light);
        std::cout << "Light 3: size=3.0m (large area, very soft shadows)\n";
    }

    std::cout << "\nExpected results:\n";
    std::cout << "- Left pillar (small light): Sharp shadow edges\n";
    std::cout << "- Middle pillar (medium light): Soft shadow edges\n";
    std::cout << "- Right pillar (large light): Very soft shadow edges\n";
    std::cout << "\nShadow softness accumulates over ~4 frames.\n";

    // Update lighting and render (required for visual test)
    ctx.update_lighting();
    ctx.render();

    // === Shadow Probe Diagnostics ===
    // Define screen-space regions to analyze lighting quality
    // Coordinates are normalized 0.0-1.0 (x: left-right, y: top-bottom)
    auto& probe = ctx.get_shadow_probe();
    probe.clear_regions();

    // In isometric view from SW (-15,-15,20):
    // - Floor extends across most of screen
    // - Pillars create shadow bands
    // - Areas between camera and pillars should be lit
    // - Areas behind pillars (relative to lights) should be shadow

    // Region 1: Center of screen - should have mixed lighting (pillars + lit areas)
    probe.add_screen_region("screen_center", {0.4f, 0.6f, 0.4f, 0.6f}, RegionExpectation::Lit);

    // Region 2: Upper left quadrant - likely lit (between lights and pillars)
    probe.add_screen_region("upper_left", {0.1f, 0.3f, 0.2f, 0.4f}, RegionExpectation::Lit);

    // Region 3: Lower right quadrant - likely shadow (behind pillars)
    probe.add_screen_region("lower_right", {0.7f, 0.9f, 0.6f, 0.8f}, RegionExpectation::Shadow);

    // Region 4: Full screen analysis to detect overall variance
    probe.add_screen_region("full_screen", {0.0f, 1.0f, 0.0f, 1.0f}, RegionExpectation::Lit);

    // Run analysis and log results
    ctx.analyze_shadows();

    std::cout << "\nControls:\n";
    std::cout << "  WASD - Move camera\n";
    std::cout << "  G - Toggle light visualization\n";

    std::cout << "\nSoft shadow test scene ready.\n";
    return true;
}
