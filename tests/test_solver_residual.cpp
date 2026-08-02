// =============================================================================
// SOLVER RESIDUAL: is the answer good, or did the loop just stop?
// =============================================================================
// IF YOU HAVE NEVER TOUCHED THIS ENGINE, START HERE.
//
// The physics solver's job is to stop bodies from passing through each other.
// It does that by repeatedly nudging their velocities: look at every pair that
// is touching, work out how hard to push them apart, apply that push, and go
// round again. Each pass is called an ITERATION. Round and round until the
// answer stops changing, then move on to the next frame.
//
// "Until the answer stops changing" is the part that matters here. The solver
// decides it is done by watching how big its own nudges are. When a full pass
// barely changes anything, it stops. Sensible, and it is the right rule for
// deciding when more iterations are wasted effort.
//
// It is NOT a measure of whether the answer is correct, and the difference is
// not academic. A solver can get stuck somewhere wrong and sit there. Nothing
// changes, the nudges go to zero, and it reports success. We measured exactly
// that: a stack of 8 boxes settles 1.3 mm lower than a stack of 1, every box
// slightly inside the one below it, and the solver calls it converged.
//
// So this file adds a second, independent question. Not "did the loop stop"
// but "how wrong is the answer it stopped at". After the solver finishes, walk
// every contact it was supposed to satisfy and ask how badly each is still
// violated. That number is the RESIDUAL.
//
// WHY THE FIRST HALF OF THIS TEST LOOKS LIKE IT IS TESTING NOTHING.
//
// A new instrument that reads zero is indistinguishable from a broken
// instrument, and this campaign has already shipped several measurements that
// were confidently reporting nothing at all. So before any result is believed,
// the test builds a scene that is DEFINITELY violating physics (two boxes
// spawned 40% inside each other, which no solver can fix in one frame) and
// checks the residual is large. Then a scene that is definitely fine (one box
// resting at exactly contact distance, left alone to settle) and checks it is
// small. If those two read the same, the instrument is blind, and this test
// FAILS rather than printing a table of meaningless numbers.
//
// Only after that does it run the actual study.
//
//   ./build-release/logosphere-tests --test test_solver_residual --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "../src/core/telemetry.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace T = ::logosphere::telemetry;

namespace {

// Everything the instrument reports, plus where the bodies actually ended up.
// The two are independent: `rest_z` is measured from the particle, not derived
// from the residual, so they can disagree and that disagreement is a finding.
struct Reading {
    T::SolveResidual res{};     // the LAST frame's reading
    double peak_pen = 0.0;      // worst overlap seen at ANY point in the run
    uint32_t peak_a = 0, peak_b = 0;  // and WHICH PAIR produced it
    int      peak_frame = -1;
    uint32_t peak_rows = 0;     // most solvable rows seen at any point
    float rest_z = 0.0f;
};

// Builds a floor and drops `stack` boxes on it.
//
//   overlap = 0.0  boxes placed exactly touching. Nothing to fix; correct
//                  behaviour is a residual of zero.
//   overlap > 0.0  boxes spawned that fraction of their height INSIDE each
//                  other. Physically impossible, and the solver cannot undo it
//                  in the frames we watch, so the residual must be large.
//
// `settle_frames` is how long to run before reading. Reading during the first
// frames of an overlap is the point of the violating case; reading a settled
// scene is the point of the clean one.
Reading run(int stack, float overlap, int settle_frames) {
    Reading out;

    EngineConfig cfg;
    cfg.create_display = false;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) { printf("  ERROR: engine init failed\n"); return out; }
    T::set_enabled(true);
    // Also CLEARS the published reading. Without that, this second engine would
    // start life reading the previous engine's last solve: see the comment in
    // telemetry.cpp, which is where that trap was fixed.
    T::set_residual_enabled(true);
    auto& ps = engine.get_particle_system();

    const float SIZE = 1.0f;

    // The floor. Deliberately SMALL: 3x3 tiles, not the 5x5 an earlier test
    // used. The residual's worst-row search is a maximum over every contact in
    // the world, so a big floor's own internal contacts can drown out the
    // handful of contacts this test is actually about. Minimal scenes, or the
    // test measures the scenery.
    //
    // KINEMATIC means "the solver may not move this". The floor holds still
    // because of that flag, not because it is special: in this engine the only
    // truly immovable thing is the world boundary, and everything else that
    // looks static is a body being told to hold position.
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

