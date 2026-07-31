// Engine acceptance test — Engine::set_cinematic_pause freezes
// game time but keeps real time advancing. The cinematic pause is
// the engine-side hook every cinematic state machine builds on
// (camera dollies, particle effects, narrative beats). Catches
// regressions where TimeSystem stops advancing real time too, or
// where the pause flag and TimeSystem fall out of sync.

#include "application.h"
#include "core/engine.h"
#include "core/time_system.h"

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

static void init_engine(Engine& engine) {
    EngineConfig config;
    config.create_display = false;
    config.window_width = 200;
    config.window_height = 200;
    config.window_title = "cinematic-pause-at";
    config.show_debug_overlay = false;
    config.show_kg_inspector = false;
    config.enable_chat_window = false;
    int observer = engine.initialize(config);
    if (observer < 0)
        throw std::runtime_error("engine.initialize() failed in headless");
}

static void tick(Engine& engine, double dt) {
    engine.update(dt);
    engine.render();
}

// =========================================================================
// AT — cinematic pause stops game time but real time keeps flowing.
// =========================================================================
void cinematic_pause_freezes_game_time_only() {
    HeadlessTestApp app;
    Engine engine(&app);
    init_engine(engine);

    auto& ts = engine.get_time_system();

    // Baseline: tick a frame, both clocks advance.
    const double pre_game_t = ts.get_total_game_time();
    const double pre_real_t = ts.get_total_real_time();
    tick(engine, 0.1);
    ASSERT_TRUE(ts.get_total_game_time() > pre_game_t,
        "game time must advance before pause");
    ASSERT_TRUE(ts.get_total_real_time() > pre_real_t,
        "real time must advance before pause");

    // Engage cinematic pause.
    ASSERT_TRUE(!engine.is_cinematic_paused(),
        "should not be paused at start");
    engine.set_cinematic_pause(true);
    ASSERT_TRUE(engine.is_cinematic_paused(),
        "pause flag must be set");

    const double paused_game_t = ts.get_total_game_time();
    const double paused_real_t = ts.get_total_real_time();

    // Tick several frames while paused.
    for (int i = 0; i < 5; ++i) tick(engine, 0.1);

    // Game time stays put; real time keeps moving.
    ASSERT_TRUE(ts.get_total_game_time() == paused_game_t,
        std::string("game time advanced during cinematic pause: was ")
        + std::to_string(paused_game_t) + " now "
        + std::to_string(ts.get_total_game_time()));
    ASSERT_TRUE(ts.get_total_real_time() > paused_real_t,
        "real time must keep advancing during cinematic pause");

    // Resume restores game-time advancement.
    engine.set_cinematic_pause(false);
    ASSERT_TRUE(!engine.is_cinematic_paused(),
        "pause flag must clear on resume");
    const double resumed_game_t = ts.get_total_game_time();
    tick(engine, 0.1);
    ASSERT_TRUE(ts.get_total_game_time() > resumed_game_t,
        "game time must advance again after resume");

    engine.shutdown();
}

// =========================================================================
// AT — set_cinematic_pause(true) twice in a row is a no-op (idempotent).
// Catches regressions where double-pause confuses TimeSystem's saved
// scale and resume restores 0 instead of 1.
// =========================================================================
void cinematic_pause_is_idempotent() {
    HeadlessTestApp app;
    Engine engine(&app);
    init_engine(engine);

    auto& ts = engine.get_time_system();
    tick(engine, 0.1);  // Get past startup transients.

    engine.set_cinematic_pause(true);
    engine.set_cinematic_pause(true);  // Second call must be no-op.
    engine.set_cinematic_pause(false);

    const float scale_after = ts.get_time_scale();
    ASSERT_TRUE(scale_after == 1.0f,
        std::string("time_scale must return to 1.0 after pause/pause/resume; got ")
        + std::to_string(scale_after));

    engine.shutdown();
}

int main() {
    if (std::getenv("CI")) {
        std::cout << "SKIP all (CI)" << std::endl;
        return 0;
    }
    std::cout << "=== AT — Engine::set_cinematic_pause ===" << std::endl;
    TEST(cinematic_pause_freezes_game_time_only);
    TEST(cinematic_pause_is_idempotent);
    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
