// =============================================================================
// SEE THE TREE BUG AND ITS FIX, SIDE BY SIDE (issue #38)
// =============================================================================
//   INTERACTIVE=1 ./build-release/logosphere-tests --test test_tree_repair_visual --no-head
//   SPACE advances the stage. ESC quits.
//
// WHAT YOU ARE LOOKING AT.
//
// Two trees, generated from the SAME seed, so they start life identical.
//
//   LEFT  (x = -6)   BROKEN: the generator's carefully placed leaf positions are
//                    thrown away and every leaf on a branch is put at the same
//                    point, on top of its siblings.
//   RIGHT (x = +6)   FIXED: the placed positions are honoured.
//
// Nothing else differs. Same seed, same spec, same solver, same frame.
//
// WHY SIDE BY SIDE INSTEAD OF A TOGGLE. Four visual tests in this repo shipped
// with a toggle the user pressed and could not tell whether anything had
// happened, because the change was invisible or the toggle silently did
// nothing. Two trees in one frame need no trust: if they look the same, there
// is no effect, and you can see that immediately.
//
// THE STAGES.
//
//   1  FROZEN AT BIRTH. Physics is off. Both trees look like trees. This is the
//      important one: the bug is already fully present here and completely
//      invisible, because overlap does not show until something reacts to it.
//      The readout counts the bodies created inside each other.
//   2  RELEASED. Physics runs. The left canopy is thrown apart; the right one
//      settles where it was drawn. Live speed and drift for each.
//   3  SETTLED. Final numbers and the verdict the automated gate applies.
//
// TEXT IS DRAWN THROUGH REGISTERED WIDGETS, NEVER draw_text.
// Engine::present() clears the overlay plane and then re-renders only the
// registered widgets, so immediate-mode text drawn from a test between render()
// and present() is erased before it reaches the screen. That is documented in
// docs/VISUAL_TESTS.md and it is why several earlier tests had readouts nobody
// ever saw. tests/test_ui_label_actually_renders.cpp is the proof that the
// widget route works.
//
// Headless (no INTERACTIVE) it runs the same stages, prints the same numbers,
// and asserts the same verdict, so CI covers what the eye is being shown.
// =============================================================================

#include "../src/core/engine.h"
#include "../src/core/particle_system.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include "../src/ui/ui_system.h"
#include "../src/ui/widgets.h"
#include "logosphere/worldgen/physics_tree_generator.h"
#include "logosphere/worldgen/tree_generator.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef __APPLE__
#include <GLFW/glfw3.h>
#endif

