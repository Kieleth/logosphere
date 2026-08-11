// =============================================================================
// JOINT HIERARCHY SWAP INTEGRITY
// =============================================================================
// Regression test for a chunk-streaming bookkeeping gap discovered while
// diagnosing Eva's upper-body drift in Eden. The mechanism:
//
//   HumanoidParts::joint_hierarchy stores each joint's parent / child
//   particle as an integer index into ParticleSystem.
//   ParticleSystem uses swap-and-pop on chunk unload: the particle at
//   the last index moves to the slot of the removed particle. Every
//   cached ID in game state must be updated via the swap callback.
//
//   notify_particle_swap in particle_dynamics_system.cpp fixed the
//   obvious caches (parts.hips, left_leg_particles, all_particle_indices,
//   root.particle_id, …) but NOT joint_hierarchy.joints[i].parent_particle
//   or .child_particle. After the first chunk swap, FK wrote to WHATEVER
//   particle happened to land at Eva's old indices (a floor tile or tree
//   segment), while Eva's actual chest / neck / shoulders kept their
//   stale ANIMATION ownership and were never rewritten.
//
//   snap_to_hips skips ANIMATION-owned particles, so chest was not
//   re-placed relative to hips either. Net effect: hips advances by
//   velocity*dt every frame, chest doesn't, upper body drifts behind
//   hips until the deep-probe pair-separation threshold (0.3 m) fires.
//
// What this test codifies: after a humanoid walks far enough to trigger
// chunk unload (→ particle swaps → notify_particle_swap fires), the
// body's spine remains structurally intact. Hips<>chest horizontal
// distance must stay near rest (< 10 cm) — in a working system chest
// sits directly above hips and only knee flex / hip hike wiggle the pair
// marginally.
//
// On the bug: hips<>chest horizontal distance grows at ~velocity*dt per
// frame after the first swap, blowing well past the 10 cm threshold in
// a few hundred frames.
//
// Run: ./build/logosphere-tests --test test_joint_hierarchy_swap_integrity --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/worldgen/worldgen_system.h"
#include "logosphere/worldgen/strata_floor_generator.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include <cstdio>
#include <cmath>
#include <vector>

