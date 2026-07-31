#include "test_framework.h"
#include "../src/core/engine.h"           // Use the actual Engine
#include "../src/particle.h"  // Now using the proper shared header!
#include "../src/core/particle_system.h"  // For ParticleSystem access
#include "../src/vision_system.h"    // For VisionSystem access
#include "../src/core/light_system.h"     // For LightSystem access
#include "../src/core/input_system.h"     // For InputSystem
#include "logosphere/physics/physics_system.h"   // For PhysicsSystem
#include <GLFW/glfw3.h>

// Global test registry
std::vector<TestCase> g_test_registry;

// EDUCATIONAL NOTE: No More Extern Dependencies
//
// We've eliminated all 'extern' dependencies to old global functions.
// TestEngine now manages everything through its own Engine instance.
// This makes tests self-contained and eliminates linking issues.

GLFWwindow* window = nullptr;  // TestEngine owns this

// Test Engine now properly owns and manages an Engine instance
// All system access goes through the Engine

TestEngine::TestEngine(bool headless) 
    : headless_mode(headless)
    , initialized(false)
    , engine_(nullptr) {
    // EDUCATIONAL NOTE: Why Pointers Instead of Direct Members?
    //
    // I'm using pointers here because we want to control when these
    // systems are created and destroyed. This gives us flexibility
    // to reset the test environment between tests.
}

TestEngine::~TestEngine() {
    cleanup();
}

bool TestEngine::initialize() {
    std::cout << "DEBUG: TestEngine::initialize() called" << std::endl;
    // Initialize GLFW even in headless mode (we need it for input simulation)
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW in test mode" << std::endl;
        return false;
    }
    
    if (!headless_mode) {
        // Create a visible window for debugging
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        
        window = glfwCreateWindow(1600, 1200, "Logosphere Test Mode", nullptr, nullptr);
        if (!window) {
            std::cerr << "Failed to create test window" << std::endl;
            glfwTerminate();
            return false;
        }
        
        // No callbacks needed - we'll simulate input directly through InputSystem
    } else {
        // In headless mode, we still need a window handle but it won't be shown
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        
        window = glfwCreateWindow(1600, 1200, "Headless Test", nullptr, nullptr);
        if (!window) {
            std::cerr << "Failed to create headless window" << std::endl;
            glfwTerminate();
            return false;
        }
    }
    
    // EDUCATIONAL NOTE: Fixing Test Framework After Refactoring
    //
    // I discovered the tests were segfaulting because TestEngine was trying
    // to use the old global framebuffer that we removed. Now we need to
    // initialize RenderSystem properly for tests.
    
    // Create and initialize the Engine
    engine_ = new Engine();
    EngineConfig config;
    config.window_width = 1600;
    config.window_height = 1200;
    config.create_display = false;  // Use Headless mode to avoid running tests 
    config.window_title = "Test Mode";
    config.target_fps = 60;
    
    std::cout << "DEBUG: TestEngine calling engine->initialize..." << std::endl;
    int result = engine_->initialize(config);
    std::cout << "DEBUG: Engine initialize returned: " << result << std::endl;
    if (result < 0) {
        std::cerr << "Failed to initialize Engine in test mode with error: " << result << std::endl;
        delete engine_;
        engine_ = nullptr;
        return false;
    }
    
    // EDUCATIONAL NOTE: Getting Systems from Engine
    //
    // We now get the systems directly from the Engine instance.
    // This ensures our tests use exactly the same systems as production.
    // No more duplicate systems or different code paths!
    
    // Enable the pixel debug buffer (runtime opt-in, tests only)
    engine_->get_framebuffer_buffer().set_debug_mode(true);
    engine_->get_render_buffer().set_debug_mode(true);
    
    // Initialize character through Engine's systems
    std::cout << "DEBUG: Getting particle system..." << std::endl;
    auto& particle_system = engine_->get_particle_system();
    std::cout << "DEBUG: Getting physics system..." << std::endl;
    auto& physics_system = engine_->get_physics_system();
    
    // Clear particles through particle system
    std::cout << "DEBUG: Clearing particles..." << std::endl;
    particle_system.clear_particles();
    particle_system.clear_spatial_memory();
    
    // Add character particle directly (no Character struct needed)
    Particle char_particle;
    char_particle.x = 0.0f;
    char_particle.y = 0.0f;
    char_particle.vx = 0.0f;
    char_particle.vy = 0.0f;
    char_particle.r = 0.0f;
    char_particle.g = 0.8f;
    char_particle.b = 0.6f;
    char_particle.a = 1.0f;
    char_particle.size = 1.5f;
    
    // Add through particle system
    std::cout << "DEBUG: Adding character particle..." << std::endl;
    engine_->add_particle(char_particle);
    
    std::cout << "DEBUG: TestEngine initialization complete!" << std::endl;
    initialized = true;
    return true;
}

