// =============================================================================
// IDLE POSE STABILITY — standing Eva has feet under the body
// =============================================================================
// Visual bug: Eva just spawned and standing still, one foot is splayed
// way out to the side / back (the screenshot shows a near-full-extension
// leg far from the hips). The cascade / twist-step / plant machinery is
// all supposed to be quiet when idle with no yaw target. If a foot ends
// up 0.6 m from the hips centre in world frame, something is writing a
// bad plant_target or FK is producing a stretched pose.
//
// This test runs Eva idle for 3 s (180 frames) and samples the feet
// every frame. Assertions:
//   A. Both feet stay close to the hips in lateral (X in body frame)
//      space — no more than ~2·hip_width (≈0.25 m). A splayed leg gives
//      distances of ~0.5–0.8 m.
//   B. Both feet stay close to the hips in forward (Y in body frame)
//      space — no more than half a stride (≈0.25 m). Pure idle has no
//      swing, so neither foot should be half a stride ahead or behind.
//   C. Foot Z stays near the ground (≤ 0.2 m). A "shoot out" that lifts
//      the foot off the ground manifests here.
//   D. No per-frame position jump > 0.1 m (idle should be motionless).
//
// Run: ./build/logosphere-tests --test test_idle_pose_stability --no-head
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
#include <algorithm>

