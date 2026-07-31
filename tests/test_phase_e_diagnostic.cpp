// =============================================================================
// PHASE E DIAGNOSTIC — headless RCA for "shooting stuff" Eden bug
// =============================================================================
// Phase E (commit dd1ddfc) makes upper-body physics-drive default-on. All
// existing headless tests pass, but Eden visual shows particles flying
// outward. This harness reproduces Eden's humanoid spawn path in headless
// mode, tracks every body particle each frame, and trips on:
//   - per-frame position delta > 0.5 m
//   - velocity magnitude > 10 m/s
//   - distance from hips > 5 m
//
// On first trip, dumps the responsible particle, frame, recent history,
// and all simultaneous body-particle state. Failure case is the
// diagnostic; success would mean we never see what Eden showed.
//
// Run: ./build/logosphere-tests --test test_phase_e_diagnostic --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "logosphere/animation/humanoid_locomotion.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/worldgen/worldgen_system.h"
#include "logosphere/worldgen/strata_floor_generator.h"
#include "../src/math/quat.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

namespace {

struct ParticleSnapshot {
    int id = -1;
    std::string name;
    float x = 0, y = 0, z = 0;
    float vx = 0, vy = 0, vz = 0;
    float dist_from_hips = 0;
    float frame_delta = 0;
    float rot_qw = 1, rot_qx = 0, rot_qy = 0, rot_qz = 0;
    float rot_norm = 1;     // quat magnitude (should always be ~1)
    float rot_delta = 0;    // angular delta in radians vs prev frame
};

struct FrameSnapshot {
    int frame = -1;
    float hips_yaw = 0;     // hips rotation_z, for body-frame metrics
    std::vector<ParticleSnapshot> particles;
};

// Detection thresholds — any one trips dump.
// Walk swing at 2 m/s peaks ~0.12 m/frame on the swinging foot/forearm.
// "Shooting" in Eden manifests visually as particles flying away — set
// thresholds at 3x normal walk peaks so we catch anomalies, not gait.
constexpr float MAX_FRAME_DELTA = 0.30f;   // 300 mm — 3x normal swing peak
constexpr float MAX_VELOCITY    = 8.0f;
constexpr float MAX_DIST_HIPS   = 2.5f;
constexpr float MAX_ROT_DELTA   = 1.5f;    // radians per frame (~86°/frame)
constexpr float MAX_ROT_NORM_ERR= 0.05f;   // unit quat allowed ±5%

// Leg entanglement detection (Eden report 2026-06-12: "legs all
// entangled"). Two signals, both sustained to avoid tripping on normal
// gait moments:
//   - feet CROSSED in the hips frame (left foot right of right foot)
//   - leg segment (thigh↔shin / shin↔foot) stretched vs settled pose
constexpr float CROSS_MARGIN        = -0.02f; // sep below this = crossed
constexpr int   CROSS_SUSTAIN       = 15;     // frames (~0.25 s)
constexpr float SEG_STRETCH_FACTOR  = 1.75f;  // vs settled length
constexpr int   STRETCH_SUSTAIN     = 5;      // frames

void dump_snapshot(const FrameSnapshot& s) {
    printf("  --- frame %d ---\n", s.frame);
    for (const auto& p : s.particles) {
        printf("    [id=%-3d %-20s] pos=(%+7.3f,%+7.3f,%+7.3f) vel=(%+7.3f,%+7.3f,%+7.3f) "
               "Δframe=%6.4f dist_hips=%6.3f rot_q=(%.3f,%.3f,%.3f,%.3f)\n",
               p.id, p.name.c_str(),
               p.x, p.y, p.z, p.vx, p.vy, p.vz,
               p.frame_delta, p.dist_from_hips,
               p.rot_qw, p.rot_qx, p.rot_qy, p.rot_qz);
    }
}

}  // namespace

