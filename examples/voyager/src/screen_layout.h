// Where things sit. One screen, two columns, no frames.
//
// Kept as arithmetic with no UI types in it so the numbers can be
// read and argued with in one place, and measured in
// test_voyager_layout rather than looked at. The window is sized in
// POINTS to fit a laptop display: a window wider than the screen is
// not a big window, it is a window you never see.
//
// TYPE SIZE. The bitmap font is 5x7 with a pixel of spacing, and the
// draw surface can blit it at an integer scale. This game draws its
// text through its own widgets, on the window's own grid, so the
// pointer and the drawing agree without any mapping. Rendering
// smaller and scaling up was tried for the same effect and put every
// click short by the scale (2026-09-02).
//
// The scale is a runtime choice, not a constant: the player changes
// it with the plus and minus keys and every rectangle below moves.
// Which is why one rule holds the screen together: no other file
// computes a position of its own.
//
// WHAT THE WINDOW HAS TO BE. Both columns want twenty glyphs, and
// three pads separate them, so the screen wants 276 * scale points of
// width: 1104 at the largest type. A narrower window still gets
// rectangles that fit inside it and still do not overlap; the columns
// are simply tighter than comfortable.

#ifndef VOYAGER_SCREEN_LAYOUT_H
#define VOYAGER_SCREEN_LAYOUT_H

#include "logosphere/rendering/font_renderer.h"

#include <algorithm>

namespace voyager {

constexpr int kScreenW = 1280;   // the window, in points
constexpr int kScreenH = 800;
constexpr int kRenderW = kScreenW;   // the grid the game is drawn on
constexpr int kRenderH = kScreenH;

constexpr int kMinTextScale = 1;
constexpr int kMaxTextScale = 4;
constexpr int kTextScale = 2;    // the size the game starts at

// The engine's text field, which is how the player's own words get
// in. It draws its input row at a fixed height whatever this game's
// type size is, so the box it needs is a fixed height too.
constexpr int kFieldH = 34;

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;

    bool empty() const { return w <= 0 || h <= 0; }
    bool overlaps(const Rect& other) const {
        if (empty() || other.empty()) return false;
        return x < other.x + other.w && other.x < x + w &&
               y < other.y + other.h && other.y < y + h;
    }
    // Every pixel this covers is inside. A box with no width or no
    // height covers nothing, so it is inside anything.
    bool within(int width, int height) const {
        if (empty()) return true;
        return x >= 0 && y >= 0 && x + w <= width && y + h <= height;
    }
};

struct ScreenLayout {
    int text_scale = kTextScale;
    int glyph_w = 0;     // one character, spacing included
    int line = 0;        // one row of text, leading included
    int pad = 0;

    Rect left;           // the story, the question, the doors, the field
    Rect right;          // the sheet and its notes

    Rect title;
    Rect prose;
    Rect prompt;
    Rect doors;
    Rect field;          // where the engine's text field is placed
    Rect sheet;
    Rect note;

    int prose_lines = 0;
    int prose_columns = 0;
    int prompt_lines = 0;       // the reserve; a longer question scrolls
    int sheet_rows_shown = 0;   // the rest of the sheet is scrolled to
    int note_lines = 0;
    int note_columns = 0;

    // Where the doors start once the question turns out to be shorter
    // than its reserve. A two-line question must not leave two blank
    // lines between itself and the first door.
    Rect doors_after(int prompt_shown) const {
        const int shown = std::max(0, std::min(prompt_shown, prompt_lines));
        const int floor_y = doors.y + doors.h;
        const int top =
            std::min(prompt.y + shown * line + line / 2, floor_y);
        return {doors.x, top, doors.w, std::max(0, floor_y - top)};
    }

    // The rows of the question the screen actually draws, which is
    // what the doors are placed under.
    Rect prompt_shown_rect(int prompt_shown) const {
        const int shown = std::max(0, std::min(prompt_shown, prompt_lines));
        return {prompt.x, prompt.y, prompt.w, shown * line};
    }
};

