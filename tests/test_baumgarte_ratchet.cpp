// ============================================================================
// THE BAUMGARTE RATCHET — position repair must not inject momentum
// ============================================================================
// THE LAW THIS ENCODES. A position error is geometry. Repairing it may move
// bodies, but it must not LEAVE them moving: correction velocity is borrowed
// for the repair and given back, never deposited into momentum. An engine that
// deposits it has built a motor, and any constraint that cannot reach zero
// error runs that motor forever.
//
// THE DEFECT, measured with the solver's own canary in Eden (P3946, frame 1):
//
//   F1 I0  v_rel=-0.01  impulse=-1.03  dvz=+3.90   <- constraint SATISFIED at
//   F1 END vel=(3.36,-3.36,3.73)                      velocity level; impulse
//   F2 I0  v_rel=-0.15             dvz=+3.76          fired anyway, from bias
//   F3 I0  v_rel=-0.20             dvz=+3.71
//   MAX_UP: vz 3.73 -> 6.62 -> 12.17 -> 20.07 -> 30.98 -> 44.94 -> 60.75
//
// The solver converges perfectly every frame — to v_rel = -4.0, which is
// GLUON_MAX_BIAS_VELOCITY. The bias is injected into REAL velocity, velocity
// carries across frames, so any bond that cannot reach zero error adds ~4 m/s
// per interface per frame and the chain becomes a rocket.
// physics_system_v4.cpp:2919 says it plainly: "Only turtle contacts use split
// impulse - box contacts still have bias."
//
// THREE SCENARIOS, one per bias path, each asking the same two questions:
// does the error get REPAIRED (the fix must not cheat by deleting correction),
// and does the repair end QUIET (no deposited momentum)?
//
//   1 POP     two deeply overlapping free boxes on a floor. Contact bias only.
//             They must end separated AND slow. Today they pop apart
//             ballistically at up to the 4 m/s cushion.
//   2 SLING   a body bonded to a kinematic anchor, released with its bond
//             STRETCHED but within the tear limit. Gluon bias only. It must
//             arrive AND hang quiet.
//             The first draft released it 2 m from a 0.5 m target — 4.7x rest
//             length against OrganicGluon::max_strain_ratio() of 2.0. The bond
//             correctly TORE, and the test then measured a body in free fall
//             and called the solver broken. Traced with CANARY_PID on the body:
//             frames 481-484 show the rows solved and repair lifting it
//             0.0333 m per frame, z 0.500 -> 0.633; at frame 485 the rows
//             VANISH and velocity becomes pure gravity, -0.082, -0.163, -0.245.
//             The engine was right and the scenario was asking a fibre to
//             stretch five times its length.
//   3 ROCKET  a rooted column of five boxes whose bonds are COMPRESSED to a
//             third of their target, so every bias pushes its pair apart with
//             an error that never closes. The column must settle. Today it
//             launches. NOTE the first draft of this scenario used overlapping
//             boxes bonded at their placed distance, expecting bond-versus-
//             contact conflict, and measured 0.000 everywhere: physics_system_
//             v4.cpp:710 skips contact generation for ANY pair a gluon owns.
//             Eden's ratchet is gluon-only and the canary agrees — the rows
//             converging to -4.00 are the three gluon axis rows.
//
//   ./build-release/logosphere-tests --test test_baumgarte_ratchet --no-head
// ============================================================================

#include "../src/core/engine.h"
#include "../src/core/particle_system.h"
#include "../src/materials.h"
#include "../src/particle_core.h"
#include "logosphere/physics/physics_system.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

