// =============================================================================
// SPIDER-EVA SHIN CRUSH — narrow wedge test
// =============================================================================
// Reproduces the specific failure mode captured in Eden on 2026-04-12:
// Eva walking SW at ~3.3 m/s on a flat strata floor collapses after ~170
// frames. Left shin and left thigh collapse onto each other — baseline
// 0.419 m, observed 0.079 m (a −340 mm compression).
//
// A normal walk-cycle knee flex compresses bone-midpoint distance to roughly
// 0.25–0.30 m (≈60° interior angle). Anything below 0.15 m is pathological:
// shin and thigh nearly coincident = dismemberment, not animation.
//
// This test is the WEDGE. It's intentionally minimal — no obstacles, no NPCs,
// no trench, no look-at spin — so a failure points straight at the mechanism
// that produces the crush, not at surrounding interactions. The broader
// `test_humanoid_strata_integrity` did not reproduce; the likely differentiator
// is speed (3.3 m/s, above the 2 m/s walk target) + stride extension under
// foot-planting IK.
//
// Run: ./build/logosphere-tests --test test_spider_eva_shin_crush --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/worldgen/worldgen_system.h"
#include "logosphere/worldgen/strata_floor_generator.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include <cstdio>
#include <cmath>
#include <vector>

bool test_spider_eva_shin_crush() {
    printf("\n=== Spider-Eva Shin Crush (wedge) ===\n");

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "spider eva shin crush";
    cfg.enable_chat_window = false;
    if (engine.initialize(cfg) != 0) {
        printf("  ERROR: engine init failed\n");
        return false;
    }

    auto& ps      = engine.get_particle_system();
    auto& dyn     = engine.get_dynamics_system();

    // --- Strata floor, same layer config Eden uses when the bug fires.
    auto& strata = engine.get_worldgen_system().get_strata_floor_generator();
    strata.set_tile_size(4.0f);
    strata.set_tiles_per_chunk(5);
    strata.set_tiles_per_entity(1);
    strata.set_load_radius(60.0f);
    strata.set_unload_radius(70.0f);

    std::vector<StrataLayerSpec> layers;
    auto add_layer = [&](const char* n, Materials::Type m, float th,
                         float r, float g, float b, bool bond, float bs) {
        StrataLayerSpec s;
        s.name = n; s.material = m; s.thickness = th;
        s.r = r; s.g = g; s.b = b;
        s.bond_within_layer = bond;
        s.bond_strength = bs;
        layers.push_back(s);
    };
    add_layer("bedrock",  Materials::Type::STONE, 0.30f, 0.35f, 0.33f, 0.30f, true,  8000.0f);
    add_layer("sediment", Materials::Type::STONE, 0.15f, 0.45f, 0.40f, 0.30f, false, 0.0f);
    add_layer("organic",  Materials::Type::DIRT,  0.10f, 0.30f, 0.45f, 0.22f, false, 0.0f);
    strata.set_layers(std::move(layers));
    strata.set_enabled(true);
    strata.preload_chunks_around(25.0f, -25.0f, 3);

    // --- Eva, at Eden's spawn position. The bug fires after Eva travels
    // from (25,-25) to (20,-41) — matching Eden so chunk swap indices and
    // BVH state align with the reproducing conditions.
    auto& hgen = engine.get_worldgen_system().get_humanoid_generator();
    auto eva = hgen.generate_humanoid_physics(
        // 0.55, not 0.5: world_z is the FEET'S BOTTOM and this scene's strata
        // surface is 0.30 + 0.15 + 0.10 = 0.55 (INV-37).
        25.0f, -25.0f, 0.55f, -1, HumanoidSpec::eva(), false);
    auto& kg = engine.get_kg();
    eva.create_kg_entities(kg, "Human", 180.0f, 800.0f);
    engine.get_humanoid_locomotion().register_humanoid_direct(
        eva.hips_id,
        eva.left_leg_ids, eva.right_leg_ids,
        eva.left_arm_ids, eva.right_arm_ids,
        eva.torso_ids, 180.0f, 800.0f);

    // Leg particle IDs. Generator push order (see humanoid_generator.cpp:570-628):
    //   left_leg_particles = [foot, toe, shin, thigh]
    int l_foot_id  = eva.left_leg_ids.size()  > 0 ? eva.left_leg_ids[0]  : -1;
    int l_shin_id  = eva.left_leg_ids.size()  > 2 ? eva.left_leg_ids[2]  : -1;
    int l_thigh_id = eva.left_leg_ids.size()  > 3 ? eva.left_leg_ids[3]  : -1;
    int r_shin_id  = eva.right_leg_ids.size() > 2 ? eva.right_leg_ids[2] : -1;
    int r_thigh_id = eva.right_leg_ids.size() > 3 ? eva.right_leg_ids[3] : -1;
    int hips_cache = eva.hips_id;

    if (l_shin_id < 0 || l_thigh_id < 0 || r_shin_id < 0 || r_thigh_id < 0) {
        printf("  ERROR: could not resolve leg particle IDs\n");
        return false;
    }

    // Swap-safe caching — strata streaming triggers swap-and-pop cascades.
    int& l_shin_ref  = l_shin_id;
    int& l_thigh_ref = l_thigh_id;
    int& r_shin_ref  = r_shin_id;
    int& r_thigh_ref = r_thigh_id;
    int& l_foot_ref  = l_foot_id;
    int& hips_ref    = hips_cache;
    ps.add_swap_callback([&](size_t old_idx, size_t new_idx) {
        auto fixup = [&](int& id) { if (id == (int)old_idx) id = (int)new_idx; };
        fixup(l_shin_ref); fixup(l_thigh_ref);
        fixup(r_shin_ref); fixup(r_thigh_ref);
        fixup(l_foot_ref); fixup(hips_ref);
    });

    // --- Settle Eva onto strata.
    const float settle_dt = 1.0f / 60.0f;
    for (int i = 0; i < 30; ++i) engine.update(settle_dt);

    // Baseline bone-midpoint distances (recorded at rest).
    float baseline_l = 0.0f;
    float baseline_r = 0.0f;
    {
        auto v = ps.lock_particles_for_read();
        const Particle& ls = v[l_shin_id];
        const Particle& lt = v[l_thigh_id];
        const Particle& rs = v[r_shin_id];
        const Particle& rt = v[r_thigh_id];
        baseline_l = std::sqrt((ls.x-lt.x)*(ls.x-lt.x) + (ls.y-lt.y)*(ls.y-lt.y) + (ls.z-lt.z)*(ls.z-lt.z));
        baseline_r = std::sqrt((rs.x-rt.x)*(rs.x-rt.x) + (rs.y-rt.y)*(rs.y-rt.y) + (rs.z-rt.z)*(rs.z-rt.z));
    }
    printf("  Baselines: l_shin<>l_thigh=%.3f m  r_shin<>r_thigh=%.3f m\n",
           baseline_l, baseline_r);

    // --- Match Eden's input pattern: PlayerController feeds
    // set_body_relative_velocity (body frame, not world frame) + a moving
    // look-at target that rotates Eva. As Eva's facing rotates, her
    // world-velocity recomputes every frame — this is what continuously
    // re-triggers heel-strike plant_target recomputation.
    //
    // Empirics from earlier attempts:
    //   - World-frame set_target_velocity at |v|=3.32, static facing → PASS
    //     (min shin<>thigh 0.561 m, just normal run flex).
    //   - World-frame velocity + rotating look-at → PASS (Eva rotates but
    //     world-velocity is fixed, so plant targets are consistent).
    //   - Body-relative velocity + rotating look-at → matches Eden; this is
    //     the condition we expect to reproduce under.
    engine.get_humanoid_locomotion().set_volitional(eva.hips_id, true);

    // --- Threshold. Normal knee flex: shin<>thigh compresses to ~0.25–0.30 m.
    // Eden observed 0.079 m. Any frame with either leg below 0.22 m fails.
    // Tightened from 0.15 → 0.22 (2026-04-18): current min across the 330-
    // frame walk is 0.510 m — plenty of margin, and 0.22 still cleanly
    // separates "normal deep knee flex" from "pathological crush". 0.22
    // is also ≈3σ above the lunge depth of a proper stride.
    constexpr float CRUSH_LIMIT = 0.22f;

    // Run 300 frames — Eden's bug fired at f170; give margin for the
    // look-at rotation to align with a heel-strike + chunk swap.
    constexpr int   MAX_FRAMES = 300;

    float min_l = 1e9f, min_r = 1e9f;
    int   min_l_frame = -1, min_r_frame = -1;
    bool  crushed = false;
    int   crush_frame = -1;
    float crush_l = 0.0f, crush_r = 0.0f;

    // Deterministic PRNG for reproducible velocity + look-at jitter.
    unsigned int seed = 0xC0FFEE;
    auto rnd = [&seed]() { seed = seed * 1103515245u + 12345u; return (seed >> 8) & 0xFFFF; };
    auto rnd_range = [&](float lo, float hi) {
        return lo + (rnd() / 65535.0f) * (hi - lo);
    };

    for (int frame = 0; frame < MAX_FRAMES; ++frame) {
        // Inject noisy WASD-style input: change body-relative velocity every
        // 15 frames (0.25 s), jump the look-at target every 5 frames (twitchy
        // mouse). Speed range covers walk (2) through sprint (6). This is
        // the harness Eden showed: input changing faster than the walk
        // cycle completes, IK plant target re-armed at poorly-chosen phases,
        // facing rotating while stride is mid-swing.
        if (frame % 15 == 0) {
            float fwd = rnd_range(0.0f, 6.0f);
            float str = rnd_range(-2.0f, 2.0f);
            engine.get_humanoid_locomotion().set_body_relative_velocity(eva.hips_id, fwd, str);
        }
        if (frame % 5 == 0) {
            auto v = ps.lock_particles_for_read();
            const Particle& hips = v[hips_ref];
            float tx = hips.x + rnd_range(-8.0f, 8.0f);
            float ty = hips.y + rnd_range(-8.0f, 8.0f);
            engine.get_humanoid_locomotion().set_look_at_target(eva.hips_id, tx, ty);
        }

        // Variable dt mimics Eden under GPU load (10–25 ms frames).
        float dt = (frame % 14 < 7) ? (1.0f / 60.0f) : (22.0f / 1000.0f);
        engine.update(dt);

        float hx = 0.0f, hy = 0.0f;
        float dl = 0.0f, dr = 0.0f;
        {
            auto v = ps.lock_particles_for_read();
            const Particle& hips = v[hips_ref];
            hx = hips.x; hy = hips.y;
            const Particle& ls = v[l_shin_ref];
            const Particle& lt = v[l_thigh_ref];
            const Particle& rs = v[r_shin_ref];
            const Particle& rt = v[r_thigh_ref];
            dl = std::sqrt((ls.x-lt.x)*(ls.x-lt.x) + (ls.y-lt.y)*(ls.y-lt.y) + (ls.z-lt.z)*(ls.z-lt.z));
            dr = std::sqrt((rs.x-rt.x)*(rs.x-rt.x) + (rs.y-rt.y)*(rs.y-rt.y) + (rs.z-rt.z)*(rs.z-rt.z));
            if (dl < min_l) { min_l = dl; min_l_frame = frame; }
            if (dr < min_r) { min_r = dr; min_r_frame = frame; }
        }
        strata.update(hx, hy);

        if (!crushed && (dl < CRUSH_LIMIT || dr < CRUSH_LIMIT)) {
            crushed = true;
            crush_frame = frame;
            crush_l = dl; crush_r = dr;
            printf("  >>> CRUSH f%d: l_shin<>l_thigh=%.3f  r_shin<>r_thigh=%.3f\n",
                   frame, dl, dr);
        }

        if (frame % 30 == 0) {
            printf("  [f%3d] hips=(%.2f,%.2f)  l_s<>l_t=%.3f  r_s<>r_t=%.3f\n",
                   frame, hx, hy, dl, dr);
        }
    }

    printf("\n  Summary:\n");
    printf("    min l_shin<>l_thigh: %.3f m @ f%d (baseline %.3f, limit %.3f)\n",
           min_l, min_l_frame, baseline_l, CRUSH_LIMIT);
    printf("    min r_shin<>r_thigh: %.3f m @ f%d (baseline %.3f, limit %.3f)\n",
           min_r, min_r_frame, baseline_r, CRUSH_LIMIT);
    if (crushed) {
        printf("    FIRST CRUSH: f%d  l=%.3f  r=%.3f\n", crush_frame, crush_l, crush_r);
    }

    bool ok = !crushed;
    printf("  %s\n", ok ? "[PASS]" : "[FAIL — spider-Eva dismemberment reproduced]");

    // Status as of 2026-04-12:
    //   - Eden reliably reproduces the crush (deep probe fires at f160–196
    //     across three runs, l_shin<>l_thigh collapses to 0.079 m vs a
    //     walking baseline of 0.419 m — a 340 mm compression).
    //   - This wedge runs the closest-matching headless scenario: Eva spawn
    //     at (25,-25), strata floor, body-relative velocity + look-at
    //     rotation + jittery input + variable dt. Min compression observed
    //     ~0.12 m (pure knee flex). No crush.
    //   - The wedge therefore does NOT yet reproduce. It stands as a tight
    //     regression boundary: the day headless starts exhibiting the bug
    //     (or a fix regresses), this test will catch it within 300 frames.
    //   - Suspect trigger we don't yet replicate: the actual PlayerController
    //     input path (GLFW events, NOT synthetic) or an interaction with the
    //     GPU render pipeline that changes chunk-unload timing. Worth a
    //     focused investigation with logging added inside
    //     anticipate_step_climbing + foot_planting IK in the interactive run.

    return ok;
}
