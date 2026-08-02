// =============================================================================
// PHYSICS BATTERY: one mechanism per scenario, increasing complexity
// =============================================================================
// WHAT THIS IS FOR.
//
// The engine's physics had, until recently, almost no instrumentation and no
// scenario coverage between "one particle on the floor" and "Eden with 19,000
// bodies". Everything in between was untested, so every bug arrived as
// "something looks wrong in the big scene" and had to be chased backwards.
//
// This is the ladder that was missing. Each scenario isolates ONE mechanism and
// adds exactly one thing to the one before it:
//
//   1  FREE FALL        gravity, no contacts at all
//   2  SINGLE IMPACT    one contact, made once, under speed
//   3  RESTING CONTACT  one contact, held forever
//   4  STACK            a chain of contacts, load passed down it
//   5  LEANING PILE     a chain that is not aligned, so contacts couple
//   6  PROJECTILE       momentum arriving from outside the pile
//   7  SPHERE ON PLANE  curved geometry and rotation instead of flat boxes
//   8  GLUON NAILS      a bond that must hold two bodies together under load
//
// If scenario 4 misbehaves and 3 is clean, the fault is in how load passes
// along a chain, and you have narrowed a bug to one mechanism without touching
// a debugger.
//
// WHAT IT MEASURES, AND WHY NOT THE OBVIOUS THING.
//
// Not "did the solver converge". That counter answers whether the iteration
// loop stopped changing its own numbers, which it now does on the first pass
// for every scene here, so it cannot tell these scenarios apart at all.
//
// It measures the RESIDUAL: how far the world is from legal when the solve
// finishes. Specifically the deepest overlap between two bodies that the solve
// left behind. Bodies are not allowed inside each other, so any overlap is
// error, in metres, that a person could in principle see.
//
// Alongside it, each scenario checks something physical that is true
// independently of any solver: a body in free fall covers 0.5*g*t^2, a resting
// body does not sink, a stack does not drift sideways.
//
// SCENES ARE DELIBERATELY TINY. The residual is a maximum over every contact in
// the world, so scenery contributes to it. An earlier test was contaminated by
// its own 25-tile floor drowning out the two bodies it meant to study. Every
// floor here is 3x3 and nothing is in the scene that the scenario does not
// need.
//
//   ./build-release/logosphere-tests --test test_physics_battery --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "../src/core/telemetry.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace T = ::logosphere::telemetry;

namespace {

struct Outcome {
    std::string name;
    double peak_pen   = 0.0;   // deepest overlap at any point: the residual
    double final_pen  = 0.0;
    uint32_t peak_rows = 0;    // most solvable contacts at once
    uint32_t unsolvable = 0;   // rows between two bodies that cannot move
    double check_value = 0.0;  // the scenario's own physical measurement
    double check_want  = 0.0;  // what physics says it should be
    double check_tol   = 0.0;
    std::string check_name;
    bool  checked = false;
    bool  passed  = true;
};

// Shared scaffolding. Every scenario gets an engine, the residual instrument
// armed, and optionally a small floor.
struct Scene {
    Engine engine;
    bool ok = false;

    bool start(bool with_floor) {
        EngineConfig cfg;
        cfg.create_display = false;
        cfg.enable_chat_window = false;
        cfg.show_debug_overlay = false;
        if (engine.initialize(cfg) != 0) return false;
        T::set_enabled(true);
        T::set_residual_enabled(true);   // also clears the previous scenario
        if (with_floor) add_floor();
        ok = true;
        return true;
    }

    // 3x3 KINEMATIC tiles. KINEMATIC means the solver may not move it; in this
    // engine the world boundary is the only genuinely immovable thing, so
    // anything that looks static is a body being held.
    void add_floor() {
        auto& ps = engine.get_particle_system();
        for (int c = -1; c <= 1; ++c)
            for (int d = -1; d <= 1; ++d) {
                Particle p = {};
                p.shape = ParticleShape::BOX;
                p.x = (float)c; p.y = (float)d; p.z = 0.05f;
                p.width = p.height = 1.0f; p.thickness = 0.1f; p.size = 1.0f;
                p.r = p.g = p.b = 0.5f; p.a = 1.0f;
                p.SetMaterial(Materials::Type::STONE);
                int id = engine.add_particle(p);
                auto v = ps.lock_particles_for_write();
                v[id].solver_mode = ParticleSolverMode::KINEMATIC;
                v[id].owner = ParticleOwner::DYNAMICS;
                v[id].is_at_rest = true;
            }
    }

