// =============================================================================
// GRASS MUST YIELD: walking through blades should cost a wade, not a wall
// =============================================================================
//   INTERACTIVE=1 ./build-release/logosphere-tests --test test_grass_yields --no-head
//
// THE OWNER'S OBSERVATION, watching the microscope: Eva gets STUCK when she
// touches blades. The expectation is that grass bends gracefully out of a
// walker's way. This file is the instrumentation for that expectation: it
// measures, per frame, the three numbers that decide who is right and WHY.
//
//   SPEED RETENTION   her actual forward speed against the commanded 1.2 m/s.
//                     Wading through grass costs something; walking into
//                     posts costs everything. Retention is the stuck-ness
//                     number, and the gate: a graceful wade keeps >= 75%.
//   POST CONTACTS     every Eva-vs-grass collision event, split by whether
//                     the grass side is KINEMATIC. This is the diagnosis
//                     column: the bonding fix rooted each blade's BASE
//                     segment as KINEMATIC so plants stand, and a kinematic
//                     body is an immovable post. If the split shows her
//                     grinding against kinematic bases, the rooting choice is
//                     convicted; if the posts are zero and she still sticks,
//                     the fault is elsewhere and this column just acquitted it.
//   BLADE BEND        max gluon stretch across the grass while she passes,
//                     current length minus rest length. Bending grass shows
//                     stretch that rises near her and recovers behind her.
//                     Zero stretch while she is stuck means the blades are
//                     not even being ASKED to bend: the contact stops her
//                     before the bond ever feels it.
//
// Headless asserts the expectation (RED today is the point: it documents the
// stuck-ness with numbers). Interactive shows her wade with the three vitals
// live. Same lessons as every viewer: widgets, aimed camera, lit-pixel proof,
// nothing in the scene the scenario does not need.
// =============================================================================

#include "../src/core/engine.h"
#include "../src/core/explosion_detector.h"
#include "../src/core/particle_system.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include "../src/ui/ui_system.h"
#include "../src/ui/widgets.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/worldgen/grass_patch_spec.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/worldgen/organic_generator.h"
#include "logosphere/worldgen/scene_chunk_generator.h"
#include "logosphere/worldgen/worldgen_system.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#ifdef __APPLE__
#include <GLFW/glfw3.h>
#endif

namespace X = ::logosphere::expdet;

