// Window points into the UI's own coordinates.
//
// The UI overlay lives on the RENDER grid: widgets are positioned and
// hit-tested in render pixels, drawn into a plane the size of the
// render buffer, and that plane is scaled with the scene to fill the
// window. The mouse arrives in WINDOW points. The two coincide only
// while a game renders at its window's size. A game that renders on a
// smaller grid and scales up (bigger type, cheaper frames) has every
// pointer position land short of what is drawn, by exactly the scale,
// growing with distance from the origin: one centimetre mid-screen,
// two near the bottom.
//
// This is the one place that arithmetic lives, pure and testable.
// The render size it is built from must be the size the UI plane
// actually has; a copy that stopped being updated is the defect this
// header replaced (2026-09-02).

#ifndef LOGOSPHERE_CORE_UI_SPACE_H
#define LOGOSPHERE_CORE_UI_SPACE_H

namespace logosphere {

struct UiSpace {
    int render_w = 0;
    int render_h = 0;
    int window_w = 0;
    int window_h = 0;

    bool valid() const {
        return render_w > 0 && render_h > 0 && window_w > 0 && window_h > 0;
    }

    // Window point -> UI (render-grid) point. Identity when the grids
    // match. Leaves the point untouched when the sizes are unknown,
    // which is the behaviour before a window exists.
    void window_to_ui(double& x, double& y) const {
        if (!valid()) return;
        x = x * render_w / window_w;
        y = y * render_h / window_h;
    }

    // UI point -> window point: where a widget at (x, y) is drawn.
    void ui_to_window(double& x, double& y) const {
        if (!valid()) return;
        x = x * window_w / render_w;
        y = y * window_h / render_h;
    }
};

}  // namespace logosphere

#endif  // LOGOSPHERE_CORE_UI_SPACE_H
