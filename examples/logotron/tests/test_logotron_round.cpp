// v0.6 — Round status: pure function of the two cycles' KG state.
//
// Verifies the four-state machine (IN_PROGRESS, PLAYER_WON, AI_WON,
// DRAW) given combinations of RIDING / CRASHED. End-to-end: spawn,
// step both cycles until one crashes into a wall, assert the right
// winner.

#include "arena.h"
#include "cycle.h"
#include "round.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"
#include "logotron_ontology_registry.h"
#include "generated/logosphere_ontology_registry.h"

#include <iostream>
#include <memory>
#include <string>
#include <stdexcept>

using namespace logotron;

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    std::cout << "  " #name "... "; \
    try { test_##name(); tests_passed++; std::cout << "PASS" << std::endl; } \
    catch (const std::exception& e) { tests_failed++; std::cout << "FAIL: " << e.what() << std::endl; }

#define ASSERT_TRUE(cond, msg) \
    if (!(cond)) throw std::runtime_error(std::string(msg))

static std::unique_ptr<kg::KGModule> create_logotron_kg() {
    auto kg = std::make_unique<kg::KGModule>(logosphere::ontology::registry());
    kg->setMode(kg::KGMode::MINIMAL);
    kg->extendOntology(logotron::ontology::registry());
    return kg;
}

void test_both_alive_is_in_progress() {
    auto kg = create_logotron_kg();
    auto p = spawn_cycle(*kg, "PlayerCycle", 0.5f, 0.5f, Direction::EAST);
    auto a = spawn_cycle(*kg, "AICycle",     0.5f, 5.5f, Direction::EAST);
    ASSERT_TRUE(check_round_status(*kg, p, a) == RoundStatus::IN_PROGRESS,
                "two healthy cycles → IN_PROGRESS");
}

void test_ai_crashed_player_wins() {
    auto kg = create_logotron_kg();
    auto p = spawn_cycle(*kg, "PlayerCycle", 0.5f, 0.5f, Direction::EAST);
    auto a = spawn_cycle(*kg, "AICycle",     0.5f, 5.5f, Direction::EAST);
    kg->setProperty(a, "cycle_state", "CRASHED");
    ASSERT_TRUE(check_round_status(*kg, p, a) == RoundStatus::PLAYER_WON,
                "AI crashed, player riding → PLAYER_WON");
}

void test_player_crashed_ai_wins() {
    auto kg = create_logotron_kg();
    auto p = spawn_cycle(*kg, "PlayerCycle", 0.5f, 0.5f, Direction::EAST);
    auto a = spawn_cycle(*kg, "AICycle",     0.5f, 5.5f, Direction::EAST);
    kg->setProperty(p, "cycle_state", "CRASHED");
    ASSERT_TRUE(check_round_status(*kg, p, a) == RoundStatus::AI_WON,
                "player crashed, AI riding → AI_WON");
}

void test_both_crashed_draw() {
    auto kg = create_logotron_kg();
    auto p = spawn_cycle(*kg, "PlayerCycle", 0.5f, 0.5f, Direction::EAST);
    auto a = spawn_cycle(*kg, "AICycle",     0.5f, 5.5f, Direction::EAST);
    kg->setProperty(p, "cycle_state", "CRASHED");
    kg->setProperty(a, "cycle_state", "CRASHED");
    ASSERT_TRUE(check_round_status(*kg, p, a) == RoundStatus::DRAW,
                "both crashed → DRAW");
}

void test_full_round_player_runs_into_wall_first() {
    // Player starts near the east edge, AI starts in the NW corner.
    // Both go in non-conflicting directions; player hits the east
    // wall in well under a second; AI is still riding south.
    auto kg = create_logotron_kg();
    float arena_w = 40.0f, arena_h = 40.0f;
    auto p = spawn_cycle(*kg, "PlayerCycle", 38.5f, 20.0f, Direction::EAST);
    auto a = spawn_cycle(*kg, "AICycle",      5.0f,  5.0f, Direction::SOUTH);

    int ticks = 0;
    while (check_round_status(*kg, p, a) == RoundStatus::IN_PROGRESS && ticks < 1000) {
        step_cycle_in_kg_with_collision(*kg, p, arena_w, arena_h, 0.01f);
        step_cycle_in_kg_with_collision(*kg, a, arena_w, arena_h, 0.01f);
        ticks++;
    }
    auto status = check_round_status(*kg, p, a);
    ASSERT_TRUE(status == RoundStatus::AI_WON,
                std::string("player should crash first into wall (status=")
                + round_status_name(status) + ")");
    ASSERT_TRUE(ticks < 100, "player should crash within ~1 sec of game time");
}

int main() {
    std::cout << "=== Logotron v0.6 — Round Status ===" << std::endl;
    TEST(both_alive_is_in_progress);
    TEST(ai_crashed_player_wins);
    TEST(player_crashed_ai_wins);
    TEST(both_crashed_draw);
    TEST(full_round_player_runs_into_wall_first);
    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
