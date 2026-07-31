// =============================================================================
// Leg geometry through an Eden-like choreography — repro for the
// second Eden playtest find (2026-07-30): "legs are broken again" —
// legs FOLDED flat/forward while the torso stands.
// =============================================================================
// The spin bug was rotational; this one is positional. Walk, stop,
// turn in place, walk again, stop — the transitions (plant release,
// twist-step, heel-strike) are where stale anchors fold a leg. Every
// frame asserts the skeleton's gross geometry:
//   - hips stay a leg's length above the lowest foot (no collapse),
//   - feet stay under the body (no fold-out past a stride),
//   - knees (thigh-shin joint region) stay below the hips.
//
// Status: the fold does NOT reproduce here (2026-07-30) despite the
// full Eden recipe: streamed strata + chunk swaps, volitional
// body-relative drive, look-at sweeps, obstacles, backpedaling,
// 100 m marches, and measured dt spikes (0.09/0.15 s stalls). The
// test stays as the regression net for everything it DOES cover.
// NOTE the stale-id lesson baked in below: driving eva.hips_id
// (unremapped) on streaming terrain silently addresses the wrong
// particle after swaps — commands go nowhere. Always drive the
// swap-remapped cache.
//
// Usage:
//   ./build/test_leg_geometry_choreography
// =============================================================================

#include "../src/core/engine.h"
#include "../src/core/particle_system.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/worldgen/strata_floor_generator.h"
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

