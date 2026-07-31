// Logosphere Test Runner
// Standalone test executable that uses Engine as a library

#include "test_harness.h"
#include "unified_test_runner.h"  // For TestDebugConfig
#include <iostream>
#include <string>
#include <cstring>

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --test [test_name]        Run specific test (or all if no name given)" << std::endl;
    std::cout << "  --visual-test             Run tests with visual debugging" << std::endl;
    std::cout << "  --verbose                 Enable verbose output" << std::endl;
    std::cout << "  --help                    Show this help message" << std::endl;
    std::cout << "" << std::endl;
    std::cout << "Lighting Debug Options:" << std::endl;
    std::cout << "  --debug-light-casting     Log light casting start/end" << std::endl;
    std::cout << "  --debug-light-tracing     Log individual ray directions" << std::endl;
    std::cout << "  --debug-light-intersections Log ray-surface intersections" << std::endl;
    std::cout << "  --debug-light-accumulation  Log light accumulation on surfaces" << std::endl;
    std::cout << "  --debug-origin-lights     Enable debug for lights at origin" << std::endl;
    std::cout << "  --debug-light-verbose     Enable verbose lighting output" << std::endl;
    std::cout << "  --debug-light-all         Enable ALL lighting debug modes" << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "Logosphere Test Runner Starting..." << std::endl;
    
    // Parse command line arguments
    bool verbose = false;
    bool visual_mode = false;
    std::string specific_test;
    TestDebugConfig debug_config;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--test") {
            // Check if next argument is a test name
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                specific_test = argv[i + 1];
                std::cout << "Running specific test: " << specific_test << std::endl;
                i++; // Skip the test name in next iteration
            } else {
                std::cout << "Running all tests" << std::endl;
            }
        } else if (arg == "--visual-test") {
            visual_mode = true;
            std::cout << "Visual test mode enabled" << std::endl;
        } else if (arg == "--verbose") {
            verbose = true;
            std::cout << "Verbose output enabled" << std::endl;
        } else if (arg == "--debug-light-casting") {
            debug_config.debug_light_casting = true;
            std::cout << "Enabled: Light casting debug" << std::endl;
        } else if (arg == "--debug-light-tracing") {
            debug_config.debug_light_tracing = true;
            std::cout << "Enabled: Light ray tracing debug" << std::endl;
        } else if (arg == "--debug-light-intersections") {
            debug_config.debug_light_intersections = true;
            std::cout << "Enabled: Light ray intersection debug" << std::endl;
        } else if (arg == "--debug-light-accumulation") {
            debug_config.debug_light_accumulation = true;
            std::cout << "Enabled: Light accumulation debug" << std::endl;
        } else if (arg == "--debug-origin-lights") {
            debug_config.debug_origin_lights = true;
            std::cout << "Enabled: Origin lights debug" << std::endl;
        } else if (arg == "--debug-light-verbose") {
            debug_config.debug_light_verbose = true;
            std::cout << "Enabled: Verbose lighting debug" << std::endl;
        } else if (arg == "--debug-light-all") {
            debug_config.debug_light_casting = true;
            debug_config.debug_light_tracing = true;
            debug_config.debug_light_intersections = true;
            debug_config.debug_light_accumulation = true;
            debug_config.debug_origin_lights = true;
            debug_config.debug_light_verbose = true;
            std::cout << "Enabled: ALL lighting debug modes" << std::endl;
        } else if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
    }
    
    // Create test harness
    TestHarness harness(verbose);
    harness.set_debug_config(debug_config);
    
    // Run appropriate test mode
    int exit_code;
    if (visual_mode) {
        exit_code = harness.run_visual_tests();
    } else {
        exit_code = harness.run_tests(specific_test);
    }
    
    std::cout << "Test runner complete. Exit code: " << exit_code << std::endl;
    return exit_code;
}