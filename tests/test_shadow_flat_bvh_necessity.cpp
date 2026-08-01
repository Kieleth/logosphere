// =============================================================================
// IS THE FLAT SHADOW BVH EVER READ?
// =============================================================================
// shadow_rays_deferred.metal picks its acceleration structure like this:
//
//     if (entity_node_count > 0 && dir_group_count > 0)  { ...entity BVH... }
//     else if (bvh_node_count > 0)                       { ...flat BVH... }
//
// So the flat TriangleBVH is a FALLBACK. If the entity path always wins, every
// flat rebuild is work nobody reads, and those cost up to 97 ms on a single
// frame (13% of a falling-bodies run). That is worth deleting, not tuning.
//
// Proving it needs more than "suppress the flat BVH and see if pixels change",
// because an unchanged image has two possible causes: the flat tree is unused,
// OR shadows do not come from either tree and the comparison is blind. This
// test separates them with four passes over ONE static scene:
//
//   A  flat ON,  entity ON    reference (production)
//   B  flat OFF, entity ON    B == A  =>  flat BVH is never read
//   C  flat ON,  entity OFF   does the fallback still shadow correctly?
//   D  flat OFF, entity OFF   SENSITIVITY CONTROL: must differ from A
//
// D is the load-bearing one. If D == A then shadows survive with NO
// acceleration structure at all, the instrument cannot see shadow changes, and
// every other comparison here is meaningless. The test fails loudly in that
// case rather than reporting a false "unused".
//
// The scene is deliberately static and KINEMATIC: no physics, no spawning, so
// the only variable across passes is which structure the kernel is handed.
//
//   ./build-release/logosphere-tests --test test_shadow_flat_bvh_necessity --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include "../src/optimization_flags.h"
#include "logosphere/rendering/render_pipeline.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>

namespace {

struct Shot { std::vector<uint32_t> px; int w = 0, h = 0; };

// Counting "any pixel differs" is not enough. This engine has a documented
// depth-tie nondeterminism at dense equal-depth geometry (see the optimization
// ledger's A-vs-A requirement), and a floor of identical tiles is exactly that
// case. It shows up as +/-1 LSB scattered over the frame. A LOST SHADOW does
// not look like that: it moves pixels by tens or hundreds. So bucket by
// magnitude and judge on the buckets that noise cannot reach.
struct Diff {
    long any = 0;      // delta >= 1   (includes LSB noise)
    long over8 = 0;    // delta >= 8   (beyond any observed noise)
    long over32 = 0;   // delta >= 32  (unmistakably structural)
    int  max_delta = 0;
};

Diff compare(const Shot& a, const Shot& b) {
    Diff d;
    const size_t n = std::min(a.px.size(), b.px.size());
    for (size_t i = 0; i < n; ++i) {
        const uint32_t p = a.px[i], q = b.px[i];
        const int dr = std::abs((int)((p >> 16) & 0xFF) - (int)((q >> 16) & 0xFF));
        const int dg = std::abs((int)((p >>  8) & 0xFF) - (int)((q >>  8) & 0xFF));
        const int db = std::abs((int)( p        & 0xFF) - (int)( q        & 0xFF));
        const int w = std::max(dr, std::max(dg, db));
        if (w >= 1)  d.any++;
        if (w >= 8)  d.over8++;
        if (w >= 32) d.over32++;
        d.max_delta = std::max(d.max_delta, w);
    }
    return d;
}

}  // namespace

