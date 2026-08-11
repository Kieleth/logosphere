// ============================================================================
// BRANCH PLACEMENT LADDER (issue #57)
// ============================================================================
// Simplest possible case first, then one more thing at a time, the way the
// rotation ladder was built. Each rung is analytically predictable, so a red
// says not just "wrong" but "wrong BY HOW MUCH, and why".
//
// It mirrors the generator's own arithmetic rather than calling it, so the
// failure is attributable to the formula and not to recursion, seeds or
// spec fields:
//
//     branch_center_x = parent_x     + dir_x * length * 0.5f;   // parent CENTRE
//     branch_center_y = parent_y     + dir_y * length * 0.5f;   // parent CENTRE
//     branch_center_z = parent_top_z + dir_z * length * 0.5f;   // parent TOP
//     offset_a = (0, 0, parent_half_length);                    // parent LOCAL +Z
//     offset_b = (0, 0, -length * 0.5f);                        // child  LOCAL -Z
//
// A bond is at rest when centre_b - centre_a == R_a*offset_a - R_b*offset_b,
// i.e. when the child is placed relative to the parent's TOP on ALL THREE
// axes. The placement above uses the parent's top for z and the parent's
// CENTRE for x and y.
//
// THE PREDICTION THIS LADDER EXISTS TO TEST:
//   Rung 1, upright trunk: its top sits directly above its centre, so the
//     horizontal terms are zero and EVERY branch angle should read 1.0.
//     The child's own tilt is carried by its rotation and must not matter.
//   Rung 2, tilted parent: the parent's top is displaced horizontally from
//     its centre by |sin(tilt)| * parent_half_length, and the placement never
//     adds it. Strain should appear here, scaling with the PARENT's tilt and
//     independent of the child's.
//
// If rung 1 is red the diagnosis in issue #57 is wrong. If rung 1 is green
// and rung 2 is red, it is right and the fix is located exactly.
//
//   ./build-release/logosphere-tests --test test_branch_placement_ladder --no-head
// ============================================================================

#include "../src/core/engine.h"
#include "../src/core/particle_system.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include "logosphere/physics/physics_system.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

namespace {

// One parent, one child, placed by the generator's formula and bonded with
// the generator's offsets. Returns the bond's strain at birth.
struct Rung {
    float strain;
    float predicted_gap;   // what the top-vs-centre theory says the error is
};

Rung place_one(Engine& engine, float parent_tilt_deg, float child_elev_deg,
               float parent_len, float child_len) {
    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();

    // PARENT. Its local +Z runs along its own axis; tilt it about Y so its
    // top swings out in +X, which is the displacement the theory says the
    // placement code drops.
    const float ptilt = parent_tilt_deg * (float)M_PI / 180.0f;
    Particle parent = {};
    parent.shape = ParticleShape::BOX;
    parent.x = 0.0f; parent.y = 0.0f; parent.z = 2.0f;
    parent.width = parent.height = 0.20f;
    parent.thickness = parent_len;          // thickness IS length for a segment
    parent.size = 0.20f;
    parent.rotation_y = ptilt;
    parent.r = 0.4f; parent.g = 0.3f; parent.b = 0.2f; parent.a = 1.0f;
    parent.SetMaterial(Materials::Type::WOOD_HARD);
    const int pa = engine.add_particle(parent);
    ps.flush_pending_particles();

    // The generator's direction vector for the child.
    const float rad_v = child_elev_deg * (float)M_PI / 180.0f;
    const float dir_x = std::cos(0.0f) * std::cos(rad_v);
    const float dir_y = std::sin(0.0f) * std::cos(rad_v);
    const float dir_z = std::sin(rad_v);

    const float parent_half = parent_len * 0.5f;

    // THE GENERATOR'S PLACEMENT, copied exactly. parent_top_z is a SCALAR,
    // which is the whole problem: a tilted segment's top is a vector.
    float parent_x, parent_y, parent_z;
    {
        auto v = ps.lock_particles_for_write();
        parent_x = v[pa].x; parent_y = v[pa].y; parent_z = v[pa].z;
    }
    const float parent_top_z = parent_z + std::cos(ptilt) * parent_half;

    Particle child = {};
    child.shape = ParticleShape::BOX;
    child.x = parent_x     + dir_x * child_len * 0.5f;
    child.y = parent_y     + dir_y * child_len * 0.5f;
    child.z = parent_top_z + dir_z * child_len * 0.5f;
    child.width = child.height = 0.12f;
    child.thickness = child_len;
    child.size = 0.12f;
    child.rotation_y = (float)M_PI * 0.5f - rad_v;   // local +Z along dir
    child.r = 0.5f; child.g = 0.4f; child.b = 0.2f; child.a = 1.0f;
    child.SetMaterial(Materials::Type::WOOD_HARD);
    const int cb = engine.add_particle(child);
    ps.flush_pending_particles();

    // THE GENERATOR'S OFFSETS.
    auto g = std::make_unique<OrganicGluon>();
    g->offset_a = Vec3(0.0f, 0.0f, parent_half);
    g->offset_b = Vec3(0.0f, 0.0f, -child_len * 0.5f);
    g->target_distance = 0.0f;
    g->rotate_offsets = true;
    g->contact_area = 1e-3f;
    g->stiffness = 5000.0f;
    g->damping = 100.0f;
    physics.add_gluon_between(pa, cb, std::move(g));

    // Strain at birth, using the tear law's own rest formula. NO update().
    Rung r{0.0f, 0.0f};
    {
        auto v = ps.lock_particles_for_write();
        const float dx = v[cb].x - v[pa].x, dy = v[cb].y - v[pa].y,
                    dz = v[cb].z - v[pa].z;
        const float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
        const GluonConstraintBase* gg = physics.get_gluon(pa, cb);
        const float rest = gg ? std::max({gg->target_distance,
                                          gg->get_segment_length(), 0.01f})
                              : 0.01f;
        r.strain = dist / rest;
    }
    // The theory: the parent's top is displaced horizontally from its centre
    // by sin(tilt) * half-length, and the placement never adds it.
    r.predicted_gap = std::fabs(std::sin(ptilt)) * parent_half;
    return r;
}

} // namespace

