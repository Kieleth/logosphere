// Debug Overlay Tests with Standardized Output
#include "test_formatter.h" 
#include "logosphere/rendering/font_renderer.h"
#include <string>
#include <vector>

class TestOverlay {
public:
    static bool run_all_tests() {
        TestFormatter::print_header("DEBUG OVERLAY TESTS");
        
        int passed = 0, total = 0;
        
        total++; if (test_character_support()) passed++;
        total++; if (test_overlay_strings()) passed++;
        
        TestFormatter::print_summary(passed, total);
        return passed == total;
    }
    
private:
    static bool test_character_support() {
        auto start = TestFormatter::start_timer();
        bool passed = true;
        
        // Test all characters that should be supported
        const char* supported = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:.%,=()+-[]> ";
        
        FontRenderer font_renderer;
        for (int i = 0; supported[i]; i++) {
            char c = supported[i];
            int index = font_renderer.get_char_index(c);
            
            if (index < 0) {
                passed = false;
            }
        }
        
        double duration = TestFormatter::end_timer(start);
        TestFormatter::print_test_result("character_support", "Font character availability", passed, duration);
        return passed;
    }
    
    static bool test_overlay_strings() {
        auto start = TestFormatter::start_timer();
        bool passed = true;
        
        // Test strings that will appear in the overlay
        std::vector<std::string> overlay_texts = {
            "FPS:", "FRAME:", "MS", "PARTICLES:", "MEMORY:",
            "=== CONTROLS ===", "=== SPAWN ===", "=== DEBUG ===", "=== CHARACTER ===",
            "POS:", "VEL:", "LOOK:", "=== COMPASS ===", "N(+Y)", "S(-Y)", "E(+X)", "W(-X)",
            "[O]", "SCREEN MAPPING:", "PROJECTION:", "MODE:", "1 2 3 4 5 6", "WASD", "P"
        };
        
        FontRenderer font_renderer;
        for (const auto& text : overlay_texts) {
            for (char c : text) {
                int index = font_renderer.get_char_index(c);
                if (index == 0 && c != ' ') {
                    passed = false;
                    break;
                }
            }
        }
        
        double duration = TestFormatter::end_timer(start);
        TestFormatter::print_test_result("overlay_strings", "UI text rendering support", passed, duration);
        return passed;
    }
};

// Test entry point for engine tests
bool test_debug_overlay() {
    return TestOverlay::run_all_tests();
}