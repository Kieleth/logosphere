// Small trees come out as a bare pole.
//
// Second thread of issue #21: a space-colonization tree finished after
// one iteration with one segment. The collapse retry in Logogenesis
// masks it, so it only ever surfaced as an oddity in a log.
//
// Space colonization grows toward attractors and then deletes every
// attractor within kill_distance of ANY node. So if the kill radius
// covers the whole attractor cloud, the first iteration deletes all of
// them and the loop exits with one segment. The two radii are derived
// independently, and only one of them knows how big the tree is:
//
//   crown_reach   = min(max(crown_radius, crown_height*0.45, 2), height*0.6)
//   kill_distance = min(4, max(1.5, 2.5*crown_reach/5))
//
// crown_reach is capped by the tree's height. kill_distance has a hard
// floor of 1.5 m that is not. Below some height the floor swallows the
// crown whole.
//
// This sweeps height and reports where that happens, so the threshold
// is measured rather than argued.
//
// Usage:
//   ./build/test_tree_collapse_threshold

#include "core/engine.h"
#include "logosphere/worldgen/worldgen_system.h"
#include "logosphere/worldgen/tree_generator.h"
#include "logosphere/kg/kg_module.h"
#include "generated/earth_ontology_registry.h"

#include <iomanip>
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
        config.window_width = 640;
        config.window_height = 480;
        config.window_title = "tree collapse";
        config.show_debug_overlay = false;
        config.enable_chat_window = false;
        if (engine.initialize(config) < 0)
            throw std::runtime_error("Engine::initialize() failed headless");
        // "Tree" lives in the earth setting pack, not the core
        // ontology, and createEntity rejects unregistered types.
        engine.get_kg().extendOntology(earth::ontology::registry());
    }
    ~Harness() { engine.shutdown(); }
};

// One tree, grown the way Logogenesis grows it, reported by size.
size_t grow(Harness& h, float height, float crown_radius) {
    auto& tg = h.engine.get_worldgen_system().get_tree_generator();
    TreeSpec spec;
    spec.height = height;
    spec.crown_radius = crown_radius;
    spec.random_seed = 12345;
    kg::EntityID tree = tg.generate_tree_space_colonization(0.0f, 0.0f, 0.0f,
                                                           spec);
    if (tree == kg::INVALID_ENTITY) {
        std::cout << "    (entity rejected)" << std::endl;
        return 0;
    }
    return h.engine.get_kg().getEntityKGParticles(tree).size();
}

// A tree is a tree at every size it is asked for. The engine either
// grows a crown or it does not; "only above 2 m" is not a tree
// generator, it is a tree generator with an undocumented minimum.
void test_small_trees_are_not_bare_poles() {
    Harness h;
    // Logogenesis calls this collapsed: under 12 particles is a pole.
    const size_t COLLAPSED = 12;

    std::cout << "  height  crown_radius  particles" << std::endl;
    std::vector<float> heights = {0.6f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f,
                                  4.0f, 6.0f, 10.0f, 20.0f, 40.0f};
    float tallest_collapse = -1.0f;
    for (float hgt : heights) {
        // crown_radius as a normal caller would give it, and as the
        // collapse retry gives it (a flat 5 m).
        size_t natural = grow(h, hgt, hgt * 0.35f);
        size_t retried = grow(h, hgt, 5.0f);
        std::cout << "  " << std::setw(5) << hgt
                  << "   natural " << std::setw(4) << natural
                  << "   retry(5m) " << std::setw(4) << retried << std::endl;
        if (natural < COLLAPSED) tallest_collapse = hgt;
    }

    std::cout << "  [measure] tallest height that still collapses: "
              << tallest_collapse << " m" << std::endl;

    CHECK(tallest_collapse < 0.0f,
          "no requested height collapses to a bare pole (tallest that "
          "does: " + std::to_string(tallest_collapse) + " m)");

    // Fixing small trees must not quietly cost big ones their detail.
    // Before this fix a 20 m tree came out at 262 particles, and the
    // first attempt at a scale-free rewrite dropped it to 195 while
    // the small-tree numbers looked fine. That is the regression this
    // pins: the length fractions are calibrated so a full-size tree
    // lands where it always did.
    size_t big = grow(h, 20.0f, 7.0f);
    std::cout << "  [measure] 20 m tree: " << big
              << " particles (was 262 before the fix)" << std::endl;
    CHECK(big >= 240,
          "a 20 m tree keeps its detail (got " + std::to_string(big) +
          ", baseline 262)");
}

// The retry exists because collapse happens. If the retry cannot save
// a small tree either, the masking is total and nothing downstream can
// tell a sapling from a failure.
void test_the_collapse_retry_cannot_rescue_a_small_tree() {
    Harness h;
    const size_t COLLAPSED = 12;
    // The retry's own parameters: height clamped to 25, crown forced
    // to 5 m. For a small tree the height is unchanged, so if height
    // is what starves it, the retry changes nothing.
    size_t first = grow(h, 1.2f, 0.4f);
    size_t retry = grow(h, 1.2f, 5.0f);
    std::cout << "  [measure] 1.2 m tree: first " << first
              << " particles, retry-with-5m-crown " << retry << std::endl;
    CHECK(first >= COLLAPSED || retry >= COLLAPSED,
          "a 1.2 m tree survives either the first attempt or the retry "
          "(got " + std::to_string(first) + " then " +
          std::to_string(retry) + ")");
}

}  // namespace

int main() {
    std::cout << "Tree collapse threshold (a tree at every size)"
              << std::endl;
    test_small_trees_are_not_bare_poles();
    test_the_collapse_retry_cannot_rescue_a_small_tree();
    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
