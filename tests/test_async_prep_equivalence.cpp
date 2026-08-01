// =============================================================================
// ASYNC GPU PREP EQUIVALENCE: does moving prep off the main thread change the
// picture, or only when it finishes?
// =============================================================================
// WHY. prepare_gpu_data runs synchronously on the main thread and costs 4.58 ms
// of a 12.89 ms CPU render at retina. Optimizations::USE_ASYNC_GPU_PREP exists
// to overlap it with the GPU, and was TRUE at the initial commit. It was set
// false on 2026-04-04 "(for testing)" during an Eva-shadow investigation, which
// concluded the shadows were correct and recorded that the bug it chased
// reproduced in BOTH sync and async modes. It was never restored. Four months
// unexercised.
//
// Moving work to another thread must not change a single pixel. This asserts
// that, before any performance claim is allowed to matter.
//
// WHAT IT ASSERTS
//   1. A-vs-A noise floor FIRST. This engine moves ~30,000 pixels re-rendering
//      an identical frame (equal-depth depth-tie nondeterminism), so "pixels
//      differ" is not a signal and every verdict is on delta>=8.
//   2. Sync and async agree above that floor.
//   3. Shadow triangle counts match. In April the reported symptom was shadow
//      triangles collapsing from 14,580 to 2,604 mid-run. A pixel diff on a
//      settled frame can miss a transient like that; the counter cannot.
//   4. Async actually engaged, via the RenderHandoff phase being non-zero.
//      Without this the test passes trivially when the lever does nothing,
//      which is the blind-test failure that cost five runs on the shadow seam.
//
// KNOWN HAZARD, deliberately left for this test to expose rather than papered
// over: the async worker captures `camera_system` BY REFERENCE while surfaces
// and particles are copied by value precisely because the detached thread
// outlives the caller. prepare_gpu_data reads the camera (get_forward_vector,
// get_look_at_target, and per-surface colour). If the main thread moves the
// camera while the worker reads it, prep sees a torn camera and this test
// should catch it as divergence. CameraSystem holds a unique_ptr, so it is
// move-only and cannot simply be snapshotted; fixing it properly means passing
// the values prep needs. Do that if this test goes red on a moving camera.
//
//   ./build-release/logosphere-tests --test test_async_prep_equivalence --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include "../src/core/telemetry.h"
#include "logosphere/rendering/render_pipeline.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

namespace {

struct Shot {
    std::vector<uint32_t> px;
    double handoff_ms = 0.0;
    double prep_ms    = 0.0;
    uint64_t shadow_tris = 0;
};

struct Diff { long any = 0, over8 = 0; int max_delta = 0; };

Diff compare(const Shot& a, const Shot& b) {
    Diff d;
    const size_t n = std::min(a.px.size(), b.px.size());
    for (size_t i = 0; i < n; ++i) {
        const uint32_t p = a.px[i], q = b.px[i];
        const int w = std::max({
            std::abs((int)((p >> 16) & 0xFF) - (int)((q >> 16) & 0xFF)),
            std::abs((int)((p >>  8) & 0xFF) - (int)((q >>  8) & 0xFF)),
            std::abs((int)( p        & 0xFF) - (int)( q        & 0xFF))});
        if (w >= 1) d.any++;
        if (w >= 8) d.over8++;
        d.max_delta = std::max(d.max_delta, w);
    }
    return d;
}

}  // namespace