    // The stack. At overlap 0 each box sits exactly on the one below it, so
    // the scene starts at rest and there is no impact to confuse the reading.
    std::vector<int> ids;
    for (int i = 0; i < stack; ++i) {
        Particle p = {};
        p.shape = ParticleShape::BOX;
        p.x = 0.0f; p.y = 0.0f;
        p.z = 0.10f + SIZE * 0.5f + i * SIZE * (1.0f - overlap);
        p.width = p.height = p.thickness = SIZE; p.size = SIZE;
        p.r = 0.8f; p.g = 0.4f; p.b = 0.3f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        ids.push_back(engine.add_particle(p));
    }
    ps.flush_pending_particles();
    // SAMPLE EVERY FRAME, not just the last one.
    //
    // The first version of this test read the residual once, at the end, and
    // saw zeros everywhere. The reason is worth writing down: once a scene
    // settles, every body is flagged at-rest, at-rest bodies have zero inverse
    // mass, and a row between two of those cannot be solved at all. So the
    // final frame has NOTHING solvable in it. The error is not absent, it was
    // made earlier and then frozen in place when the bodies went to sleep.
    // A peak across the window sees it; a snapshot at the end cannot.
    for (int f = 0; f < settle_frames; ++f) {
        engine.update(1.0 / 60.0);
        const T::SolveResidual s = T::solve_residual();
        if (s.max_penetration > out.peak_pen) {
            out.peak_pen = s.max_penetration;
            out.peak_a = s.worst_a; out.peak_b = s.worst_b; out.peak_frame = f;
        }
        if (s.rows > out.peak_rows) out.peak_rows = s.rows;
    }

    out.res = T::solve_residual();
    {
        auto v = ps.lock_particles_for_write();
        if (!ids.empty()) out.rest_z = v[ids[0]].z;
    }
    engine.shutdown();
    return out;
}

void header() {
    printf("  %-26s %10s %10s %9s %8s %8s %8s %8s\n",
           "scene", "PEAK pen", "worst pair", "max vel", "pk rows", "fin rows",
           "UNSOLV", "rest z");
}

void print_row(const char* label, const Reading& r) {
    char pair[24];
    snprintf(pair, sizeof(pair), "%u-%u@f%d", r.peak_a, r.peak_b, r.peak_frame);
    printf("  %-26s %10.5f %10s %9.5f %8u %8u %8u %8.4f\n",
           label, r.peak_pen, pair, r.res.max_violation,
           r.peak_rows, r.res.rows, r.res.rows_unsolvable, r.rest_z);
}

}  // namespace

