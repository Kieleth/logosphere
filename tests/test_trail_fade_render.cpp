// =============================================================================
// Regression (measured, headless): fade_out visibly dims a self-emissive
// particle on screen. Guards the emissive-fade rendering contract.
// =============================================================================
// History (fade RCA, 2026-07-18): the crashed-trail fade "popped" instead of
// fading. Root cause, measured in pixels: alpha is a render ROUTING KEY. The
// moment particle_a < 1.0, prepare_gpu_data reroutes the surface out of the
// deferred (opaque) pipeline into the forward transparent pass, whose emissive
// branch draws full-bright regardless of color. So an emissive fade that
// touches alpha freezes at full brightness until deletion. The fix: fade_out
// ramps COLOR with alpha pinned at its initial value for self-emissive
// particles (a glow dims, it never turns translucent); normally-lit particles
// keep the alpha fade.
//
// This test locks both halves:
//   part 1 (pixels)  the REAL fade_out effect on a rendered self-emissive box:
//                    the GPU framebuffer's brightness must RAMP DOWN across
//                    the fade (bright -> dim), not hold flat until deletion.
//   part 2 (contract) during the fade the particle's alpha stays pinned at
//                    its initial value while the color ramps — the mechanism
//                    that keeps the surface in the deferred pipeline.
//
// Run:  cmake --build build --target test_trail_fade_render
//       ./build/test_trail_fade_render 2>/dev/null | grep -E 't=|maxR|PASS|FAIL'
// =============================================================================

#include "application.h"
#include "core/engine.h"
#include "core/particle_system.h"
#include "logosphere/interaction/particle_interaction_system.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/rendering/pixel_buffer.h"
#include "particle.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

class HeadlessApp : public Logosphere::IApplication {
public:
    bool initialize() override { return true; }
    void shutdown() override {}
    GLFWwindow* get_window() override { return nullptr; }
};

namespace {
int max_red(const std::vector<uint32_t>& px) {
    int m = 0;
    for (uint32_t p : px) { int r = (p >> 16) & 0xff; if (r > m) m = r; }
    return m;
}
} // namespace

