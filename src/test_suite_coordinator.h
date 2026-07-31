#ifndef TEST_SUITE_COORDINATOR_H
#define TEST_SUITE_COORDINATOR_H

#include <string>
#include <vector>
#include <functional>

// Test result structure for individual test modules
struct TestModuleResult {
    std::string module_name;
    std::string theme;          // "Core Engine", "Rendering", "Physics", etc.
    int passed;
    int total;
    double duration_ms;
    bool critical;              // If false, module failure doesn't fail overall suite
    
    TestModuleResult(const std::string& name, const std::string& theme_name, 
                    int pass_count, int total_count, double time_ms, bool is_critical = true)
        : module_name(name), theme(theme_name), passed(pass_count), 
          total(total_count), duration_ms(time_ms), critical(is_critical) {}
    
    bool all_passed() const { return passed == total; }
    double pass_rate() const { return total > 0 ? (double)passed / total * 100.0 : 100.0; }
};

// Theme summary for grouped results
struct ThemeResult {
    std::string theme_name;
    int total_passed = 0;
    int total_tests = 0;
    int modules_count = 0;
    int critical_failures = 0;
    double total_duration_ms = 0.0;
    std::vector<TestModuleResult> modules;
    
    void add_module(const TestModuleResult& result) {
        modules.push_back(result);
        total_passed += result.passed;
        total_tests += result.total;
        modules_count++;
        total_duration_ms += result.duration_ms;
        if (result.critical && !result.all_passed()) {
            critical_failures++;
        }
    }
    
    bool all_passed() const { return total_passed == total_tests; }
    bool has_critical_failures() const { return critical_failures > 0; }
    double pass_rate() const { return total_tests > 0 ? (double)total_passed / total_tests * 100.0 : 100.0; }
};

// Individual test failure details
struct TestFailure {
    std::string module_name;
    std::string test_name;
    std::string reason;
};

// Test suite coordinator - the supra-formatter
class TestSuiteCoordinator {
private:
    std::vector<TestModuleResult> all_results_;
    std::vector<ThemeResult> theme_results_;
    std::vector<TestFailure> failures_;
    double total_suite_time_ms_ = 0.0;
    bool verbose_output_ = false;
    
    void organize_by_themes();
    void print_theme_summary(const ThemeResult& theme) const;
    void print_overall_summary() const;
    
public:
    TestSuiteCoordinator(bool verbose = false) : verbose_output_(verbose) {}
    
    // Register a test module result
    void add_module_result(const TestModuleResult& result);
    
    // Run a test module and capture its result
    template<typename TestFunc>
    void run_module(const std::string& module_name, const std::string& theme, 
                   TestFunc test_function, bool critical = true);
    
    // Generate the final unified report
    void generate_final_report();
    
    // Track test failures
    void add_failure(const std::string& module, const std::string& test, const std::string& reason = "");
    
    // Get overall suite status
    bool all_tests_passed() const;
    bool has_critical_failures() const;
    
    // Print detailed failure list
    void print_failure_details() const;
    int get_total_passed() const;
    int get_total_tests() const;
    double get_total_duration_ms() const;
};

// Test module function signature
using TestModuleFunction = std::function<TestModuleResult()>;

#endif // TEST_SUITE_COORDINATOR_H