    int add_box(float x, float y, float z, float size = 1.0f) {
        Particle p = {};
        p.shape = ParticleShape::BOX;
        p.x = x; p.y = y; p.z = z;
        p.width = p.height = p.thickness = size; p.size = size;
        p.r = 0.8f; p.g = 0.4f; p.b = 0.3f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        return engine.add_particle(p);
    }

    int add_sphere(float x, float y, float z, float diameter = 1.0f) {
        Particle p = {};
        p.shape = ParticleShape::SPHERE;
        p.x = x; p.y = y; p.z = z;
        p.size = diameter;
        p.width = p.height = p.thickness = diameter;
        p.r = 0.3f; p.g = 0.6f; p.b = 0.85f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        return engine.add_particle(p);
    }

    void flush() { engine.get_particle_system().flush_pending_particles(); }

    // Steps the world and keeps the worst residual seen. Sampling every frame
    // matters: once bodies fall asleep every contact becomes unsolvable and a
    // reading taken at the end sees nothing at all.
    void run(int frames, Outcome& out) {
        for (int f = 0; f < frames; ++f) {
            engine.update(1.0 / 60.0);
            const T::SolveResidual s = T::solve_residual();
            if (s.max_penetration > out.peak_pen) out.peak_pen = s.max_penetration;
            if (s.rows > out.peak_rows) out.peak_rows = s.rows;
            if (s.rows_unsolvable > out.unsolvable) out.unsolvable = s.rows_unsolvable;
        }
        out.final_pen = T::solve_residual().max_penetration;
    }

    Particle read(int id) {
        auto v = engine.get_particle_system().lock_particles_for_write();
        return v[id];
    }

    void set_velocity(int id, float vx, float vy, float vz) {
        auto v = engine.get_particle_system().lock_particles_for_write();
        v[id].vx = vx; v[id].vy = vy; v[id].vz = vz;
    }

