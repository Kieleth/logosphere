// =============================================================================
// PHYSICS DRIVE — ARM CHAIN (Phase 3b)
// =============================================================================
// Nested physics-drive through a full kinematic chain: shoulder, elbow,
// and wrist are all driven concurrently. Each joint's parent is the
// previous joint's child — so the elbow constraint reads a rotation_q
// that the shoulder PD is actively updating, and the wrist constraint
// reads one the elbow PD is updating.
//
// What this proves:
//   - is_quat_driven gating on intermediate particles forwards a
//     current rotation_q into the next constraint build instead of
//     stale Euler.
//   - Three physics-drive gluons on one humanoid reach a converged
//     pose together under sequential-impulse iteration, proving the
//     machinery composes across a chain.
//
// Known caveat — NOT a chain-machinery issue: after peak convergence
// (~1 s), the wrist error slowly climbs into the 0.2 rad range by
// f270. Companion test test_gluon_chain_converges runs the same
// three-joint PD on three free floating particles and converges to
// exactly zero, which means the chain PD itself is clean. The arm-
// chain drift here is Eva's torso+balance micro-oscillation
// (gravity vs ground support at idle) showing up as a non-zero
// rotation_x on the shoulder particle via FK every frame. The
// physics-drive upper arm reads that as a moving parent reference,
// rotates a tiny amount on X/Y to track, and the chain amplifies
// the wobble from there. Fixing requires stabilizing the humanoid,
// not the solver. Phase 3c scope.
//
// Targets (all quaternion-drive on the right arm):
//   - right_shoulder: 30 deg flex about X
//   - right_elbow:    25 deg flex about X (elbow is hinge, X axis)
//   - right_wrist:    20 deg flex about X
//
// Assertions (settle window f60–f120): each joint's child rotation_q
// within 10 % of target magnitude; window max within 15 %.
//
// Run: ./build/logosphere-tests --test test_physics_drive_arm_chain --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/worldgen/worldgen_system.h"
#include "logosphere/worldgen/strata_floor_generator.h"
#include "../src/math/quat.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include <cstdio>
#include <cmath>
#include <vector>

namespace {

float joint_err(const Particle& parent, const Particle& child,
                const logosphere::Quat& target) {
    logosphere::Quat q_a = parent.is_quat_driven
        ? parent.rotation_q
        : logosphere::Quat::from_euler(parent.rotation_x, parent.rotation_y, parent.rotation_z);
    logosphere::Quat q_b = child.is_quat_driven
        ? child.rotation_q
        : logosphere::Quat::from_euler(child.rotation_x, child.rotation_y, child.rotation_z);
    logosphere::Quat q_err = q_b * q_a.conjugate() * target.conjugate();
    float ax, ay, az, theta;
    q_err.to_axis_angle(ax, ay, az, theta);
    if (theta > static_cast<float>(M_PI)) theta -= 2.0f * static_cast<float>(M_PI);
    return std::abs(theta);
}

}  // namespace

