// =============================================================================
// GPU OCCUPANCY SANITY: a metric that exceeds 100% of wall clock is not a
// metric, and this catches the whole class.
// =============================================================================
// WHAT WENT WRONG (2026-08-01). telemetry::gpu_window() publishes, per frame,
// the busy and span of that frame's command buffers. Summing busy_ms across
// frames and dividing by wall clock LOOKS like GPU occupancy. On Eden at retina
// it returned 122%, and was read as "the GPU is the bottleneck". It is not a
// measurement, it is double counting: the GPU runs about a frame behind the CPU,
// so consecutive frames' windows OVERLAP and the shared region is counted twice.
//
// The tell is that the broken metric AGREES with the correct one exactly when
// the answer does not matter. Windowed, where the GPU idles between frames and
// nothing overlaps, sum and union both say ~52%. At retina, where the answer
// decides what to optimise next, sum says 122% and the union says 95.8%.
//
// THE CORRECT COMPUTATION is the UNION of [start_s, end_s] across frames over
// the elapsed GPU timeline. This test is the reference implementation as well as
// the guard: any consumer wanting occupancy should do what merge_busy() does.
//
// WHAT THIS ASSERTS
//   1. The absolute bounds are populated at all. They exist so a consumer CAN
//      union; drop them and the double counting becomes invisible again, which
//      is how the defect survived. Bounds missing or non-monotonic is a failure.
//   2. Union occupancy is <= 100%. This is a physical invariant of one GPU
//      timeline. Breaking it means the data or the attribution is wrong.
//   3. Overlap is REPORTED, not asserted, because whether frames overlap is a
//      property of the scene and the resolution, not a correctness property.
//
// Deliberately does NOT call wait_for_completion() in the measured loop: that
// serialises CPU and GPU and would erase the very overlap under test.
//
//   ./build-release/logosphere-tests --test test_gpu_occupancy_sanity --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include "../src/core/telemetry.h"
#include "logosphere/rendering/gpu/gpu_rasterizer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

struct Win { double start, end; };

// Merge overlapping intervals, then total their length. This is the whole fix:
// sum(end-start) double counts overlap, union does not.
double merge_busy(std::vector<Win> w, double& timeline_out, size_t& merged_out) {
    if (w.empty()) { timeline_out = 0.0; merged_out = 0; return 0.0; }
    std::sort(w.begin(), w.end(), [](const Win& a, const Win& b) { return a.start < b.start; });
    std::vector<Win> merged{w.front()};
    for (size_t i = 1; i < w.size(); ++i) {
        if (w[i].start <= merged.back().end) merged.back().end = std::max(merged.back().end, w[i].end);
        else                                 merged.push_back(w[i]);
    }
    double busy = 0.0;
    for (const Win& m : merged) busy += m.end - m.start;
    timeline_out = merged.back().end - merged.front().start;
    merged_out   = merged.size();
    return busy;
}

}  // namespace

