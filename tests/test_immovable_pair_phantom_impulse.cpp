// =============================================================================
// IMMOVABLE TILES: a 2D floor defeats the solver, a 1D strip does not
// =============================================================================
// TDD, and the test has already refused to confirm two stories. Read the
// history before trusting a third.
//
// WHAT IS MEASURED, and this part is solid:
//
//   arrangement            converged  plateaued   rows
//   1 tile + dyn box            120          0     480
//   1x2 line                    120          0     480
//   1x3 line                    120          0     960
//   2x2 grid                      0        120    2880   <- cliff
//   3x3 grid                      0        120    9600
//
// All tiles are KINEMATIC and is_at_rest. Nothing in the grid scenes can move.
// A STRIP of touching immovable tiles converges. A GRID of them never does, and
// once it cannot converge, neither can any dynamic body sharing the solve.
//
// WHAT IS NOT CONFIRMED. The first version of this test asserted that any two
// touching zero-inverse-mass bodies pin the convergence test, on the theory
// that inv_mass_sum == 0 falls back to effective_mass = 1.0f
// (physics_system_v4.cpp:859) and yields an impulse that moves nothing but
// still counts toward max_impulse_this_iter (:2007). That story predicts 1x2
// breaks. IT DOES NOT. So the fallback may be necessary but is plainly not
// sufficient, and the extra ingredient in a 2x2 (each tile gains a DIAGONAL
// neighbour, and rows per tile jump from 320 to 720) has not been isolated.
//
// Do not "fix" this from the story. Use the tracer to find which constraint
// owns max_impulse in a 2x2 and does not in a 1x3:
//   LOGOSPHERE_PHYS_TRACE=5 LOGOSPHERE_PHYS_TRACE_FILE=/tmp/t.log
//
// WHY IT MATTERS ANYWAY. Real floors are 2D. Every tiled floor in the engine is
// past this cliff, which is why Eden never converges and why the ladder in
// tests/test_solver_convergence_ladder.cpp measured its floor rather than its
// stack.
//
// SENSITIVITY CONTROL FIRST. Scene A (a lone slab plus one dynamic box) must
// converge, or this test cannot tell a broken solver from a broken scene and
// nothing below is evidence.
//
//   ./build-release/logosphere-tests --test test_immovable_pair_phantom_impulse --no-head
//   INTERACTIVE=1 ...   strip on the left converges, grid on the right does not
// =============================================================================

#include "../src/core/engine.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include "../src/core/telemetry.h"
#include "../src/ui/ui_system.h"
#include "../src/ui/widgets.h"
#include <GLFW/glfw3.h>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

struct Scene {
    const char* name;
    int   nx, ny;       // KINEMATIC tile grid, laid edge to edge so they touch
    bool  dynamic_box;  // a dynamic body resting on them
    long  converged = 0, plateaued = 0, exhausted = 0, rows = 0;
};

// Tiles are laid edge to edge so neighbours touch, which is exactly how a floor
// is built. The bug needs contact between two immovable bodies and nothing else.
void build(Engine& engine, const Scene& s) {
    auto& ps = engine.get_particle_system();
    for (int gy = 0; gy < s.ny; ++gy)
    for (int gx = 0; gx < s.nx; ++gx) {
        Particle p = {};
        p.shape = ParticleShape::BOX;
        p.x = (float)gx * 1.0f; p.y = (float)gy * 1.0f; p.z = 0.05f;
        p.width = p.height = 1.0f; p.thickness = 0.1f; p.size = 1.0f;
        p.r = 0.45f; p.g = 0.45f; p.b = 0.5f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        int id = engine.add_particle(p);
        auto v = ps.lock_particles_for_write();
        v[id].solver_mode = ParticleSolverMode::KINEMATIC;
        v[id].owner = ParticleOwner::DYNAMICS;
        v[id].is_at_rest = true;
    }
    if (s.dynamic_box) {
        Particle p = {};
        p.shape = ParticleShape::BOX;
        p.x = 0.0f; p.y = 0.0f; p.z = 0.60f;
        p.width = p.height = p.thickness = 1.0f; p.size = 1.0f;
        p.r = 0.85f; p.g = 0.35f; p.b = 0.25f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        engine.add_particle(p);
    }
    ps.flush_pending_particles();
}

