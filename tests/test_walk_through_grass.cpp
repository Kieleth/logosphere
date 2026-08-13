// =============================================================================
// A HUMAN WALKS THROUGH GRASS, AND EVERYTHING MUST STAY GOOD (issue #47)
// =============================================================================
//   INTERACTIVE=1 ./build-release/logosphere-tests --test test_walk_through_grass --no-head
//
// THE OWNER'S QUESTION, verbatim: "why don't we have a human walking through
// different grasses and asserting all is good?" This is that test. Everything
// before it checked pieces (bonds exist, a kicked patch holds); this checks
// the sentence the game actually needs to be true:
//
//   Eva walks a straight line through three patches of grass, two kinds,
//   brushing through dozens of blades, and at the end:
//
//     1  SHE GOT THERE.     Hips advanced most of the path. Grass did not
//                           stop her, launch her, or trip her into the void.
//     2  SHE IS STILL UP.   Hips at walking height, not buried, not orbiting.
//     3  NOTHING DETONATED. The explosion detector stayed silent. Before the
//                           bonding fix, brushing grass was how Eden's world
//                           blew up: unbonded blade-plates amplified any
//                           touch (measured 1.78e12 m/s, clamped to 100).
//     4  THE GRASS IS STILL GRASS. Blades may bend and shift when a person
//                           pushes through them, real grass does. They may
//                           not fly away: no blade ends up further than 2 m
//                           from where it grew.
//
// This is an INTEGRATION test by design: floor, locomotion, grass, bonds,
// contacts, detector, all at once. The pieces have their own tests; this one
// exists so that "all is good" is a measured claim, not a mood.
//
// Interactive mode shows the walk from the side with live vitals. Lessons
// carried: widgets only, camera aimed + lit-pixel proof, no scenery beyond
// what the scenario itself requires (the floor is Eva's ground, a subject).
// =============================================================================

#include "../src/core/engine.h"
#include "../src/core/explosion_detector.h"
#include "../src/core/particle_system.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include "../src/ui/ui_system.h"
#include "../src/ui/widgets.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/worldgen/grass_patch_spec.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/worldgen/organic_generator.h"
#include "logosphere/worldgen/scene_chunk_generator.h"
#include "logosphere/worldgen/worldgen_system.h"
#include <cmath>
#include <cstdio>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef __APPLE__
#include <GLFW/glfw3.h>
#endif

namespace X = ::logosphere::expdet;