bool test_phase_e_diagnostic() {
    printf("\n=== Phase E diagnostic — headless Eden-shaped repro ===\n");

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "phase E diag";
    cfg.enable_chat_window = false;
    if (engine.initialize(cfg) != 0) {
        printf("  ERROR: engine init failed\n");
        return false;
    }

    auto& ps  = engine.get_particle_system();
    auto& humanoid = engine.get_humanoid_locomotion();

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
    strata.preload_chunks_around(0.0f, 0.0f, 3);

    auto& hgen = engine.get_worldgen_system().get_humanoid_generator();
    auto eva = hgen.generate_humanoid_physics(
        0.0f, 0.0f, 0.5f, -1, HumanoidSpec::eva(), false);
    auto& kg = engine.get_kg();
    eva.create_kg_entities(kg, "Human", 180.0f, 800.0f);
    const kg::EntityID eva_entity = eva.entity_id;
    humanoid.register_humanoid_direct(
        eva.hips_id,
        eva.left_leg_ids, eva.right_leg_ids,
        eva.left_arm_ids, eva.right_arm_ids,
        eva.torso_ids, 180.0f, 800.0f,
        eva_entity);

    // Build the body particle list with names. body_ids holds all of them.
    struct NamedId { int id; const char* name; };
    std::vector<NamedId> tracked;
    tracked.push_back({eva.hips_id, "hips"});
    auto label_torso = [&](size_t idx, const char* default_name) -> const char* {
        if (idx < eva.torso_ids.size()) return default_name;
        return "torso?";
    };
    if (eva.torso_ids.size() >= 5) {
        // generate_humanoid_physics convention: 0=hips,1=abdomen,2=chest,3=neck,4=head
        tracked.push_back({eva.torso_ids[1], "abdomen"});
        tracked.push_back({eva.torso_ids[2], "chest"});
        tracked.push_back({eva.torso_ids[3], "neck"});
        tracked.push_back({eva.torso_ids[4], "head"});
    }
    auto add_chain = [&](const std::vector<int>& ids,
                         const char* p0, const char* p1, const char* p2, const char* p3) {
        const char* names[] = { p0, p1, p2, p3 };
        for (size_t i = 0; i < ids.size() && i < 4; ++i) {
            tracked.push_back({ids[i], names[i]});
        }
    };
    add_chain(eva.left_leg_ids,  "L_foot", "L_shin", "L_thigh", "L_toe");
    add_chain(eva.right_leg_ids, "R_foot", "R_shin", "R_thigh", "R_toe");
    add_chain(eva.left_arm_ids,  "L_upper_arm", "L_forearm", "L_hand", "?");
    add_chain(eva.right_arm_ids, "R_upper_arm", "R_forearm", "R_hand", "?");

    int hips_id = eva.hips_id;

    // Causal write tracing on the arm chains (who moved this particle?).
    auto& tracer = engine.get_particle_tracer();
    for (const auto& n : tracked) {
        if (n.name[0] == 'L' || n.name[0] == 'R') {
            tracer.trace(n.id, n.name);
        }
    }

    ps.add_swap_callback([&](size_t old_idx, size_t new_idx) {
        auto fix = [&](int& id) { if (id == (int)old_idx) id = (int)new_idx; };
        fix(hips_id);
        for (auto& n : tracked) fix(n.id);
        // Keep tracer labels following the particle across index swaps.
        if (tracer.is_traced((int)old_idx)) {
            auto label = tracer.label_of((int)old_idx);
            tracer.untrace((int)old_idx);
            tracer.trace((int)new_idx, std::move(label));
        }
    });

    const float dt_frame = 1.0f / 60.0f;

    // Settle for 30 frames before measuring (legs default-on init still
    // applies; same warm-up walk_legs uses).
    for (int i = 0; i < 30; ++i) engine.update(dt_frame);

    // Reference leg-segment lengths from the settled pose, for stretch
    // detection. Look ids up via `tracked` (swap-corrected).
    auto id_of = [&](const char* name) -> int {
        for (const auto& n : tracked) if (n.name == std::string(name)) return n.id;
        return -1;
    };
    float ref_seg[4] = {0, 0, 0, 0};  // L thigh-shin, L shin-foot, R thigh-shin, R shin-foot
    {
        auto view = ps.lock_particles_for_read();
        auto seg = [&](int a, int b) -> float {
            if (a < 0 || b < 0) return 0.0f;
            float dx = view[a].x - view[b].x, dy = view[a].y - view[b].y,
                  dz = view[a].z - view[b].z;
            return std::sqrt(dx*dx + dy*dy + dz*dz);
        };
        ref_seg[0] = seg(id_of("L_thigh"), id_of("L_shin"));
        ref_seg[1] = seg(id_of("L_shin"),  id_of("L_foot"));
        ref_seg[2] = seg(id_of("R_thigh"), id_of("R_shin"));
        ref_seg[3] = seg(id_of("R_shin"),  id_of("R_foot"));
        printf("  settled leg segments: L(%.3f, %.3f) R(%.3f, %.3f)\n",
               ref_seg[0], ref_seg[1], ref_seg[2], ref_seg[3]);
    }

    auto take_snapshot = [&](int frame, const std::vector<ParticleSnapshot>& prev) {
        FrameSnapshot s;
        s.frame = frame;
        auto view = ps.lock_particles_for_read();
        const Particle& hips = view[hips_id];
        s.hips_yaw = hips.rotation_z;
        for (const auto& n : tracked) {
            if (n.id < 0 || (size_t)n.id >= view.size()) continue;
            const Particle& p = view[n.id];
            ParticleSnapshot ps_;
            ps_.id = n.id;
            ps_.name = n.name;
            ps_.x = p.x; ps_.y = p.y; ps_.z = p.z;
            ps_.vx = p.vx; ps_.vy = p.vy; ps_.vz = p.vz;
            float dx = p.x - hips.x, dy = p.y - hips.y, dz = p.z - hips.z;
            ps_.dist_from_hips = std::sqrt(dx*dx + dy*dy + dz*dz);
            ps_.rot_qw = p.rotation_q.w;
            ps_.rot_qx = p.rotation_q.x;
            ps_.rot_qy = p.rotation_q.y;
            ps_.rot_qz = p.rotation_q.z;
            ps_.rot_norm = std::sqrt(ps_.rot_qw*ps_.rot_qw + ps_.rot_qx*ps_.rot_qx +
                                     ps_.rot_qy*ps_.rot_qy + ps_.rot_qz*ps_.rot_qz);
            // Frame delta (vs prev snapshot of same particle id)
            for (const auto& pp : prev) {
                if (pp.id == n.id) {
                    float ex = ps_.x - pp.x, ey = ps_.y - pp.y, ez = ps_.z - pp.z;
                    ps_.frame_delta = std::sqrt(ex*ex + ey*ey + ez*ez);
                    // Angular delta: 2*acos(|<q1,q2>|)
                    float dot = ps_.rot_qw*pp.rot_qw + ps_.rot_qx*pp.rot_qx +
                                ps_.rot_qy*pp.rot_qy + ps_.rot_qz*pp.rot_qz;
                    float adot = std::fabs(dot);
                    if (adot > 1.0f) adot = 1.0f;
                    ps_.rot_delta = 2.0f * std::acos(adot);
                    break;
                }
            }
            s.particles.push_back(ps_);
        }
        return s;
    };

    auto detect = [&](const FrameSnapshot& s) -> int {
        // Returns index of first offending particle, -1 if clean.
        for (size_t i = 0; i < s.particles.size(); ++i) {
            const auto& p = s.particles[i];
            if (p.frame_delta > MAX_FRAME_DELTA) return (int)i;
            float vmag = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
            if (vmag > MAX_VELOCITY) return (int)i;
            if (p.dist_from_hips > MAX_DIST_HIPS) return (int)i;
            if (p.rot_delta > MAX_ROT_DELTA) return (int)i;
            if (std::fabs(p.rot_norm - 1.0f) > MAX_ROT_NORM_ERR) return (int)i;
        }
        return -1;
    };

    auto run_case = [&](const char* label, int n_frames, float vy_speed,
                        bool exercise_facing, bool exercise_look_at,
                        std::vector<FrameSnapshot>& history) -> bool {
        printf("\n  >>> CASE: %s (%d frames @ vy=%.2f, facing=%d, look_at=%d) <<<\n",
               label, n_frames, vy_speed, exercise_facing ? 1 : 0,
               exercise_look_at ? 1 : 0);
        if (vy_speed > 0.0f) {
            humanoid.set_volitional(eva.hips_id, true);
            humanoid.set_body_relative_velocity(eva.hips_id, vy_speed, 0.0f);
        } else {
            humanoid.set_volitional(eva.hips_id, false);
        }
        // Entanglement state (sustained-signal counters, reset per case).
        int cross_frames = 0;
        int stretch_frames = 0;
        auto find_p = [](const FrameSnapshot& s, const char* name) -> const ParticleSnapshot* {
            for (const auto& p : s.particles)
                if (p.name == name) return &p;
            return nullptr;
        };

        for (int frame = 0; frame < n_frames; ++frame) {
            // Exercise yaw cascade by ramping facing direction. Mirrors
            // Eden's mouse-driven turning.
            if (exercise_facing) {
                float t = static_cast<float>(frame) / 60.0f;  // seconds
                float facing = std::sin(t * 0.7f) * 1.5f;   // ±1.5 rad sweep
                humanoid.set_facing_direction(eva.hips_id, facing);
            }
            // Exercise look-at by moving the target around.
            if (exercise_look_at) {
                float t = static_cast<float>(frame) / 60.0f;
                float lx = std::cos(t) * 3.0f;
                float ly = std::sin(t * 0.7f) * 3.0f + 5.0f;
                humanoid.set_look_at_target(eva.hips_id, lx, ly);
            }

            engine.update(dt_frame);
            const std::vector<ParticleSnapshot>* prev =
                history.empty() ? nullptr : &history.back().particles;
            FrameSnapshot s = take_snapshot(frame, prev ? *prev : std::vector<ParticleSnapshot>{});
            history.push_back(s);

            // Periodic snapshot dump — see drift trends even when no trip.
            if (frame % 60 == 0 || frame == n_frames - 1) {
                {
                    const auto* hips_p = find_p(s, "hips");
                    const auto* lf = find_p(s, "L_foot");
                    const auto* rf = find_p(s, "R_foot");
                    if (hips_p && lf && rf) {
                        float cy = std::cos(s.hips_yaw), sy = std::sin(s.hips_yaw);
                        float lat_l = (lf->x - hips_p->x) * cy - (lf->y - hips_p->y) * sy;
                        float lat_r = (rf->x - hips_p->x) * cy - (rf->y - hips_p->y) * sy;
                        printf("    [feet  f%-3d] sep=%+.3f (latL=%+.3f latR=%+.3f yaw=%+.2f)\n",
                               frame, lat_r - lat_l, lat_l, lat_r, s.hips_yaw);
                    }
                }
                printf("    [trend f%-3d] ", frame);
                for (const auto& p : s.particles) {
                    if (p.name == "hips" || p.name == "head" || p.name == "L_hand" ||
                        p.name == "R_hand" || p.name == "L_foot" || p.name == "R_foot") {
                        float vmag = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
                        printf("%s=(%.2f,%.2f,%.2f vm=%.2f) ", p.name.c_str(),
                               p.x, p.y, p.z, vmag);
                    }
                }
                printf("\n");
            }

            // -------- Leg entanglement detection --------
            {
                const auto* hips_p = find_p(s, "hips");
                const auto* lf = find_p(s, "L_foot");
                const auto* rf = find_p(s, "R_foot");
                float sep = 1.0f;
                if (hips_p && lf && rf) {
                    // world→body: lateral = dx*cos(yaw) - dy*sin(yaw)
                    // (CW yaw, 0 = facing +Y, body-right = +X at rest)
                    float cy = std::cos(s.hips_yaw), sy = std::sin(s.hips_yaw);
                    float lat_l = (lf->x - hips_p->x) * cy - (lf->y - hips_p->y) * sy;
                    float lat_r = (rf->x - hips_p->x) * cy - (rf->y - hips_p->y) * sy;
                    sep = lat_r - lat_l;   // positive = uncrossed
                }
                cross_frames = (sep < CROSS_MARGIN) ? cross_frames + 1 : 0;

                float worst_stretch = 0.0f;
                const char* seg_names[4][2] = {
                    {"L_thigh", "L_shin"}, {"L_shin", "L_foot"},
                    {"R_thigh", "R_shin"}, {"R_shin", "R_foot"},
                };
                const char* worst_seg = "?";
                for (int si = 0; si < 4; ++si) {
                    if (ref_seg[si] <= 0.001f) continue;
                    const auto* pa = find_p(s, seg_names[si][0]);
                    const auto* pb = find_p(s, seg_names[si][1]);
                    if (!pa || !pb) continue;
                    float dx = pa->x - pb->x, dy = pa->y - pb->y, dz = pa->z - pb->z;
                    float ratio = std::sqrt(dx*dx + dy*dy + dz*dz) / ref_seg[si];
                    if (ratio > worst_stretch) { worst_stretch = ratio; worst_seg = seg_names[si][0]; }
                }
                stretch_frames = (worst_stretch > SEG_STRETCH_FACTOR) ? stretch_frames + 1 : 0;

                if (cross_frames >= CROSS_SUSTAIN || stretch_frames >= STRETCH_SUSTAIN) {
                    printf("\n  *** ENTANGLEMENT TRIP at frame %d in case '%s' ***\n", frame, label);
                    printf("      feet lateral sep=%.3f m (crossed %d consecutive frames)\n",
                           sep, cross_frames);
                    printf("      worst segment stretch=%.2fx at %s (%d consecutive frames)\n",
                           worst_stretch, worst_seg, stretch_frames);
                    printf("      hips_yaw=%.3f\n", s.hips_yaw);
                    printf("\n      Full snapshot at trip frame:\n");
                    dump_snapshot(s);
                    return false;
                }
            }

            int bad = detect(s);
            if (bad >= 0) {
                const auto& bp = s.particles[bad];
                float vmag = std::sqrt(bp.vx*bp.vx + bp.vy*bp.vy + bp.vz*bp.vz);
                printf("\n  *** TRIP at frame %d in case '%s' ***\n", frame, label);
                printf("      offending: id=%d %s\n", bp.id, bp.name.c_str());
                printf("      pos=(%+7.3f,%+7.3f,%+7.3f) vel_mag=%.3f\n",
                       bp.x, bp.y, bp.z, vmag);
                printf("      Δframe=%.4f  dist_hips=%.3f\n",
                       bp.frame_delta, bp.dist_from_hips);

                // Last 5 frames' history of THIS particle:
                printf("\n      Last 5 frames of this particle:\n");
                size_t start_h = history.size() >= 5 ? history.size() - 5 : 0;
                for (size_t hi = start_h; hi < history.size(); ++hi) {
                    for (const auto& hp : history[hi].particles) {
                        if (hp.id != bp.id) continue;
                        float hvm = std::sqrt(hp.vx*hp.vx + hp.vy*hp.vy + hp.vz*hp.vz);
                        printf("        f%-3d pos=(%+7.3f,%+7.3f,%+7.3f) vel_mag=%6.3f "
                               "Δ=%6.4f dist=%6.3f hips_yaw=%+.3f\n",
                               history[hi].frame, hp.x, hp.y, hp.z, hvm,
                               hp.frame_delta, hp.dist_from_hips,
                               history[hi].hips_yaw);
                    }
                }
                printf("\n      Full snapshot at trip frame:\n");
                dump_snapshot(s);
                printf("\n      Causal write trace (last 3 frames):\n");
                engine.get_particle_tracer().dump(std::cout, 3);
                return false;
            }
        }
        printf("  case '%s' clean for %d frames\n", label, n_frames);
        return true;
    };

    bool all_clean = true;
    {
        std::vector<FrameSnapshot> history;
        if (!run_case("idle", 120, 0.0f, false, false, history)) all_clean = false;
    }
    if (all_clean) {
        std::vector<FrameSnapshot> history;
        if (!run_case("idle+all-yaw", 360, 0.0f, true, true, history)) all_clean = false;
    }
    if (all_clean) {
        std::vector<FrameSnapshot> history;
        if (!run_case("walk-1mps", 600, 1.0f, false, false, history)) all_clean = false;
    }
    if (all_clean) {
        std::vector<FrameSnapshot> history;
        if (!run_case("walk-2mps", 600, 2.0f, false, false, history)) all_clean = false;
    }
    if (all_clean) {
        std::vector<FrameSnapshot> history;
        if (!run_case("walk-2mps+all-yaw", 900, 2.0f, true, true, history)) all_clean = false;
    }

    if (!all_clean) {
        printf("\n[FAIL] Phase E diagnostic tripped. See trace above.\n");
        return false;
    }
    printf("\n[PASS] All cases clean — no shooting detected (idle, 1 m/s, 2 m/s)\n");
    return true;
}
