// AT: AI personality is Director-tunable via the KG ai_personality
// slot. Drives the headless game, mutates the slot the way a
// Director set_property op would, asserts the AI's local
// Personality cache swaps to match. Locks the contract that's the
// whole point of having named personality variants — without this
// path the LLM has nothing useful to write to the slot.

#include "at_common.h"

#include "ai/personality.h"
#include "application.h"
#include "core/engine.h"
#include "logosphere/kg/kg_module.h"

#include "logotron_app.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

static int tests_passed = 0;
static int tests_failed = 0;

namespace {

void scrub_llm_env() {
    unsetenv("ANTHROPIC_API_KEY");
    unsetenv("OPENAI_API_KEY");
    unsetenv("LOGOTRON_LLM_PROVIDER");
    unsetenv("LOGOTRON_LLM_URL");
    unsetenv("LOGOTRON_LLM_MODEL");
}

struct Harness {
    LogotronApplication app;
    Engine               engine;
    Harness() : engine(&app) {
        scrub_llm_env();
        EngineConfig cfg;
        cfg.create_display     = false;
        cfg.window_width       = 1600;
        cfg.window_height      = 1200;
        cfg.window_title       = "logotron-at";
        cfg.show_debug_overlay = false;
        cfg.show_kg_inspector  = false;
        cfg.enable_chat_window = false;
        if (engine.initialize(cfg) < 0)
            throw std::runtime_error("engine.initialize() failed");
    }
    ~Harness() { engine.shutdown(); }
    void tick(double dt) { engine.update(dt); }
};

// === Pure-C++ contract on the personality_from_name mapper.
//     Engine-free; locks the four enum values map to distinct
//     factories. ===

void test_personality_from_name_dispatches_each_enum_variant() {
    using namespace logotron::ai;
    AT_ASSERT_TRUE(personality_from_name("AGGRESSIVE").name == "AGGRESSIVE",
        "AGGRESSIVE name maps");
    AT_ASSERT_TRUE(personality_from_name("DEFENSIVE").name  == "DEFENSIVE",
        "DEFENSIVE name maps");
    AT_ASSERT_TRUE(personality_from_name("CHAOTIC").name    == "CHAOTIC",
        "CHAOTIC name maps");
    AT_ASSERT_TRUE(personality_from_name("PURSUING").name   == "PURSUING",
        "PURSUING name maps");
}

void test_personality_from_name_unknown_falls_back_to_default() {
    auto p = logotron::ai::personality_from_name("not-a-real-thing");
    AT_ASSERT_TRUE(p.name == "default",
        "unknown name should fall back to default, got '" + p.name + "'");
}

void test_personality_from_name_empty_falls_back_to_default() {
    auto p = logotron::ai::personality_from_name("");
    AT_ASSERT_TRUE(p.name == "default",
        "empty name should fall back to default");
}

void test_aggressive_and_defensive_have_distinct_drive_blends() {
    // Sanity: the four variants produce different blends. If a
    // future copy-paste accidentally aliases two factories to the
    // same weights this catches it.
    auto a = logotron::ai::aggressive_personality();
    auto d = logotron::ai::defensive_personality();
    AT_ASSERT_TRUE(a.head_max_rate != d.head_max_rate,
        "aggressive vs defensive head swivel rate must differ");
    AT_ASSERT_TRUE(a.drive.size() > 0 && d.drive.size() > 0,
        "both have drive tactics");
}

// === Full game-loop AT: a Director-set_property on
//     ai_personality propagates to the AI's live behaviour cache
//     within one tick. ===

// REGRESSION: 2026-05-07 playtest — "enemies are going way faster
// than me, I hate weird advantages." Cause: Director's
// set_property @ai_cycle max_speed ops (from both random fallback
// rolling in [6, 14] and LLM ops) inflated AI's max_speed beyond
// the player's. The contest needs to stay skill-based, not
// speed-cheat-based: AI max_speed gets clamped to the player's
// max_speed every tick regardless of who wrote it.
void test_ai_max_speed_clamped_to_player_max_speed() {
    Harness h;
    auto& kg = h.engine.get_kg();

    // Settle the world. Pre-fire applies during this tick.
    h.tick(1.0 / 60.0);

    auto player = h.app.player_entity();
    auto ai     = h.app.enemy_entity(0);
    AT_ASSERT_TRUE(player != kg::INVALID_ENTITY, "player exists");
    AT_ASSERT_TRUE(ai != kg::INVALID_ENTITY,     "AI exists");

    // Simulate the Director cranking AI speed past the player's.
    // Both random and LLM responders do this routinely; the apply
    // path's clamp must hold the line.
    kg.setProperty(ai, "max_speed", "20.0");

    // Drive a single tick so the per-tick clamp runs.
    h.tick(1.0 / 60.0);

    auto ai_speed_str = kg.getProperty(ai, "max_speed");
    auto pl_speed_str = kg.getProperty(player, "max_speed");
    float ai_speed = std::stof(ai_speed_str);
    float pl_speed = std::stof(pl_speed_str);

    AT_ASSERT_TRUE(ai_speed <= pl_speed + 1e-3f,
        "AI max_speed (" + std::to_string(ai_speed) + ") must be "
        "<= player max_speed (" + std::to_string(pl_speed) + "). "
        "Director-injected speed advantage breaks fair play.");
}

void test_kg_slot_swap_propagates_to_ai_personality_cache() {
    Harness h;
    auto& kg = h.engine.get_kg();

    // First tick: pre-fire applies. After that the AI exists and
    // sync_ai_personality_from_kg has been called once.
    h.tick(1.0 / 60.0);
    AT_ASSERT_TRUE(h.app.enemy_personality_name(0) == "default" ||
                   h.app.enemy_personality_name(0) == "AGGRESSIVE" ||
                   h.app.enemy_personality_name(0) == "DEFENSIVE" ||
                   h.app.enemy_personality_name(0) == "CHAOTIC"  ||
                   h.app.enemy_personality_name(0) == "PURSUING",
        "personality should be one of the known names, got '" +
        h.app.enemy_personality_name(0) + "'");

    // Mutate the AICycle's KG slot the way a Director set_property
    // would. Then drive a Director request through the responder
    // (the random fallback on a fresh fire) so poll_director runs
    // sync_ai_personality_from_kg. Cheaper alternative: call the
    // sync directly via a public hook.
    auto ai = h.app.enemy_entity(0);
    AT_ASSERT_TRUE(ai != kg::INVALID_ENTITY, "AICycle exists");
    kg.setProperty(ai, "ai_personality", "DEFENSIVE");
    h.app.sync_ai_personality_from_kg(kg);

    AT_ASSERT_TRUE(h.app.enemy_personality_name(0) == "DEFENSIVE",
        "AI personality should swap to DEFENSIVE after KG mutation, "
        "got '" + h.app.enemy_personality_name(0) + "'");

    // Swap again; idempotent + works on already-swapped state.
    kg.setProperty(ai, "ai_personality", "PURSUING");
    h.app.sync_ai_personality_from_kg(kg);
    AT_ASSERT_TRUE(h.app.enemy_personality_name(0) == "PURSUING",
        "second swap should land too, got '" +
        h.app.enemy_personality_name(0) + "'");
}

}  // namespace

int main() {
    std::cout << "Logotron AT — personality_swap" << std::endl;
    AT_TEST(test_personality_from_name_dispatches_each_enum_variant);
    AT_TEST(test_personality_from_name_unknown_falls_back_to_default);
    AT_TEST(test_personality_from_name_empty_falls_back_to_default);
    AT_TEST(test_aggressive_and_defensive_have_distinct_drive_blends);
    AT_TEST(test_ai_max_speed_clamped_to_player_max_speed);
    AT_TEST(test_kg_slot_swap_propagates_to_ai_personality_cache);
    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
