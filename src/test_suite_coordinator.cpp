#include "test_suite_coordinator.h"
#include "test_formatter.h"
#include <algorithm>
#include <unordered_map>
#include <iostream>
#include <iomanip>
#include <chrono>

void TestSuiteCoordinator::add_module_result(const TestModuleResult& result) {
    all_results_.push_back(result);
    total_suite_time_ms_ += result.duration_ms;
}

template<typename TestFunc>
void TestSuiteCoordinator::run_module(const std::string& module_name, const std::string& theme, 
                                     TestFunc test_function, bool critical) {
    auto start = std::chrono::high_resolution_clock::now();
    
    // Run the test function and capture result
    TestModuleResult result = test_function();
    
    auto end = std::chrono::high_resolution_clock::now();
    double duration = std::chrono::duration<double, std::milli>(end - start).count();
    
    // Override timing if module provided its own
    if (result.duration_ms == 0.0) {
        result.duration_ms = duration;
    }
    
    // Ensure module info is set correctly
    result.module_name = module_name;
    result.theme = theme;
    result.critical = critical;
    
    add_module_result(result);
}

void TestSuiteCoordinator::organize_by_themes() {
    theme_results_.clear();
    std::unordered_map<std::string, ThemeResult> theme_map;
    
    // Group results by theme
    for (const auto& result : all_results_) {
        if (theme_map.find(result.theme) == theme_map.end()) {
            theme_map[result.theme] = ThemeResult{result.theme};
        }
        theme_map[result.theme].add_module(result);
    }
    
    // Convert to vector and sort by theme name
    for (const auto& pair : theme_map) {
        theme_results_.push_back(pair.second);
    }
    
    std::sort(theme_results_.begin(), theme_results_.end(),
              [](const ThemeResult& a, const ThemeResult& b) {
                  return a.theme_name < b.theme_name;
              });
}

void TestSuiteCoordinator::print_theme_summary(const ThemeResult& theme) const {
    // Simple format: Theme: passed/total (status)
    std::string status = theme.has_critical_failures() ? "CRITICAL" : 
                        theme.all_passed() ? "PASS" : "PARTIAL";
    
    std::cout << theme.theme_name << ": " << theme.total_passed << "/" << theme.total_tests 
              << " (" << status << ")" << std::endl;
    
    // Show failed modules only
    if (!theme.all_passed()) {
        for (const auto& module : theme.modules) {
            if (!module.all_passed()) {
                std::cout << "  " << module.module_name << ": " << module.passed << "/" << module.total;
                if (!module.critical) std::cout << " [non-critical]";
                std::cout << std::endl;
            }
        }
    }
}

void TestSuiteCoordinator::print_overall_summary() const {
    int total_passed = get_total_passed();
    int total_tests = get_total_tests();
    bool critical_failures = has_critical_failures();
    
    std::cout << "\nTEST SUMMARY" << std::endl;
    std::cout << "Total: " << total_passed << "/" << total_tests;
    
    if (critical_failures) {
        std::cout << " (CRITICAL FAILURES)";
    } else if (total_passed == total_tests) {
        std::cout << " (ALL PASSED)";
    } else {
        std::cout << " (PARTIAL)";
    }
    
    std::cout << std::endl;
    std::cout << "Duration: " << total_suite_time_ms_ << "ms" << std::endl;
}

void TestSuiteCoordinator::generate_final_report() {
    organize_by_themes();
    
    int total_passed = get_total_passed();
    int total_tests = get_total_tests();
    int total_failed = total_tests - total_passed;
    
    // INDUSTRY BEST PRACTICE: Clear separator and title
    std::cout << "\n=================================================================" << std::endl;
    std::cout << "LOGOSPHERE TEST SUITE COMPLETE" << std::endl;
    std::cout << "=================================================================" << std::endl;
    
    // CRITICAL: Summary stats FIRST, like Google Test.
    // std::dec defensively: a test that leaked std::hex into cout once
    // printed "Passed: 1b" in the release-gate summary.
    std::cout << std::dec;
    std::cout << "Test Modules Run: " << all_results_.size() << std::endl;
    std::cout << "Total Tests: " << total_tests << std::endl;
    std::cout << "Passed: " << total_passed << std::endl;
    
    // Use color if terminal supports it (simplified - just use bold for now)
    if (total_failed > 0) {
        std::cout << "FAILED: " << total_failed << std::endl;
    }
    
    std::cout << "Duration: " << std::fixed << std::setprecision(1) 
              << total_suite_time_ms_ / 1000.0 << " seconds" << std::endl;
    
    // MOST IMPORTANT: List of failed tests BY NAME, like all pro frameworks
    if (!failures_.empty()) {
        std::cout << "\nFAILED TESTS:" << std::endl;
        int failure_num = 1;
        for (const auto& failure : failures_) {
            std::cout << failure_num++ << ". " << failure.module_name;
            if (!failure.test_name.empty()) {
                std::cout << " > " << failure.test_name;
            }
            if (!failure.reason.empty()) {
                std::cout << " - " << failure.reason;
            }
            std::cout << std::endl;
        }
    } else if (total_failed > 0) {
        // We have failures but no detailed failure records
        std::cout << "\nFAILED MODULES (run with --verbose for details):" << std::endl;
        int module_num = 1;
        for (const auto& result : all_results_) {
            if (!result.all_passed()) {
                std::cout << module_num++ << ". " << result.module_name 
                          << " (" << result.passed << "/" << result.total << " passed)" << std::endl;
            }
        }
    }
    
    // Final verdict - CLEAR AND OBVIOUS
    std::cout << "\n";
    if (total_failed == 0) {
        std::cout << "✅ ALL TESTS PASSED!" << std::endl;
    } else {
        std::cout << "❌ " << total_failed << " TEST" << (total_failed > 1 ? "S" : "") << " FAILED" << std::endl;
        std::cout << "Exit Code: " << total_failed << std::endl;
    }
    
    std::cout << "=================================================================" << std::endl;
}

bool TestSuiteCoordinator::all_tests_passed() const {
    return std::all_of(all_results_.begin(), all_results_.end(),
                       [](const TestModuleResult& result) { return result.all_passed(); });
}

bool TestSuiteCoordinator::has_critical_failures() const {
    return std::any_of(all_results_.begin(), all_results_.end(),
                       [](const TestModuleResult& result) { 
                           return result.critical && !result.all_passed(); 
                       });
}

int TestSuiteCoordinator::get_total_passed() const {
    int total = 0;
    for (const auto& result : all_results_) {
        total += result.passed;
    }
    return total;
}

int TestSuiteCoordinator::get_total_tests() const {
    int total = 0;
    for (const auto& result : all_results_) {
        total += result.total;
    }
    return total;
}

double TestSuiteCoordinator::get_total_duration_ms() const {
    return total_suite_time_ms_;
}

void TestSuiteCoordinator::add_failure(const std::string& module, const std::string& test, const std::string& reason) {
    failures_.push_back({module, test, reason});
}

void TestSuiteCoordinator::print_failure_details() const {
    std::cout << "\n╭─────────────────────────────────────────────────────────────╮\n";
    std::cout << "│ FAILED TESTS DETAIL                                        │\n";
    std::cout << "╰─────────────────────────────────────────────────────────────╯\n";
    
    for (const auto& failure : failures_) {
        std::cout << "  ✗ " << failure.module_name;
        if (!failure.test_name.empty()) {
            std::cout << " :: " << failure.test_name;
        }
        if (!failure.reason.empty()) {
            std::cout << "\n    Reason: " << failure.reason;
        }
        std::cout << "\n";
    }
}