bool test_joint_hierarchy_swap_integrity() {
    printf("\n=== Joint Hierarchy Swap Integrity ===\n");

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "joint swap integrity";
    cfg.enable_chat_window = false;
    if (engine.initialize(cfg) != 0) {
        printf("  ERROR: engine init failed\n");
        return false;
    }

    auto& ps  = engine.get_particle_system();
    auto& dyn = engine.get_dynamics_system();

    // Pile of "throwaway" particles created FIRST — they occupy low
    // indices. Eva is created AFTER them, so her particles sit at higher
    // indices (the tail of the particle array). When we remove throwaways
    // via swap-and-pop, the last particle (an Eva body part) moves down
    // into the vacated slot, firing notify_particle_swap(last_idx, low_idx).
    // That is the exact path that goes stale in Eden when chunks unload.
    std::vector<int> throwaway_ids;
    for (int i = 0; i < 60; ++i) {
        Particle d = {};
        d.shape = ParticleShape::BOX;
        d.x = 100.0f + i * 0.5f;  // Far from Eva so physics doesn't interact
        d.y = 100.0f;
        d.z = 5.0f;
        d.width = d.height = d.thickness = 0.1f;
        d.r = 0.5f; d.g = 0.5f; d.b = 0.5f; d.a = 1.0f;
        d.SetMaterial(Materials::Type::STONE);
        d.owner = ParticleOwner::STATIC;
        d.is_at_rest = true;
        int id = engine.add_particle(d);
        throwaway_ids.push_back(id);
    }

    // Ground: a big box under Eva so she has something to stand on.
    Particle floor = {};
    floor.shape = ParticleShape::BOX;
    floor.x = 0; floor.y = 0; floor.z = 0.025f;  // bottom on the turtle, top unchanged at 0.05
    floor.width = 200; floor.height = 200; floor.thickness = 0.05f;
    floor.r = 0.3f; floor.g = 0.5f; floor.b = 0.2f; floor.a = 1.0f;
    floor.SetMaterial(Materials::Type::STONE);
    floor.is_at_rest = true;
    int floor_id = engine.add_particle(floor);

    // --- Eva at origin (created last — she sits at the tail of the array).
    auto& hgen = engine.get_worldgen_system().get_humanoid_generator();
    auto eva = hgen.generate_humanoid_physics(
        0.0f, 0.0f, 0.5f, -1, HumanoidSpec::eva(), false);
    auto& kg = engine.get_kg();
    eva.create_kg_entities(kg, "Human", 180.0f, 800.0f);
    engine.get_humanoid_locomotion().register_humanoid_direct(
        eva.hips_id,
        eva.left_leg_ids, eva.right_leg_ids,
        eva.left_arm_ids, eva.right_arm_ids,
        eva.torso_ids, 180.0f, 800.0f);

    // body_ids order (humanoid_generator.cpp):
    //   8: hips, 9: abdomen, 10: chest, 11: neck, 21: l_shoulder, 22: r_shoulder
    printf("  eva.body_ids: size=%zu  [0]=%d  [8]=%d  [10]=%d\n",
           eva.body_ids.size(), eva.body_ids[0], eva.body_ids.at(8), eva.body_ids.at(10));
    int hips_id   = eva.body_ids.at(8);
    int chest_id  = eva.body_ids.at(10);
    int neck_id   = eva.body_ids.at(11);
    int l_sh_id   = eva.body_ids.at(21);
    int r_sh_id   = eva.body_ids.at(22);

    // Swap-safe id tracking — notify_particle_swap must keep these in sync.
    int swap_count = 0;
    ps.add_swap_callback([&](size_t old_idx, size_t new_idx) {
        auto fix = [&](int& id) { if (id == (int)old_idx) id = (int)new_idx; };
        fix(hips_id); fix(chest_id); fix(neck_id); fix(l_sh_id); fix(r_sh_id);
        fix(floor_id);
        for (auto& d : throwaway_ids) fix(d);
        ++swap_count;
    });

    // --- Settle 30 frames, then walk forward at 3 m/s for 600 frames.
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 30; ++i) engine.update(dt);

    // Baseline horizontal distances (at rest pose, chest/neck/shoulders
    // are directly above hips so this should be near zero).
    auto measure_horizontal = [&](int a_id, int b_id) {
        auto v = ps.lock_particles_for_read();
        const Particle& pa = v[a_id];
        const Particle& pb = v[b_id];
        float dx = pa.x - pb.x;
        float dy = pa.y - pb.y;
        return std::sqrt(dx*dx + dy*dy);
    };

    float base_hc  = measure_horizontal(hips_id, chest_id);
    float base_hn  = measure_horizontal(hips_id, neck_id);
    float base_hls = measure_horizontal(hips_id, l_sh_id);
    float base_hrs = measure_horizontal(hips_id, r_sh_id);
    printf("  Baseline horizontal distances from hips:\n");
    printf("    hips<>chest      = %.3f m\n", base_hc);
    printf("    hips<>neck       = %.3f m\n", base_hn);
    printf("    hips<>l_shoulder = %.3f m\n", base_hls);
    printf("    hips<>r_shoulder = %.3f m\n", base_hrs);

    engine.get_humanoid_locomotion().set_volitional(eva.hips_id, true);
    engine.get_humanoid_locomotion().set_body_relative_velocity(eva.hips_id, 3.0f, 0.0f);

    // Drift from baseline. Walking wiggle with the fix in place: ~3 cm
    // for chest / ~5 cm for neck (torso leans forward during stride).
    // Bug without the fix: 6-10 m drift in 600 frames (catastrophic).
    // 8 cm threshold gives margin for walk animation while catching any
    // reappearance of the stale-joint class of bug.
    constexpr int FRAMES = 600;
    constexpr float DRIFT_LIMIT = 0.08f;
    float max_hc = 0.0f, max_hn = 0.0f, max_hls = 0.0f, max_hrs = 0.0f;
    int   max_hc_frame = 0;
    bool  breached = false;
    int   breach_frame = -1;

    for (int frame = 0; frame < FRAMES; ++frame) {
        float hx = 0.0f, hy = 0.0f;
        {
            auto v = ps.lock_particles_for_read();
            hx = v[hips_id].x; hy = v[hips_id].y;
        }

        // Remove one throwaway particle every 5 frames for the first half
        // of the test. Throwaways were created BEFORE Eva, so each removal
        // triggers swap-and-pop that moves an Eva body part (the last
        // particle in the array) into a low-index slot — exactly the
        // index shift that chunk streaming causes in Eden.
        const bool enable_swaps = true;
        if (enable_swaps && frame < 300 && (frame % 5 == 0)) {
            int tid = -1;
            for (auto& t : throwaway_ids) {
                if (t >= 0) { tid = t; t = -1; break; }
            }
            if (tid >= 0) {
                ps.remove_particle(static_cast<size_t>(tid));
            }
        }

        engine.update(dt);

        float hc  = measure_horizontal(hips_id, chest_id);
        float hn  = measure_horizontal(hips_id, neck_id);
        float hls = measure_horizontal(hips_id, l_sh_id);
        float hrs = measure_horizontal(hips_id, r_sh_id);
        if (hc  > max_hc ) { max_hc  = hc;  max_hc_frame = frame; }
        if (hn  > max_hn ) max_hn  = hn;
        if (hls > max_hls) max_hls = hls;
        if (hrs > max_hrs) max_hrs = hrs;

        if (!breached && (hc - base_hc) > DRIFT_LIMIT) {
            breached = true;
            breach_frame = frame;
            printf("  >>> FIRST BREACH f%d  hips<>chest = %.3f m (baseline %.3f, limit %.2f)\n",
                   frame, hc, base_hc, DRIFT_LIMIT);
        }

        if (frame % 100 == 0) {
            int chest_owner = 0, hips_owner = 0;
            {
                auto v = ps.lock_particles_for_read();
                chest_owner = (int)v[chest_id].owner;
                hips_owner  = (int)v[hips_id].owner;
            }
            printf("  [f%4d] hips=(%.2f,%.2f) id=%d/owner=%d  chest_id=%d/owner=%d  h<>c=%.3f h<>n=%.3f h<>ls=%.3f h<>rs=%.3f  swaps=%d\n",
                   frame, hx, hy, hips_id, hips_owner, chest_id, chest_owner,
                   hc, hn, hls, hrs, swap_count);
        }
    }

    float drift_hc  = std::max(0.0f, max_hc  - base_hc);
    float drift_hn  = std::max(0.0f, max_hn  - base_hn);
    float drift_hls = std::max(0.0f, max_hls - base_hls);
    float drift_hrs = std::max(0.0f, max_hrs - base_hrs);

    printf("\n  Summary after %d frames of walking (%d swaps fired):\n",
           FRAMES, swap_count);
    printf("    hips<>chest      drift = %.3f m (max %.3f, baseline %.3f)\n",
           drift_hc, max_hc, base_hc);
    printf("    hips<>neck       drift = %.3f m (max %.3f, baseline %.3f)\n",
           drift_hn, max_hn, base_hn);
    printf("    hips<>l_shoulder drift = %.3f m (max %.3f, baseline %.3f)\n",
           drift_hls, max_hls, base_hls);
    printf("    hips<>r_shoulder drift = %.3f m (max %.3f, baseline %.3f)\n",
           drift_hrs, max_hrs, base_hrs);
    printf("    final hips_id=%d  chest_id=%d  (started 1883 / 1885)\n",
           hips_id, chest_id);
    printf("    drift limit = %.3f m\n", DRIFT_LIMIT);

    bool ok = (drift_hc  <= DRIFT_LIMIT) && (drift_hn  <= DRIFT_LIMIT)
           && (drift_hls <= DRIFT_LIMIT) && (drift_hrs <= DRIFT_LIMIT);
    printf("  %s\n", ok ? "[PASS]" : "[FAIL — joint hierarchy swap bookkeeping broken]");
    if (breached) {
        printf("         first breach at f%d (chunk unloads after ~300 frames)\n",
               breach_frame);
    }
    return ok;
}
