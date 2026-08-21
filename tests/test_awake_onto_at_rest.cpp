// =============================================================================
// AWAKE ONTO AT-REST — minimal 2-particle trace test
// =============================================================================
// The simplest possible case. One awake particle spawned 5cm above one at-rest
// particle of the same XY size, both STONE, under gravity. Expected: the awake
// particle stacks on top of the at-rest one. Observed (as of 2026-04-12): the
// awake particle passes through and lands on the turtle.
//
// This test prints per-frame state for both particles — position, velocity,
// at_rest flag — for 60 frames. No pass/fail logic yet: the goal is raw data.
// Once the data points to the failing mechanism, we add physics-side
// telemetry, then fix at the cause.
//
// LAWS THIS TEST ENFORCES (assert-protocol migration, 2026-08-21):
//   INV-18 sleep hides nothing: a sleeping body's geometry and overlaps are
//          evaluated exactly as an awake body's. The historical failure —
//          the awake box passing THROUGH the at-rest one — is precisely a
//          sleeping body whose geometry stopped being consulted.
//   INV-2  no interpenetration beyond SLOP in steady state.
//   PROPOSED REST-IS-REACHED (INV_PROPOSALS.md) — the stack settles
//          and stays settled.
//
// Run: ./logosphere-tests --test test_awake_onto_at_rest
// =============================================================================

#include "../src/core/engine.h"
#include "logosphere/physics/physics_system.h"
#include "../src/core/force.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include <iostream>
#include <iomanip>

bool test_awake_onto_at_rest() {
    std::cout << "\n";
    std::cout << "========================================================\n";
    std::cout << "  AWAKE-ONTO-AT-REST — minimal trace\n";
    std::cout << "========================================================\n";

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_width = 800;
    cfg.window_height = 600;
    cfg.window_title = "awake onto at-rest";
    cfg.enable_chat_window = false;
    if (engine.initialize(cfg) != 0) {
        std::cout << "engine init failed\n"; return false;
    }

    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();

    physics.add_force(std::make_unique<GravityForce>(0.0f, 0.0f, -9.8f));

    // =========================================================================
    // BOTTOM (at-rest) — STONE, 2x2x0.4, z so bottom sits on turtle z=0.
    // =========================================================================
    Particle bottom = {};
    bottom.shape     = ParticleShape::BOX;
    bottom.x         = 0.0f;
    bottom.y         = 0.0f;
    bottom.z         = 0.2f;            // center at thickness/2
    bottom.width     = 2.0f;
    bottom.height    = 2.0f;
    bottom.thickness = 0.4f;
    bottom.size      = 2.0f;
    bottom.r = 0.3f; bottom.g = 0.3f; bottom.b = 0.35f; bottom.a = 1.0f;
    bottom.friction  = 0.5f;
    bottom.is_at_rest = true;            // our scenario: bottom is resting.
    bottom.vx = bottom.vy = bottom.vz = 0.0f;
    bottom.SetMaterial(Materials::Type::STONE);
    int bottom_id = engine.add_particle(bottom);

    // =========================================================================
    // TOP (awake) — STONE, 2x2x0.25, spawned 5 cm above the bottom's top face.
    // Bottom top face is at z=0.4. Top spawns so its BOTTOM is at z=0.45.
    // Center = 0.45 + 0.25/2 = 0.575.
    // =========================================================================
    Particle top = {};
    top.shape     = ParticleShape::BOX;
    top.x         = 0.0f;
    top.y         = 0.0f;
    top.z         = 0.575f;
    top.width     = 2.0f;
    top.height    = 2.0f;
    top.thickness = 0.25f;
    top.size      = 2.0f;
    top.r = 0.45f; top.g = 0.4f; top.b = 0.3f; top.a = 1.0f;
    top.friction  = 0.5f;
    top.is_at_rest = false;
    top.vx = top.vy = top.vz = 0.0f;
    top.SetMaterial(Materials::Type::STONE);
    int top_id = engine.add_particle(top);

    std::cout << "[setup] bottom id=" << bottom_id
              << " z=" << bottom.z
              << " mass=" << bottom.GetMass()
              << " at_rest=1"
              << "\n";
    std::cout << "[setup] top    id=" << top_id
              << " z=" << top.z
              << " mass=" << top.GetMass()
              << " at_rest=0"
              << " spawn_gap=" << (top.z - top.thickness*0.5f) - (bottom.z + bottom.thickness*0.5f)
              << " m\n";

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "\n frame  | bot z | bot vz | bot rest | top z    | top vz    | top rest |  gap   |\n";
    std::cout << "--------+-------+--------+----------+----------+-----------+----------+--------+\n";

    const float dt = 1.0f / 60.0f;
    for (int f = 0; f < 60; f++) {
        engine.update(dt);

        auto view = ps.lock_particles_for_read();
        if (view.size() < 2) { std::cout << "particles went missing\n"; return false; }
        const Particle& b = view[bottom_id];
        const Particle& t = view[top_id];
        float top_bottom = t.z - t.thickness * 0.5f;
        float bot_top    = b.z + b.thickness * 0.5f;
        float gap        = top_bottom - bot_top;

        std::cout << " " << std::setw(4) << f
                  << "   | " << std::setprecision(3) << std::setw(5) << b.z
                  << " | " << std::setw(6) << b.vz
                  << " | " << std::setw(8) << (int)b.is_at_rest
                  << " | " << std::setprecision(4) << std::setw(8) << t.z
                  << " | " << std::setw(9) << t.vz
                  << " | " << std::setw(8) << (int)t.is_at_rest
                  << " | " << std::setprecision(3) << std::setw(6) << gap
                  << " |\n";
        std::cout << std::setprecision(6);
    }

    // =========================================================================
    // Assertions: top should stack ON bottom, not pass through it.
    // Top's bottom face (z - thickness/2) should be within contact slop of
    // bottom's top face (z + thickness/2), and top should be at rest.
    // =========================================================================
    auto view = ps.lock_particles_for_read();
    const Particle& b = view[bottom_id];
    const Particle& t = view[top_id];
    float top_bottom_face = t.z - t.thickness * 0.5f;
    float bot_top_face    = b.z + b.thickness * 0.5f;
    float gap             = top_bottom_face - bot_top_face;

    std::cout << "\n[end] top z=" << t.z
              << " top_bottom_face=" << top_bottom_face
              << " bot_top_face=" << bot_top_face
              << " gap=" << gap
              << " top_vz=" << t.vz
              << " top_at_rest=" << (int)t.is_at_rest
              << "\n";

    constexpr float CONTACT_SLOP = 0.005f;  // 5mm tolerance for resting contact
    bool ok = true;

    if (gap < -CONTACT_SLOP) {
        std::cout << "FAIL INV-2 (and INV-18 when it sinks clean through: the "
                     "at-rest body's geometry stopped being consulted): top "
                     "penetrates bottom by " << -gap*1000 << "mm\n";
        ok = false;
    }
    if (gap > 0.05f) {
        std::cout << "FAIL INV-18: top floating " << gap*1000 << "mm above bottom "
                     "(no contact — a sleeping body's geometry must be evaluated "
                     "exactly as an awake body's)\n";
        ok = false;
    }
    if (std::abs(t.vz) > 0.1f) {
        std::cout << "FAIL PROPOSED REST-IS-REACHED: top vz=" << t.vz << " m/s, the stack never "
                     "comes to rest\n";
        ok = false;
    }

    std::cout << (ok ? "[PASS]" : "[FAIL]") << " test_awake_onto_at_rest\n";
    return ok;
}
