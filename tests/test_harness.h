#ifndef TEST_HARNESS_H
#define TEST_HARNESS_H

#include <string>

// Forward declarations
class Engine;
class UnifiedTestRunner;
struct TestDebugConfig;

// TestHarness - Standalone test executor that uses Engine as a library
// This separates test execution from the engine itself, following the principle
// that "The engine CANNOT run standalone" - it's a library used by tests
class TestHarness {
private:
    Engine* engine_;
    UnifiedTestRunner* test_runner_;
    bool verbose_;
    
public:
    TestHarness(bool verbose = false);
    ~TestHarness();
    
    // Run all tests or a specific test
    // Returns 0 on success, non-zero on failure (for CI/CD)
    int run_tests(const std::string& specific_test = "");
    
    // Run visual/interactive tests (future expansion)
    int run_visual_tests();
    
    // Configure debug output for tests
    void set_debug_config(const TestDebugConfig& config);
    
private:
    // Initialize engine in headless mode for testing
    bool initialize_engine();
    
    // Clean up engine after tests
    void cleanup_engine();
};

#endif // TEST_HARNESS_H