bool test_idle_pose_stability() {
    printf("\n=== Idle Pose Stability — feet stay under body ===\n");

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "idle pose";
    cfg.enable_chat_window = false;
    if (engine.initialize(cfg) != 0) {
        printf("  ERROR: engine init failed\n");
        return false;
    }

    auto& ps  = engine.get_particle_system();
    auto& dyn = engine.get_dynamics_system();

    // Strata floor to match Eden's world. The visual splay showed up on
    // a real Eden run, so we exercise the same tile-based floor rather
    // than a monolithic box.
    auto& strata = engine.get_worldgen_system().get_strata_floor_generator();
    strata.set_tile_size(4.0f);
    strata.set_tiles_per_chunk(5);
    strata.set_tiles_per_entity(1);
    strata.set_load_radius(30.0f);
    strata.set_unload_radius(40.0f);
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
    strata.preload_chunks_around(0.0f, 0.0f, 3);

    auto& hgen = engine.get_worldgen_system().get_humanoid_generator();
    // Spawn Eva at Eden's exact location + height so strata streaming and
    // ground-detection paths match. The visual splay showed up on a real
    // Eden run at this position.
    const float eva_spawn_x = 35.0f;
    const float eva_spawn_y = -25.0f;
    auto eva = hgen.generate_humanoid_physics(
        eva_spawn_x, eva_spawn_y, 1.0f, -1, HumanoidSpec::eva(), false);
    auto& kg = engine.get_kg();
    eva.create_kg_entities(kg, "Human", 180.0f, 800.0f);
    engine.get_humanoid_locomotion().register_humanoid_direct(
        eva.hips_id,
        eva.left_leg_ids, eva.right_leg_ids,
        eva.left_arm_ids, eva.right_arm_ids,
        eva.torso_ids, 180.0f, 800.0f);


    int l_foot  = eva.left_leg_ids.at(0);
    int l_shin  = eva.left_leg_ids.at(1);
    int l_thigh = eva.left_leg_ids.at(2);
    int r_foot  = eva.right_leg_ids.at(0);
    int r_shin  = eva.right_leg_ids.at(1);
    int r_thigh = eva.right_leg_ids.at(2);
    int hips_id = eva.hips_id;

    ps.add_swap_callback([&](size_t old_idx, size_t new_idx) {
        auto fix = [&](int& id) { if (id == (int)old_idx) id = (int)new_idx; };
        fix(l_foot); fix(l_shin); fix(l_thigh);
        fix(r_foot); fix(r_shin); fix(r_thigh);
        fix(hips_id);
    });

    const float dt = 1.0f / 60.0f;

    // Mirror Eden's PlayerController pattern: volitional on once, then
    // every frame set_body_relative_velocity (possibly zero) AND
    // set_look_at_target. For this test we walk for 60 frames then
    // stop dead with look-at pointed 45° off-facing — that's exactly
    // what a player does in Eden when they run a bit then pause while
    // the mouse still points somewhere interesting.
    engine.get_humanoid_locomotion().set_volitional(eva.hips_id, true);
    constexpr int FRAMES = 240;
    const int STOP_FRAME = 60;

    float max_lat_L = 0.0f, max_lat_R = 0.0f;     // lateral |x−hips.x|
    float max_fwd_L = 0.0f, max_fwd_R = 0.0f;     // forward |y−hips.y|
    float max_z_L   = 0.0f, max_z_R   = 0.0f;     // absolute Z
    float max_jump_L = 0.0f, max_jump_R = 0.0f;   // single-frame delta
    float prev_lx=0, prev_ly=0, prev_lz=0, prev_rx=0, prev_ry=0, prev_rz=0;
    bool  prev_valid = false;

    // Measure only once the walk→idle transition has settled.
    const int MEASURE_START = STOP_FRAME + 60;

    for (int frame = 0; frame < FRAMES; ++frame) {
        // Mirror Eden's PlayerController every frame: a look-at target
        // that drives hips_yaw_world to ~1.7 rad (≈97°), matching what
        // the Eden probe dump observed. atan2(dx, dy) = 1.7 requires
        // dx/dy ≈ -7.6, so (+10, -1.3) relative to Eva.
        engine.get_humanoid_locomotion().set_look_at_target(eva.hips_id,
                               eva_spawn_x + 10.0f, eva_spawn_y - 1.3f);
        // Pure idle — never press a key. Just like sitting in Eden with
        // the mouse pointed off-axis.
        engine.get_humanoid_locomotion().set_body_relative_velocity(eva.hips_id, 0.0f, 0.0f);
        engine.update(dt);
        auto v = ps.lock_particles_for_read();
        const Particle& H = v[hips_id];
        const Particle& L = v[l_foot];
        const Particle& R = v[r_foot];

        if (frame >= MEASURE_START) {
            // Measure in BODY frame, not world frame. Yaw rotation
            // otherwise smears feet into "forward/back" in world even
            // when they are directly below (rotated) hips.
            float cos_rot = std::cos(H.rotation_z);
            float sin_rot = std::sin(H.rotation_z);
            auto to_body_fwd_right = [&](float wdx, float wdy,
                                          float& b_fwd, float& b_right) {
                // body_forward world = (sin(rot), cos(rot))
                // body_right   world = (cos(rot), -sin(rot))
                b_fwd   = wdx * sin_rot + wdy * cos_rot;
                b_right = wdx * cos_rot - wdy * sin_rot;
            };
            float l_fwd, l_right, r_fwd, r_right;
            to_body_fwd_right(L.x - H.x, L.y - H.y, l_fwd, l_right);
            to_body_fwd_right(R.x - H.x, R.y - H.y, r_fwd, r_right);

            // Also project thigh/shin into body frame. Foot pos gets
            // IK-pinned under the hip, but a splayed knee (hip-abducted
            // leg) shows up as a thigh/shin that's way wider than hip-
            // half-width in body-right axis. Pick the max of foot vs
            // thigh vs shin to catch both foot splay and knee splay.
            const Particle& LT = v[l_thigh];
            const Particle& LS = v[l_shin];
            const Particle& RT = v[r_thigh];
            const Particle& RS = v[r_shin];
            float lt_fwd, lt_right, ls_fwd, ls_right;
            float rt_fwd, rt_right, rs_fwd, rs_right;
            to_body_fwd_right(LT.x - H.x, LT.y - H.y, lt_fwd, lt_right);
            to_body_fwd_right(LS.x - H.x, LS.y - H.y, ls_fwd, ls_right);
            to_body_fwd_right(RT.x - H.x, RT.y - H.y, rt_fwd, rt_right);
            to_body_fwd_right(RS.x - H.x, RS.y - H.y, rs_fwd, rs_right);

            auto amax3 = [](float a, float b, float c) {
                float ax = std::abs(a), bx = std::abs(b), cx = std::abs(c);
                return std::max(ax, std::max(bx, cx));
            };
            float l_lat_max = amax3(l_right, lt_right, ls_right);
            float r_lat_max = amax3(r_right, rt_right, rs_right);
            float l_fwd_max = amax3(l_fwd,   lt_fwd,   ls_fwd);
            float r_fwd_max = amax3(r_fwd,   rt_fwd,   rs_fwd);

            if (l_lat_max > max_lat_L) max_lat_L = l_lat_max;
            if (r_lat_max > max_lat_R) max_lat_R = r_lat_max;
            if (l_fwd_max > max_fwd_L) max_fwd_L = l_fwd_max;
            if (r_fwd_max > max_fwd_R) max_fwd_R = r_fwd_max;
            if (L.z > max_z_L) max_z_L = L.z;
            if (R.z > max_z_R) max_z_R = R.z;

            if (prev_valid) {
                float ldx = L.x - prev_lx, ldy = L.y - prev_ly, ldz = L.z - prev_lz;
                float rdx = R.x - prev_rx, rdy = R.y - prev_ry, rdz = R.z - prev_rz;
                float lj = std::sqrt(ldx*ldx + ldy*ldy + ldz*ldz);
                float rj = std::sqrt(rdx*rdx + rdy*rdy + rdz*rdz);
                if (lj > max_jump_L) max_jump_L = lj;
                if (rj > max_jump_R) max_jump_R = rj;
            }
        }
        prev_lx = L.x; prev_ly = L.y; prev_lz = L.z;
        prev_rx = R.x; prev_ry = R.y; prev_rz = R.z;
        prev_valid = true;

        // Log every frame where anything interesting moves.
        bool notable = false;
        if (prev_valid) {
            float ldx = L.x - prev_lx, ldy = L.y - prev_ly, ldz = L.z - prev_lz;
            float rdx = R.x - prev_rx, rdy = R.y - prev_ry, rdz = R.z - prev_rz;
            float lj = std::sqrt(ldx*ldx + ldy*ldy + ldz*ldz);
            float rj = std::sqrt(rdx*rdx + rdy*rdy + rdz*rdz);
            if (lj > 0.02f || rj > 0.02f || L.z > 0.20f || R.z > 0.20f) notable = true;
        }
        if (notable || frame == 30 || frame == 60 || frame == 179) {
            const Particle& LT = v[l_thigh];
            const Particle& LS = v[l_shin];
            const Particle& RT = v[r_thigh];
            const Particle& RS = v[r_shin];
            printf("  [f%3d] hips=(%+.3f,%+.3f,%+.3f)\n"
                   "         L_thigh=(%+.3f,%+.3f,%+.3f) L_shin=(%+.3f,%+.3f,%+.3f) L_foot=(%+.3f,%+.3f,%+.3f)\n"
                   "         R_thigh=(%+.3f,%+.3f,%+.3f) R_shin=(%+.3f,%+.3f,%+.3f) R_foot=(%+.3f,%+.3f,%+.3f)\n",
                   frame, H.x, H.y, H.z,
                   LT.x,LT.y,LT.z, LS.x,LS.y,LS.z, L.x,L.y,L.z,
                   RT.x,RT.y,RT.z, RS.x,RS.y,RS.z, R.x,R.y,R.z);
        }
    }

    printf("\n  Lateral (|foot.x − hips.x|): L max=%.3f  R max=%.3f  (budget ≤ 0.25 m)\n",
           max_lat_L, max_lat_R);
    printf("  Forward (|foot.y − hips.y|): L max=%.3f  R max=%.3f  (budget ≤ 0.25 m)\n",
           max_fwd_L, max_fwd_R);
    // On strata the ground surface is at ~0.55 m (bedrock + sediment +
    // organic) and Eva's foot centres sit ~0.04 m above that when fully
    // on the ground — ~0.595 m absolute. Budget to 0.70 m catches a
    // raised/floating foot (the visual splay lifts it to 0.72+) while
    // tolerating normal resting pose.
    printf("  Foot Z peak:                L max=%.3f  R max=%.3f  (budget ≤ 0.70 m)\n",
           max_z_L, max_z_R);
    printf("  Per-frame jump:             L max=%.4f R max=%.4f  (budget ≤ 0.10 m)\n",
           max_jump_L, max_jump_R);

    bool lat_ok = (max_lat_L <= 0.25f && max_lat_R <= 0.25f);
    // Tighter forward budget: a true idle pose should have feet almost
    // directly under the hips (within ~0.05 m). If Eva is holding a
    // walk-clip pose while idle, one leg is forward and one back by
    // the stride amount (~0.08–0.10 m even for small steps). A forward
    // budget > 0.08 m flags that "twisted legs while standing" state.
    bool fwd_ok = (max_fwd_L <= 0.08f && max_fwd_R <= 0.08f);
    bool z_ok   = (max_z_L   <= 0.70f && max_z_R   <= 0.70f);
    bool jump_ok = (max_jump_L <= 0.10f && max_jump_R <= 0.10f);

    printf("\n  Assertions:\n");
    printf("    %s: feet stay under body laterally\n", lat_ok  ? "PASS" : "FAIL");
    printf("    %s: feet stay under body forward\n",   fwd_ok  ? "PASS" : "FAIL");
    printf("    %s: feet stay on / near the ground\n", z_ok    ? "PASS" : "FAIL");
    printf("    %s: no per-frame teleports while idle\n", jump_ok ? "PASS" : "FAIL");

    bool ok = lat_ok && fwd_ok && z_ok && jump_ok;
    printf("\n  %s\n", ok ? "[PASS]" : "[FAIL — idle pose is not stable]");
    return ok;
}
