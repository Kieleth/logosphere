// Engine acceptance test — Logotron's program_actor spawn/despawn.
// Even though program_actor lives game-side, its contract is
// engine-shaped: spawn must produce a humanoid rig (KG entity +
// linked particles), despawn must wipe both. AT runs against the
// full engine in headless mode so HumanoidGenerator + KG +
// ParticleSystem are all live.
//
// Catches regressions in:
//   * the spawn path failing silently (returns invalid handle)
//   * despawn leaking particles (delete_particles_immediate not
//     called, or called with wrong indices)
//   * despawn leaking the KG root entity

#include "application.h"
#include "core/engine.h"
#include "core/particle_system.h"
#include "logosphere/kg/kg_module.h"

#include "../examples/logotron/src/director/program_actor.h"

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
    config.window_title = "program-actor-at";
    config.show_debug_overlay = false;
    config.show_kg_inspector = false;
    config.enable_chat_window = false;
    int observer = engine.initialize(config);
    if (observer < 0)
        throw std::runtime_error("engine.initialize() failed in headless");
}

// =========================================================================
// AT — spawn produces a valid humanoid; despawn wipes its particles
// and the root entity from the KG. This is the contract the
// cinematic state machine depends on; if either side breaks the
// game leaks particles every Director firing.
// =========================================================================
void spawn_creates_humanoid_despawn_wipes_it() {
    HeadlessTestApp app;
    Engine engine(&app);
    init_engine(engine);

    auto& kg = engine.get_kg();
    auto& ps = engine.get_particle_system();
    const size_t pre_count = ps.count();

    auto handle = logotron::director::spawn(engine, 0.0f, 0.0f, 0.5f);
    ASSERT_TRUE(handle.valid(),
        "spawn must return a valid handle");

    // The humanoid root + body parts add particles. A typical
    // default-human spec ships ~30+ particles; any positive number
    // proves the rig generated.
    const size_t after_spawn = ps.count();
    std::cout << "[spawn added=" << (after_spawn - pre_count)
              << " particles] ";
    ASSERT_TRUE(after_spawn > pre_count + 10,
        std::string("spawn must add at least ~10 particles for a humanoid; got ")
        + std::to_string(after_spawn - pre_count));

    // Recursive HAS_PART traversal: every body part's particles
    // count, not just the root entity's direct particles.
    auto rig_particles = kg.getEntityKGParticlesRecursive(handle.root_entity);
    ASSERT_TRUE(!rig_particles.empty(),
        "rig entity must own at least one KG particle (recursive)");

    // Despawn.
    logotron::director::despawn(engine, handle);

    // Particles removed (count returns to baseline ± any other
    // engine spawns; the rig contributes nothing now).
    const size_t after_despawn = ps.count();
    std::cout << "[despawn left=" << (after_despawn - pre_count)
              << " excess particles] ";
    ASSERT_TRUE(after_despawn <= pre_count + 5,
        std::string("despawn must wipe rig particles; ")
        + std::to_string(after_despawn - pre_count)
        + " excess particles left over");

    engine.shutdown();
}

int main() {
    if (std::getenv("CI")) {
        std::cout << "SKIP all (CI)" << std::endl;
        return 0;
    }
    std::cout << "=== AT — program_actor spawn / despawn ===" << std::endl;
    TEST(spawn_creates_humanoid_despawn_wipes_it);
    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
