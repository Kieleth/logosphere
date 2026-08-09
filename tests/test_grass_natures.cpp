// ============================================================================
// GRASS NATURES — three SIMPLE vertical blades, then a human
// ============================================================================
// Owner: "make grass simpler, 3 single blades/particles vertical, add
// instrumentation." No growth randomness: each blade is three thin vertical
// particles on a kinematic peg, stem-anchored palette bonds (the exact
// physics of generated grass, none of the space-colonization noise).
//
// PART 1  BENT | STRAIGHT | BRITTLE, one bar sweeps the top third, every
//         blade instrumented per frame: tip deflection, worst bond stretch
//         ratio, segment rotation, bonds alive, worst particle speed.
// PART 2  a human walks over a fourth (STRAIGHT) blade.
//
// INTERACTIVE=1: SPACE gates each part, hold at end. AUTOPILOT=1 skips waits.
// ============================================================================

#include "../src/core/engine.h"
#include "../src/core/explosion_detector.h"
#include "../src/core/particle_system.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include "../src/ui/ui_system.h"
#include "../src/ui/widgets.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/worldgen/humanoid_generator.h"

#include <GLFW/glfw3.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <set>
#include <utility>
#include <vector>

namespace X = ::logosphere::expdet;

namespace {

bool g_inter = false, g_auto = false;
ui::Label *g_title = nullptr, *g_live = nullptr;

void gstep(Engine& e) {
    e.update(1.0 / 60.0);
    if (g_inter) { e.render(); e.present(); }
}
bool gwait(Engine& e) {
    if (!g_inter || g_auto) return true;
    bool was = true;
    while (e.is_running()) {
        const auto& in = e.get_input_system().get_input_state();
        const bool sp = in.keys[GLFW_KEY_SPACE];
        if (sp && !was) return true;
        was = sp;
        if (in.keys[GLFW_KEY_ESCAPE]) return false;
        e.update(0.0); e.render(); e.present();
    }
    return false;
}
void ghold(Engine& e) {
    if (!g_inter) return;
    if (g_auto) { for (int f = 0; f < 30 && e.is_running(); ++f) gstep(e); return; }
    while (e.is_running()) {
        if (e.get_input_system().get_input_state().keys[GLFW_KEY_ESCAPE]) return;
        gstep(e);
    }
}

float d3(float ax, float ay, float az, float bx, float by, float bz) {
    const float dx = ax - bx, dy = ay - by, dz = az - bz;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
void axis_of(const Particle& p, float& wx, float& wy, float& wz) {
    const float cx = std::cos(p.rotation_x), sx = std::sin(p.rotation_x);
    const float cy = std::cos(p.rotation_y), sy = std::sin(p.rotation_y);
    const float cz = std::cos(p.rotation_z), sz = std::sin(p.rotation_z);
    const float y1 = -sx, z1 = cx;
    const float x2 = z1 * sy, z2 = z1 * cy;
    wx = x2 * cz - y1 * sz; wy = x2 * sz + y1 * cz; wz = z2;
}
float ang3(float ax, float ay, float az, float bx, float by, float bz) {
    const float da = std::sqrt(ax*ax + ay*ay + az*az);
    const float db = std::sqrt(bx*bx + by*by + bz*bz);
    if (da < 1e-9f || db < 1e-9f) return 0.0f;
    float c = (ax*bx + ay*by + az*bz) / (da*db);
    c = std::fmax(-1.0f, std::fmin(1.0f, c));
    return std::acos(c);
}

// One blade: three thin vertical particles on a kinematic peg.
constexpr float SEG_W = 0.03f, SEG_T = 0.3f;   // 3 cm x 3 cm x 30 cm
constexpr int SEGS = 3;

struct Nature {
    const char* tag;
    float stiffness, damping, ang_k, ang_c;
    float yield_angle;   // 0 = elastic
    float max_strain;    // 0 = default 2.0
};
const Nature NATURES[3] = {
    {"BENT",     5000.0f, 100.0f,  10.0f, 2.0f, 0.25f, 5.0f},
    {"STRAIGHT", 5000.0f, 100.0f,  60.0f, 8.0f, 0.0f,  6.0f},
    {"BRITTLE",  5000.0f, 100.0f, 400.0f, 2.0f, 0.0f,  1.3f},
};

struct Blade {
    const char* tag = "";
    float x = 0;
    int peg = -1;
    int seg[SEGS] = {-1, -1, -1};
    size_t bonds0 = 0;
    float tip0[3] = {0, 0, 0};
    // instrumentation
    float peak_tip = 0, fin_tip = 0, peak_rot = 0, peak_stretch = 0;
    float worst_speed = 0;
    int first_tear = -1;
};

Blade make_blade(Engine& engine, float x, const Nature& n) {
    auto& ps = engine.get_particle_system();
    Blade b;
    b.tag = n.tag;
    b.x = x;
    Particle peg = {};
    peg.shape = ParticleShape::BOX;
    peg.x = x; peg.y = 0.0f; peg.z = 0.14f;
    peg.width = peg.height = 0.08f; peg.thickness = 0.08f; peg.size = 0.08f;
    peg.r = 0.45f; peg.g = 0.3f; peg.b = 0.2f; peg.a = 1.0f;
    peg.SetMaterial(Materials::Type::STONE);
    b.peg = engine.add_particle(peg);
    for (int j = 0; j < SEGS; ++j) {
        Particle p = {};
        p.shape = ParticleShape::BOX;
        p.x = x; p.y = 0.0f; p.z = 0.18f + SEG_T * 0.5f + SEG_T * (float)j;
        p.width = p.height = SEG_W; p.thickness = SEG_T; p.size = SEG_W;
        p.r = 0.35f; p.g = 0.8f; p.b = 0.3f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::WOOD_SOFT);
        b.seg[j] = engine.add_particle(p);
    }
    ps.flush_pending_particles();
    {
        auto v = ps.lock_particles_for_write();
        v[b.peg].solver_mode = ParticleSolverMode::KINEMATIC;
        v[b.peg].owner = ParticleOwner::DYNAMICS;
        v[b.peg].material_strength = 1e9f;
        for (int j = 0; j < SEGS; ++j) {
            v[b.seg[j]].material_strength = 5e6f;   // soft plant tissue
            v[b.seg[j]].is_quat_driven = true;
            v[b.seg[j]].rotation_q = logosphere::Quat::identity();
        }
        const Particle& tip = v[b.seg[SEGS - 1]];
        b.tip0[0] = tip.x; b.tip0[1] = tip.y; b.tip0[2] = tip.z;
    }
    auto bond = [&](int a, int bId, float za, float zb) {
        auto g = std::make_unique<OrganicGluon>();
        g->offset_a = {0.0f, 0.0f, za};
        g->offset_b = {0.0f, 0.0f, zb};
        g->target_distance = 0.0f;
        g->rotate_offsets = true;
        g->contact_area = std::max(1e-8f, SEG_W * SEG_W);
        g->stiffness = n.stiffness;
        g->damping = n.damping;
        g->enable_angular_constraint = true;
        g->angular_drive_enabled = true;
        g->use_quat_target = true;       // grown pose: identity (upright)
        g->angular_stiffness = n.ang_k;
        g->angular_damping = n.ang_c;
        if (n.yield_angle > 0.0f)
            g->plastic_yield_angle = n.yield_angle;
        if (n.max_strain > 0.0f) g->max_strain = n.max_strain;
        engine.get_physics_system().add_gluon_between(a, bId, std::move(g));
    };
    bond(b.peg, b.seg[0], +0.04f, -SEG_T * 0.5f);
    bond(b.seg[0], b.seg[1], +SEG_T * 0.5f, -SEG_T * 0.5f);
    bond(b.seg[1], b.seg[2], +SEG_T * 0.5f, -SEG_T * 0.5f);
    b.bonds0 = 3;
    return b;
}

size_t bonds_alive(Engine& engine, const Blade& b) {
    std::set<std::pair<size_t, size_t>> edges;
    auto add = [&](int id) {
        for (const auto* g : engine.get_physics_system().get_gluons_for_particle(id))
            edges.insert(std::minmax(g->particle_a, g->particle_b));
    };
    add(b.peg);
    for (int j = 0; j < SEGS; ++j) add(b.seg[j]);
    return edges.size();
}

} // namespace

