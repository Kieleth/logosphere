// ============================================================================
// SHADOW QUALITY TESTS
// ============================================================================
// Two focused regression tests:
//   1. test_shadow_frame_stability - GI temporal convergence (no pulsating)
//   2. test_shadow_penumbra_exists - Shadow edges have soft gradient
//
// Run:
//   ./logosphere-tests --test test_shadow_frame_stability
//   ./logosphere-tests --test test_shadow_penumbra_exists
//
// Scene: Case 0 from test_ssgi_visual (floor + obelisk + light, all at rest)
// ============================================================================

#include "../src/core/engine.h"
#include "../src/particle.h"
#include <iostream>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

// ============================================================================
// Shared scene setup (identical to test_ssgi_visual case 0)
// ============================================================================

static void setup_case0_scene(Engine& engine) {
    auto& ps = engine.get_particle_system();
    ps.clear_particles();

    // Floor: 50x50 at z=0
    Particle floor = {};
    floor.x = 0.0f; floor.y = 0.0f; floor.z = 0.0f;
    floor.shape = ParticleShape::BOX;
    floor.width = 50.0f; floor.height = 50.0f; floor.thickness = 0.15f;
    floor.size = 50.0f;
    floor.r = 0.8f; floor.g = 0.8f; floor.b = 0.8f; floor.a = 1.0f;
    floor.is_at_rest = true;
    engine.add_particle(floor);

    // Obelisk: at (0,0,1.5), size 0.5x0.5x3.0
    Particle obelisk = {};
    obelisk.x = 0.0f; obelisk.y = 0.0f; obelisk.z = 1.5f;
    obelisk.shape = ParticleShape::BOX;
    obelisk.width = 0.5f; obelisk.height = 0.5f; obelisk.thickness = 3.0f;
    obelisk.size = 3.0f;
    obelisk.r = 0.5f; obelisk.g = 0.5f; obelisk.b = 0.5f; obelisk.a = 1.0f;
    obelisk.is_at_rest = true;
    engine.add_particle(obelisk);

    // Light: at (3,-3,4), light_size=0.1, strength=500000
    Particle light = {};
    light.x = 3.0f; light.y = -3.0f; light.z = 4.0f;
    light.shape = ParticleShape::BOX;
    light.width = 0.1f; light.height = 0.1f; light.thickness = 0.15f;
    light.size = 0.1f;
    light.r = 1.0f; light.g = 1.0f; light.b = 1.0f; light.a = 1.0f;
    light.is_at_rest = true;
    light.is_light_source = true;
    light.emission_strength = 500000.0f;
    light.emission_radius = 30.0f;
    engine.add_particle(light);
}

// ============================================================================
// Pixel helpers
// ============================================================================

struct SPixel { uint8_t r, g, b, a; };

static SPixel read_pixel(const uint8_t* data, int w, int x, int y) {
    // BGRA format
    int idx = (y * w + x) * 4;
    SPixel p;
    p.b = data[idx + 0];
    p.g = data[idx + 1];
    p.r = data[idx + 2];
    p.a = data[idx + 3];
    return p;
}

static float brightness(SPixel p) {
    return (p.r + p.g + p.b) / 3.0f;
}

static bool is_background(SPixel p) {
    return p.r <= 12 && p.g <= 12 && p.b <= 17;
}

// PPM dump for debugging
static void dump_ppm(const uint8_t* data, int w, int h, const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        // BGRA -> RGB
        fputc(data[i * 4 + 2], f);  // R
        fputc(data[i * 4 + 1], f);  // G
        fputc(data[i * 4 + 0], f);  // B
    }
    fclose(f);
}

// ============================================================================
// TEST 1: Frame stability (GI convergence)
// ============================================================================
// After enough frames, the framebuffer must be perfectly stable.
// If GI temporal accumulation never converges (host wraps frame_index),
// pixels at shadow edges keep changing every frame.
// ============================================================================

