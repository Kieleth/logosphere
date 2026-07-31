// =============================================================================
// GPU WAIT — no fixed sleep in the drain primitive
// =============================================================================
// 2026-07-29 live-stall RCA (task #21). Every chunk unload stalled the frame
// 61-83 ms. Attribution: [STALL-BREAKDOWN] GPU wait 61-83 ms, workers 0.00,
// delete 0.5 — the whole hitch was renderer_->wait_for_completion(), which
// carried a hardcoded usleep(50000) "waiting for drawable pool to drain".
//
// That drain guards CAMetalLayer.drawableSize changes. The resize path does
// NOT go through wait_for_completion: it holds acquire_all_gpu_slots() and
// calls force_drawable_resize(), which has its OWN 100 ms stabilization
// sleep. So the 50 ms was pure dead cost on every OTHER caller — deletion
// flush (every chunk unload), shutdown, scene reset, and every serialized
// headless oracle frame.
//
// This test locks the primitive's cost: on an idle GPU, wait_for_completion()
// must return promptly. It fails at ~50 ms if the sleep ever returns.
//
// Run: ./build/logosphere-tests --test test_gpu_wait_no_fixed_sleep --no-head
// =============================================================================

#include "../src/core/engine.h"
#include <cstdio>
#include <chrono>
#include <algorithm>
#include <vector>

bool test_gpu_wait_no_fixed_sleep() {
    printf("\n=== GPU wait: no fixed sleep in the drain primitive ===\n");

    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "gpu wait sleep guard";
    cfg.enable_chat_window = false;
    cfg.window_width = 640;
    cfg.window_height = 480;
    Engine engine;
    if (engine.initialize(cfg) != 0) {
        printf("  ERROR: engine init failed\n");
        return false;
    }

    // Warm the pipeline: shaders compiled, buffers allocated, slots cycled.
    for (int i = 0; i < 5; ++i) {
        engine.update(1.0 / 60.0);
        engine.render();
        engine.get_renderer().wait_for_completion();
    }

    // Measure the drain on a quiet GPU. Median of several calls: the honest
    // per-call cost, immune to one scheduling hiccup.
    std::vector<double> samples;
    for (int i = 0; i < 5; ++i) {
        engine.update(1.0 / 60.0);
        engine.render();
        auto t0 = std::chrono::high_resolution_clock::now();
        engine.get_renderer().wait_for_completion();
        auto t1 = std::chrono::high_resolution_clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    std::sort(samples.begin(), samples.end());
    double median = samples[samples.size() / 2];
    double worst  = samples.back();

    printf("  wait_for_completion: median=%.2f ms  worst=%.2f ms  (samples:", median, worst);
    for (double s : samples) printf(" %.1f", s);
    printf(")\n");

    // The removed sleep was 50 ms. A real drain of one small headless frame is
    // a few ms. 25 ms sits well above honest drain cost and well below any
    // reintroduced fixed sleep.
    const double BAR_MS = 25.0;
    if (median > BAR_MS) {
        printf("  FAIL: drain costs %.2f ms (bar %.1f) — a fixed sleep is back in\n"
               "        wait_for_completion, or the drain is doing more than it should.\n",
               median, BAR_MS);
        return false;
    }

    printf("  PASS (median %.2f ms < %.1f ms bar)\n", median, BAR_MS);
    return true;
}