namespace {

struct Side {
    const char* name;
    float       x_offset;
    bool        legacy;              // true = the bug
    PhysicsTreeResult tree;
    std::vector<int>  leaf_ids;
    std::vector<float> x0, y0, z0;   // where each leaf was placed
    int    coincident_at_birth = 0;
    double peak_speed = 0.0;
    double mean_drift = 0.0;
    double max_drift  = 0.0;
};

float dist3(float ax, float ay, float az, float bx, float by, float bz) {
    const float dx = ax - bx, dy = ay - by, dz = az - bz;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Same bounds the automated gate uses (test_foliage_stays_attached), repeated
// here so what you watch and what CI asserts cannot drift apart.
constexpr double MAX_SPEED_MS   = 2.0;
constexpr double MAX_MEAN_DRIFT = 0.5;

void build(Engine& engine, Side& s) {
    PhysicsTreeGenerator::set_legacy_placement(s.legacy);
    PhysicsTreeGenerator gen;
    gen.initialize(&engine);
    TreeSpec spec;
    spec.random_seed = 12345;          // identical for both sides
    s.tree = gen.generate_tree_with_roots(s.x_offset, 0.0f, 0.0f, spec);
    PhysicsTreeGenerator::set_legacy_placement(false);
    engine.get_particle_system().flush_pending_particles();

    auto v = engine.get_particle_system().lock_particles_for_write();
    for (int id : s.tree.leaf_ids) {
        if (id < 0 || (size_t)id >= v.size()) continue;
        s.leaf_ids.push_back(id);
        s.x0.push_back(v[id].x); s.y0.push_back(v[id].y); s.z0.push_back(v[id].z);
    }
    // Bodies created in the same place. This is the bug, before anything moves.
    for (size_t i = 0; i < s.leaf_ids.size(); ++i)
        for (size_t j = i + 1; j < s.leaf_ids.size(); ++j)
            if (dist3(s.x0[i], s.y0[i], s.z0[i], s.x0[j], s.y0[j], s.z0[j]) < 0.001f)
                s.coincident_at_birth++;
}

void sample(Engine& engine, Side& s) {
    auto v = engine.get_particle_system().lock_particles_for_write();
    double sum = 0.0;
    s.max_drift = 0.0;
    for (size_t i = 0; i < s.leaf_ids.size(); ++i) {
        const int id = s.leaf_ids[i];
        if (id < 0 || (size_t)id >= v.size()) continue;
        const Particle& p = v[id];
        const double d = dist3(p.x, p.y, p.z, s.x0[i], s.y0[i], s.z0[i]);
        sum += d;
        if (d > s.max_drift) s.max_drift = d;
        const double spd = std::sqrt((double)p.vx * p.vx + (double)p.vy * p.vy
                                   + (double)p.vz * p.vz);
        if (spd > s.peak_speed) s.peak_speed = spd;
    }
    s.mean_drift = s.leaf_ids.empty() ? 0.0 : sum / (double)s.leaf_ids.size();
}

const char* STAGE_TITLE[3] = {
    "STAGE 1 of 3   FROZEN AT BIRTH   physics is off",
    "STAGE 2 of 3   RELEASED          physics running",
    "STAGE 3 of 3   SETTLED           final verdict",
};
const char* STAGE_BODY[3] = {
    "Both trees were just created and look fine. The bug is ALREADY HERE and you cannot see it:"
    " overlap is invisible until something reacts to it. Read the COINCIDENT counts below.",
    "Physics is now running. Watch the LEFT canopy get thrown outward while the RIGHT one"
    " settles where it was drawn. Nothing differs but where the leaves were placed.",
    "Both trees have stopped. The gate is NO SHOOTING: a leaf may fall, it may not be launched."
    " Limits are 2.00 m/s peak speed and 0.50 m mean canopy drift.",
};

}  // namespace

bool test_tree_repair_visual() {
    const bool interactive = std::getenv("INTERACTIVE") != nullptr;
    printf("\n=== TREE REPAIR, SIDE BY SIDE (issue #38) ===\n");
    printf("  mode: %s%s\n", interactive ? "INTERACTIVE" : "HEADLESS",
           interactive ? "  (SPACE advances, ESC quits)" : "  (set INTERACTIVE=1 to watch it)");

    EngineConfig cfg;
    cfg.create_display = interactive;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) { printf("  ERROR: engine init failed\n"); return false; }

    // AIM THE CAMERA. Without this the default view points somewhere else and
    // the window is empty: the first run of this test rendered 1,860 surfaces
    // for 44,000 frames and showed nothing, because "it is drawing" and "you
    // can see it" are different claims. The trees sit at x = -6 and x = +6 and
    // stand about 8 m tall, so the view has to cover roughly 24 m across.
    auto& camera = engine.get_camera_system();
    camera.set_position(-16.0f, -20.0f, 13.0f);
    camera.look_at(0.0f, 0.0f, 4.0f);      // between the two trees, mid-trunk
    camera.set_pixels_per_unit(46.0f);

    auto& ps = engine.get_particle_system();
    ps.queue_light(0.0f, -14.0f, 26.0f, 900000.0f, 60.0f, 1.0f, 0.96f, 0.9f);

    // NO DECORATIVE FLOOR. One was added here so the trees would read as
    // standing on something, and it moved the fixed tree from 1.19 m/s to
    // 6.65 and its drift from 0.34 m to 2.14. The trees rest on the turtle;
    // a slab of 325 extra bodies underneath them is scenery, and scenery
    // participates in the physics because in this engine every particle is a
    // body. "Minimal scenes, or the test measures the scenery" is written into
    // three other test files in this repo and I broke it here within a minute
    // of writing it. The trees are the subject; nothing else belongs.

