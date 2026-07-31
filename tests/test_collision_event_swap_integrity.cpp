// =============================================================================
// COLLISION EVENT SWAP INTEGRITY — particle deletion vs event lifecycle
// =============================================================================
// Collision events carry raw particle indices and live from the physics
// solve until the next solve's clear(). Particle deletion (swap-and-pop)
// can happen in between: gluons get remapped via notify_particle_swap and
// dropped via remove_gluons_for_particle, but events used to keep their
// stale indices. Any post-physics consumer doing particles[evt.particle_x]
// then reads past the live end of the particle vector — UB, caught by
// ASan as a container-overflow in handle_collision_events (2026-06-11).
//
// This test locks the rule: after a delete, no event references a dead
// slot, and events referencing the swapped (moved) particle follow it.
//
// Scenario: three spheres resting on a KINEMATIC floor box, so each
// generates a persistent (sphere, floor) contact event every frame.
//   A → keeps its event (control)
//   F → deleted; its event must be DROPPED
//   D → last particle; deleting F swap-and-pops D into F's slot, so
//       D's event must be REMAPPED to the new slot.
// Assertions after delete_particle_immediate(F):
//   1. No event references an index >= live particle count.
//   2. No event references F's event by its old pairing (F was deleted).
//   3. D's event now reads (F's old slot, floor).
//   4. A's event is untouched.
//
// Run: ./build/logosphere-tests --test test_collision_event_swap_integrity --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "logosphere/physics/physics_system.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {

int add_box(Engine& engine, float x, float y, float z) {
    Particle p = {};
    p.shape = ParticleShape::BOX;
    p.x = x; p.y = y; p.z = z;
    p.width = 0.3f; p.height = 0.3f; p.thickness = 0.3f;
    p.r = 0.5f; p.g = 0.5f; p.b = 0.5f; p.a = 1.0f;
    p.SetMaterial(Materials::Type::DIRT);  // high damping, settles fast
    p.is_at_rest = false;
    return engine.add_particle(p);
}

bool has_event_pair(const std::vector<CollisionEvent>& events, size_t x, size_t y) {
    for (const auto& e : events) {
        if ((e.particle_a == x && e.particle_b == y) ||
            (e.particle_a == y && e.particle_b == x)) return true;
    }
    return false;
}

} // namespace

bool test_collision_event_swap_integrity() {
    printf("\n=== Collision Event Swap Integrity ===\n");

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "collision event swap";
    cfg.enable_chat_window = false;
    if (engine.initialize(cfg) != 0) {
        printf("  ERROR: engine init failed\n");
        return false;
    }

    engine.get_physics_system().add_force(
        std::make_unique<GravityForce>(0.0f, 0.0f, -9.8f));

    // KINEMATIC floor box; three spheres dropped just above it.
    Particle floor_p = {};
    floor_p.shape = ParticleShape::BOX;
    // Bottom flush with the turtle surface (z=0) so the boundary doesn't
    // shove it on frame 1.
    floor_p.x = 0.0f; floor_p.y = 0.0f; floor_p.z = 0.25f;
    floor_p.width = 10.0f; floor_p.height = 10.0f; floor_p.thickness = 0.5f;
    floor_p.r = 0.3f; floor_p.g = 0.3f; floor_p.b = 0.3f; floor_p.a = 1.0f;
    floor_p.SetMaterial(Materials::Type::STONE);
    int floor_id = engine.add_particle(floor_p);

    // Spawned just clear of the floor top (0.5): they drop 10 cm, land,
    // and settle into resting contact, which re-emits a collision event
    // every frame while touching.
    int a = add_box(engine, -2.0f, 0.0f, 0.75f);
    int f = add_box(engine,  0.0f, 0.0f, 0.75f);   // gets deleted
    int d = add_box(engine,  2.0f, 0.0f, 0.75f);   // last particle added

    auto& ps = engine.get_particle_system();
    {
        auto view = ps.lock_particles_for_write();
        view[floor_id].solver_mode = ParticleSolverMode::KINEMATIC;
    }

    auto& physics = engine.get_physics_system();

    // Pending-particle flush + falling onto the floor takes a few frames;
    // run until all three spheres have a floor contact event.
    bool pre_a = false, pre_f = false, pre_d = false;
    int warm = 0;
    for (; warm < 180; ++warm) {
        engine.update(1.0 / 60.0);
        const auto& evts = physics.get_collision_events();
        pre_a = has_event_pair(evts, (size_t)a, (size_t)floor_id);
        pre_f = has_event_pair(evts, (size_t)f, (size_t)floor_id);
        pre_d = has_event_pair(evts, (size_t)d, (size_t)floor_id);
        if (warm < 6 || warm % 10 == 0) {
            auto view = ps.lock_particles_for_read();
            printf("  [warm %2d] events=%zu  A z=%.3f vz=%.3f rest=%d  floor z=%.3f mode=%d\n",
                   warm, evts.size(), view[a].z, view[a].vz, (int)view[a].is_at_rest,
                   view[floor_id].z, (int)view[floor_id].solver_mode);
        }
        if (pre_a && pre_f && pre_d) break;
    }
    printf("  %zu collision events after %d update(s)\n",
           physics.get_collision_events().size(), warm + 1);
    printf("  %s: events (A,floor) (F,floor) (D,floor) all present pre-delete\n",
           (pre_a && pre_f && pre_d) ? "PASS" : "FAIL");
    if (!pre_a || !pre_f || !pre_d) {
        printf("  [FAIL — scenario didn't produce the expected events]\n");
        return false;
    }

    // Delete F. Swap-and-pop moves the last particle (D) into F's slot.
    // Events must follow: F's event dropped, D's event remapped to F's
    // slot, A's event untouched, nothing references a dead slot.
    ps.delete_particle_immediate(f);

    size_t live_count;
    {
        auto view = ps.lock_particles_for_read();
        live_count = view.size();
    }

    bool no_stale = true;
    for (const auto& e : physics.get_collision_events()) {
        if (e.particle_a >= live_count || e.particle_b >= live_count) {
            printf("  stale event: (a=%zu, b=%zu) but live count is %zu\n",
                   e.particle_a, e.particle_b, live_count);
            no_stale = false;
        }
    }
    // D swapped into F's old slot: its floor event must now read (f, floor).
    bool post_a = has_event_pair(physics.get_collision_events(), (size_t)a, (size_t)floor_id);
    bool post_d = has_event_pair(physics.get_collision_events(), (size_t)f, (size_t)floor_id);

    printf("  %s: no event references a slot >= live count (%zu)\n",
           no_stale ? "PASS" : "FAIL", live_count);
    printf("  %s: D's event remapped to its new slot (%d, floor=%d)\n",
           post_d ? "PASS" : "FAIL", f, floor_id);
    printf("  %s: A's event untouched (A=%d, floor=%d)\n", post_a ? "PASS" : "FAIL", a, floor_id);

    bool ok = no_stale && post_d && post_a;
    printf("  [%s]\n", ok ? "PASS" : "FAIL — collision events went stale across delete");
    return ok;
}

