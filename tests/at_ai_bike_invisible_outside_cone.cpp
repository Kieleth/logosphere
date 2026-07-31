// Engine acceptance test — vision-cone fog-of-war (Pass 4 of the
// deferred renderer) must darken pixels outside the viewer's FOV.
// Original motivating bug (logotron, observed live 2026-04-23): the
// AI bike was still visible outside the player's cone of vision
// instead of being hidden behind the dim "fog" the cone applies. The
// bike is dynamic (`is_dynamic = true`) so the vision-memory grid
// should not light it back up either.
//
// Scope today (scaffold). Two things shipped:
//
//   1. Cone-darkening of sky/clear pixels works end-to-end. With the
//      cone disabled, the engine's clear color (RGBA 10,10,15,255 →
//      BGRA 0xff0a0a0f) dominates an empty headless scene. With the
//      cone enabled, the metal kernel multiplies sky pixels by
//      `params.darkness` (default 0.15, set explicitly here for
//      determinism). Asserting that the cleared backdrop becomes
//      visibly darker proves the cone is wired through Engine →
//      RenderPipeline → GPURasterizer → kernel uniform.
//
//   2. Cone-disable is symmetric: re-running with the cone off keeps
//      the clear color intact.
//
// What this DOES NOT yet cover (follow-on once the scaffold is
// trusted):
//
//   - Dynamic-target hiding: place a bright `is_dynamic = true` BOX
//     behind the viewer, assert its pixels end up at darkness*
//     albedo. This needs a deterministic camera + light setup
//     (default headless scene yields ~achromatic full-screen
//     output, which makes per-target color sniffing unreliable).
//     Likely a separate AT that drives the camera explicitly + adds
//     a known-good occluder geometry. Tracked in
//     project_headless_render_path.md.
//
//   - LOS occlusion bin math (`set_vision_cone_occlusion`). Needs an
//     explicit occluder + a known-good distance array.

#include "application.h"
#include "core/engine.h"
#include "core/particle_system.h"
#include "logosphere/rendering/pixel_buffer.h"
#include "particle.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

class HeadlessTestApp : public Logosphere::IApplication {
public:
    bool initialize() override { return true; }
    void shutdown() override {}
    GLFWwindow* get_window() override { return nullptr; }
};

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    std::cout << "  " #name "... "; \
    try { name(); tests_passed++; std::cout << "PASS" << std::endl; } \
    catch (const std::exception& e) { tests_failed++; std::cout << "FAIL: " << e.what() << std::endl; }

#define ASSERT_TRUE(cond, msg) \
    if (!(cond)) throw std::runtime_error(std::string(msg))

static void init_engine(Engine& engine) {
    EngineConfig config;
    config.create_display = false;
    config.window_width = 200;
    config.window_height = 200;
    config.window_title = "vision-cone-at";
    config.show_debug_overlay = false;
    config.show_kg_inspector = false;
    config.enable_chat_window = false;
    int observer = engine.initialize(config);
    if (observer < 0)
        throw std::runtime_error("engine.initialize() failed in headless");
}

static void tick_and_sync(Engine& engine, int frames) {
    auto& renderer = engine.get_renderer();
    for (int i = 0; i < frames; ++i) {
        engine.update(1.0f / 60.0f);
        engine.render();
        renderer.wait_for_completion();
    }
}

// One trivial particle is needed to kick the GPU rasterizer out of
// its triangle_count==0 early-exit; without it Pass 1/3/4 never
// dispatch and the framebuffer slot is never written.
static void add_trigger_particle(Engine& engine) {
    Particle p = {};
    p.shape = ParticleShape::BOX;
    p.x = 0; p.y = 0; p.z = 0.5f;
    p.width = 0.1f; p.height = 0.1f; p.thickness = 0.1f;
    p.r = 0.5f; p.g = 0.5f; p.b = 0.5f; p.a = 1.0f;
    p.SetMaterial(Materials::Type::STONE);
    p.owner = ParticleOwner::STATIC;
    p.is_at_rest = true;
    engine.get_particle_system().add_particle_to_entity(p, nullptr, 0);
}

// Sample BGRA pixel and return its R+G+B sum. Useful when comparing
// "darkened" vs "bright" without committing to an exact channel.
static unsigned brightness(uint32_t bgra) {
    unsigned b = (bgra >> 0)  & 0xFF;
    unsigned g = (bgra >> 8)  & 0xFF;
    unsigned r = (bgra >> 16) & 0xFF;
    return r + g + b;
}

// Sky pixels equal the clear color exactly when the cone is OFF.
// 0xff0a0a0f = (R=10, G=10, B=15, A=255). Centered count rather
// than a per-pixel assert so a stray non-sky pixel from the trigger
// particle doesn't fail the test.
static size_t count_at_brightness(const std::vector<uint32_t>& pixels,
                                  unsigned target_sum,
                                  unsigned tolerance) {
    size_t hits = 0;
    for (uint32_t p : pixels) {
        unsigned s = brightness(p);
        unsigned diff = (s > target_sum) ? (s - target_sum) : (target_sum - s);
        if (diff <= tolerance) hits++;
    }
    return hits;
}