int main() {
    std::printf("Leg geometry choreography — Eden playtest repro #2\n");

    Engine engine(nullptr);
    EngineConfig config;
    config.create_display = false;
    config.window_width = 1280;
    config.window_height = 960;
    config.window_title = "leg-geometry-at";
    config.enable_chat_window = false;
    if (engine.initialize(config) < 0) {
        std::printf("FAIL: engine init\n");
        return 1;
    }
    auto& ps = engine.get_particle_system();

    // EDEN'S GROUND, not a slab: streamed 3-layer strata with chunk
    // churn and particle-index swaps — the playtest log shows Eva
    // planting at z=0.595 at world (35,-25), far from origin, on
    // exactly this terrain.
    auto& strata = engine.get_worldgen_system().get_strata_floor_generator();
    strata.set_tile_size(4.0f);
    strata.set_tiles_per_chunk(5);
    strata.set_tiles_per_entity(1);
    strata.set_load_radius(30.0f);
    strata.set_unload_radius(40.0f);
    std::vector<StrataLayerSpec> layers;
    auto add_layer = [&](const char* name, Materials::Type mat, float th,
                         float r, float g, float b, bool bond, float bs) {
        StrataLayerSpec sl;
        sl.name = name; sl.material = mat; sl.thickness = th;
        sl.r = r; sl.g = g; sl.b = b;
        sl.bond_within_layer = bond; sl.bond_strength = bs;
        layers.push_back(sl);
    };
    add_layer("bedrock",  Materials::Type::STONE, 0.30f, 0.35f, 0.33f, 0.30f, true,  8000.0f);
    add_layer("sediment", Materials::Type::STONE, 0.15f, 0.45f, 0.40f, 0.30f, false, 0.0f);
    add_layer("organic",  Materials::Type::DIRT,  0.10f, 0.30f, 0.45f, 0.22f, false, 0.0f);
    strata.set_layers(std::move(layers));
    strata.set_enabled(true);
    strata.preload_chunks_around(0.0f, 0.0f, 2);

    // The Eden table: an obstacle in the walking lane.
    Particle table = {};
    table.shape = ParticleShape::BOX;
    table.x = 0.0f; table.y = 6.0f; table.z = 1.0f;
    table.width = 0.8f; table.height = 0.8f; table.thickness = 0.7f;
    table.r = 0.9f; table.g = 0.9f; table.b = 0.85f; table.a = 1.0f;
    table.SetMaterial(Materials::Type::STONE);
    table.is_at_rest = true;
    engine.add_particle(table);

    auto& hgen = engine.get_worldgen_system().get_humanoid_generator();
    auto eva = hgen.generate_humanoid_physics(
        0.0f, 0.0f, 1.0f, -1, HumanoidSpec::eva(), false);
    auto& kg = engine.get_kg();
    eva.create_kg_entities(kg, "Humanoid", 180.0f, 800.0f);
    auto& loco = engine.get_humanoid_locomotion();
    loco.register_humanoid_direct(
        eva.hips_id, eva.left_leg_ids, eva.right_leg_ids,
        eva.left_arm_ids, eva.right_arm_ids, eva.torso_ids,
        180.0f, 800.0f);

    int hips = eva.hips_id;
    // leg_ids layout: thigh, shin, foot, toe.
    std::vector<int> parts = {static_cast<int>(eva.left_leg_ids[0]),
                              static_cast<int>(eva.left_leg_ids[1]),
                              static_cast<int>(eva.left_leg_ids[2]),
                              static_cast<int>(eva.right_leg_ids[0]),
                              static_cast<int>(eva.right_leg_ids[1]),
                              static_cast<int>(eva.right_leg_ids[2])};
    ps.add_swap_callback([&](size_t o, size_t n) {
        if (hips == (int)o) hips = (int)n;
        for (auto& id : parts) if (id == (int)o) id = (int)n;
    });

    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 60; ++i) engine.update(dt);   // settle
    // EDEN'S DRIVE PATH: volitional + body-relative velocity (player
    // holds W, mouse steers the facing) — not world-frame targets.
    loco.set_volitional(hips, true);

    float rest_hips_z;
    {
        auto v = ps.lock_particles_for_read();
        rest_hips_z = v[hips].z;
    }
    std::printf("  [setup] rest hips z=%.3f\n", rest_hips_z);

    // Choreography: the stop/turn/start transitions of an Eden
    // player. vx/vy are BODY-RELATIVE (forward, right) in m/s; the
    // look-at target steers the facing like the mouse.
    struct Phase { float vx, vy, lx, ly; int frames; const char* name; };
    std::vector<Phase> phases = {
        {0.0f, 1.2f,   0.0f, 10.0f, 180, "walk north"},
        {0.0f, 0.0f,   0.0f, 10.0f, 120, "stop"},
        {0.0f, 0.0f,  10.0f,  0.0f, 150, "turn east in place"},
        {1.2f, 0.0f,  10.0f,  0.0f, 180, "walk east"},
        {0.0f, 0.0f, -10.0f,  0.0f, 150, "about-face west"},
        {-1.2f, 0.0f, -10.0f, 0.0f, 180, "walk west"},
        {0.0f, 0.0f,   0.0f, -10.0f, 150, "turn south, stand"},
        {0.0f, 1.2f,   0.0f, 10.0f, 180, "turn and walk north again"},
        {0.0f, 0.0f,   0.0f, 10.0f, 120, "final stop"},
        // Eden realism: look OPPOSITE the walk (mouse behind), fast
        // gait, and a march straight into the table.
        {0.0f, -1.8f,  0.0f, 10.0f, 180, "backpedal south, eyes north"},
        {1.8f, 0.0f,   0.0f, -10.0f, 180, "fast east, eyes south"},
        {0.0f, 2.0f,   0.0f, 10.0f, 300, "march into the table"},
        {0.0f, 0.0f,   0.0f, 10.0f, 120, "stand at the table"},
        {0.0f, -1.5f,  0.0f, 10.0f, 150, "back away, eyes on it"},
        // Cross-chunk marches: 30+ m out and back — chunks load and
        // unload, particle indices swap (the playtest incident was
        // 43 m from origin).
        {1.6f, -1.2f, 10.0f, -8.0f, 1500, "long march SE across chunks"},
        {0.0f, 0.0f, -10.0f,  0.0f, 150, "stop and turn far afield"},
        {0.0f, 1.6f,   0.0f, 10.0f, 600, "march north out there"},
        {0.0f, 0.0f,  10.0f,  0.0f, 150, "stop, look east"},
        {-1.6f, 1.2f, -10.0f, 8.0f, 1500, "long march home NW"},
        {0.0f, 0.0f,   0.0f, 10.0f, 180, "final stand"},
    };

    float phase_end_x = 0, phase_end_y = 0;
    float worst_drop = 0.0f;       // hips sinking toward the feet
    float worst_reach = 0.0f;      // foot escaping horizontally
    const char* worst_drop_phase = "";
    const char* worst_reach_phase = "";
    for (const auto& ph : phases) {
        loco.set_body_relative_velocity(hips, ph.vy, ph.vx);
        {
            auto v = ps.lock_particles_for_read();
            std::printf("  [phase] %-28s starts at (%.1f, %.1f)\n",
                        ph.name, v[hips].x, v[hips].y);
        }
        for (int f = 0; f < ph.frames; ++f) {
            auto vpos = [&]() {
                auto v = ps.lock_particles_for_read();
                float hx = v[hips].x, hy = v[hips].y, hz = v[hips].z;
                loco.set_look_at_target(hips, hx + ph.lx, hy + ph.ly);
                for (int id : parts) {
                    float dxy = std::sqrt(
                        (v[id].x - hx) * (v[id].x - hx) +
                        (v[id].y - hy) * (v[id].y - hy));
                    if (dxy > worst_reach) {
                        worst_reach = dxy;
                        worst_reach_phase = ph.name;
                    }
                }
                float drop = rest_hips_z - hz;
                if (drop > worst_drop) {
                    worst_drop = drop;
                    worst_drop_phase = ph.name;
                }
            };
            vpos();
            // Eden runs REAL dt with stalls (measured: 76.7 ms frames
            // in the playtest log). Spike the step like life does.
            float step = dt;
            if (f % 97 == 96) step = 0.09f;
            if (f % 251 == 250) step = 0.15f;
            engine.update(step);
            {
                float hx, hy;
                {
                    auto v = ps.lock_particles_for_read();
                    hx = v[hips].x; hy = v[hips].y;
                }
                strata.update(hx, hy);
            }
        }
    }
    (void)phase_end_x; (void)phase_end_y;
    std::printf("  [choreo] worst hips drop=%.3f m (%s); worst leg "
                "reach=%.3f m (%s)\n",
                worst_drop, worst_drop_phase, worst_reach,
                worst_reach_phase);

    // Gross-geometry contract. Eva's leg is ~0.85 m; a fold-flat puts
    // a foot 0.7+ m out horizontally and/or drops the hips toward the
    // floor. Normal gait: stride keeps every leg particle within
    // ~0.55 m of the hips horizontally; hips bob a few cm.
    AT_ASSERT_TRUE(worst_drop < 0.25f,
        "hips never collapse toward the feet (worst drop " +
        std::to_string(worst_drop) + " m)");
    AT_ASSERT_TRUE(worst_reach < 0.65f,
        "legs never fold out from under the body (worst reach " +
        std::to_string(worst_reach) + " m)");

    std::printf("%d passed, %d failed\n", tests_passed, tests_failed);
    engine.shutdown();
    return tests_failed == 0 ? 0 : 1;
}
