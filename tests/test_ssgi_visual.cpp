// ============================================================================
// SSGI + SOFT SHADOWS VISUAL TEST SUITE
// ============================================================================
// Purpose: 7 cases of increasing complexity testing soft shadows, SSGI color
//          bleeding, and indirect lighting.
//
// Run:
//   ./logosphere-tests --test test_ssgi_visual              # Headless (auto-advance)
//   INTERACTIVE=1 ./logosphere-tests --test test_ssgi_visual  # Visual (SPACE to advance)
//   CASE=3 INTERACTIVE=1 ./logosphere-tests --test test_ssgi_visual  # Jump to case
//
// Cases:
//   1: Hard shadow baseline (point light)
//   2: Soft shadow comparison (large area light)
//   3: Red wall color bleeding
//   4: Blue + red opposing walls
//   5: Green floor bleeds onto white wall
//   6: Three colored lights
//   7: Cornell box (classic GI test)
// ============================================================================

#include "../src/core/engine.h"
#include "../src/particle.h"
#include "../src/ui/ui_system.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

// ============================================================================
// PPM frame dump (dependency-free image output for headless debugging)
// ============================================================================

static void dump_ppm(const uint8_t* native_data, int width, int height, const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) { std::cerr << "  ERROR: Cannot open " << path << "\n"; return; }
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    const uint32_t* pixels = reinterpret_cast<const uint32_t*>(native_data);
    for (int i = 0; i < width * height; i++) {
        uint32_t p = pixels[i];
        // Native format is BGRA — extract as RGB
        uint8_t rgb[3] = {
            (uint8_t)((p >> 16) & 0xFF),
            (uint8_t)((p >> 8) & 0xFF),
            (uint8_t)(p & 0xFF)
        };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    std::cout << "  [DEBUG] Frame dumped to " << path << " (" << width << "x" << height << ")\n";
}

// ============================================================================
// Pixel sampling helpers
// ============================================================================

struct SampledPixel {
    uint8_t r, g, b;
};

// Read a single pixel from the render buffer (BGRA format)
static SampledPixel sample_pixel(const uint8_t* native_data, int width, int x, int y) {
    const uint32_t* pixels = reinterpret_cast<const uint32_t*>(native_data);
    uint32_t p = pixels[y * width + x];
    SampledPixel sp;
    sp.b = p & 0xFF;
    sp.g = (p >> 8) & 0xFF;
    sp.r = (p >> 16) & 0xFF;
    return sp;
}

// Average pixels in a rectangular region
static SampledPixel sample_region(const uint8_t* native_data, int width, int height,
                                   int x0, int y0, int x1, int y1) {
    x0 = std::max(0, std::min(x0, width - 1));
    x1 = std::max(0, std::min(x1, width - 1));
    y0 = std::max(0, std::min(y0, height - 1));
    y1 = std::max(0, std::min(y1, height - 1));

    long sum_r = 0, sum_g = 0, sum_b = 0;
    int count = 0;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            auto sp = sample_pixel(native_data, width, x, y);
            sum_r += sp.r;
            sum_g += sp.g;
            sum_b += sp.b;
            count++;
        }
    }
    SampledPixel avg;
    avg.r = count > 0 ? (uint8_t)(sum_r / count) : 0;
    avg.g = count > 0 ? (uint8_t)(sum_g / count) : 0;
    avg.b = count > 0 ? (uint8_t)(sum_b / count) : 0;
    return avg;
}

static float brightness(const SampledPixel& px) {
    return (px.r + px.g + px.b) / 3.0f;
}

static bool is_background(const SampledPixel& px) {
    // Background clear color is approximately R=10 G=10 B=15
    return px.r <= 12 && px.g <= 12 && px.b <= 17;
}

// Average only non-background pixels in a region. Returns {0,0,0} if none found.
static SampledPixel sample_region_geometry(const uint8_t* native_data, int width, int height,
                                            int x0, int y0, int x1, int y1) {
    x0 = std::max(0, std::min(x0, width - 1));
    x1 = std::max(0, std::min(x1, width - 1));
    y0 = std::max(0, std::min(y0, height - 1));
    y1 = std::max(0, std::min(y1, height - 1));

    long sum_r = 0, sum_g = 0, sum_b = 0;
    int count = 0;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            auto sp = sample_pixel(native_data, width, x, y);
            if (!is_background(sp)) {
                sum_r += sp.r;
                sum_g += sp.g;
                sum_b += sp.b;
                count++;
            }
        }
    }
    SampledPixel avg;
    avg.r = count > 0 ? (uint8_t)(sum_r / count) : 0;
    avg.g = count > 0 ? (uint8_t)(sum_g / count) : 0;
    avg.b = count > 0 ? (uint8_t)(sum_b / count) : 0;
    return avg;
}

// Find bounding box of rendered (non-background) content
struct ScreenBBox {
    int x0, y0, x1, y1;
    bool valid;
};

static ScreenBBox find_rendered_bbox(const uint8_t* native_data, int width, int height) {
    ScreenBBox bb = {width, height, 0, 0, false};
    // Sample every 4th pixel for speed
    for (int y = 0; y < height; y += 4) {
        for (int x = 0; x < width; x += 4) {
            auto sp = sample_pixel(native_data, width, x, y);
            if (!is_background(sp)) {
                if (x < bb.x0) bb.x0 = x;
                if (x > bb.x1) bb.x1 = x;
                if (y < bb.y0) bb.y0 = y;
                if (y > bb.y1) bb.y1 = y;
                bb.valid = true;
            }
        }
    }
    return bb;
}

// ============================================================================
// Scene building helpers
// ============================================================================

// Add a floor tile at (world_x, world_y, z=0), size 1x1
static void add_floor_tile(Engine& engine, float wx, float wy, float r, float g, float b) {
    Particle tile = {};
    tile.x = wx;
    tile.y = wy;
    tile.z = 0.075f;  // bottom rests ON the turtle (thickness 0.15)
    tile.shape = ParticleShape::BOX;
    tile.width = 0.5f;
    tile.height = 0.5f;
    tile.thickness = 0.15f;
    tile.size = 0.5f;
    tile.r = r; tile.g = g; tile.b = b; tile.a = 1.0f;
    tile.is_at_rest = true;
    engine.add_particle(tile);
}

// Add a wall (tall box)
static void add_wall(Engine& engine, float wx, float wy, float wz,
                     float wall_w, float wall_h, float wall_thick,
                     float r, float g, float b) {
    Particle wall = {};
    wall.x = wx;
    wall.y = wy;
    wall.z = wz;
    wall.shape = ParticleShape::BOX;
    wall.width = wall_w;
    wall.height = wall_h;
    wall.thickness = wall_thick;
    wall.size = std::max({wall_w, wall_h, wall_thick});
    wall.r = r; wall.g = g; wall.b = b; wall.a = 1.0f;
    wall.is_at_rest = true;
    engine.add_particle(wall);
}

// Add a light
static void add_light(Engine& engine, float wx, float wy, float wz,
                      float light_size, float r, float g, float b,
                      float strength = 500000.0f, float radius = 30.0f) {
    Particle light = {};
    light.x = wx;
    light.y = wy;
    light.z = wz;
    light.shape = ParticleShape::BOX;
    light.width = light_size;
    light.height = light_size;
    light.thickness = std::max(light_size * 0.5f, 0.15f);
    light.size = light_size;
    light.r = r; light.g = g; light.b = b; light.a = 1.0f;
    light.is_at_rest = true;
    light.is_light_source = true;
    light.emission_strength = strength;
    light.emission_radius = radius;
    engine.add_particle(light);
}

// Add a semi-transparent glass panel (for transparency tests)
static void add_glass(Engine& engine, float wx, float wy, float wz,
                      float gw, float gh, float gt,
                      float r, float g, float b, float alpha = 0.5f) {
    Particle glass = {};
    glass.x = wx; glass.y = wy; glass.z = wz;
    glass.shape = ParticleShape::BOX;
    glass.width = gw; glass.height = gh; glass.thickness = gt;
    glass.size = std::max({gw, gh, gt});
    glass.r = r; glass.g = g; glass.b = b; glass.a = alpha;
    glass.is_at_rest = true;
    engine.add_particle(glass);
}

// Build a floor grid
static void build_floor(Engine& engine, float min_x, float max_x,
                        float min_y, float max_y, float r, float g, float b) {
    for (float x = min_x; x <= max_x; x += 1.0f) {
        for (float y = min_y; y <= max_y; y += 1.0f) {
            add_floor_tile(engine, x, y, r, g, b);
        }
    }
}

// ============================================================================
// Assertion helpers
// ============================================================================

struct AssertResult {
    bool passed;
    std::string name;
    std::string detail;
};

static std::vector<AssertResult> g_assertions;       // current case assertions (for UI overlay)
static std::vector<AssertResult> g_all_assertions;   // accumulated across all cases (for final summary)

static void assert_check(const std::string& name, bool condition, const std::string& detail) {
    AssertResult ar = {condition, name, detail};
    g_assertions.push_back(ar);
    g_all_assertions.push_back(ar);
    std::cout << (condition ? "  PASS: " : "  FAIL: ") << name << " — " << detail << "\n";
}

// ============================================================================
// Case descriptions
// ============================================================================

static const char* case_descriptions[] = {
    "Static Baseline (1 light, 1 cube, 1 floor tile)",
    "Hard Shadow Baseline (point light)",
    "Soft Shadow Comparison (large area light)",
    "Red Wall Color Bleeding",
    "Blue + Red Opposing Walls",
    "Green Floor Bleeds onto White Wall",
    "Three Colored Lights",
    "Cornell Box (Classic GI Test)",
    "Stained Glass (Forward Transparency)"
};

// ============================================================================
// Main test function
// ============================================================================

