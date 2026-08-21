// =============================================================================
// WHY A FLOOR MADE OF TILES BREAKS THE PHYSICS SOLVER
// =============================================================================
//
// ---------------------------------------------------------------------------
// PLAIN LANGUAGE FIRST. If you have never touched this engine, read this bit.
// ---------------------------------------------------------------------------
//
// WHAT A SOLVER DOES. Every frame, physics has to stop objects passing through
// each other. It looks at each pair that is touching, works out a push that
// would separate them, applies it, and then LOOKS AGAIN, because that push may
// have shoved something into something else. It repeats up to 32 times. Each
// repeat is an "iteration".
//
// WHEN DOES IT STOP? Two ways out:
//
//   "CONVERGED"  the pushes have become tiny. Everything is resting properly,
//                there is nothing left to fix. This is the happy ending.
//
//   "PLATEAUED"  the pushes are NOT tiny, but they stopped getting smaller.
//                The solver decides more repeats will not help and gives up.
//                This is the unhappy ending.
//
// The engine counts which ending it reaches. That is the whole experiment.
//
// WHAT WE FOUND. Take some floor tiles. Make them immovable (the engine calls
// this KINEMATIC: they are solid, but nothing can push them, like a wall).
// Nothing can move. There is no physics to do.
//
//   Lay them in a LINE:   the solver says CONVERGED. Correct, nothing to fix.
//   Lay them in a SQUARE: the solver says PLATEAUED, forever, every frame.
//
// Same tiles. Same immovability. Only the arrangement differs.
//
// WHY YOU SHOULD CARE. Real floors are squares of tiles, not lines. So every
// floor in this engine is in the broken case. And it is contagious: put a real
// falling box on a square floor and the box's physics stops converging too,
// because the solver's stopping test looks at ALL objects at once. Four inert
// tiles poison the answer for everything else in the world.
//
// The waste is real: about 26 constraints per floor tile, recomputed every
// frame, for tiles that cannot move. And it burns 8 solver iterations doing it.
//
// ---------------------------------------------------------------------------
// THE LAWS THIS ENFORCES (assert-protocol migration, 2026-08-21)
// ---------------------------------------------------------------------------
//
//   INV-23  no unsolvable row blocks the world. A row no finite impulse can
//           change (both endpoints immovable) contributes exactly zero and is
//           excluded from every stopping test and residual maximum. Scene B
//           IS this law: nine immovable tiles, nothing that can move, and the
//           solver must not grind. Scene C is its consequence — no solver exit
//           may be held hostage by a question with no answer, so inert
//           scenery must not change how a real body is treated.
//   INV-7   the momentum door: a zero-inverse-mass pair exchanges zero impulse.
//   INV-8   rows are live-sized: an effective mass never exceeds what the live
//           immovability predicate allows. The 2026-08-01 fix was exactly this
//           guard returning 0 instead of 1 for an immovable pair.
//   hygiene the control-converges check below guards the MEASUREMENT: a test
//           that reads the same in both worlds is not a test.
//
// ---------------------------------------------------------------------------
// THE MEASUREMENT
// ---------------------------------------------------------------------------
//
// FIXED 2026-08-01. This test was RED and is now a REGRESSION GUARD. The table
// below is what it looked like BEFORE the fix; today every row reads 120/0.
//
//   arrangement            converged  plateaued    rows
//   1 tile + falling box         120          0     480
//   1x2 line                     120          0     480
//   1x3 line                     120          0     960
//   2x2 square                     0        120    2880   <- broke here
//   3x3 square                     0        120    9600
//   3x3 square + falling box       0        120   11520   <- and it spread
//
// THE CAUSE, found with trace level 5. In a 2x2 the two DIAGONAL tiles touch at
// a corner and produce a contact row with bias 4. Both bodies are immovable, so
// inv_mass_sum is 0, and the effective-mass guard fell back to 1.0f, an
// arbitrary value picked to avoid dividing by zero. That yields impulse 4,
// applied to two bodies with zero inverse mass, so it moves nothing and is
// recomputed identically forever. The solver's stopping test is a GLOBAL max
// over every row, so that single row pinned the entire world.
//
// THE FIX is that guard returning 0.0f instead of 1.0f: infinite effective mass
// means no impulse can change anything, so the row contributes nothing. No new
// branch, no special case, just the physically correct value in a conditional
// that already existed.
//
// "rows" is how many separate push-calculations the solver was handed.
// "converged"/"plateaued" count how many times each ending was reached over 60
// frames. There are 4 solver runs per frame, so 120 = every single one.
//
// ---------------------------------------------------------------------------
// HOW TO RUN IT
// ---------------------------------------------------------------------------
//
//   ./build-release/logosphere-tests --test test_immovable_pair_phantom_impulse --no-head
//       Prints the table above. This is the real result.
//
//   PHANTOM_SCENE=line INTERACTIVE=1 ./build-release/logosphere-tests \
//       --test test_immovable_pair_phantom_impulse --no-head
//   PHANTOM_SCENE=square INTERACTIVE=1 ...
//       ONE arrangement per window, numbers printed to the TERMINAL twice a
//       second. Run it once with `line` and once with `square` and compare the
//       two terminals. `line` shows converged climbing; `square` shows it stuck
//       at 0.
//
// WHY NOT SHOW BOTH AT ONCE, OR A KEY TO TOGGLE? Both were tried and both are
// impossible here, which is worth knowing before you try again:
//
//   - Both in one window CANNOT work. The solver's stopping test is a single
//     verdict for the whole world, so the square poisons the reading for the
//     line sitting next to it. There is no per-object convergence number to
//     display.
//   - A key to delete the square at runtime CANNOT work. Writing a new position
//     onto these tiles does not stick: measured 0 of 4 tiles actually moved,
//     with and without the DYNAMICS owner flag. The probe that proves this runs
//     at the top of the headless output, so the claim stays honest.
//
// THERE IS NOTHING TO SEE IN THE PICTURE. The tiles sit there either way; the
// boxes rest correctly either way. This bug is invisible. The window is only
// there so you can confirm the scene is what the text says it is. The numbers
// in the terminal are the experiment.
//
// ---------------------------------------------------------------------------
// FOR ENGINE PEOPLE: what is suspected, and what is NOT established
// ---------------------------------------------------------------------------
//
// Trace level 2 shows every solve exiting identically, forever:
// improvement=0 and max_push=4 EXACTLY, never moving by a single bit. Something
// is handing the solver a constant it can never reduce.
//
// The suspicion is a "phantom push". Two sites disagree about what immovable
// means: the build step (physics_system_v4.cpp:856) checks only is_at_rest,
// while the apply step (:2035) checks is_at_rest OR KINEMATIC. For two
// immovable tiles the mass maths degenerates and :859 falls back to
// effective_mass = 1.0f, producing a push that is non-zero but multiplied by
// zero mass on application. It changes nothing and still counts toward the
// stopping test.
//
// THAT STORY IS NOT PROVEN. It predicts a 1x2 line breaks. It does not. So the
// fallback is at most part of it, and whatever the square adds (every tile
// gains a DIAGONAL neighbour; rows per tile go 320 -> 720) has not been
// isolated. Next step: trace level 5 on a 2x2 and a 1x3 and find which single
// constraint owns max_push in one and not the other.
//
//   LOGOSPHERE_PHYS_TRACE=5 LOGOSPHERE_PHYS_TRACE_FILE=/tmp/t.log ...
//
// Do not "fix" this from the story above. It is 0 for 2 so far.
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

    // ONE ARRANGEMENT PER LAUNCH. See the header: a line and a square cannot
    // share a window, because the solver produces ONE verdict for the whole
    // world and the square would poison the line's reading.
    const char* want = std::getenv("PHANTOM_SCENE");
    const bool square = (want && std::string(want) == "square");
    printf("\n  ============================================================\n");
    printf("   SCENE: %s\n", square ? "2x2 SQUARE of immovable tiles"
                                     : "1x3 LINE of immovable tiles");
    printf("\n   Nothing here can move. The tiles are KINEMATIC (solid, but\n");
    printf("   nothing can push them). There is no physics to do.\n");
    printf("\n   WATCH THIS TERMINAL. The window only confirms the scene is\n");
    printf("   what this text says; the bug is invisible on screen.\n");
    printf("\n   EXPECTED:  line   -> converged climbs\n");
    printf("              square -> converged STAYS 0 forever\n");
    printf("\n   Run it once with PHANTOM_SCENE=line and once with=square,\n");
    printf("   then compare the two terminals. ESC exits.\n");
    printf("  ============================================================\n\n");

    EngineConfig cfg;
    cfg.create_display = true;
    cfg.window_title = square ? "2x2 SQUARE (expect converged 0)" : "1x3 LINE (expect converged > 0)";
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
        p.r = 0.5f; p.g = 0.5f; p.b = 0.55f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        int id = engine.add_particle(p);
        auto v = ps.lock_particles_for_write();
        v[id].solver_mode = ParticleSolverMode::KINEMATIC;
        v[id].owner = ParticleOwner::DYNAMICS; v[id].is_at_rest = true;
    };
    if (square) { for (int gy = 0; gy < 2; ++gy) for (int gx = 0; gx < 2; ++gx) tile((float)gx, (float)gy); }
    else        { for (int gx = 0; gx < 3; ++gx) tile((float)gx, 0.0f); }
    ps.flush_pending_particles();
    ps.queue_light(0.0f, -7.0f, 9.0f, 260000.0f, 45.0f, 1.0f, 0.96f, 0.9f);
    ps.flush_pending_particles();

    // On-screen readout via a registered widget. Immediate-mode draw_text
    // between render() and present() is erased by the overlay clear; widgets
    // survive it. This only started working once the read-back composite was
    // fixed (see docs/VISUAL_TESTS.md).
    std::vector<ui::Label*> lines;
    if (auto* uis = engine.get_ui_system()) {
        for (int i = 0; i < 5; ++i) {
            auto* L = new ui::Label("", "phys_line_" + std::to_string(i));
            L->set_position(16, 16 + i * 20);
            L->set_size(820, 18);
            L->set_color(255, 255, 255);
            uis->add_widget(L);
            lines.push_back(L);
        }
    }

    long conv = 0, plat = 0;
    int frame = 0;
    while (engine.is_running()) {
        engine.update(1.0 / 60.0);
        engine.render();
        conv += (long)T::counter_value(T::Counter::PhysSolveConverged);
        plat += (long)T::counter_value(T::Counter::PhysSolvePlateaued);

        char b[5][200];
        snprintf(b[0], 200, "SCENE: %s of immovable tiles", square ? "2x2 SQUARE" : "1x3 LINE");
        snprintf(b[1], 200, "nothing here can move, so there is no physics to do");
        snprintf(b[2], 200, "CONVERGED (good) %ld", conv);
        snprintf(b[3], 200, "PLATEAUED (gave up) %ld", plat);
        snprintf(b[4], 200, "%s", square ? "converged should be 0: this is the bug"
                                         : "converged should climb: this is healthy");
        for (size_t i = 0; i < lines.size(); ++i) lines[i]->set_text(b[i]);
        if (!lines.empty() && square) lines[2]->set_color(255, 70, 70);

        if (frame % 30 == 0) {
            printf("  %-10s | CONVERGED %5ld | plateaued %5ld | rows %5llu\n",
                   square ? "SQUARE" : "LINE", conv, plat,
                   (unsigned long long)T::counter_value(T::Counter::PhysSolverRows));
            fflush(stdout);
        }

        engine.present();
        engine.get_platform()->poll_events();
        ++frame;
        if (engine.get_input_system().get_input_state().keys[GLFW_KEY_ESCAPE]) break;
    }
    printf("\n  %s: converged %ld, plateaued %ld over %d frames\n",
           square ? "SQUARE" : "LINE", conv, plat, frame);
    engine.shutdown();
    return true;
}

}  // namespace

