#pragma once
#include <string>
#include <chrono>
#include <iostream>
#include <iomanip>

class TestFormatter {
public:
    static void print_header(const std::string& suite_name) {
        std::cout << "\n╭─────────────────────────────────────────────────────────────╮" << std::endl;
        std::cout << "│ " << std::left << std::setw(59) << suite_name << " │" << std::endl;
        std::cout << "╰─────────────────────────────────────────────────────────────╯" << std::endl;
    }
    
    static void print_test_result(const std::string& test_name, 
                                 const std::string& description, 
                                 bool passed, 
                                 double duration_ms = 0.0) {
        std::string status = passed ? "PASS" : "FAIL";
        std::string status_color = passed ? "\033[32m" : "\033[31m"; // Green/Red
        std::string reset_color = "\033[0m";
        
        // Format: [PASS/FAIL] test_name :: description (X.Xms)
        std::cout << "[" << status_color << std::setw(4) << status << reset_color << "] "
                  << std::left << std::setw(25) << test_name << " :: " 
                  << std::left << std::setw(30) << description;
        
        if (duration_ms > 0.0) {
            std::cout << " (" << std::fixed << std::setprecision(2) << duration_ms << "ms)";
        }
        
        std::cout << std::endl;
    }
    
    static void print_summary(int passed, int total) {
        std::cout << "\n╭─────────────────────────────────────────────────────────────╮" << std::endl;
        
        if (passed == total) {
            std::cout << "│ \033[32m✓ ALL TESTS PASSED\033[0m" 
                      << std::string(42, ' ') << " │" << std::endl;
        } else {
            std::cout << "│ \033[31m✗ SOME TESTS FAILED\033[0m" 
                      << std::string(41, ' ') << " │" << std::endl;
        }
        
        std::cout << "│ Passed: " << std::setw(3) << passed << "/" << total 
                  << std::string(47, ' ') << " │" << std::endl;
        std::cout << "╰─────────────────────────────────────────────────────────────╯" << std::endl;
    }
    
    static auto start_timer() {
        return std::chrono::high_resolution_clock::now();
    }
    
    static double end_timer(const std::chrono::high_resolution_clock::time_point& start) {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        return duration.count() / 1000.0; // Convert to milliseconds
    }
};