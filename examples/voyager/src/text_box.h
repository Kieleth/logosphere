// Rows of text in a box, at this game's own type size.
//
// The engine's label draws the bitmap font at one size and one row.
// This widget draws through the draw surface's scaled text instead,
// so the type can be as large as the player asks for while the grid
// stays the window's own and the pointer needs no mapping at all.
//
// It scrolls. A life outgrows its box: the story the referee writes
// grows every season, the sheet grows a row per kind faced, and a
// note can be a paragraph. What does not fit is scrolled to, never
// cut and never trailed off, because a sentence that stops mid-word
// is a sentence the player has to invent the rest of. A mark at the
// edge says which way there is more.
//
// Each row may carry a note. Resting the pointer on the row hands
// that note back, which is how the sheet explains itself in the
// book's own words.

#ifndef VOYAGER_TEXT_BOX_H
#define VOYAGER_TEXT_BOX_H

#include "ui/ui_system.h"
#include "ui/widget.h"

#include "logosphere/rendering/i_draw_surface.h"

#include "scroll_geometry.h"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace voyager {

class TextBox : public ui::Widget {
public:
    struct Row {
        std::string text;
        std::string note;
    };

    TextBox(const std::string& id, int line_height, int text_scale)
        : ui::Widget(id), line_(line_height), scale_(text_scale) {}

    // Handed the note of whatever row the pointer is resting on.
    std::function<void(const std::string& note)> on_note;

    void set_metrics(int line_height, int text_scale) {
        line_ = line_height;
        scale_ = text_scale;
        invalidate();
    }

    void set_color(uint8_t r, uint8_t g, uint8_t b) {
        r_ = r; g_ = g; b_ = b;
        invalidate();
    }

    // Where a fresh set of rows rests: at the top, or at the end so
    // the newest is the one on screen.
    void rest_at_end(bool yes) { rest_at_end_ = yes; }

    void set_rows(std::vector<Row> rows) {
        rows_ = std::move(rows);
        scroll_ = 0;
        at_end_ = rest_at_end_;
        invalidate();
    }
    void clear() { set_rows({}); }
    size_t row_count() const { return rows_.size(); }

    void render(IDrawSurface* renderer) override {
        if (!renderer || rows_.empty()) return;
        const auto bounds = get_absolute_bounds();
        float ui_scale = 1.0f;
        if (get_ui_system()) ui_scale = get_ui_system()->get_ui_scale_multiplier();
        const auto sx = [&](int v) { return static_cast<int>(v * ui_scale); };
        const auto geometry = shape();
        const int scroll = at_now(geometry);
        const int top = bounds.y;
        const int bottom = bounds.y + bounds.height;
        int y = top - scroll;
        for (const auto& row : rows_) {
            if (y >= top && y + line_ <= bottom && !row.text.empty()) {
                renderer->draw_string_scaled(sx(bounds.x), sx(y), row.text,
                                             r_, g_, b_,
                                             static_cast<float>(scale_));
            }
            y += line_;
        }
        // Where the text continues, a mark at the edge it continues
        // past. The same two marks the doors use, for the same reason.
        const int mark_x = bounds.x + bounds.width - 6 * scale_;
        if (scroll > 0) {
            renderer->draw_string_scaled(sx(mark_x), sx(top), "^", r_, g_,
                                         b_, static_cast<float>(scale_));
        }
        if (scroll < geometry.max_scroll()) {
            renderer->draw_string_scaled(sx(mark_x), sx(bottom - line_),
                                         "v", r_, g_, b_,
                                         static_cast<float>(scale_));
        }
    }

    bool on_mouse_move(ui::MouseEvent& e) override { return tell(e); }
    bool on_mouse_enter(ui::MouseEvent& e) override { return tell(e); }

    bool on_mouse_scroll(int delta) override {
        const auto geometry = shape();
        if (geometry.max_scroll() == 0) return false;
        // A positive delta is the wheel rolling up, which brings back
        // what has already gone past the top.
        const int now = geometry.clamp_scroll(at_now(geometry) - delta * line_);
        scroll_ = now;
        at_end_ = rest_at_end_ && now >= geometry.max_scroll();
        invalidate();
        return true;
    }

private:
    ScrollGeometry shape() const {
        ScrollGeometry geometry;
        geometry.heights.assign(rows_.size(), line_);
        geometry.panel = get_absolute_bounds().height;
        return geometry;
    }
    // A box resting at the end stays there as it grows and as the
    // window changes size; that is what "the newest is on screen"
    // means once the text is still arriving.
    int at_now(const ScrollGeometry& geometry) const {
        return at_end_ ? geometry.max_scroll()
                       : geometry.clamp_scroll(scroll_);
    }
    bool tell(ui::MouseEvent& e) {
        if (!on_note) return false;
        const auto geometry = shape();
        const int at = geometry.block_at(e.local_y, at_now(geometry));
        if (at < 0 || at >= static_cast<int>(rows_.size())) return false;
        if (rows_[static_cast<size_t>(at)].note.empty()) return false;
        on_note(rows_[static_cast<size_t>(at)].note);
        return false;
    }

    std::vector<Row> rows_;
    int line_;
    int scale_;
    int scroll_ = 0;
    bool rest_at_end_ = false;
    bool at_end_ = false;
    uint8_t r_ = 232, g_ = 232, b_ = 226;
};

}  // namespace voyager

#endif  // VOYAGER_TEXT_BOX_H
