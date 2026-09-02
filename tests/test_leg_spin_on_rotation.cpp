// =============================================================================
// Leg spin on rotation — repro for the Eden playtest find (2026-07-30):
// "just when the human rotates, the leg particles spin out of control".
// =============================================================================
// Measured, headless, no visual dev: a settled humanoid's look-at
// target sweeps around the compass (the Eden mouse), then a walking
// pass does the same. Every frame, every leg particle's rotation
// (x/y/z) is sampled; the test asserts rotation RATES stay within
// what the yaw cascade can legitimately command, and that total
// accumulated rotation stays in the same order as the commanded yaw
// (a spinning leg accumulates revolutions).
//
// Usage:
//   ./build/test_leg_spin_on_rotation
// =============================================================================

#include "../src/core/engine.h"
#include "../src/core/particle_system.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/animation/humanoid_locomotion.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static int tests_passed = 0;
static int tests_failed = 0;

#define AT_ASSERT_TRUE(cond, msg)                                       \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL: %s\n", std::string(msg).c_str());        \
            tests_failed++;                                             \
        } else {                                                        \
            tests_passed++;                                             \
        }                                                               \
    } while (0)

namespace {

float wrap_pi(float a) {
    while (a > static_cast<float>(M_PI)) a -= 2.0f * static_cast<float>(M_PI);
    while (a < -static_cast<float>(M_PI)) a += 2.0f * static_cast<float>(M_PI);
    return a;
}

struct LegWatch {
    std::vector<int> ids;
    std::vector<std::string> names;
    // Per-particle accumulated |delta| per axis, and worst single-frame step.
    std::vector<float> total_x, total_y, total_z;
    std::vector<float> prev_x, prev_y, prev_z;
    float worst_step = 0.0f;
    int worst_id = -1;
    const char* worst_axis = "";

    void init(ParticleSystem& ps) {
        auto v = ps.lock_particles_for_read();
        size_t n = ids.size();
        total_x.assign(n, 0); total_y.assign(n, 0); total_z.assign(n, 0);
        prev_x.resize(n); prev_y.resize(n); prev_z.resize(n);
        for (size_t k = 0; k < n; ++k) {
            prev_x[k] = v[ids[k]].rotation_x;
            prev_y[k] = v[ids[k]].rotation_y;
            prev_z[k] = v[ids[k]].rotation_z;
        }
    }
    void sample(ParticleSystem& ps) {
        auto v = ps.lock_particles_for_read();
        for (size_t k = 0; k < ids.size(); ++k) {
            float dx = std::fabs(wrap_pi(v[ids[k]].rotation_x - prev_x[k]));
            float dy = std::fabs(wrap_pi(v[ids[k]].rotation_y - prev_y[k]));
            float dz = std::fabs(wrap_pi(v[ids[k]].rotation_z - prev_z[k]));
            total_x[k] += dx; total_y[k] += dy; total_z[k] += dz;
            auto upd = [&](float d, const char* ax) {
                if (d > worst_step) { worst_step = d; worst_id = ids[k]; worst_axis = ax; }
            };
            upd(dx, "x"); upd(dy, "y"); upd(dz, "z");
            prev_x[k] = v[ids[k]].rotation_x;
            prev_y[k] = v[ids[k]].rotation_y;
            prev_z[k] = v[ids[k]].rotation_z;
        }
    }
    float max_total() const {
        float m = 0;
        for (size_t k = 0; k < ids.size(); ++k)
            m = std::max({m, total_x[k], total_y[k], total_z[k]});
        return m;
    }
};

}  // namespace

