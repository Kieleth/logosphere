#include "test_harness.h"
#include "../src/core/engine.h"
#include "../src/test_context.h"
#include "../src/ui/ui_system.h"
#include "../src/ui/widgets.h"
#include "../src/core/camera_system.h"
#include "../src/core/particle_system.h"
#include "unified_test_runner.h"
#include <iostream>
#include <GLFW/glfw3.h>

TestHarness::TestHarness(bool verbose)
    : engine_(nullptr)
    , test_runner_(nullptr)
    , verbose_(verbose) {
}

TestHarness::~TestHarness() {
    cleanup_engine();
    delete test_runner_;
}

bool TestHarness::initialize_engine() {
    // Create engine instance
    engine_ = new Engine();
    
    // Configure for headless testing (no window)
    EngineConfig config;
    config.create_display = false;  // No window needed for tests
    config.window_width = 1600;
    config.window_height = 1200;
    config.window_title = "Logosphere Test Harness";
    config.target_fps = 60.0;
    config.show_debug_overlay = false;
    config.show_shadowcasting_debug = false;
    config.show_performance_metrics = false;
    
    // Initialize engine
    int observer_id = engine_->initialize(config);
    if (observer_id < 0) {
        std::cerr << "Failed to initialize engine for testing" << std::endl;
        delete engine_;
        engine_ = nullptr;
        return false;
    }
    
    if (verbose_) {
        std::cout << "Test harness: Engine initialized successfully" << std::endl;
    }
    
    return true;
}

void TestHarness::cleanup_engine() {
    if (engine_) {
        engine_->shutdown();
        delete engine_;
        engine_ = nullptr;
    }
}

int TestHarness::run_tests(const std::string& specific_test) {
    std::cout << "\n=== Logosphere Test Harness ===" << std::endl;
    
    // Initialize engine for testing
    if (!initialize_engine()) {
        std::cerr << "ERROR: Failed to initialize engine for testing" << std::endl;
        return 1;
    }
    
    // Create test runner
    test_runner_ = new UnifiedTestRunner(verbose_);
    
    int exit_code = 0;
    
    // Run specific test or all tests
    if (!specific_test.empty()) {
        std::cout << "\n[SPECIFIC TEST] Running: " << specific_test << std::endl;

        // STANDALONE tests (e.g. test_ssgi_visual) create their own Engine internally.
        // Having TWO engines alive simultaneously doubles GPU resources (4 Metal devices,
        // 56 worker threads, 6+ frame buffers) and can crash the system on Retina displays.
        // Fix: destroy the harness engine BEFORE running standalone tests.
        if (test_runner_->is_standalone_test(specific_test)) {
            std::cout << "[HARNESS] Standalone test detected — releasing harness engine "
                      << "to avoid GPU resource duplication\n";
            cleanup_engine();
        }

        bool test_passed;
        if (engine_) {
            test_passed = test_runner_->run_specific_test(specific_test, *engine_);
        } else {
            // Harness engine was cleaned up for standalone test.
            // Run directly — standalone tests create their own Engine.
            test_passed = test_runner_->run_standalone_test(specific_test);
        }
        exit_code = test_passed ? 0 : 1;
    } else {
        std::cout << "\n[ALL TESTS] Running complete test suite" << std::endl;
        test_runner_->run_all_tests(*engine_);
        exit_code = test_runner_->get_exit_code();
    }
    
    // Report results
    if (exit_code == 0) {
        std::cout << "\n✅ All tests passed!" << std::endl;
    } else {
        std::cout << "\n❌ Tests failed with exit code: " << exit_code << std::endl;
    }
    
    // Clean up
    cleanup_engine();
    
    return exit_code;
}

