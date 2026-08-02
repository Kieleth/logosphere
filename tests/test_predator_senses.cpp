// A predator's senses: what it can and cannot know.
//
// src/sense_system.{h,cpp} is compiled into the engine and had no
// tests. Its own header says "Core sensing in logosphere (reusable),
// game config in logomancers", which is the right split, so this
// guards the reusable half with a creature that belongs to no genre:
// a predator, some prey, a rock in the way.
//
// The behaviour is lifted from the shambler demos in logomancers
// rather than the shambler itself. Those eight files turned out to
// have ZERO assertions between them and an infinite loop each: they
// are watchable demos, not tests, and exit 0 whether the creature
// hunted perfectly or stood still. What they encode that IS worth
// keeping is the scenarios, so the scenarios are what came across.
//
// The property that matters most here is the one a shortcut destroys.
// A creature that reads its target's position straight out of the
// particle array knows everything: through walls, from any distance,
// facing the other way. Senses are what make a creature's knowledge
// smaller than the world, and smaller knowledge is the whole reason
// hiding, stalking and ambush exist. Each test below is a thing the
// predator must NOT be able to know.
//
// Usage:
//   ./build/test_predator_senses

#include "core/engine.h"
#include "core/particle_system.h"
#include "sense_system.h"
#include "particle.h"

#include <cmath>
#include <cstdlib>
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
               std::cout << "  FAIL: " << msg << std::endl; }           \
    } while (0)

namespace {

constexpr float PI_F = 3.14159265358979f;

// Facing convention, from CLAUDE.md: yaw 0 is +Y (north), +pi/2 is +X
// (east), clockwise viewed from +Z.
constexpr float NORTH = 0.0f;
constexpr float SOUTH = PI_F;

struct World {
    Engine engine;
    SenseSystem senses;

    World() : engine(nullptr) {
        EngineConfig cfg;
        cfg.create_display = false;
        cfg.window_width = 640;
        cfg.window_height = 480;
        cfg.window_title = "predator senses";
        cfg.show_debug_overlay = false;
        cfg.enable_chat_window = false;
        if (engine.initialize(cfg) < 0)
            throw std::runtime_error("Engine::initialize() failed headless");
    }
    ~World() { engine.shutdown(); }

    // Something to be seen or smelled.
    // 2 m across, deliberately. cast_vision_cone SAMPLES the cone with
    // a fixed number of rays: 32 rays over 100 degrees is 3.2 degrees
    // apart, which at 10 m is 0.56 m between neighbouring rays. A 0.5 m
    // animal falls BETWEEN the rays and is invisible at that range,
    // which is a property of the sensor rather than a bug, but it makes
    // a 0.5 m target a coin toss and a coin toss is not a test.
    //
    // Height matters for the same reason: of the six pitch angles, one
    // is horizontal and five aim DOWNWARD ({0, -0.15 ... -0.65}), tuned
    // by their own comment for "small ground objects" seen from a
    // standing creature. Prey at the viewer's eye height is served by
    // exactly one of the six.
    int prey(float x, float y, float z = 1.0f,
             OdorType odor = OdorType::LIVING_FLESH) {
        Particle p{};
        p.shape = ParticleShape::SPHERE;
        p.x = x; p.y = y; p.z = z;
        p.width = p.height = p.thickness = 2.0f;
        p.size = 2.0f;
        p.r = 0.8f; p.g = 0.3f; p.b = 0.3f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::FLESH);
        p.odor_type = odor;
        // Both are required, and both default to 0. check_smell skips
        // any particle with odor_radius <= 0, so an odour type alone
        // emits nothing at all.
        p.odor_radius = 20.0f;
        p.odor_intensity = 1.0f;
        p.solver_mode = ParticleSolverMode::KINEMATIC;
        p.is_at_rest = true;
        return engine.add_particle(p);
    }

    // Something to hide behind. Odourless, so it cannot be confused
    // with prey by the smell test.
    int boulder(float x, float y, float w, float d, float h) {
        Particle p{};
        p.shape = ParticleShape::BOX;
        p.x = x; p.y = y; p.z = h * 0.5f;
        p.width = w; p.height = d; p.thickness = h;
        p.size = std::max(w, d);
        p.r = 0.45f; p.g = 0.43f; p.b = 0.40f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        p.odor_type = OdorType::NONE;
        p.solver_mode = ParticleSolverMode::KINEMATIC;
        p.is_at_rest = true;
        return engine.add_particle(p);
    }

