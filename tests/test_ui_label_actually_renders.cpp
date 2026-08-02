// =============================================================================
// DOES A REGISTERED WIDGET ACTUALLY PUT PIXELS ON SCREEN?
// =============================================================================
// Four visual tests in this repo have now shipped with an on-screen readout the
// user could not see. docs/VISUAL_TESTS.md explains one cause (immediate-mode
// draw_text between render() and present() is erased, because present() clears
// the overlay plane first) and recommends registered widgets instead, since
// draw_ui_overlays re-renders those.
//
// THAT RECOMMENDATION WAS NEVER VERIFIED. It was inferred from the compass
// being visible. This test settles it the only way that counts: render the same
// frame with and without a Label, and compare pixels.
//
// A-vs-A FIRST. Two identical renders establish this engine's noise floor
// (equal-depth geometry has documented +/-1 LSB nondeterminism, ~30,000 pixels).
// Without that control, "pixels differ" means nothing, which is a mistake this
// campaign has already made once and paid for.
//
// If this FAILS, every test that reports through a widget is reporting into a
// void, and the honest answer is stdout.
//
//   ./build-release/logosphere-tests --test test_ui_label_actually_renders --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include "../src/ui/ui_system.h"
#include "../src/ui/widgets.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

struct Shot { std::vector<uint32_t> px; int w = 0, h = 0; };

// Bucketed by magnitude: a glyph moves pixels by tens or hundreds, noise by 1.
struct Diff { long any = 0, over32 = 0; int max_delta = 0; };

Diff compare(const Shot& a, const Shot& b) {
    Diff d;
    const size_t n = std::min(a.px.size(), b.px.size());
    for (size_t i = 0; i < n; ++i) {
        const uint32_t p = a.px[i], q = b.px[i];
        const int w = std::max({
            std::abs((int)((p >> 16) & 0xFF) - (int)((q >> 16) & 0xFF)),
            std::abs((int)((p >>  8) & 0xFF) - (int)((q >>  8) & 0xFF)),
            std::abs((int)( p        & 0xFF) - (int)( q        & 0xFF))});
        if (w >= 1)  d.any++;
        if (w >= 32) d.over32++;
        d.max_delta = std::max(d.max_delta, w);
    }
    return d;
}

Shot capture(Engine& engine, int frames) {
    for (int f = 0; f < frames; ++f) {
        engine.update(1.0 / 60.0);
        engine.render();
        engine.present();          // the overlay clear lives in here
    }
    engine.get_renderer().wait_for_completion();
    Shot s;
    s.w = engine.get_render_buffer().width();
    s.h = engine.get_render_buffer().height();
    s.px.assign((size_t)s.w * s.h, 0u);
    engine.read_latest_framebuffer(s.px.data(), s.w, s.h);
    return s;
}

}  // namespace

bool test_ui_label_actually_renders() {
    printf("\n=== DOES A REGISTERED WIDGET RENDER? ===\n");

    EngineConfig cfg;
    cfg.create_display = false;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) { printf("  ERROR: engine init failed\n"); return false; }

    // A lit floor so the frame is not uniformly black: a glyph on black and a
    // glyph on grey are different tests, and the real HUD sits over content.
    auto& ps = engine.get_particle_system();
    for (int r = -4; r <= 4; ++r)
        for (int c = -4; c <= 4; ++c) {
            Particle p = {};
            p.shape = ParticleShape::BOX;
            p.x = (float)c; p.y = (float)r; p.z = 0.05f;
            p.width = p.height = 1.0f; p.thickness = 0.1f; p.size = 1.0f;
            p.r = 0.55f; p.g = 0.55f; p.b = 0.6f; p.a = 1.0f;
            p.SetMaterial(Materials::Type::STONE);
            int id = engine.add_particle(p);
            auto v = ps.lock_particles_for_write();
            v[id].solver_mode = ParticleSolverMode::KINEMATIC;
            v[id].owner = ParticleOwner::DYNAMICS; v[id].is_at_rest = true;
        }
    ps.queue_light(-3.0f, -4.0f, 9.0f, 200000.0f, 40.0f, 1.0f, 0.97f, 0.92f);
    ps.flush_pending_particles();

    const Shot before  = capture(engine, 30);
    const Shot before2 = capture(engine, 8);     // A-vs-A: the noise floor
    const Diff noise   = compare(before, before2);

    // Now add the widget. Big, bright, top-left, over the lit floor.
    auto* label = new ui::Label("ASSERTION VIOLATED 12345678", "probe_label");
    label->set_position(40, 40);
    label->set_size(700, 24);
    label->set_color(255, 40, 40);
    auto* uis = engine.get_ui_system();
    if (!uis) { printf("  ERROR: no UI system\n"); engine.shutdown(); return false; }
    uis->add_widget(label);

    const Shot after = capture(engine, 8);
    const Diff got   = compare(before2, after);

    printf("  framebuffer          %dx%d\n", before.w, before.h);
    printf("  %-34s %10s %10s %8s\n", "comparison", "delta>=1", "delta>=32", "max");
    printf("  %-34s %10ld %10ld %8d\n", "A vs A (NOISE FLOOR)", noise.any, noise.over32, noise.max_delta);
    printf("  %-34s %10ld %10ld %8d\n", "with label vs without", got.any, got.over32, got.max_delta);

    const bool rendered = got.over32 > noise.over32;
    printf("\n");
    if (rendered) {
        printf("  WIDGETS RENDER. %ld pixels moved by >=32 when the Label was added, against\n"
               "  a floor of %ld. docs/VISUAL_TESTS.md is right: register a widget, do not\n"
               "  call draw_text between render() and present().\n", got.over32, noise.over32);
    } else {
        printf("  *** WIDGETS DO NOT RENDER. ***\n"
               "  Adding a bright red Label over a lit floor changed %ld pixels by >=32,\n"
               "  against a noise floor of %ld. Nothing reached the screen.\n"
               "  Every test in this repo reporting through a widget is reporting into a\n"
               "  void, and docs/VISUAL_TESTS.md recommends a route that does not work.\n"
               "  Until this is fixed, on-screen readouts must go to stdout or the window\n"
               "  title, and this test is the gate that says when it is fixed.\n",
               got.over32, noise.over32);
    }
    printf("\n  %s\n", rendered ? "PASS" : "FAIL");
    engine.shutdown();
    return rendered;
}
