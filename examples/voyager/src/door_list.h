// The doors, as a widget of this game.
//
// Every door is wrapped over as many lines as it needs and never cut:
// a door the player cannot read whole is not a door, it is a guess.
// Hover and selection light the whole door, not one of its lines. One
// click or the arrow keys select; Enter or a second click on the
// selected door takes it. Selection reports its index so the screen
// can show the door's note.
//
// A widget of this game rather than the engine's list because the
// engine's list is one line per item and owns its rows privately;
// what this needs is a block per item, and that is a different shape.

#ifndef VOYAGER_DOOR_LIST_H
#define VOYAGER_DOOR_LIST_H

#include "ui/ui_system.h"
#include "ui/widget.h"

#include "logosphere/rendering/i_draw_surface.h"

#include <chrono>
#include <functional>
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

    DoorList(const std::string& id, int line_height, int pad)
        : ui::Widget(id), line_(line_height), pad_(pad) {}

    std::function<void(int index)> on_selected;
    std::function<void(const std::string& key)> on_taken;

    void set_doors(std::vector<Door> doors) {
        doors_ = std::move(doors);
        hovered_ = -1;
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
        int y = bounds.y + pad_;
        for (size_t i = 0; i < doors_.size(); ++i) {
            const int height = block_height(doors_[i]);
            const bool lit = static_cast<int>(i) == selected_ ||
                             static_cast<int>(i) == hovered_;
            if (lit) {
                const bool chosen = static_cast<int>(i) == selected_;
                renderer->fill_rect(sx(bounds.x), sx(y), sx(bounds.width),
                                    sx(height), lit_r_, lit_g_, lit_b_,
                                    chosen ? 255 : 160);
            }
            int ly = y + pad_ / 2;
            for (const auto& text : doors_[i].lines) {
                renderer->draw_text(sx(bounds.x + pad_), sx(ly), text.c_str(),
                                    text_r_, text_g_, text_b_);
                ly += line_;
            }
            y += height;
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
    bool on_mouse_down(ui::MouseEvent& e) override {
        if (e.button != 0) return false;
        const int at = door_at(e.local_y);
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
    int door_at(int local_y) const {
        int y = pad_;
        for (size_t i = 0; i < doors_.size(); ++i) {
            const int height = block_height(doors_[i]);
            if (local_y >= y && local_y < y + height) return static_cast<int>(i);
            y += height;
        }
        return -1;
    }
    void select(int index) {
        selected_ = index;
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
    int selected_ = -1;
    int hovered_ = -1;
    std::chrono::steady_clock::time_point last_click_{};
    uint8_t text_r_ = 232, text_g_ = 232, text_b_ = 226;
    uint8_t lit_r_ = 40, lit_g_ = 40, lit_b_ = 52;
};

}  // namespace voyager

#endif  // VOYAGER_DOOR_LIST_H
