#include "career_list.h"

#include "ui/ui_system.h"
#include "logosphere/rendering/i_draw_surface.h"

#include <algorithm>
#include <string>

namespace logovger {
namespace {

// Cut a string to the pixels it is allowed, marking the cut so a
// truncated value never reads as a complete one.
std::string clip(const std::string& s, int pixels) {
    const size_t cols = pixels > 0 ? static_cast<size_t>(pixels / 6) : 0;
    if (cols == 0) return "";
    if (s.size() <= cols) return s;
    if (cols <= 3) return s.substr(0, cols);
    return s.substr(0, cols - 3) + "...";
}

}  // namespace

void CareerList::scroll_to_end() {
    scroll_ = std::max(0, static_cast<int>(rows_.size()) - visible_rows());
    invalidate();
}

void CareerList::set_rows(std::vector<Row> rows) {
    rows_ = std::move(rows);
    scroll_ = 0;
    selected_ = -1;
    hovered_ = -1;
    invalidate();
}

int CareerList::visible_rows() const {
    const auto b = get_absolute_bounds();
    return std::max(1, (b.height - 2 * kPad) / kRowH);
}

void CareerList::clamp_scroll() {
    const int max_scroll =
        std::max(0, static_cast<int>(rows_.size()) - visible_rows());
    scroll_ = std::max(0, std::min(scroll_, max_scroll));
}

int CareerList::row_at(int local_y) const {
    const int i = (local_y - kPad) / kRowH;
    if (i < 0) return -1;
    const int abs = scroll_ + i;
    if (abs >= static_cast<int>(rows_.size())) return -1;
    if (i >= visible_rows()) return -1;
    return abs;
}

bool CareerList::on_mouse_scroll(int delta) {
    if (rows_.empty()) return false;
    scroll_ -= delta;          // wheel up shows earlier rows
    clamp_scroll();
    invalidate();
    return true;
}

bool CareerList::on_mouse_move(ui::MouseEvent& e) {
    const int was = hovered_;
    hovered_ = row_at(e.local_y);
    if (hovered_ != was) invalidate();
    return true;
}

bool CareerList::on_mouse_leave(ui::MouseEvent& e) {
    (void)e;
    if (hovered_ != -1) { hovered_ = -1; invalidate(); }
    return true;
}

bool CareerList::on_mouse_down(ui::MouseEvent& e) {
    const int row = row_at(e.local_y);
    if (row < 0) return true;

    // First click looks, second click on the SAME row commits. No
    // timing games: a career you clicked once is a career you are
    // reading about, and clicking it again means you meant it.
    if (row == selected_) {
        if (on_choose) on_choose(rows_[static_cast<size_t>(row)].key);
        return true;
    }
    selected_ = row;
    invalidate();
    if (on_inspect) on_inspect(rows_[static_cast<size_t>(row)].key);
    return true;
}

void CareerList::render(IDrawSurface* renderer) {
    if (!is_visible() || !renderer) return;
    const auto b = get_absolute_bounds();

    renderer->fill_rect(b.x, b.y, b.width, b.height, 18, 18, 26, 235);
    renderer->draw_rect(b.x, b.y, b.width, b.height, 70, 80, 110, 255);

    const int rows_shown = visible_rows();
    const int total = static_cast<int>(rows_.size());
    for (int i = 0; i < rows_shown; ++i) {
        const int idx = scroll_ + i;
        if (idx >= total) break;
        const auto& r = rows_[static_cast<size_t>(idx)];
        const int y = b.y + kPad + i * kRowH;

        if (idx == selected_)
            renderer->fill_rect(b.x + 2, y - 2, b.width - 4, kRowH,
                                46, 62, 90, 255);
        else if (idx == hovered_)
            renderer->fill_rect(b.x + 2, y - 2, b.width - 4, kRowH,
                                32, 38, 52, 255);

        // The font is fixed-pitch at 6 px, so a column is a character
        // budget. Clip to it here rather than trusting every caller to
        // pre-trim: two columns that overlap are unreadable, and the
        // widget is the only place that knows how wide they are.
        const bool lit = (idx == selected_ || idx == hovered_);
        const int label_px = detail_x_ - kPad;
        const int detail_px = b.width - 2 * kPad - detail_x_ -
                              (total > rows_shown ? 10 : 0);
        renderer->draw_string(b.x + kPad, y, clip(r.label, label_px),
                              lit ? 255 : 210, lit ? 235 : 210,
                              lit ? 200 : 220);
        // The terms of the deal, dimmer, to the right of the name.
        renderer->draw_string(b.x + kPad + detail_x_, y,
                              clip(r.detail, detail_px),
                              lit ? 190 : 130, lit ? 200 : 140,
                              lit ? 215 : 155);
        if (idx == selected_ && !commit_hint_.empty())
            renderer->draw_string(b.x + b.width - 130, y,
                                  commit_hint_, 255, 210, 140);
    }

    // A scrollbar, so it is obvious there is more than fits.
    if (total > rows_shown) {
        const int track_x = b.x + b.width - 6;
        renderer->fill_rect(track_x, b.y + kPad, 3,
                            b.height - 2 * kPad, 40, 44, 56, 255);
        const int thumb_h = std::max(
            16, (b.height - 2 * kPad) * rows_shown / total);
        const int span = (b.height - 2 * kPad) - thumb_h;
        const int max_scroll = total - rows_shown;
        const int thumb_y = b.y + kPad +
            (max_scroll > 0 ? span * scroll_ / max_scroll : 0);
        renderer->fill_rect(track_x, thumb_y, 3, thumb_h, 120, 140, 180, 255);
        renderer->draw_string(b.x + b.width - 118, b.y + b.height - 14,
                              "scroll for more", 110, 120, 140);
    }
}

}  // namespace logovger
