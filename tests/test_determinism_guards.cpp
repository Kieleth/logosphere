// Three ways the engine used to give different answers to the same
// question, each now pinned.
//
// None of these were found by playing. They came out of asking "what
// would stop a run repeating", and each had been sitting in the engine
// long enough to be load-bearing somewhere: a reset that did not fully
// reset, a counter shared between engines that were supposed to be
// independent, and a generator seeded from the clock whose seed setter
// no caller had ever used.

#include "core/engine.h"
#include "core/time_system.h"
#include "logosphere/worldgen/rock_generator.h"
#include "logosphere/kg/kg_module.h"
#include "generated/logosphere_ontology_registry.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

int passed = 0;
int failed = 0;

#define CHECK(condition, message)                                        \
    do {                                                                 \
        if (condition) { ++passed; }                                     \
        else { ++failed; std::cout << "FAIL: " << (message) << "\n"; }   \
    } while (false)

// A reset system must be indistinguishable from a fresh one. The
// physics accumulator is time owed to the next fixed step, so leaving
// it set makes the first frame after a reset take a different number
// of substeps than the first frame of a new system.
void reset_leaves_no_time_owed() {
    // Physics steps at 30 Hz, so a 60 Hz tick banks half a step and
    // takes none. That leftover is the thing reset forgot.
    TimeSystem fresh;
    TimeSystem used;

    used.tick(1.0 / 60.0);
    used.tick_fixed_physics([](double) {});   // 0 steps, half a step owed
    used.reset();

    int fresh_steps = 0, reset_steps = 0;
    fresh.tick(1.0 / 60.0);
    fresh.tick_fixed_physics([&](double) { ++fresh_steps; });
    used.tick(1.0 / 60.0);
    used.tick_fixed_physics([&](double) { ++reset_steps; });

    // Alpha is the accumulator as a fraction of a step, and it is
    // public, so it says exactly how much time is owed. Half a step
    // after one 60 Hz tick. Without the fix the reset system had a
    // whole step banked, would have taken it here, and this would read
    // 0.0 against the fresh system's 0.5.
    const double fresh_alpha = fresh.get_interpolation_alpha();
    const double reset_alpha = used.get_interpolation_alpha();
    std::cout << "  [measure] time owed after one tick: fresh "
              << fresh_alpha << ", reset " << reset_alpha
              << " (steps " << fresh_steps << " vs " << reset_steps
              << ")\n";

    CHECK(std::fabs(fresh_alpha - 0.5) < 1e-9,
          "a fresh system owes half a step after one 60 Hz tick, got " +
              std::to_string(fresh_alpha));
    CHECK(std::fabs(reset_alpha - fresh_alpha) < 1e-9,
          "and a reset one owes exactly the same, got " +
              std::to_string(reset_alpha));
    CHECK(fresh_steps == reset_steps,
          "so both take the same number of substeps: " +
              std::to_string(fresh_steps) + " vs " +
              std::to_string(reset_steps));
    CHECK(std::fabs(used.get_total_game_time() - 1.0 / 60.0) < 1e-12,
          "and the clock restarted from zero");
}

// Two engines in one process are two engines. The update counter feeds
// the integrity monitor and the deep probes, so a shared one puts
// their windows somewhere arbitrary for whichever engine ran second.
void two_engines_do_not_share_a_frame_counter() {
    EngineConfig config;
    config.create_display = false;
    config.show_debug_overlay = false;
    config.show_kg_inspector = false;

    Engine first;
    CHECK(first.initialize(config) >= 0, "the first engine starts");
    for (int i = 0; i < 30; ++i) first.update(1.0 / 60.0);

    Engine second;
    CHECK(second.initialize(config) >= 0, "the second engine starts");
    second.update(1.0 / 60.0);

    // The counter is private, so this asks the question through
    // something that consumes it: a brand new engine has run one
    // frame, not thirty-one.
    std::cout << "  [measure] first engine: " << first.update_count()
              << " frames, second: " << second.update_count() << "\n";
    CHECK(first.update_count() == 30 && second.update_count() == 1,
          "each engine counts its own frames, not the process's: " +
              std::to_string(first.update_count()) + " and " +
              std::to_string(second.update_count()));

    first.shutdown();
    second.shutdown();
}

// A place should look like itself. The rock generator seeded from
// time(nullptr), so the same spot produced a different rock on every
// run, and the seed setter that would have fixed it had no caller
// anywhere in the repo.
//
// Rock is not an engine type, so the test supplies one the way a game
// would, and drives a real Engine because the generator writes
// particles through it.
void the_same_place_grows_the_same_rock() {
    EngineConfig config;
    config.create_display = false;
    Engine engine;
    if (engine.initialize(config) < 0) {
        CHECK(false, "the engine starts");
        return;
    }
    auto& world = engine.get_kg();

    kg::OntologyRegistry game("schema://determinism-test");
    game.addEntityType("Rock", "Entity", false);
    game.addAncestors("Rock", {"Entity", "Describable", "Identifiable",
                               "Temporal"});
    world.extendOntology(game);

    RockGenerator gen;
    gen.initialize(&engine, &world);

    // The shape is where its particles ended up.
    const auto shape_at = [&](float x, float y, float z) {
        RockSpec spec;
        spec.type = RockSpec::RockType::MEDIUM_ROCK;
        const auto rock = gen.generate_rock(x, y, z, spec);
        std::string signature;
        for (const auto id : world.getEntityKGParticles(rock)) {
            const auto p = world.getKGParticleData(id);
            signature += std::to_string(p.x) + "," + std::to_string(p.y) +
                         "," + std::to_string(p.z) + ";";
        }
        return signature;
    };

    const std::string here_once = shape_at(10.0f, 20.0f, 0.0f);
    const std::string here_again = shape_at(10.0f, 20.0f, 0.0f);
    const std::string over_there = shape_at(11.0f, 20.0f, 0.0f);

    std::cout << "  [measure] " << here_once.size()
              << " chars of geometry per rock\n";
    CHECK(!here_once.empty(),
          "the rock grew particles at all (needs a Rock type in the "
          "ontology, which the engine does not define)");
    CHECK(here_once == here_again,
          "the same spot grows the same rock twice");
    CHECK(here_once != over_there,
          "and a different spot grows a different one, so the seed is "
          "the position and not a constant");

    engine.shutdown();
}

}  // namespace

int main() {
    std::cout << "=== determinism guards ===\n";
    try {
        reset_leaves_no_time_owed();
        two_engines_do_not_share_a_frame_counter();
        the_same_place_grows_the_same_rock();
    } catch (const std::exception& error) {
        std::cout << "FAIL: " << error.what() << "\n";
        ++failed;
    }
    std::cout << "\n[measure] " << passed << " passed, " << failed
              << " failed\n";
    return failed == 0 ? 0 : 1;
}
