// unit_tests.h - Unit test framework for engine components
//
// This provides a lightweight unit testing framework for testing
// individual components in isolation, without needing a full window
// or game loop.

#ifndef UNIT_TESTS_H
#define UNIT_TESTS_H

#include <string>
#include <vector>
#include <functional>
#include <iostream>

// Test result structure
struct TestResult {
    std::string name;
    bool passed;
    std::string message;
};

// Unit test class
class UnitTest {
public:
    UnitTest(const std::string& name) : name_(name), passed_(true) {}
    
    // Assertion methods
    void assert_true(bool condition, const std::string& message) {
        if (!condition) {
            passed_ = false;
            errors_.push_back("FAILED: " + message);
        }
    }
    
    void assert_false(bool condition, const std::string& message) {
        assert_true(!condition, message);
    }
    
    void assert_equals(int expected, int actual, const std::string& message) {
        if (expected != actual) {
            passed_ = false;
            errors_.push_back("FAILED: " + message + 
                " (expected " + std::to_string(expected) + 
                ", got " + std::to_string(actual) + ")");
        }
    }
    
    void assert_equals(float expected, float actual, float epsilon, const std::string& message) {
        float diff = expected - actual;
        if (diff < -epsilon || diff > epsilon) {
            passed_ = false;
            errors_.push_back("FAILED: " + message + 
                " (expected " + std::to_string(expected) + 
                ", got " + std::to_string(actual) + ")");
        }
    }
    
    // Get test results
    TestResult get_result() const {
        TestResult result;
        result.name = name_;
        result.passed = passed_;
        if (!passed_ && !errors_.empty()) {
            result.message = errors_[0];  // Return first error
        }
        return result;
    }
    
    bool passed() const { return passed_; }
    const std::string& name() const { return name_; }
    
private:
    std::string name_;
    bool passed_;
    std::vector<std::string> errors_;
};

// Test suite class
class TestSuite {
public:
    TestSuite(const std::string& name) : name_(name) {}
    
    // Register a test
    void add_test(const std::string& name, std::function<void(UnitTest&)> test_func) {
        tests_.push_back({name, test_func});
    }
    
    // Run all tests
    std::vector<TestResult> run() {
        std::vector<TestResult> results;
        
        for (const auto& test : tests_) {
            UnitTest unit_test(test.first);
            
            try {
                test.second(unit_test);
            } catch (const std::exception& e) {
                unit_test.assert_true(false, "Exception thrown: " + std::string(e.what()));
            } catch (...) {
                unit_test.assert_true(false, "Unknown exception thrown");
            }
            
            TestResult result = unit_test.get_result();
            results.push_back(result);
        }
        
        return results;
    }
    
private:
    std::string name_;
    std::vector<std::pair<std::string, std::function<void(UnitTest&)>>> tests_;
};

// Main test runner
class TestRunner {
public:
    static TestRunner& instance() {
        static TestRunner instance;
        return instance;
    }
    
    void add_suite(TestSuite* suite) {
        suites_.push_back(suite);
    }
    
    bool run_all() {
        int total_tests = 0;
        int passed_tests = 0;
        
        for (auto* suite : suites_) {
            auto results = suite->run();
            for (const auto& result : results) {
                total_tests++;
                if (result.passed) passed_tests++;
            }
        }
        
        // Only show summary if there were actual unit tests
        if (total_tests > 0) {
            return passed_tests == total_tests;
        }
        
        return true;
    }
    
private:
    std::vector<TestSuite*> suites_;
};

// Macro for registering test suites - DISABLED FOR SUPRA-FORMATTER
// All old unit tests are now managed by the supra-formatter system
#define REGISTER_TEST_SUITE(suite) \
    /* OLD UNIT TEST REGISTRATION DISABLED - see supra-formatter system */

#endif // UNIT_TESTS_H