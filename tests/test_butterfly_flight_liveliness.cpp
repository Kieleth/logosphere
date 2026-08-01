// Butterflies must fly, and fly ABOVE whatever ground is actually
// there.
//
// Two complaints, one cause. Their shadows sat directly beneath them
// and their flight looked lifeless, because the flight system
// computed an altitude and then threw it away - "PHYSICS-003: Z
// control disabled" - leaving gravity to own Z. So they fell, landed,
// and drifted along the floor. The altitude floor that was supposed
// to save them was ALTITUDE_MIN = 0.1, an absolute height, which on
// Eden's 0.55 m strata is nearly half a metre underground.
//
// These tests hold the fix at the level of behaviour rather than
// implementation: a butterfly stays above the real surface, and its
// flight is varied enough to be worth watching.
//
// Ground here sits at 1.75 m, a height no constant would guess.
//
// Usage:
//   ./build/test_butterfly_flight_liveliness

#include "core/engine.h"
#include "core/particle_system.h"
#include "logosphere/animation/butterfly_flight.h"
#include "logosphere/worldgen/butterfly_generator.h"
#include "generated/earth_ontology_registry.h"
#include "particle.h"

#include <algorithm>
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

constexpr float GROUND_TOP = 1.75f;

struct Harness {
    Engine engine;
    Harness() : engine(nullptr) {
        EngineConfig config;
        config.create_display = false;
        config.window_width = 640;
        config.window_height = 480;
        config.window_title = "butterflies";
        config.show_debug_overlay = false;
        config.enable_chat_window = false;
        if (engine.initialize(config) < 0)
            throw std::runtime_error("Engine::initialize() failed headless");
        engine.get_kg().extendOntology(earth::ontology::registry());

        // A wide slab of ground, well above zero.
        for (int gx = -3; gx <= 3; ++gx) {
            for (int gy = -3; gy <= 3; ++gy) {
                Particle p{};
                p.shape = ParticleShape::BOX;
                p.x = gx * 4.0f; p.y = gy * 4.0f;
                p.z = GROUND_TOP - 0.25f;
                p.width = p.height = 4.0f;
                p.thickness = 0.5f;
                p.size = 4.0f;
                p.r = 0.4f; p.g = 0.35f; p.b = 0.3f; p.a = 1.0f;
                p.SetMaterial(Materials::Type::STONE);
                p.solver_mode = ParticleSolverMode::KINEMATIC;
                p.is_at_rest = true;
                engine.add_particle(p);
            }
        }
        for (int i = 0; i < 3; ++i) engine.update(1.0 / 60.0);
    }
    ~Harness() { engine.shutdown(); }
};

struct Track {
    float min_z = 1e9f, max_z = -1e9f;
    float min_clear = 1e9f;
    float total_xy = 0.0f;
    float total_turn = 0.0f;   // radians of heading change, cumulative
};

Track fly(Harness& h, const std::vector<unsigned int>& body, int frames) {
    Track t;
    float px = 0, py = 0;
    bool first = true;
    float last_dir = 0.0f;
    for (int i = 0; i < frames; ++i) {
        h.engine.update(1.0 / 60.0);
        auto view = h.engine.get_particle_system().lock_particles_for_read();
        float cx = 0, cy = 0, cz = 0;
        int n = 0;
        for (unsigned int idx : body) {
            if (idx >= view.size()) continue;
            cx += view[idx].x; cy += view[idx].y; cz += view[idx].z; ++n;
        }
        if (!n) continue;
        cx /= n; cy /= n; cz /= n;
        t.min_z = std::min(t.min_z, cz);
        t.max_z = std::max(t.max_z, cz);
        t.min_clear = std::min(t.min_clear, cz - GROUND_TOP);
        if (!first) {
            float dx = cx - px, dy = cy - py;
            t.total_xy += std::sqrt(dx * dx + dy * dy);
            float dir = std::atan2(dy, dx);
            // Cumulative turning, not abrupt jumps. A held turn - the
            // circle we actually want - changes heading by only ~0.02
            // rad per frame, so counting jumps measures flitting and
            // scores real circling as a straight line.
            float d = dir - last_dir;
            while (d >  static_cast<float>(M_PI)) d -= 2.0f * static_cast<float>(M_PI);
            while (d < -static_cast<float>(M_PI)) d += 2.0f * static_cast<float>(M_PI);
            t.total_turn += std::fabs(d);
            last_dir = dir;
        }
        px = cx; py = cy; first = false;
    }
    return t;
}

