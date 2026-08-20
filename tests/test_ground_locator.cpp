// Where can a body go? Asked, never assumed.
//
// Every scene here is one you could look at. Each builds ground at a
// height chosen to be inconvenient - never zero - because every bug
// this mechanism exists to kill came from code that knew where the
// ground was without looking:
//
//   serpents spawned at z = 0 under 0.55 m of strata, invisible
//   butterflies with a 0.1 m flight floor, swimming through soil
//   a ground search clamped to min_z = -1, blind to anything lower
//   a redwood planted at floor height, growing through a planet
//
// So no test here places ground at zero, and none of them tells the
// locator where to look.
//
// Usage:
//   ./build/test_ground_locator

#include "core/engine.h"
#include "core/particle_system.h"
#include "logosphere/core/ground_locator.h"
#include "particle.h"

#include <cmath>
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
    logosphere::GroundLocator locator;
    Harness() : engine(nullptr) {
        EngineConfig config;
        config.create_display = false;
        config.window_width = 640;
        config.window_height = 480;
        config.window_title = "ground";
        config.show_debug_overlay = false;
        config.enable_chat_window = false;
        if (engine.initialize(config) < 0)
            throw std::runtime_error("Engine::initialize() failed headless");
        locator.initialize(&engine.get_particle_system());
    }
    ~Harness() { engine.shutdown(); }

    // A slab of ground at an arbitrary height. Never zero.
    int slab(float x, float y, float top_z, float size = 2.0f,
             float thickness = 0.4f) {
        Particle p{};
        p.shape = ParticleShape::BOX;
        p.x = x; p.y = y; p.z = top_z - thickness * 0.5f;
        p.width = p.height = size;
        p.thickness = thickness;
        p.size = size;
        p.r = 0.5f; p.g = 0.45f; p.b = 0.4f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        p.solver_mode = ParticleSolverMode::KINEMATIC;
        p.is_at_rest = true;
        return engine.add_particle(p);
    }

    // Let the BVH see what was just added.
    void settle() {
        for (int i = 0; i < 3; ++i) engine.update(1.0 / 60.0);
    }
};

// The ground is wherever it is. The locator must find it without
// being told.
//
// One height IS knowable, and only one: the turtle, the world's
// absolute floor at z = 0. That is an invariant the engine enforces,
// not an assumption code may make - nothing exists below it, which is
// why this test does not look there. Every OTHER height must be
// discovered, and 0.55 / 8 / 42 below are chosen to be nothing a
// constant would ever have guessed.
void test_finds_ground_at_any_height() {
    for (float h : {0.55f, 8.0f, 42.0f}) {
        Harness hh;
        hh.slab(0.0f, 0.0f, h);
        hh.settle();

        float surface = -12345.0f;
        bool ok = hh.locator.surface_at(0.0f, 0.0f, surface);
        std::cout << "  [measure] ground at " << h << " -> found=" << ok
                  << " surface=" << surface << std::endl;
        CHECK(ok, "found ground placed at " + std::to_string(h));
        CHECK(std::fabs(surface - h) < 0.01f,
              "and reported its true height (" + std::to_string(surface) +
              " vs " + std::to_string(h) + ")");
    }
}

// The void is a legitimate answer. Guessing zero is not.
void test_the_void_reports_nothing() {
    Harness hh;
    hh.slab(50.0f, 50.0f, 3.0f);   // ground, but far away
    hh.settle();

    float surface = -12345.0f;
    bool ok = hh.locator.surface_at(0.0f, 0.0f, surface);
    CHECK(!ok, "no ground over empty space (reported " +
               std::to_string(surface) + ")");

    logosphere::PlacementRequest r;
    r.x = 0.0f; r.y = 0.0f; r.mode = logosphere::SupportMode::STANDING;
    auto p = hh.locator.locate(r);
    CHECK(!p.found, "and placement fails rather than inventing a height");
    CHECK(std::string(p.reason).find("nothing underfoot") != std::string::npos,
          "with a reason a caller can act on: '" + std::string(p.reason) + "'");
}

