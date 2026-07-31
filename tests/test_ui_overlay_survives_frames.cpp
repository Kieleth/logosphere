// =============================================================================
// UI OVERLAY SURVIVES GPU FRAMES — the flicker regression lock
// =============================================================================
// The handover asked for this one by name: "HUD pixels present in N
// consecutive presented frames" (the UI overlay design notes).
//
// The original bug: UI was rastered into the same buffer the GPU completion
// callback memcpys fresh frames into. A callback landing between the UI draw
// and the blit erased the HUD, and the screen showed a HUD-less frame. Fix A
// closed the window by putting draw+blit in one critical section; Option B
// removed the window from existing by giving the UI its own plane.
//
// This test reproduces the exact race and asserts it is now harmless:
//   1. render a real frame (UI rasters into the overlay plane),
//   2. overwrite the ENTIRE scene buffer — precisely what the completion
//      callback does, at the worst possible moment,
//   3. build the presentable overlay content and assert the HUD is still in
//      it, composited over the NEW scene.
// Repeated N times: the "N consecutive presented frames" the handover wants.
//
// Also locks the accumulation invariant: building presentable content must
// not mutate the scene buffer, because the same scene is re-presented on
// every UI-only refresh and blending into it would stack UI over UI.
//
// Run: ./build/logosphere-tests --test test_ui_overlay_survives_frames --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "../src/ui/ui_system.h"
#include "../src/object_id.h"
#include "logosphere/rendering/pixel_buffer.h"
#include <cstdio>
#include <vector>

bool test_ui_overlay_survives_frames() {
    printf("\n=== UI overlay survives GPU frames (flicker regression lock) ===\n");

    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "ui overlay survives frames";
    cfg.enable_chat_window = false;
    cfg.window_width = 800;
    cfg.window_height = 600;
    cfg.show_time_display = true;   // real HUD content every frame
    Engine engine;
    if (engine.initialize(cfg) != 0) {
        printf("  ERROR: engine init failed\n");
        return false;
    }

    PixelBuffer& scene = engine.get_render_buffer();

    // Sentinel the completion callback "writes": a colour the UI never uses,
    // so any non-sentinel pixel in the presentable rect came from the HUD.
    const uint8_t SR = 7, SG = 11, SB = 13;

    const int FRAMES = 30;
    int frames_with_hud = 0;
    int frames_scene_mutated = 0;
    long min_hud_px = -1;

    for (int f = 0; f < FRAMES; ++f) {
        // (1) A real frame: UI rasters into the overlay plane.
        engine.update(1.0 / 60.0);
        engine.render();
        engine.get_renderer().wait_for_completion();

        // (2) The race: a GPU completion callback lands right now and memcpys
        //     a fresh scene over everything — including any UI that had been
        //     composited into the scene buffer.
        scene.clear(SR, SG, SB, 255, ObjectID::BACKGROUND);

        // (3) What present() would upload.
        int x = 0, y = 0, w = 0, h = 0;
        if (!engine.build_overlay_staging(x, y, w, h)) {
            printf("  frame %d: FAIL — nothing presentable; the HUD would be\n"
                   "           missing from this presented frame\n", f);
            return false;
        }
        const std::vector<uint32_t>& staging = engine.get_overlay_staging();

        long hud_px = 0;
        for (int py = 0; py < h; ++py) {
            for (int px = 0; px < w; ++px) {
                uint32_t bgra = staging[static_cast<size_t>(py) * w + px];
                uint8_t r = (bgra >> 16) & 0xFF;
                uint8_t g = (bgra >> 8)  & 0xFF;
                uint8_t b =  bgra        & 0xFF;
                if (r != SR || g != SG || b != SB) hud_px++;
            }
        }
        if (hud_px > 0) frames_with_hud++;
        if (min_hud_px < 0 || hud_px < min_hud_px) min_hud_px = hud_px;

        // Accumulation invariant: the scene must still be pure sentinel.
        bool mutated = false;
        for (int py = y; py < y + h && !mutated; ++py) {
            for (int px = x; px < x + w; ++px) {
                auto s = scene.get_pixel(px, py);
                if (s.r != SR || s.g != SG || s.b != SB) { mutated = true; break; }
            }
        }
        if (mutated) frames_scene_mutated++;
    }

    printf("  %d/%d presented frames carried HUD pixels (min %ld px per frame)\n",
           frames_with_hud, FRAMES, min_hud_px);
    printf("  scene buffer mutated by staging build in %d/%d frames\n",
           frames_scene_mutated, FRAMES);

    if (frames_with_hud != FRAMES) {
        printf("  FAIL: %d frame(s) would have presented without the HUD —\n"
               "        the flicker is reachable again\n", FRAMES - frames_with_hud);
        return false;
    }
    if (frames_scene_mutated != 0) {
        printf("  FAIL: the staging build wrote into the scene buffer; UI would\n"
               "        stack over UI across UI-only refreshes\n");
        return false;
    }

    printf("  PASS\n");
    return true;
}