bool test_shadow_frame_stability() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "  TEST: Shadow Frame Stability (GI Convergence)\n";
    std::cout << "============================================================\n\n";

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_width = 1600;
    cfg.window_height = 1200;
    cfg.window_title = "Shadow Stability";
    cfg.enable_chat_window = false;

    if (engine.initialize(cfg) != 0) {
        std::cerr << "ERROR: Engine init failed\n";
        return false;
    }

    setup_case0_scene(engine);

    auto& rs = engine;
    int w = rs.get_resolution_manager().get_render_width();
    int h = rs.get_resolution_manager().get_render_height();
    int total_pixels = w * h;
    std::cout << "  Render: " << w << "x" << h << "\n";

    const float dt = 1.0f / 60.0f;

    // Phase 1: Run 100 frames to get past GI convergence window (64 frames)
    const int WARMUP_FRAMES = 100;
    std::cout << "  Running " << WARMUP_FRAMES << " warmup frames...\n";
    for (int i = 0; i < WARMUP_FRAMES; i++) {
        engine.update(dt);
        engine.render();
    }

    // Capture frame A
    rs.get_render_pipeline().wait_for_gpu_completion();
    const uint8_t* data_a = rs.get_render_buffer().get_native_data();
    if (!data_a) {
        std::cerr << "  ERROR: No render buffer data after warmup\n";
        return false;
    }
    std::vector<uint32_t> frame_a(total_pixels);
    std::memcpy(frame_a.data(), data_a, total_pixels * sizeof(uint32_t));
    dump_ppm(data_a, w, h, "/tmp/stability_frame_a.ppm");

    // Run 3 more frames (one full triple-buffer cycle)
    for (int i = 0; i < 3; i++) {
        engine.update(dt);
        engine.render();
    }

    // Capture frame B
    rs.get_render_pipeline().wait_for_gpu_completion();
    const uint8_t* data_b = rs.get_render_buffer().get_native_data();
    if (!data_b) {
        std::cerr << "  ERROR: No render buffer data after frame B\n";
        return false;
    }
    std::vector<uint32_t> frame_b(total_pixels);
    std::memcpy(frame_b.data(), data_b, total_pixels * sizeof(uint32_t));
    dump_ppm(data_b, w, h, "/tmp/stability_frame_b.ppm");

    // Compare frame A and frame B
    int changed_pixels = 0;
    int max_delta = 0;
    int max_x = 0, max_y = 0;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int idx = y * w + x;
            if (frame_a[idx] != frame_b[idx]) {
                changed_pixels++;
                // Calculate per-channel delta
                uint8_t* a = (uint8_t*)&frame_a[idx];
                uint8_t* b = (uint8_t*)&frame_b[idx];
                int delta = std::abs(a[0] - b[0]) + std::abs(a[1] - b[1]) +
                            std::abs(a[2] - b[2]);
                if (delta > max_delta) {
                    max_delta = delta;
                    max_x = x;
                    max_y = y;
                }
            }
        }
    }

    std::cout << "  Frames " << WARMUP_FRAMES << " vs " << (WARMUP_FRAMES + 3)
              << ": " << changed_pixels << " pixels changed\n";
    if (changed_pixels > 0) {
        std::cout << "  Max delta: " << max_delta << " at (" << max_x << "," << max_y << ")\n";
    }

    // ASSERTION: After 100 frames of convergence, framebuffer must be stable
    bool stable = (changed_pixels == 0);
    std::cout << "\n  " << (stable ? "PASS" : "FAIL")
              << ": Frame stability after convergence (changed=" << changed_pixels << ")\n";

    if (!stable) {
        std::cout << "  [CAUSE] GI temporal accumulation has not converged.\n";
        std::cout << "  [CAUSE] The Halton sequence and/or EMA blend are still changing.\n";
    }

    return stable;
}

// ============================================================================
// TEST 2: Penumbra exists at shadow edges
// ============================================================================
// Shadow edges should have a gradual brightness transition (penumbra),
// not a hard 1-2 pixel step. We scan horizontal lines through the shadow
// region and measure the width of brightness transitions.
// ============================================================================

