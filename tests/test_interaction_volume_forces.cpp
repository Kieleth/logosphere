// =============================================================================
// PARTICLE INTERACTION — volume forces (Phase 3 contract)
// =============================================================================
// A filtered overlap with a medium-declaring profile is not decoration:
// the medium applies forces to the intruder. All assertions are
// DISPLACEMENT/VELOCITY-measured against physical expectations (the
// post-PR#14 discipline), never against internals.
//
// The solver applies hardcoded gravity (PhysicsV4::GRAVITY along -z)
// to every awake DYNAMIC particle — there is no zero-g world to test
// in. Contracts therefore measure fixed windows with the ball
// guaranteed INSIDE a large medium column, and buoyancy is checked
// against the solver's real gravity (get_solver_gravity), not the
// vestigial force registry.
//
// LAWS (assert-protocol migration, 2026-08-21):
//   v1 DRAG      G-2 (sphere-into-mud) and INV-19. G-2 is the exact
//                experiment: what is an interaction when the other party is
//                not a surface, there is no contact point and no normal, only
//                a volume the body is inside. Its answer is the closed form
//                this contract measures — relax toward the medium's velocity
//                with time constant m/c — and INV-19 requires that the energy
//                removed be real dissipation booked to a bucket, acting on
//                RELATIVE motion only.
//   v2 BUOYANCY  G-2's second half: settle where drag balances gravity net of
//                buoyancy. INV-3: the lift is a transformation, not creation.
//   v3 FIELD     INV-7, the momentum door: a declared force reaches the body
//                through the same predicate as any other momentum, and the
//                no-field control is what makes the claim non-vacuous.
//   v4 EVENTS    hygiene (the interaction seam's bookkeeping, not a physics
//                law): one enter and one exit per episode.
//
//   INV-6's zero-g clause CANNOT BE EXERCISED HERE, and the header above
//   already says why: the solver applies a hardcoded -Z gravity to every awake
//   DYNAMIC particle, so no scene in this engine can turn it off. G-26 names
//   the same gap for the ambient medium ("air is NOT DECLARABLE: a scene
//   cannot turn it off, cannot change it"). Measuring buoyancy against
//   get_solver_gravity rather than assuming 9.81 is the honest workaround, not
//   a fix.
//
// Contracts:
//   v1  DRAG: a ball moving horizontally inside a water column
//       (drag_coefficient c, collides_with = 0 so default particles
//       pass through) decelerates; over a 1 s window the horizontal
//       velocity ratio tracks the linear-drag closed form
//       vx1/vx0 = exp(-(c/m)·T). Gravity pulls on vz; linear drag
//       decouples per-axis, so the vx contract is exact regardless.
//   v2  BUOYANCY: buoyancy_factor B=2 doubles gravity's magnitude as
//       lift (gross), net +|g| upward: a ball released at rest inside
//       the column RISES against gravity.
//   v3  FIELD: a declared directional force (Newtons) deflects the
//       ball laterally while inside; a control run without the field
//       does not deflect.
//   v4  EVENTS: falling into the column emits VolumeEvent{entered=
//       true, medium_profile=id}; falling out the bottom emits
//       {entered=false} — once each per episode.
//
// Run: ./build/logosphere-physics-guards --test test_interaction_volume_forces
// =============================================================================

#include "core/engine.h"
#include "logosphere/interaction/particle_interaction_system.h"
#include "logosphere/events/event_bus.h"
#include "materials.h"
#include "particle.h"

#include <cmath>
#include <cstdio>

namespace {

struct VolumeRig {
    Engine engine;
    bool ok = false;
    int medium = -1;

    // A large KINEMATIC column carrying `profile_id`:
    // x,y ∈ [-20,20], z ∈ [10,50]. Big enough that a ball measured for
    // 1 s while falling under gravity stays comfortably inside.
    explicit VolumeRig(const char* title, uint32_t profile_id) {
        EngineConfig cfg;
        cfg.create_display = false;
        cfg.window_title = title;
        cfg.enable_chat_window = false;
        ok = (engine.initialize(cfg) == 0);
        if (!ok) return;

        Particle col = {};
        col.shape = ParticleShape::BOX;
        col.x = 0.0f; col.y = 0.0f; col.z = 30.0f;
        col.width = 40.0f; col.height = 40.0f; col.thickness = 40.0f;
        col.SetMaterial(Materials::Type::STONE);
        medium = engine.add_particle(col);
        auto view = engine.get_particle_system().lock_particles_for_write();
        view[medium].solver_mode = ParticleSolverMode::KINEMATIC;
        view[medium].is_at_rest = false;
        view[medium].interaction_profile_id = profile_id;
    }

