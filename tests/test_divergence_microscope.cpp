// =============================================================================
// THE DIVERGENCE, UNDER A MICROSCOPE (issue #47, task #37)
// =============================================================================
//   INTERACTIVE=1 ./build-release/logosphere-tests --test test_divergence_microscope --no-head
//   SPACE: single physics tick (after the freeze). ESC: quit.
//
// WHAT THIS IS. The walk-through-grass gate proved that a walking human
// brushing bonded tall grass makes the solver CREATE energy: a 5 kg stem goes
// from resting to 2.4e9 m/s inside one physics tick. One tick. There is no
// slow-motion footage of something that completes between two frames, so this
// viewer does the only honest thing:
//
//   1  Eva wades into one tall patch at normal speed. A live label shows the
//      fastest body in the world and a HISTORY of the last eight frames, so
//      the cliff is visible as numbers: 1.1  0.9  1.2  1.0  2.4e9.
//   2  The instant the explosion detector fires, the world FREEZES (dt=0).
//      The title names the frame, the body, and its speed. This is the crime
//      scene, held still.
//   3  From the freeze, every SPACE advances exactly ONE physics tick. You
//      step the aftermath: shrapnel leaving (a body at that speed vanishes
//      from the frame between two of your key presses, which is itself the
//      truest picture of the bug), the detector counters climbing.
//
// The divergence itself (impulse growing 16k -> 4.5e8 across the solver's
// iterations WITHIN the frozen tick) lives one level deeper than any camera:
// it is captured by the level-5 physics trace
// (LOGOSPHERE_PHYS_TRACE=5 LOGOSPHERE_PHYS_TRACE_IDS=<body>), and the freeze
// tells you exactly which frame and body to point that trace at.
//
// Headless, the same run reports whether the microscope CAUGHT a divergence.
// On this branch it should: the bug is open. The day the solver is fixed this
// prints that no freeze occurred, which is the gate (walk-through-grass)
// turning green, seen from here.
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
#include <cstdlib>
#include <deque>
#include <string>
#include <vector>

#ifdef __APPLE__
#include <GLFW/glfw3.h>
#endif

namespace X = ::logosphere::expdet;

