// Logotron — continuous cycle motion tests.
//
// No Engine, no KG, no GLFW. Pure functions from cycle.h.

#include "cycle.h"
#include <cmath>
#include <iostream>
#include <string>
#include <stdexcept>

using namespace logotron;

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    std::cout << "  " #name "... "; \
    try { test_##name(); tests_passed++; std::cout << "PASS" << std::endl; } \
    catch (const std::exception& e) { tests_failed++; std::cout << "FAIL: " << e.what() << std::endl; }

#define ASSERT_NEAR(a, b, tol, msg) \
    if (std::abs((a) - (b)) > (tol)) throw std::runtime_error( \
        std::string(msg) + " [got=" + std::to_string(a) + " want=" + std::to_string(b) + "]")

#define ASSERT_EQ(a, b, msg) \
    if ((a) != (b)) throw std::runtime_error( \
        std::string(msg) + " [got=" + std::to_string(a) + " want=" + std::to_string(b) + "]")

#define ASSERT_TRUE(cond, msg) \
    if (!(cond)) throw std::runtime_error(std::string(msg))

void test_cycle_at_origin_moves_east_continuous() {
    Cycle c;  // defaults: (0,0) EAST RIDING
    step_cycle(c, 1.0f);  // one full second
    ASSERT_NEAR(c.x, kCycleSpeed, 0.001f, "EAST 1s: x = speed");
    ASSERT_NEAR(c.y, 0.0f, 0.001f, "EAST 1s: y unchanged");
}

void test_cycle_moves_in_each_cardinal_direction() {
    const float dt = 0.5f;
    const float d = kCycleSpeed * dt;

    Cycle n{10.0f, 10.0f, Direction::NORTH, CycleState::RIDING};
    step_cycle(n, dt);
    ASSERT_NEAR(n.x, 10.0f,     0.001f, "NORTH: x unchanged");
    ASSERT_NEAR(n.y, 10.0f + d, 0.001f, "NORTH: y increases (+y is north)");

    Cycle s{10.0f, 10.0f, Direction::SOUTH, CycleState::RIDING};
    step_cycle(s, dt);
    ASSERT_NEAR(s.x, 10.0f,     0.001f, "SOUTH: x unchanged");
    ASSERT_NEAR(s.y, 10.0f - d, 0.001f, "SOUTH: y decreases");

    Cycle e{10.0f, 10.0f, Direction::EAST, CycleState::RIDING};
    step_cycle(e, dt);
    ASSERT_NEAR(e.x, 10.0f + d, 0.001f, "EAST: x increases");
    ASSERT_NEAR(e.y, 10.0f,     0.001f, "EAST: y unchanged");

    Cycle w{10.0f, 10.0f, Direction::WEST, CycleState::RIDING};
    step_cycle(w, dt);
    ASSERT_NEAR(w.x, 10.0f - d, 0.001f, "WEST: x decreases");
    ASSERT_NEAR(w.y, 10.0f,     0.001f, "WEST: y unchanged");
}

void test_many_small_dt_equals_one_large_dt() {
    // Integration discipline: summing 100 × 0.01s steps should match a
    // single 1.0s step to numerical precision.
    Cycle a; step_cycle(a, 1.0f);
    Cycle b;
    for (int i = 0; i < 100; i++) step_cycle(b, 0.01f);
    ASSERT_NEAR(a.x, b.x, 0.01f, "continuous integration is linear");
    ASSERT_NEAR(a.y, b.y, 0.01f, "continuous integration is linear");
}

void test_direction_change_applied_immediately() {
    // Turning changes the integration direction on the next step.
    Cycle c;
    step_cycle(c, 0.2f);
    ASSERT_NEAR(c.x, kCycleSpeed * 0.2f, 0.001f, "first east step");
    ASSERT_NEAR(c.y, 0.0f, 0.001f, "first east step y");

    c.direction = Direction::SOUTH;
    step_cycle(c, 0.2f);
    ASSERT_NEAR(c.x, kCycleSpeed * 0.2f,  0.001f, "x preserved on south");
    ASSERT_NEAR(c.y, -kCycleSpeed * 0.2f, 0.001f, "y decreases on south");
}

void test_crashed_cycle_does_not_move() {
    Cycle c{5.0f, 5.0f, Direction::EAST, CycleState::CRASHED};
    step_cycle(c, 5.0f);  // large dt
    ASSERT_NEAR(c.x, 5.0f, 0.001f, "CRASHED x stays");
    ASSERT_NEAR(c.y, 5.0f, 0.001f, "CRASHED y stays");
}

void test_zero_and_negative_dt_is_noop() {
    Cycle c{3.0f, 4.0f, Direction::EAST, CycleState::RIDING};
    step_cycle(c, 0.0f);
    ASSERT_NEAR(c.x, 3.0f, 0.001f, "dt=0 no move");
    step_cycle(c, -1.0f);
    ASSERT_NEAR(c.x, 3.0f, 0.001f, "dt<0 no move");
}

void test_turn_left_rotates_ccw_through_all_directions() {
    ASSERT_TRUE(turn_left(Direction::NORTH) == Direction::WEST,  "NORTH ←  WEST");
    ASSERT_TRUE(turn_left(Direction::WEST)  == Direction::SOUTH, "WEST  ← SOUTH");
    ASSERT_TRUE(turn_left(Direction::SOUTH) == Direction::EAST,  "SOUTH ← EAST");
    ASSERT_TRUE(turn_left(Direction::EAST)  == Direction::NORTH, "EAST  ← NORTH");
}

void test_turn_right_rotates_cw_through_all_directions() {
    ASSERT_TRUE(turn_right(Direction::NORTH) == Direction::EAST,  "NORTH → EAST");
    ASSERT_TRUE(turn_right(Direction::EAST)  == Direction::SOUTH, "EAST  → SOUTH");
    ASSERT_TRUE(turn_right(Direction::SOUTH) == Direction::WEST,  "SOUTH → WEST");
    ASSERT_TRUE(turn_right(Direction::WEST)  == Direction::NORTH, "WEST  → NORTH");
}

void test_left_then_right_returns_to_original() {
    for (auto d : {Direction::NORTH, Direction::EAST, Direction::SOUTH, Direction::WEST}) {
        ASSERT_TRUE(turn_right(turn_left(d)) == d, "L then R is identity");
        ASSERT_TRUE(turn_left(turn_right(d)) == d, "R then L is identity");
    }
}

void test_default_cycle_uses_base_speed_until_ramp_kicks_in() {
    // Defaults: base=5, max=8, ramp=1 m/s². At t=0+ε, current_speed
    // is base. At t = (max-base)/ramp = 3 s, current_speed should be
    // pinned at max.
    Cycle c;  // EAST RIDING at origin, speed defaults
    step_cycle(c, 0.001f);
    ASSERT_NEAR(c.current_speed, c.base_speed, 0.01f, "speed at t≈0 = base");
}

void test_speed_ramps_linearly_until_clamped_at_max() {
    Cycle c;
    c.base_speed = 5.0f;
    c.max_speed  = 8.0f;
    c.speed_ramp_rate = 1.0f;
    c.current_speed = c.base_speed;
    c.time_since_turn = 0.0f;
    // After 1 s of straight driving: 5 + 1 * 1 = 6.
    step_cycle(c, 1.0f);
    ASSERT_NEAR(c.current_speed, 6.0f, 0.001f, "1s: 5 + 1*1 = 6");
    // Cumulative 4 s: 5 + 1*4 = 9, clamped to 8.
    step_cycle(c, 1.0f); step_cycle(c, 1.0f); step_cycle(c, 1.0f);
    ASSERT_NEAR(c.current_speed, c.max_speed, 0.001f, "ramp clamps at max_speed");
}

void test_turn_resets_speed_and_ramp_clock() {
    Cycle c;
    c.base_speed = 5.0f;
    c.max_speed  = 8.0f;
    c.speed_ramp_rate = 1.0f;
    // Drive long enough to reach max.
    for (int i = 0; i < 10; i++) step_cycle(c, 1.0f);
    ASSERT_NEAR(c.current_speed, c.max_speed, 0.001f, "reached max before turn");
    // Turn — should snap back to base, restart ramp clock.
    turn(c, Direction::NORTH);
    ASSERT_NEAR(c.current_speed, c.base_speed, 0.001f, "turn snaps to base");
    ASSERT_NEAR(c.time_since_turn, 0.0f, 0.001f, "turn resets clock");
    // Drive another 1s — back to 6.
    step_cycle(c, 1.0f);
    ASSERT_NEAR(c.current_speed, 6.0f, 0.001f, "post-turn ramp restarts");
}

void test_turn_to_same_direction_is_noop() {
    // turn() must NOT reset speed if the direction didn't actually
    // change — otherwise repeated player input on the same key would
    // hold the bike at base_speed forever.
    Cycle c;
    for (int i = 0; i < 3; i++) step_cycle(c, 1.0f);
    float speed_before = c.current_speed;
    float clock_before = c.time_since_turn;
    turn(c, c.direction);  // same dir
    ASSERT_NEAR(c.current_speed, speed_before, 0.001f, "same-dir turn keeps speed");
    ASSERT_NEAR(c.time_since_turn, clock_before, 0.001f, "same-dir turn keeps clock");
}

void test_zero_ramp_rate_holds_speed_at_base() {
    // Setting ramp_rate=0 disables the §18 mechanic — useful for
    // tests / personalities that want flat constant-speed.
    Cycle c;
    c.base_speed = 5.0f;
    c.max_speed  = 5.0f;
    c.speed_ramp_rate = 0.0f;
    for (int i = 0; i < 100; i++) step_cycle(c, 0.1f);
    ASSERT_NEAR(c.current_speed, 5.0f, 0.001f, "no ramp, no climb");
}

void test_is_legal_turn_blocks_u_turn() {
    ASSERT_TRUE(is_legal_turn(Direction::NORTH, Direction::NORTH), "same direction OK");
    ASSERT_TRUE(is_legal_turn(Direction::NORTH, Direction::EAST),  "NORTH → EAST legal");
    ASSERT_TRUE(is_legal_turn(Direction::NORTH, Direction::WEST),  "NORTH → WEST legal");
    ASSERT_TRUE(!is_legal_turn(Direction::NORTH, Direction::SOUTH), "NORTH → SOUTH blocked");
    ASSERT_TRUE(!is_legal_turn(Direction::EAST,  Direction::WEST),  "EAST → WEST blocked");
    ASSERT_TRUE(!is_legal_turn(Direction::SOUTH, Direction::NORTH), "SOUTH → NORTH blocked");
    ASSERT_TRUE(!is_legal_turn(Direction::WEST,  Direction::EAST),  "WEST → EAST blocked");
}

int main() {
    std::cout << "=== Logotron — Continuous Cycle Motion Tests ===" << std::endl;
    TEST(cycle_at_origin_moves_east_continuous);
    TEST(cycle_moves_in_each_cardinal_direction);
    TEST(many_small_dt_equals_one_large_dt);
    TEST(direction_change_applied_immediately);
    TEST(crashed_cycle_does_not_move);
    TEST(zero_and_negative_dt_is_noop);
    TEST(turn_left_rotates_ccw_through_all_directions);
    TEST(turn_right_rotates_cw_through_all_directions);
    TEST(left_then_right_returns_to_original);
    TEST(is_legal_turn_blocks_u_turn);
    TEST(default_cycle_uses_base_speed_until_ramp_kicks_in);
    TEST(speed_ramps_linearly_until_clamped_at_max);
    TEST(turn_resets_speed_and_ramp_clock);
    TEST(turn_to_same_direction_is_noop);
    TEST(zero_ramp_rate_holds_speed_at_base);
    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