    int ball(float x, float y, float z, float vx, float vy, float vz) {
        Particle p = {};
        p.shape = ParticleShape::SPHERE;
        p.x = x; p.y = y; p.z = z;
        p.width = 0.3f; p.height = 0.3f; p.thickness = 0.3f;
        p.size = 0.3f;
        p.SetMaterial(Materials::Type::STONE);
        int id = engine.add_particle(p);
        auto view = engine.get_particle_system().lock_particles_for_write();
        view[id].solver_mode = ParticleSolverMode::DYNAMIC;
        view[id].is_at_rest = false;
        view[id].vx = vx; view[id].vy = vy; view[id].vz = vz;
        return id;
    }

    float vel_x(int id) {
        auto v = engine.get_particle_system().lock_particles_for_read();
        return v[id].vx;
    }
    float vel_z(int id) {
        auto v = engine.get_particle_system().lock_particles_for_read();
        return v[id].vz;
    }
    float pos_x(int id) {
        auto v = engine.get_particle_system().lock_particles_for_read();
        return v[id].x;
    }
    float pos_z(int id) {
        auto v = engine.get_particle_system().lock_particles_for_read();
        return v[id].z;
    }
    float mass(int id) {
        auto v = engine.get_particle_system().lock_particles_for_read();
        return v[id].GetMass();
    }
};

// Water-like: passable by everything (mask 0), category bit 3.
logosphere::interaction::InteractionProfile medium_profile(uint32_t id) {
    logosphere::interaction::InteractionProfile p;
    p.id = id;
    p.category = 1u << 3;
    p.collides_with = 0u;
    return p;
}

} // namespace