bool test_gpu_occupancy_sanity() {
    namespace T = ::logosphere::telemetry;
    printf("\n=== GPU OCCUPANCY SANITY (a metric cannot exceed wall clock) ===\n");

    EngineConfig cfg;
    cfg.create_display = false;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) { printf("  ERROR: engine init failed\n"); return false; }
    auto& ps = engine.get_particle_system();

    // Enough geometry and lights that the GPU has real work, so the windows are
    // wide enough for overlap to be possible. Static and at rest: this test is
    // about the measurement, not about physics.
    const int SIDE = 30;
    for (int r = 0; r < SIDE; ++r)
        for (int c = 0; c < SIDE; ++c) {
            Particle p = {};
            p.shape = ParticleShape::BOX;
            p.x = (float)(c - SIDE / 2); p.y = (float)(r - SIDE / 2); p.z = 0.05f;
            p.width = p.height = 1.0f; p.thickness = 0.1f; p.size = 1.0f;
            p.r = 0.70f; p.g = 0.68f; p.b = 0.64f; p.a = 1.0f;
            p.SetMaterial(Materials::Type::STONE);
            int id = engine.add_particle(p);
            auto v = ps.lock_particles_for_write();
            v[id].solver_mode = ParticleSolverMode::KINEMATIC;
            v[id].owner = ParticleOwner::DYNAMICS; v[id].is_at_rest = true;
        }
    for (int i = 0; i < 24; ++i) {
        Particle p = {};
        p.shape = ParticleShape::SPHERE;
        const float a = (float)i * 2.399963f, rad = 3.0f + 5.0f * ((i % 5) + 1) / 5.0f;
        p.x = rad * std::cos(a); p.y = rad * std::sin(a); p.z = 2.0f + 0.4f * (i % 6);
        p.width = p.height = p.thickness = 1.4f; p.size = 1.4f;
        p.r = 0.85f; p.g = 0.35f; p.b = 0.25f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        int id = engine.add_particle(p);
        auto v = ps.lock_particles_for_write();
        v[id].solver_mode = ParticleSolverMode::KINEMATIC;
        v[id].owner = ParticleOwner::DYNAMICS; v[id].is_at_rest = true;
    }
    for (int i = 0; i < 4; ++i) {
        const float a = (float)i * 1.5707963f;
        ps.queue_light(9.0f * std::cos(a), 9.0f * std::sin(a), 13.0f,
                       200000.0f, 40.0f, 1.0f, 0.97f, 0.92f);
    }
    ps.flush_pending_particles();

    const int WARMUP = 60, MEASURED = 200;
    for (int f = 0; f < WARMUP; ++f) { engine.update(1.0 / 60.0); engine.render(); }

    std::vector<Win> wins;
    double cpu_wall_ms = 0.0;
    uint64_t last_seen = UINT64_MAX;
    for (int f = 0; f < MEASURED; ++f) {
        engine.update(1.0 / 60.0);
        engine.render();                      // NO wait_for_completion: see header
        const T::GpuWindow w = T::gpu_window();
        // gpu_window() republishes the same frame until a newer one completes,
        // so de-duplicate on the bounds rather than counting frames.
        if (w.end_s > w.start_s) {
            const uint64_t key = (uint64_t)(w.start_s * 1e6);
            if (key != last_seen) { wins.push_back({w.start_s, w.end_s}); last_seen = key; }
        }
        cpu_wall_ms += T::phase_ms(T::Phase::Frame);
    }

    bool ok = true;

    // (1) bounds must exist. Without them no consumer can union, which is how
    // the sum-based number went unchallenged for as long as it did.
    if (wins.empty()) {
        printf("\n  FAIL: no GPU windows carried absolute bounds. start_s/end_s are what\n"
               "        make correct occupancy computable at all. See telemetry.h.\n");
        engine.shutdown();
        return false;
    }

    double timeline = 0.0; size_t merged = 0;
    const double busy_union = merge_busy(wins, timeline, merged);
    double naive = 0.0;
    for (const Win& w : wins) naive += w.end - w.start;

    const double occ_union = timeline > 0.0 ? busy_union / timeline : 0.0;
    const double occ_naive = timeline > 0.0 ? naive      / timeline : 0.0;

    printf("\n  frames measured        %d  (%zu distinct GPU windows)\n", MEASURED, wins.size());
    printf("  CPU wall               %.3f s\n", cpu_wall_ms / 1000.0);
    printf("  GPU timeline           %.3f s\n", timeline);
    printf("  GPU busy (UNION)       %.3f s   occupancy %6.1f%%   <- correct\n", busy_union, occ_union * 100.0);
    printf("  GPU busy (naive sum)   %.3f s   occupancy %6.1f%%\n", naive, occ_naive * 100.0);
    printf("  windows merged into    %zu intervals  (fewer than %zu means frames overlap)\n",
           merged, wins.size());

    // (2) the physical invariant. One GPU, one timeline.
    if (occ_union > 1.0001) {
        printf("\n  FAIL: union occupancy %.1f%% exceeds 100%%. One GPU cannot be busy longer\n"
               "        than the elapsed time. Either the windows are attributed to the wrong\n"
               "        frame or the bounds are not on a single clock.\n", occ_union * 100.0);
        ok = false;
    } else {
        printf("\n  invariant OK: union occupancy is within 100%%.\n");
    }

    // (3) overlap is scene-dependent, so report it and do not judge it.
    if (merged < wins.size()) {
        printf("  overlap present: %zu windows merged away. The naive sum reads %.1f%% here,\n"
               "                   overstating by %.1f points. This is the defect this test guards.\n",
               wins.size() - merged, occ_naive * 100.0, (occ_naive - occ_union) * 100.0);
    } else {
        printf("  no overlap in this run, so sum and union agree (%.1f%% vs %.1f%%). That is\n"
               "                   expected on an idle GPU and is NOT evidence the sum is safe.\n",
               occ_naive * 100.0, occ_union * 100.0);
    }

    // (4) the serialized diagnostic mode must be OFF unless explicitly asked
    // for. It blocks on every pass, destroying CPU/GPU overlap by design, and
    // measured 51.00 ms against 20.08 ms pipelined on Eden at retina. Shipping
    // with it on would halve the frame rate for a profiling aid.
    if (Logosphere::gpu_serialized_diagnostic() && !std::getenv("LOGOSPHERE_GPU_SERIALIZED")) {
        printf("\n  FAIL: serialized diagnostic mode is ON without LOGOSPHERE_GPU_SERIALIZED.\n"
               "        It is a profiling aid that costs ~2.5x frame time. It must never\n"
               "        be the default. See gpu_rasterizer.h.\n");
        ok = false;
    } else {
        printf("  serialized mode OFF by default, as required.\n");
    }

    printf("\n  %s\n", ok ? "PASS" : "FAIL");
    engine.shutdown();
    return ok;
}