namespace {

bool start(Engine& engine) {
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    return engine.initialize(cfg) == 0;
}

void add_floor(Engine& engine, int half) {
    auto& ps = engine.get_particle_system();
    for (int cx = -half; cx <= half; ++cx)
        for (int cy = -half; cy <= half; ++cy) {
            Particle t = {};
            t.shape = ParticleShape::BOX;
            t.x = (float)cx; t.y = (float)cy; t.z = 0.05f;
            t.width = t.height = 1.0f; t.thickness = 0.1f; t.size = 1.0f;
            t.SetMaterial(Materials::Type::STONE);
            const int id = engine.add_particle(t);
            ps.flush_pending_particles();
            auto v = ps.lock_particles_for_write();
            v[id].solver_mode = ParticleSolverMode::KINEMATIC;
            v[id].owner = ParticleOwner::DYNAMICS;
            v[id].is_at_rest = true;
        }
}

int add_box(Engine& engine, float x, float y, float z, float s,
            Materials::Type mat) {
    Particle p = {};
    p.shape = ParticleShape::BOX;
    p.x = x; p.y = y; p.z = z;
    p.width = p.height = p.thickness = s; p.size = s;
    p.r = 0.6f; p.g = 0.5f; p.b = 0.4f; p.a = 1.0f;
    p.SetMaterial(mat);
    return engine.add_particle(p);
}

// The organic bond recipe: zero offsets, placed centre distance as target.
void bond(Engine& engine, int a, int b, float target,
          float stiffness, float damping) {
    auto g = std::make_unique<OrganicGluon>();
    g->offset_a = {0.0f, 0.0f, 0.0f};
    g->offset_b = {0.0f, 0.0f, 0.0f};
    g->target_distance = target;
    g->rotate_offsets = false;
    g->contact_area = 1.0f;
    g->stiffness = stiffness;
    g->damping = damping;
    g->enable_angular_constraint = false;
    engine.get_physics_system().add_gluon_between(a, b, std::move(g));
}

struct Motion {
    float peak = 0.0f;    // fastest any watched body ever moved
    float final_v = 0.0f; // fastest watched body over the last 60 frames
};

Motion run_watching(Engine& engine, const std::vector<int>& ids, int frames) {
    Motion m;
    auto& ps = engine.get_particle_system();
    for (int f = 0; f < frames; ++f) {
        engine.update(1.0 / 60.0);
        float worst = 0.0f;
        {
            auto v = ps.lock_particles_for_write();
            for (int id : ids) {
                if (id < 0 || (size_t)id >= v.size()) continue;
                const Particle& p = v[id];
                worst = std::fmax(worst, std::sqrt(p.vx*p.vx + p.vy*p.vy +
                                                   p.vz*p.vz));
            }
        }
        m.peak = std::fmax(m.peak, worst);
        if (f >= frames - 60) m.final_v = std::fmax(m.final_v, worst);
    }
    return m;
}

int failures = 0;
void judge(const char* what, bool ok, float got, float want,
           const char* unit) {
    printf("      %-52s %8.3f %s (want %s %.3f)  %s\n",
           what, got, unit, ok ? "<=" : "<=", want, ok ? "ok" : "*** RED ***");
    if (!ok) failures++;
}

} // namespace

