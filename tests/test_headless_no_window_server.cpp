// Engine acceptance test: a headless Engine must never connect this
// process to the window server.
//
// The bug this exists to prevent: PlatformMacOS's constructor called
// glfwInit() unconditionally, headless included. On Cocoa, glfwInit()
// ends in
//
//     if (![[NSRunningApplication currentApplication] isFinishedLaunching])
//         [NSApp run];
//
// which turns the process into a full NSApplication and blocks until
// the window server answers the launch handshake. That handshake is a
// per-user, session-global resource, serialized across every process
// asking at once: measured 63 ms for one process on an M4 Max and
// 550 ms with 32 asking together. Under `ctest -j 8` some process
// periodically never received applicationDidFinishLaunching: at all
// and sat in mach_msg forever. Every test passed alone; the suite
// deadlocked in parallel, on a different test each time.
//
// So this test asserts the mechanism, not the symptom. A parallel-run
// timing test would be flaky and would only fail after the damage.
// This one is deterministic and fails the moment anything puts the
// window server back on the headless path.
//
// Usage:
//   ./build/test_headless_no_window_server --no-head

#include "application.h"
#include "core/engine.h"
#include "core/particle_system.h"
#include "logosphere/rendering/pixel_buffer.h"
#include "logosphere/rendering/i_renderer.h"
#include "particle.h"
#include "platform/platform_macos.h"

#include <GLFW/glfw3.h>

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

class HeadlessApp : public Logosphere::IApplication {
public:
    bool initialize() override { return true; }
    void shutdown() override {}
    GLFWwindow* get_window() override { return nullptr; }
};

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                                                        \
    std::cout << "  " #name "..." << std::endl;                           \
    try { name(); tests_passed++; std::cout << "  PASS" << std::endl; }   \
    catch (const std::exception& e) {                                     \
        tests_failed++;                                                   \
        std::cout << "  FAIL: " << e.what() << std::endl;                 \
    }

#define ASSERT_TRUE(cond, msg) \
    if (!(cond)) throw std::runtime_error(std::string(msg))

namespace {

// --- Probe B: ask GLFW itself, not our own bookkeeping -------------------
//
// glfwGetPrimaryMonitor() is documented to raise GLFW_NOT_INITIALIZED
// and return NULL when the library is not initialized, and to return a
// real monitor when it is. So the two states are distinguishable by a
// POSITIVE observation either way: an error code arrives, or a monitor
// does. Neither outcome is "nothing happened", which is what keeps
// this from being a test that cannot fail.
int g_last_error = 0;

void record_error(int code, const char* /*desc*/) {
    // Only the code is kept. GLFW's description points into a buffer
    // it reuses, so holding the pointer past the callback prints
    // garbage.
    g_last_error = code;
}

struct GlfwProbe {
    bool  initialized;   // GLFW's own verdict
    int   error_code;    // what the error callback received
    void* monitor;
};

// Engine construction installs PlatformMacOS's own error callback, so
// re-arm ours every time before probing.
GlfwProbe probe_glfw() {
    g_last_error = 0;
    glfwSetErrorCallback(record_error);

    GLFWmonitor* m = glfwGetPrimaryMonitor();

    GlfwProbe p{};
    p.monitor    = static_cast<void*>(m);
    p.error_code = g_last_error;
    p.initialized = (g_last_error != GLFW_NOT_INITIALIZED);
    return p;
}

void report(const std::string& when, const GlfwProbe& p, bool platform_live) {
    std::cout << "    [measure] " << when
              << ": glfw_initialized=" << (p.initialized ? "YES" : "no")
              << " err=0x" << std::hex << p.error_code << std::dec
              << (p.error_code == GLFW_NOT_INITIALIZED
                      ? " (GLFW_NOT_INITIALIZED)" : " (none)")
              << " primary_monitor=" << (p.monitor ? "non-null" : "null")
              << " platform.is_window_system_live="
              << (platform_live ? "YES" : "no") << std::endl;
}

// --- Probe A: the platform's own record ---------------------------------
bool platform_window_system_live(Engine& engine) {
    auto* base = engine.get_platform();
    ASSERT_TRUE(base != nullptr, "engine has no platform system");
    // If this ever stops being PlatformMacOS the whole test is
    // meaningless, so say so loudly rather than passing vacuously.
    ASSERT_TRUE(std::string(base->get_platform_name()) == "macOS",
                std::string("expected the macOS platform, got '")
                    + base->get_platform_name()
                    + "', so this AT only means anything on the full profile");
    return static_cast<Platform::PlatformMacOS*>(base)->is_window_system_live();
}

void init_headless(Engine& engine, int w, int h) {
    EngineConfig config;
    config.create_display = false;
    config.window_width = w;
    config.window_height = h;
    config.window_title = "no-window-server";
    config.show_debug_overlay = false;
    config.show_kg_inspector = false;
    config.enable_chat_window = false;
    if (engine.initialize(config) < 0)
        throw std::runtime_error("Engine::initialize() failed headless");
}

// Do real work, so the test cannot pass just because the engine
// never got off the ground. One particle is enough to make the GPU
// rasterizer take the full g-buffer + deferred path instead of its
// "nothing to draw" early-exit.
void render_some_frames(Engine& engine) {
    Particle quad = {};
    quad.shape = ParticleShape::BOX;
    quad.x = 0; quad.y = 0; quad.z = 0.5f;
    quad.width = 1.0f; quad.height = 1.0f; quad.thickness = 0.5f;
    quad.r = 1.0f; quad.g = 1.0f; quad.b = 1.0f; quad.a = 1.0f;
    quad.SetMaterial(Materials::Type::STONE);
    quad.owner = ParticleOwner::STATIC;
    quad.is_at_rest = true;
    engine.get_particle_system().add_particle_to_entity(quad, nullptr, 0);

    auto& renderer = engine.get_renderer();
    for (int i = 0; i < 5; ++i) {
        engine.update(1.0f / 60.0f);
        engine.render();
        renderer.wait_for_completion();
    }

    const int w = engine.get_render_buffer().width();
    const int h = engine.get_render_buffer().height();
    std::vector<uint32_t> pixels(static_cast<size_t>(w) * static_cast<size_t>(h));
    int got_w = 0, got_h = 0;
    bool ok = engine.read_latest_framebuffer(pixels.data(), got_w, got_h);
    ASSERT_TRUE(ok, "read_latest_framebuffer failed, so the engine did no GPU "
                    "work, so the window-server assertion would be vacuous");
    ASSERT_TRUE(got_w == w && got_h == h, "framebuffer read-back size mismatch");
    std::cout << "    [measure] rendered and read back " << got_w << "x" << got_h
              << " pixels headless" << std::endl;
}

}  // namespace

