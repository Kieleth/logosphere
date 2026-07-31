// Logotron — AI driver tactics + head rotation + perception contract.
//
// These tests lock in the driver-confinement contract: tactics see
// only a PerceivedWorld snapshot and never read the KG. They also
// cover head-rotation rate limiting and the KG-integrating
// build_perceived_world path (occlusion, cone filtering).

#include "ai/decide.h"
#include "ai/head.h"
#include "ai/perception.h"
#include "ai/personality.h"
#include "ai/tactic.h"
#include "ai/tactics/avoid_imminent_crash.h"
#include "ai/tactics/chase_seen_opponent.h"
#include "ai/tactics/maintain_direction.h"
#include "arena.h"
#include "cycle.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"
#include "logotron_ontology_registry.h"
#include "generated/logosphere_ontology_registry.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

using namespace logotron;
using namespace logotron::ai;

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    std::cout << "  " #name "... "; \
    try { test_##name(); tests_passed++; std::cout << "PASS" << std::endl; } \
    catch (const std::exception& e) { tests_failed++; std::cout << "FAIL: " << e.what() << std::endl; }

#define ASSERT_TRUE(cond, msg) \
    if (!(cond)) throw std::runtime_error(std::string(msg))
#define ASSERT_FALSE(cond, msg) \
    if ((cond)) throw std::runtime_error(std::string(msg))
#define ASSERT_NEAR(a, b, tol, msg) \
    if (std::fabs((a) - (b)) > (tol)) throw std::runtime_error( \
        std::string(msg) + " [got=" + std::to_string(a) + " want=" + std::to_string(b) + "]")

static constexpr float kPi = 3.14159265358979323846f;

// -------------------------- head rotation --------------------------

void test_head_rotation_is_rate_limited() {
    HeadState h;
    h.current_yaw = 0.0f;
    h.target_yaw  = kPi * 0.5f;  // want to look +90°
    h.max_rate    = 1.0f;        // 1 rad/s
    step_head(h, 0.1f);          // 0.1 s → can move 0.1 rad
    ASSERT_NEAR(h.current_yaw, 0.1f, 1e-5f,
        "single 0.1s step must advance 0.1 rad exactly");
}

void test_head_rotation_snaps_at_arrival() {
    HeadState h{0.0f, 0.01f, 10.0f};  // 0.01 rad away, max_rate 10 rad/s
    step_head(h, 0.1f);                // 1.0 rad budget — way past target
    ASSERT_NEAR(h.current_yaw, 0.01f, 1e-6f,
        "head snaps to target when step exceeds remaining delta");
}

void test_head_rotation_wraps_shortest_arc() {
    // From +170° to -170° should go through +180° (20°), not 340° back.
    HeadState h{+170.0f * kPi / 180.0f,
                 -170.0f * kPi / 180.0f,
                 1.0f};
    step_head(h, 0.1f);  // budget = 0.1 rad
    // Shortest arc is +0.349 rad (= 20°); delta is POSITIVE (CCW→CW wrap).
    // After one step we should have moved +0.1 rad.
    float expected = h.current_yaw;  // captured post-step
    // Reset and verify expected = original + 0.1
    HeadState h2{+170.0f * kPi / 180.0f,
                  -170.0f * kPi / 180.0f,
                  1.0f};
    step_head(h2, 0.1f);
    float delta = shortest_arc_delta(+170.0f * kPi / 180.0f, h2.current_yaw);
    ASSERT_NEAR(delta, 0.1f, 1e-5f,
        "shortest-arc step advances 0.1 rad along the short way through +π");
    (void)expected;
}

// -------------------------- tactics --------------------------

static PerceivedWorld make_blank_pw() {
    PerceivedWorld pw;
    pw.self.direction = Direction::NORTH;
    pw.self.x = 10.0f; pw.self.y = 10.0f;
    pw.self.run_start_x = 10.0f; pw.self.run_start_y = 10.0f;
    // All open.
    for (int i = 0; i < 4; ++i) pw.lethal_distance[i] = PerceivedWorld::INFINITY_F;
    for (int i = 0; i < 4; ++i) pw.arena_edge_distance[i] = 30.0f;
    pw.head.current_yaw = 0.0f;
    pw.head.target_yaw  = 0.0f;
    pw.head.max_rate    = 4.0f;
    return pw;
}

void test_avoid_crash_hard_punishes_near_wall() {
    AvoidImminentCrashTactic t;
    auto pw = make_blank_pw();
    pw.lethal_distance[(int)DirIndex::NORTH] = 1.0f;  // 1 m to doom
    float s = t.score(Direction::NORTH, pw);
    ASSERT_TRUE(s < -0.8f,
        "1 m to lethal hit must score below -0.8 [got=" + std::to_string(s) + "]");
}

void test_avoid_crash_neutral_when_clear() {
    AvoidImminentCrashTactic t;
    auto pw = make_blank_pw();
    float s = t.score(Direction::NORTH, pw);
    ASSERT_TRUE(s >= -0.01f && s <= 0.01f,
        "clear open lookahead must score ~0 [got=" + std::to_string(s) + "]");
}

void test_chase_is_neutral_when_opponent_invisible() {
    ChaseSeenOpponentTactic t;
    auto pw = make_blank_pw();
    ASSERT_FALSE(pw.opponent_rider.has_value(), "opponent must be invisible");
    for (auto d : {Direction::NORTH, Direction::EAST, Direction::SOUTH, Direction::WEST}) {
        float s = t.score(d, pw);
        ASSERT_NEAR(s, 0.0f, 1e-6f,
            "no cheating: chase tactic scores 0 when opponent not visible");
    }
}

void test_chase_closes_distance_when_opponent_visible() {
    ChaseSeenOpponentTactic t;
    auto pw = make_blank_pw();
    // Opponent to the east (+X).
    SeenPoint op;
    op.x = pw.self.x + 5.0f; op.y = pw.self.y;
    op.distance = 5.0f; op.angle_offset = kPi * 0.5f;
    pw.opponent_rider = op;
    float s_east = t.score(Direction::EAST, pw);
    float s_west = t.score(Direction::WEST, pw);
    ASSERT_TRUE(s_east > s_west,
        "east must score higher than west when opponent is east");
    ASSERT_TRUE(s_east > 0.0f, "east should be positive");
    ASSERT_TRUE(s_west < 0.0f, "west should be negative");
}

// -------------------------- decide --------------------------

void test_decide_never_returns_180_reversal() {
    // Synthesize a scenario where EVERY direction except a 180°
    // reversal is terrible, yet the reversal should still be
    // excluded via is_legal_turn.
    auto pw = make_blank_pw();
    pw.self.direction = Direction::NORTH;
    pw.lethal_distance[(int)DirIndex::NORTH] = 0.5f;
    pw.lethal_distance[(int)DirIndex::EAST]  = 0.5f;
    pw.lethal_distance[(int)DirIndex::WEST]  = 0.5f;
    pw.lethal_distance[(int)DirIndex::SOUTH] = 20.0f;  // only clear way
    Personality p = default_personality();
    auto d = decide(p, pw);
    ASSERT_TRUE(d.drive != Direction::SOUTH,
        "decide() must NOT pick 180° reversal even when it looks safest");
}

void test_decide_ties_prefer_current_direction() {
    // All dirs equally open and opponent invisible. Only the tiny
    // Maintain bias should tip the scale toward the current dir.
    auto pw = make_blank_pw();
    pw.self.direction = Direction::EAST;
    Personality p = default_personality();
    auto d = decide(p, pw);
    ASSERT_TRUE(d.drive == Direction::EAST,
        "tied scores must prefer current direction via Maintain bias");
}

// -------------------------- perception KG integration --------------------------

static std::unique_ptr<kg::KGModule> make_kg() {
    auto kg = std::make_unique<kg::KGModule>(logosphere::ontology::registry());
    kg->setMode(kg::KGMode::MINIMAL);
    kg->extendOntology(logotron::ontology::registry());
    return kg;
}

void test_opponent_behind_trail_is_occluded() {
    auto kg = make_kg();
    // AI at (10, 10) looking north (+Y). Opponent at (10, 15).
    // A horizontal trail at y=12 from x=5 to x=15 blocks the view.
    auto ai = spawn_cycle(*kg, "AICycle", 10.0f, 10.0f, Direction::NORTH);
    auto pl = spawn_cycle(*kg, "PlayerCycle", 10.0f, 15.0f, Direction::NORTH);
    (void)pl;
    // Place a sealed TrailSegment by hand.
    auto trail = kg->createEntity("TrailSegment");
    kg->setProperty(trail, "start_x", "5");
    kg->setProperty(trail, "start_y", "12");
    kg->setProperty(trail, "end_x",   "15");
    kg->setProperty(trail, "end_y",   "12");
    kg->setProperty(trail, "direction", "EAST");
    kg->setProperty(trail, "trail_state", "SOLID");
    // Owner: a bogus third entity so the "live-opponent active run"
    // code doesn't pick this up as someone's run_start→x path.
    auto ghost = kg->createEntity("ArenaWall");
    kg->setProperty(trail, "owner_cycle_id", std::to_string(ghost));

    HeadState head{0.0f, 0.0f, 4.0f};  // looking north
    auto pw = build_perceived_world(*kg, ai, head, kPi, 20.0f, 40.0f, 40.0f);
    ASSERT_FALSE(pw.opponent_rider.has_value(),
        "opponent 5 m north must be occluded by trail at y=12");
}

void test_opponent_outside_cone_is_invisible() {
    auto kg = make_kg();
    // AI at (20, 20) head facing NORTH. Opponent directly SOUTH.
    auto ai = spawn_cycle(*kg, "AICycle", 20.0f, 20.0f, Direction::NORTH);
    auto pl = spawn_cycle(*kg, "PlayerCycle", 20.0f, 15.0f, Direction::NORTH);
    (void)pl;
    HeadState head{0.0f, 0.0f, 4.0f};  // looking NORTH (yaw=0)
    auto pw = build_perceived_world(*kg, ai, head, 2.0944f /*120°*/,
                                    20.0f, 40.0f, 40.0f);
    ASSERT_FALSE(pw.opponent_rider.has_value(),
        "opponent directly behind rider (180° off head yaw) must be invisible");
}

void test_head_rotation_reveals_previously_hidden_opponent() {
    auto kg = make_kg();
    // Opponent to the east (+X); head starts facing NORTH (yaw=0).
    auto ai = spawn_cycle(*kg, "AICycle", 20.0f, 20.0f, Direction::NORTH);
    auto pl = spawn_cycle(*kg, "PlayerCycle", 25.0f, 20.0f, Direction::NORTH);
    (void)pl;
    // Cone 60° → opponent at angle +π/2 from head-NORTH is outside.
    HeadState head{0.0f, 0.0f, 4.0f};
    auto pw1 = build_perceived_world(*kg, ai, head, 1.0472f /*60°*/,
                                     20.0f, 40.0f, 40.0f);
    ASSERT_FALSE(pw1.opponent_rider.has_value(),
        "head NORTH + 60° cone: opponent to EAST must be outside");
    head.current_yaw = kPi * 0.5f;  // rotate to face EAST
    auto pw2 = build_perceived_world(*kg, ai, head, 1.0472f,
                                     20.0f, 40.0f, 40.0f);
    ASSERT_TRUE(pw2.opponent_rider.has_value(),
        "head EAST + 60° cone: opponent to EAST becomes visible");
}

// -------------------------- main --------------------------

int main() {
    std::cout << "=== Logotron — AI driver tactics + perception ===" << std::endl;
    TEST(head_rotation_is_rate_limited);
    TEST(head_rotation_snaps_at_arrival);
    TEST(head_rotation_wraps_shortest_arc);
    TEST(avoid_crash_hard_punishes_near_wall);
    TEST(avoid_crash_neutral_when_clear);
    TEST(chase_is_neutral_when_opponent_invisible);
    TEST(chase_closes_distance_when_opponent_visible);
    TEST(decide_never_returns_180_reversal);
    TEST(decide_ties_prefer_current_direction);
    TEST(opponent_behind_trail_is_occluded);
    TEST(opponent_outside_cone_is_invisible);
    TEST(head_rotation_reveals_previously_hidden_opponent);
    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
