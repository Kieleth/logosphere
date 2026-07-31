// ============================================================================
// MEMORY LEAK TEST — GPU METAL ALLOCATION MONITORING
// ============================================================================
// Catches unbounded Metal GPU memory growth.
//
// The bug: Metal currentAllocatedSize grows 51MB/frame in Eden (6.17GB
// per 120 frames). Something is allocating massive Metal buffers every
// frame without releasing them.
//
// This test renders 200 frames and checks Metal's currentAllocatedSize
// directly. Fails if growth exceeds 500MB (a real leak at 51MB/frame
// would hit 10GB in 200 frames).
//
// Run:
//   ./logosphere-tests --test test_memory_leak
// ============================================================================

#include "../src/test_context.h"
#include "../src/core/engine.h"
#include "../src/particle.h"
#include "logosphere/worldgen/worldgen_system.h"
#include "logosphere/worldgen/floor_generator.h"
#include <cstdio>
#include <cmath>

// Forward declare the GPU memory query (implemented in gpu_rasterizer.mm)
namespace Logosphere {
    size_t get_metal_allocated_bytes();
}

bool test_memory_leak(TestContext& ctx) {
    printf("\n=== Memory Leak Test (Metal GPU Allocation) ===\n");

    auto& engine = ctx.get_engine();
    auto& ps = engine.get_particle_system();
    const float dt = 1.0f / 60.0f;

    ctx.clear_particles();
    ctx.add_light_particle(0.0f, 0.0f, 10.0f, 1.0f, 255, 255, 255, 500000.0f, 100.0f);

    // Configure streaming floor (same as Eden)
    auto& floor_gen = engine.get_worldgen_system().get_floor_generator();
    floor_gen.set_tile_size(4.0f);
    floor_gen.set_tile_thickness(0.1f);
    floor_gen.set_tiles_per_chunk(5);
    floor_gen.set_tiles_per_entity(1);
    floor_gen.set_load_radius(40.0f);
    floor_gen.set_unload_radius(50.0f);
    floor_gen.set_floor_type(FloorType::GRASS);
    floor_gen.set_enabled(true);

    float obs_x = 0.0f, obs_y = 0.0f;
    floor_gen.preload_chunks_around(obs_x, obs_y, 2);

    // Warm up
    for (int i = 0; i < 5; i++) {
        engine.update(dt);
        engine.render();
    }
    engine.get_renderer().wait_for_completion();

    size_t baseline_bytes = Logosphere::get_metal_allocated_bytes();
    double baseline_mb = baseline_bytes / (1024.0 * 1024.0);
    printf("  Baseline Metal allocated: %.0f MB\n", baseline_mb);

    // Render 500 frames with walking (triggers chunk streaming + RT rebuilds)
    printf("  Rendering 500 frames...\n\n");

    for (int frame = 0; frame < 500; frame++) {
        size_t before = Logosphere::get_metal_allocated_bytes();

        obs_x += 0.5f;
        floor_gen.update(obs_x, obs_y);
        engine.update(dt);
        engine.render();
        engine.get_renderer().wait_for_completion();

        size_t after = Logosphere::get_metal_allocated_bytes();
        size_t delta = after > before ? after - before : 0;
        if (delta > 1024 * 1024) {  // Log any frame with > 1MB allocation
            printf("  [ALLOC] Frame %d: +%zu MB (before=%zu MB after=%zu MB)\n",
                   frame, delta / (1024*1024), before / (1024*1024), after / (1024*1024));
        }

        if (frame % 50 == 49) {
            engine.get_renderer().wait_for_completion();
            size_t current_bytes = Logosphere::get_metal_allocated_bytes();
            double current_mb = current_bytes / (1024.0 * 1024.0);
            double growth_mb = current_mb - baseline_mb;
            int pc = ps.lock_particles_for_read().size();

            printf("  Frame %3d: Metal=%.0f MB (growth: %+.0f MB) particles=%d\n",
                   frame + 1, current_mb, growth_mb, pc);

            // SAFETY: abort if Metal allocation grows too fast
            if (growth_mb > 2000.0) {
                printf("\n  *** ABORT: Metal growth > 2GB. Active leak! ***\n");
                floor_gen.set_enabled(false);
                return false;
            }
        }
    }

    engine.get_renderer().wait_for_completion();
    size_t final_bytes = Logosphere::get_metal_allocated_bytes();
    double final_mb = final_bytes / (1024.0 * 1024.0);
    double total_growth_mb = final_mb - baseline_mb;

    printf("\n  Final Metal allocated: %.0f MB (growth: %+.0f MB)\n", final_mb, total_growth_mb);

    // At 51MB/frame leak rate, 200 frames = 10GB growth. Threshold 500MB.
    bool pass = total_growth_mb < 500.0;
    printf("\n%s: Memory leak (Metal growth %.0f MB, threshold < 500 MB)\n",
           pass ? "PASS" : "FAIL", total_growth_mb);

    floor_gen.set_enabled(false);
    return pass;
}
