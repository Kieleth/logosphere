// Orbit performance study (issue #9 follow-up): frame cost while the
// camera azimuth animates vs. parked. Interactive report: 6-10 FPS
// during orbit in a full Logogenesis scene.
//
// Protocol (A/B/A, wall-clock per Engine::update):
//   A: 180 frames parked at azimuth 0 (after 60 warmup frames)
//   B: 180 frames orbiting (one full revolution)
//   C: 180 frames parked again at the landing bearing
//
// The study PRINTS everything measured. The one hard assert is the
// persistence ratchet: C must not stay slower than A (a stuck
// invalidation or cache-thrash bug would show as C >> A). B/A is the
// study's headline number and is printed, not asserted, until the
// mechanism behind it is decided on the issue.
//
// Usage:
//   ./build/test_orbit_performance

#include "../src/core/engine.h"
#include "../src/core/particle_system.h"
#include "../src/core/camera_system.h"
#include "logosphere/worldgen/strata_generator.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

static int tests_passed = 0;
static int tests_failed = 0;

namespace {

struct PhaseStats {
    double avg_ms = 0.0, median_ms = 0.0, p90_ms = 0.0, max_ms = 0.0;
};

PhaseStats run_phase(Engine& engine, int frames,
                     const std::function<void(int)>& per_frame) {
    // Headless: update() does not render; drive the full pipeline
    // explicitly and block on GPU completion so the timing covers the
    // whole frame (the pattern test_engine_headless_render locks).
    auto& renderer = engine.get_renderer();
    std::vector<double> ms;
    ms.reserve(frames);
    for (int i = 0; i < frames; ++i) {
        per_frame(i);
        auto t0 = std::chrono::steady_clock::now();
        engine.update(1.0 / 60.0);
        engine.render();
        renderer.wait_for_completion();
        auto t1 = std::chrono::steady_clock::now();
        ms.push_back(
            std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    std::sort(ms.begin(), ms.end());
    PhaseStats st;
    for (double v : ms) st.avg_ms += v;
    st.avg_ms /= ms.size();
    st.median_ms = ms[ms.size() / 2];
    st.p90_ms = ms[static_cast<size_t>(ms.size() * 0.9)];
    st.max_ms = ms.back();
    return st;
}

void print_phase(const char* name, const PhaseStats& st) {
    std::cout << "  [phase " << name << "] avg=" << st.avg_ms
              << " ms median=" << st.median_ms << " ms p90=" << st.p90_ms
              << " ms max=" << st.max_ms << " ms (avg "
              << (1000.0 / st.avg_ms) << " fps)" << std::endl;
}

}  // namespace

int main() {
    std::cout << "Orbit performance study (A/B/A)" << std::endl;

    Engine engine(nullptr);
    EngineConfig config;
    config.create_display = false;
    config.window_width = 1600;
    config.window_height = 1200;
    config.window_title = "orbit-perf";
    config.show_debug_overlay = false;
    config.enable_chat_window = false;
    if (engine.initialize(config) < 0)
        throw std::runtime_error("Engine::initialize() failed headless");

    // A Logogenesis-shaped scene: layered earth (the heavy part),
    // a strong overhead light (shadow work), and a stack of boxes
    // (something to occlude and swing around).
    auto specs = StrataGenerator::earth_preset();
    StrataGenerator::ChunkBounds bounds{-8.0f, 8.0f, -8.0f, 8.0f};
    std::mt19937 rng(5);
    auto ground = StrataGenerator::generate(engine, specs, bounds, rng);
    float top = ground.layers[2].max_top_z;

    Particle light = {};
    light.shape = ParticleShape::SPHERE;
    light.x = 6.0f; light.y = -6.0f; light.z = top + 25.0f;
    light.size = light.width = light.height = light.thickness = 0.5f;
    light.r = 1.0f; light.g = 0.95f; light.b = 0.85f; light.a = 1.0f;
    light.is_light_source = true;
    light.emission_strength = 30000000.0f;
    light.emission_radius = 80.0f;
    engine.add_particle(light);

    for (int i = 0; i < 3; ++i) {
        Particle box = {};
        box.shape = ParticleShape::BOX;
        box.x = -1.0f; box.y = 1.0f; box.z = top + 0.6f + 1.2f * i;
        box.width = box.height = box.thickness = box.size = 1.2f;
        box.r = 0.7f; box.g = 0.5f; box.b = 0.3f; box.a = 1.0f;
        box.SetMaterial(Materials::Type::STONE);
        engine.add_particle(box);
    }

    size_t particle_count;
    {
        auto view = engine.get_particle_system().lock_particles_for_read();
        particle_count = view.size();
    }
    std::cout << "  [scene] " << particle_count << " particles, light + "
              << "3-box stack on layered earth" << std::endl;

    auto& cam = engine.get_camera_system();

    // Warmup: temporal buffers, caches, first-frame allocations.
    for (int i = 0; i < 60; ++i) {
        engine.update(1.0 / 60.0);
        engine.render();
        engine.get_renderer().wait_for_completion();
    }

    auto a = run_phase(engine, 180, [](int) {});
    print_phase("A static ", a);

    const float TWO_PI = 6.2831853f;
    auto b = run_phase(engine, 180, [&](int i) {
        cam.set_view_azimuth(TWO_PI * (i + 1) / 180.0f);
    });
    print_phase("B orbit  ", b);

    cam.set_view_azimuth(TWO_PI);  // parked exactly one lap later
    auto c = run_phase(engine, 180, [](int) {});
    print_phase("C static'", c);

    // Ratios are taken on MEDIANS, not averages. Frame times here are
    // spiky by nature (periodic BVH rebuilds, issue #6: p90 runs
    // 1.5-2x median), and an average over 180 frames inherits however
    // many spikes that phase happened to catch. Measured over 5 idle
    // runs: average-based residue spread 0.92-1.10, median-based
    // 0.988-1.008. On a loaded machine the average form exceeded 1.30
    // and failed this test spuriously; the median form is what
    // actually answers "did the typical frame get slower".
    double slowdown = b.median_ms / a.median_ms;
    double residue = c.median_ms / a.median_ms;
    std::cout << "  [measure] orbit/static slowdown x" << slowdown
              << ", post-orbit residue x" << residue
              << " (medians; avg-based residue x" << (c.avg_ms / a.avg_ms)
              << ")" << std::endl;

    // Scale check: a Logogenesis-sized world (4x the ground area, more
    // lights). If the interactive 6-10 FPS were an orbit interaction,
    // the orbit/static ratio would grow with scene size; if the scene
    // is simply heavy, static and orbit stay matched and BOTH are slow.
    {
        StrataGenerator::ChunkBounds big{-16.0f, 16.0f, -16.0f, 16.0f};
        std::mt19937 rng2(9);
        auto more = StrataGenerator::generate(engine, specs, big, rng2);
        (void)more;
        for (int i = 0; i < 2; ++i) {
            Particle lamp = {};
            lamp.shape = ParticleShape::SPHERE;
            lamp.x = -10.0f + 20.0f * i; lamp.y = 10.0f; lamp.z = top + 20.0f;
            lamp.size = lamp.width = lamp.height = lamp.thickness = 0.5f;
            lamp.r = 1.0f; lamp.g = 0.9f; lamp.b = 0.8f; lamp.a = 1.0f;
            lamp.is_light_source = true;
            lamp.emission_strength = 20000000.0f;
            lamp.emission_radius = 60.0f;
            engine.add_particle(lamp);
        }
        {
            auto view = engine.get_particle_system().lock_particles_for_read();
            std::cout << "  [scene2] " << view.size() << " particles, 3 lights"
                      << std::endl;
        }
        for (int i = 0; i < 60; ++i) {
            engine.update(1.0 / 60.0);
            engine.render();
            engine.get_renderer().wait_for_completion();
        }
        cam.set_view_azimuth(0.0f);
        auto a2 = run_phase(engine, 120, [](int) {});
        print_phase("A2 static", a2);
        auto b2 = run_phase(engine, 120, [&](int i) {
            cam.set_view_azimuth(TWO_PI * (i + 1) / 120.0f);
        });
        print_phase("B2 orbit ", b2);
        std::cout << "  [measure] big-scene orbit/static x"
                  << (b2.avg_ms / a2.avg_ms) << std::endl;
    }

    // Ratchet: the orbit must not leave the renderer slower after it
    // ends. Median spread on idle is +/-1.2%, so 1.25 leaves wide
    // headroom for a loaded machine while still catching a real
    // systematic regression; a stuck cache/invalidation bug shows as
    // multiples.
    if (residue < 1.25) {
        tests_passed++;
    } else {
        tests_failed++;
        std::cout << "FAIL: post-orbit frames stay " << residue
                  << "x slower than pre-orbit — something did not "
                  << "recover when the orbit ended" << std::endl;
    }
    // Sanity: the study measured real work.
    if (a.avg_ms > 0.05) {
        tests_passed++;
    } else {
        tests_failed++;
        std::cout << "FAIL: static frames measure " << a.avg_ms
                  << " ms — the engine did no per-frame work; the "
                  << "study is not measuring rendering" << std::endl;
    }

    engine.shutdown();
    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
