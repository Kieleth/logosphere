// =============================================================================
// ASYNC GPU PREP UNDER PARTICLE CHURN (issue #31)
// =============================================================================
// THE BUG, in plain terms.
//
// Rendering a frame takes two things: the shapes of every body, and a lookup
// saying which entity each body belongs to, so the renderer knows what colour
// and material to shade it with.
//
// Async prep moves the shape-building work onto a background thread so it can
// run while the GPU draws the previous frame. To do that safely it takes a COPY
// of every particle and hands it to the worker. But the entity lookup was not
// copied: the worker asked the live world for it, one frame later.
//
// That is fine while nothing is created or destroyed. It is not fine in Eden,
// where terrain streams in and out constantly and particles are deleted in
// batches of seventy-odd. Deletion here works by swap-and-pop: to remove an
// item, the last one is moved into its slot. Cheap, and it means every survivor
// can get a NEW index. So the worker held body #400's shape while the lookup
// had already reassigned #400 to a completely different entity, and the body
// was drawn with that other entity's material. Foliage went black, because
// grass is the most numerous and most-churned thing in the world and so the
// most likely to be caught mid-reshuffle.
//
// WHY THE EXISTING TEST COULD NOT CATCH IT.
//
// tests/test_async_prep_equivalence.cpp proves sync and async produce identical
// pixels, and it is a good test. Its scene is completely static: every particle
// is KINEMATIC, at rest, and nothing is ever added or removed. The bug needs
// churn to exist at all, so that test was blind to it by construction, not by
// oversight. A passing test is only evidence about the situations it creates.
//
// WHAT THIS TEST DOES INSTEAD.
//
// It creates churn on purpose (spawn and delete bodies every frame, forcing the
// reindexing) and asserts the mechanical invariant that the fix establishes:
// THE ENTITY LOOKUP IS NEVER READ FROM THE WORKER THREAD. Not "the pixels
// happen to match", which would depend on catching the race in the act, but the
// structural property that makes the race impossible.
//
//   ./build-release/logosphere-tests --test test_async_prep_churn --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include "logosphere/rendering/render_pipeline.h"
#include "../src/core/particle_system.h"
#include <cstdio>
#include <vector>

namespace {

// Spawns and deletes bodies every frame so render indices keep being reassigned
// by swap-and-pop. Returns how many deletions were actually requested, because
// a test that churns nothing would pass for the wrong reason.
int run_with_churn(Engine& engine, bool async, int frames) {
    ::logosphere::set_async_gpu_prep(async);
    auto& ps = engine.get_particle_system();
    std::vector<int> live;
    int deletions = 0;

    for (int f = 0; f < frames; ++f) {
        // Spawn three.
        for (int k = 0; k < 3; ++k) {
            Particle p = {};
            p.shape = ParticleShape::BOX;
            p.x = (float)((f * 3 + k) % 7) - 3.0f;
            p.y = (float)((f + k) % 5) - 2.0f;
            p.z = 2.0f + (float)k * 0.4f;
            p.width = p.height = p.thickness = 0.4f; p.size = 0.4f;
            p.r = 0.2f + 0.1f * k; p.g = 0.7f; p.b = 0.3f; p.a = 1.0f;
            p.SetMaterial(Materials::Type::STONE);
            live.push_back(engine.add_particle(p));
        }
        ps.flush_pending_particles();

        // Delete a batch once there are enough, which is what forces the
        // swap-and-pop reindexing the bug depended on.
        if ((int)live.size() > 12) {
            for (int k = 0; k < 5 && !live.empty(); ++k) {
                ps.delete_particle_immediate(live.front());
                live.erase(live.begin());
                deletions++;
            }
        }

        engine.update(1.0 / 60.0);
        engine.render();
        engine.present();
    }
    engine.get_renderer().wait_for_completion();
    ::logosphere::set_async_gpu_prep(false);
    return deletions;
}

}  // namespace

bool test_async_prep_churn() {
    printf("\n=== ASYNC GPU PREP UNDER PARTICLE CHURN (issue #31) ===\n");
    printf("Bodies are spawned and deleted every frame, so the renderer's\n");
    printf("index-to-entity lookup keeps being reshuffled underneath it.\n\n");

    EngineConfig cfg;
    cfg.create_display = false;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) { printf("  ERROR: engine init failed\n"); return false; }

    auto& ps = engine.get_particle_system();
    for (int c = -3; c <= 3; ++c)
        for (int d = -3; d <= 3; ++d) {
            Particle p = {};
            p.shape = ParticleShape::BOX;
            p.x = (float)c; p.y = (float)d; p.z = 0.05f;
            p.width = p.height = 1.0f; p.thickness = 0.1f; p.size = 1.0f;
            p.r = p.g = p.b = 0.5f; p.a = 1.0f;
            p.SetMaterial(Materials::Type::STONE);
            int id = engine.add_particle(p);
            auto v = ps.lock_particles_for_write();
            v[id].solver_mode = ParticleSolverMode::KINEMATIC;
            v[id].owner = ParticleOwner::DYNAMICS;
            v[id].is_at_rest = true;
        }
    ps.queue_light(-3.0f, -4.0f, 9.0f, 200000.0f, 40.0f, 1.0f, 0.97f, 0.92f);
    ps.flush_pending_particles();

    // Warm up synchronously first, so the guard latches the frame thread from
    // the ordinary path rather than from whichever thread happens to run first.
    ::logosphere::reset_render_kg_snapshot_guard();
    const int sync_deletions = run_with_churn(engine, /*async=*/false, 20);
    const uint64_t after_sync = ::logosphere::render_kg_snapshots_off_main_thread();

    const int async_deletions = run_with_churn(engine, /*async=*/true, 60);
    const uint64_t after_async = ::logosphere::render_kg_snapshots_off_main_thread();

    printf("  %-42s %8d\n", "deletions forced, sync phase", sync_deletions);
    printf("  %-42s %8d\n", "deletions forced, async phase", async_deletions);
    printf("  %-42s %8llu\n", "KG lookups off the frame thread, sync",
           (unsigned long long)after_sync);
    printf("  %-42s %8llu\n", "KG lookups off the frame thread, async",
           (unsigned long long)after_async);

    // Control first. If nothing was deleted, no reindexing happened, and a
    // clean result would mean nothing at all.
    if (async_deletions == 0) {
        printf("\n  *** NO CHURN HAPPENED. ***\n"
               "  Nothing was deleted, so no render index was ever reassigned and this\n"
               "  test did not exercise the bug. Its verdict would be meaningless.\n");
        printf("\n  FAIL\n");
        engine.shutdown();
        return false;
    }

    const bool clean = (after_async == 0);
    printf("\n");
    if (clean) {
        printf("  CLEAN. %d deletions forced reindexing across 60 async frames and the\n"
               "  entity lookup was never once read from a worker. The lookup is now\n"
               "  captured on the frame thread beside the particle snapshot, so the two\n"
               "  always describe the same instant and cannot disagree.\n", async_deletions);
    } else {
        printf("  *** REGRESSED. %llu entity lookups were taken off the frame thread. ***\n"
               "  Each one resolves a particle array captured at one instant through a\n"
               "  mapping read at another. With swap-and-pop deletion in between, bodies\n"
               "  get shaded with the wrong entity's material. This is issue #31 and it\n"
               "  shows up as foliage rendering black in Eden.\n",
               (unsigned long long)after_async);
    }

    printf("\n  %s\n", clean ? "PASS" : "FAIL");
    engine.shutdown();
    return clean;
}
