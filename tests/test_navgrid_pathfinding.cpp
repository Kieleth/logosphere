// A* grid pathfinding: the other half of the engine's NPC layer.
//
// src/npc-ai/pathfinding_system.{h,cpp} is compiled into the engine and
// had no tests. GOAP decides WHAT an NPC does; this decides HOW it gets
// there. Between them they are the whole of the engine's contribution
// to creature movement, and neither was guarded.
//
// Pure std, so it runs in the headless profile alongside the planner.
//
// Written to find out what it DOES. Several of these cases are ones a
// game reaches for immediately and where an untested implementation is
// most likely to be wrong: a goal inside a wall, a start inside a wall,
// coordinates off the edge of the grid.
//
// Usage:
//   ./build/test_navgrid_pathfinding

#include "npc-ai/pathfinding_system.h"

#include <cmath>
#include <iostream>
#include <string>

static int tests_passed = 0;
static int tests_failed = 0;
static int tests_xfailed = 0;

static void check(bool ok, const std::string& msg, bool xfail = false) {
    if (ok) { tests_passed++; }
    else if (xfail) {
        tests_xfailed++;
        std::cout << "  XFAIL: " << msg << "  (known-red, not gating)"
                  << std::endl;
    } else {
        tests_failed++;
        std::cout << "  FAIL: " << msg << std::endl;
    }
}

