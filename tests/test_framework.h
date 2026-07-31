#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <vector>
#include <string>
#include <iostream>
#include <cmath>
#include "logosphere/rendering/pixel_buffer.h"  // EnhancedPixel

// EDUCATIONAL NOTE: TestEngine Refactoring
//
// I'm updating TestEngine to use the same modular systems as Engine.
// This ensures our tests catch bugs that would affect the real game.
//
// Key principle: Tests should exercise the same code paths as production.
// Otherwise, we get false positives - tests pass but the game is broken.

// Forward declarations to avoid including main.mm here
struct Particle;
class Engine;

// Test engine that wraps our framebuffer-based engine
class TestEngine {
public:
    TestEngine(bool headless = true);
    ~TestEngine();
    
    // Initialize engine in test mode
    bool initialize();
    
    // Clean up resources
    void cleanup();
    
    // Run single frame with fixed timestep
    void tick(double delta_time = 1.0/60.0);
    
    // Simulate input
    void simulateKeyPress(int key);
    void simulateKeyRelease(int key);
    void simulateMouseMove(double x, double y);
    void simulateMouseClick(int button);
    
    // Create test entities
    void createParticle(float x, float y, float r = 1.0f, float g = 1.0f, float b = 1.0f);
    void createWall(float start_x, float start_y, float end_x, float end_y);
    void createLightSource(float x, float y, float intensity, float radius);
    
    // Set character position directly for testing
    void setCharacterPosition(float x, float y);
    
    // Query engine state
    float getCharacterX() const;
    float getCharacterY() const;
    int getParticleCount() const;
    bool isParticleAt(float x, float y, float tolerance = 0.1f) const;
    bool isParticleLit(size_t particle_index) const;
    float getParticleLightLevel(size_t particle_index) const;
    
    // Get enhanced framebuffer for pixel-level verification
    const std::vector<EnhancedPixel>& getEnhancedFramebuffer() const;
    int getFramebufferWidth() const { return 800; }
    int getFramebufferHeight() const { return 600; }
    
    // Check pixel color at specific location
    bool isPixelColor(int x, int y, unsigned char r, unsigned char g, unsigned char b, 
                      unsigned char tolerance = 10) const;
    
    // Access to the Engine for tests that need direct system access
    Engine* getEngine() { return engine_; }
    const Engine* getEngine() const { return engine_; }
    
private:
    bool headless_mode;
    bool initialized;
    
    // Use the actual Engine for consistency
    // This ensures tests catch the same bugs as production
    Engine* engine_;
    
    // No more separate system pointers - everything goes through Engine
};

// Test assertion macros
#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << "TEST FAILED: " << message << " at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while(0)

#define TEST_ASSERT_EQUAL(expected, actual, message) \
    TEST_ASSERT((expected) == (actual), message << " (expected: " << expected << ", actual: " << actual << ")")

#define TEST_ASSERT_NEAR(expected, actual, tolerance, message) \
    TEST_ASSERT(std::abs((expected) - (actual)) < (tolerance), \
                message << " (expected: " << expected << ", actual: " << actual << ", diff: " << std::abs((expected) - (actual)) << ")")

// Test registration
typedef bool (*TestFunction)();

struct TestCase {
    std::string name;
    TestFunction func;
};

// Global test registry
extern std::vector<TestCase> g_test_registry;

// Macro to register tests
#define REGISTER_TEST(name, func) \
    static struct TestRegistrar_##func { \
        TestRegistrar_##func() { \
            g_test_registry.push_back({name, func}); \
        } \
    } test_registrar_##func;

#endif // TEST_FRAMEWORK_H