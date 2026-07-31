// test_shadow_diagonal.cpp
// Debug test to reproduce diagonal shadow pattern issue
// Setup: 4 trees at N/S/E/W, sun at (-150,50,50), vision cone

#include "../src/core/engine.h"
#include "../src/test_context.h"
#include "../src/materials.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <cmath>
#include <cstdlib>

bool test_shadow_diagonal(TestContext& /* ctx */) {
    std::cout << "\n=== Shadow Diagonal Pattern Debug Test ===" << std::endl;
    std::cout << "Setup: 4 trees at N/S/E/W, sun at (-150,50,50)" << std::endl;

    const char* interactive_env = std::getenv("INTERACTIVE");
    bool is_interactive = (interactive_env != nullptr && std::string(interactive_env) == "1");

    if (!is_interactive) {
        std::cout << "SKIPPED: Run with INTERACTIVE=1 for visual test" << std::endl;
        return true;
    }

    // ========================================
    // ENGINE INIT
    // ========================================
    Engine engine;
    EngineConfig config;
    config.mode = EngineMode::Interactive;
    config.window_width = 1600;
    config.window_height = 1200;
    config.window_title = "Shadow Diagonal Debug";
    config.enable_chat_window = false;

    if (engine.initialize(config) != 0) {
        std::cout << "Engine init failed" << std::endl;
        return false;
    }

    auto& ps = engine.get_particle_system();

    // ========================================
    // SUN - EXACT position from logomancers/src/main.cpp:1044-1046
    // ========================================
    std::cout << "\n1. Creating SUN at (-150, 50, 50) - Northwest, 50m up" << std::endl;
    Particle sun = {};
    sun.x = -150.0f;
    sun.y = 50.0f;
    sun.z = 50.0f;
    sun.size = 5.0f;
    sun.width = 5.0f; sun.height = 5.0f; sun.thickness = 5.0f;
    sun.SetMaterial(Materials::Type::LIGHT);
    sun.is_light_source = true;
    sun.emission_strength = 100000000.0f;  // 100M lumens
    sun.emission_radius = 1000.0f;         // 1km radius
    sun.r = 1.0f; sun.g = 0.95f; sun.b = 0.9f; sun.a = 1.0f;
    engine.add_particle(sun);

    // ========================================
    // 4 TREES at compass positions (centered at 0,0)
    // ========================================
    const float TREE_DIST = 10.0f;
    const float TREE_SIZE = 4.0f;
    const float TREE_Z = TREE_SIZE / 2.0f;

    std::cout << "\n2. Creating 4 trees at compass positions:" << std::endl;

    // NORTH tree
    Particle tree_n = {};
    tree_n.x = 0.0f; tree_n.y = TREE_DIST; tree_n.z = TREE_Z;
    tree_n.shape = ParticleShape::BOX;
    tree_n.width = TREE_SIZE; tree_n.height = TREE_SIZE; tree_n.thickness = TREE_SIZE;
    tree_n.r = 0.2f; tree_n.g = 0.6f; tree_n.b = 0.2f; tree_n.a = 1.0f;
    tree_n.SetMaterial(Materials::Type::WOOD_HARD);
    engine.add_particle(tree_n);
    std::cout << "   NORTH tree at (0, " << TREE_DIST << ", " << TREE_Z << ")" << std::endl;

    // SOUTH tree
    Particle tree_s = {};
    tree_s.x = 0.0f; tree_s.y = -TREE_DIST; tree_s.z = TREE_Z;
    tree_s.shape = ParticleShape::BOX;
    tree_s.width = TREE_SIZE; tree_s.height = TREE_SIZE; tree_s.thickness = TREE_SIZE;
    tree_s.r = 0.3f; tree_s.g = 0.7f; tree_s.b = 0.3f; tree_s.a = 1.0f;
    tree_s.SetMaterial(Materials::Type::WOOD_HARD);
    engine.add_particle(tree_s);
    std::cout << "   SOUTH tree at (0, " << -TREE_DIST << ", " << TREE_Z << ")" << std::endl;

    // EAST tree
    Particle tree_e = {};
    tree_e.x = TREE_DIST; tree_e.y = 0.0f; tree_e.z = TREE_Z;
    tree_e.shape = ParticleShape::BOX;
    tree_e.width = TREE_SIZE; tree_e.height = TREE_SIZE; tree_e.thickness = TREE_SIZE;
    tree_e.r = 0.25f; tree_e.g = 0.65f; tree_e.b = 0.25f; tree_e.a = 1.0f;
    tree_e.SetMaterial(Materials::Type::WOOD_HARD);
    engine.add_particle(tree_e);
    std::cout << "   EAST tree at (" << TREE_DIST << ", 0, " << TREE_Z << ")" << std::endl;

    // WEST tree
    Particle tree_w = {};
    tree_w.x = -TREE_DIST; tree_w.y = 0.0f; tree_w.z = TREE_Z;
    tree_w.shape = ParticleShape::BOX;
    tree_w.width = TREE_SIZE; tree_w.height = TREE_SIZE; tree_w.thickness = TREE_SIZE;
    tree_w.r = 0.35f; tree_w.g = 0.75f; tree_w.b = 0.35f; tree_w.a = 1.0f;
    tree_w.SetMaterial(Materials::Type::WOOD_HARD);
    engine.add_particle(tree_w);
    std::cout << "   WEST tree at (" << -TREE_DIST << ", 0, " << TREE_Z << ")" << std::endl;

    // ========================================
    // FLOOR
    // ========================================
    Particle floor = {};
    floor.x = 0.0f; floor.y = 0.0f; floor.z = -0.05f;
    floor.shape = ParticleShape::BOX;
    floor.width = 40.0f; floor.height = 40.0f; floor.thickness = 0.1f;
    floor.r = 0.4f; floor.g = 0.35f; floor.b = 0.3f; floor.a = 1.0f;
    floor.SetMaterial(Materials::Type::HEAVY_STATIC);
    floor.is_at_rest = true;
    engine.add_particle(floor);
    std::cout << "\n3. Floor at (0, 0, -0.05)" << std::endl;

    // ========================================
    // CAMERA (SW looking NE, like logomancers)
    // ========================================
    auto& camera = engine.get_camera_system();
    camera.set_position(-20.0f, -20.0f, 25.0f);
    camera.look_at(0.0f, 0.0f, 0.0f);
    std::cout << "\n4. Camera at (-20,-20,25) looking at (0,0,0)" << std::endl;

    // ========================================
    // VISION CONE - START DISABLED to isolate shadow issue
    // ========================================
    engine.set_vision_cone_enabled(false);  // START DISABLED
    engine.set_vision_cone(0.0f, 0.0f, 0.0f, 2.0f, 50.0f);  // viewer at origin, looking north
    std::cout << "5. Vision cone DISABLED (press V to enable)" << std::endl;

    // ========================================
    // DEBUG: Expected lighting analysis
    // ========================================
    // Sun at (-150, 50, 50), all trees ~160m away
    // Light direction from tree → sun is roughly (-0.9, +0.3, +0.3)
    //
    // Visible faces (camera at SW):
    // - South face: normal (0,-1,0) · light_dir = -0.3 → lambertian=0 (NO LIGHT)
    // - West face:  normal (-1,0,0) · light_dir = +0.9 → lambertian=0.9 (BRIGHT)
    // - Top face:   normal (0,0,1)  · light_dir = +0.3 → lambertian=0.3 (SOME)
    //
    // All 4 trees should have IDENTICAL lighting patterns!
    // If diagonal pattern appears → it's NOT the physics, it's post-processing
    std::cout << "\n=== EXPECTED: All 4 trees should have identical lighting ===" << std::endl;
    std::cout << "    West face: BRIGHT (lambertian ~0.9)" << std::endl;
    std::cout << "    Top face: DIM (lambertian ~0.3)" << std::endl;
    std::cout << "    South face: DARK (lambertian 0)" << std::endl;

    // ========================================
    // MAIN LOOP
    // ========================================
    std::cout << "\nPress ESC to exit, WASD to move camera target" << std::endl;
    std::cout << "Press V to toggle vision cone" << std::endl;
    std::cout << "Press G to toggle light visualization" << std::endl;

    GLFWwindow* window = static_cast<GLFWwindow*>(engine.get_platform()->get_native_window_handle());
    auto& input = engine.get_input_system();

    float viewer_x = 0.0f, viewer_y = 0.0f;
    float look_dir = 0.0f;
    bool vision_enabled = false;  // Start disabled to isolate shadow issue
    int frame = 0;

    while (!glfwWindowShouldClose(window)) {
        engine.get_platform()->poll_events();

        const auto& state = input.get_input_state();
        if (state.keys[GLFW_KEY_ESCAPE]) {
            break;
        }

        // Move viewer with WASD
        float move_speed = 0.2f;
        if (state.keys[GLFW_KEY_W]) viewer_y += move_speed;
        if (state.keys[GLFW_KEY_S]) viewer_y -= move_speed;
        if (state.keys[GLFW_KEY_A]) viewer_x -= move_speed;
        if (state.keys[GLFW_KEY_D]) viewer_x += move_speed;

        // Rotate look direction with Q/E
        if (state.keys[GLFW_KEY_Q]) look_dir += 0.02f;
        if (state.keys[GLFW_KEY_E]) look_dir -= 0.02f;

        // Toggle vision cone with V (debounced)
        static bool v_was_pressed = false;
        if (state.keys[GLFW_KEY_V] && !v_was_pressed) {
            vision_enabled = !vision_enabled;
            engine.set_vision_cone_enabled(vision_enabled);
            std::cout << "Vision cone: " << (vision_enabled ? "ON" : "OFF") << std::endl;
        }
        v_was_pressed = state.keys[GLFW_KEY_V];

        // Update vision cone position
        engine.set_vision_cone(viewer_x, viewer_y, look_dir, 2.0f, 50.0f);

        // Update and render
        engine.update(0.016f);
        engine.render();
        engine.present();  // Actually display to screen!

        frame++;
        if (frame % 300 == 0) {
            std::cout << "Frame " << frame << " - Viewer at (" << viewer_x << ", " << viewer_y
                      << ") look_dir=" << look_dir << std::endl;
        }
    }

    std::cout << "\n=== Test Complete ===" << std::endl;
    return true;
}
