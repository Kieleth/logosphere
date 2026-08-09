// ============================================================================
// GRASS NATURES — the four-nature palette on REAL generated grass
// ============================================================================
// Owner's plan, verbatim: "creating different ones, and do the bar-hitting
// and measuring all of them, that's the first part of the test, then after
// esc we send a human over a single grass blade."
//
// PART 1  four grass blades (OrganicGenerator, seed-grown, KG bonds carrying
//         the palette: BENT / LEANING / STRAIGHT / BRITTLE), one wide bar
//         sweeps them all, every blade measured.
// PART 2  a human walks over a fifth (STRAIGHT) blade: the original #47
//         scenario on the new physics. She crosses, nothing detonates, the
//         blade bends by rotating and comes back.
//
// INTERACTIVE=1: SPACE starts the bar, SPACE again starts the human,
// final pose holds until ESC/close. AUTOPILOT=1 skips waits.
// ============================================================================

#include "../src/core/engine.h"
#include "../src/core/explosion_detector.h"
#include "../src/core/particle_system.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include "../src/ui/ui_system.h"
#include "../src/ui/widgets.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/worldgen/organic_generator.h"
#include "logosphere/worldgen/organic_spec.h"

#include <GLFW/glfw3.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace X = ::logosphere::expdet;

namespace {

bool g_inter = false;
bool g_auto = false;
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

struct Blade {
    const char* tag;
    float x;
    std::vector<size_t> ids;
    size_t tip = 0;
    float tx = 0, ty = 0, tz = 0;
    size_t bonds0 = 0;
    float peak = 0, fin = 0;
    int swings = 0;
    size_t bonds_end = 0;
};

OrganicSpec nature_spec(int k) {
    OrganicSpec sp = OrganicSpec::grass_blade();
    sp.height = 1.0f;            // thigh-high, one clear subject (proven config)
    sp.random_seed = 4242 + k;   // deterministic growth
    sp.gluon_quat_drive = true;
    switch (k) {
        case 0:  // BENT: damage sticks, plastic does not tear
            sp.gluon_plastic_yield = 0.25f;
            sp.gluon_max_strain = 5.0f;
            break;
        case 1:  // LEANING: one bounce, settles off vertical
            sp.gluon_damping = 20.0f;
            sp.gluon_angular_damping = 2.0f;
            sp.gluon_plastic_yield = 1.15f;
            sp.gluon_max_strain = 4.0f;
            break;
        case 2:  // STRAIGHT: near-critical, springs back
            break;
        case 3:  // BRITTLE: stiff joints, honest strength — it snaps
            sp.gluon_angular_stiffness = 2000.0f;
            sp.material_strength = 2e5f;
            break;
    }
    return sp;
}

size_t count_bonds(Engine& engine, const std::vector<size_t>& ids) {
    std::set<std::pair<size_t, size_t>> edges;
    for (size_t id : ids)
        for (const auto* g : engine.get_physics_system().get_gluons_for_particle(id))
            edges.insert(std::minmax(g->particle_a, g->particle_b));
    return edges.size();
}

} // namespace