bool test_walk_through_grass() {
    const bool interactive = std::getenv("INTERACTIVE") != nullptr;
    printf("\n=== A HUMAN WALKS THROUGH GRASS (issue #47) ===\n");
    printf("  mode: %s\n", interactive ? "INTERACTIVE (ESC quits)" : "HEADLESS");

    EngineConfig cfg;
    cfg.create_display = interactive;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) { printf("  ERROR: engine init failed\n"); return false; }

    {   // bare engines reject Grass; extend the ontology the sanctioned way
        kg::OntologyRegistry reg;
        reg.addEntityType("Grass",      "Plant", false);
        reg.addEntityType("GrassPatch", "Plant", false);
        reg.addAncestors("Grass",      {"Plant", "LivingEntity", "WorldEntity", "Entity"});
        reg.addAncestors("GrassPatch", {"Plant", "LivingEntity", "WorldEntity", "Entity"});
        engine.get_kg().extendOntology(reg);
    }

    auto& ps = engine.get_particle_system();

    // The path: a floor strip Eva walks along, +Y, ~14 m. The floor is not
    // scenery here, it is her ground; without it there is no walking at all.
    for (int x = -2; x <= 2; ++x)
        for (int y = -2; y <= 12; ++y) {
            Particle p = {};
            p.shape = ParticleShape::BOX;
            p.x = (float)x; p.y = (float)y; p.z = 0.05f;
            p.width = p.height = 1.0f; p.thickness = 0.1f; p.size = 1.0f;
            p.r = 0.35f; p.g = 0.32f; p.b = 0.28f; p.a = 1.0f;
            p.SetMaterial(Materials::Type::STONE);
            int id = engine.add_particle(p);
            auto v = ps.lock_particles_for_write();
            v[id].solver_mode = ParticleSolverMode::KINEMATIC;
            v[id].owner = ParticleOwner::DYNAMICS;
            v[id].is_at_rest = true;
        }
    ps.queue_light(0.0f, 4.0f, 16.0f, 500000.0f, 50.0f, 1.0f, 0.96f, 0.9f);
    ps.flush_pending_particles();

    // Three patches ON the path, two kinds: short at y=3, tall at y=6 and
    // y=9. "Different grasses", as asked.
    auto& ogen  = engine.get_worldgen_system().get_organic_generator();
    auto& scene = engine.get_worldgen_system().get_scene_generator();
    std::vector<size_t> grass_ids;
    std::vector<float> gx0, gy0, gz0;
    size_t grass_gluons = 0;
    {
        const size_t b0 = [&] { auto v = ps.lock_particles_for_write(); return v.size(); }();
        const size_t g0 = engine.get_physics_system().get_total_gluon_count();
        struct Spot { float y; GrassPatchSpec spec; };
        const Spot spots[3] = {
            {3.0f, GrassPatchSpec::short_grass()},
            {6.0f, GrassPatchSpec::tall_grass()},
            {9.0f, GrassPatchSpec::tall_grass()},
        };
        for (const Spot& sp : spots) {
            kg::EntityID patch = ogen.generate_grass_patch(0.0f, sp.y, 0.1f, sp.spec);
            scene.activate_entity_now(patch);
        }
        ps.flush_pending_particles();
        grass_gluons = engine.get_physics_system().get_total_gluon_count() - g0;
        auto v = ps.lock_particles_for_write();
        for (size_t i = b0; i < v.size(); ++i) {
            grass_ids.push_back(i);
            gx0.push_back(v[i].x); gy0.push_back(v[i].y); gz0.push_back(v[i].z);
        }
    }
    printf("  grass: %zu bodies across 3 patches (2 kinds), %zu gluons\n",
           grass_ids.size(), grass_gluons);
    if (grass_ids.empty() || grass_gluons == 0) {
        printf("\n  *** NO BONDED GRASS ON THE PATH: %zu bodies, %zu gluons. Nothing this\n"
               "  test claims can be tested. Fix the plumbing first.\n",
               grass_ids.size(), grass_gluons);
        printf("\n  FAIL\n");
        if (interactive) {
        printf("  [INTERACTIVE] holding final state 20 s for inspection...\n");
        for (int f = 0; f < 1200; ++f) {
            engine.update(1.0 / 60.0);
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }
    engine.shutdown();
        return false;
    }

    // Eva, at the start of the path, facing +Y (north: the engine's forward).
    HumanoidGenerator hgen;
    hgen.initialize(&engine, &engine.get_kg());
    HumanoidSpec spec = HumanoidSpec::hunter();
    spec.facing_angle = 0.0f;
    auto eva = hgen.generate_humanoid_physics(0.0f, -1.0f, 0.1f, -1, spec, false);
    auto& loco = engine.get_humanoid_locomotion();
    loco.register_humanoid_direct(eva.hips_id,
                                  eva.left_leg_ids, eva.right_leg_ids,
                                  eva.left_arm_ids, eva.right_arm_ids,
                                  eva.torso_ids, 150.0f, 600.0f);
    loco.reset_humanoid_position(eva.hips_id);
    loco.set_volitional(eva.hips_id, true);
    loco.set_target_velocity(eva.hips_id, 0.0f, 1.2f);   // a walk, not a sprint

    const float eva_y0 = [&] {
        auto v = ps.lock_particles_for_write(); return v[eva.hips_id].y;
    }();

    if (interactive) {
        auto& camera = engine.get_camera_system();
        camera.set_position(-9.0f, 5.0f, 4.0f);   // side-on, midway up the path
        camera.look_at(0.0f, 5.0f, 1.0f);
        camera.set_pixels_per_unit(90.0f);
    }
    ui::Label *l_title = nullptr, *l_live = nullptr;
    if (auto* uis = engine.get_ui_system()) {
        l_title = new ui::Label(
            "EVA WALKS THROUGH 3 GRASS PATCHES. Good = she crosses, grass bends but stays,"
            " nothing detonates.", "");
        l_title->set_position(24, 24); l_title->set_size(1500, 22);
        l_title->set_color(255, 255, 255);
        uis->add_widget(l_title);
        l_live = new ui::Label("", "");
        l_live->set_position(24, 54); l_live->set_size(1500, 22);
        l_live->set_color(120, 220, 255);
        uis->add_widget(l_live);
    }

    const size_t gluons_at_start =
        engine.get_physics_system().get_total_gluon_count();

    X::set_enabled(true);
    X::reset();

    // The walk: 12 seconds at 60 Hz, enough for ~10-14 m at 1.2 m/s.
    const int FRAMES = 720;
    double grass_worst_drift = 0.0;
    float eva_y = eva_y0, eva_z = 0.0f;
    for (int f = 0; f < FRAMES; ++f) {
        engine.update(1.0 / 60.0);
        // INTERACTIVE: pace to real time so the walk is watchable; headless
        // runs at full speed as before.
        if (interactive) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        {
            auto v = ps.lock_particles_for_write();
            eva_y = v[eva.hips_id].y;
            eva_z = v[eva.hips_id].z;
        }
        if (interactive) {
            if (l_live) {
                char buf[256];
                const X::Stats s = X::stats();
                snprintf(buf, sizeof(buf),
                         "Eva y=%5.2f m (start %.2f)  hips z=%4.2f  detector events %llu  worst %5.1f m/s",
                         eva_y, eva_y0, eva_z,
                         (unsigned long long)s.speed_events, s.worst_speed);
                l_live->set_text(buf);
            }
            engine.render();
            engine.present();
            const auto& input = engine.get_input_system();
            if (input.get_input_state().keys[GLFW_KEY_ESCAPE]) break;
            if (!engine.is_running()) break;
        }
    }
    loco.set_target_velocity(eva.hips_id, 0.0f, 0.0f);

    // Final measurements.
    const X::Stats s = X::stats();
    {
        auto v = ps.lock_particles_for_write();
        for (size_t k = 0; k < grass_ids.size(); ++k) {
            const size_t i = grass_ids[k];
            if (i >= v.size()) continue;
            const double dx = v[i].x - gx0[k], dy = v[i].y - gy0[k], dz = v[i].z - gz0[k];
            grass_worst_drift = std::fmax(grass_worst_drift,
                                          std::sqrt(dx * dx + dy * dy + dz * dz));
        }
    }
    // WHO DRIFTED, AND WAS IT STILL ATTACHED? A max over a set says a blade
    // moved; it does not say whether it was dragged while bonded or torn loose
    // and thrown. Those are different defects with different fixes.
    {
        auto v = ps.lock_particles_for_write();
        size_t worst_k = 0; double worst = -1.0;
        for (size_t k = 0; k < grass_ids.size(); ++k) {
            const size_t i = grass_ids[k];
            if (i >= v.size()) continue;
            const double dx = v[i].x - gx0[k], dy = v[i].y - gy0[k], dz = v[i].z - gz0[k];
            const double d = std::sqrt(dx*dx + dy*dy + dz*dz);
            if (d > worst) { worst = d; worst_k = k; }
        }
        const size_t wi = grass_ids[worst_k];
        if (wi < v.size()) {
            const size_t bonds_now =
                engine.get_physics_system().get_gluons_for_particle(wi).size();
            printf("\n  worst drifter P%zu: d=(%.2f, %.2f, %.2f) m\n",
                   wi, v[wi].x - gx0[worst_k], v[wi].y - gy0[worst_k],
                   v[wi].z - gz0[worst_k]);
            printf("    bonds still attached %zu | at_rest %d | mode %d | mass %.6f kg\n",
                   bonds_now, (int)v[wi].is_at_rest, (int)v[wi].solver_mode,
                   v[wi].GetMass());
            printf("    grass bonds in world: %zu at start -> %zu now (%s)\n",
                   gluons_at_start,
                   engine.get_physics_system().get_total_gluon_count(),
                   engine.get_physics_system().get_total_gluon_count() < gluons_at_start
                       ? "BONDS TORE" : "none tore");
        }
    }

    const float progressed = eva_y - eva_y0;

    printf("\n  %-46s %8.2f m  (need > 6.0)\n", "1. SHE GOT THERE: hips advanced", progressed);
    printf("  %-46s %8.2f m  (need 0.4 .. 2.0)\n", "2. SHE IS STILL UP: hips height", eva_z);
    printf("  %-46s %8llu    (need 0; worst %.1f m/s)\n", "3. NOTHING DETONATED: detector events",
           (unsigned long long)s.speed_events, s.worst_speed);
    printf("  %-46s %8.2f m  (need < 2.0)\n", "4. GRASS IS STILL GRASS: worst blade drift",
           grass_worst_drift);

    const bool got_there = progressed > 6.0f;
    const bool still_up  = eva_z > 0.4f && eva_z < 2.0f;
    const bool no_boom   = s.speed_events == 0;
    const bool grass_ok  = grass_worst_drift < 2.0;

    printf("\n");
    if (got_there && still_up && no_boom && grass_ok) {
        printf("  ALL IS GOOD, and now that is a measured claim: a human crossed three\n"
               "  patches of two grass kinds, brushing through bonded blades the whole\n"
               "  way, and arrived upright with the world intact behind her.\n");
    } else {
        if (!got_there) printf("  *** SHE DID NOT GET THERE: %.2f m. Grass stopped or deflected her.\n", progressed);
        if (!still_up)  printf("  *** SHE IS NOT UPRIGHT: hips at %.2f m. Fallen, buried, or launched.\n", eva_z);
        if (!no_boom)   printf("  *** SOMETHING DETONATED: %llu events, worst %.1f m/s.\n",
                               (unsigned long long)s.speed_events, s.worst_speed);
        if (!grass_ok)  printf("  *** GRASS FLEW: a blade ended %.2f m from home.\n", grass_worst_drift);
    }

    const bool pass = got_there && still_up && no_boom && grass_ok;
    printf("\n  %s\n", pass ? "PASS" : "FAIL");
    engine.shutdown();
    return pass;
}