    // Let the BVH catch up with what was just added.
    void settle() { for (int i = 0; i < 3; ++i) engine.update(1.0 / 60.0); }

    SenseResult look(const SenseConfig& cfg, float from_x, float from_y,
                     float facing, const std::vector<int>& exclude = {}) {
        auto view = engine.get_particle_system().lock_particles_for_read();
        const BVH* bvh = engine.get_particle_system().get_shadow_bvh();
        if (!bvh) return {};
        return senses.cast_vision_cone(cfg, from_x, from_y, 1.0f, facing,
                                       view.get(), *bvh, {}, exclude);
    }

    SmellResult sniff(const SmellConfig& cfg, float x, float y,
                      float discernment = 1.0f,
                      const std::vector<int>& exclude = {}) {
        auto view = engine.get_particle_system().lock_particles_for_read();
        return senses.check_smell(cfg, x, y, 1.0f, discernment,
                                  view.get(), exclude);
    }

    // Smell is a rand() roll against
    //   P = intensity * (1 - (d/r)^2) * state_factor * sensitivity
    // so a single sniff proves nothing either way. Sniff repeatedly and
    // measure the rate, which is the thing the model actually claims.
    float sniff_rate(const SmellConfig& cfg, float x, float y, int n = 400) {
        int hits = 0;
        for (int i = 0; i < n; ++i)
            if (sniff(cfg, x, y).detected) ++hits;
        return static_cast<float>(hits) / static_cast<float>(n);
    }
};

// A predator: a wide, long-range hunter's eye. Deliberately NOT one of
// the archetype presets, because a test that uses a preset only proves
// that preset works.
SenseConfig predator_eyes() {
    SenseConfig c;
    c.fov_degrees = 100.0f;
    c.vision_range = 25.0f;
    c.ray_count = 32;
    c.vision_quality = 1.0f;
    return c;
}

SmellConfig predator_nose() {
    SmellConfig c;
    c.sensitivity = 1.0f;
    c.state_factor = 1.0f;
    c.attracted_to = {OdorType::LIVING_FLESH, OdorType::BLOOD};
    return c;
}

// ---------------------------------------------------------------- sight

void test_it_sees_what_is_in_front_of_it() {
    World w;
    const int p = w.prey(0.0f, 10.0f);       // due north, well in range
    w.settle();

    SenseResult r = w.look(predator_eyes(), 0.0f, 0.0f, NORTH);
    std::cout << "  [measure] prey 10 m due north: " << r.visible_targets.size()
              << " target(s) visible" << std::endl;
    CHECK(r.any_visible(), "prey straight ahead is seen");
    CHECK(r.find_by_id(p) != nullptr, "and it is the right particle");
    if (const SenseTarget* t = r.find_by_id(p)) {
        std::cout << "  [measure] reported distance " << t->distance
                  << " m, angle offset " << t->angle_offset << " rad"
                  << std::endl;
        CHECK(std::fabs(t->distance - 10.0f) < 1.0f,
              "at roughly the right distance (" +
              std::to_string(t->distance) + ")");
        CHECK(std::fabs(t->angle_offset) < 0.15f,
              "and dead ahead (" + std::to_string(t->angle_offset) + " rad)");
    }
}

// The first thing a shortcut throws away: a predator facing away
// should not know what is behind it.
void test_it_cannot_see_behind_itself() {
    World w;
    const int p = w.prey(0.0f, 10.0f);       // north
    w.settle();

    SenseResult r = w.look(predator_eyes(), 0.0f, 0.0f, SOUTH);   // looking away
    std::cout << "  [measure] facing south, prey to the north: "
              << r.visible_targets.size() << " visible" << std::endl;
    CHECK(r.find_by_id(p) == nullptr,
          "prey behind the predator is not seen");
}

void test_range_is_a_real_limit() {
    World w;
    const int near_prey = w.prey(0.0f, 10.0f);
    const int far_prey  = w.prey(0.0f, 40.0f);   // past a 25 m range
    w.settle();

    SenseResult r = w.look(predator_eyes(), 0.0f, 0.0f, NORTH);
    std::cout << "  [measure] range 25 m: prey at 10 m seen="
              << (r.find_by_id(near_prey) != nullptr)
              << ", prey at 40 m seen="
              << (r.find_by_id(far_prey) != nullptr) << std::endl;
    CHECK(r.find_by_id(near_prey) != nullptr, "prey inside the range is seen");
    CHECK(r.find_by_id(far_prey) == nullptr, "prey beyond it is not");
}