bool test_branch_placement_ladder() {
    printf("\n=== BRANCH PLACEMENT LADDER (issue #57) ===\n\n");
    int failures = 0;

    // ---- RUNG 1: upright parent, every child angle -------------------------
    printf("  RUNG 1 — upright trunk, one branch. Every angle must read 1.000,\n");
    printf("           because an upright segment's top is directly above its centre.\n\n");
    printf("    %-16s %10s %10s\n", "child elev", "strain", "verdict");
    for (float elev : {0.0f, 15.0f, 30.0f, 45.0f, 60.0f, 90.0f}) {
        EngineConfig cfg; cfg.create_display = false;
        cfg.enable_chat_window = false; cfg.show_debug_overlay = false;
        Engine e;
        if (e.initialize(cfg) != 0) { printf("  init failed\n  FAIL\n"); return false; }
        const Rung r = place_one(e, /*parent_tilt=*/0.0f, elev, 2.0f, 1.0f);
        const bool ok = std::fabs(r.strain - 1.0f) <= 0.02f;
        if (!ok) failures++;
        printf("    %11.0f deg %10.4f %10s\n", elev, r.strain, ok ? "ok" : "*** RED ***");
        e.shutdown();
    }

    // ---- RUNG 2: tilted parent --------------------------------------------
    printf("\n  RUNG 2 — TILTED parent, branch straight out. Strain must still be\n");
    printf("           1.000. The theory says it will not be, and that the gap is\n");
    printf("           exactly sin(tilt) x parent_half_length.\n\n");
    printf("    %-16s %10s %14s %10s\n", "parent tilt", "strain", "predicted gap", "verdict");
    for (float tilt : {0.0f, 15.0f, 30.0f, 45.0f, 60.0f}) {
        EngineConfig cfg; cfg.create_display = false;
        cfg.enable_chat_window = false; cfg.show_debug_overlay = false;
        Engine e;
        if (e.initialize(cfg) != 0) { printf("  init failed\n  FAIL\n"); return false; }
        const Rung r = place_one(e, tilt, /*child_elev=*/0.0f, 2.0f, 1.0f);
        const bool ok = std::fabs(r.strain - 1.0f) <= 0.02f;
        if (!ok) failures++;
        printf("    %11.0f deg %10.4f %14.4f %10s\n",
               tilt, r.strain, r.predicted_gap, ok ? "ok" : "*** RED ***");
        e.shutdown();
    }

    printf("\n  %d rung(s) red\n", failures);
    const bool pass = (failures == 0);
    printf("\n  %s\n", pass ? "PASS"
        : "FAIL (a branch is not placed where its bond says it is)");
    return pass;
}
