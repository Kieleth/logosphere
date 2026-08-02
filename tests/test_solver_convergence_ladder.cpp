// =============================================================================
// SOLVER CONVERGENCE LADDER: the simplest scene in which the solver gives up
// =============================================================================
// S19 established that the sequential-impulse solver NEVER converges: the
// "plateaued, more iterations won't help" exit fires on every substep of every
// frame at 2,000 through 14,000 bodies, and the "converged" exit has never once
// fired. Because Gauss-Seidel applies each constraint against velocities already
// changed by the ones before it, always stopping short makes constraint ORDER
// load-bearing on the physical result, which closes parallelism, islands, SoA
// and sorting as optimisations.
//
// That was measured on a 14,000-body pile. It does not say WHY, and the why
// decides everything:
//
//   If ONE BOX resting on the floor already plateaus, the plateau detector or
//   the bias formulation is broken. That is a BUG with a cheap fix, and fixing
//   it would reopen parallelism and rewrite the whole optimisation programme.
//
//   If simple cases converge and convergence degrades with contact-chain depth,
//   sequential impulse is behaving as sequential impulse does, and the answer is
//   a different formulation or an accepted limit.
//
// This walks a ladder that isolates ONE variable: the depth of the contact
// chain. Each rung is otherwise identical. A healthy solver converges on rung 1
// trivially (one contact, one body, at equilibrium, zero residual impulse) and
// degrades somewhere further up. Where it first fails is the answer.
//
// Deliberately minimal: no gluons, no rotation, no friction interplay, boxes
// only, perfectly axis-aligned, dropped from rest at contact distance so there
// is no impact transient to confuse the reading. If this cannot converge,
// nothing can.
//
//   ./build-release/logosphere-tests --test test_solver_convergence_ladder --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include "../src/core/telemetry.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

struct Rung {
    int  stack;          // dynamic boxes stacked on the floor
    long converged = 0, plateaued = 0, exhausted = 0;
    long iterations = 0, solves = 0, rows = 0;
    float settle_z = 0.0f;   // final z of the lowest dynamic box
};

// One KINEMATIC floor tile, then `stack` dynamic boxes resting on it, each
// placed exactly at contact distance so the scene starts at equilibrium.
// Anything that fails here fails in the easiest world that exists.
void run_rung(Rung& r) {
    namespace T = ::logosphere::telemetry;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) return;
    T::set_enabled(true);
    auto& ps = engine.get_particle_system();

    const float SIZE = 1.0f;
    // Floor extent is a knob because the first run showed `rows` identical at
    // every stack height, which means the stack is not what generates them.
    const int FEXT = std::getenv("LADDER_FLOOR") ? std::atoi(std::getenv("LADDER_FLOOR")) : 2;
    // Floor: a single wide KINEMATIC slab. The turtle is the only truly
    // immovable thing; immobility here comes from solver mode, as it must.
    for (int c = -FEXT; c <= FEXT; ++c)
        for (int d = -FEXT; d <= FEXT; ++d) {
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

    std::vector<int> ids;
    for (int i = 0; i < r.stack; ++i) {
        Particle p = {};
        p.shape = ParticleShape::BOX;
        p.x = 0.0f; p.y = 0.0f;
        p.z = 0.10f + SIZE * 0.5f + i * SIZE;   // exactly touching, no drop
        p.width = p.height = p.thickness = SIZE; p.size = SIZE;
        p.r = 0.8f; p.g = 0.4f; p.b = 0.3f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        ids.push_back(engine.add_particle(p));
    }
    ps.flush_pending_particles();

    // Let it settle, then measure only the settled window: an impact transient
    // would legitimately plateau and would tell us nothing about equilibrium.
    const int WARM = 90, MEASURE = 60;
    for (int f = 0; f < WARM; ++f) engine.update(1.0 / 60.0);

    long c0 = (long)T::counter_value(T::Counter::PhysSolveConverged);
    long p0 = (long)T::counter_value(T::Counter::PhysSolvePlateaued);
    long e0 = (long)T::counter_value(T::Counter::PhysSolveExhausted);
    long i0 = (long)T::counter_value(T::Counter::PhysSolverIterations);
    long w0 = (long)T::counter_value(T::Counter::PhysSolverRows);

    for (int f = 0; f < MEASURE; ++f) {
        engine.update(1.0 / 60.0);
        r.converged  += (long)T::counter_value(T::Counter::PhysSolveConverged);
        r.plateaued  += (long)T::counter_value(T::Counter::PhysSolvePlateaued);
        r.exhausted  += (long)T::counter_value(T::Counter::PhysSolveExhausted);
        r.iterations += (long)T::counter_value(T::Counter::PhysSolverIterations);
        r.rows       += (long)T::counter_value(T::Counter::PhysSolverRows);
    }
    (void)c0; (void)p0; (void)e0; (void)i0; (void)w0;
    r.solves = r.converged + r.plateaued + r.exhausted;

    {
        auto v = ps.lock_particles_for_write();
        if (!ids.empty()) r.settle_z = v[ids[0]].z;
    }
    engine.shutdown();
}

}  // namespace

bool test_solver_convergence_ladder() {
    printf("\n=== SOLVER CONVERGENCE LADDER ===\n");
    printf("One variable: contact-chain depth. Everything else held fixed.\n");
    printf("A healthy solver converges on rung 1: one body, one contact, already\n");
    printf("at equilibrium, nothing left to solve.\n\n");

    const int rungs[] = {1, 2, 3, 4, 6, 8};
    std::vector<Rung> results;
    for (int s : rungs) { Rung r; r.stack = s; run_rung(r); results.push_back(r); }

    printf("  %-7s %10s %10s %10s %12s %10s %9s\n",
           "stack", "CONVERGED", "plateaued", "exhausted", "iters/solve", "rows/frm", "rest z");
    for (const auto& r : results) {
        printf("  %-7d %10ld %10ld %10ld %12.1f %10.1f %9.4f\n",
               r.stack, r.converged, r.plateaued, r.exhausted,
               r.solves ? (double)r.iterations / r.solves : 0.0,
               r.converged + r.plateaued + r.exhausted ? (double)r.rows / 60.0 : 0.0,
               r.settle_z);
    }

    const Rung& one = results.front();
    printf("\n");
    if (one.solves == 0) {
        printf("  INCONCLUSIVE: rung 1 recorded no solves at all. Either the bodies never\n"
               "  generated a contact, or the counters are not reaching this path.\n");
        return true;
    }
    if (one.converged > 0 && one.plateaued == 0) {
        printf("  RUNG 1 CONVERGES. The plateau is therefore NOT unconditional, and the\n"
               "  cliff is somewhere up this ladder. Read the table for where.\n");
    } else {
        printf("  *** RUNG 1 PLATEAUS. ONE BOX. ONE CONTACT. AT EQUILIBRIUM. ***\n"
               "  A solver that cannot converge on a single resting body is not hitting a\n"
               "  limit of sequential impulse, it has a defect: either the plateau detector\n"
               "  fires on a healthy solve, or the bias keeps injecting impulse at rest.\n"
               "  That is a BUG, and fixing it would reopen parallelism, islands and SoA,\n"
               "  all of which S19 closed on the strength of the plateau reading.\n");
    }
    printf("\n  DIAGNOSTIC, not a gate: this reports, it does not fail.\n");
    return true;
}
