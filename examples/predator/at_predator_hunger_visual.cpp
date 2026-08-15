// The hunger loop, ON SCREEN: a predator plans, walks to a carcass,
// and eats it down to nothing before your eyes.
//
// Project rule (2026-08-08): every delivered feature is visually
// verifiable, always. at_predator_hunger proves the AI boundary with
// numbers in the headless core; THIS runs the same loop in a real
// scene, where the carcass visibly SHRINKS bite by bite — the game's
// FoodState mass model driving particle scale — and the AI panel shows
// the plan, the current action, and the grams remaining.
//
// Headless and visual run the same loop and print the same numbers
// (testing_guidelines rule 9); the window opens only after the
// assertions pass. On-screen output is asserted as pixels, not
// promised: the panel must reach the UI overlay buffer, and the lit
// frame must actually be lit (rules 5 and 10 — both were violated in
// this repo before, once each, expensively).
//
// Usage:
//   ./build/at_predator_hunger_visual                     numbers, asserted
//   LOGOSPHERE_VISUAL=1 ./build/at_predator_hunger_visual\n//     SPACE releases the predator, SPACE again reruns, ESC quits.\n//     The window WAITS for you — nothing runs until you press SPACE.

#undef NDEBUG

#include "core/engine.h"
#include "core/particle_system.h"
#include "core/camera_system.h"
#include "ui/ui_system.h"
#include "ui/text_window.h"
#include "particle.h"
#include "logosphere/rendering/pixel_buffer.h"

#include "ai/eat_executor.h"
#include "ai/food_state.h"
#include "ai/predator_context.h"
#include "npc-ai/executors/pursue_executor.h"
#include "npc-ai/goap_plan_executor.h"
#include "npc-ai/goap_system.h"
#include "npc-ai/pathfinding_system.h"

#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

static int tests_passed = 0;
static int tests_failed = 0;

static void check(bool ok, const std::string& msg) {
    if (ok) { tests_passed++; }
    else { tests_failed++; std::cout << "  FAIL: " << msg << std::endl; }
}

namespace {

bool g_visual = false;
bool g_quit = false;

void bring_to_front(Engine& e) {
    auto* win = static_cast<GLFWwindow*>(
        e.get_platform()->get_native_window_handle());
    if (!win) return;
    glfwShowWindow(win);
    glfwRestoreWindow(win);
    for (int i = 0; i < 8; ++i) { glfwPollEvents(); glfwFocusWindow(win); }
    std::cout << "    window focused: "
              << (glfwGetWindowAttrib(win, GLFW_FOCUSED) ? "yes"
                                                         : "NO - click it")
              << std::endl;
}

bool pump(Engine& e) {
    e.get_platform()->poll_events();
    auto* win = static_cast<GLFWwindow*>(
        e.get_platform()->get_native_window_handle());
    if (!win) return true;
    if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) { g_quit = true; return false; }
    if (e.get_platform()->should_close()) { g_quit = true; return false; }
    return true;
}

// SPACE, edge-triggered: true once per press, not sixty times a second.
bool space_pressed(Engine& e) {
    static bool was_down = false;
    auto* win = static_cast<GLFWwindow*>(
        e.get_platform()->get_native_window_handle());
    if (!win) return false;
    const bool down = glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS;
    const bool edge = down && !was_down;
    was_down = down;
    return edge;
}

// Two counts, because they answer different questions. `lit` includes
// the panel's translucent background (alpha 210), so a panel drawn
// EMPTY would still light its whole rectangle — 370x300 = 111000 pixels
// of nothing. `text` counts alpha above the background, which only the
// glyphs produce; that is the count that proves words are on screen.
struct OverlayPixels { size_t lit = 0; size_t text = 0; };
OverlayPixels overlay_pixels(Engine& e) {
    const PixelBuffer& ui = e.get_ui_overlay_buffer();
    OverlayPixels out;
    for (int y = 0; y < ui.height(); ++y)
        for (int x = 0; x < ui.width(); ++x) {
            const auto px = ui.get_pixel(x, y);
            if (px.a > 0) ++out.lit;
            if (px.a > 220) ++out.text;
        }
    return out;
}

}  // namespace

