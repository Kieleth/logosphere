#ifndef UNIFIED_TEST_RUNNER_H
#define UNIFIED_TEST_RUNNER_H

#include "test_suite_coordinator.h"

// Debug configuration for tests
struct TestDebugConfig {
    bool debug_light_casting = false;
    bool debug_light_tracing = false;
    bool debug_light_intersections = false;
    bool debug_light_accumulation = false;
    bool debug_origin_lights = false;
    bool debug_light_verbose = false;
};

// Global test debug configuration (accessible by all tests)
extern TestDebugConfig g_test_debug_config;

// Main unified test runner class
class UnifiedTestRunner {
private:
    TestSuiteCoordinator coordinator_;
    bool verbose_mode_;
    TestDebugConfig debug_config_;
    
public:
    UnifiedTestRunner(bool verbose = false) 
        : coordinator_(verbose), verbose_mode_(verbose) {}
    
    // Set debug configuration for all tests
    void set_debug_config(const TestDebugConfig& config) {
        debug_config_ = config;
        g_test_debug_config = config;  // Set global for test access
    }
    
    // Run all engine test modules
    void run_all_tests();  // Old version without engine - being phased out
    void run_all_tests(class Engine& engine);  // New version with engine for TestContext
    
    // Run a specific test by name
    bool run_specific_test(const std::string& test_name, class Engine& engine);

    // Standalone tests create their own Engine and ignore TestContext.
    // Check before running to avoid double-Engine GPU resource duplication.
    bool is_standalone_test(const std::string& test_name) const;

    // Run a standalone test without needing an Engine reference.
    // The test creates its own Engine internally.
    bool run_standalone_test(const std::string& test_name);
    
    // Run tests for specific themes
    void run_core_engine_tests();
    void run_rendering_tests();
    void run_physics_tests();
    bool run_physics_tests(class Engine& engine);  // Run all physics tests as group
    void run_lighting_and_shadow_tests();  // NEW: Proper lighting/shadow module
    void run_interaction_tests();
    void run_mathematics_tests();
    
    // Get final results
    bool all_passed() const { return coordinator_.all_tests_passed(); }
    bool has_critical_failures() const { return coordinator_.has_critical_failures(); }
    // INDUSTRY BEST PRACTICE: Return number of failures as exit code
    // This gives more information than just 0/1 and matches Google Test behavior
    int get_exit_code() const { 
        int total_failed = coordinator_.get_total_tests() - coordinator_.get_total_passed();
        // Cap at 255 for systems that only support 8-bit exit codes
        return total_failed > 255 ? 255 : total_failed;
    }
};

#endif // UNIFIED_TEST_RUNNER_H