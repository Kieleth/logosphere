// Logotron — line-segment collision (runs as axis-aligned segments).

#include "arena.h"
#include "cycle.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"
#include "logotron_ontology_registry.h"
#include "generated/logosphere_ontology_registry.h"

#include <cmath>
#include <iostream>
#include <limits>
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

#define ASSERT_TRUE(cond, msg) \
    if (!(cond)) throw std::runtime_error(std::string(msg))

#define ASSERT_FALSE(cond, msg) \
    if ((cond)) throw std::runtime_error(std::string(msg))

static const float NaNv = std::numeric_limits<float>::quiet_NaN();

static std::unique_ptr<kg::KGModule> create_logotron_kg() {
    auto kg = std::make_unique<kg::KGModule>(logosphere::ontology::registry());
    kg->setMode(kg::KGMode::MINIMAL);
    kg->extendOntology(logotron::ontology::registry());
    return kg;
}

static void place_horizontal_run(kg::KGModule& kg, float sx, float sy, float ex) {
    auto t = kg.createEntity("TrailSegment");
    kg.setProperty(t, "start_x", std::to_string(sx));
    kg.setProperty(t, "start_y", std::to_string(sy));
    kg.setProperty(t, "end_x",   std::to_string(ex));
    kg.setProperty(t, "end_y",   std::to_string(sy));
    kg.setProperty(t, "direction", ex > sx ? "EAST" : "WEST");
    kg.setProperty(t, "trail_state", "SOLID");
}

static void place_vertical_run(kg::KGModule& kg, float sx, float sy, float ey) {
    auto t = kg.createEntity("TrailSegment");
    kg.setProperty(t, "start_x", std::to_string(sx));
    kg.setProperty(t, "start_y", std::to_string(sy));
    kg.setProperty(t, "end_x",   std::to_string(sx));
    kg.setProperty(t, "end_y",   std::to_string(ey));
    kg.setProperty(t, "direction", ey > sy ? "NORTH" : "SOUTH");
    kg.setProperty(t, "trail_state", "SOLID");
}

void test_negative_world_coords_are_lethal() {
    auto kg = create_logotron_kg();
    ASSERT_TRUE(check_collision(*kg, -0.5f,  5.0f, 40.0f, 40.0f, NaNv, NaNv), "-x lethal");
    ASSERT_TRUE(check_collision(*kg,  5.0f, -0.5f, 40.0f, 40.0f, NaNv, NaNv), "-y lethal");
}

void test_at_or_past_world_bounds_lethal() {
    auto kg = create_logotron_kg();
    ASSERT_TRUE(check_collision(*kg, 40.0f,  5.0f, 40.0f, 40.0f, NaNv, NaNv), "x==w lethal");
    ASSERT_TRUE(check_collision(*kg,  5.0f, 40.0f, 40.0f, 40.0f, NaNv, NaNv), "y==h lethal");
}

void test_in_bounds_no_trails_is_safe() {
    auto kg = create_logotron_kg();
    ASSERT_FALSE(check_collision(*kg, 20.0f, 20.0f, 40.0f, 40.0f, NaNv, NaNv),
                 "empty arena midfield safe");
}

void test_horizontal_trail_lethal_on_line_between_endpoints() {
    auto kg = create_logotron_kg();
    place_horizontal_run(*kg, 5.0f, 10.0f, 15.0f);  // along y=10, x in [5, 15]
    ASSERT_TRUE( check_collision(*kg, 10.0f, 10.0f, 40.0f, 40.0f, NaNv, NaNv), "on line midrange");
    ASSERT_TRUE( check_collision(*kg,  5.0f, 10.0f, 40.0f, 40.0f, NaNv, NaNv), "at start endpoint");
    ASSERT_TRUE( check_collision(*kg, 15.0f, 10.0f, 40.0f, 40.0f, NaNv, NaNv), "at end endpoint");
    ASSERT_FALSE(check_collision(*kg, 10.0f, 10.5f, 40.0f, 40.0f, NaNv, NaNv), "clearly perpendicular off");
    ASSERT_FALSE(check_collision(*kg, 20.0f, 10.0f, 40.0f, 40.0f, NaNv, NaNv), "past end along axis");
    ASSERT_FALSE(check_collision(*kg,  0.0f, 10.0f, 40.0f, 40.0f, NaNv, NaNv), "before start along axis");
}