bool test_divergence_microscope() {
    const bool interactive = std::getenv("INTERACTIVE") != nullptr;
    printf("\n=== THE DIVERGENCE, UNDER A MICROSCOPE (issue #47) ===\n");
    printf("  mode: %s\n", interactive
        ? "INTERACTIVE (freezes on detonation; then SPACE = one physics tick, ESC quits)"
        : "HEADLESS");

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

    // Minimal ground: just enough path for two metres of wading. Subjects, not
    // scenery: Eva's feet and the grass roots both need it.
    // EXACTLY the walk-through-grass scene. A minimal version (one patch,
    // Eva entering from a standing start 0.8 m away) ran 601 frames with NO
    // divergence: the trigger needs her at FULL GAIT, seven metres of stride
    // behind her, arms and legs swinging, exactly as the walk test has it.
    // A microscope pointed at a reproduction of the crime, not the crime,
    // sees nothing; so this is the crime, with a freeze-frame bolted on.
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
    ps.queue_light(-3.0f, -2.0f, 12.0f, 400000.0f, 45.0f, 1.0f, 0.96f, 0.9f);
    ps.flush_pending_particles();

    // One tall patch, dead ahead. The known detonator.
    auto& ogen  = engine.get_worldgen_system().get_organic_generator();
    auto& scene = engine.get_worldgen_system().get_scene_generator();
    size_t grass_gluons = 0;
    {
        const size_t g0 = engine.get_physics_system().get_total_gluon_count();
        struct Spot { float y; GrassPatchSpec spec; };
        const Spot spots[3] = {
            {3.0f, GrassPatchSpec::short_grass()},
            {6.0f, GrassPatchSpec::tall_grass()},   // the known detonator
            {9.0f, GrassPatchSpec::tall_grass()},
        };
        for (const Spot& sp : spots) {
            kg::EntityID patch = ogen.generate_grass_patch(0.0f, sp.y, 0.1f, sp.spec);
            scene.activate_entity_now(patch);
        }
        ps.flush_pending_particles();
        grass_gluons = engine.get_physics_system().get_total_gluon_count() - g0;
    }

    // Eva, one step away, walking in.
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
    loco.set_target_velocity(eva.hips_id, 0.0f, 1.2f);   // the walk test speed

    printf("  scene: %zu gluons across 3 patches, Eva walking the full path\n",
           grass_gluons);
    if (grass_gluons == 0) {
        printf("\n  *** NO BONDED GRASS: nothing to put under the microscope.\n\n  FAIL\n");
        engine.shutdown();
        return false;
    }

    if (interactive) {
        auto& camera = engine.get_camera_system();
        // Aimed at the KNOWN detonator, the tall patch at y=6, close enough
        // that blades are blades. Eva walks INTO frame from the left.
        camera.set_position(-4.5f, 3.6f, 2.6f);
        camera.look_at(0.0f, 6.0f, 1.2f);
        camera.set_pixels_per_unit(180.0f);
    }
    ui::Label *l_title = nullptr, *l_hist = nullptr, *l_hint = nullptr;
    if (auto* uis = engine.get_ui_system()) {
        auto mk = [&](int y, uint8_t r, uint8_t g, uint8_t b) {
            auto* L = new ui::Label("", "");
            L->set_position(24, y); L->set_size(1500, 22); L->set_color(r, g, b);
            uis->add_widget(L);
            return L;
        };
        l_title = mk(24, 255, 255, 255);
        l_hist  = mk(54, 120, 220, 255);
        l_hint  = mk(88, 140, 145, 150);
    }

    X::set_enabled(true);
    X::reset();

    // The run. Normal ticks until the detector fires; then frozen, and each
    // SPACE buys exactly one more tick.
    std::deque<double> history;           // last 8 frames' fastest body speed
    bool frozen = false;
    int frame = 0, freeze_frame = -1, freeze_body = -1;
    float freeze_speed = 0.0f;
    uint64_t events_seen = 0;
    bool quit = false;
    int post_freeze_ticks = 0;

    auto fmt_speed = [](double v) -> std::string {
        char b[32];
        if (v >= 1e6) snprintf(b, sizeof(b), "%.1e", v);
        else          snprintf(b, sizeof(b), "%.1f", v);
        return b;
    };

    while (!quit) {
        bool tick = !frozen;
        // interactive input first, so SPACE can request a tick while frozen
        if (interactive) {
            const auto& input = engine.get_input_system();
            static bool space_was = false;
            const bool space = input.get_input_state().keys[GLFW_KEY_SPACE];
            if (space && !space_was && frozen) { tick = true; post_freeze_ticks++; }
            space_was = space;
            if (input.get_input_state().keys[GLFW_KEY_ESCAPE]) quit = true;
            if (!engine.is_running()) quit = true;
        }

        // Detector state BEFORE the tick, so the freeze lands on the exact
        // frame the event fired and the world is held one tick after it.
        engine.update(tick ? (1.0 / 60.0) : 0.0);
        if (tick) frame++;
        const X::Stats s = X::stats();

        if (!frozen && s.speed_events > events_seen) {
            frozen = true;
            freeze_frame = frame;
            freeze_body = s.worst_id;
            freeze_speed = s.worst_speed;
        }
        events_seen = s.speed_events;

        if (tick) {
            // fastest body this frame, sampled cheaply from the detector
            history.push_back(s.worst_speed);
            if (history.size() > 8) history.pop_front();
        }

        if (l_title) {
            char buf[256];
            if (!frozen)
                snprintf(buf, sizeof(buf),
                         "WADING IN. frame %d   Eva brushing the patch; watch the history for the cliff.",
                         frame);
            else
                snprintf(buf, sizeof(buf),
                         "FROZEN ON THE CRIME SCENE. frame %d: particle %d reached %s m/s in ONE tick.",
                         freeze_frame, freeze_body, fmt_speed(freeze_speed).c_str());
            l_title->set_text(buf);
        }
        if (l_hist) {
            std::string h = "fastest body, last frames: ";
            for (double v : history) { h += fmt_speed(v); h += "  "; }
            if (frozen) h += "   (+" + std::to_string(post_freeze_ticks) + " stepped ticks)";
            l_hist->set_text(h);
        }
        if (l_hint) l_hint->set_text(interactive
            ? (frozen ? "SPACE: advance ONE physics tick   ESC: quit"
                      : "she is walking in; the freeze is automatic   ESC: quit")
            : "");

        engine.render();
        engine.present();

        if (!interactive) {
            if (frozen || frame > 900) quit = true;   // headless: capture and stop
        }
    }

    printf("\n");
    if (freeze_frame >= 0) {
        printf("  CAUGHT IT: frame %d, particle %d at %s m/s, %zu gluons in place.\n",
               freeze_frame, freeze_body, fmt_speed(freeze_speed).c_str(), grass_gluons);
        printf("  Point the deep trace at it:\n"
               "    LOGOSPHERE_PHYS_TRACE=5 LOGOSPHERE_PHYS_TRACE_IDS=%d \\\n"
               "    LOGOSPHERE_PHYS_TRACE_FILE=/tmp/div.log <this test headless>\n",
               freeze_body);
        printf("  history into the cliff: ");
        for (double v : history) printf("%s  ", fmt_speed(v).c_str());
        printf("\n");
    } else {
        printf("  NO DIVERGENCE OCCURRED in %d frames. If the solver has been fixed,\n"
               "  this is the walk-through-grass gate turning green, seen from here;\n"
               "  retire this microscope or point it at the next open detonation.\n", frame);
    }
    printf("\n  PASS (diagnostic)\n");
    engine.shutdown();
    return true;
}