// =========================================================================
// AT — the cone uniform actually reaches the GPU. Run two passes:
// cone OFF then cone ON. Compare the dominant brightness in the sky
// region. Cone ON should drive the cleared sky from R+G+B = 35 down
// to ~0.15 * 35 ≈ 5. If the uniform isn't reaching the kernel, both
// passes look the same and the assert fires.
// =========================================================================
void cone_disabled_keeps_clear_color_then_enabled_darkens_it() {
    constexpr uint32_t kClearBGRA = 0xff0a0a0fu;  // R=10 G=10 B=15
    constexpr unsigned kClearSum  = 10u + 10u + 15u;  // 35

    // -------------------- Pass 1: cone disabled --------------------
    HeadlessTestApp app;
    Engine engine(&app);
    init_engine(engine);
    add_trigger_particle(engine);

    engine.set_vision_cone(/*viewer_x=*/0.0f, /*viewer_y=*/0.0f,
                           /*look_direction=*/0.0f,
                           /*fov_radians=*/static_cast<float>(M_PI) * 0.5f,
                           /*range=*/20.0f);
    engine.set_vision_cone_style(/*inner_falloff=*/0.6f,
                                 /*darkness=*/0.15f);
    engine.set_vision_cone_enabled(false);

    tick_and_sync(engine, 5);

    const int w = engine.get_render_buffer().width();
    const int h = engine.get_render_buffer().height();
    std::vector<uint32_t> off_pixels(static_cast<size_t>(w) * h);
    int got_w = 0, got_h = 0;
    ASSERT_TRUE(engine.read_latest_framebuffer(off_pixels.data(), got_w, got_h),
        "pass-1 read_latest_framebuffer failed");

    size_t off_clear_count = count_at_brightness(off_pixels, kClearSum, 2);
    double off_clear_ratio = double(off_clear_count) / double(off_pixels.size());

    // -------------------- Pass 2: cone enabled --------------------
    engine.set_vision_cone_enabled(true);
    tick_and_sync(engine, 5);

    std::vector<uint32_t> on_pixels(static_cast<size_t>(w) * h);
    ASSERT_TRUE(engine.read_latest_framebuffer(on_pixels.data(), got_w, got_h),
        "pass-2 read_latest_framebuffer failed");

    // After cone darken: clear color RGB(10,10,15) * 0.15 = ~(1,1,2),
    // sum ≈ 4 (integer floor). Allow ±2.
    constexpr unsigned kDarkenedSum = 4u;
    size_t on_dark_count = count_at_brightness(on_pixels, kDarkenedSum, 2);
    double on_dark_ratio = double(on_dark_count) / double(on_pixels.size());

    std::cout << "[fb=" << w << "x" << h
              << " off_clear=" << off_clear_count
              << "/" << off_pixels.size()
              << " (" << (off_clear_ratio * 100.0) << "%)"
              << " on_dark=" << on_dark_count
              << "/" << on_pixels.size()
              << " (" << (on_dark_ratio * 100.0) << "%)] ";

    ASSERT_TRUE(off_clear_ratio > 0.95,
        std::string("with cone OFF, expected >95% pixels to match the "
                    "clear color; got ") + std::to_string(off_clear_ratio));

    ASSERT_TRUE(on_dark_ratio > 0.95,
        std::string("with cone ON, expected >95% pixels to be darkened "
                    "to ~clear*0.15; got ") + std::to_string(on_dark_ratio));

    engine.shutdown();
}

// Spawn a bright, dynamic light source at (x, y, z). Bright enough
// to clearly stand out from the cone-darkened sky. Tagged
// is_dynamic=true so the vision-memory grid does NOT light it back
// up after the cone moves past — matches the production AI-bike
// flag the original bug repro depends on.
static void add_dynamic_light(Engine& engine, float x, float y, float z) {
    Particle p = {};
    p.shape = ParticleShape::SPHERE;
    p.x = x; p.y = y; p.z = z;
    p.width = 0.6f; p.height = 0.6f; p.thickness = 0.6f;
    p.r = 1.0f; p.g = 1.0f; p.b = 1.0f; p.a = 1.0f;
    p.is_light_source   = true;
    p.emission_strength = 6.0f;
    p.emission_radius   = 8.0f;
    p.is_dynamic        = true;        // <- the AI-bike-bug flag
    p.SetMaterial(Materials::Type::STONE);
    p.owner = ParticleOwner::STATIC;
    p.is_at_rest = true;
    engine.get_particle_system().add_particle_to_entity(p, nullptr, 0);
}

// Count pixels visibly brighter than the cone-darkened sky baseline.
// "Bright" = R+G+B > threshold. With cone darkness=0.15 and clear
// (10,10,15), darkened sky sums to ~4. A lit dynamic source at peak
// emits white that sums near 765. Threshold 60 is safely above
// any cone-darkened backdrop and well below a fully-lit pixel.
static size_t count_bright(const std::vector<uint32_t>& pixels,
                           unsigned threshold = 60) {
    size_t hits = 0;
    for (uint32_t p : pixels) {
        if (brightness(p) > threshold) ++hits;
    }
    return hits;
}