namespace {

using pathfinding::NavGrid;
using pathfinding::Path;
using pathfinding::Pathfinder;
using pathfinding::Point;

// A 20x20 m field of 1 m cells, origin at the corner.
NavGrid open_field() {
    NavGrid g;
    g.init(0.0f, 0.0f, 20.0f, 20.0f, 1.0f);
    return g;
}

// ---------------------------------------------------------------- grid

void test_the_grid_covers_what_it_was_asked_to() {
    NavGrid g = open_field();
    std::cout << "  [measure] 20x20 m at 1 m cells -> " << g.width() << "x"
              << g.height() << " cells, cell_size " << g.cell_size()
              << std::endl;
    check(g.width() >= 20 && g.height() >= 20,
          "the grid spans the requested area (" +
          std::to_string(g.width()) + "x" + std::to_string(g.height()) + ")");
    check(g.cell_size() == 1.0f, "and remembers its cell size");

    NavGrid coarse;
    coarse.init(0.0f, 0.0f, 20.0f, 20.0f, 2.0f);
    std::cout << "  [measure] same area at 2 m cells -> " << coarse.width()
              << "x" << coarse.height() << std::endl;
    check(coarse.width() < g.width(),
          "a coarser cell size makes a smaller grid");
}

void test_blocking_and_clearing() {
    NavGrid g = open_field();
    check(g.is_walkable(5.0f, 5.0f), "open ground is walkable to begin with");

    g.set_blocked(5.0f, 5.0f, true);
    check(!g.is_walkable(5.0f, 5.0f), "a blocked cell is not walkable");
    check(g.is_walkable(6.0f, 5.0f), "and its neighbour is untouched");

    g.set_blocked(5.0f, 5.0f, false);
    check(g.is_walkable(5.0f, 5.0f), "unblocking restores it");

    g.set_blocked_rect(2.0f, 2.0f, 4.0f, 4.0f, true);
    check(!g.is_walkable(3.0f, 3.0f), "a blocked rectangle blocks its middle");
    check(g.is_walkable(10.0f, 10.0f), "and leaves the rest of the field");

    g.clear();
    check(g.is_walkable(3.0f, 3.0f), "clear() reopens everything");
}

// Out-of-bounds is a question every caller hits the moment an NPC walks
// to the edge of its grid. It must answer, not crash.
void test_out_of_bounds_is_answered_not_crashed() {
    NavGrid g = open_field();
    const bool far_out = g.is_walkable(1000.0f, 1000.0f);
    const bool negative = g.is_walkable(-50.0f, -50.0f);
    std::cout << "  [measure] is_walkable off-grid: (1000,1000)=" << far_out
              << " (-50,-50)=" << negative << std::endl;
    check(!far_out, "a point far outside the grid is not walkable");
    check(!negative, "nor is one before its origin");
    g.set_blocked(1000.0f, 1000.0f, true);   // must not corrupt anything
    check(g.is_walkable(5.0f, 5.0f),
          "blocking an off-grid cell does not damage the grid");
}

// ---------------------------------------------------------------- paths

void test_a_straight_run_across_open_ground() {
    NavGrid g = open_field();
    Pathfinder pf;
    Path p = pf.find_path(g, Point(1.0f, 1.0f), Point(10.0f, 1.0f));

    const float straight = Point(1.0f, 1.0f).distance_to(Point(10.0f, 1.0f));
    std::cout << "  [measure] open ground: valid=" << p.valid
              << " waypoints=" << p.size() << " length=" << p.total_length()
              << " (straight line " << straight << ")" << std::endl;

    check(p.valid, "a path exists across open ground");
    check(!p.empty(), "and it has waypoints");
    // A* on a grid cannot beat the straight line, and should not wander
    // far past it either.
    check(p.total_length() >= straight - 1.5f,
          "it is not shorter than a straight line");
    check(p.total_length() <= straight * 1.6f + 2.0f,
          "and does not ramble (" + std::to_string(p.total_length()) +
          " vs " + std::to_string(straight) + ")");
}

void test_it_goes_around_a_wall() {
    NavGrid g = open_field();
    // A wall across the middle with a gap at the top.
    g.set_blocked_rect(9.0f, 0.0f, 10.0f, 15.0f, true);

    Pathfinder pf;
    Path p = pf.find_path(g, Point(2.0f, 2.0f), Point(18.0f, 2.0f));
    const float straight = 16.0f;
    std::cout << "  [measure] wall with a gap: valid=" << p.valid
              << " length=" << p.total_length()
              << " (straight line would be " << straight << ")" << std::endl;

    check(p.valid, "a way around is found");
    check(p.total_length() > straight,
          "and it is longer than the blocked straight line (" +
          std::to_string(p.total_length()) + ")");

    // Every waypoint must stand on walkable ground, or the path is a
    // suggestion to walk into a wall.
    bool all_clear = true;
    for (const Point& w : p.waypoints)
        if (!g.is_walkable(w.x, w.y)) all_clear = false;
    check(all_clear, "every waypoint is on walkable ground");
}

void test_a_sealed_goal_has_no_path() {
    NavGrid g = open_field();
    // Box the goal in completely.
    g.set_blocked_rect(14.0f, 14.0f, 18.0f, 18.0f, true);
    g.set_blocked_rect(15.0f, 15.0f, 17.0f, 17.0f, false);   // hollow inside

    Pathfinder pf;
    Path p = pf.find_path(g, Point(2.0f, 2.0f), Point(16.0f, 16.0f));
    std::cout << "  [measure] sealed room: valid=" << p.valid
              << " waypoints=" << p.size() << std::endl;
    check(!p.valid || p.empty(),
          "a goal sealed behind walls yields no path");
}

// A start inside a wall happens constantly in a real game: a creature
// is nudged into geometry, or the grid is rebuilt under it.
void test_a_start_inside_a_wall() {
    NavGrid g = open_field();
    g.set_blocked_rect(2.0f, 2.0f, 6.0f, 6.0f, true);

    Pathfinder pf;
    Path p = pf.find_path(g, Point(4.0f, 4.0f), Point(15.0f, 15.0f));
    std::cout << "  [measure] start inside a wall: valid=" << p.valid
              << " waypoints=" << p.size() << std::endl;
    // Either answer is defensible; what matters is that it answers.
    // The check is that it did not hang or crash getting here.
    check(true, "asking from inside a wall returns rather than hanging");
    if (p.valid && !p.empty()) {
        bool escapes = g.is_walkable(p.waypoints.back().x,
                                     p.waypoints.back().y);
        check(escapes, "and if it returns a path, it ends somewhere walkable");
    }
}

void test_start_equals_goal() {
    NavGrid g = open_field();
    Pathfinder pf;
    Path p = pf.find_path(g, Point(5.0f, 5.0f), Point(5.0f, 5.0f));
    std::cout << "  [measure] start == goal: valid=" << p.valid
              << " waypoints=" << p.size() << " length=" << p.total_length()
              << std::endl;
    check(p.total_length() < 2.0f,
          "going nowhere costs nothing much (" +
          std::to_string(p.total_length()) + ")");
}

// Diagonal movement is a switch on the Pathfinder. Turning it off must
// actually change the route, or the switch is decorative.
void test_diagonal_movement_is_a_real_switch() {
    NavGrid g = open_field();
    Pathfinder diag, ortho;
    ortho.set_allow_diagonal(false);

    Path pd = diag.find_path(g, Point(1.0f, 1.0f), Point(11.0f, 11.0f));
    Path po = ortho.find_path(g, Point(1.0f, 1.0f), Point(11.0f, 11.0f));

    std::cout << "  [measure] corner to corner: diagonal length "
              << pd.total_length() << ", orthogonal " << po.total_length()
              << std::endl;
    check(pd.valid && po.valid, "both settings find a path");
    check(po.total_length() >= pd.total_length() - 0.01f,
          "orthogonal-only is never shorter than diagonal (" +
          std::to_string(po.total_length()) + " vs " +
          std::to_string(pd.total_length()) + ")");
}

// ---------------------------------------------------------------- walking

void test_a_path_is_walked_front_to_back() {
    NavGrid g = open_field();
    Pathfinder pf;
    Path p = pf.find_path(g, Point(1.0f, 1.0f), Point(8.0f, 1.0f));
    check(p.valid && !p.empty(), "a path to walk");

    const size_t n = p.size();
    const Point first = p.current();
    p.advance();
    check(p.size() == n - 1, "advance consumes one waypoint");
    if (!p.empty()) {
        check(!(p.current().x == first.x && p.current().y == first.y),
              "and the next waypoint is a different place");
    }
    while (!p.empty()) p.advance();
    check(p.empty(), "the path runs out");
    p.advance();                      // must be harmless
    check(p.empty(), "advancing past the end is harmless");
    const Point nowhere = p.current();
    check(nowhere.x == 0.0f && nowhere.y == 0.0f,
          "and current() on an empty path is the documented (0,0)");
}

}  // namespace

int main() {
    std::cout << "A* grid pathfinding (src/npc-ai/pathfinding_system)"
              << std::endl;
    test_the_grid_covers_what_it_was_asked_to();
    test_blocking_and_clearing();
    test_out_of_bounds_is_answered_not_crashed();
    test_a_straight_run_across_open_ground();
    test_it_goes_around_a_wall();
    test_a_sealed_goal_has_no_path();
    test_a_start_inside_a_wall();
    test_start_equals_goal();
    test_diagonal_movement_is_a_real_switch();
    test_a_path_is_walked_front_to_back();

    std::cout << tests_passed << " passed, " << tests_failed << " failed";
    if (tests_xfailed)
        std::cout << ", " << tests_xfailed << " known-red (not gating)";
    std::cout << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
