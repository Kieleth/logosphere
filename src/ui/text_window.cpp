#include "text_window.h"
#include "logosphere/rendering/i_draw_surface.h"

TextWindow::TextWindow(const std::string& title, const std::string& id)
    : Window(title, id)
{
}

void TextWindow::add_line(const std::string& text, uint8_t r, uint8_t g, uint8_t b) {
    if (newest_at_top_) {
        lines_.push_front({text, r, g, b});
    } else {
        lines_.push_back({text, r, g, b});
    }

    while (lines_.size() > max_lines_) {
        if (newest_at_top_) {
            lines_.pop_back();
        } else {
            lines_.pop_front();
        }
    }
}

void TextWindow::clear() {
    lines_.clear();
}

void TextWindow::render(IDrawSurface* renderer) {
    if (!renderer || !is_visible()) return;

    // Get widget bounds
    ui::Rectangle bounds = get_absolute_bounds();
    int x = bounds.x;
    int y = bounds.y;
    int w = bounds.width;
    int h = bounds.height;

    // Background
    renderer->fill_rect(x, y, w, h, 20, 20, 30, bg_alpha_);

    // Border
    renderer->draw_rect(x, y, w, h, 100, 100, 120, 255);

    // Title bar (if has title)
    const std::string& title = get_title();
    if (!title.empty()) {
        renderer->fill_rect(x, y, w, 16, 40, 40, 60, bg_alpha_);
        renderer->draw_text(x + padding_, y + 2, title.c_str(), 180, 180, 220);
        y += 18;  // Move content area down
        h -= 18;
    }

    // Draw lines
    int text_y = y + padding_;
    int visible_lines = (h - padding_ * 2) / line_height_;

    size_t count = 0;
    for (const auto& line : lines_) {
        if (count >= static_cast<size_t>(visible_lines)) break;

        renderer->draw_text(x + padding_, text_y, line.text.c_str(), line.r, line.g, line.b);
        text_y += line_height_;
        count++;
    }
}