void TestEngine::cleanup() {
    // Clean up the engine
    if (engine_) {
        delete engine_;  // Engine's destructor handles cleanup
        engine_ = nullptr;
    }
    
    if (window) {
        glfwDestroyWindow(window);
        window = nullptr;
    }
    glfwTerminate();
    initialized = false;
}

void TestEngine::tick(double delta_time) {
    if (!initialized || !engine_) return;
    
    // Poll events (even in headless mode)
    glfwPollEvents();
    
    // EDUCATIONAL NOTE: Using Engine's Update Method
    //
    // We now just call Engine::update() which handles everything.
    // This ensures tests use exactly the same code path as production.
    //
    // New way (same as Engine):
    //   1. InputSystem processes input and sets velocity
    //   2. PhysicsSystem moves character based on velocity
    //   3. PhysicsSystem SHOULD sync the particle (was missing!)
    
    // Let the engine handle all updates
    engine_->update(delta_time);
    
    // EDUCATIONAL NOTE: Character-Particle Sync in Tests
    //
    // I'm adding the same sync that Engine does. This ensures our tests
    // catch any issues with character movement. Previously, TestEngine
    // was calling the old update_character_movement() which included
    // this sync, so tests passed even when the real game was broken!
    
    // Render (engine already updated everything)
    engine_->render();
    
    // Display is handled internally by render() based on mode
}

void TestEngine::simulateKeyPress(int key) {
    if (!initialized || !engine_) return;
    // EDUCATIONAL NOTE: Simulating Input Through Engine's InputSystem
    //
    // We get the InputSystem from Engine and update its state.
    // This matches how GLFW callbacks work in the real game.
    auto& input_system = engine_->get_input_system();
    input_system.get_input_state().keys[key] = true;
}

void TestEngine::simulateKeyRelease(int key) {
    if (!initialized || !engine_) return;
    auto& input_system = engine_->get_input_system();
    input_system.get_input_state().keys[key] = false;
}

void TestEngine::simulateMouseMove(double x, double y) {
    if (!initialized || !engine_) return;
    auto& input_system = engine_->get_input_system();
    input_system.get_input_state().mouse_x = x;
    input_system.get_input_state().mouse_y = y;
}

void TestEngine::simulateMouseClick(int button) {
    if (!initialized || !engine_) return;
    auto& input_system = engine_->get_input_system();
    input_system.get_input_state().mouse_buttons[button] = true;
    // Simulate click release after a frame
    input_system.get_input_state().mouse_buttons[button] = false;
}

void TestEngine::createParticle(float x, float y, float r, float g, float b) {
    if (!engine_) return;
    
    Particle p;
    p.x = x;
    p.y = y;
    p.vx = 0.0f;
    p.vy = 0.0f;
    p.r = r;
    p.g = g;
    p.b = b;
    p.a = 1.0f;
    p.size = 1.0f;
    p.is_light_source = false;
    
    // Add through Engine's particle system
    engine_->add_particle(p);
}

void TestEngine::createWall(float start_x, float start_y, float end_x, float end_y) {
    if (!engine_) return;
    // Use Engine's particle system
    engine_->get_particle_system().create_wall(start_x, start_y, end_x, end_y, 0.5f);
}

