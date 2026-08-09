// ============================================================================
// THE ROTATION LADDER — iteration 1 of bend-by-rotation, one rung at a time
// ============================================================================
//
// The single-blade study measured the disease: a walked-through blade bends
// 58.5 degrees in GEOMETRY and 0.0 degrees in POSE. All bending is shear,
// because bonds constrain scalar centre distance and physics never rotates a
// default particle. The owner's order: rotation TDD in tiny steps, red first,
// green after, a visual OK between rungs, complexity grows one rung at a time.
//
// RUNG 1 — the anchor follows the parent's FULL rotation.
//   A KINEMATIC parent post with a child bonded to its top via anchors
//   (offset_a = parent top, offset_b = child bottom, target 0: anchors
//   coincide). Rotate the parent 90 degrees about X. The parent's top now
//   points along -Y, so the child must be dragged around to sit at the
//   rotated anchor. Today the offset math rotates XY by rotation_z only and
//   NEVER rotates the Z component (physics_system_v4.cpp:1285-1298), so the
//   anchor never moves and the child never follows: RED.
//
// Later rungs (each lands only after the previous is OK'd visually):
//   RUNG 2 — anchor forces produce torque (r x J): the child ROTATES.
//   RUNG 3 — a 3-segment chain bends by rotating, shear stays ~0.
//   RUNG 4 — the single-blade gate clause 'bends by ROTATING' goes green.
// ============================================================================

#include "../src/core/engine.h"
#include "../src/core/particle_system.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include "logosphere/physics/physics_system.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {

struct Rung {
    const char* name = "";
    bool passed = false;
    char detail[160] = {0};
};

float dist3(float ax, float ay, float az, float bx, float by, float bz) {
    const float dx = ax - bx, dy = ay - by, dz = az - bz;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// ---------------------------------------------------------------------------
// RUNG 1: the anchor follows the parent's full rotation.
// ---------------------------------------------------------------------------
Rung rung1(Engine& engine) {
    Rung r; r.name = "1 anchor follows full rotation";
    auto& ps = engine.get_particle_system();

    auto make_box = [&](float x, float y, float z, float s) {
        Particle p = {};
        p.shape = ParticleShape::BOX;
        p.x = x; p.y = y; p.z = z;
        p.width = p.height = p.thickness = s; p.size = s;
        p.r = 0.7f; p.g = 0.5f; p.b = 0.3f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        return engine.add_particle(p);
    };

    // Parent post at z=5 (mid-air; the bond, not a floor, holds the child).
    // Child sits on top: parent top anchor (0,0,+0.5) meets child bottom
    // anchor (0,0,-0.5); centres 1 m apart, anchors coincident.
    const int parent = make_box(0, 0, 5.0f, 1.0f);
    const int child  = make_box(0, 0, 6.0f, 1.0f);
    ps.flush_pending_particles();
    {
        auto v = ps.lock_particles_for_write();
        v[parent].solver_mode = ParticleSolverMode::KINEMATIC;
        v[parent].owner = ParticleOwner::DYNAMICS;
        v[parent].material_strength = 1e9f;   // breaking is not this rung
        v[child].material_strength  = 1e9f;
    }
    auto g = std::make_unique<OrganicGluon>();
    g->offset_a = {0.0f, 0.0f, +0.5f};
    g->offset_b = {0.0f, 0.0f, -0.5f};
    g->target_distance = 0.0f;    // anchors coincide
    g->rotate_offsets = true;
    g->contact_area = 1.0f;
    g->stiffness = 50000.0f;
    g->damping = 1000.0f;
    engine.get_physics_system().add_gluon_between(parent, child, std::move(g));

    // Settle: child must simply stay put on the post.
    for (int f = 0; f < 60; ++f) engine.update(1.0 / 60.0);
    float settle_gap;
    {
        auto v = ps.lock_particles_for_write();
        settle_gap = dist3(v[parent].x, v[parent].y, v[parent].z + 0.5f,
                           v[child].x,  v[child].y,  v[child].z - 0.5f);
    }

    // The kinematic writer turns the post 90 degrees about X. Its top anchor
    // swings from parent + (0,0,+0.5) to parent + R_x(90)*(0,0,+0.5)
    //   = parent + (0,-0.5, 0).
    {
        auto v = ps.lock_particles_for_write();
        v[parent].rotation_x = (float)(M_PI / 2.0);
    }
    for (int f = 0; f < 120; ++f) engine.update(1.0 / 60.0);

    // Where must the child's bottom anchor be? At the ROTATED parent anchor.
    // (The child has no torque path yet, so its own offset stays unrotated;
    // this rung only demands the PARENT side of the bond rotate.)
    float gap_after, child_y, child_z, py, pz;
    {
        auto v = ps.lock_particles_for_write();
        const float ax = v[parent].x, ay = v[parent].y - 0.5f, az = v[parent].z;
        gap_after = dist3(ax, ay, az,
                          v[child].x, v[child].y, v[child].z - 0.5f);
        child_y = v[child].y; child_z = v[child].z;
        py = v[parent].y; pz = v[parent].z;
    }

    const bool settled  = settle_gap < 0.05f;
    const bool followed = gap_after < 0.10f;
    r.passed = settled && followed;
    snprintf(r.detail, sizeof r.detail,
             "settle gap %.3f m; after 90deg parent turn, child anchor is "
             "%.3f m from the rotated anchor (need < 0.10; child at y=%+.2f "
             "z=%+.2f, rotated anchor at y=%+.2f z=%+.2f)",
             settle_gap, gap_after, child_y, child_z, py - 0.5f, pz);
    return r;
}

} // namespace

bool test_rotation_ladder() {
    printf("\n=== THE ROTATION LADDER: bend-by-rotation, one rung at a time ===\n\n");

    EngineConfig cfg;
    cfg.create_display = false;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) {
        printf("  engine init failed\n  FAIL\n");
        return false;
    }

    Rung rungs[] = { rung1(engine) };

    bool all = true;
    for (const Rung& r : rungs) {
        printf("  %-34s %s\n      %s\n", r.name,
               r.passed ? "GREEN" : "*** RED ***", r.detail);
        all = all && r.passed;
    }
    printf("\n  %s\n", all ? "PASS" : "FAIL (the ladder is climbed rung by rung; "
                                      "RED marks the rung under construction)");
    engine.shutdown();
    return all;
}