int main() {
    std::printf("Leg spin on rotation — Eden playtest repro\n");

    Engine engine(nullptr);
    EngineConfig config;
    config.create_display = false;
    config.window_width = 1280;
    config.window_height = 960;
    config.window_title = "leg-spin-at";
    config.enable_chat_window = false;
    if (engine.initialize(config) < 0) {
        std::printf("FAIL: engine init\n");
        return 1;
    }

    auto& ps = engine.get_particle_system();

    Particle floor = {};
    floor.shape = ParticleShape::BOX;
    floor.x = 0; floor.y = 0; floor.z = 0.025f;  // bottom on the turtle, top unchanged at 0.05
    floor.width = 50; floor.height = 50; floor.thickness = 0.05f;
    floor.r = 0.3f; floor.g = 0.5f; floor.b = 0.2f; floor.a = 1.0f;
    floor.SetMaterial(Materials::Type::STONE);
    floor.is_at_rest = true;
    engine.add_particle(floor);

    auto& hgen = engine.get_worldgen_system().get_humanoid_generator();
    auto eva = hgen.generate_humanoid_physics(
        // 0.55, not 0.5: world_z is the FEET'S BOTTOM and this scene's strata
        // surface is 0.30 + 0.15 + 0.10 = 0.55. At 0.5 every foot was born
        // 50 mm inside the organic layer, which INV-37 refuses.
        0.0f, 0.0f, 0.55f, -1, HumanoidSpec::eva(), false);
    auto& kg = engine.get_kg();
    eva.create_kg_entities(kg, "Humanoid", 180.0f, 800.0f);
    auto& loco = engine.get_humanoid_locomotion();
    loco.register_humanoid_direct(
        eva.hips_id, eva.left_leg_ids, eva.right_leg_ids,
        eva.left_arm_ids, eva.right_arm_ids, eva.torso_ids,
        180.0f, 800.0f);

    LegWatch legs;
    auto add_leg = [&](const std::vector<int>& ids, const char* side) {
        static const char* part[] = {"thigh", "shin", "foot", "toe"};
        for (size_t k = 0; k < ids.size(); ++k) {
            legs.ids.push_back(ids[k]);
            legs.names.push_back(std::string(side) + "_" +
                                 (k < 4 ? part[k] : "part"));
        }
    };
    add_leg(eva.left_leg_ids, "l");
    add_leg(eva.right_leg_ids, "r");

    ps.add_swap_callback([&](size_t o, size_t n) {
        for (auto& id : legs.ids) if (id == (int)o) id = (int)n;
    });

    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 60; ++i) engine.update(dt);   // settle

    // --- Phase 1: IDLE, look-at target sweeps the compass (the Eden
    // mouse wandering around a standing human). Two slow laps, 8 s.
    legs.init(ps);
    constexpr int P1_FRAMES = 480;
    for (int f = 0; f < P1_FRAMES; ++f) {
        float a = 2.0f * static_cast<float>(M_PI) * 2.0f *
                  static_cast<float>(f) / P1_FRAMES;
        loco.set_look_at_target(eva.hips_id,
                                10.0f * std::sin(a), 10.0f * std::cos(a));
        engine.update(dt);
        legs.sample(ps);
    }
    std::printf("  [idle-sweep] worst step=%.3f rad/frame (P%d axis %s), "
                "max total=%.2f rad\n",
                legs.worst_step, legs.worst_id, legs.worst_axis,
                legs.max_total());
    // The contract (leg-spin RCA, todos/LEG_SPIN_ON_ROTATION.md):
    // the cascade commands 2 laps of yaw (12.6 rad); legs may add
    // twist-step swings but never snap (>0.6 rad/frame) and never
    // accumulate revolutions beyond ~1.5x the commanded yaw.
    AT_ASSERT_TRUE(legs.worst_step < 0.6f,
        "idle rotation: no leg snaps (worst " +
        std::to_string(legs.worst_step) + " rad/frame)");
    AT_ASSERT_TRUE(legs.max_total() < 19.0f,
        "idle rotation: no spun-up revolutions (max total " +
        std::to_string(legs.max_total()) + " rad)");

    // --- Phase 2: WALKING while the target keeps sweeping (turning
    // during locomotion — the other spin trigger candidates).
    LegWatch legs2;
    legs2.ids = legs.ids;
    legs2.names = legs.names;
    legs2.init(ps);
    loco.set_target_velocity(eva.hips_id, 0.0f, 1.2f);
    constexpr int P2_FRAMES = 480;
    for (int f = 0; f < P2_FRAMES; ++f) {
        float a = 2.0f * static_cast<float>(M_PI) *
                  static_cast<float>(f) / P2_FRAMES;
        loco.set_look_at_target(eva.hips_id,
                                10.0f * std::sin(a), 10.0f * std::cos(a));
        engine.update(dt);
        legs2.sample(ps);
    }
    std::printf("  [walk-sweep] worst step=%.3f rad/frame (P%d axis %s), "
                "max total=%.2f rad\n",
                legs2.worst_step, legs2.worst_id, legs2.worst_axis,
                legs2.max_total());
    // Walking swings legs legitimately (FK clips), so the total runs
    // higher; the SNAP bound is the tripwire that matters.
    AT_ASSERT_TRUE(legs2.worst_step < 0.6f,
        "walking rotation: no leg snaps (worst " +
        std::to_string(legs2.worst_step) + " rad/frame)");
    AT_ASSERT_TRUE(legs2.max_total() < 40.0f,
        "walking rotation: leg rotation stays in gait range (total " +
        std::to_string(legs2.max_total()) + " rad)");

    std::printf("%d passed, %d failed\n", tests_passed, tests_failed);
    engine.shutdown();
    return tests_failed == 0 ? 0 : 1;
}
