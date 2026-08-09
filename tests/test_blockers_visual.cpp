// =============================================================================
// UNBONDED vs BONDED GRASS, SIDE BY SIDE (issue #47)
// =============================================================================
//   INTERACTIVE=1 ./build-release/logosphere-tests --test test_blockers_visual --no-head
//   SPACE advances. ESC quits.
//
// WHAT YOU ARE LOOKING AT. Two tall-grass patches from the same spec:
//
//   LEFT  (x = -3)   UNBONDED: generated with the legacy lever on, exactly what
//                    the engine produced for its whole life. Each blade is a
//                    TOWER OF LOOSE PLATES, 2-5 ultra-thin particles stacked
//                    with nothing joining them. If it looks like "stuff
//                    stacked", that is because it literally is: this is the
//                    bug's anatomy, visible.
//   RIGHT (x = +3)   BONDED: the fix. Same spec, but every blade's plates are
//                    joined by gluons and the base is rooted. It should look
//                    the same at rest, and that resemblance is the point: the
//                    difference is not how they look, it is what the solver
//                    has to DO to keep them there, and what happens when
//                    anything touches them.
//
// A previous version of this viewer showed one patch and two twigs and asked
// the viewer to appreciate that nothing happened. Nothing happening is
// invisible. This version shows the DISEASE next to the CURE and prints each
// side's vital signs live, so even a calm frame tells you which side is
// working and how hard:
//
//   contact rows   what the solver must grind every frame to fake cohesion.
//                  The unbonded side pays this forever; it is the sandwich the
//                  iteration DIVERGED on in Eden (16k -> 4.5e8 in one frame).
//   gluons         what the bonded side uses instead. Bonds are solved too,
//                  but they are the DESIGNED mechanism, not an accident.
//   worst speed    anything over ~2 m/s here is a body being thrown, not
//                  settling. Eden's unbonded grass reached 100 m/s (clamped
//                  from 1.78e12).
//   mean drift     how far each side's plates have moved from where they were
//                  placed. Holding still is the whole job of a plant.
//
// STAGES: 1 frozen and labelled, 2 released (watch the vitals diverge), 3 a
// STOMP: the same downward kick is applied to one plate on EACH side, because
// a resting comparison hides the difference; disturbance reveals it. 4 verdict.
//
// Lessons carried: registered widgets only; camera aimed + lit-pixel proof;
// dt=0 freeze (still pumps the window); no scenery; self-checking A/B (if the
// two sides are not actually different in bonding, it says so and fails).
// =============================================================================

#include "../src/core/engine.h"
#include "../src/core/explosion_detector.h"
#include "../src/core/particle_system.h"
#include "../src/core/telemetry.h"
#include "../src/particle.h"
#include "../src/ui/ui_system.h"
#include "../src/ui/widgets.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/worldgen/grass_patch_spec.h"
#include "logosphere/worldgen/organic_generator.h"
#include "logosphere/worldgen/scene_chunk_generator.h"
#include "logosphere/worldgen/worldgen_system.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef __APPLE__
#include <GLFW/glfw3.h>
#endif

namespace X = ::logosphere::expdet;
namespace T = ::logosphere::telemetry;

namespace {

struct Side {
    const char* tag;
    float x;
    bool legacy;
    std::vector<size_t> ids;
    std::vector<float> x0, y0, z0;
    size_t gluons = 0;
    double mean_drift = 0.0, worst_speed = 0.0;
};

const char* STAGE_TITLE[4] = {
    "STAGE 1 of 4   FROZEN    LEFT: unbonded (the bug)   RIGHT: bonded (the fix)",
    "STAGE 2 of 4   RELEASED  same spec, same physics; watch the vitals, not the shapes",
    "STAGE 3 of 4   STOMP     one plate on EACH side just got the same downward kick",
    "STAGE 4 of 4   VERDICT   the numbers below are what headless CI reads",
};
const char* STAGE_BODY[4] = {
    "Every blade is 2-5 thin plates stacked into a tower; that IS what tall grass is here."
    " LEFT the plates are loose (zero gluons, the engine's lifelong default). RIGHT each"
    " tower is glued and rooted. At rest they look identical, and that is the trap.",
    "The LEFT side has no bonds, so the solver fakes cohesion with contact rows, forever."
    " That grinding sandwich is what diverged in Eden at 1.78e12 m/s. The RIGHT side's"
    " plates are held by design. Compare 'worst' and 'drift' per side as time passes.",
    "A resting comparison flatters the bug, so both sides get the same insult: one plate"
    " kicked downward into its own tower. Watch which side swallows it and which side"
    " scatters. This is a canopy being brushed by anything that moves, in miniature.",
    "If LEFT drifted and RIGHT held, you have seen issue #47 and its fix in one frame."
    " The blockers (Eden's rod detonation, the FPS bill) live on the issue; this scene"
    " is the part of the story that CAN be seen.",
};

}  // namespace