void test_vertical_trail_lethal_on_line_between_endpoints() {
    auto kg = create_logotron_kg();
    place_vertical_run(*kg, 10.0f, 5.0f, 15.0f);  // along x=10, y in [5, 15]
    ASSERT_TRUE( check_collision(*kg, 10.0f, 10.0f, 40.0f, 40.0f, NaNv, NaNv), "on line");
    ASSERT_FALSE(check_collision(*kg, 10.5f, 10.0f, 40.0f, 40.0f, NaNv, NaNv), "perpendicular off");
    ASSERT_FALSE(check_collision(*kg, 10.0f, 20.0f, 40.0f, 40.0f, NaNv, NaNv), "past end");
}

void test_self_run_endpoint_suppresses_collision() {
    // When the cycle is riding out of a run it just froze, the
    // run's end equals the cycle's current run_start. That trail
    // should be ignored by the collision check so the cycle
    // doesn't crash into its own just-laid wall.
    auto kg = create_logotron_kg();
    place_horizontal_run(*kg, 5.0f, 10.0f, 10.0f);  // ends at (10, 10)
    ASSERT_TRUE( check_collision(*kg, 10.0f, 10.0f, 40.0f, 40.0f, NaNv, NaNv),
                 "without self-filter: lethal");
    ASSERT_FALSE(check_collision(*kg, 10.0f, 10.0f, 40.0f, 40.0f, 10.0f, 10.0f),
                 "with self-filter at that endpoint: safe");
}

void test_step_into_wall_crashes_cycle() {
    auto kg = create_logotron_kg();
    auto e = spawn_cycle(*kg, "PlayerCycle", 38.5f, 20.0f, Direction::EAST);
    for (int i = 0; i < 1000; i++) {
        step_cycle_in_kg_with_collision(*kg, e, 40.0f, 40.0f, 0.01f);
        if (read_cycle(*kg, e).state == CycleState::CRASHED) break;
    }
    Cycle c = read_cycle(*kg, e);
    ASSERT_TRUE(c.state == CycleState::CRASHED, "crashed on east wall");
    ASSERT_TRUE(c.x < 40.0f, "did not exit arena");
}

void test_step_into_pre_existing_trail_crashes_cycle() {
    auto kg = create_logotron_kg();
    auto e = spawn_cycle(*kg, "PlayerCycle", 5.0f, 5.0f, Direction::EAST);
    // Vertical trail at x=7 (perpendicular to cycle's heading).
    place_vertical_run(*kg, 7.0f, 3.0f, 10.0f);
    for (int i = 0; i < 1000; i++) {
        step_cycle_in_kg_with_collision(*kg, e, 40.0f, 40.0f, 0.01f);
        if (read_cycle(*kg, e).state == CycleState::CRASHED) break;
    }
    Cycle c = read_cycle(*kg, e);
    ASSERT_TRUE(c.state == CycleState::CRASHED, "crashed into trail");
    ASSERT_TRUE(c.x < 7.5f, "stopped before passing through");
}

void test_cycle_does_not_collide_with_its_own_just_frozen_trail() {
    auto kg = create_logotron_kg();
    auto e = spawn_cycle(*kg, "PlayerCycle", 5.0f, 5.0f, Direction::EAST);
    for (int i = 0; i < 50; i++) step_cycle_in_kg(*kg, e, 0.01f);
    freeze_run(*kg, e);
    // Cycle is now at ~(7.5, 5.0) and the just-frozen trail ends
    // there. Continue straight — the endpoint should not be
    // flagged against the current run_start.
    for (int i = 0; i < 50; i++) {
        step_cycle_in_kg_with_collision(*kg, e, 40.0f, 40.0f, 0.01f);
    }
    Cycle c = read_cycle(*kg, e);
    ASSERT_TRUE(c.state == CycleState::RIDING, "still riding after passing through own seam");
}

// Reported live: player rode straight through the AI's actively-
// drawing trail without crashing. The active (unsealed) run isn't a
// TrailSegment, so the old TrailSegment-only iteration missed it.
// This test spawns two cycles, never turns either, and drives one
// into the other's live run — expect CRASHED.
void test_cycle_crashes_against_other_cycles_active_run() {
    auto kg = create_logotron_kg();
    // AI cycle heading north along the x=20 column from y=5 to y=30.
    auto ai = spawn_cycle(*kg, "AICycle", 20.0f, 5.0f, Direction::NORTH);
    for (int i = 0; i < 250; ++i) step_cycle_in_kg(*kg, ai, 0.01f);
    // Player heading east at y=15; its straight path crosses x=20.
    auto player = spawn_cycle(*kg, "PlayerCycle", 5.0f, 15.0f, Direction::EAST);
    for (int i = 0; i < 300; ++i) {
        step_cycle_in_kg_with_collision(*kg, player, 40.0f, 40.0f, 0.01f);
        if (read_cycle(*kg, player).state == CycleState::CRASHED) break;
    }
    ASSERT_TRUE(read_cycle(*kg, player).state == CycleState::CRASHED,
        "player must crash against opponent's active (unsealed) run");
    // AI should still be riding — it hasn't hit anything.
    ASSERT_TRUE(read_cycle(*kg, ai).state == CycleState::RIDING,
        "AI untouched by player's collision");
}

