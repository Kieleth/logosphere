// =============================================================================
// POSITION AUTHORITY — one integrator per particle, by contract
// =============================================================================
// The ownership contract (src/particle_types.h): the physics solver
// integrates solver-DYNAMIC particles; a DYNAMICS-owned (KINEMATIC)
// particle is moved by its dynamics system; ANIMATION-owned particles
// are FK-written. Nothing else moves particles.
//
// Found 2026-07-09 (the cap-double-advance RCA notes +
// this branch's RCA): ParticleSystem::update was a main.mm relic that
// Euler-integrated x/y from vx/vy for EVERY particle, unconditionally,
// every frame. Every particle with sustained horizontal velocity moved
// ~2x its velocity field: humanoid bodies (legacy loop + the locomotion
// shape pipeline) and solver-DYNAMIC bodies (legacy loop + the solver).
// The FORGE speed cap therefore did not bind actual ground speed.
//
// LAWS (assert-protocol migration, 2026-08-21). The central claim of this
// file — that exactly ONE integrator advances a given particle's position in
// a given frame — has no registry record. It is written up as PROPOSED
// ONE-POSITION-WRITER in tests/invariants/INV_PROPOSALS.md; a1, a2 and b's
// velocity-field term cite it. a4 is INV-7, the momentum door, verbatim: a
// KINEMATIC endpoint cannot receive momentum from any subsystem, gluons
// included. a3 and a5 are engine bookkeeping rather than physics and are
// tagged hygiene, with the reason in the line itself.
//
// These contracts pin the fixed world:
//   a1  an unowned KINEMATIC, DYNAMICS-owned particle with velocity
//       must NOT move (no authority, no motion).
//   a2  a solver-DYNAMIC particle's HORIZONTAL displacement is exactly
//       v·t (one 1/30 physics-step tolerance; physics ticks at 30 Hz
//       via the fixed-timestep accumulator; default gravity pulls it
//       down in z, which is orthogonal to the x contract).
//   b   a registered humanoid's measured ground speed equals the
//       commanded speed when sub-cap, equals the cap when commanded
//       above it (both TWO-SIDED), and hips vx/vy reports true ground
//       speed.
//
// Run: ./build/logosphere-tests --test test_position_authority --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/worldgen/worldgen_system.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <memory>

namespace {

struct EngineBox {
    Engine engine;
    bool ok = false;
    EngineBox(const char* title) {
        EngineConfig cfg;
        cfg.create_display = false;
        cfg.window_title = title;
        cfg.enable_chat_window = false;
        ok = (engine.initialize(cfg) == 0);
    }
};

} // namespace