// Does a KINEMATIC tile actually move when we write its position? The
// interactive toggle depends on it and has been silently failing: the tiles
// carried ParticleOwner::DYNAMICS, so an external writer restored them every
// frame. This checks whether dropping that owner makes the write stick, BEFORE
// the toggle claims anything to a user again.
bool probe_teleport_sticks(bool with_dynamics_owner) {
    EngineConfig cfg;
    cfg.create_display = false; cfg.enable_chat_window = false; cfg.show_debug_overlay = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) return false;
    auto& ps = engine.get_particle_system();
    std::vector<int> ids;
    for (int gy = 0; gy < 2; ++gy)
        for (int gx = 0; gx < 2; ++gx) {
            Particle p = {};
            p.shape = ParticleShape::BOX;
            p.x = (float)gx; p.y = (float)gy; p.z = 0.05f;
            p.width = p.height = 1.0f; p.thickness = 0.1f; p.size = 1.0f;
            p.r = p.g = p.b = 0.5f; p.a = 1.0f;
            p.SetMaterial(Materials::Type::STONE);
            int id = engine.add_particle(p);
            auto v = ps.lock_particles_for_write();
            v[id].solver_mode = ParticleSolverMode::KINEMATIC;
            if (with_dynamics_owner) v[id].owner = ParticleOwner::DYNAMICS;
            v[id].is_at_rest = true;
            ids.push_back(id);
        }
    ps.flush_pending_particles();
    for (int f = 0; f < 40; ++f) engine.update(1.0 / 60.0);
    { auto v = ps.lock_particles_for_write(); for (int id : ids) v[id].z = -500.0f; }
    for (int f = 0; f < 30; ++f) engine.update(1.0 / 60.0);
    int moved = 0;
    { auto v = ps.lock_particles_for_write(); for (int id : ids) if (v[id].z < -100.0f) ++moved; }
    engine.shutdown();
    printf("    owner=%-9s -> %d of %zu tiles actually moved\n",
           with_dynamics_owner ? "DYNAMICS" : "(none)", moved, ids.size());
    return moved == (int)ids.size();
}