bool test_shadow_penumbra_exists() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "  TEST: Shadow Penumbra Exists\n";
    std::cout << "============================================================\n\n";

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_width = 1600;
    cfg.window_height = 1200;
    cfg.window_title = "Penumbra Test";
    cfg.enable_chat_window = false;

    if (engine.initialize(cfg) != 0) {
        std::cerr << "ERROR: Engine init failed\n";
        return false;
    }

    setup_case0_scene(engine);

    auto& rs = engine;
    int w = rs.get_resolution_manager().get_render_width();
    int h = rs.get_resolution_manager().get_render_height();
    std::cout << "  Render: " << w << "x" << h << "\n";

    const float dt = 1.0f / 60.0f;

    // Run 30 frames for rendering to stabilize
    const int FRAMES = 30;
    std::cout << "  Running " << FRAMES << " frames...\n";
    for (int i = 0; i < FRAMES; i++) {
        engine.update(dt);
        engine.render();
    }

    rs.get_render_pipeline().wait_for_gpu_completion();
    const uint8_t* data = rs.get_render_buffer().get_native_data();
    if (!data) {
        std::cerr << "  ERROR: No render buffer data\n";
        return false;
    }

    dump_ppm(data, w, h, "/tmp/penumbra_test.ppm");

    // Find rendered bounding box
    int bb_x0 = w, bb_y0 = h, bb_x1 = 0, bb_y1 = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            auto px = read_pixel(data, w, x, y);
            if (!is_background(px)) {
                bb_x0 = std::min(bb_x0, x);
                bb_y0 = std::min(bb_y0, y);
                bb_x1 = std::max(bb_x1, x);
                bb_y1 = std::max(bb_y1, y);
            }
        }
    }

    if (bb_x1 <= bb_x0 || bb_y1 <= bb_y0) {
        std::cerr << "  ERROR: No rendered geometry found\n";
        return false;
    }

    std::cout << "  Rendered bbox: (" << bb_x0 << "," << bb_y0
              << ")-(" << bb_x1 << "," << bb_y1 << ")\n";

    // Strategy: find the shadow edge by looking for the steepest per-pixel
    // brightness gradient, then measure the transition zone width around it.
    //
    // The SHADOW EDGE has a sharp brightness drop (lit floor -> dark shadow).
    // With a HARD shadow, this drop happens in 1-2 pixels (gradient spike).
    // With a PENUMBRA, the drop is spread over 5+ pixels (wide gradient).
    //
    // We measure the "penumbra width" as the number of consecutive pixels
    // around the steepest gradient where |derivative| > GRAD_THRESH.

    const float GRAD_THRESH = 3.0f;  // Min per-pixel change to be "in transition"
    const float MIN_EDGE_GRADIENT = 15.0f;  // Min gradient to qualify as shadow edge

    int total_edges = 0;
    int hard_edges = 0;  // transition width <= 3px
    int soft_edges = 0;  // transition width >= 5px
    int max_penumbra_width = 0;
    int max_pw_y = 0;
    float avg_penumbra_width = 0.0f;

    struct EdgeInfo {
        int y, x, width;
        float gradient;
    };
    std::vector<EdgeInfo> edges;

    // Sample every 2nd line in the middle 60% of the bbox
    int scan_y0 = bb_y0 + (bb_y1 - bb_y0) * 2 / 10;
    int scan_y1 = bb_y0 + (bb_y1 - bb_y0) * 8 / 10;

    for (int y = scan_y0; y <= scan_y1; y += 2) {
        // Build brightness profile for this line (geometry only)
        std::vector<float> profile;
        std::vector<int> profile_x;
        for (int x = bb_x0; x <= bb_x1; x++) {
            auto px = read_pixel(data, w, x, y);
            if (!is_background(px)) {
                profile.push_back(brightness(px));
                profile_x.push_back(x);
            }
        }

        if (profile.size() < 10) continue;

        // Find the steepest negative gradient (brightness DROP = shadow edge)
        float max_drop = 0.0f;
        int max_drop_idx = -1;
        for (int i = 0; i < (int)profile.size() - 1; i++) {
            float drop = profile[i] - profile[i + 1];
            if (drop > max_drop) {
                max_drop = drop;
                max_drop_idx = i;
            }
        }

        if (max_drop < MIN_EDGE_GRADIENT || max_drop_idx < 0) continue;

        // Found a shadow edge. Measure the transition zone:
        // Expand left while gradient > GRAD_THRESH
        int left = max_drop_idx;
        while (left > 0) {
            float grad = std::abs(profile[left] - profile[left - 1]);
            if (grad < GRAD_THRESH) break;
            left--;
        }

        // Expand right while gradient > GRAD_THRESH
        int right = max_drop_idx + 1;
        while (right < (int)profile.size() - 1) {
            float grad = std::abs(profile[right + 1] - profile[right]);
            if (grad < GRAD_THRESH) break;
            right++;
        }

        int transition_width = profile_x[right] - profile_x[left];
        total_edges++;

        if (transition_width <= 3) hard_edges++;
        if (transition_width >= 5) soft_edges++;

        avg_penumbra_width += transition_width;

        if (transition_width > max_penumbra_width) {
            max_penumbra_width = transition_width;
            max_pw_y = y;
        }

        edges.push_back({y, profile_x[max_drop_idx], transition_width, max_drop});
    }

    if (total_edges > 0) {
        avg_penumbra_width /= total_edges;
    }

    std::cout << "  Shadow edges found: " << total_edges << "\n";
    std::cout << "  Hard edges (<=3px): " << hard_edges << "\n";
    std::cout << "  Soft edges (>=5px): " << soft_edges << "\n";
    std::cout << "  Max penumbra width: " << max_penumbra_width << " px (at y=" << max_pw_y << ")\n";
    std::cout << "  Avg penumbra width: " << avg_penumbra_width << " px\n";

    // Show top 5 edges
    std::sort(edges.begin(), edges.end(),
        [](const EdgeInfo& a, const EdgeInfo& b) { return a.width > b.width; });
    int show = std::min((int)edges.size(), 5);
    for (int i = 0; i < show; i++) {
        std::cout << "    #" << (i+1) << " y=" << edges[i].y
                  << " x=" << edges[i].x
                  << " width=" << edges[i].width
                  << " gradient=" << edges[i].gradient << "\n";
    }

    // ASSERTION: With a true penumbra, the total brightness drop at the
    // shadow edge is SPREAD over many pixels, so no single pixel-to-pixel
    // jump should exceed ~25. With a hard shadow, the full drop (50-100+
    // units) happens in 1-2 pixels → steepest gradient > 30.
    //
    // We check: what percentage of shadow edges have steepest gradient > 25?
    // Hard shadow: nearly all (>50%). Penumbra: very few (<30%).

    const float HARD_EDGE_THRESH = 25.0f;
    int hard_gradient_edges = 0;
    for (auto& e : edges) {
        if (e.gradient > HARD_EDGE_THRESH) hard_gradient_edges++;
    }

    float hard_pct = total_edges > 0 ? 100.0f * hard_gradient_edges / total_edges : 100.0f;

    std::cout << "  Hard gradient edges (>" << HARD_EDGE_THRESH << "): "
              << hard_gradient_edges << "/" << total_edges
              << " (" << hard_pct << "%)\n";

    // PASS if fewer than 30% of edges have hard gradients
    bool has_penumbra = (hard_pct < 30.0f && total_edges > 5);

    std::cout << "\n  " << (has_penumbra ? "PASS" : "FAIL")
              << ": Penumbra exists (hard_gradient=" << hard_pct
              << "%, required <30%)\n";

    if (!has_penumbra) {
        std::cout << "  [CAUSE] Shadow edges have hard single-pixel gradients.\n";
        std::cout << "  [CAUSE] Check PENUMBRA_MODE in optimization_flags.h\n";
    }

    return has_penumbra;
}