// The self_cycle argument must exclude only the CALLER — a second,
// RIDING cycle's active run must still kill even if they're the same
// entity type. This is the mirror of the bug above: if I passed the
// opponent's id as self_cycle by mistake we'd skip the wrong run.
void test_self_cycle_excludes_only_caller() {
    auto kg = create_logotron_kg();
    auto a = spawn_cycle(*kg, "PlayerCycle", 5.0f, 15.0f, Direction::EAST);
    auto b = spawn_cycle(*kg, "PlayerCycle", 20.0f, 5.0f, Direction::NORTH);
    for (int i = 0; i < 250; ++i) step_cycle_in_kg(*kg, b, 0.01f);
    // Drive `a` toward b's column at x=20. Use the overload that
    // takes self_cycle and pass `a` explicitly — b's run should kill.
    for (int i = 0; i < 300; ++i) {
        Cycle ca = read_cycle(*kg, a);
        auto delta = delta_for(ca.direction);
        float nx = ca.x + static_cast<float>(delta.dx) * 2.5f * 0.01f;
        float ny = ca.y + static_cast<float>(delta.dy) * 2.5f * 0.01f;
        if (check_collision(*kg, nx, ny, 40.0f, 40.0f,
                            ca.run_start_x, ca.run_start_y, a)) {
            ca.state = CycleState::CRASHED;
            write_cycle(*kg, a, ca);
            break;
        }
        step_cycle_in_kg(*kg, a, 0.01f);
    }
    ASSERT_TRUE(read_cycle(*kg, a).state == CycleState::CRASHED,
        "caller (a) should crash against sibling b's live run");
}

// Once a cycle crashes, its already-frozen TrailSegments stop being
// lethal — the visual fade is a 2 s polish on top, but the gameplay
// rule is "dead duelist, dead wall". Without this the player would
// ride into an invisible wall after beating the AI.
void test_crashed_cycle_trails_are_not_lethal() {
    auto kg = create_logotron_kg();
    auto ai = spawn_cycle(*kg, "AICycle", 5.0f, 15.0f, Direction::EAST);
    // Drive the AI east, then freeze its run into a TrailSegment.
    for (int i = 0; i < 300; ++i) step_cycle_in_kg(*kg, ai, 0.01f);
    freeze_run(*kg, ai);
    // Sanity: the trail IS lethal while the AI is still RIDING.
    ASSERT_TRUE(check_collision(*kg, 10.0f, 15.0f, 40.0f, 40.0f, NaNv, NaNv),
                "live AI's frozen trail must be lethal");
    // Kill the AI via the real write_cycle path so this test fails
    // if either the production read or the test write drift apart on
    // the property name (the "crashed into nothing" bug from
    // 2026-04-21 was both sides writing/reading "state" while
    // write_cycle stored "cycle_state" — both wrong, mutually
    // confirming, mask the prod bug). Always go through the same
    // I/O the game actually uses.
    auto c = read_cycle(*kg, ai);
    c.state = CycleState::CRASHED;
    write_cycle(*kg, ai, c);
    ASSERT_FALSE(check_collision(*kg, 10.0f, 15.0f, 40.0f, 40.0f, NaNv, NaNv),
                 "crashed AI's frozen trail must NOT be lethal anymore");
}