bool test_grass_natures() {
    g_inter = std::getenv("INTERACTIVE") != nullptr;
    g_auto = std::getenv("AUTOPILOT") != nullptr;
    printf("\n=== GRASS NATURES: three simple blades, then a human ===\n");
    printf("  mode: %s\n", g_inter ? "INTERACTIVE" : "HEADLESS");

    EngineConfig cfg;
    cfg.create_display = g_inter;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) { printf("  engine init failed\n  FAIL\n"); return false; }
    auto& ps = engine.get_particle_system();

    for (int cx = 0; cx <= 8; ++cx)
        for (int cy = -3; cy <= 3; ++cy) {
            Particle t = {};
            t.shape = ParticleShape::BOX;
            t.x = (float)cx; t.y = (float)cy; t.z = 0.05f;
            t.width = t.height = 1.0f; t.thickness = 0.1f; t.size = 1.0f;
            t.r = 0.35f; t.g = 0.4f; t.b = 0.35f; t.a = 1.0f;
            t.SetMaterial(Materials::Type::STONE);
            const int id = engine.add_particle(t);
            ps.flush_pending_particles();
            auto v = ps.lock_particles_for_write();
            v[id].solver_mode = ParticleSolverMode::KINEMATIC;
            v[id].owner = ParticleOwner::DYNAMICS;
            v[id].is_at_rest = true;
        }

    if (g_inter) {
        ps.queue_light(2.5f, -5.0f, 14.0f, 600000.0f, 50.0f, 1.0f, 0.96f, 0.9f);
        auto& cam = engine.get_camera_system();
        cam.set_position(0.0f, -3.5f, 1.6f);
        cam.look_at(2.5f, 0.0f, 0.6f);
        cam.set_pixels_per_unit(170.0f);
        if (auto* uis = engine.get_ui_system()) {
            auto mk = [&](int y, uint8_t r, uint8_t g, uint8_t b) {
                auto* L = new ui::Label("", "");
                L->set_position(24, y); L->set_size(1600, 22); L->set_color(r, g, b);
                uis->add_widget(L);
                return L;
            };
            g_title = mk(24, 255, 255, 255);
            g_live = mk(54, 120, 220, 255);
        }
    }

    Blade blades[4];
    for (int k = 0; k < 3; ++k)
        blades[k] = make_blade(engine, 1.5f + 1.0f * (float)k, NATURES[k]);
    blades[3] = make_blade(engine, 6.5f, NATURES[1]);   // the human's, STRAIGHT
    blades[3].tag = "HUMAN'S";

    X::set_enabled(true);
    X::reset();

    // ---- PART 1: the bar over the top third -----------------------------
    Particle bar = {};
    bar.shape = ParticleShape::BOX;
    bar.x = 2.5f; bar.y = -1.5f; bar.z = 0.95f;
    bar.width = 3.5f; bar.height = 0.15f; bar.thickness = 0.15f; bar.size = 0.15f;
    bar.r = 0.8f; bar.g = 0.35f; bar.b = 0.3f; bar.a = 1.0f;
    bar.SetMaterial(Materials::Type::STONE);
    const int bar_id = engine.add_particle(bar);
    ps.flush_pending_particles();
    {
        auto v = ps.lock_particles_for_write();
        v[bar_id].solver_mode = ParticleSolverMode::KINEMATIC;
        v[bar_id].owner = ParticleOwner::DYNAMICS;
        v[bar_id].material_strength = 1e9f;
    }

    if (g_title) g_title->set_text(
        "GRASS pt 1: BENT | STRAIGHT | BRITTLE, 3 segments each.  SPACE: bar sweeps the top third");
    for (int f = 0; f < 60 && engine.is_running(); ++f) gstep(engine);
    if (!gwait(engine)) { engine.shutdown(); return false; }

    auto measure = [&](int f, const char* ph, bool table) {
        auto v = ps.lock_particles_for_write();
        for (int k = 0; k < 3; ++k) {
            Blade& b = blades[k];
            const Particle& tip = v[b.seg[SEGS - 1]];
            const float td = d3(tip.x, tip.y, tip.z, b.tip0[0], b.tip0[1], b.tip0[2]);
            b.peak_tip = std::fmax(b.peak_tip, td);
            b.fin_tip = td;
            for (int j = 0; j < SEGS; ++j) {
                const Particle& p = v[b.seg[j]];
                float ax, ay, az;
                axis_of(p, ax, ay, az);
                b.peak_rot = std::fmax(b.peak_rot, ang3(ax, ay, az, 0, 0, 1));
                b.worst_speed = std::fmax(b.worst_speed,
                    std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz));
            }
            for (int j = 0; j + 1 < SEGS; ++j) {
                const Particle& pa = v[b.seg[j]];
                const Particle& pb = v[b.seg[j + 1]];
                b.peak_stretch = std::fmax(b.peak_stretch,
                    d3(pa.x, pa.y, pa.z, pb.x, pb.y, pb.z) / SEG_T);
            }
            const size_t alive = bonds_alive(engine, b);
            if (alive < b.bonds0 && b.first_tear < 0) b.first_tear = f;
        }
        if (table && (f % 30) == 0) {
            printf("      [%s f%3d]", ph, f);
            for (int k = 0; k < 3; ++k)
                printf("  %s tip %.2f rot %3.0f str %.2f spd %4.1f b%zu",
                       blades[k].tag, blades[k].fin_tip,
                       blades[k].peak_rot * 57.3f, blades[k].peak_stretch,
                       blades[k].worst_speed, bonds_alive(engine, blades[k]));
            printf("  det %llu\n", (unsigned long long)X::stats().speed_events);
        }
        if (g_live && (f % 10) == 0) {
            char buf[288];
            snprintf(buf, sizeof buf,
                     "%s f%3d  tips: BENT %.2f | STRAIGHT %.2f | BRITTLE %.2f   "
                     "bonds %zu|%zu|%zu   det %llu",
                     ph, f, blades[0].fin_tip, blades[1].fin_tip, blades[2].fin_tip,
                     bonds_alive(engine, blades[0]), bonds_alive(engine, blades[1]),
                     bonds_alive(engine, blades[2]),
                     (unsigned long long)X::stats().speed_events);
            g_live->set_text(buf);
        }
    };
    for (int f = 0; f < 180 && engine.is_running(); ++f) {
        {
            auto v = ps.lock_particles_for_write();
            v[bar_id].y = -1.5f + (float)f * (1.0f / 60.0f);
            v[bar_id].vy = 1.0f;
        }
        gstep(engine);
        measure(f, "SWEEP", true);
    }
    {
        auto v = ps.lock_particles_for_write();
        v[bar_id].x = 40.0f; v[bar_id].y = -40.0f; v[bar_id].vy = 0.0f;
    }
    if (g_title) g_title->set_text("GRASS pt 1: bar gone — recoveries by nature");
    for (int f = 0; f < 400 && engine.is_running(); ++f) {
        gstep(engine);
        measure(180 + f, "RECOV", true);
    }
    const uint64_t det_p1 = X::stats().speed_events;

    printf("\n  PART 1, measured\n");
    printf("  %-9s %8s %8s %8s %9s %8s %6s %10s\n",
           "nature", "peak_tip", "final", "rot_deg", "stretch_x", "spd_max",
           "bonds", "first_tear");
    for (int k = 0; k < 3; ++k)
        printf("  %-9s %8.2f %8.2f %8.0f %9.2f %8.1f %3zu of %zu %7d\n",
               blades[k].tag, blades[k].peak_tip, blades[k].fin_tip,
               blades[k].peak_rot * 57.3f, blades[k].peak_stretch,
               blades[k].worst_speed, bonds_alive(engine, blades[k]),
               blades[k].bonds0, blades[k].first_tear);
    printf("  detector events part 1: %llu (worst %.1f m/s)\n",
           (unsigned long long)det_p1, X::stats().worst_speed);

    // ---- PART 2: the human ----------------------------------------------
    if (g_title) g_title->set_text("GRASS pt 2: SPACE — the human walks over the fourth blade");
    if (g_inter) {
        auto& cam = engine.get_camera_system();
        cam.set_position(4.5f, -3.0f, 1.8f);
        cam.look_at(6.5f, 0.3f, 0.6f);
        cam.set_pixels_per_unit(150.0f);
    }
    if (!gwait(engine)) { engine.shutdown(); return false; }

    HumanoidGenerator hgen;
    hgen.initialize(&engine, &engine.get_kg());
    HumanoidSpec hspec = HumanoidSpec::hunter();
    hspec.facing_angle = 0.0f;
    auto eva = hgen.generate_humanoid_physics(6.6f, -2.0f, 0.1f, -1, hspec, false);
    auto& loco = engine.get_humanoid_locomotion();
    loco.register_humanoid_direct(eva.hips_id, eva.left_leg_ids, eva.right_leg_ids,
                                  eva.left_arm_ids, eva.right_arm_ids,
                                  eva.torso_ids, 150.0f, 600.0f);
    loco.reset_humanoid_position(eva.hips_id);
    loco.set_volitional(eva.hips_id, true);
    loco.set_target_velocity(eva.hips_id, 0.0f, 1.2f);

    Blade& hb = blades[3];
    float hb_peak = 0, hb_fin = 0, eva_y = -2.0f;
    for (int f = 0; f < 420 && engine.is_running(); ++f) {
        gstep(engine);
        auto v = ps.lock_particles_for_write();
        eva_y = v[eva.hips_id].y;
        const Particle& tip = v[hb.seg[SEGS - 1]];
        const float td = d3(tip.x, tip.y, tip.z, hb.tip0[0], hb.tip0[1], hb.tip0[2]);
        hb_peak = std::fmax(hb_peak, td);
        hb_fin = td;
        if (g_live && (f % 10) == 0) {
            char buf[224];
            snprintf(buf, sizeof buf,
                     "HUMAN f%3d  eva y %+.2f  blade tip %.2f (peak %.2f)  bonds %zu  det %llu",
                     f, eva_y, td, hb_peak, bonds_alive(engine, hb),
                     (unsigned long long)(X::stats().speed_events - det_p1));
            g_live->set_text(buf);
        }
    }
    loco.set_target_velocity(eva.hips_id, 0.0f, 0.0f);
    for (int f = 0; f < 240 && engine.is_running(); ++f) {
        gstep(engine);
        auto v = ps.lock_particles_for_write();
        const Particle& tip = v[hb.seg[SEGS - 1]];
        hb_fin = d3(tip.x, tip.y, tip.z, hb.tip0[0], hb.tip0[1], hb.tip0[2]);
    }
    const size_t hb_bonds = bonds_alive(engine, hb);
    const uint64_t det_p2 = X::stats().speed_events - det_p1;

    printf("\n  PART 2: the human over one blade\n");
    printf("  %-42s %+.2f m (need > 1.5)\n", "she crossed: hips y", eva_y);
    printf("  %-42s %.2f peak, %.2f final\n", "blade tip deflection", hb_peak, hb_fin);
    printf("  %-42s %zu of %zu\n", "blade bonds after the pass", hb_bonds, hb.bonds0);
    printf("  %-42s %llu (worst %.1f m/s)\n", "detector events part 2",
           (unsigned long long)det_p2, X::stats().worst_speed);

    // ---- the gate --------------------------------------------------------
    const bool bent_ok     = blades[0].peak_tip > 0.10f && blades[0].fin_tip > 0.06f &&
                             bonds_alive(engine, blades[0]) == blades[0].bonds0;
    const bool straight_ok = blades[1].peak_tip > 0.10f && blades[1].fin_tip < 0.05f &&
                             bonds_alive(engine, blades[1]) == blades[1].bonds0;
    const bool brittle_ok  = bonds_alive(engine, blades[2]) < blades[2].bonds0;
    const bool rotates_ok  = blades[0].peak_rot > 0.26f && blades[1].peak_rot > 0.26f;
    const bool p1_calm     = det_p1 == 0;
    const bool p2_crossed  = eva_y > 1.5f;
    const bool p2_blade    = hb_bonds == hb.bonds0 && hb_fin < 0.20f;
    const bool p2_calm     = det_p2 == 0;

    printf("\n  THE GATE\n");
    printf("  %-46s %s\n", "BENT bends, stays displaced, holds bonds",
           bent_ok ? "ok" : "*** OFF ***");
    printf("  %-46s %s\n", "STRAIGHT bends, springs back, holds bonds",
           straight_ok ? "ok" : "*** OFF ***");
    printf("  %-46s %s\n", "BRITTLE snaps", brittle_ok ? "ok" : "*** OFF ***");
    printf("  %-46s %s\n", "blades bend by ROTATING",
           rotates_ok ? "ok" : "*** TRANSLATE ONLY ***");
    printf("  %-46s %s\n", "part 1 calm", p1_calm ? "ok" : "*** DETONATED ***");
    printf("  %-46s %s\n", "human crossed", p2_crossed ? "ok" : "*** STUCK ***");
    printf("  %-46s %s\n", "her blade held and recovered",
           p2_blade ? "ok" : "*** OFF ***");
    printf("  %-46s %s\n", "part 2 calm", p2_calm ? "ok" : "*** DETONATED ***");

    const bool pass = bent_ok && straight_ok && brittle_ok && rotates_ok &&
                      p1_calm && p2_crossed && p2_blade && p2_calm;
    if (g_title) {
        char buf[160];
        snprintf(buf, sizeof buf, "GRASS NATURES %s — ESC or close when satisfied",
                 pass ? "GREEN" : "RED (see terminal)");
        g_title->set_text(buf);
    }
    ghold(engine);
    printf("\n  %s\n", pass ? "PASS" : "FAIL (the numbers above are the work list)");
    engine.shutdown();
    return pass;
}