bool test_grass_natures() {
    g_inter = std::getenv("INTERACTIVE") != nullptr;
    g_auto = std::getenv("AUTOPILOT") != nullptr;
    printf("\n=== GRASS NATURES: the palette on real grass, then a human ===\n");
    printf("  mode: %s\n", g_inter ? "INTERACTIVE" : "HEADLESS");

    EngineConfig cfg;
    cfg.create_display = g_inter;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) { printf("  engine init failed\n  FAIL\n"); return false; }

    {   // bare engines reject Grass; extend the ontology the sanctioned way
        kg::OntologyRegistry reg;
        reg.addEntityType("Grass", "Plant", false);
        reg.addAncestors("Grass", {"Plant", "LivingEntity", "WorldEntity", "Entity"});
        engine.get_kg().extendOntology(reg);
    }

    auto& ps = engine.get_particle_system();

    // Ground: tiles under the blade row (x 1..9) and the human lane (x 12).
    for (int cx = 0; cx <= 13; ++cx)
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
        ps.queue_light(4.0f, -6.0f, 16.0f, 700000.0f, 55.0f, 1.0f, 0.96f, 0.9f);
        auto& cam = engine.get_camera_system();
        cam.set_position(-1.0f, -8.0f, 3.5f);
        cam.look_at(5.0f, 0.0f, 0.6f);
        cam.set_pixels_per_unit(70.0f);
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

    // ---- grow the four blades + the human's blade ----------------------
    auto& ogen = engine.get_worldgen_system().get_organic_generator();
    auto& scene = engine.get_worldgen_system().get_scene_generator();
    static const char* TAGS[4] = {"BENT", "LEANING", "STRAIGHT", "BRITTLE"};
    Blade blades[5];
    for (int k = 0; k < 5; ++k) {
        const int nk = (k < 4) ? k : 2;              // human's blade: STRAIGHT
        const float bx = (k < 4) ? (2.0f + 2.0f * k) : 12.0f;
        Blade& b = blades[k];
        b.tag = (k < 4) ? TAGS[k] : "HUMAN'S";
        b.x = bx;
        const size_t p0 = [&] { auto v = ps.lock_particles_for_write(); return v.size(); }();
        kg::EntityID ent = ogen.generate_grass(bx, 0.0f, 0.1f, nature_spec(nk));
        scene.activate_entity_now(ent);
        ps.flush_pending_particles();
        auto v = ps.lock_particles_for_write();
        float best = -1.0f;
        for (size_t i = p0; i < v.size(); ++i) {
            b.ids.push_back(i);
            if (v[i].z > best) { best = v[i].z; b.tip = i;
                                 b.tx = v[i].x; b.ty = v[i].y; b.tz = v[i].z; }
        }
    }
    for (int k = 0; k < 5; ++k) blades[k].bonds0 = count_bonds(engine, blades[k].ids);
    printf("  blades grown: ");
    for (int k = 0; k < 5; ++k)
        printf("%s %zu bodies/%zu bonds  ", blades[k].tag, blades[k].ids.size(),
               blades[k].bonds0);
    printf("\n");
    for (int k = 0; k < 5; ++k)
        if (blades[k].ids.empty() || blades[k].bonds0 == 0) {
            printf("  *** blade %s grew empty or unbonded: nothing to test\n  FAIL\n",
                   blades[k].tag);
            engine.shutdown();
            return false;
        }

    X::set_enabled(true);
    X::reset();

    // ---- PART 1: the bar ------------------------------------------------
    Particle bar = {};
    bar.shape = ParticleShape::BOX;
    // Top-third contact: a HIT, not a mow. At blade-middle height the bar
    // is a lawnmower blade and mowing is the correct physics.
    bar.x = 5.0f; bar.y = -2.0f; bar.z = 0.85f;
    bar.width = 8.5f; bar.height = 0.25f; bar.thickness = 0.25f; bar.size = 0.25f;
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
        "GRASS NATURES pt 1: BENT | LEANING | STRAIGHT | BRITTLE.  SPACE: the bar sweeps them");
    for (int f = 0; f < 60 && engine.is_running(); ++f) gstep(engine);
    if (g_inter && std::getenv("GRASS_DUMP")) {
        engine.get_renderer().wait_for_completion();
        int w = engine.get_render_buffer().width();
        int h = engine.get_render_buffer().height();
        std::vector<uint32_t> px((size_t)w * h, 0u);
        if (engine.read_latest_framebuffer(px.data(), w, h)) {
            FILE* fp = fopen("/tmp/grass_frame.ppm", "wb");
            if (fp) {
                fprintf(fp, "P6\n%d %d\n255\n", w, h);
                for (uint32_t c : px) {
                    unsigned char rgb[3] = {(unsigned char)((c >> 16) & 0xFF),
                                            (unsigned char)((c >> 8) & 0xFF),
                                            (unsigned char)(c & 0xFF)};
                    fwrite(rgb, 1, 3, fp);
                }
                fclose(fp);
                printf("      [frame] dumped /tmp/grass_frame.ppm\n");
            }
        }
    }
    if (!gwait(engine)) { engine.shutdown(); return false; }

    auto sample = [&](int f, const char* ph) {
        auto v = ps.lock_particles_for_write();
        for (int k = 0; k < 4; ++k) {
            Blade& b = blades[k];
            const float td = d3(v[b.tip].x, v[b.tip].y, v[b.tip].z, b.tx, b.ty, b.tz);
            b.peak = std::fmax(b.peak, td);
            b.fin = td;
        }
        if (g_live && (f % 10) == 0) {
            char buf[288];
            snprintf(buf, sizeof buf,
                     "%s f%3d  tips m: BENT %.2f | LEANING %.2f | STRAIGHT %.2f | "
                     "BRITTLE %.2f   detector %llu",
                     ph, f,
                     d3(v[blades[0].tip].x, v[blades[0].tip].y, v[blades[0].tip].z,
                        blades[0].tx, blades[0].ty, blades[0].tz),
                     d3(v[blades[1].tip].x, v[blades[1].tip].y, v[blades[1].tip].z,
                        blades[1].tx, blades[1].ty, blades[1].tz),
                     d3(v[blades[2].tip].x, v[blades[2].tip].y, v[blades[2].tip].z,
                        blades[2].tx, blades[2].ty, blades[2].tz),
                     d3(v[blades[3].tip].x, v[blades[3].tip].y, v[blades[3].tip].z,
                        blades[3].tx, blades[3].ty, blades[3].tz),
                     (unsigned long long)X::stats().speed_events);
            g_live->set_text(buf);
        }
    };
    // BREAK TIMELINE (owner: 'the grass is breaking!'): per blade, the
    // frame its first bond tore and the bond-count curve, sampled coarse.
    int first_break[4] = {-1, -1, -1, -1};
    auto watch_breaks = [&](int f, const char* ph) {
        bool any = false;
        for (int k = 0; k < 4; ++k) {
            const size_t alive = count_bonds(engine, blades[k].ids);
            if (alive < blades[k].bonds0 && first_break[k] < 0) {
                first_break[k] = f;
                any = true;
            }
        }
        if (any || (f % 60) == 0) {
            printf("      [bonds %s f%3d] %s %zu/%zu  %s %zu/%zu  %s %zu/%zu  %s %zu/%zu  det %llu\n",
                   ph, f,
                   blades[0].tag, count_bonds(engine, blades[0].ids), blades[0].bonds0,
                   blades[1].tag, count_bonds(engine, blades[1].ids), blades[1].bonds0,
                   blades[2].tag, count_bonds(engine, blades[2].ids), blades[2].bonds0,
                   blades[3].tag, count_bonds(engine, blades[3].ids), blades[3].bonds0,
                   (unsigned long long)X::stats().speed_events);
        }
    };
    for (int f = 0; f < 240 && engine.is_running(); ++f) {
        {
            auto v = ps.lock_particles_for_write();
            v[bar_id].y = -2.0f + (float)f * (1.0f / 60.0f);
            v[bar_id].vy = 1.0f;
        }
        gstep(engine);
        sample(f, "SWEEP");
        watch_breaks(f, "SWEEP");
    }
    {
        auto v = ps.lock_particles_for_write();
        v[bar_id].x = 50.0f; v[bar_id].y = -40.0f; v[bar_id].vy = 0.0f;
    }
    if (g_title) g_title->set_text("GRASS NATURES pt 1: bar gone — four recoveries by nature");
    float prev_vy[4] = {0, 0, 0, 0};
    for (int f = 0; f < 500 && engine.is_running(); ++f) {
        gstep(engine);
        sample(f, "RECOV");
        if ((f % 100) == 0) watch_breaks(240 + f, "RECOV");
        auto v = ps.lock_particles_for_write();
        for (int k = 0; k < 4; ++k) {
            const float vy = v[blades[k].tip].vy;
            if (std::fabs(vy) > 0.05f && vy * prev_vy[k] < 0.0f) blades[k].swings++;
            if (std::fabs(vy) > 0.05f) prev_vy[k] = vy;
        }
    }
    for (int k = 0; k < 4; ++k) blades[k].bonds_end = count_bonds(engine, blades[k].ids);

    const uint64_t det_p1 = X::stats().speed_events;
    printf("\n  PART 1: the bar, measured\n");
    printf("  %-10s %9s %9s %7s %12s\n", "nature", "peak_tip", "final", "swings", "bonds");
    for (int k = 0; k < 4; ++k)
        printf("  %-10s %9.2f %9.2f %7d %6zu of %zu\n",
               blades[k].tag, blades[k].peak, blades[k].fin, blades[k].swings,
               blades[k].bonds_end, blades[k].bonds0);
    printf("  detector events part 1: %llu (worst %.1f m/s)\n",
           (unsigned long long)det_p1, X::stats().worst_speed);
    printf("  first bond tear: BENT f%d  LEANING f%d  STRAIGHT f%d  BRITTLE f%d\n",
           first_break[0], first_break[1], first_break[2], first_break[3]);

    // ---- PART 2: the human over one blade -------------------------------
    if (g_title) g_title->set_text(
        "GRASS NATURES pt 2: SPACE — a human walks over the fifth blade");
    if (g_inter) {
        auto& cam = engine.get_camera_system();
        cam.set_position(8.0f, -4.5f, 2.6f);
        cam.look_at(12.0f, 0.5f, 0.8f);
        cam.set_pixels_per_unit(110.0f);
    }
    if (!gwait(engine)) { engine.shutdown(); return false; }

    HumanoidGenerator hgen;
    hgen.initialize(&engine, &engine.get_kg());
    HumanoidSpec hspec = HumanoidSpec::hunter();
    hspec.facing_angle = 0.0f;
    auto eva = hgen.generate_humanoid_physics(12.1f, -2.2f, 0.1f, -1, hspec, false);
    auto& loco = engine.get_humanoid_locomotion();
    loco.register_humanoid_direct(eva.hips_id, eva.left_leg_ids, eva.right_leg_ids,
                                  eva.left_arm_ids, eva.right_arm_ids,
                                  eva.torso_ids, 150.0f, 600.0f);
    loco.reset_humanoid_position(eva.hips_id);
    loco.set_volitional(eva.hips_id, true);
    loco.set_target_velocity(eva.hips_id, 0.0f, 1.2f);

    Blade& hb = blades[4];
    float hb_peak = 0.0f, hb_fin = 0.0f, eva_y = -2.2f;
    for (int f = 0; f < 480 && engine.is_running(); ++f) {
        gstep(engine);
        auto v = ps.lock_particles_for_write();
        eva_y = v[eva.hips_id].y;
        const float td = d3(v[hb.tip].x, v[hb.tip].y, v[hb.tip].z, hb.tx, hb.ty, hb.tz);
        hb_peak = std::fmax(hb_peak, td);
        hb_fin = td;
        if (g_live && (f % 10) == 0) {
            char buf[224];
            snprintf(buf, sizeof buf,
                     "HUMAN f%3d  eva y %+.2f  blade tip defl %.2f (peak %.2f)  detector %llu",
                     f, eva_y, td, hb_peak,
                     (unsigned long long)(X::stats().speed_events - det_p1));
            g_live->set_text(buf);
        }
    }
    loco.set_target_velocity(eva.hips_id, 0.0f, 0.0f);
    for (int f = 0; f < 300 && engine.is_running(); ++f) {
        gstep(engine);
        auto v = ps.lock_particles_for_write();
        hb_fin = d3(v[hb.tip].x, v[hb.tip].y, v[hb.tip].z, hb.tx, hb.ty, hb.tz);
    }
    const size_t hb_bonds = count_bonds(engine, hb.ids);
    const uint64_t det_p2 = X::stats().speed_events - det_p1;

    printf("\n  PART 2: the human over one blade\n");
    printf("  %-42s %+.2f m (need > 1.5)\n", "she crossed: hips y", eva_y);
    printf("  %-42s %.2f m peak, %.2f final (recover < 0.30)\n",
           "blade tip deflection", hb_peak, hb_fin);
    printf("  %-42s %zu of %zu\n", "blade bonds after the pass", hb_bonds, hb.bonds0);
    printf("  %-42s %llu (worst %.1f m/s)\n", "detector events part 2",
           (unsigned long long)det_p2, X::stats().worst_speed);

    // ---- the gate --------------------------------------------------------
    const bool p1_bent     = blades[0].peak > 0.10f && blades[0].fin > 0.06f;
    const bool p1_leaning  = blades[1].peak > 0.10f && blades[1].fin > 0.015f &&
                             blades[1].fin < blades[0].fin;
    const bool p1_straight = blades[2].peak > 0.10f && blades[2].fin < 0.03f;
    const bool p1_brittle  = blades[3].bonds_end < blades[3].bonds0;
    const bool p1_whole    = blades[0].bonds_end == blades[0].bonds0 &&
                             blades[1].bonds_end == blades[1].bonds0 &&
                             blades[2].bonds_end == blades[2].bonds0;
    const bool p1_calm     = det_p1 == 0;
    const bool p2_crossed  = eva_y > 1.5f;
    const bool p2_blade    = hb_bonds == hb.bonds0 && hb_fin < 0.30f;
    const bool p2_calm     = det_p2 == 0;

    printf("\n  THE GATE\n");
    printf("  %-46s %s\n", "1 BENT stays displaced",      p1_bent ? "ok" : "*** OFF ***");
    printf("  %-46s %s\n", "2 LEANING keeps a trace",     p1_leaning ? "ok" : "*** OFF ***");
    printf("  %-46s %s\n", "3 STRAIGHT springs back",     p1_straight ? "ok" : "*** OFF ***");
    printf("  %-46s %s\n", "4 BRITTLE snaps, others hold",
           (p1_brittle && p1_whole) ? "ok" : "*** OFF ***");
    printf("  %-46s %s\n", "part 1 calm",                 p1_calm ? "ok" : "*** DETONATED ***");
    printf("  %-46s %s\n", "human crossed",               p2_crossed ? "ok" : "*** STUCK ***");
    printf("  %-46s %s\n", "her blade held and recovered", p2_blade ? "ok" : "*** OFF ***");
    printf("  %-46s %s\n", "part 2 calm",                 p2_calm ? "ok" : "*** DETONATED ***");

    const bool pass = p1_bent && p1_leaning && p1_straight && p1_brittle && p1_whole &&
                      p1_calm && p2_crossed && p2_blade && p2_calm;
    if (g_title) {
        char buf[192];
        snprintf(buf, sizeof buf, "GRASS NATURES %s — ESC or close when satisfied",
                 pass ? "GREEN" : "RED (see terminal)");
        g_title->set_text(buf);
    }
    ghold(engine);
    printf("\n  %s\n", pass ? "PASS" : "FAIL (red first: the numbers above are the work list)");
    engine.shutdown();
    return pass;
}
