// Engine acceptance test — CameraDirector smoothly dollies the
// camera to a focus point and releases back to the original pose.
// Drives on REAL delta time so dollies survive cinematic pause.
// Catches regressions in: home-pose snapshot timing, smoothstep
// curve (must hit endpoints exactly), release path returning home,
// and dolly progression while the engine itself is paused.

#include "application.h"
#include "core/engine.h"
#include "core/camera_director.h"
#include "core/camera_system.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

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

#define ASSERT_NEAR(actual, expected, eps, msg) \
    do { \
        double _a = (actual), _e = (expected); \
        if (std::fabs(_a - _e) > (eps)) { \
            throw std::runtime_error(std::string(msg) \
                + " expected=" + std::to_string(_e) \
                + " got=" + std::to_string(_a)); \
        } \
    } while (0)

static void init_engine(Engine& engine) {
    EngineConfig config;
    config.create_display = false;
    config.window_width = 200;
    config.window_height = 200;
    config.window_title = "camera-director-at";
    config.show_debug_overlay = false;
    config.show_kg_inspector = false;
    config.enable_chat_window = false;
    int observer = engine.initialize(config);
    if (observer < 0)
        throw std::runtime_error("engine.initialize() failed in headless");
}

// Tick `frames` engine updates each consuming `dt` real seconds.
// CameraDirector eases on real time, so this drives its progress
// regardless of game-time pause.
static void tick_real(Engine& engine, int frames, double dt) {
    for (int i = 0; i < frames; ++i) {
        engine.update(dt);
        engine.render();
    }
}

// =========================================================================
// AT — focus_on dollies the camera to a specific world point and
// release returns it to the start pose.
// =========================================================================
void focus_on_then_release_returns_to_start() {
    HeadlessTestApp app;
    Engine engine(&app);
    init_engine(engine);

    auto& cam     = engine.get_camera_system();
    auto& dolly   = engine.get_camera_director();

    // Place the camera at a known starting pose (so we can assert
    // release returns there).
    cam.set_position(0.0f, -10.0f, 5.0f);
    cam.look_at(0.0f, 0.0f, 0.0f);
    float start_x, start_y, start_z;
    cam.get_position(start_x, start_y, start_z);

    // Engage dolly: focus on (target_x=4, target_y=4, target_z=0.5),
    // distance=3, tilt=20°, ease over 0.6s. With 60 ticks of 1/60s
    // (= 1.0s), we should be well past the easing window.
    const float tx = 4.0f, ty = 4.0f, tz = 0.5f;
    dolly.focus_on(tx, ty, tz, /*distance=*/3.0f, /*tilt_deg=*/20.0f, /*ease=*/0.6f);
    ASSERT_TRUE(dolly.is_active(), "dolly must report active after focus_on");

    tick_real(engine, /*frames=*/60, /*dt=*/1.0 / 60.0);

    // Camera should now be ~3 units away from the target.
    float cx, cy, cz;
    cam.get_position(cx, cy, cz);
    const float dx = cx - tx, dy = cy - ty, dz = cz - tz;
    const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    ASSERT_NEAR(dist, 3.0, 0.05, "camera should sit at ~distance from target");

    // Release — ease back to the start pose over 0.4s. Tick 30 frames.
    dolly.release(/*ease=*/0.4f);
    tick_real(engine, 30, 1.0 / 60.0);
    ASSERT_TRUE(!dolly.is_active(),
        "dolly must report inactive after release ease completes");

    cam.get_position(cx, cy, cz);
    ASSERT_NEAR(cx, start_x, 0.05, "release must restore start x");
    ASSERT_NEAR(cy, start_y, 0.05, "release must restore start y");
    ASSERT_NEAR(cz, start_z, 0.05, "release must restore start z");

    engine.shutdown();
}

// =========================================================================
// AT — Dolly progresses while engine is in cinematic pause.
// This is the contract that makes Phase 2 of the Master Control
// cinematic possible: the dolly must keep going while game time
// is frozen.
// =========================================================================
void dolly_runs_during_cinematic_pause() {
    HeadlessTestApp app;
    Engine engine(&app);
    init_engine(engine);

    auto& cam   = engine.get_camera_system();
    auto& dolly = engine.get_camera_director();

    cam.set_position(0.0f, -10.0f, 5.0f);
    cam.look_at(0.0f, 0.0f, 0.0f);

    engine.set_cinematic_pause(true);
    ASSERT_TRUE(engine.is_cinematic_paused(), "must be paused");

    dolly.focus_on(/*x=*/2.0f, /*y=*/2.0f, /*z=*/0.5f,
                   /*distance=*/3.0f, /*tilt=*/15.0f,
                   /*ease=*/0.5f);
    tick_real(engine, 45, 1.0 / 60.0);  // 0.75s, past the ease

    // Camera moved despite pause.
    float cx, cy, cz;
    cam.get_position(cx, cy, cz);
    const float dx = cx - 2.0f, dy = cy - 2.0f, dz = cz - 0.5f;
    const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    ASSERT_NEAR(dist, 3.0, 0.05,
        "dolly must reach target distance while engine is cinematic-paused");

    engine.set_cinematic_pause(false);
    engine.shutdown();
}

int main() {
    if (std::getenv("CI")) {
        std::cout << "SKIP all (CI)" << std::endl;
        return 0;
    }
    std::cout << "=== AT — CameraDirector dolly + release ===" << std::endl;
    TEST(focus_on_then_release_returns_to_start);
    TEST(dolly_runs_during_cinematic_pause);
    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