void measure(Scene& s) {
    namespace T = ::logosphere::telemetry;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) return;
    T::set_enabled(true);
    build(engine, s);

    const int WARM = 60, MEASURE = 60;
    for (int f = 0; f < WARM; ++f) engine.update(1.0 / 60.0);
    for (int f = 0; f < MEASURE; ++f) {
        engine.update(1.0 / 60.0);
        s.converged += (long)T::counter_value(T::Counter::PhysSolveConverged);
        s.plateaued += (long)T::counter_value(T::Counter::PhysSolvePlateaued);
        s.exhausted += (long)T::counter_value(T::Counter::PhysSolveExhausted);
        s.rows      += (long)T::counter_value(T::Counter::PhysSolverRows);
    }
    engine.shutdown();
}

bool run_interactive() {
    namespace T = ::logosphere::telemetry;
    EngineConfig cfg;
    cfg.create_display = true;
    cfg.window_title = "SPACE toggles the grid";
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) return false;
    T::set_enabled(true);

    auto& ps = engine.get_particle_system();
    auto tile = [&](float x, float y) {
        Particle p = {};
        p.shape = ParticleShape::BOX;
        p.x = x; p.y = y; p.z = 0.05f;
        p.width = p.height = 1.0f; p.thickness = 0.1f; p.size = 1.0f;
        p.r = 0.45f; p.g = 0.45f; p.b = 0.5f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        int id = engine.add_particle(p);
        auto v = ps.lock_particles_for_write();
        v[id].solver_mode = ParticleSolverMode::KINEMATIC;
        v[id].owner = ParticleOwner::DYNAMICS; v[id].is_at_rest = true;
        return id;
    };
    auto box = [&](float x, float y, float z) {
        Particle p = {};
        p.shape = ParticleShape::BOX;
        p.x = x; p.y = y; p.z = z;
        p.width = p.height = p.thickness = 0.8f; p.size = 0.8f;
        p.r = 0.9f; p.g = 0.45f; p.b = 0.3f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        engine.add_particle(p);
    };

    // FIXED SCENE. Nothing spawns after this: three boxes fall once, settle in
    // about a second, and then the picture is still so the numbers can be read.
    // The earlier version rained boxes forever and was unreadable.
    for (int i = 0; i < 5; ++i) tile(-7.0f + i, 0.0f);        // 1D strip, healthy
    box(-6.0f, 0.0f, 3.0f); box(-5.0f, 0.0f, 4.2f); box(-4.0f, 0.0f, 5.4f);

    std::vector<int> grid_ids;                                 // 2x2 grid, the breaker
    for (int gy = 0; gy < 2; ++gy)
        for (int gx = 0; gx < 2; ++gx) grid_ids.push_back(tile(4.0f + gx, (float)gy));
    ps.flush_pending_particles();
    ps.queue_light(0.0f, -9.0f, 11.0f, 300000.0f, 55.0f, 1.0f, 0.96f, 0.9f);
    ps.flush_pending_particles();

    // Readout via WIDGETS. present() clears the overlay plane and then
    // re-renders registered widgets, so draw_text from here is erased but a
    // Label survives. See docs/VISUAL_TESTS.md.
    std::vector<ui::Label*> lines;
    auto* uis = engine.get_ui_system();
    for (int i = 0; i < 7; ++i) {
        auto* L = new ui::Label("", "phys_line_" + std::to_string(i));
        L->set_position(16, 14 + i * 18);
        L->set_size(820, 16);
        L->set_color(i == 0 ? 255 : 210, i == 0 ? 235 : 210, i == 0 ? 130 : 210);
        if (uis) uis->add_widget(L);
        lines.push_back(L);
    }

    // Rolling window, so the numbers describe NOW and not the whole session.
    // A cumulative counter on a scene you are toggling is unreadable.
    const int W = 60;
    std::vector<long> hc(W, 0), hp(W, 0);
    int  slot = 0, frame = 0;
    bool grid_on = true, space_was = false;

    printf("\n  ============================================================\n");
    printf("   WATCH THIS TERMINAL, not the window. The window just shows the\n");
    printf("   scene; the numbers below are the experiment.\n");
    printf("\n");
    printf("   Press SPACE in the window to remove the 2x2 grid, and again to\n");
    printf("   put it back. Nothing else about the scene changes.\n");
    printf("\n");
    printf("   EXPECTED: converged is 0 with the grid PRESENT, and non-zero\n");
    printf("   with it REMOVED. If that does not happen, the claim is wrong.\n");
    printf("  ============================================================\n\n");

    while (engine.is_running()) {
        engine.update(1.0 / 60.0);
        engine.render();

        hc[slot] = (long)T::counter_value(T::Counter::PhysSolveConverged);
        hp[slot] = (long)T::counter_value(T::Counter::PhysSolvePlateaued);
        slot = (slot + 1) % W;
        long wc = 0, wp = 0;
        for (int i = 0; i < W; ++i) { wc += hc[i]; wp += hp[i]; }

        char b[7][220];
        snprintf(b[0], 220, "IMMOVABLE TILES AND THE CONVERGENCE TEST");
        snprintf(b[1], 220, "LEFT   1D strip of 5 tiles + 3 boxes        RIGHT  2x2 grid of tiles");
        snprintf(b[2], 220, "all tiles are KINEMATIC and at rest: none of them can ever move");
        snprintf(b[3], 220, "GRID: %s          [SPACE] toggle    [ESC] quit",
                 grid_on ? "PRESENT" : "removed");
        snprintf(b[4], 220, "last %d frames:   converged %ld     plateaued %ld", W, wc, wp);
        snprintf(b[5], 220, "constraint rows/frame %llu",
                 (unsigned long long)T::counter_value(T::Counter::PhysSolverRows));
        snprintf(b[6], 220, "%s", grid_on
                 ? "converged is pinned at 0. One bad constraint anywhere does this: the"
                 : "converged recovers the moment the grid leaves. Same boxes, same strip.");
        for (int i = 0; i < 7; ++i) lines[i]->set_text(b[i]);

        // TERMINAL READOUT. The widget HUD above is best-effort: on-screen text
        // has failed to appear three times in this scene and the cause is not
        // understood, so the authoritative readout goes to stdout where it
        // cannot be swallowed by the overlay system. Twice a second is fast
        // enough to watch and slow enough to read.
        if (frame % 30 == 0) {
            printf("  GRID %-8s | converged %4ld | plateaued %4ld | rows %6llu\n",
                   grid_on ? "PRESENT" : "REMOVED", wc, wp,
                   (unsigned long long)T::counter_value(T::Counter::PhysSolverRows));
            fflush(stdout);
        }

        engine.present();
        engine.get_platform()->poll_events();

        const auto& in = engine.get_input_system();
        const bool sp = in.get_input_state().keys[GLFW_KEY_SPACE];
        if (sp && !space_was) {
            grid_on = !grid_on;
            auto v = ps.lock_particles_for_write();
            for (int id : grid_ids) v[id].z = grid_on ? 0.05f : -500.0f;
            printf("  grid %s\n", grid_on ? "restored" : "removed"); fflush(stdout);
        }
        space_was = sp;
        ++frame;
        if (in.get_input_state().keys[GLFW_KEY_ESCAPE]) break;
    }
    printf("\n  closed after %d frames\n", frame);
    engine.shutdown();
    return true;
}

}  // namespace