bool test_grass_yields() {
    const bool interactive = std::getenv("INTERACTIVE") != nullptr;
    printf("\n=== GRASS MUST YIELD (owner: 'blades should bend gracefully') ===\n");
    printf("  mode: %s\n", interactive ? "INTERACTIVE (ESC quits)" : "HEADLESS");

    EngineConfig cfg;
    cfg.create_display = interactive;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) { printf("  ERROR: engine init failed\n"); return false; }

    {   // ontology, the sanctioned way
        kg::OntologyRegistry reg;
        reg.addEntityType("Grass",      "Plant", false);
        reg.addEntityType("GrassPatch", "Plant", false);
        reg.addAncestors("Grass",      {"Plant", "LivingEntity", "WorldEntity", "Entity"});
        reg.addAncestors("GrassPatch", {"Plant", "LivingEntity", "WorldEntity", "Entity"});
        engine.get_kg().extendOntology(reg);
    }

    auto& ps = engine.get_particle_system();
    for (int x = -2; x <= 2; ++x)
        for (int y = -2; y <= 9; ++y) {
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
    ps.queue_light(-3.0f, 2.0f, 14.0f, 450000.0f, 48.0f, 1.0f, 0.96f, 0.9f);
    ps.flush_pending_particles();

    // One tall patch dead on her line. Dense enough that she MUST wade.
    auto& ogen  = engine.get_worldgen_system().get_organic_generator();
    auto& scene = engine.get_worldgen_system().get_scene_generator();
    std::set<size_t> grass_ids;
    size_t grass_gluons = 0;
    {
        const size_t b0 = [&] { auto v = ps.lock_particles_for_write(); return v.size(); }();
        const size_t g0 = engine.get_physics_system().get_total_gluon_count();
        kg::EntityID patch = ogen.generate_grass_patch(0.0f, 4.0f, 0.1f,
                                                       GrassPatchSpec::tall_grass());
        scene.activate_entity_now(patch);
        ps.flush_pending_particles();
        grass_gluons = engine.get_physics_system().get_total_gluon_count() - g0;
        auto v = ps.lock_particles_for_write();
        for (size_t i = b0; i < v.size(); ++i) grass_ids.insert(i);
    }

    // Rest lengths of every grass gluon, captured before contact, so bend is
    // measured against what generation built and not a guess.
    struct BondRef { size_t a, b; float rest; };
    std::vector<BondRef> bonds;
    {
        auto v = ps.lock_particles_for_write();
        std::set<std::pair<size_t,size_t>> seen;
        for (size_t id : grass_ids) {
            for (const auto* g : engine.get_physics_system().get_gluons_for_particle(id)) {
                auto key = std::minmax(g->particle_a, g->particle_b);
                if (!seen.insert(key).second) continue;
                const Particle& pa = v[g->particle_a];
                const Particle& pb = v[g->particle_b];
                const float dx = pb.x - pa.x, dy = pb.y - pa.y, dz = pb.z - pa.z;
                bonds.push_back({g->particle_a, g->particle_b,
                                 std::sqrt(dx*dx + dy*dy + dz*dz)});
            }
        }
    }

    HumanoidGenerator hgen;
    hgen.initialize(&engine, &engine.get_kg());
    HumanoidSpec spec = HumanoidSpec::hunter();
    spec.facing_angle = 0.0f;
    auto eva = hgen.generate_humanoid_physics(0.0f, -1.0f, 0.1f, -1, spec, false);
    std::set<size_t> eva_ids;
    {
        auto add = [&](const std::vector<int>& v2) { for (int i : v2) eva_ids.insert((size_t)i); };
        eva_ids.insert((size_t)eva.hips_id);
        add(eva.left_leg_ids); add(eva.right_leg_ids);
        add(eva.left_arm_ids); add(eva.right_arm_ids); add(eva.torso_ids);
    }
    auto& loco = engine.get_humanoid_locomotion();
    loco.register_humanoid_direct(eva.hips_id,
                                  eva.left_leg_ids, eva.right_leg_ids,
                                  eva.left_arm_ids, eva.right_arm_ids,
                                  eva.torso_ids, 150.0f, 600.0f);
    loco.reset_humanoid_position(eva.hips_id);
    loco.set_volitional(eva.hips_id, true);
    const float COMMANDED = 1.2f;
    loco.set_target_velocity(eva.hips_id, 0.0f, COMMANDED);

    printf("  scene: %zu grass bodies, %zu gluons (%zu bonds tracked), Eva wading at %.1f m/s\n",
           grass_ids.size(), grass_gluons, bonds.size(), COMMANDED);
    if (grass_ids.empty() || grass_gluons == 0 || bonds.empty()) {
        printf("\n  *** SCENE DID NOT BUILD (grass %zu, gluons %zu, bonds %zu).\n\n  FAIL\n",
               grass_ids.size(), grass_gluons, bonds.size());
        engine.shutdown();
        return false;
    }

    if (interactive) {
        auto& camera = engine.get_camera_system();
        camera.set_position(-10.0f, 0.0f, 5.5f);
        camera.look_at(0.0f, 4.0f, 1.0f);
        camera.set_pixels_per_unit(60.0f);
    }
    ui::Label *l_title = nullptr, *l_live = nullptr, *l_diag = nullptr;
    if (auto* uis = engine.get_ui_system()) {
        auto mk = [&](int y, uint8_t r, uint8_t g, uint8_t b) {
            auto* L = new ui::Label("", "");
            L->set_position(24, y); L->set_size(1500, 22); L->set_color(r, g, b);
            uis->add_widget(L);
            return L;
        };
        l_title = mk(24, 255, 255, 255);
        l_live  = mk(54, 120, 220, 255);
        l_diag  = mk(84, 255, 180, 90);
        if (l_title) l_title->set_text(
            "GRASS MUST YIELD: she should wade through at speed while blades bend and recover.");
    }

    X::set_enabled(true);
    X::reset();

    // The wade: measure only WHILE SHE IS IN OR NEAR THE PATCH (y in 2.5..5.5),
    // because averaging in the open-road approach would dilute the stuck-ness
    // this instrument exists to see.
    const int FRAMES = 660;
    double sum_speed_in_patch = 0.0; int frames_in_patch = 0;
    uint64_t contacts_total = 0, contacts_kinematic = 0;
    double max_bend = 0.0, bend_at_exit = 0.0;
    float prev_y = -1.0f;

    for (int f = 0; f < FRAMES; ++f) {
        engine.update(1.0 / 60.0);
        float eva_y, eva_vy;
        {
            auto v = ps.lock_particles_for_write();
            eva_y  = v[eva.hips_id].y;
            eva_vy = v[eva.hips_id].vy;
        }
        (void)prev_y; prev_y = eva_y;

        // POST CONTACTS: every Eva-vs-grass event this frame, split by whether
        // the grass side is kinematic (the rooted base = the suspected post).
        for (const auto& ev : engine.get_physics_system().get_collision_events()) {
            const bool a_eva = eva_ids.count(ev.particle_a) > 0;
            const bool b_eva = eva_ids.count(ev.particle_b) > 0;
            const bool a_grass = grass_ids.count(ev.particle_a) > 0;
            const bool b_grass = grass_ids.count(ev.particle_b) > 0;
            if ((a_eva && b_grass) || (b_eva && a_grass)) {
                contacts_total++;
                const bool grass_kin = a_grass ? ev.a_is_kinematic : ev.b_is_kinematic;
                if (grass_kin) contacts_kinematic++;
            }
        }

        // BLADE BEND: current bond length vs rest, worst across the patch.
        double bend_now = 0.0;
        {
            auto v = ps.lock_particles_for_write();
            for (const BondRef& b : bonds) {
                if (b.a >= v.size() || b.b >= v.size()) continue;
                const float dx = v[b.b].x - v[b.a].x, dy = v[b.b].y - v[b.a].y,
                            dz = v[b.b].z - v[b.a].z;
                const double stretch = std::fabs(std::sqrt(dx*dx + dy*dy + dz*dz) - b.rest);
                bend_now = std::fmax(bend_now, stretch);
            }
        }
        max_bend = std::fmax(max_bend, bend_now);
        if (f == FRAMES - 1) bend_at_exit = bend_now;

        if (eva_y > 2.5f && eva_y < 5.5f) {
            sum_speed_in_patch += eva_vy;
            frames_in_patch++;
        }

        if (interactive) {
            if (l_live) {
                char buf[256];
                snprintf(buf, sizeof(buf),
                         "Eva y=%5.2f  fwd %4.2f/%.1f m/s  bend now %5.3f m  detector %llu",
                         eva_y, eva_vy, COMMANDED, bend_now,
                         (unsigned long long)X::stats().speed_events);
                l_live->set_text(buf);
            }
            if (l_diag) {
                char buf[256];
                snprintf(buf, sizeof(buf),
                         "grass contacts %llu, of which vs KINEMATIC (rooted posts) %llu",
                         (unsigned long long)contacts_total,
                         (unsigned long long)contacts_kinematic);
                l_diag->set_text(buf);
            }
            engine.render();
            engine.present();
            const auto& input = engine.get_input_system();
            if (input.get_input_state().keys[GLFW_KEY_ESCAPE]) break;
            if (!engine.is_running()) break;
        }
    }
    loco.set_target_velocity(eva.hips_id, 0.0f, 0.0f);

    const double mean_speed = frames_in_patch ? sum_speed_in_patch / frames_in_patch : 0.0;
    const double retention = mean_speed / COMMANDED;
    const X::Stats s = X::stats();
    const double kin_fraction = contacts_total
        ? (double)contacts_kinematic / (double)contacts_total : 0.0;

    printf("\n  %-52s %7.2f m/s of %.1f (%.0f%%; need >= 75%%)\n",
           "SPEED RETENTION in the patch", mean_speed, COMMANDED, retention * 100.0);
    printf("  %-52s %7llu of %llu (%.0f%%)\n",
           "POST CONTACTS: grass side KINEMATIC",
           (unsigned long long)contacts_kinematic,
           (unsigned long long)contacts_total, kin_fraction * 100.0);
    printf("  %-52s %7.3f m (exit %.3f; bend then recover)\n",
           "BLADE BEND: worst bond stretch", max_bend, bend_at_exit);
    printf("  %-52s %7llu (worst %.1f m/s)\n", "detector events",
           (unsigned long long)s.speed_events, s.worst_speed);

    const bool wades   = retention >= 0.75 && frames_in_patch > 30;
    const bool no_boom = s.speed_events == 0;
    printf("\n");
    if (wades && no_boom) {
        printf("  SHE WADES. %.0f%% of commanded speed through the patch, blades bent up\n"
               "  to %.3f m and settled to %.3f m, nothing detonated. Graceful enough.\n",
               retention * 100.0, max_bend, bend_at_exit);
    } else {
        if (!wades) {
            printf("  *** SHE IS STUCK: %.0f%% speed retention in the patch. ***\n", retention * 100.0);
            if (kin_fraction > 0.3)
                printf("  DIAGNOSIS COLUMN: %.0f%% of her grass contacts are against KINEMATIC\n"
                       "  bodies, the rooted blade bases. A kinematic body is an immovable post;\n"
                       "  she is not wading through grass, she is walking into fence pickets the\n"
                       "  bonding fix planted. The rooting mechanism needs to yield (dynamic base\n"
                       "  with a strong anchor gluon, or contacts that exempt walker-vs-root).\n",
                       kin_fraction * 100.0);
            else
                printf("  DIAGNOSIS COLUMN: only %.0f%% of contacts are kinematic, so the rooted\n"
                       "  bases are ACQUITTED. The resistance is elsewhere: bond stiffness, the\n"
                       "  AABB slab colliders, or locomotion yielding to contact impulses.\n",
                       kin_fraction * 100.0);
            if (max_bend < 0.01)
                printf("  Bend never exceeded %.3f m: the blades were not even ASKED to bend;\n"
                       "  contact stops her before the bonds feel anything.\n", max_bend);
        }
        if (!no_boom)
            printf("  *** DETONATED while wading: %llu events, worst %.1f m/s. ***\n",
                   (unsigned long long)s.speed_events, s.worst_speed);
    }

    const bool pass = wades && no_boom;
    printf("\n  %s\n", pass ? "PASS" : "FAIL (this failure is the owner's observation, measured)");
    engine.shutdown();
    return pass;
}
