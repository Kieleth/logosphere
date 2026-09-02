// The doors, as a widget of this game.
//
// Every door is wrapped over as many lines as it needs and never cut:
// a door the player cannot read whole is not a door, it is a guess.
// Hover and selection light the whole door, not one of its lines. One
// click or the arrow keys select; Enter or a second click on the
// selected door takes it. When the doors outgrow the panel the list
// scrolls: the wheel moves it, and selecting a door keeps it in view.
// Selection reports its index so the screen can show the door's note.
//
// A widget of this game rather than the engine's list because the
// engine's list is one line per item and owns its rows privately;
// what this needs is a block per item, and that is a different shape.
//
// The scrolling arithmetic is not here. It is in scroll_geometry.h,
// shared with the boxes of text that scroll for the same reason and
// measured in test_voyager_doors, because a click landing on the
// door above the one under the pointer is not a thing to find out
// about by clicking.

#ifndef VOYAGER_DOOR_LIST_H
#define VOYAGER_DOOR_LIST_H

#include "ui/ui_system.h"
#include "ui/widget.h"

#include "logosphere/rendering/i_draw_surface.h"

#include "scroll_geometry.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace voyager {

class DoorList : public ui::Widget {
public:
    struct Door {
        std::string key;
        std::vector<std::string> lines;   // already wrapped to fit
    };

    DoorList(const std::string& id, int line_height, int pad, int text_scale)
        : ui::Widget(id), line_(line_height), pad_(pad), scale_(text_scale) {}

    std::function<void(int index)> on_selected;
    std::function<void(const std::string& key)> on_taken;

    // The type size changed under it: same doors, new metrics.
    void set_metrics(int line_height, int pad, int text_scale) {
        line_ = line_height;
        pad_ = pad;
        scale_ = text_scale;
        scroll_ = shape().clamp_scroll(scroll_);
        invalidate();
    }

    void set_doors(std::vector<Door> doors) {
        doors_ = std::move(doors);
        hovered_ = -1;
        scroll_ = 0;
        selected_ = doors_.empty() ? -1 : 0;
        invalidate();
        if (selected_ >= 0 && on_selected) on_selected(selected_);
    }
    void clear() { set_doors({}); }
    int selected() const { return selected_; }

    void set_colors(uint8_t text_r, uint8_t text_g, uint8_t text_b,
                    uint8_t lit_r, uint8_t lit_g, uint8_t lit_b) {
        text_r_ = text_r; text_g_ = text_g; text_b_ = text_b;
        lit_r_ = lit_r; lit_g_ = lit_g; lit_b_ = lit_b;
        invalidate();
    }

    void render(IDrawSurface* renderer) override {
        if (!renderer) return;
        const auto bounds = get_absolute_bounds();
        float scale = 1.0f;
        if (get_ui_system()) scale = get_ui_system()->get_ui_scale_multiplier();
        const auto sx = [&](int v) { return static_cast<int>(v * scale); };
        const int top = bounds.y;
        const int bottom = bounds.y + bounds.height;
        int y = bounds.y + pad_ - scroll_;
        for (size_t i = 0; i < doors_.size(); ++i) {
            const int height = block_height(doors_[i]);
            const bool shown = y + height > top && y < bottom;
            if (shown) {
                const bool lit = static_cast<int>(i) == selected_ ||
                                 static_cast<int>(i) == hovered_;
                if (lit) {
                    const bool chosen = static_cast<int>(i) == selected_;
                    const int from = std::max(y, top);
                    const int to = std::min(y + height, bottom);
                    renderer->fill_rect(sx(bounds.x), sx(from), sx(bounds.width),
                                        sx(to - from), lit_r_, lit_g_, lit_b_,
                                        chosen ? 255 : 160);
                }
                int ly = y + pad_ / 2;
                for (const auto& text : doors_[i].lines) {
                    if (ly >= top && ly + line_ <= bottom) {
                        renderer->draw_string_scaled(
                            sx(bounds.x + pad_), sx(ly), text, text_r_,
                            text_g_, text_b_, static_cast<float>(scale_));
                    }
                    ly += line_;
                }
            }
            y += height;
        }
        // Where the list continues, a quiet mark at the edge it
        // continues past.
        const int mark_x = bounds.x + bounds.width - pad_ - 6 * scale_;
        if (scroll_ > 0) {
            renderer->draw_string_scaled(sx(mark_x), sx(top), "^",
                                         lit_r_ + 90, lit_g_ + 90,
                                         lit_b_ + 90,
                                         static_cast<float>(scale_));
        }
        if (scroll_ < max_scroll()) {
            renderer->draw_string_scaled(sx(mark_x), sx(bottom - line_), "v",
                                         lit_r_ + 90, lit_g_ + 90,
                                         lit_b_ + 90,
                                         static_cast<float>(scale_));
        }
    }

