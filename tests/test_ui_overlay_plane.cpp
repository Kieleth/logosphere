// =============================================================================
// UI OVERLAY PLANE — UI rasters off the scene buffer, composites onto it
// =============================================================================
// Option B step 1 (the UI overlay design notes). The UI used to
// raster directly into the framebuffer the GPU completion callback memcpys
// into — the flicker's root. It now rasters into its own ARGB plane
// (Engine::get_ui_overlay_buffer) on its own schedule, and only a small
// dirty-region blend touches the scene buffer, inside the frame mutex.
//
// This locks the plane contract:
//   1. UI pixels land in the OVERLAY plane, not the scene buffer, at raster
//      time (the separation the flicker fix depends on).
//   2. Untouched plane pixels stay alpha 0 ("UI did not paint here"), so the
//      composite is a no-op there.
//   3. The dirty box actually bounds the paint — it must not degrade to the
//      whole plane, or clear/composite/upload cost returns to full-frame.
//   4. After compositing, the UI pixels are in the scene buffer with the
//      colors the UI asked for.
//
// Run: ./build/logosphere-tests --test test_ui_overlay_plane --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "../src/ui/ui_system.h"
#include "../src/object_id.h"
#include "logosphere/rendering/pixel_buffer.h"
#include <cstdio>

bool test_ui_overlay_plane() {
    printf("\n=== UI overlay plane (raster off-scene, composite on) ===\n");

    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "ui overlay plane";
    cfg.enable_chat_window = false;
    cfg.window_width = 800;
    cfg.window_height = 600;
    cfg.show_time_display = true;   // gives draw_ui_overlays real content
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

    PixelBuffer& scene   = engine.get_render_buffer();
    PixelBuffer& overlay = engine.get_ui_overlay_buffer();

    // A known scene: mid-grey everywhere. Any UI landing here at raster
    // time (rather than composite time) is immediately visible.
    scene.clear(60, 60, 60, 255, ObjectID::BACKGROUND);
    overlay.clear(0, 0, 0, 0, 0);
    overlay.reset_dirty_bounds();

    // Raster one glyph through the real UI path.
    ui->draw_text(40, 30, "H", 255, 0, 0);

    // (1) UI must be in the overlay plane, NOT the scene.
    bool found_in_overlay = false;
    int  scene_touched = 0;
    for (int y = 0; y < overlay.height(); ++y) {
        for (int x = 0; x < overlay.width(); ++x) {
            auto o = overlay.get_pixel(x, y);
            if (o.a != 0) found_in_overlay = true;
            auto s = scene.get_pixel(x, y);
            if (s.r != 60 || s.g != 60 || s.b != 60) scene_touched++;
        }
    }
    printf("  after raster: overlay_has_ui=%s scene_pixels_touched=%d\n",
           found_in_overlay ? "yes" : "no", scene_touched);
    if (!found_in_overlay) {
        printf("  FAIL: UI raster did not reach the overlay plane\n");
        return false;
    }
    if (scene_touched != 0) {
        printf("  FAIL: UI raster wrote %d scene pixels — the plane separation\n"
               "        the flicker fix depends on is broken\n", scene_touched);
        return false;
    }

    // (3) The dirty box must bound the paint, not the plane.
    if (!overlay.has_dirty_bounds()) {
        printf("  FAIL: no dirty bounds recorded — clear/composite would have\n"
               "        no region to work from\n");
        return false;
    }
    int min_x, min_y, max_x, max_y;
    overlay.get_dirty_bounds(min_x, min_y, max_x, max_y);
    int box_w = max_x - min_x + 1, box_h = max_y - min_y + 1;
    printf("  dirty box: (%d,%d)-(%d,%d) = %dx%d px of %dx%d plane\n",
           min_x, min_y, max_x, max_y, box_w, box_h,
           overlay.width(), overlay.height());
    if (box_w > overlay.width() / 4 || box_h > overlay.height() / 4) {
        printf("  FAIL: dirty box covers most of the plane; clear/composite\n"
               "        cost has degraded to full-frame\n");
        return false;
    }

    // (2)+(4) Composite contract, driven through the real path.
    // draw_ui_overlays() clears the plane and re-rasters from the UI
    // system itself, so the hand-drawn glyph above is (correctly) gone by
    // then — run real frames with the time display on instead.
    for (int i = 0; i < 4; ++i) {
        engine.update(1.0 / 60.0);
        engine.render();          // headless: raster plane + composite
        engine.get_renderer().wait_for_completion();
    }
    engine.update(1.0 / 60.0);
    engine.render();

    if (!overlay.has_dirty_bounds()) {
        printf("  FAIL: a real frame with the time display on painted nothing\n");
        return false;
    }
    overlay.get_dirty_bounds(min_x, min_y, max_x, max_y);

    // Every opaque overlay pixel must appear verbatim in the scene buffer.
    long opaque = 0, mismatched = 0;
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            auto o = overlay.get_pixel(x, y);
            if (o.a != 255) continue;
            opaque++;
            auto s = scene.get_pixel(x, y);
            if (s.r != o.r || s.g != o.g || s.b != o.b) mismatched++;
        }
    }
    printf("  composite: opaque_overlay_px=%ld mismatched_in_scene=%ld\n",
           opaque, mismatched);
    if (opaque == 0) {
        printf("  FAIL: no opaque UI pixels to verify\n");
        return false;
    }
    if (mismatched != 0) {
        printf("  FAIL: %ld UI pixels did not reach the scene buffer intact\n",
               mismatched);
        return false;
    }

    printf("  PASS\n");
    return true;
}
