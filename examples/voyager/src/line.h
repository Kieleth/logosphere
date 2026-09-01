// One line of text, at this game's own type size.
//
// The engine's label draws the bitmap font at one size. This widget
// draws through the draw surface's scaled text instead, so the type
// can be as large as the game wants while the grid stays the window's
// own and the pointer needs no mapping at all. It also reports the
// pointer arriving, so a line of the sheet can explain itself.

#ifndef VOYAGER_LINE_H
#define VOYAGER_LINE_H

#include "ui/ui_system.h"
#include "ui/widget.h"

#include "logosphere/rendering/i_draw_surface.h"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

namespace voyager {

class Line : public ui::Widget {
public:
    Line(const std::string& id, int text_scale)
        : ui::Widget(id), scale_(text_scale) {}

    std::function<void()> on_hover;

    void set_text(std::string text) {
        if (text == text_) return;
        text_ = std::move(text);
        invalidate();
    }
    const std::string& text() const { return text_; }
    void set_color(uint8_t r, uint8_t g, uint8_t b) {
        r_ = r; g_ = g; b_ = b;
        invalidate();
    }

    void render(IDrawSurface* renderer) override {
        if (!renderer || text_.empty()) return;
        const auto bounds = get_absolute_bounds();
        float ui_scale = 1.0f;
        if (get_ui_system()) ui_scale = get_ui_system()->get_ui_scale_multiplier();
        renderer->draw_string_scaled(static_cast<int>(bounds.x * ui_scale),
                                     static_cast<int>(bounds.y * ui_scale),
                                     text_, r_, g_, b_,
                                     static_cast<float>(scale_));
    }
    bool on_mouse_enter(ui::MouseEvent&) override {
        if (on_hover) on_hover();
        return false;
    }
    bool on_mouse_move(ui::MouseEvent&) override {
        if (on_hover) on_hover();
        return false;
    }

private:
    std::string text_;
    int scale_;
    uint8_t r_ = 232, g_ = 232, b_ = 226;
};

}  // namespace voyager

#endif  // VOYAGER_LINE_H
