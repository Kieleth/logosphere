// The pointer lands where the widget is drawn.
//
// THE DEFECT THIS LOCKS (2026-09-02). Voyager rendered on a 1024x640
// grid inside a 1280x800 window for bigger type. The UI plane followed
// the render size, so everything was drawn at 1.25x; the mouse kept
// arriving in window points and was hit-tested unmapped, so a widget
// drawn at window y=625 answered to the pointer at y=500. One
// centimetre off mid-screen, two near the bottom, exactly the scale.
// The first fix mapped by a render size the engine kept in a second
// copy that never learned about the change, so it mapped by 1.0 and
// changed nothing. This test pins the arithmetic on the one struct
// the input path now uses; the engine keeps one render size.

#undef NDEBUG

#include "logosphere/core/ui_space.h"

#include <cmath>
#include <iostream>

namespace {

int passed = 0;
int failed = 0;

#define CHECK(condition, message)                                       \
    do {                                                                \
        if (condition) {                                                \
            ++passed;                                                   \
        } else {                                                        \
            ++failed;                                                   \
            std::cout << "FAIL: " << message << '\n';                   \
        }                                                               \
    } while (false)

bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

}  // namespace

int main() {
    // 1. Grids match: the mapping is the identity, which is every
    //    game that renders at its window's size, and every game before
    //    this change.
    {
        logosphere::UiSpace same{1280, 800, 1280, 800};
        double x = 640, y = 400;
        same.window_to_ui(x, y);
        CHECK(near(x, 640) && near(y, 400),
              "equal grids must map to identity, got (" << x << "," << y << ")");
    }

    // 2. The Voyager case: 1024x640 drawn into 1280x800. A widget at
    //    render (400, 500) is drawn at window (500, 625); the pointer
    //    at the drawn position must map back onto the widget.
    {
        logosphere::UiSpace voyager{1024, 640, 1280, 800};
        double wx = 400, wy = 500;
        voyager.ui_to_window(wx, wy);
        CHECK(near(wx, 500) && near(wy, 625),
              "a widget at render (400,500) is drawn at window (500,625), "
              "computed (" << wx << "," << wy << ")");
        voyager.window_to_ui(wx, wy);
        CHECK(near(wx, 400) && near(wy, 500),
              "the pointer at the drawn position must land on the widget, "
              "landed at (" << wx << "," << wy << ")");

        // The corners agree too: the window's far edge is the grid's.
        double cx = 1280, cy = 800;
        voyager.window_to_ui(cx, cy);
        CHECK(near(cx, 1024) && near(cy, 640),
              "the window's far corner must map to the grid's, got ("
                  << cx << "," << cy << ")");
    }

    // 3. THE DEFECT SHAPE: a mapping built from a render size that
    //    was never updated is the identity, and it puts the pointer
    //    short of the drawing by the full scale, growing with distance.
    //    Stated here as the measurable signature, so a regression
    //    reads as "one centimetre mid-screen, two near the bottom".
    {
        logosphere::UiSpace stale{1280, 800, 1280, 800};   // never told
        logosphere::UiSpace truth{1024, 640, 1280, 800};
        double drawn_x = 0, drawn_y = 500;
        truth.ui_to_window(drawn_x, drawn_y);            // 625 in window
        double hit_x = drawn_x, hit_y = drawn_y;
        stale.window_to_ui(hit_x, hit_y);                // 625, unmapped
        CHECK(!near(hit_y, 500),
              "a stale render size must NOT be able to satisfy this test");
        CHECK(near(hit_y - 500, 125),
              "the stale mapping lands " << (hit_y - 500)
                  << " grid pixels below the widget; the defect was 125");
    }

    // 4. Unknown sizes leave the point alone rather than dividing by
    //    zero or guessing.
    {
        logosphere::UiSpace none{};
        double x = 12, y = 34;
        none.window_to_ui(x, y);
        CHECK(near(x, 12) && near(y, 34),
              "an unsized space must leave the point untouched");
    }

    std::cout << (failed == 0 ? "OK " : "FAILED ") << passed << " passed, "
              << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
