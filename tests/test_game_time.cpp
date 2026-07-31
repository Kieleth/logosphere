// GameTime static singleton tests
//
// Covers the basic time authority: advance, time scale, pause/resume,
// jump, calendar helpers, and timeline branching.
//
// Usage:
//   ./build/test_game_time

#include "core/game_time.h"
#include <iostream>
#include <cmath>

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "FAIL: " << msg << std::endl; \
        tests_failed++; \
    } else { \
        tests_passed++; \
    } \
} while (0)

static int tests_passed = 0;
static int tests_failed = 0;

void test_initialize_sets_time() {
    GameTime::reset();
    GameTime::initialize(0.0);
    ASSERT(GameTime::get_current_time() == 0.0, "init at 0");
    ASSERT(GameTime::get_time_scale() == 1.0, "default scale is 1.0");

    GameTime::reset();
    GameTime::initialize(100.0);
    ASSERT(GameTime::get_current_time() == 100.0, "init at arbitrary time");
}

void test_advance_scales_by_time_scale() {
    GameTime::reset();
    GameTime::initialize(0.0);

    GameTime::advance(1.0);
    ASSERT(std::abs(GameTime::get_current_time() - 1.0) < 1e-9,
           "advance(1) at scale=1 → +1");

    GameTime::set_time_scale(2.0);
    GameTime::advance(1.0);
    ASSERT(std::abs(GameTime::get_current_time() - 3.0) < 1e-9,
           "advance(1) at scale=2 → +2");

    GameTime::set_time_scale(0.5);
    GameTime::advance(2.0);
    ASSERT(std::abs(GameTime::get_current_time() - 4.0) < 1e-9,
           "advance(2) at scale=0.5 → +1");
}

void test_pause_stops_time() {
    GameTime::reset();
    GameTime::initialize(10.0);

    GameTime::pause();
    ASSERT(GameTime::get_time_scale() == 0.0, "pause sets scale=0");

    GameTime::advance(5.0);
    ASSERT(GameTime::get_current_time() == 10.0, "paused: advance does nothing");

    GameTime::resume();
    ASSERT(GameTime::get_time_scale() == 1.0, "resume sets scale=1");

    GameTime::advance(2.0);
    ASSERT(std::abs(GameTime::get_current_time() - 12.0) < 1e-9,
           "resumed: advance works again");
}

void test_jump_sets_time_directly() {
    GameTime::reset();
    GameTime::initialize(0.0);

    GameTime::jump_to(1000.0);
    ASSERT(GameTime::get_current_time() == 1000.0, "jump forward");

    GameTime::jump_to(500.0);
    ASSERT(GameTime::get_current_time() == 500.0, "jump backward");
}

void test_calendar_helpers() {
    GameTime::reset();
    GameTime::initialize(0.0);

    ASSERT(GameTime::get_day(0.0) == 0, "day 0 at time 0");
    ASSERT(GameTime::get_day(GameTime::SECONDS_PER_DAY) == 1, "day 1 after 1 day");
    ASSERT(GameTime::get_day(GameTime::SECONDS_PER_DAY * 2.5) == 2,
           "day 2 at 2.5 days (floor)");

    ASSERT(GameTime::get_year(0.0) == 0, "year 0 at time 0");
    ASSERT(GameTime::get_year(GameTime::SECONDS_PER_YEAR) == 1, "year 1 after 1 year");

    // Day fraction: 0.5 at noon
    double noon = GameTime::SECONDS_PER_DAY * 0.5;
    ASSERT(std::abs(GameTime::get_day_fraction(noon) - 0.5) < 1e-6,
           "day_fraction = 0.5 at noon");

    // Rolls over at midnight
    double day_plus_six_hours = GameTime::SECONDS_PER_DAY + (GameTime::SECONDS_PER_DAY * 0.25);
    ASSERT(std::abs(GameTime::get_day_fraction(day_plus_six_hours) - 0.25) < 1e-6,
           "day_fraction wraps each day");
}

void test_timeline_branching() {
    GameTime::reset();
    GameTime::initialize(0.0);

    timeline_id root = GameTime::get_current_timeline();

    // Advance on root timeline
    GameTime::advance(100.0);

    // Branch at t=100
    timeline_id branch = GameTime::create_timeline_branch(
        root, 100.0, "test branch");
    ASSERT(branch != root, "new timeline id is distinct");
    ASSERT(GameTime::get_parent_timeline(branch) == root,
           "branch parent is root");
    ASSERT(std::abs(GameTime::get_branch_time(branch) - 100.0) < 1e-9,
           "branch time recorded");

    auto all = GameTime::get_all_timelines();
    ASSERT(all.size() >= 2, "at least 2 timelines after branching");

    GameTime::set_timeline(branch);
    ASSERT(GameTime::get_current_timeline() == branch, "switched to branch");
}

int main() {
    std::cout << "=== GameTime Tests ===" << std::endl;

    test_initialize_sets_time();
    test_advance_scales_by_time_scale();
    test_pause_stops_time();
    test_jump_sets_time_directly();
    test_calendar_helpers();
    test_timeline_branching();

    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
