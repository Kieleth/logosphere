// Logotron — per-run trail segments (one TrailSegment per straight
// run, created on direction change via freeze_run).

#include "arena.h"
#include "cycle.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"
#include "logotron_ontology_registry.h"
#include "generated/logosphere_ontology_registry.h"

#include <cmath>
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

#define ASSERT_EQ(a, b, msg) \
    if ((a) != (b)) throw std::runtime_error( \
        std::string(msg) + " [got=" + std::to_string(a) + " want=" + std::to_string(b) + "]")

#define ASSERT_NEAR(a, b, tol, msg) \
    if (std::abs((a) - (b)) > (tol)) throw std::runtime_error( \
        std::string(msg) + " [got=" + std::to_string(a) + " want=" + std::to_string(b) + "]")

#define ASSERT_STR_EQ(a, b, msg) \
    if ((a) != (b)) throw std::runtime_error( \
        std::string(msg) + " [got=\"" + std::string(a) + "\" want=\"" + std::string(b) + "\"]")

#define ASSERT_TRUE(cond, msg) \
    if (!(cond)) throw std::runtime_error(std::string(msg))

static std::unique_ptr<kg::KGModule> create_logotron_kg() {
    auto kg = std::make_unique<kg::KGModule>(logosphere::ontology::registry());
    kg->setMode(kg::KGMode::MINIMAL);
    kg->extendOntology(logotron::ontology::registry());
    return kg;
}

void test_spawn_cycle_writes_continuous_position_and_run_start() {
    auto kg = create_logotron_kg();
    auto e = spawn_cycle(*kg, "PlayerCycle", 10.5f, 7.25f, Direction::NORTH);
    Cycle c = read_cycle(*kg, e);
    ASSERT_NEAR(c.x, 10.5f, 0.001f, "round-trip x");
    ASSERT_NEAR(c.y, 7.25f, 0.001f, "round-trip y");
    ASSERT_NEAR(c.run_start_x, 10.5f, 0.001f, "run_start init to spawn x");
    ASSERT_NEAR(c.run_start_y, 7.25f, 0.001f, "run_start init to spawn y");
}

void test_step_advances_position_without_laying_trails() {
    auto kg = create_logotron_kg();
    auto e = spawn_cycle(*kg, "PlayerCycle", 5.0f, 5.0f, Direction::EAST);
    for (int i = 0; i < 50; i++) step_cycle_in_kg(*kg, e, 0.01f);
    Cycle c = read_cycle(*kg, e);
    ASSERT_NEAR(c.x, 5.0f + kCycleSpeed * 0.5f, 0.05f, "cycle covered 2.5 m east");
    ASSERT_EQ(count_trails_owned_by(*kg, e), 0, "no trail entities before turn");
    // run_start unchanged by plain stepping
    ASSERT_NEAR(c.run_start_x, 5.0f, 0.001f, "run_start stays at spawn");
    ASSERT_NEAR(c.run_start_y, 5.0f, 0.001f, "run_start stays at spawn");
}

void test_freeze_run_on_turn_creates_one_trail_segment() {
    auto kg = create_logotron_kg();
    auto e = spawn_cycle(*kg, "PlayerCycle", 5.0f, 5.0f, Direction::EAST);
    for (int i = 0; i < 50; i++) step_cycle_in_kg(*kg, e, 0.01f);

    freeze_run(*kg, e);
    ASSERT_EQ(count_trails_owned_by(*kg, e), 1, "one trail after one freeze");

    auto matches = kg->findByProperty("owner_cycle_id", std::to_string(e));
    auto t = matches[0];
    auto sx = std::stof(kg->getProperty(t, "start_x"));
    auto sy = std::stof(kg->getProperty(t, "start_y"));
    auto ex = std::stof(kg->getProperty(t, "end_x"));
    auto ey = std::stof(kg->getProperty(t, "end_y"));
    ASSERT_NEAR(sx, 5.0f, 0.001f, "start_x = spawn x");
    ASSERT_NEAR(sy, 5.0f, 0.001f, "start_y = spawn y");
    ASSERT_NEAR(ex, 7.5f, 0.05f, "end_x ~ 7.5 after 0.5 s east");
    ASSERT_NEAR(ey, 5.0f, 0.001f, "end_y unchanged");
    ASSERT_STR_EQ(kg->getProperty(t, "direction"), "EAST", "run records direction");
}

void test_freeze_updates_run_start_to_current() {
    auto kg = create_logotron_kg();
    auto e = spawn_cycle(*kg, "PlayerCycle", 0.0f, 0.0f, Direction::EAST);
    for (int i = 0; i < 40; i++) step_cycle_in_kg(*kg, e, 0.01f);
    freeze_run(*kg, e);
    Cycle c = read_cycle(*kg, e);
    ASSERT_NEAR(c.run_start_x, c.x, 0.001f, "run_start_x advanced to current");
    ASSERT_NEAR(c.run_start_y, c.y, 0.001f, "run_start_y advanced to current");
}

void test_freeze_without_motion_is_noop() {
    // Turning twice in quick succession could call freeze_run at
    // the same position. No zero-length trail should be created.
    auto kg = create_logotron_kg();
    auto e = spawn_cycle(*kg, "PlayerCycle", 5.0f, 5.0f, Direction::EAST);
    freeze_run(*kg, e);
    ASSERT_EQ(count_trails_owned_by(*kg, e), 0, "no trail for zero-length run");
}

void test_two_cycles_independent_trail_counts() {
    auto kg = create_logotron_kg();
    auto a = spawn_cycle(*kg, "PlayerCycle", 0.0f, 0.0f, Direction::EAST);
    auto b = spawn_cycle(*kg, "AICycle",     0.0f, 5.0f, Direction::EAST);

    // Both drive a bit, then each turns; two trails total, one per cycle.
    for (int i = 0; i < 30; i++) { step_cycle_in_kg(*kg, a, 0.01f);
                                    step_cycle_in_kg(*kg, b, 0.01f); }
    freeze_run(*kg, a);
    freeze_run(*kg, b);

    ASSERT_EQ(count_trails_owned_by(*kg, a), 1, "cycle A has one run segment");
    ASSERT_EQ(count_trails_owned_by(*kg, b), 1, "cycle B has one run segment");
}

int main() {
    std::cout << "=== Logotron — Per-Run Trail Segments ===" << std::endl;
    TEST(spawn_cycle_writes_continuous_position_and_run_start);
    TEST(step_advances_position_without_laying_trails);
    TEST(freeze_run_on_turn_creates_one_trail_segment);
    TEST(freeze_updates_run_start_to_current);
    TEST(freeze_without_motion_is_noop);
    TEST(two_cycles_independent_trail_counts);
    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
