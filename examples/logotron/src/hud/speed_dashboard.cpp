#include "hud/speed_dashboard.h"
#include "logosphere/rendering/i_draw_surface.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace logotron::hud {

namespace {

// Player-bike cyan, scaled to 0..255 — same palette as the bike body
// so the HUD reads as "the player's instrumentation."
constexpr uint8_t kCyanR = 38;
constexpr uint8_t kCyanG = 242;
constexpr uint8_t kCyanB = 255;
// Dim cyan for unlit bar segments / faded labels.
constexpr uint8_t kDimR  = 12;
constexpr uint8_t kDimG  = 60;
constexpr uint8_t kDimB  = 80;
// Panel background — near-black, faintly blue, semi-transparent so
// the arena bleeds through (Tron HUDs are translucent, not opaque).
constexpr uint8_t kBgR   = 6;
constexpr uint8_t kBgG   = 10;
constexpr uint8_t kBgB   = 18;
constexpr uint8_t kBgA   = 200;

// Layered glow border — three concentric outlines, brightest inside.
// Crude but cheap "neon" effect that costs three rectangle outlines
// per frame.
void draw_glow_border(IDrawSurface* r, int x, int y, int w, int h) {
    // Outer ring: dim
    r->draw_rectangle_outline(x - 2, y - 2, w + 4, h + 4, kDimR,  kDimG,  kDimB);
    // Middle ring: medium
    r->draw_rectangle_outline(x - 1, y - 1, w + 2, h + 2, kCyanR/2, kCyanG/2, kCyanB/2);
    // Inner ring: bright cyan — the actual border line
    r->draw_rectangle_outline(x,     y,     w,     h,     kCyanR, kCyanG, kCyanB);
}

// Tron-style corner brackets — short L-shapes one pixel outside the
// panel corners. Visual flourish from sci-fi UI design (think Tron:
// Legacy disc readouts).
void draw_corner_brackets(IDrawSurface* r, int x, int y, int w, int h, int len = 12) {
    auto bracket = [&](int cx, int cy, int dx, int dy) {
        r->draw_line(cx, cy, cx + len * dx, cy,            kCyanR, kCyanG, kCyanB, 255);
        r->draw_line(cx, cy, cx,            cy + len * dy, kCyanR, kCyanG, kCyanB, 255);
    };
    bracket(x - 4,         y - 4,         +1, +1);  // top-left
    bracket(x + w + 3,     y - 4,         -1, +1);  // top-right
    bracket(x - 4,         y + h + 3,     +1, -1);  // bottom-left
    bracket(x + w + 3,     y + h + 3,     -1, -1);  // bottom-right
}

// Right-aligned 3-digit speed display ("042"). The ramp tops at ~8
// m/s today so 3 digits is overkill — but Weirden mutations could
// crank max_speed up later, and the leading-zero look IS the Tron
// readout aesthetic.
std::string format_speed(float speed) {
    int v = static_cast<int>(speed + 0.5f);
    if (v < 0) v = 0;
    if (v > 999) v = 999;
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%03d", v);
    return std::string(buf);
}

} // namespace

SpeedDashboard::SpeedDashboard(const std::string& id) : ui::Panel(id) {
    // Background + border are drawn by our render() override; turn off
    // Panel's defaults so we don't double-draw.
    set_background_color(0, 0, 0, 0);   // transparent base
    set_border(false);
    // No focus, no input — see the on_mouse_* overrides in the header.
    set_can_focus(false);
}

void SpeedDashboard::set_state(float current_speed, float max_speed,
                               float base_speed, float time_since_turn) {
    current_speed_   = current_speed;
    max_speed_       = (max_speed > 0.001f) ? max_speed : 1.0f;
    base_speed_      = base_speed;
    time_since_turn_ = time_since_turn;
}

void SpeedDashboard::render(IDrawSurface* renderer) {
    if (!renderer) return;
    auto b = get_bounds();
    const int x = b.x, y = b.y, w = b.width, h = b.height;

    // 1. Background panel (translucent).
    renderer->draw_rectangle_alpha(x, y, w, h, kBgR, kBgG, kBgB, kBgA);

    // 2. Glow border + corner brackets.
    draw_glow_border(renderer, x, y, w, h);
    draw_corner_brackets(renderer, x, y, w, h);

    // 3. "VELOCITY" label, top-left, small.
    renderer->draw_string_scaled(x + 12, y + 8, "VELOCITY",
                                 kCyanR, kCyanG, kCyanB, 1.0f);

    // 4. Big speed readout (3-digit, scale 4 for chunky Tron look)
    //    + "M/S" suffix in normal scale.
    {
        const float kBigScale = 4.0f;
        const int   big_y = y + 26;
        std::string num = format_speed(current_speed_);
        renderer->draw_string_scaled(x + 18, big_y, num,
                                     kCyanR, kCyanG, kCyanB, kBigScale);
        // "M/S" suffix — placed manually so it sits next to the
        // 3-digit block without measuring text width (the bitmap
        // font is 5 px wide × scale per char, plus 1 px tracking).
        const int num_w = static_cast<int>(num.size() * 6 * kBigScale);
        renderer->draw_string_scaled(x + 24 + num_w, big_y + 12, "M/S",
                                     kCyanR, kCyanG, kCyanB, 1.5f);
    }

    // 5. Segmented bar — 16 cells from base→max. Cells under the
    //    current speed are bright cyan; above are dim. base_speed
    //    floor anchors the empty bar so the player sees "0" at base.
    {
        const int kCells       = 16;
        const int bar_x        = x + 14;
        const int bar_y        = y + h - 26;
        const int bar_w        = w - 28;
        const int cell_w       = (bar_w - (kCells - 1)) / kCells;  // 1 px gap
        const int bar_h        = 10;

        float t = (current_speed_ - base_speed_) / (max_speed_ - base_speed_);
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        int lit = static_cast<int>(t * kCells + 0.5f);
        if (lit > kCells) lit = kCells;

        for (int i = 0; i < kCells; ++i) {
            int cx = bar_x + i * (cell_w + 1);
            if (i < lit) {
                renderer->draw_rectangle(cx, bar_y, cell_w, bar_h,
                                         kCyanR, kCyanG, kCyanB);
            } else {
                renderer->draw_rectangle(cx, bar_y, cell_w, bar_h,
                                         kDimR, kDimG, kDimB);
                // 1 px bright accent on dim cells — keeps the bar
                // from looking dead empty at base speed.
                renderer->draw_line(cx, bar_y + bar_h - 1,
                                    cx + cell_w - 1, bar_y + bar_h - 1,
                                    kCyanR/3, kCyanG/3, kCyanB/3, 255);
            }
        }
    }

    // 6. "TURN +X.Xs" recency indicator, bottom-right.
    {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "TURN +%4.1fs", time_since_turn_);
        // Right-align: use a generous offset; bitmap font is ~6 px
        // per char × scale, so 11 chars × 6 × 1.0 ≈ 66 px.
        renderer->draw_string_scaled(x + w - 80, y + 8, buf,
                                     kCyanR, kCyanG, kCyanB, 1.0f);
    }
}

} // namespace logotron::hud