// THE ONE THAT MATTERS. Occlusion is what makes cover mean anything,
// and it is exactly what reading a position out of the particle array
// throws away.
void test_a_boulder_hides_prey() {
    World w;
    const int p = w.prey(0.0f, 12.0f);
    // A wide, tall boulder squarely between predator and prey.
    w.boulder(0.0f, 6.0f, 8.0f, 1.0f, 4.0f);
    w.settle();

    SenseResult r = w.look(predator_eyes(), 0.0f, 0.0f, NORTH);
    std::cout << "  [measure] boulder at 6 m, prey at 12 m: prey seen="
              << (r.find_by_id(p) != nullptr) << std::endl;
    CHECK(r.find_by_id(p) == nullptr,
          "prey behind cover is NOT seen, which is what makes cover cover");

    // And the same question asked the other way round must agree.
    bool direct = false;
    {
        auto view = w.engine.get_particle_system().lock_particles_for_read();
        const BVH* bvh = w.engine.get_particle_system().get_shadow_bvh();
        if (bvh) direct = w.senses.can_see_point(0.0f, 0.0f, 1.0f,
                                                 0.0f, 12.0f, 1.0f,
                                                 view.get(), *bvh, {});
    }
    std::cout << "  [measure] can_see_point through the boulder: "
              << direct << std::endl;
    CHECK(!direct, "can_see_point agrees with the cone");
}

void test_stepping_aside_reveals_the_prey() {
    World w;
    const int p = w.prey(0.0f, 12.0f);
    w.boulder(0.0f, 6.0f, 4.0f, 1.0f, 4.0f);   // narrower: can be flanked
    w.settle();

    SenseResult blocked = w.look(predator_eyes(), 0.0f, 0.0f, NORTH);
    // Move well clear of the boulder's edge and look north-east.
    SenseResult flanked = w.look(predator_eyes(), 9.0f, 0.0f,
                                 std::atan2(-9.0f, 12.0f));

    std::cout << "  [measure] from behind cover seen="
              << (blocked.find_by_id(p) != nullptr)
              << ", after flanking seen="
              << (flanked.find_by_id(p) != nullptr) << std::endl;
    CHECK(blocked.find_by_id(p) == nullptr, "hidden from straight on");
    CHECK(flanked.find_by_id(p) != nullptr,
          "and revealed by moving around the cover, so occlusion is "
          "geometry rather than a blanket refusal");
}

// A damaged eye must actually cost the creature something.
void test_a_blind_eye_costs_half_the_cone() {
    World w;
    const int left_prey  = w.prey(-8.0f, 8.0f);   // to the predator's left
    const int right_prey = w.prey( 8.0f, 8.0f);   // to its right
    w.settle();

    SenseConfig both = predator_eyes();
    SenseResult r_both = w.look(both, 0.0f, 0.0f, NORTH);

    SenseConfig half = predator_eyes();
    half.left_eye_blind = true;
    SenseResult r_half = w.look(half, 0.0f, 0.0f, NORTH);

    std::cout << "  [measure] both eyes: " << r_both.visible_targets.size()
              << " visible, left eye blind: "
              << r_half.visible_targets.size() << std::endl;
    CHECK(r_both.find_by_id(left_prey) && r_both.find_by_id(right_prey),
          "a whole predator sees prey on both sides");
    CHECK(r_half.visible_targets.size() < r_both.visible_targets.size(),
          "a blind eye genuinely narrows the cone (" +
          std::to_string(r_half.visible_targets.size()) + " vs " +
          std::to_string(r_both.visible_targets.size()) + ")");
}

// ---------------------------------------------------------------- smell

// Smell exists precisely because sight is narrow and blockable. This is
// the sense the shambler's shortcut made unreachable, so it is worth
// proving it works on its own terms.
void test_smell_reaches_where_sight_cannot() {
    World w;
    const int p = w.prey(0.0f, 6.0f);            // north, close
    w.boulder(0.0f, 3.0f, 10.0f, 1.0f, 4.0f);    // fully blocking
    w.settle();

    SenseResult sight = w.look(predator_eyes(), 0.0f, 0.0f, NORTH);
    const float rate = w.sniff_rate(predator_nose(), 0.0f, 0.0f);

    // Ask about the PREY, not about whether anything at all is visible.
    // With no target filter the cone reports every particle it hits, so
    // the boulder itself is a perfectly correct visible target and
    // any_visible() is true whenever the predator can see the thing
    // blocking its view.
    // P = 1.0 * (1 - (6/20)^2) = 0.91 at this distance.
    std::cout << "  [measure] behind a boulder: prey seen="
              << (sight.find_by_id(p) != nullptr)
              << ", boulder itself visible=" << sight.any_visible()
              << ", smelled on " << (rate * 100.0f)
              << "% of sniffs (model predicts 91%)" << std::endl;
    CHECK(sight.find_by_id(p) == nullptr, "the prey is hidden by the boulder");
    CHECK(sight.any_visible(),
          "while the boulder itself is plainly visible, which is what "
          "being behind cover means");
    CHECK(rate > 0.75f,
          "but smell still finds it, reliably, which is the whole point "
          "of having a nose as well as eyes (rate " +
          std::to_string(rate) + ")");
}