// A body stands ON the surface; a flier holds its height ABOVE it.
// Both are relative to what is actually there.
void test_standing_and_flying_are_relative_to_the_surface() {
    Harness hh;
    const float ground = 0.55f;    // Eden's real strata height
    hh.slab(0.0f, 0.0f, ground);
    hh.settle();

    logosphere::PlacementRequest stand;
    stand.x = stand.y = 0.0f;
    stand.height = 1.8f;
    stand.mode = logosphere::SupportMode::STANDING;
    auto sp = hh.locator.locate(stand);
    CHECK(sp.found && std::fabs(sp.z - ground) < 0.01f,
          "a standing body sits on the surface, not at zero (z " +
          std::to_string(sp.z) + ")");

    logosphere::PlacementRequest fly = stand;
    fly.mode = logosphere::SupportMode::FLYING;
    fly.clearance = 2.0f;
    fly.height = 0.2f;
    auto fp = hh.locator.locate(fly);
    std::cout << "  [measure] ground " << ground << " -> stand z=" << sp.z
              << " fly z=" << fp.z << std::endl;
    CHECK(fp.found && std::fabs(fp.z - (ground + 2.0f)) < 0.01f,
          "a flier holds its clearance ABOVE the surface (z " +
          std::to_string(fp.z) + ", expected " +
          std::to_string(ground + 2.0f) + ")");
    // The bug this kills: a flight floor of 0.1 absolute would put
    // this butterfly 0.45 m UNDER the ground.
    CHECK(fp.z > ground,
          "and never below it — an absolute flight floor is how "
          "butterflies ended up swimming through soil");
}

// Rooting is not standing. A tree needs real ground, not a plank.
void test_rooting_demands_more_than_standing() {
    Harness hh;
    // A small ledge: big enough to stand on, too small to root in.
    hh.slab(0.0f, 0.0f, 4.0f, /*size=*/0.3f);
    hh.settle();

    logosphere::PlacementRequest stand;
    stand.x = stand.y = 0.0f;
    stand.footprint = 0.2f;
    stand.mode = logosphere::SupportMode::STANDING;
    auto sp = hh.locator.locate(stand);
    CHECK(sp.found, "a creature can stand on a small ledge");

    logosphere::PlacementRequest root = stand;
    root.mode = logosphere::SupportMode::ROOTED;
    auto rp = hh.locator.locate(root);
    CHECK(!rp.found,
          "but a tree cannot root in it (got z " + std::to_string(rp.z) + ")");
    CHECK(std::string(rp.reason).find("root") != std::string::npos,
          "and says why: '" + std::string(rp.reason) + "'");
}

// Ground alone is not enough: the space above must be free. This is
// the "surrounding space" question - a spot can have perfect footing
// and still be unusable.
void test_headroom_is_part_of_the_answer() {
    Harness hh;
    hh.slab(0.0f, 0.0f, 2.0f);              // floor
    hh.slab(0.0f, 0.0f, 3.2f);              // ceiling 1.2 m above it
    hh.settle();

    logosphere::PlacementRequest small;
    small.x = small.y = 0.0f;
    small.height = 0.5f;
    small.mode = logosphere::SupportMode::STANDING;
    auto sp = hh.locator.locate(small);

    logosphere::PlacementRequest tall = small;
    tall.height = 6.0f;                      // a tree that will not fit
    auto tp = hh.locator.locate(tall);

    std::cout << "  [measure] under a ledge: short body found=" << sp.found
              << " z=" << sp.z << ", tall body found=" << tp.found
              << " (" << tp.reason << ")" << std::endl;

    // The short body fits under the ledge, or stands on top of it;
    // either is a real surface. What matters is that it found one.
    CHECK(sp.found, "a short body finds somewhere to be");
    // The tall body must not be placed in a gap it cannot occupy.
    CHECK(!tp.found || tp.z >= 3.2f,
          "a tall body is never placed in a gap too small for it "
          "(z " + std::to_string(tp.z) + ")");
}