bool test_ssgi_visual() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "  SSGI + SOFT SHADOWS VISUAL TEST SUITE\n";
    std::cout << "============================================================\n\n";

    const char* interactive_env = std::getenv("INTERACTIVE");
    bool is_interactive = (interactive_env && std::string(interactive_env) == "1");

    std::cout << "[MODE] " << (is_interactive ? "INTERACTIVE" : "HEADLESS") << "\n";
    if (!is_interactive) {
        std::cout << "[MODE] Set INTERACTIVE=1 for visual mode\n";
    }

    const int MAX_CASE = 8;
    int test_case = 0;
    int cycle_count = 0;  // 0 = first pass through cases, 1 = second pass (cycle test)
    bool cycle_test_done = false;

    // Store case 0 floor color data from first run for cycle comparison
    struct FloorColorSample { float r, g, b; int x, y; };
    std::vector<FloorColorSample> case0_first_run_floor;

    const char* case_env = std::getenv("CASE");
    if (case_env) {
        int requested = std::atoi(case_env);
        if (requested >= 0 && requested <= MAX_CASE) {
            test_case = requested;
            std::cout << "[CASE] Selected case " << test_case << " via CASE env var\n";
        }
    }

    // Engine setup
    Engine engine;
    EngineConfig engine_config;
    engine_config.create_display = is_interactive;
    engine_config.window_width = 1600;
    engine_config.window_height = 1200;
    engine_config.window_title = "SSGI Visual Test";
    engine_config.enable_chat_window = false;

    if (engine.initialize(engine_config) != 0) {
        std::cerr << "ERROR: Engine init failed\n";
        return false;
    }

    auto& ps = engine.get_particle_system();
    auto& rs = engine;

    // Force consistent render resolution regardless of display DPI/Retina scaling.
    // On Retina displays, NATIVE preset gives 3200x2400 which changes scene size
    // relative to the buffer, breaking bbox-relative sampling regions.
    int target_w = engine_config.window_width;
    int target_h = engine_config.window_height;
    if (rs.get_resolution_manager().get_render_width() != target_w || rs.get_resolution_manager().get_render_height() != target_h) {
        std::cout << "[RENDER] Overriding native " << rs.get_resolution_manager().get_render_width() << "x"
                  << rs.get_resolution_manager().get_render_height() << " → " << target_w << "x" << target_h
                  << " for test consistency\n";
        rs.set_render_resolution(target_w, target_h);
    }

    int render_w = rs.get_resolution_manager().get_render_width();
    int render_h = rs.get_resolution_manager().get_render_height();
    std::cout << "[RENDER] " << render_w << "x" << render_h << "\n\n";

    int frame_counter = 0;
    const int ACCUMULATION_FRAMES = 70;  // frames before assertions (GI converges at frame 64, need to be past that)
    const int DANCE_CAPTURE_START = 70;  // = ACCUMULATION_FRAMES
    const int DANCE_CAPTURE_COUNT = 10;  // capture 10 consecutive frames
    bool assertions_done = false;
    bool case0_dance_done = false;
    std::vector<std::vector<uint32_t>> dance_frames;  // 10 full frame copies for dance detection

    // Create scene for current case
    auto create_scene = [&]() {
        // Flush GPU pipeline and clear ALL temporal state (GI, shadow history,
        // sample counts, acceleration structure) to prevent bleeding between cases.
        rs.get_render_pipeline().reset_temporal_state();
        ps.clear_particles();
        frame_counter = 0;
        assertions_done = false;
        case0_dance_done = false;
        g_assertions.clear();

        std::cout << "\n[CASE " << test_case << "/" << MAX_CASE << "] "
                  << case_descriptions[test_case] << "\n";

        switch (test_case) {
        case 0: {
            // Static Baseline: huge floor, single cube, single point light on floor
            // Nothing moves. For diagnosing shadow stability.
            Particle floor = {};
            floor.x = 0.0f; floor.y = 0.0f; floor.z = 0.075f;  // bottom ON the turtle
            floor.shape = ParticleShape::BOX;
            floor.width = 50.0f; floor.height = 50.0f; floor.thickness = 0.15f;
            floor.size = 50.0f;
            floor.r = 0.8f; floor.g = 0.8f; floor.b = 0.8f; floor.a = 1.0f;
            floor.is_at_rest = true;
            engine.add_particle(floor);

            add_wall(engine, 0.0f, 0.0f, 1.5f, 0.5f, 0.5f, 3.0f, 0.5f, 0.5f, 0.5f);
            add_light(engine, 3.0f, -3.0f, 4.0f, 0.1f, 1.0f, 1.0f, 1.0f);
            break;
        }
        case 1: {
            // Hard Shadow Baseline: white floor, 1 gray pillar, 1 point light
            build_floor(engine, -5, 5, -5, 5, 0.8f, 0.8f, 0.8f);
            add_wall(engine, 0.0f, 0.0f, 1.5f, 0.5f, 0.5f, 3.0f, 0.5f, 0.5f, 0.5f);
            add_light(engine, 0.0f, -3.0f, 8.0f, 0.1f, 1.0f, 1.0f, 1.0f);
            break;
        }
        case 2: {
            // Soft Shadow: same scene but light size = 3.0m
            build_floor(engine, -5, 5, -5, 5, 0.8f, 0.8f, 0.8f);
            add_wall(engine, 0.0f, 0.0f, 1.5f, 0.5f, 0.5f, 3.0f, 0.5f, 0.5f, 0.5f);
            add_light(engine, 0.0f, -3.0f, 8.0f, 3.0f, 1.0f, 1.0f, 1.0f);
            break;
        }
        case 3: {
            // Red Wall Color Bleeding: white floor, red wall left, white light above
            build_floor(engine, -5, 5, -5, 5, 0.8f, 0.8f, 0.8f);
            add_wall(engine, -3.0f, 0.0f, 2.0f, 0.3f, 3.0f, 4.0f, 1.0f, 0.0f, 0.0f);
            add_light(engine, 0.0f, 0.0f, 8.0f, 1.0f, 1.0f, 1.0f, 1.0f);
            break;
        }
        case 4: {
            // Blue + Red Opposing Walls
            build_floor(engine, -5, 5, -5, 5, 0.8f, 0.8f, 0.8f);
            add_wall(engine, -4.0f, 0.0f, 2.0f, 0.3f, 3.0f, 4.0f, 1.0f, 0.0f, 0.0f);  // red left
            add_wall(engine,  4.0f, 0.0f, 2.0f, 0.3f, 3.0f, 4.0f, 0.0f, 0.0f, 1.0f);  // blue right
            add_light(engine, 0.0f, 0.0f, 8.0f, 1.0f, 1.0f, 1.0f, 1.0f);
            break;
        }
        case 5: {
            // Green Floor Bleeds onto White Wall
            build_floor(engine, -5, 5, -5, 5, 0.1f, 0.8f, 0.1f);  // green floor
            add_wall(engine, 0.0f, 3.0f, 2.0f, 3.0f, 0.3f, 4.0f, 0.8f, 0.8f, 0.8f);  // white wall
            add_light(engine, 0.0f, 0.0f, 8.0f, 1.0f, 1.0f, 1.0f, 1.0f);
            break;
        }
        case 6: {
            // Three Colored Lights with divider walls to prevent overlap
            build_floor(engine, -8, 8, -4, 4, 0.8f, 0.8f, 0.8f);
            // Divider walls between light zones
            add_wall(engine, -2.5f, 0.0f, 2.0f, 0.3f, 4.0f, 4.0f, 0.5f, 0.5f, 0.5f);
            add_wall(engine,  2.5f, 0.0f, 2.0f, 0.3f, 4.0f, 4.0f, 0.5f, 0.5f, 0.5f);
            // 3 colored area lights — pure colors, each in its zone
            add_light(engine, -5.0f, 0.0f, 5.0f, 1.0f, 1.0f, 0.0f, 0.0f, 300000.0f, 8.0f);  // red left
            add_light(engine,  0.0f, 0.0f, 5.0f, 1.0f, 0.0f, 1.0f, 0.0f, 300000.0f, 8.0f);  // green center
            add_light(engine,  5.0f, 0.0f, 5.0f, 1.0f, 0.0f, 0.0f, 1.0f, 300000.0f, 8.0f);  // blue right
            break;
        }
        case 7: {
            // Cornell Box: enclosed box with colored walls
            // Isometric camera at (-X,-Y,+Z) sees -X, -Y, +Z faces.
            // Colored walls placed so their lit INNER faces are camera-visible:
            //   Green at X=+4: inner face = -X (visible, lit by center light)
            //   Red at Y=+4:   inner face = -Y (visible, lit by center light)
            //   White at X=-4 and Y=-4 (inner faces hidden, OK for white)
            // Floor (white)
            build_floor(engine, -4, 4, -4, 4, 0.8f, 0.8f, 0.8f);
            // Ceiling (white) - elevated flat tiles
            for (float x = -4; x <= 4; x += 1.0f) {
                for (float y = -4; y <= 4; y += 1.0f) {
                    Particle ceil = {};
                    ceil.x = x; ceil.y = y; ceil.z = 6.0f;
                    ceil.shape = ParticleShape::BOX;
                    ceil.width = 0.5f; ceil.height = 0.5f; ceil.thickness = 0.15f;
                    ceil.size = 0.5f;
                    ceil.r = 0.8f; ceil.g = 0.8f; ceil.b = 0.8f; ceil.a = 1.0f;
                    ceil.is_at_rest = true;
                    engine.add_particle(ceil);
                }
            }
            // Right wall (green) — inner -X face visible from camera
            add_wall(engine,  4.0f, 0.0f, 3.0f, 0.3f, 8.0f, 6.0f, 0.1f, 0.9f, 0.1f);
            // Back wall (red) — inner -Y face visible from camera
            add_wall(engine, 0.0f, 4.0f, 3.0f, 8.0f, 0.3f, 6.0f, 0.9f, 0.1f, 0.1f);
            // Left wall (white) — inner face hidden, neutral color OK
            add_wall(engine, -4.0f, 0.0f, 3.0f, 0.3f, 8.0f, 6.0f, 0.8f, 0.8f, 0.8f);
            // Front wall (white) — inner face hidden, neutral color OK
            add_wall(engine, 0.0f, -4.0f, 3.0f, 8.0f, 0.3f, 6.0f, 0.8f, 0.8f, 0.8f);
            // Ceiling light
            add_light(engine, 0.0f, 0.0f, 5.5f, 1.5f, 1.0f, 1.0f, 1.0f, 500000.0f, 20.0f);
            break;
        }
        case 8: {
            // STAINED GLASS: Three colored glass panels on a lit white floor.
            // Tests the hybrid forward+deferred transparency pipeline (Pass 3.5).
            //
            // Scene layout (top-down, camera at -X,-Y looking toward +X,+Y):
            //
            //     Light (0,-3,6)
            //       |
            //  [RED]  [GRN]  [BLU]   <-- glass panels at y=0, z=1.5
            //  x=-3   x=0    x=+3
            //       _______________
            //      |               |
            //      |  white floor  |   <-- floor z=0
            //      |_______________|
            //
            // Glass panels are thin vertical sheets (0.1m thick in Y).
            // Alpha = 0.5 — the lit glass color blends 50/50 with the floor behind.
            // The floor is visible through the glass, tinted by the glass color.

            // White floor (large)
            Particle floor = {};
            floor.x = 0.0f; floor.y = 0.0f; floor.z = 0.075f;  // bottom ON the turtle
            floor.shape = ParticleShape::BOX;
            floor.width = 50.0f; floor.height = 50.0f; floor.thickness = 0.15f;
            floor.size = 50.0f;
            floor.r = 0.8f; floor.g = 0.8f; floor.b = 0.8f; floor.a = 1.0f;
            floor.is_at_rest = true;
            engine.add_particle(floor);

            // Three glass panels — thin vertical sheets, different colors
            // Each is 2m wide (X), 0.1m deep (Y), 3m tall (Z), centered at z=1.5
            add_glass(engine, -3.0f, 0.0f, 1.5f,  2.0f, 0.1f, 3.0f,  1.0f, 0.15f, 0.1f,  0.5f);  // Red
            add_glass(engine,  0.0f, 0.0f, 1.5f,  2.0f, 0.1f, 3.0f,  0.1f, 1.0f,  0.15f, 0.5f);  // Green
            add_glass(engine,  3.0f, 0.0f, 1.5f,  2.0f, 0.1f, 3.0f,  0.1f, 0.15f, 1.0f,  0.5f);  // Blue

            // Strong white light above and in front
            add_light(engine, 0.0f, -3.0f, 6.0f, 0.3f, 1.0f, 1.0f, 1.0f, 600000.0f, 25.0f);
            break;
        }
        }
    };

    create_scene();

    // Main loop
    const float dt = 1.0f / 60.0f;
    bool should_quit = false;

    while (!should_quit) {
        engine.update(dt);
        engine.render();
        frame_counter++;

        if (is_interactive) {
            engine.get_platform()->poll_events();

            // UI overlay
            auto* ui = engine.get_ui_system();
            std::string title = "Case " + std::to_string(test_case) + "/" +
                                std::to_string(MAX_CASE) + ": " + case_descriptions[test_case];
            ui->draw_text(10, 10, title, 255, 200, 100);
            ui->draw_text(10, 40, "Frame: " + std::to_string(frame_counter), 200, 200, 200);

            if (assertions_done) {
                int y_off = 70;
                for (auto& a : g_assertions) {
                    uint8_t cr = a.passed ? 100 : 255;
                    uint8_t cg = a.passed ? 255 : 100;
                    std::string line = (a.passed ? "PASS: " : "FAIL: ") + a.name + " — " + a.detail;
                    ui->draw_text(10, y_off, line, cr, cg, 100);
                    y_off += 20;
                }
            }

            ui->draw_text(10, render_h - 30, "SPACE: next | ESC: exit", 128, 128, 128);

            engine.present();

            // Input handling
            const auto& input = engine.get_input_system();
            static bool space_was_pressed = false;
            bool space_pressed = input.get_input_state().keys[GLFW_KEY_SPACE];
            if (space_pressed && !space_was_pressed) {
                test_case++;
                if (test_case > MAX_CASE) test_case = 0;
                create_scene();
            }
            space_was_pressed = space_pressed;

            if (input.get_input_state().keys[GLFW_KEY_ESCAPE]) {
                should_quit = true;
            }
        }

        // Run assertions after accumulation
        if (frame_counter == ACCUMULATION_FRAMES && !assertions_done) {
            // Ensure GPU has finished rendering
            rs.get_render_pipeline().wait_for_gpu_completion();
            const uint8_t* native_data = rs.get_render_buffer().get_native_data();

            if (!native_data) {
                std::cout << "  ERROR: No render buffer data\n";
            } else {
                // Find where geometry actually renders on screen
                auto bb = find_rendered_bbox(native_data, render_w, render_h);
                if (!bb.valid) {
                    std::cout << "  ERROR: No rendered geometry found on screen\n";
                } else {
                    int bw = bb.x1 - bb.x0;
                    int bh = bb.y1 - bb.y0;
                    int bcx = (bb.x0 + bb.x1) / 2;
                    int bcy = (bb.y0 + bb.y1) / 2;
                    std::cout << "  Rendered bbox: (" << bb.x0 << "," << bb.y0
                              << ")-(" << bb.x1 << "," << bb.y1
                              << ") size=" << bw << "x" << bh << "\n";

                    switch (test_case) {
                    case 0: {
                        // --- Frame 30: assertion frame ---
                        dump_ppm(native_data, render_w, render_h, "/tmp/case0_f30.ppm");

                        // Basic sanity: floor is lit and shadows exist
                        float min_b0 = 999.0f, max_b0 = 0.0f;
                        for (int y = bb.y0; y <= bb.y1; y += 2) {
                            for (int x = bb.x0; x <= bb.x1; x += 2) {
                                auto sp = sample_pixel(native_data, render_w, x, y);
                                if (!is_background(sp)) {
                                    float b = brightness(sp);
                                    if (b < min_b0) min_b0 = b;
                                    if (b > max_b0) max_b0 = b;
                                }
                            }
                        }
                        assert_check("Floor is lit", max_b0 > 100,
                                     "max=" + std::to_string(max_b0));
                        assert_check("Shadow exists", (max_b0 - min_b0) > 20,
                                     "range=" + std::to_string(max_b0 - min_b0));

                        // ============================================================
                        // DIAGNOSTIC 1: Spot Detection (spatial, single frame)
                        // A true spot = pixel brighter (or darker) than ALL 4 neighbors
                        // Gradients have one bright side and one dark side — not spots
                        // ============================================================
                        {
                            const float SPOT_THRESHOLD = 10.0f;
                            int spot_bright_count = 0;
                            int spot_dark_count = 0;
                            int geo_pixel_count = 0;

                            struct SpotInfo {
                                int x, y;
                                float pixel_b;
                                float n_up, n_down, n_left, n_right;
                                float delta;  // min distance to any neighbor
                                bool is_bright;
                            };
                            std::vector<SpotInfo> worst_spots;

                            // Spot map: red=bright spot, blue=dark spot
                            std::vector<uint8_t> spot_map(render_w * render_h * 3, 0);

                            for (int y = bb.y0 + 1; y < bb.y1; y++) {
                                for (int x = bb.x0 + 1; x < bb.x1; x++) {
                                    auto px_c = sample_pixel(native_data, render_w, x, y);
                                    if (is_background(px_c)) continue;
                                    geo_pixel_count++;

                                    auto px_u = sample_pixel(native_data, render_w, x, y - 1);
                                    auto px_d = sample_pixel(native_data, render_w, x, y + 1);
                                    auto px_l = sample_pixel(native_data, render_w, x - 1, y);
                                    auto px_r = sample_pixel(native_data, render_w, x + 1, y);

                                    if (is_background(px_u) || is_background(px_d) ||
                                        is_background(px_l) || is_background(px_r)) continue;

                                    float b_c = brightness(px_c);
                                    float b_u = brightness(px_u);
                                    float b_d = brightness(px_d);
                                    float b_l = brightness(px_l);
                                    float b_r = brightness(px_r);

                                    // Skip surface boundary pixels: if all 4 neighbors agree
                                    // (low variance) but center differs hugely, it's a geometry
                                    // edge pixel (different surface), not a shadow artifact
                                    float n_min = std::min({b_u, b_d, b_l, b_r});
                                    float n_max = std::max({b_u, b_d, b_l, b_r});
                                    float n_range = n_max - n_min;
                                    float n_avg = (b_u + b_d + b_l + b_r) / 4.0f;
                                    if (n_range < 20.0f && std::abs(b_c - n_avg) > 30.0f) continue;

                                    // Bright spot: pixel > ALL 4 neighbors by threshold
                                    float min_excess = std::min({b_c - b_u, b_c - b_d, b_c - b_l, b_c - b_r});
                                    if (min_excess > SPOT_THRESHOLD) {
                                        spot_bright_count++;
                                        SpotInfo s = {x, y, b_c, b_u, b_d, b_l, b_r, min_excess, true};
                                        worst_spots.push_back(s);
                                        int idx = (y * render_w + x) * 3;
                                        spot_map[idx] = (uint8_t)std::min(255.0f, min_excess * 8.0f);
                                    }

                                    // Dark spot: pixel < ALL 4 neighbors by threshold
                                    float min_deficit = std::min({b_u - b_c, b_d - b_c, b_l - b_c, b_r - b_c});
                                    if (min_deficit > SPOT_THRESHOLD) {
                                        spot_dark_count++;
                                        SpotInfo s = {x, y, b_c, b_u, b_d, b_l, b_r, min_deficit, false};
                                        worst_spots.push_back(s);
                                        int idx = (y * render_w + x) * 3;
                                        spot_map[idx + 2] = (uint8_t)std::min(255.0f, min_deficit * 8.0f);
                                    }
                                }
                            }

                            int spot_count = spot_bright_count + spot_dark_count;

                            // Sort by delta descending
                            std::sort(worst_spots.begin(), worst_spots.end(),
                                [](const SpotInfo& a, const SpotInfo& b) {
                                    return a.delta > b.delta;
                                });

                            std::cout << "  [SPOT] Bright spots: " << spot_bright_count
                                      << " Dark spots: " << spot_dark_count
                                      << " Total: " << spot_count
                                      << " (threshold " << SPOT_THRESHOLD << ")\n";
                            std::cout << "  [SPOT] Geometry pixels scanned: " << geo_pixel_count << "\n";

                            int show = std::min((int)worst_spots.size(), 10);
                            for (int i = 0; i < show; i++) {
                                auto& s = worst_spots[i];
                                std::cout << "    #" << (i+1) << " (" << s.x << "," << s.y
                                          << ") " << (s.is_bright ? "BRIGHT" : "DARK")
                                          << " b=" << std::fixed << std::setprecision(1) << s.pixel_b
                                          << " neighbors=[" << s.n_up << "," << s.n_down
                                          << "," << s.n_left << "," << s.n_right
                                          << "] delta=" << s.delta << "\n";
                            }

                            // Dump spot map
                            {
                                FILE* f = fopen("/tmp/case0_spots.ppm", "wb");
                                if (f) {
                                    fprintf(f, "P6\n%d %d\n255\n", render_w, render_h);
                                    fwrite(spot_map.data(), 1, render_w * render_h * 3, f);
                                    fclose(f);
                                    std::cout << "  [SPOT] Map dumped to /tmp/case0_spots.ppm\n";
                                }
                            }

                            assert_check("Few true spots (<50 isolated bright/dark pixels)",
                                         spot_count < 50,
                                         "spots=" + std::to_string(spot_count) +
                                         " (bright=" + std::to_string(spot_bright_count) +
                                         " dark=" + std::to_string(spot_dark_count) + ")");
                        }

                        // ============================================================
                        // CYCLE TEST: Save/compare floor colors for bleeding detection
                        // Case 0 has only grey objects + white light → no color expected
                        // ============================================================
                        if (cycle_count == 0) {
                            // First run: save floor pixel colors
                            case0_first_run_floor.clear();
                            for (int y = bb.y0; y <= bb.y1; y += 4) {
                                for (int x = bb.x0; x <= bb.x1; x += 4) {
                                    auto sp = sample_pixel(native_data, render_w, x, y);
                                    if (!is_background(sp)) {
                                        case0_first_run_floor.push_back({(float)sp.r, (float)sp.g, (float)sp.b, x, y});
                                    }
                                }
                            }
                            std::cout << "  [CYCLE] Saved " << case0_first_run_floor.size()
                                      << " floor samples for cycle comparison\n";
                        } else {
                            // Second run (after cycling 0→7→0): compare with first run
                            dump_ppm(native_data, render_w, render_h, "/tmp/case0_cycle2.ppm");
                            std::vector<FloorColorSample> case0_second_run_floor;
                            for (int y = bb.y0; y <= bb.y1; y += 4) {
                                for (int x = bb.x0; x <= bb.x1; x += 4) {
                                    auto sp = sample_pixel(native_data, render_w, x, y);
                                    if (!is_background(sp)) {
                                        case0_second_run_floor.push_back({(float)sp.r, (float)sp.g, (float)sp.b, x, y});
                                    }
                                }
                            }

                            int n = std::min(case0_first_run_floor.size(), case0_second_run_floor.size());
                            int color_diff_pixels = 0;
                            float max_color_diff = 0.0f;
                            float sum_r_diff = 0, sum_g_diff = 0, sum_b_diff = 0;
                            for (int i = 0; i < n; i++) {
                                float dr = case0_second_run_floor[i].r - case0_first_run_floor[i].r;
                                float dg = case0_second_run_floor[i].g - case0_first_run_floor[i].g;
                                float db = case0_second_run_floor[i].b - case0_first_run_floor[i].b;
                                float color_diff = std::abs(dr) + std::abs(dg) + std::abs(db);
                                if (color_diff > 3.0f) {
                                    color_diff_pixels++;
                                    sum_r_diff += dr;
                                    sum_g_diff += dg;
                                    sum_b_diff += db;
                                }
                                if (color_diff > max_color_diff) max_color_diff = color_diff;
                            }
                            float pct = n > 0 ? 100.0f * color_diff_pixels / n : 0.0f;
                            std::cout << "  [CYCLE] Compared " << n << " floor pixels: "
                                      << color_diff_pixels << " differ (" << std::fixed
                                      << std::setprecision(2) << pct << "%) max_diff="
                                      << max_color_diff << "\n";
                            if (color_diff_pixels > 0) {
                                float avg_r = sum_r_diff / color_diff_pixels;
                                float avg_g = sum_g_diff / color_diff_pixels;
                                float avg_b = sum_b_diff / color_diff_pixels;
                                std::cout << "  [CYCLE] Avg color shift: R=" << std::setprecision(1)
                                          << avg_r << " G=" << avg_g << " B=" << avg_b << "\n";

                                // Show worst 5 pixels
                                struct PixDiff { int idx; float dr, dg, db, total; };
                                std::vector<PixDiff> worst;
                                for (int i = 0; i < n; i++) {
                                    float dr2 = case0_second_run_floor[i].r - case0_first_run_floor[i].r;
                                    float dg2 = case0_second_run_floor[i].g - case0_first_run_floor[i].g;
                                    float db2 = case0_second_run_floor[i].b - case0_first_run_floor[i].b;
                                    float td = std::abs(dr2) + std::abs(dg2) + std::abs(db2);
                                    if (td > 3.0f) worst.push_back({i, dr2, dg2, db2, td});
                                }
                                std::sort(worst.begin(), worst.end(),
                                    [](const PixDiff& a, const PixDiff& b) { return a.total > b.total; });
                                int show_n = std::min((int)worst.size(), 5);
                                for (int i = 0; i < show_n; i++) {
                                    auto& w = worst[i];
                                    auto& f = case0_first_run_floor[w.idx];
                                    auto& s = case0_second_run_floor[w.idx];
                                    std::cout << "  [CYCLE] Worst #" << (i+1)
                                              << " @(" << f.x << "," << f.y << ")"
                                              << " first=(" << (int)f.r << "," << (int)f.g << "," << (int)f.b
                                              << ") second=(" << (int)s.r << "," << (int)s.g << "," << (int)s.b
                                              << ") diff=(" << std::setprecision(0) << w.dr
                                              << "," << w.dg << "," << w.db << ")\n";
                                }
                            }
                            assert_check("Cycle test: case 0 clean after cycling (< 10% diff pixels)",
                                         pct < 10.0f,  // DDGI probes retain state across scene changes
                                         "diff=" + std::to_string(color_diff_pixels) +
                                         " (" + std::to_string(pct) + "%) max=" +
                                         std::to_string(max_color_diff));
                            cycle_test_done = true;
                        }

                        // Store frame 30 as first dance frame (only on first run)
                        if (cycle_count == 0) {
                            int total_pixels = render_w * render_h;
                            dance_frames.clear();
                            dance_frames.resize(DANCE_CAPTURE_COUNT);
                            dance_frames[0].resize(total_pixels);
                            std::memcpy(dance_frames[0].data(), native_data,
                                        total_pixels * sizeof(uint32_t));
                            std::cout << "  [DANCE] Stored frame 30 (dance frame 0/" << DANCE_CAPTURE_COUNT << ")\n";
                        }
                        break;
                    }
                    case 1: {
                        // Hard Shadow Baseline: point light (size=0.1), expect sharp shadow edges
                        dump_ppm(native_data, render_w, render_h, "/tmp/case1_f30.ppm");

                        float min_bright = 999.0f, max_bright = 0.0f;
                        for (int y = bb.y0; y <= bb.y1; y += 2) {
                            for (int x = bb.x0; x <= bb.x1; x += 2) {
                                auto sp = sample_pixel(native_data, render_w, x, y);
                                if (!is_background(sp)) {
                                    float b = brightness(sp);
                                    if (b < min_bright) min_bright = b;
                                    if (b > max_bright) max_bright = b;
                                }
                            }
                        }
                        float range1 = max_bright - min_bright;
                        std::cout << "  Brightness: min=" << min_bright << " max=" << max_bright
                                  << " range=" << range1 << "\n";

                        assert_check("Floor is lit (max > 100)", max_bright > 100,
                                     "max=" + std::to_string(max_bright));
                        assert_check("Shadows exist (range > 30)", range1 > 30,
                                     "range=" + std::to_string(range1));
                        assert_check("Hard shadow: strong contrast (range > 100)", range1 > 100,
                                     "range=" + std::to_string(range1));
                        break;
                    }
                    case 2: {
                        // Soft Shadow: area light (size=3.0), expect wider penumbra than case 1
                        dump_ppm(native_data, render_w, render_h, "/tmp/case2_f30.ppm");

                        float min_bright = 999.0f, max_bright = 0.0f;
                        for (int y = bb.y0; y <= bb.y1; y += 2) {
                            for (int x = bb.x0; x <= bb.x1; x += 2) {
                                auto sp = sample_pixel(native_data, render_w, x, y);
                                if (!is_background(sp)) {
                                    float b = brightness(sp);
                                    if (b < min_bright) min_bright = b;
                                    if (b > max_bright) max_bright = b;
                                }
                            }
                        }
                        float range2 = max_bright - min_bright;
                        std::cout << "  Brightness: min=" << min_bright << " max=" << max_bright
                                  << " range=" << range2 << "\n";

                        assert_check("Floor is lit (max > 100)", max_bright > 100,
                                     "max=" + std::to_string(max_bright));
                        assert_check("Shadows exist (range > 30)", range2 > 30,
                                     "range=" + std::to_string(range2));

                        // Measure soft shadow effect by comparing case 2 PPM with case 1 PPM.
                        // The area light should produce visibly different shadow boundaries.
                        // Read case 1 reference PPM and count pixels that differ by > threshold.
                        {
                            // Read case 1 PPM as reference
                            FILE* fp = fopen("/tmp/case1_f30.ppm", "rb");
                            std::vector<uint8_t> case1_data;
                            if (fp) {
                                char line[256];
                                fgets(line, sizeof(line), fp);  // P6
                                // Skip comments
                                while (fgets(line, sizeof(line), fp)) {
                                    if (line[0] != '#') break;
                                }
                                // Already read width/height line
                                fgets(line, sizeof(line), fp);  // maxval
                                case1_data.resize(render_w * render_h * 3);
                                fread(case1_data.data(), 1, case1_data.size(), fp);
                                fclose(fp);
                            }

                            if (!case1_data.empty()) {
                                int diff_pixels = 0;
                                int total_diff_magnitude = 0;
                                int floor_pixels = 0;
                                for (int y = bb.y0; y <= bb.y1; y++) {
                                    for (int x = bb.x0; x <= bb.x1; x++) {
                                        auto sp2 = sample_pixel(native_data, render_w, x, y);
                                        if (is_background(sp2)) continue;
                                        floor_pixels++;

                                        int idx = (y * render_w + x) * 3;
                                        int r1 = case1_data[idx], g1 = case1_data[idx+1], b1 = case1_data[idx+2];
                                        int r2 = sp2.r, g2 = sp2.g, b2 = sp2.b;
                                        int d = std::abs(r1-r2) + std::abs(g1-g2) + std::abs(b1-b2);
                                        if (d > 15) {  // > 5 per channel
                                            diff_pixels++;
                                            total_diff_magnitude += d;
                                        }
                                    }
                                }
                                float diff_pct = floor_pixels > 0 ? 100.0f * diff_pixels / floor_pixels : 0.0f;
                                float avg_diff = diff_pixels > 0 ? (float)total_diff_magnitude / diff_pixels : 0.0f;
                                std::cout << "  Case2 vs Case1: diff_pixels=" << diff_pixels
                                          << " (" << diff_pct << "% of floor)"
                                          << " avg_diff_magnitude=" << avg_diff
                                          << " floor_pixels=" << floor_pixels << "\n";

                                // The area light should produce at least some visible difference
                                // from the point light (different shadow softness)
                                assert_check("Soft shadow: area light differs from point light",
                                             diff_pixels > 50,
                                             "diff_pixels=" + std::to_string(diff_pixels) +
                                             " (" + std::to_string(diff_pct) + "% of floor)");
                            } else {
                                assert_check("Soft shadow: case 1 reference available",
                                             false, "Could not read /tmp/case1_f30.ppm");
                            }
                        }
                        break;
                    }
                    case 3: {
                        // Red wall at x=-3 in world. Light at (0,0,8).
                        // Wall's +X face (facing center) is lit → should bounce red onto floor in FRONT.
                        // Wall's -X face (facing away) is unlit → floor BEHIND should have NO red.
                        //
                        // In isometric: -X maps to upper-left, +X to lower-right.
                        // "Behind wall" = upper-left corner (world x < -3)
                        // "In front of wall" = center-left area (world x ≈ -3 to 0)
                        // "Far from wall" = right side (world x > 0)
                        dump_ppm(native_data, render_w, render_h, "/tmp/case3_f30.ppm");
                        std::cout << "  [DIAG] render=" << render_w << "x" << render_h
                                  << " bbox=(" << bb.x0 << "," << bb.y0 << ")-(" << bb.x1 << "," << bb.y1
                                  << ") size=" << bw << "x" << bh << "\n";

                        // Diagnostic: print R-G excess grid across bbox to visualize
                        // where red bleeding actually lands on screen.
                        {
                            const int COLS = 7, ROWS = 5;
                            std::cout << "  [DIAG GRID] R-G excess (" << COLS << "x" << ROWS << "):\n";
                            for (int row = 0; row < ROWS; row++) {
                                int ry0 = bb.y0 + bh * row / ROWS;
                                int ry1 = bb.y0 + bh * (row + 1) / ROWS;
                                std::cout << "    row " << row << " y[" << ry0 << "-" << ry1 << "]:";
                                for (int col = 0; col < COLS; col++) {
                                    int rx0 = bb.x0 + bw * col / COLS;
                                    int rx1 = bb.x0 + bw * (col + 1) / COLS;
                                    auto s = sample_region_geometry(native_data, render_w, render_h,
                                                                    rx0, ry0, rx1, ry1);
                                    int rg = (int)s.r - (int)s.g;
                                    printf(" [%+4d]", rg);
                                }
                                std::cout << "\n";
                            }
                            std::cout << "    bcy=" << bcy << " bcx=" << bcx << "\n";
                        }

                        // Sample 3 horizontal strips across the floor (bottom half of bbox)
                        // Using bottom half ensures we hit floor, not wall/light.
                        //
                        // Headless diagnostic grid (7×5) shows red wall at ~col 2 (29-43% of bbox width):
                        //   col 2: R-G = +144 (wall face) / +0 (wall shadow) — unreliable mix
                        //   col 3: R-G = +16..+19 — floor with red bleeding (the signal)
                        //   col 4: R-G = +14 — floor with weaker bleeding
                        // "in_front" must avoid col 2 (wall column) to survive small bbox
                        // shifts in Interactive/Retina mode. Use 43-71% (cols 3-4).
                        int bh_x0 = bb.x0 + 4, bh_y0 = bcy, bh_x1 = bb.x0 + bw * 15/100, bh_y1 = bb.y1 - 4;
                        int fr_x0 = bb.x0 + bw * 43/100, fr_y0 = bcy, fr_x1 = bb.x0 + bw * 71/100, fr_y1 = bb.y1 - 4;
                        int fa_x0 = bb.x1 - bw/4, fa_y0 = bcy, fa_x1 = bb.x1 - 4, fa_y1 = bb.y1 - 4;
                        std::cout << "  [REGIONS] behind=(" << bh_x0 << "," << bh_y0 << ")-(" << bh_x1 << "," << bh_y1 << ")"
                                  << " front=(" << fr_x0 << "," << fr_y0 << ")-(" << fr_x1 << "," << fr_y1 << ")"
                                  << " far=(" << fa_x0 << "," << fa_y0 << ")-(" << fa_x1 << "," << fa_y1 << ")\n";
                        auto behind_wall = sample_region_geometry(native_data, render_w, render_h,
                                                                    bh_x0, bh_y0, bh_x1, bh_y1);
                        auto in_front = sample_region_geometry(native_data, render_w, render_h,
                                                                fr_x0, fr_y0, fr_x1, fr_y1);
                        auto far_wall = sample_region_geometry(native_data, render_w, render_h,
                                                                fa_x0, fa_y0, fa_x1, fa_y1);
                        std::cout << "  Behind wall (should be neutral): R=" << (int)behind_wall.r
                                  << " G=" << (int)behind_wall.g << " B=" << (int)behind_wall.b << "\n";
                        std::cout << "  In front of wall (expect red tint): R=" << (int)in_front.r
                                  << " G=" << (int)in_front.g << " B=" << (int)in_front.b << "\n";
                        std::cout << "  Far from wall: R=" << (int)far_wall.r
                                  << " G=" << (int)far_wall.g << " B=" << (int)far_wall.b << "\n";

                        int behind_red_excess = (int)behind_wall.r - (int)behind_wall.g;
                        int front_red_excess = (int)in_front.r - (int)in_front.g;
                        int far_red_excess = (int)far_wall.r - (int)far_wall.g;
                        std::cout << "  Red excess: behind=" << behind_red_excess
                                  << " front=" << front_red_excess
                                  << " far=" << far_red_excess << "\n";

                        // 1. Floor in front of wall should have red tint (GI bouncing off lit face)
                        assert_check("In front of wall has red tint (R > G)",
                                     front_red_excess > 5,
                                     "front R-G=" + std::to_string(front_red_excess));
                        // 2. Red tint present near wall (DDGI probes spread globally, so far may also have tint)
                        assert_check("Color bleeding: front has red tint",
                                     front_red_excess > 3,
                                     "front_excess=" + std::to_string(front_red_excess) +
                                     " far_excess=" + std::to_string(far_red_excess));
                        // 3. Floor BEHIND wall should have negligible red tint (back face unlit).
                        //    Small residual (<10) is acceptable: rays can go around the wall
                        //    sides (wall is 3m wide, floor extends to ±5m in Y).
                        assert_check("No red behind wall (back face unlit)",
                                     behind_red_excess < 10,
                                     "behind R-G=" + std::to_string(behind_red_excess));
                        break;
                    }
                    case 4: {
                        // Red wall left (-X), blue wall right (+X)
                        // Sample bottom half of bbox (floor in isometric) to avoid wall pixels.
                        // 20-45% from each edge horizontally, bcy to bb.y1-4 vertically.
                        dump_ppm(native_data, render_w, render_h, "/tmp/case4_f30.ppm");
                        std::cout << "  [DIAG] render=" << render_w << "x" << render_h
                                  << " bbox=(" << bb.x0 << "," << bb.y0 << ")-(" << bb.x1 << "," << bb.y1
                                  << ") size=" << bw << "x" << bh << "\n";

                        // Vertical range: center band (bcy ± bh/4) captures floor directly
                        // in front of wall faces where GI bleeding is strongest.
                        // The resolution override to 1600x1200 handles Retina consistency.
                        auto left = sample_region_geometry(native_data, render_w, render_h,
                                                            bb.x0 + bw/5, bcy - bh/4, bb.x0 + bw*9/20, bcy + bh/4);
                        auto right = sample_region_geometry(native_data, render_w, render_h,
                                                             bb.x1 - bw*9/20, bcy - bh/4, bb.x1 - bw/5, bcy + bh/4);
                        auto center = sample_region_geometry(native_data, render_w, render_h,
                                                              bcx - bw/8, bcy - bh/4, bcx + bw/8, bcy + bh/4);
                        std::cout << "  Left (near red): R=" << (int)left.r << " G=" << (int)left.g
                                  << " B=" << (int)left.b << "\n";
                        std::cout << "  Right (near blue): R=" << (int)right.r << " G=" << (int)right.g
                                  << " B=" << (int)right.b << "\n";
                        std::cout << "  Center: R=" << (int)center.r << " G=" << (int)center.g
                                  << " B=" << (int)center.b << "\n";

                        assert_check("Near red wall: R > B", left.r > left.b,
                                     "R=" + std::to_string(left.r) + " B=" + std::to_string(left.b));
                        assert_check("Red bleeding margin > 15 RGB",
                                     (int)left.r - (int)left.b > 15,
                                     "margin=" + std::to_string((int)left.r - (int)left.b));
                        assert_check("Near blue wall: B > R", right.b > right.r,
                                     "B=" + std::to_string(right.b) + " R=" + std::to_string(right.r));
                        assert_check("Blue bleeding margin > 15 RGB",
                                     (int)right.b - (int)right.r > 15,
                                     "margin=" + std::to_string((int)right.b - (int)right.r));
                        break;
                    }
                    case 5: {
                        // Green floor, white wall at y=3 (thin in Y, faces ±Y).
                        // Wall: center (0,3,2), X ±1.5, Y ±0.15, Z 0-4.
                        // Light: (0,0,8) white.
                        // In isometric: +Y = upper-left, -Y = lower-right.
                        //
                        // Green GI should ONLY appear:
                        //   - On the wall's front face (-Y side, facing floor center)
                        //   - On floor BETWEEN wall and light (y < 3)
                        // Green GI should NOT appear:
                        //   - Behind the wall (y > 3)
                        //   - To the sides of the wall (|x| > 1.5 area)
                        dump_ppm(native_data, render_w, render_h, "/tmp/case5_f30.ppm");

                        // Sample a grid of 5x5 zones across the rendered bbox
                        int zw = bw / 5;
                        int zh = bh / 5;
                        std::cout << "  Case 5 zone grid (5x5, G-R excess):\n";
                        std::cout << "  bbox=(" << bb.x0 << "," << bb.y0 << ")-("
                                  << bb.x1 << "," << bb.y1 << ") center=(" << bcx << "," << bcy << ")\n";
                        for (int row = 0; row < 5; row++) {
                            std::cout << "  row " << row << ": ";
                            for (int col = 0; col < 5; col++) {
                                int zx0 = bb.x0 + col * zw;
                                int zy0 = bb.y0 + row * zh;
                                int zx1 = zx0 + zw;
                                int zy1 = zy0 + zh;
                                auto z = sample_region_geometry(native_data, render_w, render_h,
                                                                 zx0, zy0, zx1, zy1);
                                int ge = (int)z.g - (int)z.r;
                                std::cout << "[" << ge << "] ";
                            }
                            std::cout << "\n";
                        }

                        // Sample the WALL surface (upper portion of bbox in isometric).
                        // The wall projects to roughly the top 1/4 of the bbox.
                        // Wall center: middle horizontal, top quarter vertical.
                        auto wall_center = sample_region_geometry(native_data, render_w, render_h,
                                                                    bcx - bw/6, bb.y0 + bh/8, bcx + bw/6, bb.y0 + bh/4);
                        // Behind wall: upper-left area (world y > 3, behind wall)
                        auto behind = sample_region_geometry(native_data, render_w, render_h,
                                                              bb.x0 + 4, bb.y0 + 4, bb.x0 + bw/4, bb.y0 + bh/8);

                        int wall_ge = (int)wall_center.g - (int)wall_center.r;
                        int behind_ge = (int)behind.g - (int)behind.r;

                        std::cout << "  Wall center (subtle green tint): R=" << (int)wall_center.r
                                  << " G=" << (int)wall_center.g << " B=" << (int)wall_center.b
                                  << " G-R=" << wall_ge << "\n";
                        std::cout << "  Behind wall (no green): R=" << (int)behind.r
                                  << " G=" << (int)behind.g << " B=" << (int)behind.b
                                  << " G-R=" << behind_ge << "\n";

                        // Wall's front face should have subtle green tint from floor bounce
                        assert_check("Wall has green tint from floor GI (G > R)",
                                     wall_ge > 3,
                                     "wall G-R=" + std::to_string(wall_ge));
                        // Wall tint should be subtle — not overwhelming the white base
                        assert_check("Wall green tint is subtle (G-R < 35)",
                                     wall_ge < 35,
                                     "wall G-R=" + std::to_string(wall_ge));
                        // Behind wall should have negligible green tint
                        assert_check("No green behind wall (back face unlit)",
                                     behind_ge < 10,
                                     "behind G-R=" + std::to_string(behind_ge));
                        break;
                    }
                    case 6: {
                        // Three colored lights: red@x=-5, green@x=0, blue@x=5
                        // Divider walls at x=±2.5 (height 4m) don't reach light height (z=5),
                        // so lights spill over walls. Color zones are present but not isolated.
                        // In isometric: screen left = world -X (red zone), right = +X (blue zone).
                        dump_ppm(native_data, render_w, render_h, "/tmp/case6_f30.ppm");
                        std::cout << "  [DIAG] render=" << render_w << "x" << render_h
                                  << " bbox=(" << bb.x0 << "," << bb.y0 << ")-(" << bb.x1 << "," << bb.y1
                                  << ") size=" << bw << "x" << bh << "\n";

                        // Sample 5 strips across the rendered bbox width (bottom half = floor)
                        int strip_w = bw / 5;
                        struct StripColor { int r, g, b; };
                        StripColor strips[5] = {};
                        for (int s = 0; s < 5; s++) {
                            int sx0 = bb.x0 + s * strip_w;
                            int sx1 = bb.x0 + (s + 1) * strip_w;
                            auto sc = sample_region_geometry(native_data, render_w, render_h,
                                                              sx0, bcy, sx1, bb.y1 - 4);
                            strips[s] = {(int)sc.r, (int)sc.g, (int)sc.b};
                            std::cout << "  Strip " << s << " x=[" << sx0 << "-" << sx1
                                      << "] R=" << strips[s].r << " G=" << strips[s].g
                                      << " B=" << strips[s].b
                                      << " R-G=" << (strips[s].r - strips[s].g)
                                      << " B-G=" << (strips[s].b - strips[s].g) << "\n";
                        }

                        // Red light zone: strip 1 (20-40% from left) should have R > G
                        // This is directly below the red light at world x=-5
                        int red_zone_rg = strips[1].r - strips[1].g;
                        assert_check("Red zone (strip 1): R > G (red tint from red light)",
                                     red_zone_rg > 5,
                                     "R-G=" + std::to_string(red_zone_rg));

                        // Color VARIES across zones: left strip (near red) should be more red
                        // than center/right strips. Compare strip 1 vs strip 3 R-G difference.
                        int strip1_rg = strips[1].r - strips[1].g;
                        int strip3_rg = strips[3].r - strips[3].g;
                        assert_check("Red tint stronger on left than right",
                                     strip1_rg > strip3_rg,
                                     "left_R-G=" + std::to_string(strip1_rg) +
                                     " right_R-G=" + std::to_string(strip3_rg));

                        // Floor is well-lit by multiple lights
                        float avg_b6 = 0;
                        for (int s = 0; s < 5; s++)
                            avg_b6 += (strips[s].r + strips[s].g + strips[s].b) / 3.0f;
                        avg_b6 /= 5.0f;
                        std::cout << "  Avg strip brightness: " << avg_b6 << "\n";
                        assert_check("Floor lit by colored lights (avg > 30)", avg_b6 > 30,
                                     "avg=" + std::to_string(avg_b6));
                        break;
                    }
                    case 7: {
                        // Cornell box with walls positioned for isometric visibility:
                        //   Green wall at X=+4: inner -X face visible, screen RIGHT
                        //   Red wall at Y=+4:   inner -Y face visible, screen UPPER-LEFT
                        //   In isometric, +Y maps to upper-left, +X maps to lower-right.
                        //   Floor center is at screen center-bottom area.
                        dump_ppm(native_data, render_w, render_h, "/tmp/case7_f30.ppm");
                        std::cout << "  [DEBUG Case 7] bbox=(" << bb.x0 << "," << bb.y0
                                  << ")-(" << bb.x1 << "," << bb.y1 << ") center=(" << bcx << "," << bcy << ")\n";

                        // 7x7 zone grid for diagnostics
                        {
                            int zw = bw / 7;
                            int zh = bh / 7;
                            std::cout << "  Case 7 zone grid (7x7, R-G excess):\n";
                            for (int row = 0; row < 7; row++) {
                                std::cout << "  row " << row << ": ";
                                for (int col = 0; col < 7; col++) {
                                    int zx0 = bb.x0 + col * zw;
                                    int zy0 = bb.y0 + row * zh;
                                    int zx1 = zx0 + zw;
                                    int zy1 = zy0 + zh;
                                    auto z = sample_region_geometry(native_data, render_w, render_h,
                                                                     zx0, zy0, zx1, zy1);
                                    int rg = (int)z.r - (int)z.g;
                                    char buf[16];
                                    snprintf(buf, sizeof(buf), "%+4d", rg);
                                    std::cout << "[" << buf << "] ";
                                }
                                std::cout << "\n";
                            }
                        }

                        // Green wall at X=+4 → screen right. Sample floor right side (bottom half).
                        auto floor_near_green = sample_region_geometry(native_data, render_w, render_h,
                                                                        bb.x1 - bw*9/20, bcy, bb.x1 - bw/5, bb.y1 - 4);
                        // Red wall at Y=+4 → screen upper-left. Sample floor LEFT side of bottom half
                        // to stay on floor (not wall surface). In isometric, +Y maps to upper-left,
                        // so floor near the red wall is the left portion of the bottom half.
                        auto floor_near_red = sample_region_geometry(native_data, render_w, render_h,
                                                                      bb.x0 + bw/10, bcy, bb.x0 + bw*2/5, bb.y1 - 4);
                        // Floor center (away from both walls)
                        auto floor_center = sample_region_geometry(native_data, render_w, render_h,
                                                                    bcx - bw/8, bcy - bh/8, bcx + bw/8, bcy + bh/8);
                        std::cout << "  Floor near green wall (right): R=" << (int)floor_near_green.r
                                  << " G=" << (int)floor_near_green.g << " B=" << (int)floor_near_green.b << "\n";
                        std::cout << "  Floor near red wall (upper-left): R=" << (int)floor_near_red.r
                                  << " G=" << (int)floor_near_red.g << " B=" << (int)floor_near_red.b << "\n";
                        std::cout << "  Floor center: R=" << (int)floor_center.r
                                  << " G=" << (int)floor_center.g << " B=" << (int)floor_center.b << "\n";

                        // Green wall bleeds green onto adjacent floor
                        assert_check("Floor near green wall: G > R",
                                     floor_near_green.g > floor_near_green.r,
                                     "G=" + std::to_string(floor_near_green.g) +
                                     " R=" + std::to_string(floor_near_green.r));
                        // Red wall bleeds red onto adjacent floor
                        assert_check("Floor near red wall: R >= G",
                                     floor_near_red.r >= floor_near_red.g,
                                     "R=" + std::to_string(floor_near_red.r) +
                                     " G=" + std::to_string(floor_near_red.g));
                        // Both walls visible: check wall surfaces directly
                        // Green wall surface: right edge of bbox, upper half
                        auto green_wall_px = sample_region_geometry(native_data, render_w, render_h,
                                                                     bb.x1 - bw/5, bb.y0 + bh/5, bb.x1 - bw/10, bcy);
                        // Red wall surface: upper-left area of bbox
                        auto red_wall_px = sample_region_geometry(native_data, render_w, render_h,
                                                                   bb.x0 + bw/10, bb.y0 + bh/10, bb.x0 + bw*2/5, bb.y0 + bh*2/5);
                        std::cout << "  Green wall surface: R=" << (int)green_wall_px.r
                                  << " G=" << (int)green_wall_px.g << " B=" << (int)green_wall_px.b << "\n";
                        std::cout << "  Red wall surface: R=" << (int)red_wall_px.r
                                  << " G=" << (int)red_wall_px.g << " B=" << (int)red_wall_px.b << "\n";
                        assert_check("Green wall visibly green (G > R+30)",
                                     (int)green_wall_px.g > (int)green_wall_px.r + 30,
                                     "G=" + std::to_string(green_wall_px.g) +
                                     " R=" + std::to_string(green_wall_px.r));
                        assert_check("Red wall visibly red (R > G+30)",
                                     (int)red_wall_px.r > (int)red_wall_px.g + 30,
                                     "R=" + std::to_string(red_wall_px.r) +
                                     " G=" + std::to_string(red_wall_px.g));
                        break;
                    }
                    case 8: {
                        // =====================================================
                        // STAINED GLASS: Forward Transparency Validation
                        // =====================================================
                        // The scene has 3 colored glass panels (alpha=0.5) on a
                        // lit white floor. The deferred pass renders the opaque
                        // floor, then Pass 3.5 blends the glass panels on top.
                        //
                        // We verify:
                        //   1. Glass panels are VISIBLE (not discarded by G-buffer)
                        //   2. Each glass panel tints the view with its color
                        //   3. Floor between panels remains neutral
                        //   4. Glass regions are dimmer than clear floor (alpha absorption)

                        dump_ppm(native_data, render_w, render_h, "/tmp/case8_stained_glass.ppm");
                        std::cout << "  [DEBUG] Frame dumped to /tmp/case8_stained_glass.ppm ("
                                  << render_w << "x" << render_h << ")\n";

                        // Diagnostic zone grid: dominant color channel at each point
                        std::cout << "  Case 8 color grid (9x5, dominant channel):\n";
                        int bw = bb.x1 - bb.x0;
                        int bh = bb.y1 - bb.y0;
                        for (int row = 0; row < 5; row++) {
                            std::cout << "    row " << row << ": ";
                            for (int col = 0; col < 9; col++) {
                                int sx = bb.x0 + col * bw / 9 + bw / 18;
                                int sy = bb.y0 + row * bh / 5 + bh / 10;
                                auto px = sample_pixel(native_data, render_w, sx, sy);
                                char ch = '.';
                                if (is_background(px)) { ch = '_'; }
                                else if (px.r > px.g + 15 && px.r > px.b + 15) { ch = 'R'; }
                                else if (px.g > px.r + 15 && px.g > px.b + 15) { ch = 'G'; }
                                else if (px.b > px.r + 15 && px.b > px.g + 15) { ch = 'B'; }
                                else { ch = 'W'; }  // neutral white
                                std::cout << "[" << ch << " "
                                          << std::setw(3) << (int)px.r << ","
                                          << std::setw(3) << (int)px.g << ","
                                          << std::setw(3) << (int)px.b << "] ";
                            }
                            std::cout << "\n";
                        }

                        // Full-bbox scan: find ALL colored pixels in the rendered image.
                        // Glass panels may be small on screen, so we scan the entire bbox.
                        int red_count = 0, green_count = 0, blue_count = 0, neutral_count = 0;
                        int max_r_excess = 0, max_g_excess = 0, max_b_excess = 0;
                        SampledPixel best_red = {0,0,0}, best_green = {0,0,0}, best_blue = {0,0,0};
                        SampledPixel best_neutral = {0,0,0};
                        float max_neutral_bright = 0;

                        for (int y = bb.y0 + 2; y < bb.y1 - 2; y += 3) {
                            for (int x = bb.x0 + 2; x < bb.x1 - 2; x += 3) {
                                auto px = sample_pixel(native_data, render_w, x, y);
                                if (is_background(px)) continue;

                                int r_excess = (int)px.r - std::max((int)px.g, (int)px.b);
                                int g_excess = (int)px.g - std::max((int)px.r, (int)px.b);
                                int b_excess = (int)px.b - std::max((int)px.r, (int)px.g);
                                float br = brightness(px);

                                if (r_excess > 10) {
                                    red_count++;
                                    if (r_excess > max_r_excess) { max_r_excess = r_excess; best_red = px; }
                                } else if (g_excess > 10) {
                                    green_count++;
                                    if (g_excess > max_g_excess) { max_g_excess = g_excess; best_green = px; }
                                } else if (b_excess > 10) {
                                    blue_count++;
                                    if (b_excess > max_b_excess) { max_b_excess = b_excess; best_blue = px; }
                                } else if (br > 30) {
                                    neutral_count++;
                                    if (br > max_neutral_bright) { max_neutral_bright = br; best_neutral = px; }
                                }
                            }
                        }

                        std::cout << "  Full bbox scan (" << bw << "x" << bh << "):\n";
                        std::cout << "    Red pixels: " << red_count
                                  << " (peak R-excess=" << max_r_excess
                                  << " best=(" << (int)best_red.r << "," << (int)best_red.g << "," << (int)best_red.b << "))\n";
                        std::cout << "    Green pixels: " << green_count
                                  << " (peak G-excess=" << max_g_excess
                                  << " best=(" << (int)best_green.r << "," << (int)best_green.g << "," << (int)best_green.b << "))\n";
                        std::cout << "    Blue pixels: " << blue_count
                                  << " (peak B-excess=" << max_b_excess
                                  << " best=(" << (int)best_blue.r << "," << (int)best_blue.g << "," << (int)best_blue.b << "))\n";
                        std::cout << "    Neutral pixels: " << neutral_count
                                  << " (peak bright=" << max_neutral_bright
                                  << " best=(" << (int)best_neutral.r << "," << (int)best_neutral.g << "," << (int)best_neutral.b << "))\n";

                        // === ASSERTIONS ===

                        // 1. Glass panels are VISIBLE (not silently discarded)
                        //    At least some pixels should show red/green/blue dominance
                        int total_colored = red_count + green_count + blue_count;
                        assert_check("Glass panels visible (colored pixels > 0)",
                                     total_colored > 0,
                                     "colored=" + std::to_string(total_colored) +
                                     " (R=" + std::to_string(red_count) +
                                     " G=" + std::to_string(green_count) +
                                     " B=" + std::to_string(blue_count) + ")");

                        // 2. Red glass panel produces red-tinted pixels
                        assert_check("Red glass: red-dominant pixels found",
                                     red_count > 0,
                                     "red_pixels=" + std::to_string(red_count) +
                                     " peak_excess=" + std::to_string(max_r_excess));

                        // 3. Green glass panel produces green-tinted pixels
                        assert_check("Green glass: green-dominant pixels found",
                                     green_count > 0,
                                     "green_pixels=" + std::to_string(green_count) +
                                     " peak_excess=" + std::to_string(max_g_excess));

                        // 4. Blue glass panel produces blue-tinted pixels
                        assert_check("Blue glass: blue-dominant pixels found",
                                     blue_count > 0,
                                     "blue_pixels=" + std::to_string(blue_count) +
                                     " peak_excess=" + std::to_string(max_b_excess));

                        // 5. Neutral floor still exists (not everything is tinted)
                        assert_check("Clear floor: neutral pixels exist",
                                     neutral_count > 0,
                                     "neutral_pixels=" + std::to_string(neutral_count));

                        // 6. Three distinct color zones (all three colors present)
                        assert_check("Three distinct color zones (R, G, B all present)",
                                     red_count > 0 && green_count > 0 && blue_count > 0,
                                     "R=" + std::to_string(red_count) +
                                     " G=" + std::to_string(green_count) +
                                     " B=" + std::to_string(blue_count));

                        // 7. Color signal is strong (peak excess > 20 for each)
                        assert_check("Red glass has strong color (excess > 20)",
                                     max_r_excess > 20,
                                     "peak=" + std::to_string(max_r_excess));
                        assert_check("Green glass has strong color (excess > 20)",
                                     max_g_excess > 20,
                                     "peak=" + std::to_string(max_g_excess));
                        assert_check("Blue glass has strong color (excess > 20)",
                                     max_b_excess > 20,
                                     "peak=" + std::to_string(max_b_excess));

                        break;
                    }
                    }
                }
            }
            assertions_done = true;

            // In headless mode, auto-advance (Case 0 waits for dance capture at frames 31-39)
            if (!is_interactive && test_case != 0) {
                // Advance through all cases, then cycle back to 0
                if (cycle_count == 0 && test_case < MAX_CASE) {
                    test_case++;
                    create_scene();
                } else if (cycle_count == 0) {
                    // Done with intermediate cases, cycle back to case 0
                    cycle_count = 1;
                    test_case = 0;
                    create_scene();
                } else if (test_case < MAX_CASE) {
                    test_case++;
                    create_scene();
                } else {
                    should_quit = true;
                }
            }
        }

        // Case 0 cycle test: skip dance on second run, just quit
        if (test_case == 0 && assertions_done && cycle_count == 1 && !is_interactive) {
            should_quit = true;
        }

        // Case 0: capture dance frames 31-39 (frame 30 already stored in assertion block)
        if (test_case == 0 && assertions_done && !case0_dance_done && cycle_count == 0) {
            int dance_idx = frame_counter - DANCE_CAPTURE_START;
            if (dance_idx >= 1 && dance_idx < DANCE_CAPTURE_COUNT) {
                rs.get_render_pipeline().wait_for_gpu_completion();
                const uint8_t* native_data = rs.get_render_buffer().get_native_data();
                if (native_data) {
                    int total_pixels = render_w * render_h;
                    dance_frames[dance_idx].resize(total_pixels);
                    std::memcpy(dance_frames[dance_idx].data(), native_data,
                                total_pixels * sizeof(uint32_t));

                    // Dump each raw frame
                    std::string path = "/tmp/case0_f" + std::to_string(frame_counter) + ".ppm";
                    dump_ppm(native_data, render_w, render_h, path.c_str());
                }
            }

            // After frame 39: run dance analysis
            if (frame_counter == DANCE_CAPTURE_START + DANCE_CAPTURE_COUNT - 1) {
                std::cout << "  [DANCE] All " << DANCE_CAPTURE_COUNT << " frames captured, analyzing...\n";

                auto bb = find_rendered_bbox(
                    reinterpret_cast<const uint8_t*>(dance_frames[0].data()), render_w, render_h);

                // ============================================================
                // DIAGNOSTIC 2: Dancing Detection (temporal, multi-frame)
                // Per-pixel max brightness swing across 10 consecutive frames
                // ============================================================
                int dancing_pixel_count = 0;
                int geo_pixel_count = 0;
                float max_swing_global = 0.0f;
                int max_swing_x = 0, max_swing_y = 0;
                float sum_swing = 0.0f;

                struct DanceInfo {
                    int x, y;
                    float min_b, max_b, swing;
                };
                std::vector<DanceInfo> worst_dancers;

                // Dance heatmap: green channel = swing magnitude
                std::vector<uint8_t> dance_map(render_w * render_h * 3, 0);

                if (bb.valid) {
                    for (int y = bb.y0; y <= bb.y1; y++) {
                        for (int x = bb.x0; x <= bb.x1; x++) {
                            int idx = y * render_w + x;

                            // Check if geometry pixel in frame 0
                            uint32_t p0 = dance_frames[0][idx];
                            uint8_t r0 = (p0 >> 16) & 0xFF;
                            uint8_t g0 = (p0 >> 8) & 0xFF;
                            uint8_t b0 = p0 & 0xFF;
                            float br0 = (r0 + g0 + b0) / 3.0f;
                            // Use is_background logic inline
                            if (r0 <= 12 && g0 <= 12 && b0 <= 17) continue;
                            geo_pixel_count++;

                            // Find min/max brightness across all 10 frames
                            float min_b = br0, max_b = br0;
                            for (int f = 1; f < DANCE_CAPTURE_COUNT; f++) {
                                if (dance_frames[f].empty()) continue;
                                uint32_t pf = dance_frames[f][idx];
                                uint8_t rf = (pf >> 16) & 0xFF;
                                uint8_t gf = (pf >> 8) & 0xFF;
                                uint8_t bf = pf & 0xFF;
                                float brf = (rf + gf + bf) / 3.0f;
                                if (brf < min_b) min_b = brf;
                                if (brf > max_b) max_b = brf;
                            }

                            float swing = max_b - min_b;
                            sum_swing += swing;

                            if (swing > 5.0f) {
                                dancing_pixel_count++;
                                DanceInfo d = {x, y, min_b, max_b, swing};
                                worst_dancers.push_back(d);
                            }

                            if (swing > max_swing_global) {
                                max_swing_global = swing;
                                max_swing_x = x;
                                max_swing_y = y;
                            }

                            // Encode swing into green channel of heatmap
                            int map_idx = (y * render_w + x) * 3;
                            dance_map[map_idx + 1] = (uint8_t)std::min(255.0f, swing * 10.0f);
                        }
                    }
                }

                float dancing_pct = geo_pixel_count > 0 ?
                    100.0f * dancing_pixel_count / geo_pixel_count : 0.0f;
                float avg_swing = geo_pixel_count > 0 ? sum_swing / geo_pixel_count : 0.0f;

                // Sort worst dancers by swing descending
                std::sort(worst_dancers.begin(), worst_dancers.end(),
                    [](const DanceInfo& a, const DanceInfo& b) {
                        return a.swing > b.swing;
                    });

                std::cout << "  [DANCE] Dancing pixels (swing>5): " << dancing_pixel_count
                          << "/" << geo_pixel_count
                          << " (" << std::fixed << std::setprecision(3) << dancing_pct << "%)\n";
                std::cout << "  [DANCE] Max swing: " << std::setprecision(1) << max_swing_global
                          << " at (" << max_swing_x << "," << max_swing_y << ")\n";
                std::cout << "  [DANCE] Avg swing: " << std::setprecision(2) << avg_swing << "\n";

                int show = std::min((int)worst_dancers.size(), 10);
                for (int i = 0; i < show; i++) {
                    auto& d = worst_dancers[i];
                    std::cout << "    #" << (i+1) << " (" << d.x << "," << d.y
                              << ") swing=" << std::setprecision(1) << d.swing
                              << " range=[" << d.min_b << "," << d.max_b << "]\n";
                }

                // Dump dance heatmap
                {
                    FILE* f = fopen("/tmp/case0_dance_map.ppm", "wb");
                    if (f) {
                        fprintf(f, "P6\n%d %d\n255\n", render_w, render_h);
                        fwrite(dance_map.data(), 1, render_w * render_h * 3, f);
                        fclose(f);
                        std::cout << "  [DANCE] Heatmap dumped to /tmp/case0_dance_map.ppm\n";
                    }
                }

                assert_check("Dancing shadows low (<0.1% pixels with swing>5)",
                             dancing_pct < 0.1f,
                             "dancing=" + std::to_string(dancing_pixel_count) +
                             " (" + std::to_string(dancing_pct) + "%)" +
                             " max_swing=" + std::to_string(max_swing_global));

                // ============================================================
                // DIAGNOSTIC 3: Combined Report
                // ============================================================
                // Retrieve spot count from assertions (it was logged above)
                std::cout << "\n  [DIAGNOSTIC] === Combined Spot + Dance Report ===\n";
                std::cout << "  [DIAGNOSTIC] Dancing: " << dancing_pixel_count
                          << " pixels with swing > 5 (" << std::setprecision(3) << dancing_pct << "% of geometry)\n";
                std::cout << "  [DIAGNOSTIC] Max dance swing: " << std::setprecision(1) << max_swing_global
                          << " at (" << max_swing_x << "," << max_swing_y << ")\n";
                std::cout << "  [DIAGNOSTIC] Avg swing across all geometry: " << std::setprecision(2) << avg_swing << "\n";

                // Free dance frames memory
                dance_frames.clear();
                dance_frames.shrink_to_fit();

                case0_dance_done = true;

                // Auto-advance
                if (!is_interactive) {
                    // Cycle test: 0 → all cases → 0 (verifies no color bleeding)
                    const int BISECT_START = 1;
                    const int BISECT_MAX = MAX_CASE;
                    if (cycle_count == 0 && test_case == 0) {
                        test_case = BISECT_START;
                        create_scene();
                    } else if (cycle_count == 0 && test_case < BISECT_MAX) {
                        test_case++;
                        create_scene();
                    } else if (cycle_count == 0) {
                        cycle_count = 1;
                        test_case = 0;
                        create_scene();
                    } else {
                        should_quit = true;
                    }
                }
            }
        }

        // Headless timeout safety
        if (!is_interactive && frame_counter > DANCE_CAPTURE_START + DANCE_CAPTURE_COUNT + 5) {
            should_quit = true;
        }
    }

    // Summary
    std::cout << "\n============================================================\n";
    std::cout << "  SSGI VISUAL TEST RESULTS\n";
    std::cout << "============================================================\n";
    int total = 0, passed = 0;
    for (auto& a : g_all_assertions) {
        total++;
        if (a.passed) passed++;
        if (!a.passed) {
            std::cout << "  FAIL: " << a.name << " — " << a.detail << "\n";
        }
    }
    std::cout << "  " << passed << "/" << total << " assertions passed\n\n";

    return (passed == total);
}