    bool on_mouse_move(ui::MouseEvent& e) override {
        const int at = door_at(e.local_y);
        if (at != hovered_) {
            hovered_ = at;
            invalidate();
        }
        return at >= 0;
    }
    bool on_mouse_leave(ui::MouseEvent&) override {
        hovered_ = -1;
        invalidate();
        return false;
    }
    bool on_mouse_scroll(int delta) override {
        const auto geometry = shape();
        if (geometry.max_scroll() == 0) return false;
        // Positive delta is the wheel rolling up, which brings the top
        // back into view.
        scroll_ = geometry.clamp_scroll(scroll_ - delta * line_);
        invalidate();
        return true;
    }
    bool on_mouse_down(ui::MouseEvent& e) override {
        if (e.button != 0) return false;
        const int at = door_at(e.local_y);
        if (std::getenv("LOGOSPHERE_INPUT_DEBUG")) {
            const auto b = get_absolute_bounds();
            std::cout << "[DOORS] click ui=(" << e.x << "," << e.y
                      << ") local=(" << e.local_x << "," << e.local_y
                      << ") bounds=(" << b.x << "," << b.y << " " << b.width
                      << "x" << b.height << ") scroll=" << scroll_
                      << " door=" << at << std::endl;
        }
        if (at < 0) return false;
        const auto now = std::chrono::steady_clock::now();
        const bool again =
            at == selected_ &&
            now - last_click_ < std::chrono::milliseconds(450);
        last_click_ = now;
        if (again) {
            take();
        } else {
            select(at);
        }
        return true;
    }
    bool on_key_down(ui::KeyEvent& e) override {
        switch (e.key) {
            case 265:  // up
                if (selected_ > 0) select(selected_ - 1);
                return true;
            case 264:  // down
                if (selected_ + 1 < static_cast<int>(doors_.size())) {
                    select(selected_ + 1);
                }
                return true;
            case 257:  // enter
                take();
                return true;
        }
        return false;
    }

private:
    int block_height(const Door& door) const {
        return static_cast<int>(door.lines.size()) * line_ + pad_;
    }
    ScrollGeometry shape() const {
        ScrollGeometry geometry;
        geometry.heights.reserve(doors_.size());
        for (const auto& door : doors_) {
            geometry.heights.push_back(block_height(door));
        }
        geometry.panel = get_absolute_bounds().height;
        geometry.lead = pad_;
        return geometry;
    }
    int max_scroll() const { return shape().max_scroll(); }
    int door_at(int local_y) const {
        return shape().block_at(local_y, scroll_);
    }
    // Scroll just enough that the selected door is whole in the panel.
    void keep_in_view(int index) {
        scroll_ = shape().keep_in_view(static_cast<size_t>(index), scroll_);
    }
    void select(int index) {
        selected_ = index;
        keep_in_view(index);
        invalidate();
        if (on_selected) on_selected(index);
    }
    void take() {
        if (selected_ < 0 || selected_ >= static_cast<int>(doors_.size())) return;
        if (on_taken) on_taken(doors_[static_cast<size_t>(selected_)].key);
    }

    std::vector<Door> doors_;
    int line_;
    int pad_;
    int scale_;
    int selected_ = -1;
    int hovered_ = -1;
    int scroll_ = 0;
    std::chrono::steady_clock::time_point last_click_{};
    uint8_t text_r_ = 232, text_g_ = 232, text_b_ = 226;
    uint8_t lit_r_ = 40, lit_g_ = 40, lit_b_ = 52;
};

}  // namespace voyager

#endif  // VOYAGER_DOOR_LIST_H
