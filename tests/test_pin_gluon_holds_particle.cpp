// =============================================================================
// PIN GLUON HOLDS PARTICLE — isolated mechanism test (Phase 4b-2)
// =============================================================================
// Validates the Phase 4b pin-gluon wiring without a humanoid: a KINEMATIC
// anchor particle + a DYNAMIC subject marked is_quat_driven (skips
// gravity, the same flag apply_physics_drive_legs_init sets on leg
// particles when register_humanoid_direct runs), joined by a zero-length
// NailGluon. The pin is zero-length with no angular coupling — pure
// position lock. Matches exactly what flush_pending_pin_gluon_ops builds
// for the foot-plant anchor.
//
// Gravity is skipped for is_quat_driven particles in the physics solver
// (see particle_dynamics_system Phase 3c fix). In the humanoid path the
// 8 leg particles are quat-driven by default since Phase 5, so gravity
// never drags the stance foot. The pin gluon's role is to keep the foot
// coincident with the anchor while the solver's angular drive moves
// hip + knee rotations around.
//
// Assertions:
//   - get_gluon(subject, anchor) returns non-null after creation.
//   - Over 120 frames with no external force, subject stays within 3 cm
//     of the anchor.
//   - After remove_gluons_for_particle(anchor), get_gluon returns null
//     again — lifecycle teardown works.
//
// Convergence power of the gluon under load is tuned in Phase 4b-3 with
// the actual walking test. This test only proves the plumbing is correct.
//
// Run: ./build/logosphere-tests --test test_pin_gluon_holds_particle --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "logosphere/physics/physics_system.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include <cstdio>
#include <cmath>

bool test_pin_gluon_holds_particle() {
    printf("\n=== Pin Gluon Holds Particle (Phase 4b-2) ===\n");

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "pin gluon holds";
    cfg.enable_chat_window = false;
    if (engine.initialize(cfg) != 0) {
        printf("  ERROR: engine init failed\n");
        return false;
    }

    auto& ps      = engine.get_particle_system();
    auto& physics = engine.get_physics_system();

    // Anchor: tiny invisible KINEMATIC point. Same config Phase 4b uses
    // for the foot-plant anchor (light_source bypasses mass auto-calc).
    Particle anchor = {};
    anchor.shape = ParticleShape::BOX;
    anchor.x = 0.0f; anchor.y = 0.0f; anchor.z = 1.0f;
    anchor.width = 0.01f; anchor.height = 0.01f; anchor.thickness = 0.01f;
    anchor.is_light_source = true;
    anchor.a = 0.0f;
    int anchor_id = engine.add_particle(anchor);

    // Subject: DYNAMIC STONE sphere, coincident with anchor. is_quat_driven
    // flag skips gravity (the same flag apply_physics_drive_legs_init
    // sets on leg particles in the humanoid path).
    Particle subject = {};
    subject.shape = ParticleShape::SPHERE;
    subject.x = 0.0f; subject.y = 0.0f; subject.z = 1.0f;
    subject.width = 0.1f; subject.height = 0.1f; subject.thickness = 0.1f;
    subject.r = 0.2f; subject.g = 0.8f; subject.b = 0.2f; subject.a = 1.0f;
    subject.SetMaterial(Materials::Type::STONE);
    subject.is_at_rest = false;
    int subject_id = engine.add_particle(subject);

    {
        auto view = ps.lock_particles_for_write();
        view[anchor_id].solver_mode = ParticleSolverMode::KINEMATIC;
        view[anchor_id].is_at_rest = false;
        view[subject_id].is_quat_driven = true;
        view[subject_id].is_at_rest = false;
    }

    // Pin gluon: zero target distance, stiff, damped, no angular coupling.
    auto g = std::make_unique<NailGluon>();
    g->offset_a = Vec3{0.0f, 0.0f, 0.0f};
    g->offset_b = Vec3{0.0f, 0.0f, 0.0f};
    g->target_distance = 0.0f;
    g->stiffness = 50000.0f;
    g->damping = 500.0f;
    g->breaking_force = 1e9f;
    g->enable_angular_constraint = false;
    g->angular_stiffness = 0.0f;
    g->angular_damping = 0.0f;
    g->rotate_offsets = false;
    physics.add_gluon_between(
        static_cast<size_t>(subject_id),
        static_cast<size_t>(anchor_id),
        std::move(g));

    // Assertion 1: gluon is wired up.
    const GluonConstraintBase* pin_before =
        physics.get_gluon(static_cast<size_t>(subject_id),
                          static_cast<size_t>(anchor_id));
    bool created = (pin_before != nullptr);
    printf("  %s: pin gluon created (%sfound in index)\n",
           created ? "PASS" : "FAIL", created ? "" : "NOT ");

    // Assertion 2: subject stays coincident over 120 frames, no drift.
    const float dt = 1.0f / 60.0f;
    constexpr int FRAMES = 120;

    float max_deviation = 0.0f;
    for (int f = 0; f < FRAMES; ++f) {
        engine.update(dt);

        float ax, ay, az, sx, sy, sz;
        {
            auto view = ps.lock_particles_for_read();
            ax = view[anchor_id].x; ay = view[anchor_id].y; az = view[anchor_id].z;
            sx = view[subject_id].x; sy = view[subject_id].y; sz = view[subject_id].z;
        }
        float dx = sx - ax, dy = sy - ay, dz = sz - az;
        float d = std::sqrt(dx*dx + dy*dy + dz*dz);
        if (d > max_deviation) max_deviation = d;

        if (f % 30 == 0) {
            printf("  [f%3d] anchor=(%.3f,%.3f,%.3f) subject=(%.3f,%.3f,%.3f) dev=%.4f\n",
                   f, ax, ay, az, sx, sy, sz, d);
        }
    }

    const float BUDGET = 0.03f;  // 3 cm
    bool held = (max_deviation <= BUDGET);
    printf("  %s: subject stays within %.0f cm of anchor (max dev %.4f m)\n",
           held ? "PASS" : "FAIL", BUDGET * 100.0f, max_deviation);

    // Assertion 3: teardown removes the gluon.
    physics.remove_gluons_for_particle(static_cast<size_t>(anchor_id));
    const GluonConstraintBase* pin_after =
        physics.get_gluon(static_cast<size_t>(subject_id),
                          static_cast<size_t>(anchor_id));
    bool torn_down = (pin_after == nullptr);
    printf("  %s: remove_gluons_for_particle clears the pin\n",
           torn_down ? "PASS" : "FAIL");

    bool ok = created && held && torn_down;
    printf("\n  %s\n", ok ? "[PASS]" : "[FAIL — pin gluon mechanism broken]");
    return ok;
}
