// The activation queue holds entity ids, and an id can die before it
// is processed.
//
// Observed live in Logogenesis (issue #21): two trees activated in one
// frame, the first reported as "has no chunk coordinates". It had
// them. It had been destroyed.
//
// One TreeSeed produced both trees. spawn_tree grows a tree, which
// self-queues for activation; sees it came out collapsed (under 12
// particles); destroys it; and grows another. The dead id stayed in
// the queue, and because a destroyed entity returns empty for every
// property, reading chunk_x first made a corpse look like a generator
// that had forgotten to set its coordinates. The search went after the
// wrong thing.
//
// So the queue must ask whether an entity is still alive BEFORE it
// asks anything else, and the two cases must stay distinguishable:
//
//   destroyed before activation  -> normal, skip quietly
//   alive but no chunk coords    -> a real caller error, stay loud
//
// Usage:
//   ./build/test_activation_queue_stale_ids

#include "core/engine.h"
#include "logosphere/worldgen/worldgen_system.h"
#include "logosphere/worldgen/scene_chunk_generator.h"
#include "logosphere/kg/kg_module.h"

#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg)                                                \
    do {                                                                \
        if (cond) { tests_passed++; }                                   \
        else { tests_failed++;                                          \
               std::cout << "FAIL: " << msg << std::endl; }             \
    } while (0)

namespace {

// What the engine SAYS is the contract that changed here. Before the
// fix both failures printed the same error, so asserting only that the
// batch completed would pass against the bug: the batch always
// completed. Capture the report itself.
std::string capture(const std::function<void()>& fn) {
    std::ostringstream out, err;
    auto* o = std::cout.rdbuf(out.rdbuf());
    auto* e = std::cerr.rdbuf(err.rdbuf());
    fn();
    std::cout.rdbuf(o);
    std::cerr.rdbuf(e);
    return out.str() + err.str();
}

struct Harness {
    Engine engine;
    Harness() : engine(nullptr) {
        EngineConfig config;
        config.create_display = false;
        config.window_width = 640;
        config.window_height = 480;
        config.window_title = "activation queue";
        config.show_debug_overlay = false;
        config.enable_chat_window = false;
        if (engine.initialize(config) < 0)
            throw std::runtime_error("Engine::initialize() failed headless");
    }
    ~Harness() { engine.shutdown(); }

    SceneChunkGenerator& scene() {
        return engine.get_worldgen_system().get_scene_generator();
    }
    kg::KGModule& kg() { return engine.get_kg(); }
};

// The exact shape from the bug: create, queue, throw away, create
// again, and process both in one pass. The survivor must activate.
void test_a_destroyed_entity_does_not_take_the_batch_down() {
    Harness h;

    kg::EntityID first = h.kg().createEntityAtPosition("Humanoid", 0.0f, 0.0f);
    h.scene().queue_entity_activation(first);

    // The caller changes its mind, exactly as the collapse retry does.
    h.kg().destroyEntity(first);

    kg::EntityID second = h.kg().createEntityAtPosition("Humanoid", 0.0f, 0.0f);
    h.scene().queue_entity_activation(second);

    CHECK(!h.kg().exists(first), "the discarded entity is really gone");
    CHECK(h.kg().exists(second), "the replacement is alive before processing");

    // Draining the queue is what update() does; activate_entity_now is
    // the public entry point for a single id and is where the fix
    // lives, so the batch is walked the same way the queue walks it.
    const std::string said =
        capture([&] { h.scene().activate_entity_now(first);
                      h.scene().activate_entity_now(second); });

    std::cout << "  [measure] batch of 2, first destroyed. Engine said:\n"
              << said << std::endl;
    CHECK(h.kg().exists(second),
          "the live entity survives a batch containing a dead id");
    CHECK(said.find("ERROR") == std::string::npos,
          "a discarded entity is not an error (said: " + said + ")");
    CHECK(said.find("no chunk coordinates") == std::string::npos,
          "and is never blamed on missing coordinates it actually had");
    CHECK(said.find("destroyed before activation") != std::string::npos,
          "it is reported for what it is (said: " + said + ")");

    // Asking again about the dead one must stay harmless.
    capture([&] { h.scene().activate_entity_now(first); });
    CHECK(h.kg().exists(second), "and a second pass changes nothing");
}

// The loud error still has to exist, or the fix would hide the real
// mistake it was confused with: an entity made by createEntity, which
// has no place in the world and never gets particles.
void test_a_live_entity_without_coordinates_is_still_an_error() {
    Harness h;

    // createEntity, NOT createEntityAtPosition: no chunk coordinates.
    kg::EntityID placeless = h.kg().createEntity("Humanoid");
    CHECK(placeless != kg::INVALID_ENTITY, "a placeless entity was made");
    CHECK(h.kg().exists(placeless), "it is alive");
    CHECK(h.kg().getProperty(placeless, "chunk_x").empty(),
          "and genuinely has no chunk_x");

    const std::string said =
        capture([&] { h.scene().activate_entity_now(placeless); });

    std::cout << "  [measure] a live entity with no coordinates. Engine said:\n"
              << said << std::endl;
    CHECK(h.kg().exists(placeless),
          "a live placeless entity is not destroyed by activation");
    CHECK(said.find("ERROR") != std::string::npos,
          "this one IS an error and stays loud (said: " + said + ")");
    CHECK(said.find("createEntityAtPosition") != std::string::npos,
          "and says how to fix it (said: " + said + ")");
    CHECK(said.find("destroyed before activation") == std::string::npos,
          "and is not mistaken for a discarded entity");
}

// A destroyed entity and a placeless one must not look alike. This is
// the confusion that sent issue #21 after the wrong cause.
void test_the_two_failures_are_distinguishable() {
    Harness h;

    kg::EntityID dead = h.kg().createEntityAtPosition("Humanoid", 4.0f, 4.0f);
    const std::string had_x = h.kg().getProperty(dead, "chunk_x");
    h.kg().destroyEntity(dead);

    kg::EntityID placeless = h.kg().createEntity("Humanoid");

    CHECK(!had_x.empty(),
          "createEntityAtPosition really did set chunk_x (" + had_x + ")");
    // Both now read empty. Properties alone cannot tell them apart,
    // which is why existence has to be checked first.
    CHECK(h.kg().getProperty(dead, "chunk_x").empty(),
          "a destroyed entity reports no chunk_x, despite having had one");
    CHECK(h.kg().getProperty(placeless, "chunk_x").empty(),
          "and so does a placeless one");
    CHECK(!h.kg().exists(dead) && h.kg().exists(placeless),
          "existence is the only thing that separates them");

    // And the engine must not describe them the same way.
    const std::string on_dead =
        capture([&] { h.scene().activate_entity_now(dead); });
    const std::string on_placeless =
        capture([&] { h.scene().activate_entity_now(placeless); });
    CHECK(on_dead != on_placeless,
          "the two are reported differently, which is the whole point");
}

}  // namespace

int main() {
    std::cout << "Activation queue (ids can die before they are processed)"
              << std::endl;
    test_a_destroyed_entity_does_not_take_the_batch_down();
    test_a_live_entity_without_coordinates_is_still_an_error();
    test_the_two_failures_are_distinguishable();
    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
