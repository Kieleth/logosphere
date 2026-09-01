// Where things sit. One screen, two columns, no frames.
//
// Kept as arithmetic with no UI types in it so the numbers can be read
// and argued with in one place. The window is sized in POINTS to fit a
// laptop display: a window wider than the screen is not a big window,
// it is a window you never see.
//
// TYPE SIZE. The engine's bitmap font has one size, so the type grows
// by rendering the game on a smaller grid than the window and letting
// the engine scale it up: a 1024-wide render in a 1280-wide window is
// glyphs one and a quarter times the size. Every number below is in
// RENDER pixels, which is the grid the widgets live on.

#ifndef VOYAGER_SCREEN_LAYOUT_H
#define VOYAGER_SCREEN_LAYOUT_H

namespace voyager {

constexpr int kScreenW = 1280;   // the window, in points
constexpr int kScreenH = 800;
constexpr int kRenderW = 1024;   // the grid the game is drawn on
constexpr int kRenderH = 640;

constexpr int kPad = 24;
constexpr int kLine = 20;

// The engine's bitmap font is fixed pitch at six pixels a character, so
// a column count is a width in pixels over six.
constexpr int kGlyphW = 6;

// How much room the sheet gets: the lines, then below them the notes
// that explain whatever the pointer rests on, in the book's words.
constexpr int kSheetW = 380;

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
};

struct ScreenLayout {
    Rect left;          // the story, the question, and the doors
    Rect right;         // the sheet and its notes
    Rect doors;         // the list, a root widget over `left`

    int  title_y = 0;
    int  prose_top = 0;
    int  prose_lines = 0;
    int  prose_columns = 0;
    int  prompt_y = 0;
    int  prompt_lines = 0;
    int  sheet_top = 0;
    int  note_top = 0;
    int  note_lines = 0;
    int  note_columns = 0;
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
    out.prose_lines = 9;
    y += out.prose_lines * kLine + kLine;
    out.prompt_y = y;
    out.prompt_lines = 4;
    y += out.prompt_lines * kLine + kLine / 2;
    out.doors = {out.left.x, y, left_w, out.left.y + out.left.h - y};

    out.sheet_top = out.right.y + kLine * 2;
    out.note_top = out.right.y + out.right.h / 2;
    out.note_columns = (kSheetW - kPad) / kGlyphW;
    out.note_lines = (out.right.h / 2) / kLine - 1;
    return out;
}

}  // namespace voyager

#endif  // VOYAGER_SCREEN_LAYOUT_H
