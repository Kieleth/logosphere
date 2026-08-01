// Ontology levers: can an agent change the world through the KG?
//
// The knowledge graph is the only surface an agent can reach. Before
// this, setting a property wrote a record and nothing else: the world
// carried on unchanged until something happened to re-read it at
// materialisation. These tests hold the other end of that promise -
// a lever set in the graph must land on the bodies that carry it.
//
// The headline case is the one that already cost a bug. A planet was
// held up by is_at_rest, which is a solver optimisation, and a walking
// human shattered it. `solver_authority` is the honest lever for that,
// and is_at_rest deliberately has none.
//
// Usage:
//   ./build/test_ontology_levers

#include "../src/core/engine.h"
#include "../src/core/particle_system.h"
#include "logosphere/kg/entity_physical_state.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/events/event_bus.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg)                                                \
    do {                                                                \
        if (cond) { tests_passed++; }                                   \
        else { tests_failed++;                                          \
               std::cout << "FAIL: " << msg << std::endl; }             \
    } while (0)

namespace {

struct Harness {
    Engine engine;
    Harness() : engine(nullptr) {
        EngineConfig config;
        config.create_display = false;
        config.window_width = 800;
        config.window_height = 600;
        config.window_title = "levers";
        config.show_debug_overlay = false;
        config.enable_chat_window = false;
        if (engine.initialize(config) < 0)
            throw std::runtime_error("Engine::initialize() failed headless");
    }
    ~Harness() { engine.shutdown(); }
};

// A small body of loose stones, the shape of anything an agent might
// wish into being and then want to pin down.
kg::EntityID spawn_body(Engine& e, kg::KGModule& kg, float x, int count) {
    auto ent = kg.createEntity("Rock");
    kg.setProperty(ent, "x", std::to_string(x));
    kg.setProperty(ent, "y", "0");
    for (int i = 0; i < count; ++i) {
        Particle p{};
        p.shape = ParticleShape::BOX;
        p.x = x; p.y = 0.0f; p.z = 4.0f + 0.5f * i;
        p.width = p.height = p.thickness = p.size = 0.4f;
        p.r = 0.6f; p.g = 0.5f; p.b = 0.4f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        e.get_particle_system().add_particle_to_entity(p, &kg, ent);
    }
    return ent;
}

int count_mode(Engine& e, kg::KGModule& kg, kg::EntityID ent,
               ParticleSolverMode mode) {
    int n = 0;
    auto ids = kg.getEntityKGParticlesRecursive(ent, "HAS_PART");
    auto view = e.get_particle_system().lock_particles_for_read();
    for (auto pid : ids) {
        auto idx = kg.getRenderIndex(pid);
        if (idx == kg::INVALID_RENDER_INDEX || idx >= view.size()) continue;
        if (view[idx].solver_mode == mode) n++;
    }
    return n;
}

float lowest_z(Engine& e, kg::KGModule& kg, kg::EntityID ent) {
    float lo = 1e9f;
    auto ids = kg.getEntityKGParticlesRecursive(ent, "HAS_PART");
    auto view = e.get_particle_system().lock_particles_for_read();
    for (auto pid : ids) {
        auto idx = kg.getRenderIndex(pid);
        if (idx == kg::INVALID_RENDER_INDEX || idx >= view.size()) continue;
        lo = std::min(lo, view[idx].z);
    }
    return lo;
}

// Immobility must be reachable from the graph, and must actually hold
// against gravity - the thing is_at_rest could not do.
void test_solver_authority_is_reachable_and_real() {
    Harness h;
    auto& kg = h.engine.get_kg();
    logosphere::EntityPhysicalState levers;
    levers.initialize(&h.engine.get_particle_system(),
                      &h.engine.get_physics_system(), &kg);

    auto floating = spawn_body(h.engine, kg, 0.0f, 4);
    auto falling  = spawn_body(h.engine, kg, 6.0f, 4);

    // One body is pinned through the ontology; the other is not.
    kg.setProperty(floating, "solver_authority", "KINEMATIC");
    int touched = levers.apply(floating);
    CHECK(touched == 4,
          "the lever reached every particle of the body (touched " +
          std::to_string(touched) + " of 4)");
    CHECK(count_mode(h.engine, kg, floating,
                     ParticleSolverMode::KINEMATIC) == 4,
          "all four became KINEMATIC");

    float pinned_before = lowest_z(h.engine, kg, floating);
    float loose_before  = lowest_z(h.engine, kg, falling);
    for (int i = 0; i < 180; ++i) h.engine.update(1.0 / 60.0);
    float pinned_after = lowest_z(h.engine, kg, floating);
    float loose_after  = lowest_z(h.engine, kg, falling);

    std::cout << "  [measure] pinned body " << pinned_before << " -> "
              << pinned_after << " m, loose body " << loose_before
              << " -> " << loose_after << " m (3 s of gravity)"
              << std::endl;
    CHECK(std::fabs(pinned_after - pinned_before) < 1e-4f,
          "the pinned body did not move at all (drifted " +
          std::to_string(pinned_after - pinned_before) + " m)");
    CHECK(loose_after < loose_before - 0.5f,
          "the control body fell, so gravity was genuinely running (" +
          std::to_string(loose_before - loose_after) + " m)");
}

// A lever is not a one-way door: an agent can let go again.
void test_authority_can_be_given_back() {
    Harness h;
    auto& kg = h.engine.get_kg();
    logosphere::EntityPhysicalState levers;
    levers.initialize(&h.engine.get_particle_system(),
                      &h.engine.get_physics_system(), &kg);

    auto body = spawn_body(h.engine, kg, 0.0f, 3);
    kg.setProperty(body, "solver_authority", "KINEMATIC");
    levers.apply(body);
    for (int i = 0; i < 120; ++i) h.engine.update(1.0 / 60.0);
    float held = lowest_z(h.engine, kg, body);

    kg.setProperty(body, "solver_authority", "DYNAMIC");
    levers.apply(body);
    CHECK(count_mode(h.engine, kg, body,
                     ParticleSolverMode::DYNAMIC) == 3,
          "the body became DYNAMIC again");
    for (int i = 0; i < 180; ++i) h.engine.update(1.0 / 60.0);
    float released = lowest_z(h.engine, kg, body);
    std::cout << "  [measure] released body " << held << " -> "
              << released << " m" << std::endl;
    CHECK(released < held - 0.5f,
          "and fell once released (moved " +
          std::to_string(held - released) + " m) — a released body "
          "must not stay asleep in the air");
}

// The live promise: set_property ALONE changes the world, with no
// caller re-reading anything. This is what turns the graph from a
// record into a control surface.
void test_setting_a_property_changes_the_world_immediately() {
    Harness h;
    auto& kg = h.engine.get_kg();
    // Deliberately NOT wiring anything here: Engine arms the levers
    // during initialize(), so every game gets this without asking.
    auto body = spawn_body(h.engine, kg, 0.0f, 3);
    CHECK(count_mode(h.engine, kg, body,
                     ParticleSolverMode::KINEMATIC) == 0,
          "the body starts dynamic");

    // No apply() call anywhere: only the graph is touched.
    kg.setProperty(body, "solver_authority", "KINEMATIC");

    CHECK(count_mode(h.engine, kg, body,
                     ParticleSolverMode::KINEMATIC) == 3,
          "setting the property alone pinned the body — the graph is a "
          "control surface, not a record");

    float before = lowest_z(h.engine, kg, body);
    for (int i = 0; i < 180; ++i) h.engine.update(1.0 / 60.0);
    CHECK(std::fabs(lowest_z(h.engine, kg, body) - before) < 1e-4f,
          "and it held through 3 s of gravity");
}

// is_at_rest must stay unreachable: it is an optimisation, and a
// world held up by it collapses when touched. If a lever for it ever
// appears, this test should be what argues against it.
void test_rest_is_not_offered_as_a_lever() {
    CHECK(!logosphere::EntityPhysicalState::is_lever("is_at_rest"),
          "is_at_rest is not a lever");
    CHECK(!logosphere::EntityPhysicalState::is_lever("frames_at_rest"),
          "nor is frames_at_rest");
    CHECK(logosphere::EntityPhysicalState::is_lever("solver_authority"),
          "solver_authority is the lever for immobility");
}

}  // namespace

int main() {
    std::cout << "Ontology levers (KG -> bodies)" << std::endl;
    test_solver_authority_is_reachable_and_real();
    test_authority_can_be_given_back();
    test_setting_a_property_changes_the_world_immediately();
    test_rest_is_not_offered_as_a_lever();
    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
