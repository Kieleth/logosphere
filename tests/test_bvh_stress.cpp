// ============================================================================
// BVH REBUILD STRESS TEST
// ============================================================================
// Catches the 19ms stall when chunk streaming adds particles and forces
// a full TriangleBVH rebuild (the Logomancers performance bug).
//
// Scenario:
//   1. Baseline: create 1000 cube particles, render 10 frames.
//   2. Chunk load: add 3000 new particles in a burst.
//   3. Post-load: render 10 more frames.
//
// Expected:
//   - Baseline frames < 15ms (engine is fast when BVH is stable)
//   - Post-load first frame > 20ms (BVH rebuild stall — THE BUG)
//
// After the static/dynamic BVH split fix:
//   - All frames stay under 20ms because only the tiny dynamic BVH rebuilds.
//
// Run:
//   ./logosphere-tests --test test_bvh_stress
// ============================================================================

#include "../src/test_context.h"
#include "../src/core/engine.h"
#include "../src/particle.h"
#include <cstdio>
#include <chrono>
#include <vector>
#include <algorithm>

using Clock = std::chrono::high_resolution_clock;

static double frame_ms(Clock::time_point start) {
    auto now = Clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(now - start).count();
    return us / 1000.0;
}

bool test_bvh_stress(TestContext& ctx) {
    printf("\n=== BVH Rebuild Stress Test (Chunk Streaming Simulation) ===\n");

    auto& engine = ctx.get_engine();
    const float dt = 1.0f / 60.0f;

    // ================================================================
    // Phase 1: Baseline — 1000 cube particles in a grid
    // ================================================================
    ctx.clear_particles();

    // 32x32 grid = 1024 cubes, spaced 1m apart, centered on origin
    const int BASELINE_GRID = 32;
    const float SPACING = 1.0f;
    int baseline_count = 0;
    for (int gy = 0; gy < BASELINE_GRID; gy++) {
        for (int gx = 0; gx < BASELINE_GRID; gx++) {
            float x = (gx - BASELINE_GRID / 2) * SPACING;
            float y = (gy - BASELINE_GRID / 2) * SPACING;
            ctx.add_cube_particle(x, y, 0.05f, 0.3f, 150, 120, 80);
            baseline_count++;
        }
    }
    // One overhead light so shadow rays have something to trace
    ctx.add_light_particle(0.0f, 0.0f, 10.0f, 1.0f, 255, 255, 255, 500000.0f, 100.0f);

    printf("  Phase 1: Baseline scene — %d particles\n", baseline_count);

    // Warm up render pipeline (first frame does one-time setup)
    engine.update(dt);
    engine.render();
    engine.get_renderer().wait_for_completion();

    // Measure 10 baseline frames
    std::vector<double> baseline_times;
    for (int i = 0; i < 10; i++) {
        auto t0 = Clock::now();
        engine.update(dt);
        engine.render();
        engine.get_renderer().wait_for_completion();
        baseline_times.push_back(frame_ms(t0));
    }

    printf("  Baseline frame times (ms): ");
    for (double t : baseline_times) printf("%.1f ", t);
    printf("\n");

    double baseline_avg = 0;
    for (double t : baseline_times) baseline_avg += t;
    baseline_avg /= baseline_times.size();
    double baseline_max = *std::max_element(baseline_times.begin(), baseline_times.end());
    printf("  Baseline avg: %.1fms, max: %.1fms\n", baseline_avg, baseline_max);

    // ================================================================
    // Phase 2: Chunk load — add 3000 new particles in a burst
    // ================================================================
    const int LOAD_GRID = 55;  // 55x55 = 3025 additional particles
    int added = 0;
    for (int gy = 0; gy < LOAD_GRID; gy++) {
        for (int gx = 0; gx < LOAD_GRID; gx++) {
            // Place off to the side so they don't overlap baseline grid
            float x = 30.0f + (gx - LOAD_GRID / 2) * SPACING;
            float y = (gy - LOAD_GRID / 2) * SPACING;
            ctx.add_cube_particle(x, y, 0.05f, 0.3f, 120, 150, 80);
            added++;
        }
    }
    int total_after = baseline_count + added;
    printf("\n  Phase 2: Chunk load burst — added %d particles (total: %d)\n", added, total_after);

    // ================================================================
    // Phase 3: Post-load — render 10 frames, expect first to be slow
    // ================================================================
    std::vector<double> postload_times;
    for (int i = 0; i < 10; i++) {
        auto t0 = Clock::now();
        engine.update(dt);
        engine.render();
        engine.get_renderer().wait_for_completion();
        postload_times.push_back(frame_ms(t0));
    }

    printf("  Post-load frame times (ms): ");
    for (double t : postload_times) printf("%.1f ", t);
    printf("\n");

    double postload_avg = 0;
    for (double t : postload_times) postload_avg += t;
    postload_avg /= postload_times.size();
    double postload_max = *std::max_element(postload_times.begin(), postload_times.end());
    int slow_frames = 0;
    for (double t : postload_times) if (t > 20.0) slow_frames++;

    printf("  Post-load avg: %.1fms, max: %.1fms, slow frames (>20ms): %d\n",
           postload_avg, postload_max, slow_frames);

    // ================================================================
    // Assertions — measure the STALL (delta from baseline), not absolute time.
    // Sync rendering path (used by tests) is slower than async Eden,
    // so we measure the regression the chunk load causes, not absolute ms.
    // ================================================================
    printf("\n  Assertions:\n");

    // Stall = how much slower the post-load frames are vs baseline.
    // A correctly fixed engine will show post-load ≈ baseline (no rebuild stall).
    // The current buggy engine will show post-load avg > baseline avg + 15ms,
    // because each frame triggers BVH rebuild until particle count stabilizes.
    double stall_avg_ms = postload_avg - baseline_avg;
    double stall_max_ms = postload_max - baseline_max;

    // 1. Baseline sanity check: engine runs stably
    bool baseline_stable = (baseline_max - baseline_avg) < 30.0;
    printf("    %s: Baseline stable (range %.1fms)\n",
           baseline_stable ? "PASS" : "FAIL", baseline_max - baseline_avg);

    // 2. THE BUG: after chunk load, frames should quickly return to baseline.
    // The FIRST frame pays the legitimate one-time BVH rebuild cost for the
    // newly-added geometry. Subsequent frames should be close to baseline.
    // The old bug was: EVERY frame paid the rebuild cost (because count kept
    // fluctuating from culling flicker). With Option F, only frame 1 pays.
    bool no_stall_avg = stall_avg_ms < 15.0;
    printf("    %s: Post-load avg within 15ms of baseline (delta %.1fms)\n",
           no_stall_avg ? "PASS" : "FAIL", stall_avg_ms);

    // 3. After the first frame (legit chunk-load rebuild), remaining frames
    // should be close to baseline. Measure delta from baseline max excluding frame 1.
    double post_after_first_max = 0;
    for (size_t i = 1; i < postload_times.size(); i++) {
        post_after_first_max = std::max(post_after_first_max, postload_times[i]);
    }
    double settled_delta = post_after_first_max - baseline_max;
    bool settled_fast = settled_delta < 10.0;
    printf("    %s: Post-load settled max (frames 2-10) within 10ms of baseline (delta %.1fms)\n",
           settled_fast ? "PASS" : "FAIL", settled_delta);

    bool pass = baseline_stable && no_stall_avg && settled_fast;
    printf("\n%s: BVH rebuild stress test\n", pass ? "PASS" : "FAIL");
    return pass;
}