int main() {
    if (std::getenv("CI")) {
        // Same guard as test_engine_headless_render: Metal GPU work is not
        // available on CI runners for this target.
        printf("SKIP (CI)\n");
        return 0;
    }
    printf("\n=== Trail Fade Render (real fade_out, measured, headless) ===\n");

    HeadlessApp app;
    Engine engine(&app);
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_width = 200;
    cfg.window_height = 200;
    cfg.window_title = "trail-fade-render";
    cfg.show_debug_overlay = false;
    cfg.show_kg_inspector = false;
    cfg.enable_chat_window = false;
    if (engine.initialize(cfg) < 0) { printf("  ERROR: init failed\n"); return 1; }

    auto& kg = engine.get_kg();
    auto& ps = engine.get_particle_system();
    auto& renderer = engine.get_renderer();

    // Self-emissive orange box at the origin, KG-backed (so fade_out can arm).
    // No camera changes: the default view frames the origin and dispatches.
    auto ent = kg.createEntity("Rock");
    Particle box{};
    box.shape = ParticleShape::BOX;
    box.x = 0; box.y = 0; box.z = 0.5f;
    box.width = 1.0f; box.height = 1.0f; box.thickness = 0.5f;
    box.r = 1.0f; box.g = 0.5f; box.b = 0.1f; box.a = 1.0f;
    box.is_self_emissive = true;
    box.emission_strength = 2.5f;
    box.SetMaterial(Materials::Type::STONE);
    box.owner = ParticleOwner::STATIC;
    box.is_at_rest = true;
    ps.add_particle_to_entity(box, &kg, ent);
    auto kgids = kg.getEntityKGParticles(ent);
    if (kgids.empty()) { printf("  ERROR: no KG particle\n"); return 1; }
    kg::KGParticleID box_kgid = kgids[0];

    auto rule = kg.createEntity("TransformationRule");
    kg.setProperty(rule, "trigger", "on_timer");
    kg.setProperty(rule, "effect", "fade_out");
    kg.setProperty(rule, "duration_s", "2.0");
    auto& isys = engine.get_interaction_system();
    isys.load_rules_from_kg(kg);

    const int W = engine.get_render_buffer().width();
    const int H = engine.get_render_buffer().height();
    std::vector<uint32_t> px(static_cast<size_t>(W) * H);

    auto frame = [&]() -> int {
        engine.update(1.0 / 60.0);
        engine.render();
        renderer.wait_for_completion();
        int w = 0, h = 0;
        return engine.read_latest_framebuffer(px.data(), w, h) ? max_red(px) : -1;
    };

    for (int i = 0; i < 5; ++i) frame();          // warm up the GPU pipeline
    int pre = frame();
    isys.arm_transformation(rule, {box_kgid});     // fire the real fade

    struct S { float t; float data_r; float data_a; int rmax; bool present; };
    std::vector<S> log;
    for (int f = 0; f < 140; ++f) {                // ~2.33 s
        int rmax = frame();
        S s; s.t = static_cast<float>(f) / 60.0f; s.rmax = rmax;
        auto ri = kg.getRenderIndex(box_kgid);
        s.present = ri != kg::INVALID_RENDER_INDEX;
        s.data_r = -1.0f; s.data_a = -1.0f;
        if (s.present) {
            auto v = ps.lock_particles_for_read();
            s.data_r = v[ri].r;
            s.data_a = v[ri].a;
        }
        log.push_back(s);
        if (f % 15 == 0)
            printf("  t=%.2f  data_r=%.3f  data_a=%.3f  rendered_maxR=%d%s\n",
                   s.t, s.data_r, s.data_a, s.rmax,
                   s.present ? "" : "  [deleted]");
    }

    auto at = [&](float t) -> const S& {
        size_t best = 0; float bd = 1e9f;
        for (size_t i = 0; i < log.size(); ++i) {
            if (log[i].rmax < 0 || !log[i].present) continue;
            float d = log[i].t - t; if (d < 0) d = -d;
            if (d < bd) { bd = d; best = i; }
        }
        return log[best];
    };
    const S& early = at(0.20f);
    const S& mid   = at(1.00f);
    const S& late  = at(1.70f);

    printf("\n  pre-arm rendered_maxR=%d\n", pre);
    printf("  early t=%.2f rendered_maxR=%d (data_r=%.3f a=%.3f)\n",
           early.t, early.rmax, early.data_r, early.data_a);
    printf("  mid   t=%.2f rendered_maxR=%d (data_r=%.3f a=%.3f)\n",
           mid.t, mid.rmax, mid.data_r, mid.data_a);
    printf("  late  t=%.2f rendered_maxR=%d (data_r=%.3f a=%.3f)\n",
           late.t, late.rmax, late.data_r, late.data_a);

    // Part 1: rendered brightness ramps down across the fade.
    bool read_worked = early.rmax >= 0;
    bool box_lit = pre > 120;
    bool renders_ramp = early.rmax > mid.rmax + 20 && mid.rmax > late.rmax + 10;

    // Part 2: alpha stays pinned at 1.0 the whole fade (the routing contract),
    // while the color actually ramps in the data.
    bool alpha_pinned = early.data_a > 0.99f && mid.data_a > 0.99f &&
                        late.data_a > 0.99f;
    bool color_ramped = early.data_r > mid.data_r + 0.2f &&
                        mid.data_r > late.data_r + 0.1f;
    bool deleted = !log.back().present;

    if (!read_worked) printf("  ERROR: framebuffer read failed.\n");
    else if (!box_lit) printf("  ERROR: box not lit pre-fade (maxR=%d).\n", pre);

    bool pass = read_worked && box_lit && renders_ramp &&
                alpha_pinned && color_ramped && deleted;
    printf("  renders_ramp=%d alpha_pinned=%d color_ramped=%d deleted=%d\n",
           (int)renders_ramp, (int)alpha_pinned, (int)color_ramped, (int)deleted);
    printf("  [%s] emissive fade_out dims on screen with alpha pinned\n",
           pass ? "PASS" : "FAIL");

    engine.shutdown();
    return pass ? 0 : 1;
}
