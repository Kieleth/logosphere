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
#include <GLFW/glfw3.h>
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

void measure(Scene& s, bool interactive_engine_unused = false) {
    (void)interactive_engine_unused;
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
    cfg.window_title = "Immovable pair: phantom impulse";
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) return false;
    T::set_enabled(true);

    // All four scenes at once, spread along X so the difference is spatial and
    // you can see which arrangement kills convergence. Group 1 is a lone slab
    // (healthy), the others have neighbours touching (the bug).
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
    };
    auto box = [&](float x, float y, float z) {
        Particle p = {};
        p.shape = ParticleShape::BOX;
        p.x = x; p.y = y; p.z = z;
        p.width = p.height = p.thickness = 1.0f; p.size = 1.0f;
        p.r = 0.85f; p.g = 0.35f; p.b = 0.25f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        engine.add_particle(p);
    };

    // LEFT: a 1D strip of 5 touching tiles. Converges.
    // RIGHT: a 2x2 grid. Does not, and never will.
    // Same bodies, same contacts per pair, same immovability. Only the
    // arrangement differs, and that is the whole finding.
    for (int i = 0; i < 5; ++i) tile(-8.0f + i, 0.0f);
    box(-6.0f, 0.0f, 0.60f);
    for (int gy = 0; gy < 2; ++gy)
        for (int gx = 0; gx < 2; ++gx) tile(4.0f + gx, (float)gy);
    box(4.0f, 0.0f, 0.60f);
    ps.flush_pending_particles();

    ps.queue_light(0.0f, -8.0f, 10.0f, 260000.0f, 50.0f, 1.0f, 0.96f, 0.9f);
    ps.flush_pending_particles();

    printf("\n  INTERACTIVE.\n");
    printf("  LEFT  a 1D STRIP of 5 touching immovable tiles, plus a box.\n");
    printf("  RIGHT a 2x2 GRID of the same tiles, plus a box.\n");
    printf("  Identical bodies, identical immovability. Only the arrangement differs.\n");
    printf("  With the grid present the solver never converges, so the title shows\n");
    printf("  converged pinned at 0. Delete the grid and it recovers.\n");
    printf("  ESC exits.\n\n");

    long conv = 0, plat = 0;
    while (engine.is_running()) {
        engine.update(1.0 / 60.0);
        engine.render();
        conv += (long)T::counter_value(T::Counter::PhysSolveConverged);
        plat += (long)T::counter_value(T::Counter::PhysSolvePlateaued);

        // Window title, not draw_text: present() clears the overlay plane, so
        // immediate-mode text drawn here is silently erased (docs/VISUAL_TESTS.md).
        if (GLFWwindow* win = (GLFWwindow*)engine.get_window_handle()) {
            char t[220];
            snprintf(t, sizeof(t),
                     "converged %ld  |  plateaued %ld  |  rows/frame %llu  |  ESC exits",
                     conv, plat,
                     (unsigned long long)T::counter_value(T::Counter::PhysSolverRows));
            glfwSetWindowTitle(win, t);
        }
        engine.present();
        // Without this, input is stale and fires phantom keys (same doc).
        engine.get_platform()->poll_events();
        if (engine.get_input_system().get_input_state().keys[GLFW_KEY_ESCAPE]) break;
    }
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