bool test_immovable_pair_phantom_impulse() {
    if (std::getenv("INTERACTIVE")) return run_interactive();

    // PHANTOM_SCENE headless: run ONE arrangement alone, so a trace file
    // contains that arrangement and nothing else. Comparing a line against a
    // square inside one trace is hopeless.
    if (const char* only = std::getenv("PHANTOM_SCENE")) {
        const bool sq = std::string(only) == "square";
        Scene s{sq ? "2x2 square (alone)" : "1x3 line (alone)", sq ? 2 : 3, sq ? 2 : 1, false};
        measure(s);
        printf("\n  %-24s converged %ld  plateaued %ld  rows %ld\n",
               s.name, s.converged, s.plateaued, s.rows);
        return true;
    }

    printf("\n=== IMMOVABLE PAIRS MUST NOT PRODUCE WORK ===\n");
    printf("A scene in which nothing can move must not defeat the solver.\n\n");

    // A LADDER OF ARRANGEMENTS, not one case. The first version of this test
    // asserted that any two touching immovable bodies break the solver, and it
    // came back GREEN: a line of two converges fine. So the mechanism needs
    // more than "a pair with zero inverse mass on both sides", and the honest
    // move is to find WHICH arrangement crosses the line rather than to fix on
    // a story the test just refused to confirm.
    printf("  can a KINEMATIC tile be teleported at all?\n");
    const bool with_owner = probe_teleport_sticks(true);
    const bool no_owner   = probe_teleport_sticks(false);
    printf("  -> teleport %s with ParticleOwner::DYNAMICS, %s without it\n\n",
           with_owner ? "STICKS" : "REVERTS", no_owner ? "STICKS" : "REVERTS");

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
        printf("\n  FAIL hygiene (BLIND): the control scene never converged either, so this test\n"
               "  cannot distinguish a broken solver from a broken scene. Do not read the\n"
               "  rows below as evidence of anything.\n");
        return false;
    }
    printf("\n  hygiene OK: a lone slab converges (%ld solves), so the test can see convergence.\n",
           a.converged);

    // THE ASSERTION. Nothing in scene B can move. Either no constraint is built
    // (correct) or the solve converges at once. Grinding to the plateau limit on
    // work that cannot matter is the bug.
    if (b.plateaued > 0) {
        printf("\n  FAIL INV-23/INV-7/INV-8 (REGRESSION): a 3x3 grid of KINEMATIC tiles has NO dynamic\n"
               "        bodies. Nothing in it can move. Yet the solver plateaued %ld times\n"
               "        on %ld rows. This was fixed on 2026-08-01 by making the\n"
               "        effective-mass guard return 0 for an immovable pair instead of 1;\n"
               "        if it is red again, that guard has been reverted or a new row is\n"
               "        producing an impulse that moves nothing. Trace level 5 and look\n"
               "        for BOTH_IMMOVABLE rows. See kit study S22.\n",
               b.plateaued, b.rows);
        ok = false;
    } else {
        printf("  scene B OK INV-23: an immovable pair produces no solver work (%ld rows).\n", b.rows);
    }

    // And the consequence a user would actually notice: adding inert scenery
    // next to a real body destroys convergence for the real body too.
    if (c.converged == 0) {
        printf("  FAIL INV-23: a dynamic box over a 3x3 tile floor converges %ld times against\n"
               "        %ld over a single slab. Inert scenery changes how the solver treats\n"
               "        a real body, which is the consequence a user actually feels.\n",
               c.converged, a.converged);
        ok = false;
    } else {
        printf("  scene C OK INV-23: an extra tile does not destroy convergence (%ld solves).\n", c.converged);
    }

    printf("\n  %s\n", ok ? "PASS" : "FAIL");
    return ok;
}
