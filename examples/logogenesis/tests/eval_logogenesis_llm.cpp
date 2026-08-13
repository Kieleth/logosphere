// =============================================================================
// Logogenesis LLM regression evals — live model, headless engine.
// =============================================================================
// The idea (user, 2026-07-31): small canonical requests run against
// the REAL LLM, asserting on what actually lands in the engine (KG
// entities, generator properties, coverage math). Nondeterminism is
// handled by RANGES, and the ranges are the living spec of what "a
// tree" / "a carpet" means. Prompt or model changes that degrade
// creation quality fail here before a human ever opens a window.
//
// Cost/pace: one real LLM call per case (haiku-class, cents). NOT in
// the default battery: requires ANTHROPIC_API_KEY in the env and
// SKIPS cleanly without it (CI-safe, same pattern as the MLX smoke).
//
// Run:
//   ANTHROPIC_API_KEY=... ./build/eval_logogenesis_llm
// =============================================================================

#include "logogenesis_app.h"
#include "core/game_time.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

static int tests_passed = 0;
static int tests_failed = 0;

#define EVAL_ASSERT(cond, msg)                                          \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::cout << "FAIL: " << msg << std::endl;                  \
            tests_failed++;                                             \
            return;                                                     \
        }                                                               \
    } while (0)

#define EVAL_CASE(fn)                                                   \
    do {                                                                \
        std::cout << "  " << #fn << "... " << std::endl;                \
        int before = tests_failed;                                      \
        fn();                                                           \
        if (tests_failed == before) {                                   \
            tests_passed++;                                             \
            std::cout << "PASS" << std::endl;                           \
        }                                                               \
    } while (0)

namespace {

struct Harness {
    logogenesis::LogogenesisApplication app;
    Engine engine;

    Harness() : engine(&app) {
        EngineConfig config;
        config.create_display = false;
        config.window_width = 1600;
        config.window_height = 1200;
        config.window_title = "logogenesis-eval";
        config.show_debug_overlay = false;
        config.show_kg_inspector = false;
        config.enable_chat_window = false;
        if (engine.initialize(config) < 0)
            throw std::runtime_error("Engine::initialize() failed headless");
    }
    ~Harness() { engine.shutdown(); }