bool test_blockers_visual() {
    const bool interactive = std::getenv("INTERACTIVE") != nullptr;
    printf("\n=== UNBONDED vs BONDED GRASS, SIDE BY SIDE (issue #47) ===\n");
    printf("  mode: %s%s\n", interactive ? "INTERACTIVE" : "HEADLESS",
           interactive ? "  (SPACE advances, ESC quits)" : "  (set INTERACTIVE=1 to watch)");

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

    auto& camera = engine.get_camera_system();
    camera.set_position(0.0f, -8.0f, 4.5f);
    camera.look_at(0.0f, 0.0f, 1.0f);
    camera.set_pixels_per_unit(150.0f);   // close enough that blades are blades

    auto& ps = engine.get_particle_system();
    ps.queue_light(0.0f, -6.0f, 15.0f, 500000.0f, 50.0f, 1.0f, 0.96f, 0.9f);

    auto& ogen  = engine.get_worldgen_system().get_organic_generator();
    auto& scene = engine.get_worldgen_system().get_scene_generator();

    Side L{"LEFT/unbonded", -3.0f, true};
    Side R{"RIGHT/bonded",  +3.0f, false};
    for (Side* s : {&L, &R}) {
        const size_t b0 = [&] { auto v = ps.lock_particles_for_write(); return v.size(); }();
        const size_t g0 = engine.get_physics_system().get_total_gluon_count();
        OrganicGenerator::set_legacy_unbonded(s->legacy);
        kg::EntityID patch = ogen.generate_grass_patch(s->x, 0.0f, 0.0f,
                                                       GrassPatchSpec::tall_grass());
        OrganicGenerator::set_legacy_unbonded(false);
        scene.activate_entity_now(patch);
        ps.flush_pending_particles();
        s->gluons = engine.get_physics_system().get_total_gluon_count() - g0;
        auto v = ps.lock_particles_for_write();
        for (size_t i = b0; i < v.size(); ++i) {
            s->ids.push_back(i);
            s->x0.push_back(v[i].x); s->y0.push_back(v[i].y); s->z0.push_back(v[i].z);
        }
    }

    printf("  LEFT  bodies %zu, gluons %zu   |   RIGHT bodies %zu, gluons %zu\n",
           L.ids.size(), L.gluons, R.ids.size(), R.gluons);

    // Self-check: the A/B must actually differ, and both sides must exist.
    if (L.ids.empty() || R.ids.empty() || L.gluons >= R.gluons || R.gluons == 0) {
        printf("\n  *** THE TWO SIDES ARE NOT A COMPARISON. ***\n"
               "  LEFT %zu bodies / %zu gluons, RIGHT %zu bodies / %zu gluons. The left\n"
               "  must be unbonded and the right bonded, or there is nothing to see and\n"
               "  every label below would be a lie.\n",
               L.ids.size(), L.gluons, R.ids.size(), R.gluons);
        printf("\n  FAIL\n");
        engine.shutdown();
        return false;
    }

    auto sample = [&](Side& s) {
        auto v = ps.lock_particles_for_write();
        double sum = 0.0; s.worst_speed = 0.0;
        for (size_t k = 0; k < s.ids.size(); ++k) {
            const size_t i = s.ids[k];
            if (i >= v.size()) continue;
            const double dx = v[i].x - s.x0[k], dy = v[i].y - s.y0[k], dz = v[i].z - s.z0[k];
            sum += std::sqrt(dx * dx + dy * dy + dz * dz);
            const double sp = std::sqrt((double)v[i].vx * v[i].vx +
                                        (double)v[i].vy * v[i].vy +
                                        (double)v[i].vz * v[i].vz);
            s.worst_speed = std::fmax(s.worst_speed, sp);
        }
        s.mean_drift = s.ids.empty() ? 0.0 : sum / (double)s.ids.size();
    };
    auto stomp = [&](Side& s) {
        // The same insult for both: the plate nearest mid-height gets kicked
        // down into its own tower at 3 m/s, a brush, not a bullet.
        auto v = ps.lock_particles_for_write();
        size_t best = s.ids.front(); double bestd = 1e9;
        for (size_t i : s.ids) {
            if (i >= v.size()) continue;
            const double d = std::fabs(v[i].z - 1.2);
            if (d < bestd) { bestd = d; best = i; }
        }
        // -3 m/s separated nothing headlessly (0.030 vs 0.025 m): an unbonded
        // stack in isolation shrugs off a tap, which is consistent with every
        // isolation rung so far. A trample is the honest minimum insult that
        // shows the difference, and 12 m/s is the battery's projectile speed,
        // an established in-world magnitude, not a tuned number.
        v[best].vz = -12.0f;
        v[best].is_at_rest = false;
    };

    ui::Label *l_title = nullptr, *l_body = nullptr, *l_left = nullptr,
              *l_right = nullptr, *l_hint = nullptr;
    if (auto* uis = engine.get_ui_system()) {
        auto mk = [&](int y, uint8_t r, uint8_t g, uint8_t b) {
            auto* lab = new ui::Label("", "");
            lab->set_position(24, y); lab->set_size(1500, 22); lab->set_color(r, g, b);
            uis->add_widget(lab);
            return lab;
        };
        l_title = mk(24, 255, 255, 255);
        l_body  = mk(54, 190, 200, 210);
        l_left  = mk(96, 255, 110, 100);   // red: the bug
        l_right = mk(124, 110, 235, 130);  // green: the fix
        l_hint  = mk(160, 140, 145, 150);
    }

    X::set_enabled(true);
    X::reset();

    int stage = 0, frames_in_stage = 0;
    bool quit = false, stomped = false;
    const int AUTO_ADVANCE[4] = {60, 300, 300, 90};

    while (!quit) {
        engine.update(stage >= 1 ? (1.0 / 60.0) : 0.0);
        if (stage == 2 && !stomped) { stomp(L); stomp(R); stomped = true; }
        sample(L); sample(R);
        const X::Stats s = X::stats();

        char buf[512];
        if (l_title) l_title->set_text(STAGE_TITLE[stage]);
        if (l_body)  l_body->set_text(STAGE_BODY[stage]);
        if (l_left) {
            snprintf(buf, sizeof(buf),
                     "LEFT  UNBONDED   gluons %3zu   worst %6.2f m/s   mean drift %6.3f m",
                     L.gluons, L.worst_speed, L.mean_drift);
            l_left->set_text(buf);
        }
        if (l_right) {
            snprintf(buf, sizeof(buf),
                     "RIGHT BONDED     gluons %3zu   worst %6.2f m/s   mean drift %6.3f m   detector events %llu",
                     R.gluons, R.worst_speed, R.mean_drift,
                     (unsigned long long)s.speed_events);
            l_right->set_text(buf);
        }
        if (l_hint) l_hint->set_text(interactive ? "SPACE: next stage   ESC: quit" : "");

        engine.render();
        engine.present();

        if (interactive) {
            const auto& input = engine.get_input_system();
            static bool space_was = false;
            const bool space = input.get_input_state().keys[GLFW_KEY_SPACE];
            if (space && !space_was) { if (stage < 3) stage++; frames_in_stage = 0; }
            space_was = space;
            if (input.get_input_state().keys[GLFW_KEY_ESCAPE]) quit = true;
            if (!engine.is_running()) quit = true;
        } else {
            if (++frames_in_stage >= AUTO_ADVANCE[stage]) {
                frames_in_stage = 0;
                if (++stage > 3) quit = true;
            }
        }
    }

    long lit = 0;
    {
        engine.get_renderer().wait_for_completion();
        int w = engine.get_render_buffer().width();
        int h = engine.get_render_buffer().height();
        std::vector<uint32_t> px((size_t)w * h, 0u);
        if (engine.read_latest_framebuffer(px.data(), w, h)) {
            for (uint32_t c : px) {
                const int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
                if (r + g + b > 60) lit++;
            }
        }
    }

    printf("\n  %-24s %10s %12s\n", "side", "worst m/s", "mean drift m");
    printf("  %-24s %10.2f %12.3f\n", L.tag, L.worst_speed, L.mean_drift);
    printf("  %-24s %10.2f %12.3f\n", R.tag, R.worst_speed, R.mean_drift);
    printf("  lit pixels at exit: %ld (scene visible: %s)\n", lit, lit > 5000 ? "yes" : "NO");

    if (L.mean_drift > R.mean_drift * 2.0 && L.mean_drift > 0.05) {
        printf("\n  THE COMPARISON SHOWED ITSELF: the unbonded side scattered (%.3f m mean)\n"
               "  while the bonded side held (%.3f m). That gap is issue #47's fix, seen.\n",
               L.mean_drift, R.mean_drift);
    } else {
        printf("\n  The two sides ended close (%.3f vs %.3f m). In isolation the unbonded\n"
               "  stack can survive what Eden's cannot; the stomp may need to be harder, or\n"
               "  the divergence needs Eden's ingredients. Honest either way: the vitals\n"
               "  above are the observation.\n", L.mean_drift, R.mean_drift);
    }

    const bool visible = !interactive || lit > 5000;
    printf("\n  %s\n", visible ? "PASS (diagnostic)" : "FAIL (nothing was on screen)");
    engine.shutdown();
    return visible;
}