    Side left  {"BROKEN (leaf positions discarded)", -6.0f, true,  {}, {}, {}, {}, {}};
    Side right {"FIXED  (leaf positions honoured)",  +6.0f, false, {}, {}, {}, {}, {}};
    build(engine, left);
    build(engine, right);
    ps.flush_pending_particles();

    printf("\n  %-36s %8s %8s\n", "side", "leaves", "COINCIDENT at birth");
    printf("  %-36s %8zu %8d\n", left.name,  left.leaf_ids.size(),  left.coincident_at_birth);
    printf("  %-36s %8zu %8d\n", right.name, right.leaf_ids.size(), right.coincident_at_birth);

    // The A/B must actually differ, or this shows nothing and says so.
    if (left.coincident_at_birth <= right.coincident_at_birth) {
        printf("\n  *** THE TWO SIDES ARE NOT DIFFERENT. ***\n"
               "  The broken side was built with %d coincident leaf pairs and the fixed side\n"
               "  with %d. If the lever had engaged, the left number would be large and the\n"
               "  right one zero. There is nothing to look at and nothing to assert.\n",
               left.coincident_at_birth, right.coincident_at_birth);
        engine.shutdown();
        return false;
    }

    // Registered widgets. NOT draw_text: see the header.
    ui::Label* l_title = nullptr; ui::Label* l_body = nullptr;
    ui::Label* l_left = nullptr;  ui::Label* l_right = nullptr; ui::Label* l_hint = nullptr;
    if (auto* uis = engine.get_ui_system()) {
        auto mk = [&](int y, int size_y, uint8_t r, uint8_t g, uint8_t b) {
            auto* L = new ui::Label("", "");
            L->set_position(24, y); L->set_size(1400, size_y); L->set_color(r, g, b);
            uis->add_widget(L);
            return L;
        };
        l_title = mk(24,  26, 255, 255, 255);
        l_body  = mk(56,  22, 190, 200, 210);
        l_left  = mk(104, 22, 255,  90,  80);   // red: the broken side
        l_right = mk(132, 22,  90, 230, 120);   // green: the fixed side
        l_hint  = mk(170, 20, 140, 145, 150);
    }

    int  stage = 0;
    int  frames_in_stage = 0;
    bool quit = false;
    const int AUTO_ADVANCE[3] = {90, 300, 120};   // headless pacing

    while (!quit) {
        // Stage 1 is frozen on purpose: it is the whole point that a broken
        // tree looks perfectly fine until physics touches it.
        //
        // FREEZE WITH dt = 0, NEVER BY SKIPPING update(). engine.update() is
        // also what polls window events and reads the keyboard. Skipping it
        // leaves the window unpumped, which on macOS means it never paints, and
        // leaves input unread, which means SPACE does nothing. The first
        // interactive run of this test did exactly that and showed a blank
        // window that would not respond: not one bug but one omission wearing
        // two faces.
        engine.update(stage > 0 ? (1.0 / 60.0) : 0.0);
        sample(engine, left);
        sample(engine, right);

        char buf[512];
        if (l_title) l_title->set_text(STAGE_TITLE[stage]);
        if (l_body)  l_body->set_text(STAGE_BODY[stage]);
        if (l_left) {
            snprintf(buf, sizeof(buf),
                     "LEFT  BROKEN   coincident at birth %3d   peak %5.2f m/s   drift mean %5.2f m  max %5.2f m",
                     left.coincident_at_birth, left.peak_speed, left.mean_drift, left.max_drift);
            l_left->set_text(buf);
        }
        if (l_right) {
            snprintf(buf, sizeof(buf),
                     "RIGHT FIXED    coincident at birth %3d   peak %5.2f m/s   drift mean %5.2f m  max %5.2f m",
                     right.coincident_at_birth, right.peak_speed, right.mean_drift, right.max_drift);
            l_right->set_text(buf);
        }
        if (l_hint) {
            snprintf(buf, sizeof(buf), "limits: peak <= %.2f m/s, mean drift <= %.2f m    %s",
                     MAX_SPEED_MS, MAX_MEAN_DRIFT,
                     interactive ? "SPACE: next stage    ESC: quit" : "");
            l_hint->set_text(buf);
        }

        if (interactive) {
            engine.render();
            engine.present();
            const auto& input = engine.get_input_system();
            static bool space_was = false;
            const bool space = input.get_input_state().keys[GLFW_KEY_SPACE];
            if (space && !space_was) { if (++stage > 2) stage = 2; frames_in_stage = 0; }
            space_was = space;
            if (input.get_input_state().keys[GLFW_KEY_ESCAPE]) quit = true;
            if (!engine.is_running()) quit = true;   // window closed
        } else {
            engine.render();
            engine.present();
            if (++frames_in_stage >= AUTO_ADVANCE[stage]) {
                frames_in_stage = 0;
                if (++stage > 2) quit = true;
            }
        }
        if (!interactive && stage > 2) quit = true;
    }

