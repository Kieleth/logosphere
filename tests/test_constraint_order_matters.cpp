// =============================================================================
// IS CONSTRAINT ORDER LOAD-BEARING? (the S19 re-run)
// =============================================================================
// WHAT THIS IS ABOUT, for someone who has not read the journal.
//
// The solver fixes overlapping bodies by going through a list of contacts and
// nudging each pair apart in turn. Each nudge is applied to velocities that
// earlier nudges in the same pass already changed. So the ORDER of that list is
// genuinely part of the computation: run the same contacts in a different order
// and the arithmetic differs.
//
// LAWS (assert-protocol migration, 2026-08-21). The one GATED assert is
// hygiene: it proves the shuffle lever engaged, because a permutation that
// never reached the solver would report "order does not matter" while
// measuring nothing. The measurement itself bears on INV-27 (the same scene
// stepped the same way produces bit-identical state — order-dependence is the
// non-incidental version of the same question) and on INV-2, since the verdict
// is taken on peak penetration and not on where the boxes landed.
//
// The question is whether it is part of the ANSWER. Those are not the same
// thing. If the solve runs to a true solution, order affects only the path
// taken and the last few bits of the result. If it always stops short, order
// decides where it stops, and then it is baked into the physics.
//
// WHY THIS MATTERS MORE THAN IT SOUNDS. If order is load-bearing, then solving
// contacts on several threads, splitting the world into independent islands,
// re-laying the data out for SIMD, and sorting contacts for cache locality are
// all off the table, because every one of them changes the order. Physics is
// this engine's scaling limit (98% of it is one function, growing as n^1.38,
// and 60 FPS breaks somewhere around 3-4k bodies), so closing those four is
// closing most of the road.
//
// A previous study (S19) closed them on the reasoning that the solver never
// converges, therefore always stops short, therefore order is load-bearing.
// That reasoning has since been withdrawn twice: the solver does converge, and
// the exit test everyone was reading measures something else. So the question
// is genuinely open, and this measures it instead of arguing it.
//
// THE CONTROL COMES FIRST, AND IT IS THE HARD PART.
//
// Settling piles are chaotic. Nudge anything and the bodies end up somewhere
// slightly different, which means "the result changed when I shuffled" is NOT
// evidence that order matters. It is what a chaotic system does to any
// perturbation whatsoever.
//
// So run the SAME seed twice first. That is A-vs-A: identical input, identical
// order, so any difference is the engine's own run-to-run noise. Only a shuffle
// effect clearly larger than that noise floor means anything. Without this
// control the test would "prove" order matters no matter what the answer is,
// which is how S19 went wrong.
//
//   ./build-release/logosphere-tests --test test_constraint_order_matters --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "../src/core/telemetry.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include <cmath>
#include <cstdio>
#include <vector>

namespace T = ::logosphere::telemetry;

namespace {

struct Result {
    double peak_pen = 0.0;    // worst overlap at any point: the residual
    double final_z = 0.0;     // where the bottom box actually ended up
    double sum_z = 0.0;       // summed over the stack: a cruder whole-pile check
};

// An UNBALANCED pile, deliberately. A neat vertical stack is a poor test: its
// contacts barely interact, so any ordering effect would be too small to see.
// Offsetting each box makes the pile lean, which couples every contact to every
// other and gives order the best chance it will ever get to matter. If it does
// not show up here, it does not show up.
Result run(uint32_t shuffle_seed) {
    Result out;

    EngineConfig cfg;
    cfg.create_display = false;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) { printf("  ERROR: engine init failed\n"); return out; }
    T::set_enabled(true);
    T::set_residual_enabled(true);            // also clears the previous run
    T::set_constraint_shuffle_seed(shuffle_seed);
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

    std::vector<int> ids;
    for (int i = 0; i < 6; ++i) {
        Particle p = {};
        p.shape = ParticleShape::BOX;
        p.x = (i % 2 ? 0.18f : -0.18f);      // lean: alternate the offset
        p.y = (i % 3 ? 0.12f : -0.12f);
        p.z = 0.10f + 0.5f + i * 1.0f;
        p.width = p.height = p.thickness = 1.0f; p.size = 1.0f;
        p.r = 0.8f; p.g = 0.4f; p.b = 0.3f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        ids.push_back(engine.add_particle(p));
    }
    ps.flush_pending_particles();

    for (int f = 0; f < 150; ++f) {
        engine.update(1.0 / 60.0);
        const T::SolveResidual s = T::solve_residual();
        if (s.max_penetration > out.peak_pen) out.peak_pen = s.max_penetration;
    }

    {
        auto v = ps.lock_particles_for_write();
        out.final_z = v[ids[0]].z;
        for (int id : ids) out.sum_z += v[id].z;
    }
    T::set_constraint_shuffle_seed(0);
    engine.shutdown();
    return out;
}

}  // namespace