bool test_baumgarte_ratchet() {
    printf("\n=== THE BAUMGARTE RATCHET: position repair must not inject momentum ===\n");

    // ---- 1. POP — contact bias alone --------------------------------------
    {
        printf("\n  [1] POP: two deeply overlapped boxes must separate QUIETLY\n");
        Engine engine;
        if (!start(engine)) { printf("  engine init failed\n  FAIL\n"); return false; }
        auto& ps = engine.get_particle_system();
        add_floor(engine, 2);

        // 0.30 m boxes overlapped by half: centres 0.15 m apart.
        //
        // THE OVERLAP IS DRIVEN IN, NOT BORN (INV-37, owner decree
        // 2026-09-01). Spawning the pair already interlocked is a creation in
        // overlap, which the door refuses: this scenario would have had one
        // box to watch instead of two. They are BORN clear, half a metre
        // apart, and the second is then written into the first before a
        // single step runs. An external writer handing the solver a state it
        // would never have produced is INV-30's case and this fixture is
        // doing it deliberately; the solver still sees the same two boxes,
        // the same 0.15 m inside each other, on frame one.
        const int a = add_box(engine, -0.075f, 0.0f, 0.26f, 0.30f,
                              Materials::Type::WOOD_HARD);
        const int b = add_box(engine, +0.575f, 0.0f, 0.26f, 0.30f,
                              Materials::Type::WOOD_HARD);
        ps.flush_pending_particles();
        {
            auto v = ps.lock_particles_for_write();
            v[b].x = +0.075f;
            ps.mark_bvh_dirty();
        }

        const Motion m = run_watching(engine, {a, b}, 240);
        float gap = 0.0f;
        {
            auto v = ps.lock_particles_for_write();
            gap = std::fabs(v[b].x - v[a].x);
        }
        printf("      centre distance at end: %.3f m (full separation at 0.300)\n", gap);
        // Repaired means it ends AT separation, not flung past it. The first
        // version only asked gap >= 0.295 and passed on a 0.960 m overshoot,
        // which is the defect wearing the costume of a fix.
        judge("repair happened AND stopped there (0.295..0.350 m)",
              gap >= 0.295f && gap <= 0.350f, gap, 0.350f, "m ");
        judge("peak real speed during separation", m.peak <= 1.0f, m.peak, 1.0f, "m/s");
        judge("still moving at the end", m.final_v <= 0.30f, m.final_v, 0.30f, "m/s");
        engine.shutdown();
    }

    // ---- 2. SLING — gluon bias alone --------------------------------------
    {
        printf("\n  [2] SLING: a stretched bond must close QUIETLY (within tear limit)\n");
        Engine engine;
        if (!start(engine)) { printf("  engine init failed\n  FAIL\n"); return false; }
        auto& ps = engine.get_particle_system();

        const int anchor = add_box(engine, 0.0f, 0.0f, 3.0f, 0.30f,
                                   Materials::Type::STONE);
        const int body   = add_box(engine, 0.0f, 0.0f, 1.20f, 0.30f,
                                   Materials::Type::WOOD_SOFT);
        ps.flush_pending_particles();
        {
            auto v = ps.lock_particles_for_write();
            v[anchor].solver_mode = ParticleSolverMode::KINEMATIC;
            v[anchor].owner = ParticleOwner::DYNAMICS;
            v[anchor].is_at_rest = true;
        }
        // Anchor at z=3.0, body released at z=1.2, so the bond starts at 1.8 m
        // against a 1.0 m target: stretched 1.8x, inside the 2.0x tear ratio.
        // 0.8 m of position error to repair, and the bond must survive doing it.
        bond(engine, anchor, body, 1.0f, 50000.0f, 1000.0f);

        const Motion m = run_watching(engine, {body}, 300);
        float dist = 0.0f;
        {
            auto v = ps.lock_particles_for_write();
            const float dx = v[body].x - v[anchor].x;
            const float dy = v[body].y - v[anchor].y;
            const float dz = v[body].z - v[anchor].z;
            dist = std::sqrt(dx*dx + dy*dy + dz*dz);
        }
        printf("      bond length at end: %.3f m (target 1.000, released at 1.800)\n", dist);
        {   // Why did it not arrive? Name the state instead of guessing.
            auto v = ps.lock_particles_for_write();
            printf("      body: z %.3f  at_rest %d  mode %d  mass %.4f kg\n",
                   v[body].z, (int)v[body].is_at_rest,
                   (int)v[body].solver_mode, v[body].GetMass());
            printf("      anchor: z %.3f  mode %d\n",
                   v[anchor].z, (int)v[anchor].solver_mode);
        }
        judge("repair happened: within 5%% of target",
              std::fabs(dist - 1.0f) <= 0.05f, dist, 1.05f, "m ");
        judge("peak real speed during the 2 m repair", m.peak <= 1.0f,
              m.peak, 1.0f, "m/s");
        judge("still moving at the end", m.final_v <= 0.10f, m.final_v, 0.10f, "m/s");
        engine.shutdown();
    }

    // ---- 3. ROCKET — the Eden composite -----------------------------------
    {
        printf("\n  [3] ROCKET: a rooted column of bonded OVERLAPPING boxes must settle\n");
        Engine engine;
        if (!start(engine)) { printf("  engine init failed\n  FAIL\n"); return false; }
        auto& ps = engine.get_particle_system();
        add_floor(engine, 2);

        // A COMPRESSED column. Five 0.30 m boxes placed 0.20 m apart, every
        // bond asking for 0.60 m: each bond is squashed to a third of its
        // target and its bias pushes the pair APART, hard, every frame.
        //
        // Why compression and not the obvious overlap-plus-contact fight:
        // physics_system_v4.cpp:710 skips contact generation entirely for any
        // pair a gluon owns ("gluon pulls together, contact pushes apart"), so
        // a bonded overlapping pair generates no conflict at all — the first
        // version of this scenario measured 0.000 everywhere for exactly that
        // reason. Eden's ratchet is gluon-only, and the canary agrees: the
        // rows that converge to v_rel = -4.00 on P3945<->P3946 are the three
        // GLUON axis rows, not contacts. This reproduces that and nothing else.
        constexpr float S = 0.30f, STEP = 0.20f, TARGET = 0.60f;
        // BORN AT THEIR BONDS' TARGET, THEN COMPRESSED (INV-37, owner decree
        // 2026-09-01). The column used to be SPAWNED at STEP, which is a
        // birth 0.10 m inside the box below, and the creation door refuses
        // it: four of the five boxes would never have existed. They are born
        // TARGET apart - the spacing their bonds ask for, so nothing is
        // created in overlap - and then squashed onto STEP before a single
        // step runs, which is the same INV-30 external write the other two
        // scenarios use. The solver still sees five boxes 0.20 m apart on
        // bonds asking 0.60 m, on frame one.
        std::vector<int> col;
        for (int i = 0; i < 5; ++i)
            col.push_back(add_box(engine, 0.0f, 0.0f, 0.30f + TARGET * (float)i,
                                  S, Materials::Type::WOOD_HARD));
        ps.flush_pending_particles();
        {
            auto v = ps.lock_particles_for_write();
            for (int i = 0; i < 5; ++i)
                v[col[i]].z = 0.30f + STEP * (float)i;
            ps.mark_bvh_dirty();
            v[col[0]].solver_mode = ParticleSolverMode::KINEMATIC;   // rooted
            v[col[0]].owner = ParticleOwner::DYNAMICS;
            v[col[0]].is_at_rest = true;
        }
        for (int i = 0; i + 1 < 5; ++i)
            bond(engine, col[i], col[i + 1], TARGET, 50000.0f, 1000.0f);

        std::vector<float> z0;
        {
            auto v = ps.lock_particles_for_write();
            for (int id : col) z0.push_back(v[id].z);
        }

        const Motion m = run_watching(engine, col, 240);
        float max_rise = 0.0f, worst_spacing_err = 0.0f;
        {
            auto v = ps.lock_particles_for_write();
            for (size_t i = 0; i < col.size(); ++i)
                max_rise = std::fmax(max_rise, v[col[i]].z - z0[i]);
            for (size_t i = 0; i + 1 < col.size(); ++i)
                worst_spacing_err = std::fmax(worst_spacing_err,
                    std::fabs((v[col[i+1]].z - v[col[i]].z) - TARGET));
        }
        printf("      largest rise of any segment: %.3f m\n", max_rise);
        printf("      worst bond spacing error: %.3f m (target %.2f)\n",
               worst_spacing_err, TARGET);
        // NOT a cap on how far the column may rise. Four bonds each compressed
        // by 0.40 m MUST expand by 1.6 m at the top — that rise is the repair
        // succeeding. The first version asserted max_rise <= 0.50 and would
        // have called a correct result a failure. What matters is that the
        // repair lands on target and that it arrives without momentum.
        judge("repair happened: bonds reach their target spacing",
              worst_spacing_err <= 0.05f, worst_spacing_err, 0.05f, "m ");
        judge("peak real speed in the column", m.peak <= 1.0f, m.peak, 1.0f, "m/s");
        judge("still moving at the end", m.final_v <= 0.10f, m.final_v, 0.10f, "m/s");
        engine.shutdown();
    }

    const bool pass = (failures == 0);
    printf("\n  %d violation(s)\n", failures);
    printf("\n  %s\n", pass ? "PASS"
        : "FAIL (position repair is being deposited into momentum)");
    return pass;
}
