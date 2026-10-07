// =============================================================================
// Face tracks head — repro for the Eden playtest find (2026-07-30):
// "the eyes are glued to the vertical axis of Eva and not glued to
// the front of her face. same for ears and nose".
// =============================================================================
// The yaw cascade makes the head LEAD the hips (tau 80 ms vs 350 ms),
// so during any turn the two yaws diverge. Face features (eyes, ears,
// nose, hair) are head children: their offset from the head must
// rotate with the HEAD's yaw. Rotating them with the hips' yaw glues
// them to the body axis and the face slides around the skull.
//
// Measured, headless: a look-at target sweeps the compass; every
// frame, for every head child with a real horizontal offset, the
// yaw of (child - head) must track the head's rotation_z.
//
// Usage:
//   ./build/test_face_tracks_head
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
}  // namespace

int main() {
    std::printf("Face tracks head — Eden playtest repro\n");

    Engine engine(nullptr);
    EngineConfig config;
    config.create_display = false;
    config.window_width = 1280;
    config.window_height = 960;
    config.window_title = "face-track-at";
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

    int head = eva.head_id;
    std::vector<int> body = std::vector<int>(eva.body_ids.begin(),
                                             eva.body_ids.end());
    ps.add_swap_callback([&](size_t o, size_t n) {
        if (head == (int)o) head = (int)n;
        for (auto& id : body) if (id == (int)o) id = (int)n;
    });

    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 60; ++i) engine.update(dt);   // settle

    // The face by construction: torso_ids[5..] are the head children
    // (hair, ears, eyes — the locomotion registers exactly these).
    // Track every one with a real horizontal offset; a feature with
    // no horizontal offset (crown hair) has no measurable yaw.
    struct Feature { int id; float rest_rel_yaw; float horiz; };
    std::vector<Feature> features;
    float head_rest_yaw;
    {
        auto v = ps.lock_particles_for_read();
        const auto& hp = v[head];
        head_rest_yaw = hp.rotation_z;
        for (size_t i = 5; i < eva.torso_ids.size(); ++i) {
            int id = eva.torso_ids[i];
            const auto& p = v[id];
            float dx = p.x - hp.x, dy = p.y - hp.y;
            float horiz = std::sqrt(dx * dx + dy * dy);
            if (horiz > 0.01f) {
                float off_yaw = std::atan2(dx, dy);   // engine compass
                features.push_back({id,
                                    wrap_pi(off_yaw - head_rest_yaw),
                                    horiz});
            }
        }
    }
    std::printf("  [setup] %zu face features orbit the head\n",
                features.size());
    AT_ASSERT_TRUE(features.size() >= 3,
        "found the face (eyes/ears/nose candidates, got " +
        std::to_string(features.size()) + ")");

    // Sweep the look-at target around the compass, one lap in 8 s
    // (the Eden mouse). Every frame, every feature's offset yaw must
    // track the HEAD's yaw — not the hips', which lags by design.
    float worst = 0.0f;
    int worst_id = -1;
    float worst_hips_gap = 0.0f;
    std::vector<float> per_feature_worst(features.size(), 0.0f);
    constexpr int FRAMES = 480;
    for (int f = 0; f < FRAMES; ++f) {
        float a = 2.0f * static_cast<float>(M_PI) *
                  static_cast<float>(f) / FRAMES;
        loco.set_look_at_target(eva.hips_id,
                                10.0f * std::sin(a), 10.0f * std::cos(a));
        engine.update(dt);
        auto v = ps.lock_particles_for_read();
        const auto& hp = v[head];
        float head_yaw_delta = wrap_pi(hp.rotation_z - head_rest_yaw);
        float hips_yaw_delta = wrap_pi(v[eva.hips_id].rotation_z -
                                       head_rest_yaw);
        worst_hips_gap = std::max(worst_hips_gap,
            std::fabs(wrap_pi(head_yaw_delta - hips_yaw_delta)));
        for (size_t k = 0; k < features.size(); ++k) {
            const auto& ft = features[k];
            const auto& p = v[ft.id];
            float off_yaw = std::atan2(p.x - hp.x, p.y - hp.y);
            float err = std::fabs(wrap_pi(
                off_yaw - head_rest_yaw - ft.rest_rel_yaw -
                head_yaw_delta));
            per_feature_worst[k] = std::max(per_feature_worst[k], err);
            if (err > worst) { worst = err; worst_id = ft.id; }
        }
    }
    {
        auto v = ps.lock_particles_for_read();
        for (size_t k = 0; k < features.size(); ++k)
            std::printf("  [feature] P%d w=%.3f horiz=%.3f worst_err=%.3f\n",
                        features[k].id, v[features[k].id].width,
                        features[k].horiz, per_feature_worst[k]);
    }

    // Causal chain for the worst offender: who writes it each frame?
    if (worst_id >= 0) {
        auto& tracer = engine.get_particle_tracer();
        tracer.trace(worst_id, "worst_feature");
        for (int f = 0; f < 3; ++f) {
            loco.set_look_at_target(eva.hips_id, 10.0f, 0.0f);
            engine.update(dt);
        }
        tracer.dump(std::cout, /*last_n_frames=*/3);
        tracer.untrace(worst_id);
    }
    std::printf("  [sweep] worst face-vs-head yaw error=%.3f rad (P%d); "
                "head-vs-hips divergence peaked at %.3f rad\n",
                worst, worst_id, worst_hips_gap);
    // The head must genuinely lead the hips during the sweep or this
    // test can't tell the two authorities apart.
    AT_ASSERT_TRUE(worst_hips_gap > 0.15f,
        "the cascade diverges head from hips mid-turn (gap " +
        std::to_string(worst_hips_gap) + " rad)");
    // The contract: the face rides the head. 0.15 rad (~8.6 deg)
    // allows solver jitter; the hips-glued bug shows up as the full
    // head-vs-hips gap.
    AT_ASSERT_TRUE(worst < 0.15f,
        "eyes/ears/nose stay glued to the FRONT OF THE FACE (worst "
        "offset-yaw error " + std::to_string(worst) + " rad)");

    std::printf("%d passed, %d failed\n", tests_passed, tests_failed);
    engine.shutdown();
    return tests_failed == 0 ? 0 : 1;
}