// A falling boulder is not ground while it falls, and a pebble is not
// ground at all. Both are physical judgements, not type checks.
void test_only_still_and_substantial_things_are_ground() {
    {
        Harness hh;
        int id = hh.slab(0.0f, 0.0f, 5.0f);
        {   // shove it: now it is falling debris, not floor
            auto w = hh.engine.get_particle_system().lock_particles_for_write();
            auto& all = w.get_particles();
            all[id].solver_mode = ParticleSolverMode::DYNAMIC;
            all[id].is_at_rest = false;
            all[id].vz = -4.0f;
        }
        float surface = 0.0f;
        bool ok = hh.locator.surface_at(0.0f, 0.0f, surface);
        CHECK(!ok, "a falling slab is not ground while it falls");
    }
    {
        Harness hh;
        hh.slab(0.0f, 0.0f, 5.0f, /*size=*/0.05f);   // a pebble
        hh.settle();
        float surface = 0.0f;
        bool ok = hh.locator.surface_at(0.0f, 0.0f, surface);
        CHECK(!ok, "a pebble is not ground");
    }
}

// Litter does not hide the floor it is lying on.
//
// Two rules were inconsistent. A span smaller than MIN_SUPPORT_SIZE
// was rejected as ground, correctly: you cannot stand on a twig. But
// the blocking pass then let a span of ANY size veto the surface
// beneath it, so the same twig made the floor unusable. A walker
// crossing debris reported no ground under her while standing on it,
// at 27% of the path against the 95% the terrain scenarios ask for.
//
// Too small to stand on is too small to stand in the way.
void test_litter_does_not_hide_the_floor() {
    {
        Harness hh;
        hh.slab(0.0f, 0.0f, 1.0f);                    // the floor
        // A pebble resting on it, overlapping the surface plane the
        // way settled debris does.
        hh.slab(0.0f, 0.0f, 1.10f, /*size=*/0.12f, /*thickness=*/0.12f);
        hh.settle();
        float surface = 0.0f;
        const bool ok = hh.locator.surface_at(0.0f, 0.0f, surface);
        CHECK(ok, "a pebble on the floor does not hide the floor");
        CHECK(std::fabs(surface - 1.0f) < 0.01f,
              "and the floor is still where it was");
    }
    {
        // The control, and the reason this is a size test and not a
        // blanket removal: something big enough to stand on is big
        // enough to block. A slab overhead is a ledge, not litter.
        Harness hh;
        hh.slab(0.0f, 0.0f, 1.0f);
        hh.slab(0.0f, 0.0f, 1.30f);                   // full-size, overhead
        hh.settle();
        logosphere::PlacementRequest req;
        req.x = req.y = 0.0f;
        req.footprint = 0.6f;
        req.height = 1.75f;                           // a standing body
        req.mode = logosphere::SupportMode::STANDING;
        const logosphere::Placement p = hh.locator.locate(req);
        CHECK(!p.found || std::fabs(p.surface_z - 1.30f) < 0.01f,
              "a full-size slab overhead still blocks the floor under it "
              "(a body of 1.75 m does not fit in 0.30 m)");
    }
}

// A body must not mistake itself for the floor it stands on.
void test_a_body_does_not_stand_on_itself() {
    Harness hh;
    hh.slab(0.0f, 0.0f, 1.75f);
    int self = hh.slab(0.0f, 0.0f, 4.0f);   // the body, higher up
    hh.settle();

    std::vector<unsigned int> mine{static_cast<unsigned int>(self)};
    float surface = 0.0f;
    bool ok = hh.locator.surface_at(0.0f, 0.0f, surface, 0.5f, &mine);
    std::cout << "  [measure] ignoring self, surface=" << surface
              << " (expected 1.75)" << std::endl;
    CHECK(ok && std::fabs(surface - 1.75f) < 0.01f,
          "found the ground beneath, not itself (" +
          std::to_string(surface) + ")");
}

}  // namespace

int main() {
    std::cout << "Ground locator (ask, never assume)" << std::endl;
    test_finds_ground_at_any_height();
    test_the_void_reports_nothing();
    test_standing_and_flying_are_relative_to_the_surface();
    test_rooting_demands_more_than_standing();
    test_headroom_is_part_of_the_answer();
    test_only_still_and_substantial_things_are_ground();
    test_a_body_does_not_stand_on_itself();
    test_litter_does_not_hide_the_floor();
    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