// =========================================================================
// A headless Engine renders through the real GPU pipeline without ever
// initializing GLFW.
// =========================================================================
void headless_engine_never_starts_the_window_system() {
    GlfwProbe before = probe_glfw();
    report("before any Engine", before, false);
    // Baseline. If this is already YES something outside the engine
    // brought GLFW up and the rest of the test proves nothing.
    ASSERT_TRUE(!before.initialized,
                "GLFW was already initialized before any Engine existed");
    ASSERT_TRUE(before.error_code == GLFW_NOT_INITIALIZED,
                "probe is broken: expected GLFW_NOT_INITIALIZED from "
                "glfwGetPrimaryMonitor() on an uninitialized GLFW");

    HeadlessApp app;
    Engine engine(&app);
    init_headless(engine, 200, 200);
    render_some_frames(engine);

    bool live = platform_window_system_live(engine);
    GlfwProbe after = probe_glfw();
    report("after a headless Engine rendered", after, live);

    ASSERT_TRUE(!live,
        "PlatformMacOS reports the window system is live after a headless "
        "run. Something called glfwInit() off the create_display=false path; "
        "that makes this process an NSApplication and makes the suite "
        "deadlock-prone under ctest -j.");
    ASSERT_TRUE(!after.initialized,
        "GLFW is initialized after a headless Engine ran. glfwInit() is "
        "back on the headless path. See the header comment.");
    ASSERT_TRUE(after.monitor == nullptr,
        "glfwGetPrimaryMonitor() returned a monitor, so GLFW is up");
}

// =========================================================================
// The wedge was usually on the SECOND Engine in a process (an AT with
// more than one case), so hold the invariant across engine lifecycles.
// =========================================================================
void the_window_system_stays_down_across_engine_lifecycles() {
    for (int i = 1; i <= 3; ++i) {
        HeadlessApp app;
        Engine engine(&app);
        init_headless(engine, 160, 160);
        engine.update(1.0f / 60.0f);
        engine.render();
        engine.get_renderer().wait_for_completion();

        bool live = platform_window_system_live(engine);
        GlfwProbe p = probe_glfw();
        report("engine #" + std::to_string(i), p, live);

        ASSERT_TRUE(!live, "engine #" + std::to_string(i)
                    + " brought the window system up");
        ASSERT_TRUE(!p.initialized, "GLFW initialized by engine #"
                    + std::to_string(i));
        engine.shutdown();
    }
}

int main() {
    std::cout << "Headless engines do not touch the window server" << std::endl;
    TEST(headless_engine_never_starts_the_window_system);
    TEST(the_window_system_stays_down_across_engine_lifecycles);
    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
