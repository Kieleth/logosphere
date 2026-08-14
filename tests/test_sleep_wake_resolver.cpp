// =============================================================================
// SLEEP-AWAKE RESOLVER LADDER (INV-31): wake is solved physics
// =============================================================================
// Owner decree 2026-08-14 (LEDGER.md): "the awake moment needs to be
// calculated, or even pre-calculated; a grain of sand hit by a 1 m/s
// particle is not the same as a castle wall hit by a grain at 1 m/s."
//
// THE INSTRUMENT: the twin-scene invisibility check. Sleep is a cache
// over dynamics (owner ruling), and a cache has one sacred property —
// invisibility. So every rung runs the SAME impact twice: once with the
// target genuinely asleep, once with it held awake, and asserts the two
// outcomes agree within the engine's own quietness bound
// (REST_VELOCITY_THRESHOLD, the speed that admits a body into sleep).
// No expected values invented by the test: the awake twin IS the truth.
//
// Discovered by the Rube Goldberg machine, stage S3 (probe f271): an
// equal-mass strike at 1.67 m/s against a sleeper annihilated
// 343 kg*m/s in one frame because the pre-solve gate
// (m_a/(m_a+m_s))*v >= WAKE_TRANSFER_SPEED refused the wake and the
// row priced the sleeper as the turtle.
//
// RED-FIRST. Rungs 1-2 are expected RED until the resolver (task #56)
// lands. Rung 3 documents the one case the old gate gets right today.
// Rung 4 guards the other side: resting stacks must STAY asleep — the
// resolver may not buy correctness with wake-thrash.
//
// Usage:
//   ./build-release/logosphere-tests --test test_sleep_wake_resolver --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include "generated/physics_constants.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace {
namespace swr {

using PhysicsV4::REST_VELOCITY_THRESHOLD;

// Invisibility tolerance: the quietness bound that admits a body into
// sleep, plus numerical slop for two runs of a settled scene.
constexpr float TOL = REST_VELOCITY_THRESHOLD + 0.05f;

struct Outcome {
    float striker_vx_final = 0.0f;
    float target_vx_final  = 0.0f;
    float p_before = 0.0f, p_after = 0.0f;
    float m_striker = 0.0f, m_target = 0.0f;
    bool  target_was_asleep_at_impact = false;
    bool  ok = false;
};

struct Impact {
    float striker_size; Materials::Type striker_mat;
    float target_size;  Materials::Type target_mat;
    float strike_speed;
    float floor_friction = 0.02f;
};

Outcome run_impact(const Impact& im, bool target_sleeps) {
    Outcome out;
    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    if (engine.initialize(cfg) != 0) return out;

    auto& ps = engine.get_particle_system();

    auto add_box = [&](float x, float z, float size, Materials::Type mat) {
        Particle p = {};
        p.shape = ParticleShape::BOX;
        p.x = x; p.y = 0.0f; p.z = z;
        p.width = p.height = p.thickness = size; p.size = size;
        p.r = 0.6f; p.g = 0.6f; p.b = 0.6f; p.a = 1.0f;
        p.SetMaterial(mat);
        return engine.add_particle(p);
    };

    // Iced 5x3 KINEMATIC floor: momentum stays readable. Tiles are
    // born with their real thickness — the strict turtle door aborts a
    // body whose bottom starts below z=0 (it caught this fixture's
    // first draft, which placed 1 m cubes at z=0.05).
    std::vector<int> floor_ids;
    for (int c = -2; c <= 2; ++c)
        for (int r = -1; r <= 1; ++r) {
            Particle t = {};
            t.shape = ParticleShape::BOX;
            t.x = c * 1.0f; t.y = r * 1.0f; t.z = 0.05f;
            t.width = 1.0f; t.height = 1.0f; t.thickness = 0.1f;
            t.size = 1.0f;
            t.r = t.g = t.b = 0.5f; t.a = 1.0f;
            t.SetMaterial(Materials::Type::STONE);
            floor_ids.push_back(engine.add_particle(t));
        }
    const int striker = add_box(-0.9f, 0.1f + im.striker_size / 2 + 0.002f,
                                im.striker_size, im.striker_mat);
    const int target  = add_box(0.9f, 0.1f + im.target_size / 2 + 0.002f,
                                im.target_size, im.target_mat);
    ps.flush_pending_particles();
    {
        auto v = ps.lock_particles_for_write();
        for (int id : floor_ids) {
            v[id].solver_mode = ParticleSolverMode::KINEMATIC;
            v[id].owner = ParticleOwner::DYNAMICS;
            v[id].is_at_rest = true;
            v[id].friction = im.floor_friction;  // min-combined per contact
        }
    }

    // Settle. Asleep path: wait for genuine sleep. Awake path: poke.
    for (int f = 0; f < 90; ++f) {
        if (!target_sleeps) {
            auto v = ps.lock_particles_for_write();
            v[target].is_at_rest = false;
        }
        engine.update(1.0 / 60.0);
    }
    float striker_mass, target_mass;
    {
        auto v = ps.lock_particles_for_write();
        out.target_was_asleep_at_impact = v[target].is_at_rest;
        striker_mass = v[striker].GetMass();
        target_mass  = v[target].GetMass();
        out.m_striker = striker_mass;
        out.m_target  = target_mass;
        v[striker].is_at_rest = false;
        v[striker].vx = im.strike_speed;
    }
    out.p_before = striker_mass * im.strike_speed;

    const bool probe = std::getenv("SWR_PROBE") != nullptr;
    for (int f = 0; f < 90; ++f) {
        if (!target_sleeps) {
            auto v = ps.lock_particles_for_write();
            v[target].is_at_rest = false;
        }
        engine.update(1.0 / 60.0);
        if (probe && !target_sleeps && f >= 48 && f <= 70) {
            auto v = ps.lock_particles_for_write();
            printf("      [swr f%d] striker vx=%+.4f lvf=%u | "
                   "target vx=%+.4f lvf=%u | p=%.2f\n",
                   f, v[striker].vx, (unsigned)v[striker].low_velocity_frames,
                   v[target].vx, (unsigned)v[target].low_velocity_frames,
                   v[striker].GetMass() * v[striker].vx +
                   v[target].GetMass() * v[target].vx);
        }
    }
    {
        auto v = ps.lock_particles_for_write();
        out.striker_vx_final = v[striker].vx;
        out.target_vx_final  = v[target].vx;
        out.p_after = striker_mass * v[striker].vx +
                      target_mass * v[target].vx;
    }
    out.ok = true;
    engine.shutdown();
    return out;
}

int tests_passed = 0, tests_failed = 0;
void check(bool ok, const char* what) {
    if (ok) { ++tests_passed; }
    else    { ++tests_failed; printf("  FAIL: %s\n", what); }
}

// One rung = one impact, twice, compared. The awake twin is the truth.
bool rung(const char* name, const Impact& im, bool expect_red_today) {
    const Outcome asleep = run_impact(im, /*target_sleeps=*/true);
    const Outcome awake  = run_impact(im, /*target_sleeps=*/false);
    if (!asleep.ok || !awake.ok) { check(false, "engine init"); return false; }

    const float d_striker = std::fabs(asleep.striker_vx_final -
                                      awake.striker_vx_final);
    const float d_target  = std::fabs(asleep.target_vx_final -
                                      awake.target_vx_final);
    const bool invisible = d_striker <= TOL && d_target <= TOL;

    printf("  %-28s asleep: striker %+7.3f target %+7.3f (was_asleep=%d)\n",
           name, asleep.striker_vx_final, asleep.target_vx_final,
           (int)asleep.target_was_asleep_at_impact);
    printf("  %-28s awake:  striker %+7.3f target %+7.3f  "
           "| d=(%.3f, %.3f) tol=%.3f -> %s%s\n",
           "", awake.striker_vx_final, awake.target_vx_final,
           d_striker, d_target, TOL, invisible ? "INVISIBLE" : "CACHE LEAK",
           expect_red_today && !invisible ? "  [expected red until INV-31]"
                                          : "");
    // PHYSICAL ANCHOR (INV-7, INV-3): the awake twin is the truth, and
    // the truth must conserve momentum up to what the KINEMATIC floor
    // may legitimately drink through friction over the 1.5 s window.
    // This caught what invisibility alone missed: R1's twins agreed
    // perfectly at zero — equal-mass annihilation in BOTH worlds.
    const float friction_budget = (awake.m_striker + awake.m_target) *
                                  0.02f * 9.81f * 1.5f;
    const bool conserves = std::fabs(awake.p_after) >=
                           awake.p_before - friction_budget - 5.0f;
    printf("  %-28s momentum: before %.1f  after asleep %.1f  "
           "after awake %.1f kg*m/s (floor budget %.1f) -> %s%s\n",
           "", awake.p_before, asleep.p_after, awake.p_after,
           friction_budget, conserves ? "CONSERVED" : "DESTROYED",
           expect_red_today && !conserves ? "  [expected red]" : "");
    check(conserves, "momentum conserved through the awake impact");
    check(asleep.target_was_asleep_at_impact,
          "target actually slept before impact (fixture validity)");
    check(invisible, name);
    return invisible;
}

}  // namespace swr
}  // namespace