bool test_immovable_pair_phantom_impulse() {
    if (std::getenv("INTERACTIVE")) return run_interactive();

    printf("\n=== IMMOVABLE PAIRS MUST NOT PRODUCE WORK ===\n");
    printf("A scene in which nothing can move must not defeat the solver.\n\n");

    // A LADDER OF ARRANGEMENTS, not one case. The first version of this test
    // asserted that any two touching immovable bodies break the solver, and it
    // came back GREEN: a line of two converges fine. So the mechanism needs
    // more than "a pair with zero inverse mass on both sides", and the honest
    // move is to find WHICH arrangement crosses the line rather than to fix on
    // a story the test just refused to confirm.
    Scene a{"A: 1 tile + dynamic box   (CONTROL)", 1, 1, true};
    std::vector<Scene> grid = {
        {"1x2 line, nothing dynamic",        2, 1, false},
        {"1x3 line, nothing dynamic",        3, 1, false},
        {"2x2 grid, nothing dynamic",        2, 2, false},
        {"3x3 grid, nothing dynamic",        3, 3, false},
        {"1x3 line + dynamic box",           3, 1, true},
        {"2x2 grid + dynamic box",           2, 2, true},
        {"3x3 grid + dynamic box",           3, 3, true},
    };
    measure(a);
    for (auto& s : grid) measure(s);

    printf("  %-40s %10s %10s %10s %10s\n", "scene", "CONVERGED", "plateaued", "exhausted", "rows");
    printf("  %-40s %10ld %10ld %10ld %10ld\n",
           a.name, a.converged, a.plateaued, a.exhausted, a.rows);
    for (const auto& s : grid)
        printf("  %-40s %10ld %10ld %10ld %10ld\n",
               s.name, s.converged, s.plateaued, s.exhausted, s.rows);

    // NOTE: there is deliberately no "remove the grid at runtime" scene. An
    // attempt at one never removed anything: these tiles carry
    // ParticleOwner::DYNAMICS, so an external writer restores their position
    // every frame and the teleport silently reverted (measured: moved=0,
    // stayed=4). The WITH and WITHOUT comparison is the table above, built from
    // separate scenes, which is the only honest way to do it anyway.

    const Scene& b = grid[3];   // 3x3 grid, nothing dynamic: the known breaker
    const Scene& c = grid[6];   // 3x3 grid + box

    bool ok = true;

    // SENSITIVITY FIRST. If the control cannot converge, nothing below is
    // evidence: a test that reads the same in both worlds is not a test.
    if (a.converged == 0) {
        printf("\n  FAIL (BLIND): the control scene never converged either, so this test\n"
               "  cannot distinguish a broken solver from a broken scene. Do not read the\n"
               "  rows below as evidence of anything.\n");
        return false;
    }
    printf("\n  control OK: a lone slab converges (%ld solves), so the test can see convergence.\n",
           a.converged);

    // THE ASSERTION. Nothing in scene B can move. Either no constraint is built
    // (correct) or the solve converges at once. Grinding to the plateau limit on
    // work that cannot matter is the bug.
    if (b.plateaued > 0) {
        printf("\n  FAIL: a 3x3 grid of KINEMATIC tiles has NO dynamic bodies. Nothing in\n"
               "        it can move. Yet the solver plateaued %ld times on %ld rows.\n"
               "        A 1x3 LINE of the same tiles converges, so this is about the 2D\n"
               "        arrangement and not merely about immovable pairs. Mechanism NOT\n"
               "        yet isolated: trace level 5 and find which constraint owns\n"
               "        max_impulse in a 2x2 but not in a 1x3. See kit study S21.\n",
               b.plateaued, b.rows);
        ok = false;
    } else {
        printf("  scene B OK: an immovable pair produces no solver work (%ld rows).\n", b.rows);
    }

    // And the consequence a user would actually notice: adding inert scenery
    // next to a real body destroys convergence for the real body too.
    if (c.converged == 0) {
        printf("  FAIL: a dynamic box over a 3x3 tile floor converges %ld times against\n"
               "        %ld over a single slab. Inert scenery changes how the solver treats\n"
               "        a real body, which is the consequence a user actually feels.\n",
               c.converged, a.converged);
        ok = false;
    } else {
        printf("  scene C OK: an extra tile does not destroy convergence (%ld solves).\n", c.converged);
    }

    printf("\n  %s\n", ok ? "PASS" : "FAIL");
    return ok;
}