bool test_async_prep_equivalence() {
    namespace T = ::logosphere::telemetry;
    printf("\n=== ASYNC GPU PREP EQUIVALENCE ===\n");

    EngineConfig cfg;
    cfg.create_display = false;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) { printf("  ERROR: engine init failed\n"); return false; }
    // Without this every phase and counter reads 0.00 and the test is blind.
    // The sensitivity check below caught exactly that on the first run.
    T::set_enabled(true);
    auto& ps = engine.get_particle_system();

    // Sparse occluders over a pale floor: a lost or stale shadow moves a lot of
    // pixels. A dense pile cannot show a missing shadow, everything is already
    // dark. Spheres included because they carry 320 shadow triangles each and
    // are what made the April shadow-triangle count move.
    const int SIDE = 24;
    for (int r = 0; r < SIDE; ++r)
        for (int c = 0; c < SIDE; ++c) {
            Particle p = {};
            p.shape = ParticleShape::BOX;
            p.x = (float)(c - SIDE / 2); p.y = (float)(r - SIDE / 2); p.z = 0.05f;
            p.width = p.height = 1.0f; p.thickness = 0.1f; p.size = 1.0f;
            p.r = 0.72f; p.g = 0.70f; p.b = 0.66f; p.a = 1.0f;
            p.SetMaterial(Materials::Type::STONE);
            int id = engine.add_particle(p);
            auto v = ps.lock_particles_for_write();
            v[id].solver_mode = ParticleSolverMode::KINEMATIC;
            v[id].owner = ParticleOwner::DYNAMICS; v[id].is_at_rest = true;
        }
    for (int i = 0; i < 14; ++i) {
        Particle p = {};
        p.shape = (i % 2) ? ParticleShape::SPHERE : ParticleShape::BOX;
        const float a = (float)i * 2.399963f, rad = 3.0f + 4.0f * ((i % 4) + 1) / 4.0f;
        p.x = rad * std::cos(a); p.y = rad * std::sin(a); p.z = 2.6f;
        p.width = p.height = p.thickness = 1.7f; p.size = 1.7f;
        p.r = 0.86f; p.g = 0.34f; p.b = 0.24f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        int id = engine.add_particle(p);
        auto v = ps.lock_particles_for_write();
        v[id].solver_mode = ParticleSolverMode::KINEMATIC;
        v[id].owner = ParticleOwner::DYNAMICS; v[id].is_at_rest = true;
    }
    ps.queue_light(-6.0f, -5.0f, 13.0f, 240000.0f, 40.0f, 1.0f, 0.97f, 0.92f);
    ps.flush_pending_particles();

    // Long enough for temporal shadow and SSDO accumulation to converge, and
    // for async to get past its synchronous first frame and reach steady state.
    const int SETTLE = 70;
    auto run = [&](bool async) {
        ::logosphere::set_async_gpu_prep(async);
        double handoff = 0.0, prep = 0.0;
        for (int f = 0; f < SETTLE; ++f) {
            engine.update(1.0 / 60.0);
            engine.render();
            engine.get_renderer().wait_for_completion();
            handoff += T::phase_ms(T::Phase::RenderHandoff);
            prep    += T::phase_ms(T::Phase::RenderPrep);
        }
        Shot s;
        s.handoff_ms = handoff;
        s.prep_ms    = prep;
        s.shadow_tris = (uint64_t)T::counter_value(T::Counter::ShadowTrianglesBuilt);
        int w = engine.get_render_buffer().width();
        int h = engine.get_render_buffer().height();
        s.px.assign((size_t)w * h, 0u);
        engine.read_latest_framebuffer(s.px.data(), w, h);
        return s;
    };

    const Shot sync_a = run(false);
    const Shot sync_b = run(false);      // A-vs-A: this engine's own noise floor
    const Shot async_ = run(true);
    ::logosphere::set_async_gpu_prep(false);   // restore

    const Diff noise = compare(sync_a, sync_b);
    const Diff got   = compare(sync_a, async_);

    printf("\n  %-30s %10s %10s %8s\n", "comparison", "delta>=1", "delta>=8", "max");
    printf("  %-30s %10ld %10ld %8d\n", "sync' vs sync (NOISE FLOOR)", noise.any, noise.over8, noise.max_delta);
    printf("  %-30s %10ld %10ld %8d\n", "async vs sync", got.any, got.over8, got.max_delta);
    printf("\n  shadow triangles   sync %llu   async %llu\n",
           (unsigned long long)sync_a.shadow_tris, (unsigned long long)async_.shadow_tris);
    printf("  render_handoff     sync %.3f ms   async %.3f ms  (summed over %d frames)\n",
           sync_a.handoff_ms, async_.handoff_ms, SETTLE);
    printf("  render_prep        sync %.3f ms   async %.3f ms\n", sync_a.prep_ms, async_.prep_ms);

    bool ok = true;

    // (4) sensitivity: if the lever changed nothing, nothing below is evidence.
    if (async_.handoff_ms <= 0.0) {
        printf("\n  FAIL: RenderHandoff is zero in async mode, so async prep never engaged.\n"
               "        The comparison below is blind and must not be read as agreement.\n");
        ok = false;
    } else {
        printf("\n  engaged OK: async mode recorded handoff work.\n");
    }

    // (3) the April symptom was a triangle count collapsing, which a settled
    // pixel diff can miss entirely.
    if (sync_a.shadow_tris != async_.shadow_tris) {
        printf("  FAIL: shadow triangle count differs, %llu sync vs %llu async. Geometry\n"
               "        reaching the shadow path depends on WHERE prep runs, which is a\n"
               "        handoff bug, not a scheduling difference.\n",
               (unsigned long long)sync_a.shadow_tris, (unsigned long long)async_.shadow_tris);
        ok = false;
    } else {
        printf("  shadow geometry OK: identical triangle counts.\n");
    }

    // (2) pixels, judged only above the measured floor.
    if (got.over8 > noise.over8) {
        printf("  FAIL: async differs from sync on %ld pixels by >=8 (floor is %ld, max delta %d).\n"
               "        Moving work between threads must not change the image. Prime suspect is\n"
               "        the camera captured BY REFERENCE into the detached worker; see header.\n",
               got.over8, noise.over8, got.max_delta);
        ok = false;
    } else {
        printf("  image OK: async is within the A-vs-A noise floor (%ld vs %ld pixels >=8).\n",
               got.over8, noise.over8);
    }

    printf("\n  %s\n", ok ? "PASS" : "FAIL");
    engine.shutdown();
    return ok;
}