bool test_interaction_volume_forces() {
    printf("\n=== Interaction Volume Forces ===\n");
    const double dt = 1.0 / 60.0;
    bool all_ok = true;

    // ------------------------------------------------------------------
    // v1 — drag decelerates; vx ratio over exactly 1 s inside tracks
    //      exp(-(c/m)·T). The ball falls under gravity meanwhile but
    //      stays inside the column; linear drag decouples per-axis.
    // ------------------------------------------------------------------
    {
        VolumeRig rig("volume forces v1", 500u);
        if (!rig.ok) { printf("  ERROR: engine init (v1)\n"); return false; }
        auto water = medium_profile(500u);
        water.drag_coefficient = 30.0f;   // strong, visible in 1 s
        rig.engine.get_interaction_system().register_profile(water);

        int b = rig.ball(0.0f, 0.0f, 40.0f, 3.0f, 0.0f, 0.0f);
        const float m = rig.mass(b);
        const float v0 = rig.vel_x(b);

        const int frames = 60;                       // 1.0 s window
        for (int f = 0; f < frames; ++f) rig.engine.update(dt);
        const float T = static_cast<float>(frames) * static_cast<float>(dt);

        const float v1v = rig.vel_x(b);
        const float expected_ratio = std::exp(-(water.drag_coefficient / m) * T);
        const float got_ratio = v1v / v0;

        bool still_inside = std::fabs(rig.pos_x(b)) < 20.0f && rig.pos_z(b) > 10.0f;
        bool decelerated = v1v < v0 * 0.95f && v1v > 0.0f;
        bool tracks = std::fabs(got_ratio - expected_ratio) < 0.15f;
        printf("  %s: v1 drag (m=%.1fkg, %.2fs inside): vx %.2f->%.2f, ratio %.3f vs exp %.3f\n",
               (still_inside && decelerated && tracks) ? "PASS" : "FAIL",
               m, T, v0, v1v, got_ratio, expected_ratio);
        all_ok = all_ok && still_inside && decelerated && tracks;
    }

    // ------------------------------------------------------------------
    // v2 — buoyancy: B=2 gross lift = 2|g| up, solver gravity |g| down,
    //      net +|g|: a ball released at rest INSIDE the column rises.
    //      (Without the medium it would fall — the rise is the proof.)
    // ------------------------------------------------------------------
    {
        VolumeRig rig("volume forces v2", 501u);
        if (!rig.ok) { printf("  ERROR: engine init (v2)\n"); return false; }
        auto water = medium_profile(501u);
        water.buoyancy_factor = 2.0f;
        rig.engine.get_interaction_system().register_profile(water);

        int b = rig.ball(0.0f, 0.0f, 20.0f, 0.0f, 0.0f, 0.0f);
        const float z0 = rig.pos_z(b);
        float vz_max = 0.0f;
        for (int f = 0; f < 60; ++f) {
            rig.engine.update(dt);
            float vz = rig.vel_z(b);
            if (vz > vz_max) vz_max = vz;
        }
        const float dz = rig.pos_z(b) - z0;
        bool rose = dz > 2.0f;
        bool accelerated_up = vz_max > 3.0f;
        printf("  %s: v2 buoyancy: rose %.2f m in 1 s (vz_max %.2f)\n",
               (rose && accelerated_up) ? "PASS" : "FAIL", dz, vz_max);
        all_ok = all_ok && rose && accelerated_up;
    }

    // ------------------------------------------------------------------
    // v3 — field force deflects; control run does not.
    // ------------------------------------------------------------------
    {
        float deflection_with = 0.0f, deflection_without = 0.0f;
        for (int with_field = 0; with_field <= 1; ++with_field) {
            VolumeRig rig("volume forces v3", 502u);
            if (!rig.ok) { printf("  ERROR: engine init (v3)\n"); return false; }
            auto fieldp = medium_profile(502u);
            if (with_field) fieldp.field_fx = 200.0f;   // Newtons, +x
            rig.engine.get_interaction_system().register_profile(fieldp);

            // Drift along +y inside the column for 1 s; read lateral x.
            int b = rig.ball(0.0f, 0.0f, 40.0f, 0.0f, 3.0f, 0.0f);
            for (int f = 0; f < 60; ++f) rig.engine.update(dt);
            float d = rig.pos_x(b);
            if (with_field) deflection_with = d; else deflection_without = d;
        }
        // Expected ½·(F/m)·T² = ½·(200/35.3)·1² ≈ 2.8 m.
        bool deflected = deflection_with > 1.5f;
        bool control_straight = std::fabs(deflection_without) < 0.02f;
        printf("  %s: v3 field deflects +x (%.3f m) vs control (%.3f m)\n",
               (deflected && control_straight) ? "PASS" : "FAIL",
               deflection_with, deflection_without);
        all_ok = all_ok && deflected && control_straight;
    }

    // ------------------------------------------------------------------
    // v4 — VolumeEvent enter + exit, once each: the ball falls in
    //      through the column top and out through its bottom.
    // ------------------------------------------------------------------
    {
        VolumeRig rig("volume forces v4", 503u);
        if (!rig.ok) { printf("  ERROR: engine init (v4)\n"); return false; }
        auto water = medium_profile(503u);
        water.drag_coefficient = 0.5f;   // gentle: the ball must fall through
        rig.engine.get_interaction_system().register_profile(water);

        auto reader = rig.engine.get_event_bus().volume().create_reader();
        int b = rig.ball(0.0f, 0.0f, 55.0f, 0.0f, 0.0f, 0.0f);  // 5 m above top
        (void)b;

        int enters = 0, exits = 0;
        int medium_seen = -1;
        for (int f = 0; f < 300; ++f) {
            rig.engine.update(dt);
            for (const auto& e : reader.drain()) {
                if (e.entered.value_or(false)) {
                    ++enters;
                    medium_seen = e.medium_profile.value_or(-1);
                } else {
                    ++exits;
                }
            }
        }
        bool ok = (enters == 1) && (exits == 1) && (medium_seen == 503);
        printf("  %s: v4 volume events: %d enter / %d exit (medium=%d)\n",
               ok ? "PASS" : "FAIL", enters, exits, medium_seen);
        all_ok = all_ok && ok;
    }

    printf("  [%s]\n", all_ok ? "PASS" : "FAIL - volume forces broken");
    return all_ok;
}