void TestEngine::createLightSource(float x, float y, float intensity, float radius) {
    if (!engine_) return;
    
    Particle light;
    light.x = x;
    light.y = y;
    light.vx = 0.0f;
    light.vy = 0.0f;
    light.r = 1.0f;
    light.g = 1.0f;
    light.b = 0.8f;
    light.a = 1.0f;
    light.size = 0.5f;
    light.is_light_source = true;
    light.emission_strength = intensity;
    light.emission_radius = radius;
    
    // Add through Engine's particle system
    engine_->add_particle(light);
}

void TestEngine::setCharacterPosition(float x, float y) {
    if (!engine_) return;
    
    auto& physics_system = engine_->get_physics_system();
    auto& particle_system = engine_->get_particle_system();
    
    // Set character particle position directly (assuming particle 0 is character)
    if (particle_system.count() > 0) {
        auto particles_view = particle_system.lock_particles_for_write();
        particles_view[0].x = x;
        particles_view[0].y = y;
    }
}

float TestEngine::getCharacterX() const {
    if (!engine_) return 0.0f;
    auto& particle_system = engine_->get_particle_system();
    return (particle_system.count() > 0) ? particle_system.get_particle_copy(0).x : 0.0f;
}

float TestEngine::getCharacterY() const {
    if (!engine_) return 0.0f;
    auto& particle_system = engine_->get_particle_system();
    return (particle_system.count() > 0) ? particle_system.get_particle_copy(0).y : 0.0f;
}

int TestEngine::getParticleCount() const {
    if (!engine_) return 0;
    return static_cast<int>(engine_->get_particle_system().count());
}

bool TestEngine::isParticleAt(float x, float y, float tolerance) const {
    if (!engine_) return false;
    
    auto particles_view = engine_->get_particle_system().lock_particles_for_read();
    for (const auto& p : particles_view) {
        float dx = p.x - x;
        float dy = p.y - y;
        if (std::sqrt(dx*dx + dy*dy) < tolerance) {
            return true;
        }
    }
    return false;
}

bool TestEngine::isParticleLit(size_t particle_index) const {
    if (!engine_) return false;
    
    // With pixel-accurate lighting, we check if any surface can see a light
    // Get color of first surface to determine if lit
    auto color = engine_->get_light_system().get_surface_color_at(
        particle_index, 0,  // First surface
        0.5f, 0.5f,        // Center of surface
        255, 255, 255      // White base color
    );
    
    // If intensity > 0.5, particle is lit (not in shadow)
    return color.intensity > 0.5f;
}

float TestEngine::getParticleLightLevel(size_t particle_index) const {
    if (!engine_) return 0.0f;
    
    // With pixel-accurate lighting, we get the intensity from surface color
    auto color = engine_->get_light_system().get_surface_color_at(
        particle_index, 0,  // First surface
        0.5f, 0.5f,        // Center of surface
        255, 255, 255      // White base color
    );
    
    return color.intensity;
}

const std::vector<EnhancedPixel>& TestEngine::getEnhancedFramebuffer() const {
    static std::vector<EnhancedPixel> empty;
    if (!engine_) return empty;
    
    // Get the framebuffer's debug/enhanced buffer for tests
    const PixelBuffer& fb = engine_->get_framebuffer_buffer();
    return fb.get_buffer();  // This works with hybrid approach
}

bool TestEngine::isPixelColor(int x, int y, unsigned char r, unsigned char g, unsigned char b, 
                              unsigned char tolerance) const {
    if (!engine_) return false;
    
    auto& res = engine_->get_resolution_manager();
    if (x < 0 || x >= res.get_window_width() || y < 0 || y >= res.get_window_height()) {
        return false;
    }

    // Get enhanced pixel directly
    EnhancedPixel pixel = engine_->get_render_buffer().get_pixel(x, y);
    unsigned char fb_r = pixel.r;
    unsigned char fb_g = pixel.g;
    unsigned char fb_b = pixel.b;
    
    return (std::abs(fb_r - r) <= tolerance &&
            std::abs(fb_g - g) <= tolerance &&
            std::abs(fb_b - b) <= tolerance);
}