// Regression for the 2026-04-21 "crashed into nothing" bug: a
// CRASHED cycle's frozen position still has (run_start, x, y) on
// it. The active-run scan must recognize CRASHED and skip, not
// treat the stale line as a still-drawing active run. Uses
// write_cycle() to guarantee state parity with production I/O.
void test_crashed_cycle_active_run_is_not_lethal() {
    auto kg = create_logotron_kg();
    auto ai = spawn_cycle(*kg, "AICycle", 5.0f, 15.0f, Direction::EAST);
    for (int i = 0; i < 300; ++i) step_cycle_in_kg(*kg, ai, 0.01f);
    // Kill the AI via the full cycle I/O path.
    auto c = read_cycle(*kg, ai);
    c.state = CycleState::CRASHED;
    write_cycle(*kg, ai, c);
    // The stale line from run_start → current_pos should NOT kill
    // a player passing through it.
    ASSERT_FALSE(check_collision(*kg, 7.0f, 15.0f, 40.0f, 40.0f, NaNv, NaNv),
                 "CRASHED cycle's stale active-run line must not be lethal");
}

// Tunneling regression: a fast cycle whose per-frame motion exceeds
// the trail's collision band must NOT skip past a perpendicular
// wall. The bug appeared when the speed-ramp lifted current_speed
// past ~5 m/s and a 16 ms tick moved the bike further than the
// 0.15 m collision band — point-based collision sampled past the
// wall, missed it. Fixed by substepping the integrator.
void test_high_speed_cycle_does_not_tunnel_through_trail() {
    auto kg = create_logotron_kg();
    // Vertical wall at x=15, y ∈ [13, 17] (perpendicular to an
    // east-bound cycle's path).
    place_vertical_run(*kg, 15.0f, 13.0f, 17.0f);
    // Spawn the cycle just shy of the wall's NEAR collision-band
    // edge (band is [15 - 0.075, 15 + 0.075]) and crank speed so a
    // single 60 FPS step would carry it past the FAR edge:
    //   start x = 14.93,  speed = 20 m/s,  dt = 1/60 s
    //   end x  = 14.93 + 20/60 = 15.263
    // Without substepping, point-collision at 15.263 misses the
    // band entirely → the bike teleports through the wall. With
    // substepping (sub_dt sized to keep each move ≤ half-thickness)
    // at least one intermediate point lands inside the band.
    auto e = spawn_cycle(*kg, "PlayerCycle", 14.93f, 15.0f, Direction::EAST);
    auto c = read_cycle(*kg, e);
    c.base_speed       = 20.0f;
    c.max_speed        = 20.0f;
    c.speed_ramp_rate  = 0.0f;
    c.current_speed    = 20.0f;
    c.run_start_x      = 14.93f;
    c.run_start_y      = 15.0f;
    write_cycle(*kg, e, c);
    step_cycle_in_kg_with_collision(*kg, e, 40.0f, 40.0f, 1.0f / 60.0f);
    auto after = read_cycle(*kg, e);
    ASSERT_TRUE(after.state == CycleState::CRASHED,
        "fast cycle must crash on a perpendicular wall, not tunnel");
    ASSERT_TRUE(after.x <= 15.2f,
        "fast cycle position must stop at or before the wall, not past it");
}

// Guard: the owner-state lookup must not accidentally mark LIVE
// opponent trails as non-lethal. Same setup as above but keep AI
// RIDING — collision should still fire.
void test_live_opponent_trails_stay_lethal() {
    auto kg = create_logotron_kg();
    auto ai = spawn_cycle(*kg, "AICycle", 5.0f, 15.0f, Direction::EAST);
    for (int i = 0; i < 300; ++i) step_cycle_in_kg(*kg, ai, 0.01f);
    freeze_run(*kg, ai);
    ASSERT_TRUE(check_collision(*kg, 10.0f, 15.0f, 40.0f, 40.0f, NaNv, NaNv),
                "live AI's frozen trail must remain lethal");
}

int main() {
    std::cout << "=== Logotron — Line-Segment Collision ===" << std::endl;
    TEST(negative_world_coords_are_lethal);
    TEST(at_or_past_world_bounds_lethal);
    TEST(in_bounds_no_trails_is_safe);
    TEST(horizontal_trail_lethal_on_line_between_endpoints);
    TEST(vertical_trail_lethal_on_line_between_endpoints);
    TEST(self_run_endpoint_suppresses_collision);
    TEST(step_into_wall_crashes_cycle);
    TEST(step_into_pre_existing_trail_crashes_cycle);
    TEST(cycle_does_not_collide_with_its_own_just_frozen_trail);
    TEST(cycle_crashes_against_other_cycles_active_run);
    TEST(self_cycle_excludes_only_caller);
    TEST(crashed_cycle_trails_are_not_lethal);
    TEST(crashed_cycle_active_run_is_not_lethal);
    TEST(live_opponent_trails_stay_lethal);
    TEST(high_speed_cycle_does_not_tunnel_through_trail);
    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