bool test_sleep_wake_resolver() {
    using namespace swr;
    printf("\n=== SLEEP-AWAKE RESOLVER (INV-31): the twin-scene "
           "invisibility ladder ===\n");
    printf("  quietness bound (REST_VELOCITY_THRESHOLD) = %.2f m/s, "
           "tol = %.2f\n\n", REST_VELOCITY_THRESHOLD, TOL);

    // R1 — the machine's f271, distilled: equal masses, 1.67 m/s.
    rung("R1 equal-mass strike",
         {0.4f, Materials::Type::WOOD_HARD, 0.4f, Materials::Type::WOOD_HARD,
          1.67f}, /*expect_red_today=*/true);

    // R2 — the castle hits the sleeping grain at 0.9 m/s: below the old
    // 1.0 gate by construction, and the grain MUST fly regardless.
    rung("R2 castle -> sleeping grain",
         {1.2f, Materials::Type::STONE, 0.08f, Materials::Type::WOOD_SOFT,
          0.9f}, /*expect_red_today=*/true);

    // R3 — the grain taps the sleeping castle at 1.0 m/s: the castle
    // may sleep through it, and the grain's fate must match the awake
    // world anyway. The one case the old gate handles today.
    rung("R3 grain -> sleeping castle",
         {0.08f, Materials::Type::WOOD_SOFT, 1.2f, Materials::Type::STONE,
          1.0f}, /*expect_red_today=*/false);

    // R5 — THE ANALYTIC PROVER (INV-7, INV-20). Equal masses, both
    // awake, frictionless floor: a perfectly inelastic 1-D collision
    // must leave BOTH bodies at exactly half the strike speed, and the
    // momentum must be conserved outright. This rung exists because a
    // trace misreading (2026-08-14) accused the manifold split of a
    // 3.5x overshoot: the per-row eff logged is the ALREADY-SPLIT
    // share, and four rows each removing 1/4 of the remaining approach
    // is the correct Gauss-Seidel signature (1 + .75 + .5625 + ...
    // converges to eff_full * v_rel). If pricing were ever wrong by a
    // factor, this rung reports it as a velocity, not a suspicion.
    {
        const Impact im{0.4f, Materials::Type::WOOD_HARD,
                        0.4f, Materials::Type::WOOD_HARD, 1.6f, 0.0f};
        const Outcome o = run_impact(im, /*target_sleeps=*/false);
        const float want = im.strike_speed * 0.5f;
        const float ds = std::fabs(o.striker_vx_final - want);
        const float dt_ = std::fabs(o.target_vx_final - want);
        const float p_err = std::fabs(o.p_after - o.p_before) /
                            std::max(1.0f, o.p_before);
        printf("  R5 analytic inelastic      striker %+7.4f target %+7.4f "
               "(want %+.4f each)\n", o.striker_vx_final,
               o.target_vx_final, want);
        printf("  %-28s momentum %.2f -> %.2f (%.2f%% error), "
               "frictionless\n", "", o.p_before, o.p_after,
               100.0f * p_err);
        check(ds < 0.08f && dt_ < 0.08f,
              "R5: equal masses share the approach speed exactly");
        check(p_err < 0.03f, "R5: momentum conserved through the manifold");
    }

    // R4 — the other side of the law: a resting stack STAYS asleep.
    // The resolver may not buy wake-correctness with wake-thrash.
    {
        Engine engine;
        EngineConfig cfg;
        cfg.create_display = false;
        cfg.enable_chat_window = false;
        cfg.show_debug_overlay = false;
        if (engine.initialize(cfg) == 0) {
            auto& ps = engine.get_particle_system();
            auto mk = [&](float z) {
                Particle p = {};
                p.shape = ParticleShape::BOX;
                p.x = 0; p.y = 0; p.z = z;
                p.width = p.height = p.thickness = 0.5f; p.size = 0.5f;
                p.r = p.g = p.b = 0.6f; p.a = 1.0f;
                p.SetMaterial(Materials::Type::WOOD_SOFT);
                return engine.add_particle(p);
            };
            Particle f = {};
            f.shape = ParticleShape::BOX;
            f.x = 0; f.y = 0; f.z = 0.05f;
            f.width = f.height = 1.5f; f.thickness = 0.1f; f.size = 1.5f;
            f.r = f.g = f.b = 0.5f; f.a = 1.0f;
            f.SetMaterial(Materials::Type::STONE);
            const int floor_id = engine.add_particle(f);
            const int lo = mk(0.1f + 0.25f + 0.002f);
            const int hi = mk(0.1f + 0.75f + 0.006f);
            ps.flush_pending_particles();
            {
                auto v = ps.lock_particles_for_write();
                v[floor_id].solver_mode = ParticleSolverMode::KINEMATIC;
                v[floor_id].owner = ParticleOwner::DYNAMICS;
                v[floor_id].is_at_rest = true;
            }
            int asleep_frames = 0;
            for (int fr = 0; fr < 300; ++fr) {
                engine.update(1.0 / 60.0);
                auto v = ps.lock_particles_for_write();
                if (fr >= 100) {
                    check(true, "");  --tests_passed;   // counted below
                    if (v[lo].is_at_rest && v[hi].is_at_rest) ++asleep_frames;
                }
            }
            printf("  R4 resting stack             asleep %d/200 frames "
                   "after settling\n", asleep_frames);
            check(asleep_frames >= 190,
                  "R4: a resting stack stays asleep (no thrash)");
            engine.shutdown();
        }
    }

    printf("\n  Passed: %d  Failed: %d  (R1-R2 red is the INV-31 "
           "frontier, not a regression)\n", tests_passed, tests_failed);
    return tests_failed == 0;
}