std::vector<unsigned int> spawn(Harness& h, float x, float y) {
    auto& gen = h.engine.get_worldgen_system().get_butterfly_generator();
    ButterflySpec spec = ButterflySpec::monarch();
    // Spawned deliberately LOW, right at the surface: a butterfly that
    // can only fall stays here.
    kg::EntityID id = gen.generate_butterfly(x, y, GROUND_TOP + 0.1f, spec);
    h.engine.get_butterfly_flight().register_butterfly(id);

    std::vector<unsigned int> body;
    auto view = h.engine.get_particle_system().lock_particles_for_read();
    for (size_t i = 0; i < view.size(); ++i)
        if (view[i].solver_mode == ParticleSolverMode::KINEMATIC &&
            view[i].width < 1.0f)
            body.push_back(static_cast<unsigned int>(i));
    return body;
}

// It must climb away from the ground it was dropped on, and never
// sink into it.
void test_it_flies_above_the_real_ground() {
    Harness h;
    auto body = spawn(h, 0.0f, 0.0f);
    CHECK(!body.empty(), "a butterfly exists");

    Track t = fly(h, body, 900);   // 15 seconds
    std::cout << "  [measure] z range " << t.min_z << " .. " << t.max_z
              << ", closest approach to ground " << t.min_clear
              << " m (ground at " << GROUND_TOP << ")" << std::endl;

    CHECK(t.min_clear > 0.0f,
          "never sinks into the ground (closest " +
          std::to_string(t.min_clear) + " m)");
    CHECK(t.max_z > GROUND_TOP + 0.8f,
          "climbs properly away from the surface (reached " +
          std::to_string(t.max_z) + ", ground " +
          std::to_string(GROUND_TOP) + ")");
    // The old bug in one line: an absolute floor of 0.1 would put it
    // at 0.1 here, which is 1.65 m below this ground.
    CHECK(t.min_z > 0.2f,
          "and is nowhere near an absolute altitude floor (lowest " +
          std::to_string(t.min_z) + ")");
}

// Lifeless is measurable: it should wander through heights and turn,
// not hold one line at one altitude.
void test_the_flight_is_worth_watching() {
    Harness h;
    auto body = spawn(h, 0.0f, 0.0f);
    Track t = fly(h, body, 1800);   // 30 seconds

    const float band = t.max_z - t.min_z;
    std::cout << "  [measure] altitude band " << band << " m, travelled "
              << t.total_xy << " m, cumulative turning "
              << t.total_turn << " rad ("
              << (t.total_turn / (2.0f * static_cast<float>(M_PI)))
              << " full turns)" << std::endl;

    CHECK(band > 0.5f,
          "it wanders through heights rather than holding one (band " +
          std::to_string(band) + " m)");
    CHECK(t.total_xy > 5.0f,
          "it actually goes somewhere (travelled " +
          std::to_string(t.total_xy) + " m)");
    // A straight line accumulates ~0. Anything that circles racks up
    // multiples of 2*pi; 3 rad is half a turn over 30 s, which no
    // straight-line drift reaches.
    CHECK(t.total_turn > 3.0f,
          "and turns as it goes, rather than running in a straight "
          "line (turned " + std::to_string(t.total_turn) + " rad)");
}

// A flock should read as individuals. Identical altitudes are what
// made four butterflies look like one animation played four times.
void test_a_flock_is_not_a_formation() {
    Harness h;
    std::vector<std::vector<unsigned int>> bodies;
    std::vector<kg::EntityID> ids;
    auto& gen = h.engine.get_worldgen_system().get_butterfly_generator();
    for (int i = 0; i < 4; ++i) {
        ButterflySpec spec = ButterflySpec::monarch();
        kg::EntityID id = gen.generate_butterfly(
            i * 3.0f - 4.0f, 0.0f, GROUND_TOP + 0.1f, spec);
        h.engine.get_butterfly_flight().register_butterfly(id);
        ids.push_back(id);
    }
    for (int i = 0; i < 600; ++i) h.engine.update(1.0 / 60.0);

    // Heights of everything small and kinematic: the flock.
    std::vector<float> heights;
    {
        auto view = h.engine.get_particle_system().lock_particles_for_read();
        for (size_t i = 0; i < view.size(); ++i)
            if (view[i].solver_mode == ParticleSolverMode::KINEMATIC &&
                view[i].width < 1.0f)
                heights.push_back(view[i].z);
    }
    CHECK(!heights.empty(), "the flock exists");
    float lo = *std::min_element(heights.begin(), heights.end());
    float hi = *std::max_element(heights.begin(), heights.end());
    std::cout << "  [measure] flock spans " << lo << " .. " << hi
              << " m (" << (hi - lo) << " m apart)" << std::endl;
    CHECK(hi - lo > 0.4f,
          "they fly at different heights (spread " +
          std::to_string(hi - lo) + " m)");
}

}  // namespace

int main() {
    std::cout << "Butterfly flight (alive, and above the real ground)"
              << std::endl;
    test_it_flies_above_the_real_ground();
    test_the_flight_is_worth_watching();
    test_a_flock_is_not_a_formation();
    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