void test_smell_is_omnidirectional() {
    World w;
    w.prey(0.0f, -8.0f);                 // directly BEHIND a north-facer
    w.settle();

    const float rate = w.sniff_rate(predator_nose(), 0.0f, 0.0f);
    std::cout << "  [measure] prey 8 m behind: smelled on "
              << (rate * 100.0f) << "% of sniffs" << std::endl;
    CHECK(rate > 0.5f,
          "smell does not care which way the predator faces (rate " +
          std::to_string(rate) + ")");
}

// The model's whole shape is that closer smells better. If the rate
// does not fall with distance, the falloff term is decoration.
void test_smell_fades_with_distance() {
    World w;
    w.prey(0.0f, 3.0f);
    w.settle();
    const float near_rate = w.sniff_rate(predator_nose(), 0.0f, 0.0f);

    World f;
    f.prey(0.0f, 18.0f);                 // near the 20 m odour radius
    f.settle();
    const float far_rate = f.sniff_rate(predator_nose(), 0.0f, 0.0f);

    std::cout << "  [measure] 3 m: " << (near_rate * 100.0f)
              << "%, 18 m: " << (far_rate * 100.0f)
              << "% (model predicts 98% and 19%)" << std::endl;
    CHECK(near_rate > far_rate + 0.2f,
          "smell fades with distance (" + std::to_string(near_rate) +
          " vs " + std::to_string(far_rate) + ")");
    CHECK(near_rate > 0.85f, "close prey is smelled almost every time");
}

// Beyond the odour radius there is no roll at all: it must be never,
// not rarely.
void test_beyond_the_odour_radius_is_never() {
    World w;
    w.prey(0.0f, 30.0f);                 // odour radius is 20 m
    w.settle();

    const float rate = w.sniff_rate(predator_nose(), 0.0f, 0.0f, 200);
    std::cout << "  [measure] prey 30 m away, odour radius 20 m: "
              << (rate * 100.0f) << "% of sniffs" << std::endl;
    CHECK(rate == 0.0f,
          "outside the odour radius it is never detected, not merely "
          "seldom (rate " + std::to_string(rate) + ")");
}

void test_it_ignores_odours_it_does_not_hunt() {
    World w;
    w.prey(0.0f, 5.0f, 1.0f, OdorType::SMOKE);   // present, but not food
    w.settle();

    const float rate = w.sniff_rate(predator_nose(), 0.0f, 0.0f, 200);
    std::cout << "  [measure] only smoke nearby: " << (rate * 100.0f)
              << "% of sniffs" << std::endl;
    CHECK(rate == 0.0f, "an odour outside attracted_to is not a meal");
}

void test_nothing_to_smell_is_answered_cleanly() {
    World w;
    w.boulder(0.0f, 5.0f, 4.0f, 1.0f, 3.0f);     // odourless
    w.settle();

    SmellResult r = w.sniff(predator_nose(), 0.0f, 0.0f);
    CHECK(!r.detected, "an empty world smells of nothing");
    CHECK(r.source_particle_id == -1,
          "and reports no source rather than a stale one");
}

}  // namespace

int main() {
    // check_smell rolls rand(); pin the seed so a rate is
    // reproducible rather than a different number every run.
    std::srand(20260802u);
    std::cout << "Predator senses (src/sense_system)" << std::endl;
    test_it_sees_what_is_in_front_of_it();
    test_it_cannot_see_behind_itself();
    test_range_is_a_real_limit();
    test_a_boulder_hides_prey();
    test_stepping_aside_reveals_the_prey();
    test_a_blind_eye_costs_half_the_cone();
    test_smell_reaches_where_sight_cannot();
    test_smell_is_omnidirectional();
    test_smell_fades_with_distance();
    test_beyond_the_odour_radius_is_never();
    test_it_ignores_odours_it_does_not_hunt();
    test_nothing_to_smell_is_answered_cleanly();

    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