int TestHarness::run_visual_tests() {
    std::cout << "\n=== VISUAL TEST MODE WITH MENU ===" << std::endl;
    std::cout << "Creating engine with window for interactive test inspection..." << std::endl;
    std::cout << "\nMenu Controls:" << std::endl;
    std::cout << "  UP/DOWN - Navigate menu" << std::endl;
    std::cout << "  ENTER - Select test" << std::endl;
    std::cout << "  G - Toggle light visualization (show as white cubes)" << std::endl;
    std::cout << "  +/- - Adjust UI scale" << std::endl;
    
    // Create engine configuration for visual mode
    EngineConfig config;
    config.mode = EngineMode::Interactive;  // Use interactive mode with window
    config.window_width = 1600;
    config.window_height = 1200;
    config.show_debug_overlay = false;  // Start without debug overlay
    config.show_kg_inspector = true;    // Enable KG inspector for particle selection
    
    // Create engine with window
    engine_ = new Engine();
    if (engine_->initialize(config) < 0) {
        std::cerr << "Failed to initialize engine in visual mode!" << std::endl;
        return 1;
    }
    
    // Create test runner
    test_runner_ = new UnifiedTestRunner();
    test_runner_->set_debug_config(TestDebugConfig());
    
    // Get UI system from engine
    UISystem* ui_system = engine_->get_ui_system();
    if (!ui_system) {
        std::cerr << "Failed to get UI system!" << std::endl;
        cleanup_engine();
        return 1;
    }
    
    // Create retained mode menu
    ui::ListMenu* test_menu = ui_system->create_list_menu("test_selector");
    test_menu->set_bounds(600, 200, 400, 500);  // Center the menu
    
    // Populate menu with available tests
    test_menu->add_item("Soft Shadows (Area Lights)", "test_soft_shadows");
    test_menu->add_item("Triangle Shadow Artifacts", "test_triangle_shadow_artifacts");
    test_menu->add_item("Basic Cube", "test_basic_cube");
    test_menu->add_item("Eden Shadows", "test_eden_shadows");
    test_menu->add_item("Wall Rendering Issue", "test_wall_rendering_issue");
    test_menu->add_item("Top Lighting", "visual_test_top");
    test_menu->add_item("UV Coordinates", "test_uv_coordinates");
    test_menu->add_item("Basic Cube Projections", "test_basic_cube_projections");
    test_menu->add_item("Performance: 20 Particles", "test_performance_single_light_20");
    test_menu->add_item("Shadow Casting", "test_shadow_casting");
    test_menu->add_item("Surface Ray Tracing", "test_surface_ray_tracing");
    test_menu->add_item("Layered Floor v3", "test_layered_floor_v3");
    test_menu->add_item("Strata Generator", "test_strata_generator");
    test_menu->add_item("─────────────────────", "separator");  // Visual separator
    test_menu->add_item("Exit Visual Test Mode", "exit");
    
    // Select first item by default
    test_menu->select_item(0);
    
    // Set focus to the menu so it receives keyboard events
    ui_system->set_focused_widget(test_menu);
    
    // Create a title label
    ui::Label* title_label = new ui::Label("SELECT TEST", "title");
    title_label->set_position(750, 150);
    title_label->set_color(255, 255, 100);
    title_label->set_ui_system(ui_system);  // Give widget access to UI system
    ui_system->add_widget(title_label);
    
    // Track menu visibility
    bool menu_visible = true;
    test_menu->set_visible(true);
    title_label->set_visible(true);
    
    // Create a "Menu" button that's always visible during tests
    ui::Button* menu_button = new ui::Button("MENU", "menu_button");
    menu_button->set_position(10, 10);  // Top-left corner
    menu_button->set_size(80, 30);
    menu_button->set_ui_system(ui_system);
    menu_button->set_visible(false);  // Hidden when menu is visible
    ui_system->add_widget(menu_button);
    
    // Main loop with menu handling
    bool running = true;
    std::string current_test;
    
    // Set up menu button callback
    menu_button->on_click = [&]() {
        // Return to menu
        test_menu->set_visible(true);
        title_label->set_visible(true);
        menu_button->set_visible(false);
        menu_visible = true;
        
        // Set focus back to menu
        ui_system->set_focused_widget(test_menu);
        
        // Clear the scene
        engine_->get_particle_system().clear_particles();
        
        // Reset camera
        engine_->get_camera_system().set_position(-10.0f, -10.0f, 20.0f);
        engine_->get_camera_system().look_at(0.0f, 0.0f, 0.0f);
    };
    
    // Set up menu callback
    test_menu->on_item_activated = [&](const std::string& test_id) {
        if (test_id == "exit") {
            // Exit visual test mode
            running = false;
            std::cout << "\nExiting visual test mode..." << std::endl;
        } else if (test_id == "separator") {
            // Ignore separator selection
            return;
        } else if (!test_id.empty()) {
            current_test = test_id;
            // Hide menu, show menu button
            test_menu->set_visible(false);
            title_label->set_visible(false);
            menu_button->set_visible(true);  // Show menu button during tests
            menu_visible = false;

            // Clear any existing scene
            engine_->get_particle_system().clear_particles();

            // Run the selected test
            std::cout << "\nRunning test: " << current_test << std::endl;
            test_runner_->run_specific_test(current_test, *engine_);
            
            // Show controls
            std::cout << "\nControls:" << std::endl;
            std::cout << "  WASD - Move camera" << std::endl;
            std::cout << "  P - Cycle projection mode" << std::endl;
            std::cout << "  G - Toggle light visualization (show as white cubes)" << std::endl;
            std::cout << "  +/- - Increase/decrease UI scale" << std::endl;
            std::cout << "  ESC or click 'MENU' - Return to menu" << std::endl;
        }
    };
    
    // Track key states for proper press/release detection
    bool up_pressed = false;
    bool down_pressed = false;
    bool enter_pressed = false;
    bool esc_pressed = false;
    bool plus_pressed = false;
    bool minus_pressed = false;
    
    std::cout << "[Test Harness] Before main loop: running=" << running 
              << ", engine->is_running()=" << engine_->is_running() << std::endl;
    
    // Add proper frame timing
    using Clock = std::chrono::high_resolution_clock;
    auto last_frame_time = Clock::now();
    
    int loop_count = 0;  // Move outside loop!
    while (running && engine_->is_running()) {
        loop_count++;
        
        // Engine polls events internally in update()
        
        GLFWwindow* window = static_cast<GLFWwindow*>(engine_->get_window_handle());
        
        // Get mouse position for UI
        double mouse_x, mouse_y;
        glfwGetCursorPos(window, &mouse_x, &mouse_y);
        ui_system->handle_mouse_move(static_cast<int>(mouse_x), static_cast<int>(mouse_y));
        
        // Handle mouse clicks for UI only when menu is visible
        // When menu is not visible, let the engine's input system handle everything
        if (menu_visible || menu_button->is_visible()) {
            static bool mouse_was_pressed = false;
            bool mouse_pressed = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            
            if (mouse_pressed && !mouse_was_pressed) {
                // Mouse button down - let UI handle it
                ui_system->handle_mouse_down(static_cast<int>(mouse_x), static_cast<int>(mouse_y), 0);
            } else if (!mouse_pressed && mouse_was_pressed) {
                // Mouse button up
                ui_system->handle_mouse_up(static_cast<int>(mouse_x), static_cast<int>(mouse_y), 0);
            }
            
            mouse_was_pressed = mouse_pressed;
        }
        // When in test mode (menu not visible), engine's input system handles mouse through callbacks
        
        // Handle menu visibility
        if (menu_visible) {
            // Handle keyboard navigation
            bool up_now = glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS;
            bool down_now = glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS;
            bool enter_now = glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS;
            
            // Up arrow
            if (up_now && !up_pressed) {
                ui_system->handle_key_down(GLFW_KEY_UP);
            } else if (!up_now && up_pressed) {
                ui_system->handle_key_up(GLFW_KEY_UP);
            }
            up_pressed = up_now;
            
            // Down arrow
            if (down_now && !down_pressed) {
                ui_system->handle_key_down(GLFW_KEY_DOWN);
            } else if (!down_now && down_pressed) {
                ui_system->handle_key_up(GLFW_KEY_DOWN);
            }
            down_pressed = down_now;
            
            // Enter key
            if (enter_now && !enter_pressed) {
                ui_system->handle_key_down(GLFW_KEY_ENTER);
            } else if (!enter_now && enter_pressed) {
                ui_system->handle_key_up(GLFW_KEY_ENTER);
            }
            enter_pressed = enter_now;
        } else {
            // In test mode - check for ESC to return to menu
            bool esc_now = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
            if (esc_now && !esc_pressed) {
                // Return to menu
                test_menu->set_visible(true);
                title_label->set_visible(true);
                menu_button->set_visible(false);  // Hide menu button
                menu_visible = true;
                
                // Set focus back to menu
                ui_system->set_focused_widget(test_menu);
                
                // Clear the scene
                engine_->get_particle_system().clear_particles();
                
                // Reset camera
                engine_->get_camera_system().set_position(-10.0f, -10.0f, 20.0f);
                engine_->get_camera_system().look_at(0.0f, 0.0f, 0.0f);
            }
            esc_pressed = esc_now;
        }
        
        // Always run engine update with delta time (menu or not)
        // This ensures rendering and UI updates happen
        engine_->update(1.0 / 60.0);
        
        // Check for G key to toggle light visualization (works in both menu and test modes)
        static bool g_pressed = false;
        bool g_now = glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS;
        
        // Debug output to see if key is being detected
        static int g_debug_counter = 0;
        if (g_now && g_debug_counter++ % 60 == 0) {  // Print every 60 frames while held
            std::cout << "[DEBUG] G key is pressed (frame " << g_debug_counter << ")" << std::endl;
        }
        
        if (g_now && !g_pressed) {
            std::cout << "[DEBUG] G key press detected - toggling light visualization" << std::endl;
            engine_->toggle_light_visualization();
            bool lights_visible = engine_->get_show_lights_as_white();
            std::cout << "Light visualization: " << (lights_visible ? "ON (white cubes)" : "OFF (normal lighting)") << std::endl;
        }
        g_pressed = g_now;
        
        // Handle UI scale adjustment (works in both menu and test modes)
        bool shift_pressed = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || 
                            glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
        
        // For plus: check both = key (which becomes + with shift) and numpad +
        bool plus_now = (glfwGetKey(window, GLFW_KEY_EQUAL) == GLFW_PRESS && shift_pressed) ||  // Shift+= for +
                        glfwGetKey(window, GLFW_KEY_KP_ADD) == GLFW_PRESS;    // Numpad +
        bool minus_now = glfwGetKey(window, GLFW_KEY_MINUS) == GLFW_PRESS ||  // - key
                         glfwGetKey(window, GLFW_KEY_KP_SUBTRACT) == GLFW_PRESS; // Numpad -
        
        // Plus key for increasing UI scale
        if (plus_now && !plus_pressed) {
            ui_system->increase_ui_scale();
        }
        plus_pressed = plus_now;
        
        // Minus key for decreasing UI scale
        if (minus_now && !minus_pressed) {
            ui_system->decrease_ui_scale();
        }
        minus_pressed = minus_now;
        
        // UI update is handled by engine->update()

        // Render and present
        engine_->render();
        engine_->present();
        
        // Check for window close
        if (glfwWindowShouldClose(window)) {
            running = false;
        }
    }
    
    // Clean up
    cleanup_engine();
    return 0;
}

void TestHarness::set_debug_config(const TestDebugConfig& config) {
    if (test_runner_) {
        test_runner_->set_debug_config(config);
    }
}