inline ScreenLayout compute_layout(int screen_w, int screen_h,
                                   int text_scale, int sheet_rows) {
    ScreenLayout out;
    const int scale =
        std::max(kMinTextScale, std::min(text_scale, kMaxTextScale));
    const int glyph =
        (FontRenderer::CHAR_WIDTH + FontRenderer::CHAR_SPACING) * scale;
    const int line = (FontRenderer::CHAR_HEIGHT + 6) * scale;
    const int pad = 12 * scale;
    out.text_scale = scale;
    out.glyph_w = glyph;
    out.line = line;
    out.pad = pad;

    // ---- the two columns -------------------------------------------
    // The sheet asks for the width of its longest row and gives the
    // rest back to the story. Twenty glyphs is the floor for both; a
    // window too narrow for that splits what there is evenly rather
    // than letting one column swallow the other.
    const int shared = screen_w - 3 * pad;
    int sheet_w = std::min(32 * glyph + pad, shared - 20 * glyph);
    sheet_w = std::max(sheet_w, 20 * glyph + pad);
    if (sheet_w > shared - glyph) sheet_w = std::max(0, shared / 2);
    const int left_w = std::max(0, shared - sheet_w);

    const int column_h = std::max(0, screen_h - 2 * pad);
    out.right = {2 * pad + left_w, pad, sheet_w, column_h};
    out.prose_columns = left_w / glyph;
    out.note_columns = std::max(0, (sheet_w - pad) / glyph);

    const int bottom = screen_h - pad;
    const int body_top = std::min(pad + line + line / 2, std::max(0, bottom));

    // ---- the left column -------------------------------------------
    // The column's own background STOPS above the text field. The
    // engine renders its widgets in the order they were added and
    // hit-tests them in the reverse of it, and the field is the
    // engine's own widget, added before this game's: a panel that
    // covered it would paint over it AND take its clicks, which is a
    // field that is invisible and cannot be typed in.
    const int field_h = std::min(kFieldH, column_h);
    out.field = {pad, std::max(0, bottom - field_h), left_w, field_h};
    const int stop = std::max(body_top, out.field.y - pad / 2);
    // Clamped against the field a second time: `stop` has a floor of
    // its own for windows too short to hold the layout, and in one of
    // those the background would otherwise reach over the field again.
    out.left = {pad, pad, left_w,
                std::max(0, std::min(stop, out.field.y) - pad)};
    out.title = {out.left.x, std::min(pad, std::max(0, bottom)), left_w,
                 std::min(line, column_h)};

    // What is left, in whole rows, once the three gaps are taken out:
    // one line under the prose, half a line under the question, half
    // a line of lead inside the doors list itself.
    const int gaps = line + line / 2 + line / 2;
    const int rows = (stop - body_top - gaps) / line;
    int prose_l = 3, prompt_l = 2, doors_l = 3;
    int surplus = rows - (prose_l + prompt_l + doors_l);
    if (surplus < 0) {
        // A window too short for even the floors. The story gives way
        // first, the question next; the doors are what is being
        // chosen from and give way last.
        while (surplus < 0 && prose_l > 1) { --prose_l; ++surplus; }
        while (surplus < 0 && prompt_l > 1) { --prompt_l; ++surplus; }
        while (surplus < 0 && doors_l > 1) { --doors_l; ++surplus; }
    } else {
        const int to_prompt = std::min(surplus, 2);
        prompt_l += to_prompt;
        surplus -= to_prompt;
        const int to_prose = std::min(surplus, 5);
        prose_l += to_prose;
        surplus -= to_prose;
        doors_l += surplus;
    }

    int y = body_top;
    const auto band = [&](int wanted_h, int gap_after) {
        const int top = std::min(y, stop);
        const int height = std::max(0, std::min(wanted_h, stop - top));
        y = top + height + gap_after;
        return Rect{out.left.x, top, left_w, height};
    };
    out.prose = band(prose_l * line, line);
    out.prompt = band(prompt_l * line, line / 2);
    const int doors_top = std::min(y, stop);
    out.doors = {out.left.x, doors_top, left_w,
                 std::max(0, stop - doors_top)};
    out.prose_lines = line > 0 ? out.prose.h / line : 0;
    out.prompt_lines = line > 0 ? out.prompt.h / line : 0;

    // ---- the right column ------------------------------------------
    // The notes go BELOW the sheet, always, with a blank line between
    // them. The sheet grows with the life, so what will not fit is
    // scrolled to rather than dropped, and the notes never end up
    // sitting on top of a row.
    const int sheet_top = body_top;
    const int note_floor = 3 * line;
    int capacity = (bottom - sheet_top - line - note_floor) / line;
    if (capacity < 1) capacity = 1;
    out.sheet_rows_shown = std::max(0, std::min(sheet_rows, capacity));
    const int sheet_h = std::max(
        0, std::min(out.sheet_rows_shown * line, bottom - sheet_top));
    out.sheet = {out.right.x, sheet_top, sheet_w, sheet_h};
    const int note_top =
        std::min(out.sheet.y + out.sheet.h + line, std::max(0, bottom));
    out.note = {out.right.x, note_top, sheet_w,
                std::max(0, bottom - note_top)};
    out.note_lines = line > 0 ? out.note.h / line : 0;
    return out;
}

}  // namespace voyager

#endif  // VOYAGER_SCREEN_LAYOUT_H