    // ARE THE TREES ACTUALLY IN FRAME? "It is drawing" and "you can see it" are
    // different claims, and the first version of this test proved it: 1,860
    // surfaces rendered for 44,000 frames into a window the user described as
    // empty, because the camera had never been aimed. This counts lit pixels in
    // the left and right halves of the frame. If either half is bare, the view
    // is wrong and no amount of correct physics underneath will show.
    {
        engine.get_renderer().wait_for_completion();
        const int W = engine.get_render_buffer().width();
        const int H = engine.get_render_buffer().height();
        std::vector<uint32_t> px((size_t)W * H, 0u);
        int w = W, h = H;
        long lit_left = 0, lit_right = 0;
        if (engine.read_latest_framebuffer(px.data(), w, h)) {
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const uint32_t c = px[(size_t)y * w + x];
                    const int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
                    if (r + g + b > 60) { (x < w / 2 ? lit_left : lit_right)++; }
                }
        }
        const long need = (long)w * h / 400;   // 0.25% of the frame per half
        printf("\n  %-36s %10ld  %-36s %10ld   (each needs > %ld)\n",
               "lit pixels, LEFT half", lit_left, "lit pixels, RIGHT half", lit_right, need);
        if (lit_left < need || lit_right < need) {
            printf("\n  *** NOTHING TO SEE. ***\n"
                   "  One half of the frame is empty, so a tree is out of view. The camera is\n"
                   "  aimed wrong, or a tree failed to generate. Fix the view before reading\n"
                   "  any of the numbers below: they describe bodies nobody can look at.\n");
            printf("\n  FAIL\n");
            engine.shutdown();
            return false;
        }
    }

    printf("\n  %-36s %9s %9s %9s\n", "side", "peak m/s", "mean m", "max m");
    printf("  %-36s %9.4f %9.4f %9.4f\n", left.name,  left.peak_speed,  left.mean_drift,  left.max_drift);
    printf("  %-36s %9.4f %9.4f %9.4f\n", right.name, right.peak_speed, right.mean_drift, right.max_drift);

    const bool broken_shoots = left.peak_speed  > MAX_SPEED_MS || left.mean_drift  > MAX_MEAN_DRIFT;
    const bool fixed_holds   = right.peak_speed <= MAX_SPEED_MS && right.mean_drift <= MAX_MEAN_DRIFT;

    printf("\n");
    printf("  broken side exceeds the limits : %s\n", broken_shoots ? "yes, as it must" : "NO, so this shows nothing");
    printf("  fixed side stays inside them   : %s\n", fixed_holds   ? "yes" : "NO, the fix has regressed");

    const bool pass = broken_shoots && fixed_holds;
    if (!pass && !broken_shoots) {
        printf("\n  The broken side did NOT misbehave, so the comparison is empty. Either the\n"
               "  legacy lever stopped working or the bug has been fixed somewhere else too.\n");
    }
    printf("\n  %s\n", pass ? "PASS" : "FAIL");
    engine.shutdown();
    return pass;
}