bool test_position_authority() {
    printf("\n=== Position Authority (one integrator per particle) ===\n");
    const float dt = 1.0f / 60.0f;
    bool all_ok = true;

    // ------------------------------------------------------------------
    // a1 — no authority, no motion.
    // ------------------------------------------------------------------
    {
        EngineBox box("position authority a1");
        if (!box.ok) { printf("  ERROR: engine init failed (a1)\n"); return false; }
        auto& ps = box.engine.get_particle_system();

        Particle p = {};
        p.shape = ParticleShape::BOX;
        p.x = 0.0f; p.y = 0.0f; p.z = 5.0f;
        p.width = 0.3f; p.height = 0.3f; p.thickness = 0.3f;
        p.size = 0.3f;
        p.r = 0.5f; p.g = 0.5f; p.b = 0.5f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        int id = box.engine.add_particle(p);
        {
            auto view = ps.lock_particles_for_write();
            view[id].solver_mode = ParticleSolverMode::KINEMATIC;
            view[id].owner = ParticleOwner::DYNAMICS;
            view[id].vx = 1.0f;
            view[id].is_at_rest = false;
        }

        for (int f = 0; f < 60; ++f) box.engine.update(dt);

        float moved;
        {
            auto view = ps.lock_particles_for_read();
            moved = std::sqrt(view[id].x * view[id].x + view[id].y * view[id].y);
        }
        bool a1 = moved < 0.001f;
        printf("  %s: a1 PROPOSED ONE-POSITION-WRITER: unowned KINEMATIC particle stays put (moved %.4f m in 1 s, vx=1)\n",
               a1 ? "PASS" : "FAIL", moved);
        all_ok = all_ok && a1;
    }

    // ------------------------------------------------------------------
    // a2 — solver-DYNAMIC x-displacement == v·t (free flight, default gravity).
    // ------------------------------------------------------------------
    {
        EngineBox box("position authority a2");
        if (!box.ok) { printf("  ERROR: engine init failed (a2)\n"); return false; }
        auto& ps = box.engine.get_particle_system();

        Particle p = {};
        p.shape = ParticleShape::SPHERE;
        p.x = 0.0f; p.y = 0.0f; p.z = 5.0f;
        p.width = 0.3f; p.height = 0.3f; p.thickness = 0.3f;
        p.size = 0.3f;   // SPHERE AABBs use size as the diameter
        p.r = 0.5f; p.g = 0.5f; p.b = 0.5f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        int id = box.engine.add_particle(p);
        {
            auto view = ps.lock_particles_for_write();
            view[id].solver_mode = ParticleSolverMode::DYNAMIC;
            view[id].vx = 1.0f;
            view[id].is_at_rest = false;
        }

        // Measure inside the awake window: the at-rest system force-sleeps
        // an isolated free-flying particle at ~60 frames (zeroing its
        // velocity mid-flight — pre-existing oddity, out of scope here).
        constexpr int FRAMES = 40;   // 2/3 s, even number of 1/30 ticks
        for (int f = 0; f < FRAMES; ++f) box.engine.update(dt);

        float x_end, vx_end;
        bool rest_end;
        {
            auto view = ps.lock_particles_for_read();
            x_end = view[id].x;
            vx_end = view[id].vx;
            rest_end = view[id].is_at_rest;
        }
        printf("  a2 diag: x_end=%.3f vx_end=%.3f at_rest=%d\n",
               x_end, vx_end, rest_end ? 1 : 0);
        const float expected = 1.0f * FRAMES * dt;   // 0.667 m
        const float tol = 1.0f / 30.0f + 0.005f;     // one physics step + eps
        bool a2 = std::fabs(x_end - expected) <= tol;
        printf("  %s: a2 PROPOSED ONE-POSITION-WRITER: solver-DYNAMIC displacement == v*t, integrated once (got %.3f m, expected %.3f +/- %.3f)\n",
               a2 ? "PASS" : "FAIL", x_end, expected, tol);
        all_ok = all_ok && a2;

        // a3 — writer-marks-BVH: solver motion must keep the shadow BVH
        // fresh in a humanoid-free scene (the deleted legacy loop was the
        // only motion-driven dirty-marker; integrate_positions owns it
        // now). The particle is falling under default gravity while
        // drifting in x, so query at its LIVE position; the BVH rebuild
        // lags marking by at most one frame (one extra update absorbs it).
        {
            box.engine.update(dt);  // let the last mark trigger a rebuild
            const BVH* bvh = ps.get_shadow_bvh();
            bool found_live = false, found_spawn = false;
            float live_x, live_y, live_z;
            {
                auto view = ps.lock_particles_for_read();
                live_x = view[id].x; live_y = view[id].y; live_z = view[id].z;
                const float half = 0.5f * view[id].size + 0.2f;  // stored AABB span + margin

                std::vector<int> hits;
                AABB q_live;
                q_live.min_x = live_x - half; q_live.max_x = live_x + half;
                q_live.min_y = live_y - half; q_live.max_y = live_y + half;
                q_live.min_z = live_z - half; q_live.max_z = live_z + half;
                bvh->query_aabb(q_live, view.get(), hits);
                for (int h : hits) if (h == id) found_live = true;

                // Thin probe at the spawn x (x=0) at live z: a stale BVH
                // (stored box still at spawn) would report the particle
                // here; a fresh one must not (live box starts at
                // live_x - 0.5*size ≈ 0.52 - 0.15 = 0.37 > 0.05).
                hits.clear();
                AABB q_spawn;
                q_spawn.min_x = -0.05f; q_spawn.max_x = 0.05f;
                q_spawn.min_y = -0.05f; q_spawn.max_y = 0.05f;
                q_spawn.min_z = live_z - half; q_spawn.max_z = live_z + half;
                bvh->query_aabb(q_spawn, view.get(), hits);
                for (int h : hits) if (h == id) found_spawn = true;
            }
            bool a3 = found_live && !found_spawn;
            printf("  %s: a3 hygiene (engine bookkeeping, not a physics law): shadow BVH tracks solver motion (at live pos (%.2f,%.2f,%.2f): %d, at spawn x: %d)\n",
                   a3 ? "PASS" : "FAIL", live_x, live_y, live_z,
                   found_live ? 1 : 0, found_spawn ? 1 : 0);
            all_ok = all_ok && a3;
        }
    }

    // ------------------------------------------------------------------
    // a4 — gluons must not write velocity into a KINEMATIC endpoint.
    //      KINEMATIC = infinite mass: the V4.9 material-damping COM pull
    //      must treat it as such, or a pinned anchor gets dragged to its
    //      dynamic partner's velocity, never rests, and (leaked) anchors
    //      pile up as an eternally-awake contact horde (found 2026-07-10:
    //      2,100 awake anchors = 21k constraints = 20% slow substeps in
    //      the strata walk).
    // ------------------------------------------------------------------
    {
        EngineBox box("position authority a4");
        if (!box.ok) { printf("  ERROR: engine init failed (a4)\n"); return false; }
        auto& ps = box.engine.get_particle_system();
        auto& physics = box.engine.get_physics_system();

        auto spawn = [&](float x, ParticleSolverMode mode) {
            Particle p = {};
            p.shape = ParticleShape::BOX;
            p.x = x; p.y = 0.0f; p.z = 5.0f;
            p.width = 0.3f; p.height = 0.3f; p.thickness = 0.3f;
            p.size = 0.3f;
            p.r = 0.5f; p.g = 0.5f; p.b = 0.5f; p.a = 1.0f;
            p.SetMaterial(Materials::Type::STONE);
            int pid = box.engine.add_particle(p);
            auto view = ps.lock_particles_for_write();
            view[pid].solver_mode = mode;
            view[pid].is_at_rest = false;
            return pid;
        };
        int kin = spawn(0.0f, ParticleSolverMode::KINEMATIC);
        int dyn = spawn(0.4f, ParticleSolverMode::DYNAMIC);
        {
            auto view = ps.lock_particles_for_write();
            view[dyn].vy = 2.0f;   // dynamic partner moving
        }
        auto g = std::make_unique<NailGluon>();
        g->offset_a = Vec3{0.0f, 0.0f, 0.0f};
        g->offset_b = Vec3{0.0f, 0.0f, 0.0f};
        g->target_distance = 0.4f;
        g->stiffness = 0.0f;
        g->damping = 0.0f;
        g->breaking_force = 1e9f;
        g->enable_angular_constraint = false;
        g->rotate_offsets = false;
        physics.add_gluon_between((size_t)kin, (size_t)dyn, std::move(g));

        for (int f = 0; f < 30; ++f) box.engine.update(dt);

        float kin_v;
        {
            auto view = ps.lock_particles_for_read();
            kin_v = std::sqrt(view[kin].vx * view[kin].vx +
                              view[kin].vy * view[kin].vy +
                              view[kin].vz * view[kin].vz);
        }
        bool a4 = kin_v < 0.001f;
        printf("  %s: a4 INV-7: gluon never writes velocity into a KINEMATIC endpoint (|v|=%.4f)\n",
               a4 ? "PASS" : "FAIL", kin_v);
        all_ok = all_ok && a4;
    }

    // ------------------------------------------------------------------
    // a5 — deferred particle deletions flush without a render pass.
    //      flush_safe_deletions historically ran only in render(); a
    //      headless engine (render-free profile, tests, servers) must
    //      still complete queued deletions or every queue-deleted
    //      particle (foot-plant pin anchors at ~2 per walk second)
    //      leaks forever.
    // ------------------------------------------------------------------
    {
        EngineBox box("position authority a5");
        if (!box.ok) { printf("  ERROR: engine init failed (a5)\n"); return false; }
        auto& ps = box.engine.get_particle_system();

        Particle p = {};
        p.shape = ParticleShape::BOX;
        p.x = 0.0f; p.y = 0.0f; p.z = 5.0f;
        p.width = 0.3f; p.height = 0.3f; p.thickness = 0.3f;
        p.size = 0.3f;
        p.r = 0.5f; p.g = 0.5f; p.b = 0.5f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        int pid = box.engine.add_particle(p);
        box.engine.update(dt);   // ensure it is live

        size_t count_before = ps.count();
        ps.queue_particle_deletion((size_t)pid, 0);
        for (int f = 0; f < 30; ++f) box.engine.update(dt);   // no render()
        size_t count_after = ps.count();

        bool a5 = (count_after == count_before - 1);
        printf("  %s: a5 hygiene (engine bookkeeping, not a physics law): queued deletion flushes headless (count %zu -> %zu)\n",
               a5 ? "PASS" : "FAIL", count_before, count_after);
        all_ok = all_ok && a5;
    }

    // ------------------------------------------------------------------
    // b — humanoid: commanded speed == ground speed (two-sided); cap
    //     binds; hips velocity reports true ground speed.
    // ------------------------------------------------------------------
    {
        EngineBox box("position authority b");
        if (!box.ok) { printf("  ERROR: engine init failed (b)\n"); return false; }
        auto& ps  = box.engine.get_particle_system();
        auto& hum = box.engine.get_humanoid_locomotion();

        Particle floor_p = {};
        floor_p.shape = ParticleShape::BOX;
        floor_p.x = 0; floor_p.y = 0; floor_p.z = 0.25f;
        floor_p.width = 400.0f; floor_p.height = 400.0f; floor_p.thickness = 0.5f;
        floor_p.r = 0.3f; floor_p.g = 0.5f; floor_p.b = 0.2f; floor_p.a = 1.0f;
        floor_p.SetMaterial(Materials::Type::STONE);
        floor_p.is_at_rest = true;
        box.engine.add_particle(floor_p);

        auto& hgen = box.engine.get_worldgen_system().get_humanoid_generator();
        auto eva = hgen.generate_humanoid_physics(
            0.0f, 0.0f, 0.55f, -1, HumanoidSpec::eva(), false);
        auto& kg = box.engine.get_kg();
        eva.create_kg_entities(kg, "Human", 180.0f, 800.0f);
        hum.register_humanoid_direct(
            eva.hips_id,
            eva.left_leg_ids, eva.right_leg_ids,
            eva.left_arm_ids, eva.right_arm_ids,
            eva.torso_ids, 180.0f, 800.0f,
            eva.entity_id);

        int hips_id = eva.hips_id;
        ps.add_swap_callback([&](size_t old_idx, size_t new_idx) {
            if (hips_id == (int)old_idx) hips_id = (int)new_idx;
        });

        const float cap = hum.get_effective_max_speed(eva.hips_id);
        printf("  effective max speed (FORGE cap): %.3f m/s\n", cap);
        if (cap < 0.5f) {
            printf("  [FAIL - cap unusable (%.3f); capability derivation broken]\n", cap);
            return false;
        }

        // One measured case: (commanded, expected ground speed).
        auto measure_case = [&](float commanded, float expected,
                                const char* label) -> bool {
            hum.set_volitional(eva.hips_id, true);
            hum.set_target_velocity(eva.hips_id, 0.0f, commanded);

            // Accel ramp + gait settle: skip 120 frames (2 s).
            for (int f = 0; f < 120; ++f) box.engine.update(dt);

            float x0, y0;
            {
                auto view = ps.lock_particles_for_read();
                x0 = view[hips_id].x; y0 = view[hips_id].y;
            }
            constexpr int MEASURE_FRAMES = 300;   // 5 s window
            float vel_field_sum = 0.0f;
            for (int f = 0; f < MEASURE_FRAMES; ++f) {
                box.engine.update(dt);
                auto view = ps.lock_particles_for_read();
                vel_field_sum += std::sqrt(view[hips_id].vx * view[hips_id].vx +
                                           view[hips_id].vy * view[hips_id].vy);
            }
            float x1, y1;
            {
                auto view = ps.lock_particles_for_read();
                x1 = view[hips_id].x; y1 = view[hips_id].y;
            }
            const float t = MEASURE_FRAMES * dt;
            const float ground_speed =
                std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)) / t;
            const float vel_field_avg = vel_field_sum / MEASURE_FRAMES;

            bool speed_ok = std::fabs(ground_speed - expected) <= 0.10f * expected;
            bool field_ok = std::fabs(vel_field_avg - ground_speed)
                            <= 0.15f * std::max(ground_speed, 0.1f);
            printf("  %s: %s hygiene (game-layer speed policy, not a physics law): ground speed %.3f m/s (expected %.3f +/- 10%%)\n",
                   speed_ok ? "PASS" : "FAIL", label, ground_speed, expected);
            printf("  %s: %s PROPOSED ONE-POSITION-WRITER: hips velocity field %.3f m/s reports ground speed %.3f (+/- 15%%) — a second integrator shows up here as a 2x disagreement\n",
                   field_ok ? "PASS" : "FAIL", label, vel_field_avg, ground_speed);
            return speed_ok && field_ok;
        };

        const float sub_cap = std::min(2.0f, 0.6f * cap);
        bool b1 = measure_case(sub_cap, sub_cap, "sub-cap");
        bool b2 = measure_case(2.0f * cap, cap, "over-cap");
        all_ok = all_ok && b1 && b2;
    }

    printf("  [%s]\n", all_ok ? "PASS"
        : "FAIL - PROPOSED ONE-POSITION-WRITER / INV-7: position authority contract violated");
    return all_ok;
}
