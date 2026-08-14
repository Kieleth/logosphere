// Where everything sits on the screen, as arithmetic you can test.
//
// This exists because a layout bug is invisible in code review and
// expensive in play: a window wider than the display never appears at
// all, and a scrolling list that is a root widget (it must be, to get
// its own scroll events) is positioned in ABSOLUTE coordinates while
// the panel behind it lays its labels out in LOCAL ones. Mixing the
// two puts the list a pad's width off its panel, or outside it.
//
// So the geometry is a pure function of the screen size, with no UI
// types in it, and a headless test asserts the things that actually
// break: every rect on screen, every rect inside the panel it belongs
// to, no rect with a negative size, and the whole window inside a
// laptop display measured in POINTS.

#ifndef LOGOVGER_SCREEN_LAYOUT_H
#define LOGOVGER_SCREEN_LAYOUT_H

namespace logovger {

// A dark table with a lamp on it.
constexpr int kPad = 12;
constexpr int kLine = 18;
constexpr int kStatRows = 14;         // six characteristics, career, rank,
                                      // money, age, terms, the throws
constexpr int kNarrationLines = 14;
constexpr int kProvLines = 6;
constexpr int kDossierLines = 7;      // where they are from, how they look
constexpr int kFileRows = 7;          // the biopic, one clause per beat
constexpr int kAssessLines = 6;       // what the service made of them

// One place decides the window size; main.cpp and the layout agree.
// Sized to fit a laptop display in POINTS, not pixels: a window wider
// than the screen is not a big window, it is a window you never see.
constexpr int kScreenW = 1600;
constexpr int kScreenH = 1000;

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;

    bool contains(const Rect& r) const {
        return r.x >= x && r.y >= y &&
               r.x + r.w <= x + w && r.y + r.h <= y + h;
    }
    bool overlaps(const Rect& r) const {
        return x < r.x + r.w && r.x < x + w &&
               y < r.y + r.h && r.y < y + h;
    }
    bool positive() const { return w > 0 && h > 0; }
};

// Every rectangle is ABSOLUTE, in screen coordinates. Widgets that are
// children of a panel subtract their parent's origin; root widgets use
// these as they are. One convention, stated once.
struct ScreenLayout {
    Rect left;        // what is happening
    Rect dossier;     // the file
    Rect right;       // the sheet
    Rect bottom;      // why

    Rect choices;     // career list, root widget over `left`
    Rect skills;      // skill list, root widget over `right`
    Rect biopic;      // the file, written a clause at a time
    Rect record;      // service record, root widget over `dossier`

    int prompt_y = 0;         // absolute
    int narration_top = 0;    // absolute, first narration line
    int again_y = 0;          // absolute, the "another life" button
    int dossier_name_y = 0;   // absolute
    int dossier_body_y = 0;   // absolute, first wrapped line
    int file_title_y = 0;     // absolute
    int record_title_y = 0;   // absolute
    int assessment_y = 0;     // absolute, first assessment line
    int narration_columns = 0;
    int dossier_columns = 0;
};

inline ScreenLayout compute_layout(int screen_w, int screen_h) {
    ScreenLayout L;

    const int right_w = 310;
    const int dossier_w = 400;
    const int bottom_h = kProvLines * kLine + 3 * kPad;
    const int left_w = screen_w - right_w - dossier_w - 4 * kPad;
    const int top_h = screen_h - bottom_h - 3 * kPad;

    L.left    = {kPad, kPad, left_w, top_h};
    L.dossier = {2 * kPad + left_w, kPad, dossier_w, top_h};
    L.right   = {3 * kPad + left_w + dossier_w, kPad, right_w, top_h};
    L.bottom  = {kPad, 2 * kPad + top_h, screen_w - 2 * kPad, bottom_h};

    // The engine font is fixed-pitch at 6 px per character, so a
    // panel's width in characters is its width in pixels over six.
    L.narration_columns = (left_w - 3 * kPad) / 6;
    L.dossier_columns   = (dossier_w - 3 * kPad) / 6;

    // LEFT: title, the narration, the question, then the answers.
    int y = L.left.y + kPad + kLine + kPad / 2;
    L.narration_top = y;
    y += kNarrationLines * kLine + kPad / 2;
    L.prompt_y = y;
    y += kLine + 4 + kPad;
    L.choices = {L.left.x + 2 * kPad, y,
                 left_w - 4 * kPad,
                 L.left.y + top_h - y - 2 * kPad};

    // RIGHT: the sheet, then skills, then the way out.
    const int again_h = kLine + 6;
    int sy = L.right.y + kPad + kLine + kPad / 2;   // past the title
    sy += kStatRows * (kLine + 4);
    sy += kPad + kLine + 2;                         // past "SKILLS"
    L.again_y = L.right.y + top_h - again_h - kPad;
    L.skills = {L.right.x + kPad, sy, right_w - 2 * kPad,
                L.again_y - sy - kPad};

    // MIDDLE: the file. Head, record, assessment, top to bottom.
    int dy = L.dossier.y + kPad + kLine + kPad / 2;  // past "PERSONNEL FILE"
    L.dossier_name_y = dy;
    dy += kLine + 2;
    L.dossier_body_y = dy;
    dy += kDossierLines * kLine + kPad / 2;
    L.file_title_y = dy;
    dy += kLine + 2;
    // The biopic grows a clause per interaction, so it scrolls; the
    // rest of the file is sized around it.
    const int file_h = kFileRows * 22 + 2 * kPad;   // CareerList::kRowH
    L.biopic = {L.dossier.x + kPad, dy, dossier_w - 2 * kPad, file_h};
    dy += file_h + kPad / 2;
    L.record_title_y = dy;
    dy += kLine + 2;
    const int assess_h = (kAssessLines + 1) * kLine + kPad;
    L.assessment_y = L.dossier.y + top_h - assess_h + kLine;  // past title
    L.record = {L.dossier.x + kPad, dy, dossier_w - 2 * kPad,
                (L.assessment_y - kLine) - dy - kPad};

    return L;
}

}  // namespace logovger

#endif  // LOGOVGER_SCREEN_LAYOUT_H
