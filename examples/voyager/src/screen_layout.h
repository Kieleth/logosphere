// Where things sit. One screen, two columns, no frames.
//
// Kept as arithmetic with no UI types in it so the numbers can be read
// and argued with in one place. The window is sized in POINTS to fit a
// laptop display: a window wider than the screen is not a big window,
// it is a window you never see.

#ifndef VOYAGER_SCREEN_LAYOUT_H
#define VOYAGER_SCREEN_LAYOUT_H

namespace voyager {

constexpr int kScreenW = 1280;
constexpr int kScreenH = 800;

constexpr int kPad = 28;
constexpr int kLine = 20;

// The engine's bitmap font is fixed pitch at six pixels a character, so
// a column count is a width in pixels over six.
constexpr int kGlyphW = 6;

// How much room the sheet gets. Wide enough for a long short-name, a
// three-digit score and a signed modifier, and no wider: the sheet is
// the still half of the screen.
constexpr int kSheetW = 260;

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
};

struct ScreenLayout {
    Rect left;          // the story and the doors
    Rect right;         // the sheet
    Rect doors;         // the list, a root widget over `left`

    int  title_y = 0;
    int  prose_top = 0;
    int  prose_lines = 0;
    int  prose_columns = 0;
    int  prompt_y = 0;
    int  sheet_top = 0;
};

inline ScreenLayout compute_layout(int screen_w, int screen_h) {
    ScreenLayout out;
    const int left_w = screen_w - kSheetW - 3 * kPad;

    out.left = {kPad, kPad, left_w, screen_h - 2 * kPad};
    out.right = {2 * kPad + left_w, kPad, kSheetW, screen_h - 2 * kPad};

    int y = out.left.y;
    out.title_y = y;
    y += kLine * 2;
    out.prose_top = y;
    out.prose_columns = left_w / kGlyphW;
    out.prose_lines = 8;
    y += out.prose_lines * kLine + kLine;
    out.prompt_y = y;
    y += kLine + kLine / 2;
    out.doors = {out.left.x, y, left_w, out.left.y + out.left.h - y};

    out.sheet_top = out.right.y + kLine * 2;
    return out;
}

}  // namespace voyager

#endif  // VOYAGER_SCREEN_LAYOUT_H