// =========================================================================
// AT — adding a dynamic target BEHIND the viewer must not leak
// bright pixels into the framebuffer. The cone (Pass 4) should
// darken the behind-target's contribution. We measure indirectly:
// scene with only an in-front target vs scene with that same
// target PLUS a behind-target. Bright-pixel count should stay
// roughly equal (the behind target is hidden). If the count
// grows substantially, the behind target leaked through the
// cone — the live AI-bike-visible-outside-cone bug.
//
// Why this dodges the brittleness of per-target color sniffing:
// the headless camera + lighting can wash out individual colors,
// but the COUNT of bright pixels is robust to camera framing and
// only depends on whether the cone is hiding what it should.
// =========================================================================
void cone_must_hide_dynamic_target_behind_viewer() {
    HeadlessTestApp app;
    Engine engine(&app);
    init_engine(engine);

    // Two trigger particles so the GPU pipeline doesn't early-exit.
    add_trigger_particle(engine);

    // Cone: viewer at origin, looking +Y, 90° FOV, 20 m range.
    // Dark factor 0.15 — same as Logotron live.
    engine.set_vision_cone(/*viewer_x=*/0.0f, /*viewer_y=*/0.0f,
                           /*look_direction=*/0.0f,
                           /*fov_radians=*/static_cast<float>(M_PI) * 0.5f,
                           /*range=*/20.0f);
    engine.set_vision_cone_style(/*inner_falloff=*/0.6f,
                                 /*darkness=*/0.15f);
    engine.set_vision_cone_enabled(true);

    // Pass A — only an in-front (in-cone) bright dynamic target.
    add_dynamic_light(engine, /*x=*/0.0f, /*y=*/+5.0f, /*z=*/0.5f);
    tick_and_sync(engine, 6);

    const int w = engine.get_render_buffer().width();
    const int h = engine.get_render_buffer().height();
    std::vector<uint32_t> pix_a(static_cast<size_t>(w) * h);
    int got_w = 0, got_h = 0;
    ASSERT_TRUE(engine.read_latest_framebuffer(pix_a.data(), got_w, got_h),
        "pass-A read_latest_framebuffer failed");
    size_t bright_a = count_bright(pix_a);

    // Pass B — same in-front target PLUS one BEHIND the viewer at
    // (0, -5). The cone covers FOV +-45° centered on +Y, so y=-5
    // is squarely outside. If the cone hides it, bright_b ≈
    // bright_a. If the cone leaks it, bright_b > bright_a by the
    // pixel area of the behind-target's lit footprint.
    add_dynamic_light(engine, /*x=*/0.0f, /*y=*/-5.0f, /*z=*/0.5f);
    tick_and_sync(engine, 6);

    std::vector<uint32_t> pix_b(static_cast<size_t>(w) * h);
    ASSERT_TRUE(engine.read_latest_framebuffer(pix_b.data(), got_w, got_h),
        "pass-B read_latest_framebuffer failed");
    size_t bright_b = count_bright(pix_b);

    std::cout << "[fb=" << w << "x" << h
              << " bright_in_front_only=" << bright_a
              << " bright_with_behind="   << bright_b
              << " delta=" << (long)bright_b - (long)bright_a
              << "] ";

    // Tolerance: lighting noise across re-spawn frames is typically
    // tens of pixels. A correctly-functioning cone should hide the
    // behind target almost entirely — delta near zero. Allow up to
    // 50 pixels of noise; anything more means the behind target's
    // pixels are leaking through to the framebuffer.
    //
    // NOTE: This AT is currently EXPECTED TO FAIL — it's the
    // canonical regression test for the AI-bike-visible-outside-
    // cone bug. As of v0.10 the behind target leaks several
    // hundred bright pixels (delta ≈ size_of(in_front target)),
    // proving the bug is still live. The cone fix turns this
    // green; until then, this is the contract that says "cone
    // must hide behind-viewer dynamic targets".
    long delta = static_cast<long>(bright_b) - static_cast<long>(bright_a);
    ASSERT_TRUE(delta < 50,
        std::string("behind-viewer dynamic target leaked through cone: "
                    "bright-pixel delta=") + std::to_string(delta)
        + " (in-front only=" + std::to_string(bright_a)
        + ", with behind=" + std::to_string(bright_b)
        + "). The cone should hide the behind target; delta should be ~0.");

    engine.shutdown();
}

int main() {
    if (std::getenv("CI")) {
        std::cout << "SKIP all (CI)" << std::endl;
        return 0;
    }
    std::cout << "=== AT — vision cone darkens off-FOV pixels ===" << std::endl;
    TEST(cone_disabled_keeps_clear_color_then_enabled_darkens_it);
    TEST(cone_must_hide_dynamic_target_behind_viewer);
    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