    ~Scene() { if (ok) engine.shutdown(); }
};

void check(Outcome& o, const char* what, double got, double want, double tol) {
    o.check_name = what; o.check_value = got; o.check_want = want;
    o.check_tol = tol; o.checked = true;
    if (std::fabs(got - want) > tol) o.passed = false;
}

// --- 1. FREE FALL ----------------------------------------------------------
// No floor, no contacts, nothing to solve. Isolates gravity integration.
// After t seconds a body dropped from rest has fallen 0.5*g*t^2. Tolerance is
// loose because a fixed-step integrator accumulates a known small offset; this
// is checking that gravity is roughly right, not that the integrator is exact.
Outcome s1_free_fall() {
    Outcome o; o.name = "1 free fall";
    Scene s; if (!s.start(false)) return o;
    const int id = s.add_box(0, 0, 50.0f);
    s.flush();
    const float z0 = s.read(id).z;
    const int FRAMES = 60;                     // 1.0 s at 60 Hz
    s.run(FRAMES, o);
    const double t = FRAMES / 60.0;
    const double fell = z0 - s.read(id).z;
    check(o, "drop after 1s (m)", fell, 0.5 * 9.81 * t * t, 0.6);
    return o;
}

// --- 2. SINGLE IMPACT ------------------------------------------------------
// One body, one contact, arriving fast. Isolates contact creation and the
// impulse that stops a moving body. The check is that it ends up resting ON the
// floor rather than inside it or bounced away.
Outcome s2_single_impact() {
    Outcome o; o.name = "2 single impact";
    Scene s; if (!s.start(true)) return o;
    const int id = s.add_box(0, 0, 3.0f);       // ~2.4 m above the floor top
    s.flush();
    s.run(180, o);
    check(o, "rest height (m)", s.read(id).z, 0.6, 0.02);
    return o;
}

// --- 3. RESTING CONTACT ----------------------------------------------------
// Same contact as scenario 2 but never disturbed: placed exactly touching. Any
// movement at all is the solver inventing energy or losing it. This is the
// quietest scene the engine can be asked to hold and the baseline for the rest.
Outcome s3_resting() {
    Outcome o; o.name = "3 resting contact";
    Scene s; if (!s.start(true)) return o;
    const int id = s.add_box(0, 0, 0.6f);
    s.flush();
    s.run(180, o);
    check(o, "drift from 0.600 (m)", std::fabs(s.read(id).z - 0.6), 0.0, 0.005);
    return o;
}

// --- 4. STACK --------------------------------------------------------------
// Four boxes, aligned, each resting on the one below. Adds a CHAIN: the bottom
// contact carries the weight of everything above it, and that load has to
// propagate down through three intermediate contacts. This is where a
// sequential solver first has real work to do.
Outcome s4_stack() {
    Outcome o; o.name = "4 stack of 4";
    Scene s; if (!s.start(true)) return o;
    std::vector<int> ids;
    for (int i = 0; i < 4; ++i) ids.push_back(s.add_box(0, 0, 0.6f + i * 1.0f));
    s.flush();
    s.run(180, o);
    check(o, "bottom box z (m)", s.read(ids[0]).z, 0.6, 0.01);
    return o;
}

// --- 5. LEANING PILE -------------------------------------------------------
// The same chain, but each box offset so the pile leans. Now contacts are not
// axis-aligned with each other and every one couples to its neighbours. This is
// the hardest case for a solver that works through contacts one at a time.
// The check is that it does not COLLAPSE: a lean is fine, falling over is not.
Outcome s5_leaning() {
    Outcome o; o.name = "5 leaning pile";
    Scene s; if (!s.start(true)) return o;
    std::vector<int> ids;
    for (int i = 0; i < 5; ++i)
        ids.push_back(s.add_box(i % 2 ? 0.18f : -0.18f, i % 3 ? 0.12f : -0.12f,
                                0.6f + i * 1.0f));
    s.flush();
    s.run(240, o);
    check(o, "top box still up (m)", s.read(ids[4]).z, 4.6, 0.6);
    return o;
}

// --- 6. PROJECTILE ---------------------------------------------------------
// A body thrown sideways into a stack. Adds momentum arriving from outside, so
// the solver has to resolve a large velocity in one step rather than the slow
// accumulation gravity produces. Deep overlap on the impact frame would show up
// as a spike in the peak residual.
Outcome s6_projectile() {
    Outcome o; o.name = "6 projectile";
    Scene s; if (!s.start(true)) return o;
    std::vector<int> tower;
    for (int i = 0; i < 3; ++i) tower.push_back(s.add_box(0, 0, 0.6f + i * 1.0f));
    const int ball = s.add_box(-3.0f, 0, 1.6f, 0.6f);
    s.flush();
    s.set_velocity(ball, 12.0f, 0, 0);          // thrown, not dropped
    s.run(240, o);
    // No position assertion: where a struck tower ends up is chaotic and
    // asserting on it would produce a test that fails for no reason. The
    // residual is the measurement; this scenario reports rather than asserts.
    return o;
}

// --- 7. SPHERE ON PLANE ----------------------------------------------------
// Curved geometry instead of flat faces. A box on a floor touches over an area;
// a sphere touches at a point, which is a much less forgiving contact to hold.
// The check is that it comes to rest at exactly one radius above the surface.
Outcome s7_sphere() {
    Outcome o; o.name = "7 sphere on plane";
    Scene s; if (!s.start(true)) return o;
    const int id = s.add_sphere(0, 0, 2.0f, 1.0f);
    s.flush();
    s.run(240, o);
    check(o, "rest height (m)", s.read(id).z, 0.6, 0.05);
    return o;
}

// --- 8. GLUON NAILS --------------------------------------------------------
// Two boxes bonded by a gluon, the engine's model for a rigid joint (what
// holds a rock or a tree together). One is held still, the other hangs off it,
// so the bond carries the hanging box's whole weight. Isolates constraint
// holding under load, with no contact involved at all.
Outcome s8_gluon() {
    Outcome o; o.name = "8 gluon nails";
    Scene s; if (!s.start(false)) return o;
    const int anchor = s.add_box(0, 0, 5.0f);
    const int hangs  = s.add_box(1.0f, 0, 5.0f);
    s.flush();
    {
        auto v = s.engine.get_particle_system().lock_particles_for_write();
        v[anchor].solver_mode = ParticleSolverMode::KINEMATIC;
        v[anchor].owner = ParticleOwner::DYNAMICS;
        v[anchor].material_strength = 1e9f;     // must not break: that is not what this tests
        v[hangs].material_strength  = 1e9f;
    }
    auto g = std::make_unique<OrganicGluon>();
    g->offset_a = {0.5f, 0.0f, 0.0f};
    g->offset_b = {-0.5f, 0.0f, 0.0f};
    g->target_distance = 0.0f;
    g->contact_area = 1.0f;
    g->stiffness = 50000.0f;
    g->damping = 1000.0f;
    s.engine.get_physics_system().add_gluon_between(anchor, hangs, std::move(g));

    const float x0 = s.read(hangs).x, z0 = s.read(hangs).z;
    s.run(180, o);
    const float dx = s.read(hangs).x - x0, dz = s.read(hangs).z - z0;
    check(o, "bonded box drift (m)", std::sqrt(dx * dx + dz * dz), 0.0, 0.10);
    return o;
}

}  // namespace