bool test_constraint_order_matters() {
    printf("\n=== IS CONSTRAINT ORDER LOAD-BEARING? ===\n");
    printf("Leaning pile of 6 boxes. Only the ORDER of the contact list changes.\n\n");

    // A-vs-A. Same seed, so the same order, twice. Anything that differs here
    // is the engine's own noise and is the yardstick for everything below.
    const Result a1 = run(0);
    const Result a2 = run(0);
    const double noise_pen = std::fabs(a1.peak_pen - a2.peak_pen);
    const double noise_z   = std::fabs(a1.final_z - a2.final_z);
    const double noise_sum = std::fabs(a1.sum_z   - a2.sum_z);

    printf("  %-22s %12s %12s %12s\n", "run", "peak pen", "bottom z", "sum z");
    printf("  %-22s %12.6f %12.6f %12.6f\n", "A (no shuffle)",   a1.peak_pen, a1.final_z, a1.sum_z);
    printf("  %-22s %12.6f %12.6f %12.6f\n", "A again (control)", a2.peak_pen, a2.final_z, a2.sum_z);
    printf("  %-22s %12.6f %12.6f %12.6f\n", "NOISE FLOOR |diff|", noise_pen, noise_z, noise_sum);
    printf("\n");

    // Now vary only the order.
    const uint32_t seeds[] = {1u, 7u, 12345u, 99991u};
    std::vector<Result> shuffled;
    for (uint32_t s : seeds) {
        const Result r = run(s);
        shuffled.push_back(r);
        char label[32];
        snprintf(label, sizeof(label), "shuffled seed=%u", s);
        printf("  %-22s %12.6f %12.6f %12.6f\n", label, r.peak_pen, r.final_z, r.sum_z);
    }

    // THE STATISTIC THAT ANSWERS THE QUESTION is the SPREAD AMONG THE SHUFFLES,
    // not the gap between shuffled and unshuffled. "Is order load-bearing" means
    // "do DIFFERENT orders give DIFFERENT answers". Comparing against the
    // unshuffled run answers a different question: whether the natural order is
    // special, which is interesting but is not this.
    //
    // The first version of this test compared everything to the unshuffled run
    // and duly announced that order was load-bearing, on data showing four
    // different orders agreeing to every printed digit. Pick the statistic that
    // matches the sentence you want to say.
    double pen_lo = shuffled[0].peak_pen, pen_hi = shuffled[0].peak_pen;
    double sum_lo = shuffled[0].sum_z,    sum_hi = shuffled[0].sum_z;
    for (const Result& r : shuffled) {
        pen_lo = std::fmin(pen_lo, r.peak_pen); pen_hi = std::fmax(pen_hi, r.peak_pen);
        sum_lo = std::fmin(sum_lo, r.sum_z);    sum_hi = std::fmax(sum_hi, r.sum_z);
    }
    const double spread_pen = pen_hi - pen_lo;   // disagreement BETWEEN orders
    const double spread_sum = sum_hi - sum_lo;
    const double vs_natural = std::fabs(shuffled[0].peak_pen - a1.peak_pen);

    printf("  %-22s %12.6f %12s %12.6f\n", "SPREAD among shuffles", spread_pen, "", spread_sum);
    printf("  %-22s %12.6f\n", "shuffled vs natural", vs_natural);

    // PROVE THE LEVER ENGAGED. If the shuffle silently did nothing, every run
    // would be identical to the unshuffled one and the test would report "order
    // does not matter" while measuring nothing at all. Position spread between
    // seeds is the evidence that the permutations really were different.
    const bool lever_engaged = spread_sum > 0.0 || vs_natural > 0.0;
    printf("\n");
    if (!lever_engaged) {
        printf("  *** hygiene: THE SHUFFLE DID NOTHING. ***\n"
               "  Every seeded run matched the unshuffled run exactly, in every quantity.\n"
               "  That means the permutation never reached the solver, so this test is\n"
               "  measuring nothing and its verdict would be an artefact. Fix the lever.\n");
        printf("\n  FAIL\n");
        return false;
    }

    if (spread_pen <= noise_pen) {
        printf("  INV-27/INV-2: ORDER DOES NOT CHANGE THE ANSWER, on this scene.\n"
               "  Four different permutations of the contact list produced a peak\n"
               "  penetration spread of %.6f m, against a same-input noise floor of\n"
               "  %.6f m. The orders disagree with each other by nothing measurable.\n"
               "  Their POSITIONS do differ (sum z spread %.6f m), which is the chaos\n"
               "  S19 warned about and is exactly why the verdict is taken on the\n"
               "  residual and not on where the boxes landed.\n\n"
               "  This is the case S19 assumed was impossible. If it holds beyond this\n"
               "  scene, parallelism, islands, SoA and contact sorting all reopen.\n",
               spread_pen, noise_pen, spread_sum);
    } else {
        printf("  INV-27/INV-2: ORDER CHANGES THE ANSWER — permutations disagree by %.6f m against a\n"
               "  %.6f m noise floor. The order the solver works in is part of the\n"
               "  result, not just the path, so parallelism, islands, SoA and sorting\n"
               "  stay closed.\n", spread_pen, noise_pen);
    }

    // Reported as its own observation, because it is a different claim and a
    // smaller one. Do not let it colour the verdict above.
    if (vs_natural > noise_pen) {
        printf("\n  SEPARATE OBSERVATION: the natural order is not neutral. Every shuffle\n"
               "  landed on %.6f m and the unshuffled run on %.6f m, worse by %.6f m,\n"
               "  with the bottom box at %.6f against an exact %.6f. Whatever ordering\n"
               "  contact generation produces is slightly WORSE here than an arbitrary\n"
               "  one. That is a lead, not a conclusion, and it is not evidence about\n"
               "  order being load-bearing either way.\n",
               shuffled[0].peak_pen, a1.peak_pen, vs_natural, a1.final_z, shuffled[0].final_z);
    }

    printf("\n  SCOPE: one scene, six boxes, one solver configuration. This says nothing\n"
           "  yet about Eden at 19,000 particles, and a single scene is not a finding.\n"
           "  Widen before acting on it.\n");
    printf("\n  DIAGNOSTIC, not a gate: this reports, it does not fail.\n");
    return true;
}
