// =============================================================================
// A BOND NAMES TWO BODIES (INV-22, GEDANKEN-76)
// =============================================================================
// A refused birth returns -1. Bonded as a size_t it is a stranger the solver's
// range guard skips forever, and only a SECOND such bond used to trip INV-22's
// 'second live bond' line - the wrong refusal (test_branch_placement_ladder,
// night 2026-09-04, journal 18). Both bond entry points now refuse an endpoint
// at or beyond the PROMISE range (live + pending) by name, counted, not
// created. The promise range, not the live count: generators bond by
// predicted indices before the flush, and those bonds must stay legal.
//
//   1. P0 live; bond P0 <-> SIZE_MAX: refused, counted, no gluon created.
//   2. P0 live; queue P1 (a promise, still pending); bond P0 <-> P1 before
//      the flush: accepted; after the flush P1 is live.
//
// Run: ./build/test_bond_to_nothing_is_refused
// =============================================================================
#include <cstdio>
#include <cstddef>
#include <memory>
#include "../src/core/engine.h"
#include "../src/core/particle_system.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include "logosphere/physics/physics_system.h"

static Particle stone_cube(float x, float y, float z) {
    Particle p = {};
    p.shape = ParticleShape::BOX;
    p.width = p.height = p.thickness = 0.1f;
    p.size = 0.1f;
    p.x = x; p.y = y; p.z = z;
    p.SetMaterial(Materials::Type::STONE);
    return p;
}

static std::unique_ptr<OrganicGluon> nail() {
    auto g = std::make_unique<OrganicGluon>();
    g->offset_a = Vec3(0.0f, 0.0f, 0.05f);
    g->offset_b = Vec3(0.0f, 0.0f, -0.05f);
    g->target_distance = 0.0f;
    g->rotate_offsets = true;
    g->contact_area = 1e-3f;
    g->stiffness = 5000.0f;
    g->damping = 100.0f;
    return g;
}

int main() {
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.enable_chat_window = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) { std::printf("engine init failed\n"); return 1; }
    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();
    int red = 0;
    std::printf("\n=== A BOND NAMES TWO BODIES (INV-22 / G-76) ===\n");

    const int p0 = engine.add_particle(stone_cube(0.0f, 0.0f, 10.0f));
    ps.flush_pending_particles();

    // 1. a bond to nobody
    {
        const size_t gluons_before = physics.get_total_gluon_count();
        const size_t refused_before = physics.bonds_refused();
        physics.add_gluon_between(static_cast<size_t>(p0), static_cast<size_t>(-1), nail());
        const bool refused = physics.bonds_refused() == refused_before + 1;
        const bool none_made = physics.get_total_gluon_count() == gluons_before;
        std::printf("  [%s] [INV-22] a bond P%d <-> P(-1) is refused by name (refusals %zu -> %zu)\n",
                    refused ? "PASS" : "FAIL", p0, refused_before, physics.bonds_refused());
        std::printf("  [%s] [INV-22] and not created (gluons %zu -> %zu)\n",
                    none_made ? "PASS" : "FAIL", gluons_before, physics.get_total_gluon_count());
        red += !refused; red += !none_made;
    }

    // 2. a bond to a promise, before the flush
    {
        const size_t gluons_before = physics.get_total_gluon_count();
        const size_t refused_before = physics.bonds_refused();
        const int promised = ps.queue_particle_addition(stone_cube(0.0f, 0.0f, 10.1f));
        const size_t range = ps.promised_count();
        physics.add_gluon_between(static_cast<size_t>(p0), static_cast<size_t>(promised), nail());
        const bool accepted = physics.get_total_gluon_count() == gluons_before + 1 &&
                              physics.bonds_refused() == refused_before;
        std::printf("  [%s] [G-76] a bond to a still-pending promise P%d (promise range %zu, live %zu) is accepted\n",
                    accepted ? "PASS" : "FAIL", promised, range, ps.count());
        red += !accepted;
        ps.flush_pending_particles();
        float z1 = -1.0f;
        {
            auto v = ps.lock_particles_for_read();
            if (promised >= 0 && static_cast<size_t>(promised) < v.size()) z1 = v[static_cast<size_t>(promised)].z;
        }
        const bool live = z1 > 10.05f;
        std::printf("  [%s] [G-76] after the flush the promised body is live at P%d (z=%.2f)\n",
                    live ? "PASS" : "FAIL", promised, z1);
        red += !live;
    }
    std::printf("  %s (%d red)\n", red ? "[FAIL]" : "[PASS]", red);
    return red ? 1 : 0;
}