bool test_physics_battery() {
    printf("\n=== PHYSICS BATTERY ===\n");
    printf("One mechanism per scenario, each adding one thing to the last.\n");
    printf("PEAK PEN is the residual: the deepest overlap between two bodies that\n");
    printf("the solver left behind, in metres. Bodies may not be inside each other,\n");
    printf("so any value above the 1 mm slop band is error you could in principle\n");
    printf("see. It is reported for every scenario; the CHECK column is each\n");
    printf("scenario's own physical test, true independently of any solver.\n\n");

    std::vector<Outcome> all;
    all.push_back(s1_free_fall());
    all.push_back(s2_single_impact());
    all.push_back(s3_resting());
    all.push_back(s4_stack());
    all.push_back(s5_leaning());
    all.push_back(s6_projectile());
    all.push_back(s7_sphere());
    all.push_back(s8_gluon());

    printf("  %-19s %9s %9s %6s %7s  %-22s %9s %9s %s\n",
           "scenario", "PEAK pen", "final", "rows", "UNSOLV",
           "check", "got", "want", "");
    for (const Outcome& o : all) {
        printf("  %-19s %9.5f %9.5f %6u %7u  %-22s ",
               o.name.c_str(), o.peak_pen, o.final_pen, o.peak_rows, o.unsolvable,
               o.checked ? o.check_name.c_str() : "(reports only)");
        if (o.checked) printf("%9.4f %9.4f  %s\n", o.check_value, o.check_want,
                              o.passed ? "ok" : "*** OFF ***");
        else printf("%9s %9s\n", "", "");
    }

    int failures = 0;
    for (const Outcome& o : all) if (!o.passed) failures++;

    // The residual across the ladder is the diagnostic the battery exists for.
    double worst = 0.0; const char* worst_name = "";
    for (const Outcome& o : all)
        if (o.peak_pen > worst) { worst = o.peak_pen; worst_name = o.name.c_str(); }
    printf("\n  Worst residual on the ladder: %.5f m, in '%s'.\n", worst, worst_name);
    printf("  Read the PEAK PEN column top to bottom. It should stay near zero and\n"
           "  grow only where a scenario genuinely asks more of the solver. A jump\n"
           "  between two adjacent rows localises a weakness to the one mechanism\n"
           "  the second row added.\n");

    if (failures) {
        printf("\n  %d scenario(s) failed their physical check. Those are marked OFF\n"
               "  above, with the measured and expected values side by side.\n", failures);
    } else {
        printf("\n  All physical checks hold.\n");
    }

    T::set_residual_enabled(false);
    printf("\n  %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0;
}
