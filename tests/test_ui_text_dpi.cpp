// =============================================================================
// UI TEXT DPI — HUD text scales with render resolution
// =============================================================================
// 2026-07: user-reported. The UI draws 5x7 bitmap glyphs into the RENDER
// buffer at fixed pixel size. When the render resolution exceeds the logical
// window size (retina-native rendering), on-screen text shrinks by the same
// ratio — the FPS/debug HUD becomes unreadable at 3200x2102.
//
// Mechanism under test: UISystem draw_text applies a content scale
// (render width / logical window width) to positions and glyphs, fed by
// Engine::set_render_resolution. At scale 1 output is byte-identical to
// the pre-fix path.
//
// Run: ./build/logosphere-tests --test test_ui_text_dpi --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "../src/ui/ui_system.h"
#include "../src/object_id.h"
#include "logosphere/rendering/pixel_buffer.h"
#include <cstdio>

namespace {

struct Bbox { int min_x, min_y, max_x, max_y; bool any; };

// Bounding box of pure-red pixels in the render buffer.
Bbox red_bbox(Engine& engine) {
    Bbox b{1 << 30, 1 << 30, -1, -1, false};
    PixelBuffer& buf = engine.get_render_buffer();
    for (int y = 0; y < buf.height(); ++y) {
        for (int x = 0; x < buf.width(); ++x) {
            auto px = buf.get_pixel(x, y);
            if (px.r == 255 && px.g == 0 && px.b == 0) {
                b.any = true;
                if (x < b.min_x) b.min_x = x;
                if (y < b.min_y) b.min_y = y;
                if (x > b.max_x) b.max_x = x;
                if (y > b.max_y) b.max_y = y;
            }
        }
    }
    return b;
}

}  // namespace

bool test_ui_text_dpi() {
    printf("\n=== UI Text DPI (HUD glyph scale follows render resolution) ===\n");

    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "ui text dpi";
    cfg.enable_chat_window = false;
    cfg.window_width = 800;
    cfg.window_height = 600;
    Engine engine;
    if (engine.initialize(cfg) != 0) {
        printf("  ERROR: engine init failed\n");
        return false;
    }
    UISystem* ui = engine.get_ui_system();
    if (!ui) {
        printf("  ERROR: no UISystem\n");
        return false;
    }

    // Baseline: render == window -> scale 1, glyph is 5x7 at (10,10).
    engine.get_render_buffer().clear(0, 0, 0, 255, ObjectID::BACKGROUND);
    ui->draw_text(10, 10, "H", 255, 0, 0);
    Bbox base = red_bbox(engine);
    if (!base.any) {
        printf("  ERROR: baseline glyph drew nothing\n");
        return false;
    }
    int base_w = base.max_x - base.min_x + 1;
    int base_h = base.max_y - base.min_y + 1;
    printf("  scale 1: glyph at (%d,%d) %dx%d px\n",
           base.min_x, base.min_y, base_w, base_h);
    bool base_ok = (base.min_x == 10 && base.min_y == 10 &&
                    base_w == 5 && base_h == 7);
    if (!base_ok) {
        printf("  FAIL: baseline expected 5x7 at (10,10)\n");
        return false;
    }

    // Retina-native: render 2x the logical window -> glyphs must follow.
    engine.set_render_resolution(1600, 1200);
    engine.get_render_buffer().clear(0, 0, 0, 255, ObjectID::BACKGROUND);
    ui->draw_text(10, 10, "H", 255, 0, 0);
    Bbox hi = red_bbox(engine);
    if (!hi.any) {
        printf("  ERROR: hi-dpi glyph drew nothing\n");
        return false;
    }
    int hi_w = hi.max_x - hi.min_x + 1;
    int hi_h = hi.max_y - hi.min_y + 1;
    printf("  scale 2: glyph at (%d,%d) %dx%d px (expected 10x14 at (20,20))\n",
           hi.min_x, hi.min_y, hi_w, hi_h);
    bool hi_ok = (hi.min_x == 20 && hi.min_y == 20 &&
                  hi_w == 10 && hi_h == 14);
    if (!hi_ok) {
        printf("  FAIL: HUD text does not scale with render resolution\n");
        return false;
    }

    printf("  PASS\n");
    return true;
}
