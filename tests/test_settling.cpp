// ============================================================================
// SETTLING — two focused TDD tests, one per defect the owner saw
// ============================================================================
// Owner, watching the ladder: "the upper big particle on the first test that
// rotates just keeps wiggling, and the last test the rotated particles are
// not laying flat on the ground. Lets isolate these issues in individual
// focused tests simpler that we can run and use as TDD, one each."
//
//   test_settling_wiggle  a body carried by a rotating parent must STOP.
//   test_settling_flat    a body that comes to rest must rest ON THE GROUND,
//                         where its own rendered shape says it is.
//
// Both share one instrument: a scan over EVERY particle that names whatever
// is still moving. No per-test bespoke probes.
//
// LAWS THESE TESTS ENFORCE (assert-protocol migration, 2026-08-21):
//   PROPOSED REST-IS-REACHED (tests/invariants/INV_PROPOSALS.md) — a scene
//          nobody is touching goes quiet and STAYS quiet. INV-24 covers the
//          corrective half (a repair that fires forever) and is cited where
//          the failure mode is "settled then woke".
//   INV-24 every corrective mechanism terminates; a body that settles and
//          wakes again with nothing touching it is a repair pumping energy.
//   INV-12 a body collides as its true oriented shape. SETTLING 2 is exactly
//          the bounding-slab failure: a quarter-turned plate hovering at half
//          its UNROTATED height because the narrow phase read no rotation.
// ============================================================================

#include "../src/core/engine.h"
#include "../src/core/particle_system.h"
#include "../src/materials.h"
#include "../src/particle_core.h"
#include "logosphere/physics/physics_system.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// THE INSTRUMENT: who in this world is not at equilibrium?
// Scans every particle, not a hand-picked list, so nothing hides.
// ---------------------------------------------------------------------------
struct Unrest {
    size_t   moving = 0;        // bodies above the quiet threshold
    size_t   total = 0;         // bodies that could move at all
    int      worst_id = -1;
    float    worst_speed = 0.0f;
    float    worst_omega = 0.0f;
};

Unrest scan_unrest(Engine& engine, float quiet = 0.02f, bool verbose = false) {
    Unrest u;
    auto v = engine.get_particle_system().lock_particles_for_write();
    for (size_t i = 0; i < v.size(); ++i) {
        const Particle& p = v[i];
        if (p.GetMass() <= 0.0f) continue;                       // lights
        if (p.solver_mode == ParticleSolverMode::KINEMATIC) continue;
        u.total++;
        const float sp = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
        const float om = std::sqrt(p.omega_x*p.omega_x + p.omega_y*p.omega_y +
                                   p.omega_z*p.omega_z);
        if (sp > quiet || om > quiet) {
            u.moving++;
            if (verbose && u.moving <= 6)
                printf("        P%zu speed %.4f m/s  omega %.4f rad/s  "
                       "z %.3f  at_rest %d\n",
                       i, sp, om, p.z, (int)p.is_at_rest);
        }
        if (sp > u.worst_speed) { u.worst_speed = sp; u.worst_id = (int)i; }
        u.worst_omega = std::fmax(u.worst_omega, om);
    }
    return u;
}

void add_floor(Engine& engine, int half) {
    auto& ps = engine.get_particle_system();
    for (int cx = -half; cx <= half; ++cx)
        for (int cy = -half; cy <= half; ++cy) {
            Particle t = {};
            t.shape = ParticleShape::BOX;
            t.x = (float)cx; t.y = (float)cy; t.z = 0.05f;
            t.width = t.height = 1.0f; t.thickness = 0.1f; t.size = 1.0f;
            t.r = 0.35f; t.g = 0.4f; t.b = 0.35f; t.a = 1.0f;
            t.SetMaterial(Materials::Type::STONE);
            const int id = engine.add_particle(t);
            ps.flush_pending_particles();
            auto v = ps.lock_particles_for_write();
            v[id].solver_mode = ParticleSolverMode::KINEMATIC;
            v[id].owner = ParticleOwner::DYNAMICS;
            v[id].is_at_rest = true;
        }
}

bool start(Engine& engine) {
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    return engine.initialize(cfg) == 0;
}

} // namespace