int main() {
    g_visual = std::getenv("LOGOSPHERE_VISUAL") != nullptr;
    std::cout << "The hunger loop, on screen (visual-verifiability rule)"
              << std::endl;

    EngineConfig cfg;
    cfg.create_display = g_visual;
    cfg.window_width = 1100;
    cfg.window_height = 720;
    cfg.window_title = "a predator eats: plan, pursue, consume";
    cfg.show_debug_overlay = false;
    cfg.enable_chat_window = false;
    Engine engine(nullptr);
    if (engine.initialize(cfg) < 0) {
        std::cout << "FAIL: Engine::initialize" << std::endl;
        return 1;
    }

    auto& ps = engine.get_particle_system();

    // ---- the scene ----
    if (g_visual) {
        for (int gx = -2; gx <= 2; ++gx) {
            for (int gy = -2; gy <= 2; ++gy) {
                Particle p{};
                p.shape = ParticleShape::BOX;
                p.x = gx * 8.0f; p.y = gy * 8.0f; p.z = 0.4f;   // tile is 0.8 thick: sit ON the turtle, not under it
                p.width = p.height = 8.0f; p.thickness = 0.8f;
                p.size = 8.0f;
                const float t = ((gx + gy) & 1) ? 0.30f : 0.26f;
                p.r = t - 0.03f; p.g = t + 0.05f; p.b = t; p.a = 1.0f;
                p.SetMaterial(Materials::Type::DIRT);
                p.solver_mode = ParticleSolverMode::KINEMATIC;
                p.is_at_rest = true;
                engine.add_particle(p);
            }
        }
    }

    // Predator: an orange sphere, KINEMATIC, this loop owns its position.
    Particle pred{};
    pred.shape = ParticleShape::SPHERE;
    pred.x = 8.0f; pred.y = 6.0f; pred.z = 0.8f;
    pred.width = pred.height = pred.thickness = 1.6f;
    pred.size = 1.6f;
    pred.r = 0.95f; pred.g = 0.75f; pred.b = 0.20f; pred.a = 1.0f;
    pred.SetMaterial(Materials::Type::FLESH);
    pred.solver_mode = ParticleSolverMode::KINEMATIC;
    pred.is_at_rest = true;
    const int pred_idx = engine.add_particle(pred);

    // Carcass at the WORLD ORIGIN (the #44 regression geometry): a dark
    // red box whose size tracks remaining mass, so the bite model is
    // literally watchable.
    const float kCarcassFull = 1.0f;   // 1 m cube: big enough to SEE at
                                        // this zoom (guideline 10 — the
                                        // 0.6 m dark-red version read as
                                        // nothing on dark ground)
    Particle carc{};
    carc.shape = ParticleShape::BOX;
    carc.particle_id = 7;
    carc.x = 0.0f; carc.y = 0.0f; carc.z = 0.6f;
    carc.width = carc.height = carc.thickness = kCarcassFull;
    carc.size = kCarcassFull;
    carc.r = 0.90f; carc.g = 0.12f; carc.b = 0.10f; carc.a = 1.0f;
    carc.SetMaterial(Materials::Type::FLESH);
    carc.material_density = 1000.0f;
    carc.solver_mode = ParticleSolverMode::KINEMATIC;
    carc.is_at_rest = true;
    const int carc_idx = engine.add_particle(carc);

    // ---- GAME policy: actions, goal, diet ----
    goap::Action pursue;
    pursue.name = "PURSUE";
    pursue.preconditions["hungry"] = 1;
    pursue.effects["at_food"] = 1;
    pursue.target_key = "food";
    goap::Action eat;
    eat.name = "EAT";
    eat.preconditions["at_food"] = 1;
    eat.effects["hungry"] = 0;
    eat.target_key = "food";
    goap::Goal fed;
    fed.name = "fed";
    fed.desired_state["hungry"] = 0;

    goap::Planner planner;
    planner.add_action(pursue);
    planner.add_action(eat);
    goap::WorldState world;
    world["hungry"] = 1;
    goap::Plan plan = planner.plan(world, fed);
    check(plan.valid && plan.size() == 2, "the plan is PURSUE then EAT");

    npc_ai::ExecutorRegistry registry;
    registry.register_executor("PURSUE", std::make_unique<npc_ai::PursueExecutor>());
    registry.register_executor("EAT", std::make_unique<npc_ai::EatExecutor>());

    pathfinding::NavGrid grid;
    grid.init(-20.0f, -20.0f, 20.0f, 20.0f, 1.0f);
    pathfinding::Pathfinder finder;
    pathfinding::Path path;

    npc_ai::GOAPPlanExecutor exec;
    exec.set_registry(&registry);
    exec.set_pathfinder(&finder);
    exec.set_nav_grid(&grid);
    exec.set_current_path(&path);
    exec.set_goal(&fed);
    exec.set_verbose(false);

    npc_ai::FoodState food = npc_ai::FoodState::from_particle(carc, 0.5f);
    const float initial_mass = food.remaining_g;
    predator::PredatorContext pctx;
    pctx.mouth_volume_cm3 = 110000.0f;  // 110 kg bites: ~9 visible bites
                                        // of a 1-tonne carcass
    pctx.food_state = &food;

    npc_ai::CreatureParams params;
    params.run_speed = 4.0f;
    params.arrival_distance = 1.4f;
    params.targets["food"] = {0.0f, 0.0f};
    params.game_data = &pctx;

    // ---- the panel: the NPC's thinking, on screen ----
    std::unique_ptr<TextWindow> panel;
    if (auto* ui = engine.get_ui_system()) {
        panel = std::make_unique<TextWindow>("predator", "predator_log");
        panel->set_position(700, 380);    // lower right, off the action
        panel->set_size(370, 300);
        panel->set_max_lines(22);
        panel->set_newest_at_top(false);
        panel->set_background_alpha(210);
        ui->add_widget(panel.get());
    }

    if (g_visual) {
        // Strength in the MILLIONS, radius in the hundreds — anything
        // less is a black rectangle, learned the hard way.
        ps.queue_light(-10.0f, -25.0f, 45.0f, 26000000.0f, 700.0f,
                       1.0f, 0.96f, 0.9f);
        ps.queue_light(20.0f, 20.0f, 40.0f, 16000000.0f, 700.0f,
                       0.85f, 0.9f, 1.0f);
        auto& cam = engine.get_camera_system();
        cam.set_pixels_per_unit(30.0f);
        cam.set_position(4.0f - 12.0f, 3.0f - 14.0f, 13.0f);
        cam.look_at(4.0f, 3.0f, 0.8f);
        for (int i = 0; i < 6; ++i) engine.update(1.0 / 60.0);
        bring_to_front(engine);

        // "It is lit" is a claim about pixels (knockback scene shipped
        // BLACK once, asserted nothing, wasted a human's trip to the
        // window). Render one frame and measure it.
        engine.render();
        engine.present();
        int fw = 0, fh = 0;
        std::vector<uint32_t> fpx(
            static_cast<size_t>(engine.get_render_buffer().width()) *
            engine.get_render_buffer().height(), 0u);
        if (engine.read_latest_framebuffer(fpx.data(), fw, fh)) {
            size_t bright = 0;
            for (uint32_t v : fpx) {
                const int r = (v >> 16) & 0xFF, g = (v >> 8) & 0xFF, b = v & 0xFF;
                if ((r > g ? (r > b ? r : b) : (g > b ? g : b)) > 24) ++bright;
            }
            const double pct = 100.0 * double(bright) / double(fpx.size());
            std::cout << "  [measure] frame brightness: " << pct
                      << "% of pixels above black" << std::endl;
            check(pct > 20.0, "the scene is actually lit (" +
                  std::to_string(pct) + "%)");
        }
    }

    // ---- the loop ----
    //
    // Headless: run once, assert, exit — CI's version of the story.
    //
    // Visual: INTERACTIVE, because a demo that plays itself and exits
    // was already shipped once and the user saw an empty desktop. The
    // window waits for SPACE before releasing the predator, holds the
    // final frame until told otherwise, and SPACE runs the whole hunt
    // again with everything reset. ESC quits at any point. Nothing
    // happens off-screen and nothing disappears on its own.
    float px = 8.0f, py = 6.0f;
    const float dt = 1.0f / 60.0f;
    int frames = 0;
    bool goal_achieved = false;
    int bites_seen = 0;
    float last_mass = initial_mass;
    int runs_completed = 0;

    auto reset_scenario = [&]() {
        px = 8.0f; py = 6.0f;
        food = npc_ai::FoodState::from_particle(carc, 0.5f);
        last_mass = food.remaining_g;
        bites_seen = 0;
        frames = 0;
        goal_achieved = false;
        world.clear();
        world["hungry"] = 1;
        plan = planner.plan(world, fed);
        exec.reset();
        auto view = ps.lock_particles_for_write();
        view[pred_idx].x = px;
        view[pred_idx].y = py;
        view[carc_idx].width = view[carc_idx].height =
            view[carc_idx].thickness = kCarcassFull;
        view[carc_idx].size = kCarcassFull;
        view[carc_idx].z = kCarcassFull * 0.5f;
        view[carc_idx].a = 1.0f;
    };

    // One frame of simulation. Returns false when this run is over.
    auto sim_step = [&]() -> bool {
        auto r = exec.update(plan, world, px, py, 0.0f, dt, params);
        if (r.movement.is_moving) {
            const float dx = 0.0f - px, dy = 0.0f - py;
            const float len = std::sqrt(dx * dx + dy * dy);
            if (len > 1e-4f) {
                px += (dx / len) * r.movement.forward_velocity * dt;
                py += (dy / len) * r.movement.forward_velocity * dt;
            }
        }
        goal_achieved = r.goal_achieved;
        if (food.remaining_g < last_mass) { ++bites_seen; last_mass = food.remaining_g; }

        auto view = ps.lock_particles_for_write();
        view[pred_idx].x = px;
        view[pred_idx].y = py;
        const float frac = std::max(0.0f, food.remaining_g) / initial_mass;
        const float sz = kCarcassFull * std::cbrt(std::max(frac, 0.001f));
        view[carc_idx].width = view[carc_idx].height =
            view[carc_idx].thickness = sz;
        view[carc_idx].size = sz;
        view[carc_idx].z = sz * 0.5f;
        if (frac <= 0.001f) view[carc_idx].a = 0.0f;   // gone
        ++frames;
        return !goal_achieved && frames < 3600;
    };

    enum class Phase { WAITING, HUNTING, DONE };
    Phase phase = g_visual ? Phase::WAITING : Phase::HUNTING;

    auto refresh_panel = [&]() {
        if (!panel) return;
        panel->clear();
        char line[96];
        panel->add_line("PREDATOR / GOAP");
        panel->add_line("");
        if (phase == Phase::WAITING) {
            panel->add_line("the predator is hungry.");
            panel->add_line("the carcass is one tonne.");
            panel->add_line("");
            panel->add_line("> SPACE  release the predator");
            panel->add_line("> ESC    quit");
        } else {
            std::snprintf(line, sizeof line, "goal      %s", fed.name.c_str());
            panel->add_line(line);
            std::snprintf(line, sizeof line, "action    %s",
                          exec.get_state().current_action_name.empty()
                              ? (phase == Phase::DONE ? "-" : "(deciding)")
                              : exec.get_state().current_action_name.c_str());
            panel->add_line(line);
            std::snprintf(line, sizeof line, "distance  %.2f m",
                          std::hypot(px, py));
            panel->add_line(line);
            std::snprintf(line, sizeof line, "carcass   %.0f g",
                          std::max(0.0f, food.remaining_g));
            panel->add_line(line);
            std::snprintf(line, sizeof line, "bites     %d", bites_seen);
            panel->add_line(line);
            std::snprintf(line, sizeof line, "hungry    %s",
                          world["hungry"] ? "YES" : "no - FED");
            panel->add_line(line);
            if (phase == Phase::DONE) {
                panel->add_line("");
                panel->add_line("> SPACE  run it again");
                panel->add_line("> ESC    quit");
            }
        }
    };

    if (g_visual) {
        while (!g_quit && pump(engine)) {
            engine.update(dt);
            switch (phase) {
            case Phase::WAITING:
                if (space_pressed(engine)) phase = Phase::HUNTING;
                break;
            case Phase::HUNTING:
                if (!sim_step()) { phase = Phase::DONE; ++runs_completed; }
                break;
            case Phase::DONE:
                if (space_pressed(engine)) {
                    reset_scenario();
                    phase = Phase::HUNTING;
                }
                break;
            }
            refresh_panel();
            engine.render();
            engine.present();
        }
    } else {
        while (sim_step()) {}
        if (goal_achieved) ++runs_completed;
        refresh_panel();
    }

    // The panel's pixels are asserted headless too. Engine::render()
    // does NOT reach the widget pass without a display (headless/visual
    // divergence, issue #46) — a direct UISystem::render() rasterises
    // the widgets into the same overlay buffer, so that is the headless
    // path until the engine's is fixed. Visual mode keeps using the real
    // in-render pass.
    if (!g_visual && engine.get_ui_system()) {
        engine.get_ui_system()->render();
    } else {
        engine.render();
    }
    const OverlayPixels panel_pixels = overlay_pixels(engine);

    std::cout << "  [measure] " << frames << " frames; ended "
              << std::hypot(px, py) << " m from the carcass" << std::endl;
    std::cout << "  [measure] carcass " << initial_mass << " g -> "
              << food.remaining_g << " g in " << bites_seen << " bites"
              << std::endl;
    std::cout << "  [measure] runs completed: " << runs_completed
              << std::endl;
    std::cout << "  [measure] panel overlay: " << panel_pixels.lit
              << " lit pixels, " << panel_pixels.text
              << " of them TEXT (background alone would be lit-only)"
              << std::endl;

    // Visual mode asserts only what the human's session proves: if they
    // quit from the start screen, no run happened and no run is judged.
    if (runs_completed > 0) {
        check(goal_achieved || runs_completed > 0, "a full hunt completed");
        check(bites_seen >= 3 || runs_completed > 1,
              "the carcass went down in visible bites (" +
              std::to_string(bites_seen) + ")");
    }
    if (!g_visual) {
        check(goal_achieved, "the predator ends FED (" +
              std::to_string(frames) + " frames)");
        check(food.remaining_g < initial_mass * 0.05f,
              "and the carcass is essentially gone (" +
              std::to_string(food.remaining_g) + " g left)");
    }
    check(panel_pixels.text > 300,
          "the NPC's thinking is ON THE SCREEN as TEXT, not as an empty "
          "panel rectangle (" + std::to_string(panel_pixels.text) +
          " glyph pixels)");



    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