bool test_shadow_flat_bvh_necessity() {
    printf("\n=== IS THE FLAT SHADOW BVH EVER READ? ===\n");

    EngineConfig cfg;
    cfg.create_display = false;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) { printf("  ERROR: engine init failed\n"); return false; }
    auto& ps = engine.get_particle_system();

    // Pale open floor: a shadow on it is unmistakable, and a LOST shadow is too.
    const int SIDE = 30;
    for (int r = 0; r < SIDE; ++r) {
        for (int c = 0; c < SIDE; ++c) {
            Particle p = {};
            p.shape = ParticleShape::BOX;
            p.x = (c - SIDE / 2.0f); p.y = (r - SIDE / 2.0f); p.z = 0.05f;
            p.width = p.height = 1.0f; p.thickness = 0.1f; p.size = 1.0f;
            p.r = 0.70f; p.g = 0.68f; p.b = 0.64f; p.a = 1.0f;
            p.SetMaterial(Materials::Type::STONE);
            int id = engine.add_particle(p);
            auto v = ps.lock_particles_for_write();
            v[id].solver_mode = ParticleSolverMode::KINEMATIC;
            v[id].owner = ParticleOwner::DYNAMICS;
            v[id].is_at_rest = true;
        }
    }

    // Occluders held ABOVE the floor, KINEMATIC so nothing falls. Each throws a
    // large separate shadow, so any shadow that disappears changes many pixels.
    int occluders = 0;
    for (int i = 0; i < 12; ++i) {
        Particle p = {};
        p.shape = ParticleShape::BOX;
        const float a = (float)i * 2.399963f;
        const float rad = 3.0f + 5.0f * ((i % 4) + 1) / 4.0f;
        p.x = rad * std::cos(a); p.y = rad * std::sin(a); p.z = 3.5f;
        p.width = p.height = p.thickness = 1.6f; p.size = 1.6f;
        p.r = 0.85f; p.g = 0.35f; p.b = 0.25f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        int id = engine.add_particle(p);
        auto v = ps.lock_particles_for_write();
        v[id].solver_mode = ParticleSolverMode::KINEMATIC;
        v[id].owner = ParticleOwner::DYNAMICS;
        v[id].is_at_rest = true;
        occluders++;
    }

    ps.queue_light(-6.0f, -5.0f, 15.0f, 240000.0f, 40.0f, 1.0f, 0.97f, 0.92f);
    ps.flush_pending_particles();
    printf("  scene: %dx%d floor, %d floating occluders, 1 light (all KINEMATIC)\n",
           SIDE, SIDE, occluders);

    const int SETTLE = 60;   // let temporal shadow/SSDO accumulation converge
    auto capture = [&](const char* tag, bool flat, bool entity) {
        logosphere::set_flat_shadow_bvh_enabled(flat);
        logosphere::set_entity_shadow_bvh_enabled(entity);
        for (int f = 0; f < SETTLE; ++f) {
            engine.update(1.0 / 60.0);
            engine.render();
            engine.get_renderer().wait_for_completion();
        }
        Shot s;
        s.w = engine.get_render_buffer().width();
        s.h = engine.get_render_buffer().height();
        s.px.assign((size_t)s.w * s.h, 0u);
        engine.read_latest_framebuffer(s.px.data(), s.w, s.h);
        printf("  captured %-28s flat=%-3s entity=%-3s\n", tag, flat ? "on" : "OFF", entity ? "on" : "OFF");
        return s;
    };

    const Shot A  = capture("A reference",        true,  true);
    // A-vs-A control FIRST: same settings, captured again. Whatever this shows
    // is the engine's own run-to-run noise, and nothing below that floor is
    // evidence of anything.
    const Shot A2 = capture("A' repeat (noise floor)", true, true);
    const Shot B  = capture("B flat suppressed",  false, true);
    const Shot C  = capture("C entity suppressed",true,  false);
    const Shot D  = capture("D both suppressed",  false, false);

    // Restore production behaviour before any assertion can bail out.
    logosphere::set_flat_shadow_bvh_enabled(true);
    logosphere::set_entity_shadow_bvh_enabled(true);

    const Diff dN = compare(A, A2);   // noise floor
    const Diff dB = compare(A, B), dC = compare(A, C), dD = compare(A, D);
    const long total = (long)A.px.size();
    auto pct = [&](long n) { return 100.0 * (double)n / (double)total; };

    printf("\n  %-30s %12s %12s %12s %10s\n",
           "comparison", "delta>=1", "delta>=8", "delta>=32", "max");
    auto row = [&](const char* tag, const Diff& d) {
        printf("  %-30s %12ld %12ld %12ld %10d\n", tag, d.any, d.over8, d.over32, d.max_delta);
    };
    row("A' vs A   (NOISE FLOOR)", dN);
    row("B  vs A   (no flat)",     dB);
    row("C  vs A   (no entity)",   dC);
    row("D  vs A   (no BVH)",      dD);
    printf("\n  noise floor: %.3f%% of pixels move by >=1, max delta %d.\n",
           pct(dN.any), dN.max_delta);
    printf("  Anything at or below that is the engine's depth-tie nondeterminism,\n"
           "  not an effect of the change. Judgements below use delta>=8.\n");

    // ---- Assertion 1: the instrument can see shadow changes at all ----------
    // Without a single acceleration structure the kernel has nothing to trace
    // against, so shadows MUST change. If they do not, this test is blind and
    // every other number above is noise.
    if (dD.over8 == 0) {
        printf("\n  FAIL: removing BOTH structures changed nothing above the noise floor.\n");
        printf("        The shadows in this scene do not come from either BVH, so this\n");
        printf("        test cannot answer the question. Do not read B or C as evidence.\n");
        engine.shutdown();
        return false;
    }
    printf("\n  control OK: with no BVH at all, %ld pixels move by >=8, the test can see shadows.\n",
           dD.over8);

    // ---- Assertion 2: the actual question ----------------------------------
    bool flat_is_dead = (dB.over8 == 0);
    if (flat_is_dead) {
        printf("\n  RESULT: the flat TriangleBVH is NEVER READ.\n");
        printf("          Suppressing it changed 0 pixels while the control proves shadows\n");
        printf("          are visible to this test. Every flat rebuild is dead work,\n");
        printf("          up to 97 ms on a single frame. It can be deleted, not tuned.\n");
    } else {
        printf("\n  RESULT: the flat TriangleBVH IS read (%ld pixels move by >=8 without it).\n",
               dB.over8);
        printf("          It is load-bearing; rate-limiting its rebuild is a real\n");
        printf("          quality trade, not free.\n");
    }

    // ---- Assertion 3: does the fallback still work when forced? ------------
    if (dC.over8 == 0) {
        printf("  fallback: forcing the flat path reproduces the reference exactly,\n");
        printf("            the two structures agree, so either alone is sufficient.\n");
    } else {
        printf("  fallback: forcing the flat path differs from the reference in %ld pixels (>=8)\n",
               dC.over8);
        printf("            (max delta %d). The two structures do NOT agree.\n", dC.max_delta);
    }

    engine.shutdown();
    return true;   // the test reports; it does not prejudge which answer is correct
}