// =============================================================================
// GLUON REMOVAL UNINDEXES — mark_gluon_for_removal vs gluon_pair_index_
// =============================================================================
// remove_marked_gluons() frees the gluon object; if it doesn't also remove
// the gluon_pair_index_ entry, get_gluon(_mut) keeps returning the freed
// pointer for that particle pair forever. publish_physics_drive_targets
// then writes target_relative_q (unit-quaternion floats) into freed heap
// memory every frame — found 2026-06-12 as the scribbler corrupting Metal
// command encoders (SIGSEGV at autorelease-pool pop, faulting addresses
// decoding to quaternion components like 0x3f800000 = 1.0f).
//
// Contract: after a gluon is marked for removal and a physics update runs,
// get_gluon for that pair returns nullptr.
// =============================================================================

bool test_gluon_removal_unindexes() {
    printf("\n=== Gluon Removal Unindexes ===\n");

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "gluon removal unindexes";
    cfg.enable_chat_window = false;
    if (engine.initialize(cfg) != 0) {
        printf("  ERROR: engine init failed\n");
        return false;
    }

    engine.get_physics_system().add_force(
        std::make_unique<GravityForce>(0.0f, 0.0f, -9.8f));

    // KINEMATIC anchor; heavy box hangs from it on a gluon whose
    // breaking_force is tiny, so gravity snaps it within a few frames.
    // Breaking goes through mark_gluon_for_removal → remove_marked_gluons,
    // the same path the pin-gluon lifecycle and strained joints use.
    int anchor = add_box(engine, 0.0f, 0.0f, 3.0f);
    int hanging = add_box(engine, 0.0f, 0.0f, 2.6f);

    auto& ps = engine.get_particle_system();
    {
        auto view = ps.lock_particles_for_write();
        view[anchor].solver_mode = ParticleSolverMode::KINEMATIC;
    }

    auto& physics = engine.get_physics_system();
    auto g = std::make_unique<NailGluon>();
    g->offset_a = Vec3{0.0f, 0.0f, 0.0f};
    g->offset_b = Vec3{0.0f, 0.0f, 0.0f};
    g->target_distance = 0.4f;
    g->stiffness = 10000.0f;
    g->damping = 100.0f;
    g->breaking_force = 1e-6f;   // snaps on first real impulse
    g->enable_angular_constraint = false;
    g->rotate_offsets = false;
    physics.add_gluon_between((size_t)anchor, (size_t)hanging, std::move(g));

    const GluonConstraintBase* before = physics.get_gluon((size_t)anchor, (size_t)hanging);
    printf("  %s: gluon indexed after creation\n", before ? "PASS" : "FAIL");
    if (!before) return false;

    // Run until the owning vector drops the broken gluon.
    bool broke = false;
    int frame = 0;
    for (; frame < 120; ++frame) {
        engine.update(1.0 / 60.0);
        if (physics.get_gluons_for_particle((size_t)hanging).empty()) {
            broke = true;
            break;
        }
    }
    printf("  %s: gluon broke and left the constraint list (frame %d)\n",
           broke ? "PASS" : "FAIL", frame + 1);
    if (!broke) {
        printf("  [FAIL — gluon never broke; scenario needs retuning]\n");
        return false;
    }

    const GluonConstraintBase* after = physics.get_gluon((size_t)anchor, (size_t)hanging);
    bool unindexed = (after == nullptr);
    printf("  %s: get_gluon returns null after break (got %p)\n",
           unindexed ? "PASS" : "FAIL", (const void*)after);

    printf("  [%s]\n", unindexed
           ? "PASS"
           : "FAIL — pair index still serves the freed gluon (dangling pointer)");
    return unindexed;
}