// ============================================================================
// TEST 3: No colored artifacts in grayscale scene
// ============================================================================
// The test scene has only gray/white surfaces (floor=0.8, obelisk=0.5,
// light=white). After lighting, all non-background pixels should be
// approximately grayscale. Colored artifacts (red/green/blue patches)
// indicate corruption in GI, penumbra, or apply_lighting kernels.
//
// Detection: For each pixel, compute chrominance = max(|R-G|, |R-B|, |G-B|).
// Grayscale pixels have chrominance ~0. Colored artifacts have chrominance > 20.
// ============================================================================

bool test_shadow_no_color_artifacts() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "  TEST: No Colored Artifacts in Grayscale Scene\n";
    std::cout << "============================================================\n\n";

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_width = 1600;
    cfg.window_height = 1200;
    cfg.window_title = "Color Artifact Test";
    cfg.enable_chat_window = false;

    if (engine.initialize(cfg) != 0) {
        std::cerr << "ERROR: Engine init failed\n";
        return false;
    }

    setup_case0_scene(engine);

    auto& rs = engine;
    int w = rs.get_resolution_manager().get_render_width();
    int h = rs.get_resolution_manager().get_render_height();
    std::cout << "  Render: " << w << "x" << h << "\n";

    const float dt = 1.0f / 60.0f;

    // Run 100 frames to let GI converge
    const int FRAMES = 100;
    std::cout << "  Running " << FRAMES << " frames...\n";
    for (int i = 0; i < FRAMES; i++) {
        engine.update(dt);
        engine.render();
    }

    rs.get_render_pipeline().wait_for_gpu_completion();
    const uint8_t* data = rs.get_render_buffer().get_native_data();
    if (!data) {
        std::cerr << "  ERROR: No render buffer data\n";
        return false;
    }

    dump_ppm(data, w, h, "/tmp/color_artifact_test.ppm");

    // Count pixels with high chrominance (colored artifacts)
    const int CHROMA_THRESH = 20;  // Max allowed |R-G|, |R-B|, |G-B| for "grayscale"
    int total_rendered = 0;
    int colored_pixels = 0;
    int max_chroma = 0;
    int max_chroma_x = 0, max_chroma_y = 0;
    uint8_t max_r = 0, max_g = 0, max_b = 0;

    // Histogram of chrominance values for non-background pixels
    int chroma_hist[256] = {};

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            auto px = read_pixel(data, w, x, y);
            if (is_background(px)) continue;

            total_rendered++;

            int rg = std::abs((int)px.r - (int)px.g);
            int rb = std::abs((int)px.r - (int)px.b);
            int gb = std::abs((int)px.g - (int)px.b);
            int chroma = std::max({rg, rb, gb});

            if (chroma < 256) chroma_hist[chroma]++;

            if (chroma > CHROMA_THRESH) {
                colored_pixels++;
            }

            if (chroma > max_chroma) {
                max_chroma = chroma;
                max_chroma_x = x;
                max_chroma_y = y;
                max_r = px.r;
                max_g = px.g;
                max_b = px.b;
            }
        }
    }

    float colored_pct = total_rendered > 0 ? 100.0f * colored_pixels / total_rendered : 0.0f;

    std::cout << "  Total rendered pixels: " << total_rendered << "\n";
    std::cout << "  Colored pixels (chroma>" << CHROMA_THRESH << "): " << colored_pixels
              << " (" << colored_pct << "%)\n";
    std::cout << "  Max chrominance: " << max_chroma
              << " at (" << max_chroma_x << "," << max_chroma_y << ")"
              << " RGB=(" << (int)max_r << "," << (int)max_g << "," << (int)max_b << ")\n";

    // Show chrominance distribution
    std::cout << "  Chrominance distribution:\n";
    std::cout << "    [0-2]:   " << (chroma_hist[0] + chroma_hist[1] + chroma_hist[2]) << "\n";
    std::cout << "    [3-5]:   " << (chroma_hist[3] + chroma_hist[4] + chroma_hist[5]) << "\n";
    std::cout << "    [6-10]:  ";
    int sum = 0; for (int i = 6; i <= 10; i++) sum += chroma_hist[i];
    std::cout << sum << "\n";
    std::cout << "    [11-20]: ";
    sum = 0; for (int i = 11; i <= 20; i++) sum += chroma_hist[i];
    std::cout << sum << "\n";
    std::cout << "    [21-50]: ";
    sum = 0; for (int i = 21; i <= 50; i++) sum += chroma_hist[i];
    std::cout << sum << "\n";
    std::cout << "    [51+]:   ";
    sum = 0; for (int i = 51; i < 256; i++) sum += chroma_hist[i];
    std::cout << sum << "\n";

    // ASSERTION: Less than 0.1% of rendered pixels should be colored
    bool clean = (colored_pct < 0.1f);

    std::cout << "\n  " << (clean ? "PASS" : "FAIL")
              << ": No color artifacts (colored=" << colored_pct
              << "%, required <0.1%)\n";

    if (!clean) {
        std::cout << "  [CAUSE] Colored patches in a grayscale scene indicate:\n";
        std::cout << "  [CAUSE]   - GI reading incorrect triangle base_color from BVH\n";
        std::cout << "  [CAUSE]   - Penumbra kernel corrupting shadow buffer values\n";
        std::cout << "  [CAUSE]   - Apply_lighting procedural pattern producing unexpected colors\n";
    }

    return clean;
}