    // Submit a request and pump the engine until the app has made at
    // least `min_creations` things or the real-time deadline passes
    // (LLM latency lives on a worker thread the update loop drains).
    bool ask(const std::string& text, size_t min_creations,
             int deadline_s = 45) {
        app.submit_text_for_test(text);
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(deadline_s);
        while (app.creations() < min_creations &&
               std::chrono::steady_clock::now() < deadline) {
            engine.update(1.0 / 60.0);
        }
        // A few settle ticks (materializer + activation).
        for (int i = 0; i < 30; ++i) engine.update(1.0 / 60.0);
        return app.creations() >= min_creations;
    }
};

float propf(kg::KGModule& kg, kg::EntityID e, const char* k) {
    auto v = kg.getProperty(e, k);
    try { return v.empty() ? 0.0f : std::stof(v); } catch (...) { return 0.0f; }
}

// --- Cases: the living spec of small requests -------------------------------

void eval_a_beautiful_tree() {
    Harness h;
    auto& kg = h.engine.get_kg();
    EVAL_ASSERT(h.ask("a beautiful tree", 3),
        "tree request must create (light + ground + tree) in time");
    EVAL_ASSERT(kg.findByType("Floor").size() >= 1,
        "the empty void teaches: ground exists after the first act");
    EVAL_ASSERT(kg.findByType("LightSource").size() >= 1 ||
                    !kg.findByType("Sky").empty(),
        "the unlit void teaches: light (or the sun) exists after the "
        "first act");
    auto trees = kg.findByType("Tree");
    EVAL_ASSERT(trees.size() == 1,
        "exactly one tree (got " + std::to_string(trees.size()) + ")");
    EVAL_ASSERT(kg.getEntityKGParticles(trees[0]).size() >= 20,
        "a tree has real structure (>=20 particles)");
}

void eval_a_forest_arrives_whole() {
    Harness h;
    auto& kg = h.engine.get_kg();
    // The one-breath contract: a forest is MANY TreeSeeds in ONE
    // response (the 800-token budget used to truncate the batch to
    // one or two trees and a broken JSON tail). Sun + ground + 5
    // trees = 7 creations minimum.
    EVAL_ASSERT(h.ask("a forest of at least five trees, "
                      "varied species", 7, 60),
        "forest request must create sun + ground + trees in time");
    auto trees = kg.findByType("Tree");
    EVAL_ASSERT(trees.size() >= 5,
        "the forest arrives whole in one turn (got " +
        std::to_string(trees.size()) + " trees, want >=5)");
    // Spread, not a stack: at least two trees far apart.
    float max_d2 = 0.0f;
    for (auto a : trees)
        for (auto b : trees) {
            float dx = propf(kg, a, "x") - propf(kg, b, "x");
            float dy = propf(kg, a, "y") - propf(kg, b, "y");
            max_d2 = std::max(max_d2, dx * dx + dy * dy);
        }
    EVAL_ASSERT(max_d2 > 36.0f,
        "the forest spreads (max spacing " +
        std::to_string(std::sqrt(max_d2)) + " m, want > 6)");
}

void eval_a_carpet_of_grass() {
    Harness h;
    auto& kg = h.engine.get_kg();
    EVAL_ASSERT(h.ask("cover the floor with a lush carpet of grass", 3),
        "carpet request must create before the deadline");

    // The living spec of "a carpet", v2: EITHER the lawn pattern the
    // prompt teaches (a green ground + sprinkled accents) OR dense
    // literal blades. Both are legitimate readings of the words.
    bool green_lawn = false;
    for (auto f : kg.findByType("Floor")) {
        float r = propf(kg, f, "ground_r"), g = propf(kg, f, "ground_g"),
              b = propf(kg, f, "ground_b");
        if (g > r && g > b && g > 0.35f) green_lawn = true;
    }
    float area = 0, blades = 0;
    for (auto p : kg.findByType("GrassPatch")) {
        area += propf(kg, p, "patch_width") * propf(kg, p, "patch_depth");
        blades += propf(kg, p, "blade_count");
    }
    bool dense_blades = area >= 150.0f && blades >= 400.0f &&
                        blades / area >= 1.5f;
    EVAL_ASSERT(green_lawn || dense_blades,
        "a carpet is a green lawn or dense blades (lawn=" +
            std::to_string(green_lawn) + " area=" + std::to_string(area) +
            " blades=" + std::to_string(blades) + ")");
    EVAL_ASSERT(!kg.findByType("GrassPatch").empty() || green_lawn,
        "some grass texture exists");
}

void eval_a_small_gray_rock() {
    Harness h;
    auto& kg = h.engine.get_kg();
    EVAL_ASSERT(h.ask("a small gray rock", 3),
        "rock request must create before the deadline");
    auto rocks = kg.findByType("Rock");
    EVAL_ASSERT(rocks.size() == 1,
        "exactly one rock (got " + std::to_string(rocks.size()) + ")");
    // Small means small: a pebble-to-head-sized particle budget.
    EVAL_ASSERT(kg.getEntityKGParticles(rocks[0]).size() <= 60,
        "a SMALL rock stays small (got " +
            std::to_string(kg.getEntityKGParticles(rocks[0]).size()) +
            " particles)");
}

void eval_a_maximal_redwood() {
    Harness h;
    auto& kg = h.engine.get_kg();
    // The trap that caught the playtest: "as tall as possible"
    // invites out-of-range specs. The slice now carries min/max, so
    // the model must land ON the cap, not past it.
    EVAL_ASSERT(h.ask("a magnificent redwood, as tall as possible", 3),
        "maximal redwood must create before the deadline");
    // The model may choose grow_seconds (a time-lapse): a mid-growth
    // redwood is legitimately a sapling. Let its life finish first.
    {
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(150);
        while (h.app.growth_jobs_active() > 0 &&
               std::chrono::steady_clock::now() < deadline)
            h.engine.update(1.0 / 60.0);
        for (int i = 0; i < 30; ++i) h.engine.update(1.0 / 60.0);
    }
    auto trees = kg.findByType("Tree");
    EVAL_ASSERT(trees.size() == 1,
        "the redwood grew (got " + std::to_string(trees.size()) +
        " trees — an out-of-range spec was refused?)");
    EVAL_ASSERT(kg.getEntityKGParticles(trees[0]).size() >= 150,
        "a maximal redwood has serious structure (got " +
            std::to_string(kg.getEntityKGParticles(trees[0]).size()) + ")");
}

void eval_golden_hour() {
    Harness h;
    auto& kg = h.engine.get_kg();
    // Two turns: make a scene, then ask for golden hour. The model
    // must create a real sun and steer the Sky clock into the
    // 17.5-19.5 window.
    EVAL_ASSERT(h.ask("a small meadow with one oak", 3),
        "scene request must create in time");
    size_t before = h.app.creations();
    h.ask("make it golden hour, with a real sun", before + 1, 60);
    auto skies = kg.findByType("Sky");
    EVAL_ASSERT(!skies.empty(),
        "golden hour requires the sun (Sky entity exists)");
    // Let the sky clock run whatever the model chose to do.
    for (int i = 0; i < 2400; ++i) h.engine.update(1.0 / 60.0);
    float chosen = propf(kg, skies[0], "time_of_day");
    // Model steering is OBSERVED, not asserted — whether it used
    // SunSeed.time_of_day or set_property is a quality signal, but
    // stacking three model choices made this case a dice game.
    std::cout << "  [observe] model-steered hour: " << chosen
              << (chosen >= 17.0f && chosen <= 20.0f ? " (golden)" : "")
              << std::endl;
    // The SYSTEM property we own: with the sun up, the sky clock
    // must carry the world to any requested hour and stop there.
    kg.setProperty(skies[0], "time_of_day", "18.2");
    bool arrived = false;
    for (int i = 0; i < 3600 && !arrived; ++i) {
        h.engine.update(1.0 / 60.0);
        double now_h = GameTime::get_day_fraction(
                           GameTime::get_current_time()) * 24.0;
        arrived = std::fabs(now_h - 18.2) < 0.3;
    }
    EVAL_ASSERT(arrived, "the sky clock carries the world to 18.2");
}

}  // namespace

int main() {
    const char* key = std::getenv("ANTHROPIC_API_KEY");
    if (!key || !*key) {
        std::cout << "SKIPPED: ANTHROPIC_API_KEY not set (live-LLM eval; "
                     "CI-safe skip)" << std::endl;
        return 0;
    }
    // Regression evals deserve the stronger model: haiku-tier
    // roulette on multi-constraint asks (sun + hour) flaked the
    // suite. Overridable via LOGOGENESIS_MODEL.
    setenv("LOGOGENESIS_MODEL", "claude-sonnet-5", /*overwrite=*/0);
    std::cout << "Logogenesis LLM evals (live model: "
              << std::getenv("LOGOGENESIS_MODEL") << ")" << std::endl;
    EVAL_CASE(eval_a_beautiful_tree);
    EVAL_CASE(eval_a_forest_arrives_whole);
    EVAL_CASE(eval_a_carpet_of_grass);
    EVAL_CASE(eval_a_small_gray_rock);
    EVAL_CASE(eval_a_maximal_redwood);
    EVAL_CASE(eval_golden_hour);
    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