// ============================================================================
// TEST 1 — a body carried by a rotating parent must come to REST.
// The ladder's rung 1 in miniature: kinematic post, one bonded child, the
// post turns 90 degrees, and then everything must stop. The owner watched
// that child wiggle indefinitely.
// ============================================================================
bool test_settling_wiggle() {
    printf("\n=== SETTLING 1: a carried body must stop wiggling ===\n");
    Engine engine;
    if (!start(engine)) { printf("  engine init failed\n  FAIL\n"); return false; }
    auto& ps = engine.get_particle_system();
    add_floor(engine, 2);

    auto box = [&](float z, float s) {
        Particle p = {};
        p.shape = ParticleShape::BOX;
        p.x = 0.0f; p.y = 0.0f; p.z = z;
        p.width = p.height = p.thickness = s; p.size = s;
        p.r = 0.7f; p.g = 0.5f; p.b = 0.3f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        return engine.add_particle(p);
    };
    const int post  = box(1.2f, 1.0f);
    const int child = box(2.2f, 1.0f);
    ps.flush_pending_particles();
    {
        auto v = ps.lock_particles_for_write();
        v[post].solver_mode = ParticleSolverMode::KINEMATIC;
        v[post].owner = ParticleOwner::DYNAMICS;
        v[post].material_strength = 1e9f;
        v[child].material_strength = 1e9f;
        v[child].is_quat_driven = true;
    }
    auto g = std::make_unique<OrganicGluon>();
    g->offset_a = {0.0f, 0.0f, +0.5f};
    g->offset_b = {0.0f, 0.0f, -0.5f};
    g->target_distance = 0.0f;
    g->rotate_offsets = true;
    g->contact_area = 1.0f;
    g->stiffness = 50000.0f;
    g->damping = 1000.0f;
    g->enable_angular_constraint = true;
    g->angular_drive_enabled = true;
    g->use_quat_target = true;
    g->angular_stiffness = 20000.0f;
    g->angular_damping = 500.0f;
    engine.get_physics_system().add_gluon_between(post, child, std::move(g));

    for (int f = 0; f < 60; ++f) engine.update(1.0 / 60.0);
    // Sweep the post through 90 degrees, then leave the world alone.
    for (int f = 0; f < 90; ++f) {
        { auto v = ps.lock_particles_for_write();
          v[post].rotation_x = (float)(M_PI / 2.0) * (float)(f + 1) / 90.0f; }
        engine.update(1.0 / 60.0);
    }

    // How long until nothing moves, and does it STAY that way?
    int settled_at = -1, quiet_run = 0;
    for (int f = 0; f < 900; ++f) {
        engine.update(1.0 / 60.0);
        const Unrest u = scan_unrest(engine);
        if (u.moving == 0) {
            if (++quiet_run >= 60 && settled_at < 0) settled_at = f - 60;
        } else {
            quiet_run = 0;
            if (settled_at >= 0) settled_at = -2;   // woke again
        }
        if (f % 300 == 299)
            printf("      f%3d  moving %zu of %zu  worst %.4f m/s (P%d)\n",
                   f, u.moving, u.total, u.worst_speed, u.worst_id);
    }
    const Unrest end = scan_unrest(engine, 0.02f, true);
    printf("      settled at: %s\n",
           settled_at >= 0 ? "yes" : (settled_at == -2 ? "SETTLED THEN WOKE" : "NEVER"));
    printf("      still moving at the end: %zu of %zu, worst %.4f m/s, %.4f rad/s\n",
           end.moving, end.total, end.worst_speed, end.worst_omega);

    const bool pass = settled_at >= 0 && end.moving == 0;
    printf("\n  %s\n", pass ? "PASS"
        : "FAIL PROPOSED REST-IS-REACHED / INV-24 (a world nobody is touching "
          "must go quiet, and stay quiet: SETTLED-THEN-WOKE is INV-24, a "
          "correction that never terminates)");
    engine.shutdown();
    return pass;
}

// ============================================================================
// TEST 2 — a body that comes to rest must rest ON THE GROUND.
// A thin plate, tipped on its side, dropped. Where it LOOKS like it is
// (its rendered shape, rotated) and where it COLLIDES may differ: the narrow
// phase reads no rotation, so a rotated plate is still collided as its
// upright slab and can hover at half its unrotated height. The owner saw
// exactly this: "the rotated particles are not laying flat on the ground."
// ============================================================================
bool test_settling_flat() {
    printf("\n=== SETTLING 2: a body at rest must be ON the ground ===\n");
    Engine engine;
    if (!start(engine)) { printf("  engine init failed\n  FAIL\n"); return false; }
    auto& ps = engine.get_particle_system();
    add_floor(engine, 2);

    // A plate: wide and thin, like a blade segment or a shard of debris.
    constexpr float W = 0.40f, H = 0.05f, T = 0.60f;
    Particle p = {};
    p.shape = ParticleShape::BOX;
    p.x = 0.0f; p.y = 0.0f; p.z = 1.20f;
    p.width = W; p.height = H; p.thickness = T; p.size = W;
    p.r = 0.4f; p.g = 0.7f; p.b = 0.85f; p.a = 1.0f;
    p.SetMaterial(Materials::Type::WOOD_SOFT);
    const int plate = engine.add_particle(p);
    ps.flush_pending_particles();
    {   // tipped a quarter turn: its long axis now lies horizontal
        auto v = ps.lock_particles_for_write();
        v[plate].rotation_x = (float)(M_PI / 2.0);
        v[plate].is_quat_driven = false;
    }

    for (int f = 0; f < 600; ++f) engine.update(1.0 / 60.0);

    float z = 0, rx = 0;
    {
        auto v = ps.lock_particles_for_write();
        z = v[plate].z; rx = v[plate].rotation_x;
    }
    // Where the SHAPE says its lowest point is, given how it is turned:
    // rotated a quarter turn about X, the extent that hangs below the centre
    // is half its HEIGHT, not half its thickness.
    const float floor_top = 0.10f;
    const float half_down = 0.5f * (std::fabs(std::cos(rx)) * T +
                                    std::fabs(std::sin(rx)) * H);
    const float lowest = z - half_down;
    const float air = lowest - floor_top;

    const Unrest u = scan_unrest(engine, 0.02f, true);
    printf("      plate: z %.3f, rotation_x %.1f deg\n", z, rx * 57.3f);
    printf("      its shape's lowest point: %.3f   floor top: %.3f\n",
           lowest, floor_top);
    printf("      AIR UNDERNEATH: %.0f mm (want < 10)\n", air * 1000.0f);
    printf("      still moving: %zu of %zu\n", u.moving, u.total);

    const bool grounded = air < 0.010f;
    const bool quiet = u.moving == 0;
    printf("      %-44s %s\n", "INV-12: rests on the ground, not on air",
           grounded ? "ok" : "*** HOVERING ***");
    printf("      %-44s %s\n", "PROPOSED REST-IS-REACHED: and is at rest",
           quiet ? "ok" : "*** STILL MOVING ***");

    const bool pass = grounded && quiet;
    printf("\n  %s\n", pass ? "PASS"
        : "FAIL INV-12 (what a body LOOKS like and what it COLLIDES as must "
          "agree: contacts come from the true oriented shape, never the "
          "bounding slab)");
    engine.shutdown();
    return pass;
}