bool test_solver_residual() {
    printf("\n=== SOLVER RESIDUAL ===\n");
    printf("Not 'did the solver stop', but 'how wrong was it when it stopped'.\n\n");
    printf("Two columns, because there are two different questions:\n");
    printf("  max PEN  metres of overlap left standing. What a person would SEE.\n");
    printf("  max vel  how far the solver missed the velocity it was aiming for.\n");
    printf("These disagree, and that is the finding. The solver aims at a velocity\n");
    printf("derived from the overlap, so it can hit that velocity perfectly while\n");
    printf("the boxes are still buried in each other. max vel reads 0; the bodies\n");
    printf("are still wrong. Only the first column answers the question we care\n");
    printf("about, which is why this test was rewritten after the first version\n");
    printf("measured velocity alone and read zero on a 40%% overlap.\n\n");

    header();

    // -------------------------------------------------------------------
    // CONTROL 1: can the instrument see a violation that is definitely there?
    // -------------------------------------------------------------------
    // Two boxes spawned 40% inside each other. This is not a subtle case; the
    // bodies are massively interpenetrating and the solver is pushing hard to
    // separate them. Read on frame 2, while it is still working.
    const Reading violating = run(/*stack=*/2, /*overlap=*/0.4f, /*frames=*/2);
    print_row("VIOLATING (40% overlap)", violating);

    // -------------------------------------------------------------------
    // CONTROL 2: does it read low when nothing is wrong?
    // -------------------------------------------------------------------
    // One box, placed exactly at contact distance, given 90 frames to settle.
    // If this reads high the instrument is reporting noise as violation.
    const Reading clean = run(/*stack=*/1, /*overlap=*/0.0f, /*frames=*/90);
    print_row("SETTLED (1 box, no overlap)", clean);

    // The gate is on PENETRATION. Two boxes 40% inside each other overlap by
    // 0.4 m, so anything under a few centimetres here means the instrument is
    // not seeing an overlap that is impossible to miss.
    const bool sees_violation = violating.peak_pen > 0.05
                             && violating.peak_pen > clean.peak_pen * 10.0;
    printf("\n");
    if (!sees_violation) {
        printf("  *** THE INSTRUMENT IS BLIND. ***\n"
               "  Two boxes spawned 40%% inside each other, an overlap of 0.4 m, and the\n"
               "  worst overlap seen at any point was %.6f m. A settled single box saw\n"
               "  %.6f m. If the first\n"
               "  number is not obviously large, the measurement cannot see a violated\n"
               "  constraint and every number below it is noise. Do not read the study.\n",
               violating.peak_pen, clean.peak_pen);
        printf("\n  FAIL\n");
        return false;
    }
    printf("  Instrument responds: %.5f m of overlap detected against %.5f m settled.\n"
           "  Note max vel is %.5f on that same violating scene: the velocity residual\n"
           "  is blind to it by construction, exactly as described above.\n\n",
           violating.peak_pen, clean.peak_pen, violating.res.max_violation);

    // -------------------------------------------------------------------
    // THE STUDY: does the answer get worse as the stack gets deeper?
    // -------------------------------------------------------------------
    // Every rung is the same scene with more boxes on it. The solver reports
    // "converged" on all of them. The question is whether that word means the
    // same thing at the bottom of the ladder as at the top.
    printf("  Same scene, deeper stack. The solver reports CONVERGED on every rung.\n\n");
    header();

    std::vector<Reading> ladder;
    for (int s : {1, 2, 4, 8}) {
        char label[64];
        snprintf(label, sizeof(label), "stack of %d", s);
        Reading r = run(s, 0.0f, 90);
        print_row(label, r);
        ladder.push_back(r);
    }

    printf("\n");
    printf("  UNSOLVBLE counts contacts between two bodies that both refuse to move.\n"
           "  No push can change anything, so the solver can never satisfy them and it\n"
           "  is not a failure that it does not. They are built and solved every substep\n"
           "  regardless, which is why they are counted here rather than ignored: a\n"
           "  large number in that column is wasted work, not a physics problem.\n");

    // -------------------------------------------------------------------
    // CONTROL 3: the gate. Off must mean nothing is written.
    // -------------------------------------------------------------------
    // The extra pass costs about one solver iteration, so it is off by default.
    // If it ran anyway, that cost would be paid by every user forever.
    T::set_residual_enabled(false);
    T::record_solve_residual(T::SolveResidual{-1.0, -1.0, -1.0, -1.0, -1.0, 0, 0, 0, 0, 0, 0});
    {
        EngineConfig cfg;
        cfg.create_display = false; cfg.enable_chat_window = false; cfg.show_debug_overlay = false;
        Engine e;
        if (e.initialize(cfg) == 0) {
            Particle p = {};
            p.shape = ParticleShape::BOX; p.z = 2.0f;
            p.width = p.height = p.thickness = 1.0f; p.size = 1.0f; p.a = 1.0f;
            p.SetMaterial(Materials::Type::STONE);
            e.add_particle(p);
            e.get_particle_system().flush_pending_particles();
            for (int f = 0; f < 10; ++f) e.update(1.0 / 60.0);
            e.shutdown();
        }
    }
    const bool gate_holds = T::solve_residual().max_penetration < 0.0;
    printf("\n  gate off, sentinel survives 10 frames of physics: %s\n",
           gate_holds ? "yes, the pass did not run" : "NO, it ran anyway");

    T::set_residual_enabled(false);
    const bool pass = sees_violation && gate_holds;
    printf("\n  %s\n", pass ? "PASS" : "FAIL");
    return pass;
}
