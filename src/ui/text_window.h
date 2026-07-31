// TextWindow - Generic scrollable text display widget
//
// Extends ui::Window to provide a simple text log/output display.
// Use for: NPC logs, debug output, console output, etc.

#ifndef TEXT_WINDOW_H
#define TEXT_WINDOW_H

#include "widgets.h"
#include <string>
#include <deque>
#include <cstdint>

class TextWindow : public ui::Window {
public:
    TextWindow(const std::string& title = "Log", const std::string& id = "text_window");
    ~TextWindow() override = default;

    // Widget lifecycle
    void render(IDrawSurface* renderer) override;

    // Text entry with optional color
    struct Line {
        std::string text;
        uint8_t r = 200, g = 200, b = 200;
    };

    // Add text (newest appears at top by default)
    void add_line(const std::string& text, uint8_t r = 200, uint8_t g = 200, uint8_t b = 200);
    void clear();
    size_t line_count() const { return lines_.size(); }

    // Configuration
    void set_max_lines(size_t max) { max_lines_ = max; }
    size_t get_max_lines() const { return max_lines_; }

    void set_newest_at_top(bool top) { newest_at_top_ = top; }
    bool is_newest_at_top() const { return newest_at_top_; }

    void set_background_alpha(uint8_t alpha) { bg_alpha_ = alpha; }
    uint8_t get_background_alpha() const { return bg_alpha_; }

private:
    std::deque<Line> lines_;
    size_t max_lines_ = 20;
    bool newest_at_top_ = true;

    // Visual
    int padding_ = 4;
    int line_height_ = 12;
    uint8_t bg_alpha_ = 200;
};

#endif // TEXT_WINDOW_H
