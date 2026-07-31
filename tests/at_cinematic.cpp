// Engine acceptance test — Logotron's Master Control cinematic
// (director/cinematic.h). Composes Engine::set_cinematic_pause +
// CameraDirector + hide_assembly + program_actor into the show
// that wraps each Director firing.
//
// Catches regressions where:
//   * begin() forgets to pause game time (gameplay keeps running
//     during the cinematic, the LLM's thinking time becomes a
//     visible stutter)
//   * begin() forgets to hide the bike (bike + Program both visible)
//   * begin() fails to spawn the Program (cinematic with nothing
//     to look at)
//   * end() leaks the Program (extra particles each round → arena
//     fills up with humanoids)
//   * end() forgets to restore the bike (player permanently invisible)
//   * end() forgets to resume time (game freezes after the cinematic)

#include "application.h"
#include "core/engine.h"
#include "core/particle_system.h"
#include "core/time_system.h"
#include "logosphere/kg/kg_module.h"
#include "particle.h"

#include "../examples/logotron/src/director/cinematic.h"

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
    config.window_title = "cinematic-at";
    config.show_debug_overlay = false;
    config.show_kg_inspector = false;
    config.enable_chat_window = false;
    int observer = engine.initialize(config);
    if (observer < 0)
        throw std::runtime_error("engine.initialize() failed in headless");
}

// Minimal stand-in for the player bike: a Wall entity carrying
// three particles. The cinematic doesn't care that it's a bike —
// it just hides whatever entity you point it at.
static kg::EntityID spawn_stand_in_bike(Engine& engine,
                                        std::vector<int>& out_indices) {
    auto& kg = engine.get_kg();
    kg::EntityID e = kg.createEntity("Wall");
    auto& ps = engine.get_particle_system();
    for (int i = 0; i < 3; ++i) {
        Particle p = {};
        p.shape = ParticleShape::BOX;
        p.x = float(i) * 0.3f;
        p.y = 0.0f;
        p.z = 0.5f;
        p.width = 0.4f; p.height = 0.4f; p.thickness = 0.4f;
        p.r = 1.0f; p.g = 1.0f; p.b = 1.0f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        p.owner = ParticleOwner::STATIC;
        p.is_at_rest = true;
        p.entity_id = e;
        out_indices.push_back(ps.add_particle_to_entity(p, &kg, e));
    }
    return e;
}

// =========================================================================
// AT — begin pauses + hides + spawns Program; end restores all of it.
// =========================================================================
void begin_pauses_hides_spawns_end_restores() {
    HeadlessTestApp app;
    Engine engine(&app);
    init_engine(engine);

    auto& kg = engine.get_kg();
    auto& ps = engine.get_particle_system();
    auto& ts = engine.get_time_system();

    std::vector<int> bike_idx;
    kg::EntityID bike_entity = spawn_stand_in_bike(engine, bike_idx);
    const size_t pre_count = ps.count();

    // Sanity: not paused; bike particles visible.
    ASSERT_TRUE(!engine.is_cinematic_paused(),
        "must not be paused at start");
    {
        auto view = ps.lock_particles_for_read();
        for (int idx : bike_idx) {
            ASSERT_TRUE(view[idx].a == 1.0f,
                "bike particles must start at alpha=1");
        }
    }

    // ---- begin ----
    logotron::director::CinematicInputs cin;
    cin.player_bike_entity = bike_entity;
    cin.center_x = 0.5f;
    cin.center_y = 0.0f;
    cin.center_z = 0.5f;
    logotron::director::CinematicState state;
    logotron::director::begin(engine, kg, cin, state);

    ASSERT_TRUE(state.active, "state must be active after begin");
    ASSERT_TRUE(engine.is_cinematic_paused(),
        "must be paused after begin");

    // Bike particles hidden.
    {
        auto view = ps.lock_particles_for_read();
        for (int idx : bike_idx) {
            ASSERT_TRUE(view[idx].a == 0.0f,
                "bike particles must be alpha=0 after begin");
        }
    }

    // Program spawned: extra particles in the system.
    const size_t after_begin = ps.count();
    std::cout << "[program added=" << (after_begin - pre_count) << "] ";
    ASSERT_TRUE(after_begin > pre_count + 10,
        "Program rig must add particles");

    // Tick a few real-time frames; game time should not advance.
    const double game_t_before = ts.get_total_game_time();
    for (int i = 0; i < 5; ++i) {
        engine.update(1.0 / 60.0);
        engine.render();
    }
    ASSERT_TRUE(ts.get_total_game_time() == game_t_before,
        "game time must not advance during cinematic");

    // ---- end ----
    logotron::director::end(engine, state);

    ASSERT_TRUE(!state.active, "state must be inactive after end");
    ASSERT_TRUE(!engine.is_cinematic_paused(),
        "must not be paused after end");

    // Bike alpha restored.
    {
        auto view = ps.lock_particles_for_read();
        for (int idx : bike_idx) {
            ASSERT_TRUE(view[idx].a == 1.0f,
                "bike particles must be restored to alpha=1 after end");
        }
    }

    // Program despawned: particle count back to baseline (give a
    // small slack for any engine bookkeeping particles spawned
    // during init — bike count is the floor we care about).
    const size_t after_end = ps.count();
    std::cout << "[end excess=" << (after_end - pre_count) << "] ";
    ASSERT_TRUE(after_end <= pre_count + 5,
        std::string("end must wipe Program particles; ")
        + std::to_string(after_end - pre_count) + " excess remain");

    // Tick again; game time should advance now.
    const double game_t_after_end = ts.get_total_game_time();
    engine.update(1.0 / 60.0);
    engine.render();
    ASSERT_TRUE(ts.get_total_game_time() > game_t_after_end,
        "game time must resume advancing after end");

    engine.shutdown();
}

// =========================================================================
// AT — begin twice in a row is a no-op (idempotent). End once
// fully restores. Catches a regression where double-begin would
// leave a dangling Program rig that end can't reach.
// =========================================================================
void begin_is_idempotent_end_restores_once() {
    HeadlessTestApp app;
    Engine engine(&app);
    init_engine(engine);

    auto& kg = engine.get_kg();
    auto& ps = engine.get_particle_system();
    std::vector<int> bike_idx;
    kg::EntityID bike_entity = spawn_stand_in_bike(engine, bike_idx);
    const size_t pre_count = ps.count();

    logotron::director::CinematicInputs cin;
    cin.player_bike_entity = bike_entity;
    cin.center_x = 0.5f; cin.center_y = 0.0f; cin.center_z = 0.5f;
    logotron::director::CinematicState state;
    logotron::director::begin(engine, kg, cin, state);
    const size_t after_first_begin = ps.count();

    logotron::director::begin(engine, kg, cin, state);  // no-op
    ASSERT_TRUE(ps.count() == after_first_begin,
        "second begin must not spawn a second Program");

    logotron::director::end(engine, state);
    ASSERT_TRUE(ps.count() <= pre_count + 5,
        "end must wipe the single Program rig");

    engine.shutdown();
}

int main() {
    if (std::getenv("CI")) {
        std::cout << "SKIP all (CI)" << std::endl;
        return 0;
    }
    std::cout << "=== AT — Master Control cinematic begin/end ===" << std::endl;
    TEST(begin_pauses_hides_spawns_end_restores);
    TEST(begin_is_idempotent_end_restores_once);
    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