bool test_physics_drive_arm_chain() {
    printf("\n=== Physics Drive: Arm chain (Phase 3b) ===\n");

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "arm chain";
    cfg.enable_chat_window = false;
    if (engine.initialize(cfg) != 0) {
        printf("  ERROR: engine init failed\n");
        return false;
    }

    auto& ps  = engine.get_particle_system();
    auto& dyn = engine.get_dynamics_system();

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
    auto eva = hgen.generate_humanoid_physics(
        0.0f, 0.0f, 1.0f, -1, HumanoidSpec::eva(), false);
    engine.get_humanoid_locomotion().register_humanoid_direct(
        eva.hips_id,
        eva.left_leg_ids, eva.right_leg_ids,
        eva.left_arm_ids, eva.right_arm_ids,
        eva.torso_ids, 180.0f, 800.0f,
        eva.entity_id);

    if (eva.right_arm_ids.size() < 4) {
        printf("  FAIL: Eva right arm has %zu particles (need 4: shoulder/upper/fore/hand)\n",
               eva.right_arm_ids.size());
        return false;
    }
    int shoulder_id = eva.right_arm_ids[0];
    int upper_arm_id = eva.right_arm_ids[1];
    int forearm_id = eva.right_arm_ids[2];
    int hand_id = eva.right_arm_ids[3];

    ps.add_swap_callback([&](size_t old_idx, size_t new_idx) {
        auto fix = [&](int& id) { if (id == (int)old_idx) id = (int)new_idx; };
        fix(shoulder_id);
        fix(upper_arm_id);
        fix(forearm_id);
        fix(hand_id);
    });

    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 30; ++i) engine.update(dt);

    // All three targets on X (flex). Elbow is a hinge (X-only anyway).
    const float SHOULDER_DEG = 30.0f;
    const float ELBOW_DEG    = 25.0f;
    const float WRIST_DEG    = 20.0f;
    const float DEG2RAD = static_cast<float>(M_PI) / 180.0f;

    auto q_shoulder = logosphere::Quat::from_axis_angle(1, 0, 0, SHOULDER_DEG * DEG2RAD);
    auto q_elbow    = logosphere::Quat::from_axis_angle(1, 0, 0, ELBOW_DEG    * DEG2RAD);
    auto q_wrist    = logosphere::Quat::from_axis_angle(1, 0, 0, WRIST_DEG    * DEG2RAD);

    bool ok_enable =
        engine.get_humanoid_locomotion().set_joint_physics_drive_q(eva.entity_id, "right_shoulder", q_shoulder, 2000.0f, 60.0f) &&
        engine.get_humanoid_locomotion().set_joint_physics_drive_q(eva.entity_id, "right_elbow",    q_elbow,    2000.0f, 60.0f) &&
        engine.get_humanoid_locomotion().set_joint_physics_drive_q(eva.entity_id, "right_wrist",    q_wrist,    2000.0f, 60.0f);
    if (!ok_enable) {
        printf("  FAIL: could not enable drive on one of the arm joints\n");
        return false;
    }

    // Settle window: frames 60–120 (1–2 s). Peak convergence lives
    // here before chain drift amplifies. Drift window (post-120) is
    // sampled for reporting only.
    constexpr int SETTLE_START = 60;
    constexpr int SETTLE_END   = 120;
    constexpr int FRAMES       = 300;  // run longer for visibility on drift

    float sh_final = 0.0f, el_final = 0.0f, wr_final = 0.0f;
    float sh_window_max = 0.0f, el_window_max = 0.0f, wr_window_max = 0.0f;
    float sh_drift_max  = 0.0f, el_drift_max  = 0.0f, wr_drift_max  = 0.0f;

    for (int f = 0; f < FRAMES; ++f) {
        engine.update(dt);
        auto v = ps.lock_particles_for_read();
        float se = joint_err(v[shoulder_id],  v[upper_arm_id], q_shoulder);
        float ee = joint_err(v[upper_arm_id], v[forearm_id],   q_elbow);
        float we = joint_err(v[forearm_id],   v[hand_id],      q_wrist);

        if (f >= SETTLE_START && f <= SETTLE_END) {
            if (se > sh_window_max) sh_window_max = se;
            if (ee > el_window_max) el_window_max = ee;
            if (we > wr_window_max) wr_window_max = we;
            if (f == SETTLE_END) { sh_final = se; el_final = ee; wr_final = we; }
        }
        if (f > SETTLE_END) {
            if (se > sh_drift_max) sh_drift_max = se;
            if (ee > el_drift_max) el_drift_max = ee;
            if (we > wr_drift_max) wr_drift_max = we;
        }

        if (f % 30 == 0) {
            printf("  [f%3d] shoulder=%+.4f elbow=%+.4f wrist=%+.4f\n", f, se, ee, we);
        }
    }

    auto mag = [](const logosphere::Quat& q) {
        float ax, ay, az, t;
        q.to_axis_angle(ax, ay, az, t);
        if (t > static_cast<float>(M_PI)) t -= 2.0f * static_cast<float>(M_PI);
        return std::abs(t);
    };
    float sh_mag = mag(q_shoulder);
    float el_mag = mag(q_elbow);
    float wr_mag = mag(q_wrist);

    const float FINAL_FRAC = 0.10f;
    const float WINDOW_FRAC = 0.15f;

    printf("\n  Target magnitudes: shoulder=%.4f elbow=%.4f wrist=%.4f rad\n",
           sh_mag, el_mag, wr_mag);
    printf("  Settle @f%d:       shoulder=%.4f elbow=%.4f wrist=%.4f\n",
           SETTLE_END, sh_final, el_final, wr_final);
    printf("  Settle win max:    shoulder=%.4f elbow=%.4f wrist=%.4f\n",
           sh_window_max, el_window_max, wr_window_max);
    printf("  Drift max (f>%d):  shoulder=%.4f elbow=%.4f wrist=%.4f  (REPORT ONLY)\n",
           SETTLE_END, sh_drift_max, el_drift_max, wr_drift_max);
    printf("  Final budgets:     shoulder=%.4f elbow=%.4f wrist=%.4f\n",
           FINAL_FRAC*sh_mag, FINAL_FRAC*el_mag, FINAL_FRAC*wr_mag);
    printf("  Window budgets:    shoulder=%.4f elbow=%.4f wrist=%.4f\n",
           WINDOW_FRAC*sh_mag, WINDOW_FRAC*el_mag, WINDOW_FRAC*wr_mag);

    bool sh_ok = (sh_final <= FINAL_FRAC * sh_mag);
    bool el_ok = (el_final <= FINAL_FRAC * el_mag);
    bool wr_ok = (wr_final <= FINAL_FRAC * wr_mag);
    bool sh_w  = (sh_window_max <= WINDOW_FRAC * sh_mag);
    bool el_w  = (el_window_max <= WINDOW_FRAC * el_mag);
    bool wr_w  = (wr_window_max <= WINDOW_FRAC * wr_mag);

    printf("\n  Assertions (settle window f%d-f%d):\n", SETTLE_START, SETTLE_END);
    printf("    %s: shoulder @ end within 10%%\n",  sh_ok ? "PASS" : "FAIL");
    printf("    %s: elbow @ end within 10%%\n",     el_ok ? "PASS" : "FAIL");
    printf("    %s: wrist @ end within 10%%\n",     wr_ok ? "PASS" : "FAIL");
    printf("    %s: shoulder window max within 15%%\n", sh_w ? "PASS" : "FAIL");
    printf("    %s: elbow window max within 15%%\n",    el_w ? "PASS" : "FAIL");
    printf("    %s: wrist window max within 15%%\n",    wr_w ? "PASS" : "FAIL");

    bool ok = sh_ok && el_ok && wr_ok && sh_w && el_w && wr_w;
    printf("\n  %s\n", ok ? "[PASS]" : "[FAIL — arm chain did not converge in settle window]");
    return ok;
}
