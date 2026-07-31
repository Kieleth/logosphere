// Engine acceptance test: prove that an Engine constructed with
// create_display=false can still run the full GPU rendering pipeline
// and that the CPU can read the resulting pixels back via
// Engine::read_latest_framebuffer, with no window. This is the engine-level capability every game needs
// to write GPU-touching acceptance tests (vision-cone, lighting,
// post-process effects) without iterating in the live game.
//
// Scope today (Option A, surgical): just proves the read-back API
// returns real GPU-written pixels. Content-bearing ATs that light
// scenes and reproduce visible bugs come on top of this API.
// Option C (proper Renderer / Display split) will let us run many
// of these ATs in the same process. For now, keep to ONE AT per
// executable to avoid per-process Metal teardown issues.

#include "application.h"
#include "core/engine.h"
#include "core/particle_system.h"
#include "logosphere/rendering/pixel_buffer.h"
#include "particle.h"
#include "logosphere/rendering/render_pipeline.h"

#include <algorithm>
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

static void init_engine(Engine& engine, int w, int h) {
    EngineConfig config;
    config.create_display = false;
    config.window_width = w;
    config.window_height = h;
    config.window_title = "headless-test";
    config.show_debug_overlay = false;
    config.show_kg_inspector = false;
    config.enable_chat_window = false;
    int observer = engine.initialize(config);
    if (observer < 0)
        throw std::runtime_error("engine.initialize() failed in Headless mode");
}

// Drive enough frames for the GPU async pipeline to fully cycle.
// The rasterizer's slot pool needs a couple of ticks to land a
// completed framebuffer the test can read. wait_for_completion
// blocks until Metal drains all in-flight work.
static void tick_and_sync(Engine& engine, int frames) {
    auto& renderer = engine.get_renderer();
    for (int i = 0; i < frames; ++i) {
        engine.update(1.0f / 60.0f);
        engine.render();
        renderer.wait_for_completion();
    }
}

// =========================================================================
// AT — capability: Engine::read_latest_framebuffer returns real
// GPU-written pixels in Headless mode. Proves the plumbing from
// GPU-side MTLBuffer to caller-owned std::vector works end-to-end,
// independent of scene content. With an empty scene the GPU still
// runs the clear + full deferred path; we expect every returned
// pixel to equal the configured clear color (10, 10, 15) BGRA.
// =========================================================================
void engine_headless_reads_gpu_framebuffer() {
    HeadlessTestApp app;
    Engine engine(&app);
    init_engine(engine, 200, 200);

    // A single particle is needed to kick the GPU rasterizer out
    // of its "nothing to draw, skip dispatch" early-exit. With at
    // least one triangle Pass 1 (g-buffer) and Pass 3 (deferred
    // lighting + clear) allocate + populate the async framebuffer
    // slots. Shade will likely be near-black (no lights in test),
    // so the test asserts on the cleared backdrop, not the quad.
    Particle quad = {};
    quad.shape = ParticleShape::BOX;
    quad.x = 0; quad.y = 0; quad.z = 0.5f;
    quad.width = 1.0f; quad.height = 1.0f; quad.thickness = 0.5f;
    quad.r = 1.0f; quad.g = 1.0f; quad.b = 1.0f; quad.a = 1.0f;
    quad.SetMaterial(Materials::Type::STONE);
    quad.owner = ParticleOwner::STATIC;
    quad.is_at_rest = true;
    engine.get_particle_system().add_particle_to_entity(quad, nullptr, 0);

    tick_and_sync(engine, 5);

    // Render target: RenderSystem sets a 1600×1200 internal render
    // resolution regardless of window dims in headless mode. Size
    // the caller buffer to match so memcpy lands entirely in it.
    const int expected_w = engine.get_render_buffer().width();
    const int expected_h = engine.get_render_buffer().height();
    std::vector<uint32_t> pixels(
        static_cast<size_t>(expected_w) * static_cast<size_t>(expected_h));

    int w = 0, h = 0;
    bool ok = engine.read_latest_framebuffer(pixels.data(), w, h);
    ASSERT_TRUE(ok, "read_latest_framebuffer must return true in headless");
    ASSERT_TRUE(w == expected_w,
        std::string("width mismatch: got=") + std::to_string(w)
        + " expected=" + std::to_string(expected_w));
    ASSERT_TRUE(h == expected_h,
        std::string("height mismatch: got=") + std::to_string(h)
        + " expected=" + std::to_string(expected_h));

    // Clear color is RGBA(10,10,15,255). In BGRA uint32_t this is
    // 0xff0a0a0f (alpha in top byte, then R, G, B). The quad may
    // render as near-black (no lights), but the vast majority of
    // pixels is the cleared backdrop. Require >99% of pixels to
    // match — if read_latest_framebuffer returned uninitialized
    // memory or the wrong buffer, we'd see much less.
    constexpr uint32_t kClearBGRA = 0xff0a0a0fu;
    size_t clear_count = 0;
    for (uint32_t p : pixels) if (p == kClearBGRA) ++clear_count;
    const double clear_ratio = double(clear_count) / double(pixels.size());
    std::cout << "[fb=" << w << "x" << h
              << " clear_pixels=" << clear_count
              << "/" << pixels.size() << "] ";
    ASSERT_TRUE(clear_ratio > 0.99,
        std::string("expected >99% clear-color pixels; got ratio=")
        + std::to_string(clear_ratio));

    engine.shutdown();
}

// Phase 4' probe: two Engines in the same process. Before the
// ownership inversion this fails with read_latest_framebuffer
// returning false on the 2nd engine. Kept here as the TDD red
// to find and fix the leak.
void engine_second_instance_in_same_process_works() {
    HeadlessTestApp app;
    Engine engine(&app);
    init_engine(engine, 200, 200);

    Particle quad = {};
    quad.shape = ParticleShape::BOX;
    quad.x = 0; quad.y = 0; quad.z = 0.5f;
    quad.width = 1.0f; quad.height = 1.0f; quad.thickness = 0.5f;
    quad.r = 1.0f; quad.g = 1.0f; quad.b = 1.0f; quad.a = 1.0f;
    quad.SetMaterial(Materials::Type::STONE);
    quad.owner = ParticleOwner::STATIC;
    quad.is_at_rest = true;
    engine.get_particle_system().add_particle_to_entity(quad, nullptr, 0);
    tick_and_sync(engine, 5);

    const int expected_w = engine.get_render_buffer().width();
    const int expected_h = engine.get_render_buffer().height();
    std::vector<uint32_t> pixels(
        static_cast<size_t>(expected_w) * static_cast<size_t>(expected_h));

    int w = 0, h = 0;
    bool ok = engine.read_latest_framebuffer(pixels.data(), w, h);
    ASSERT_TRUE(ok, "2nd-engine read_latest_framebuffer must return true");
    engine.shutdown();
}

int main() {
    if (std::getenv("CI")) {
        std::cout << "SKIP all (CI)" << std::endl;
        return 0;
    }
    std::cout << "=== Engine Headless Render — capability AT ===" << std::endl;
    TEST(engine_headless_reads_gpu_framebuffer);
    TEST(engine_second_instance_in_same_process_works);
    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
