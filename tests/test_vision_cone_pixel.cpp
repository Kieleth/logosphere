// Engine tests for the per-pixel vision-cone visibility algorithm
// (`compute_vision_cone_visibility`). Pure CPU math, no Metal.
//
// This is the SPEC the Pass-4 Metal kernel implements pixel-by-
// pixel. When the kernel and these tests disagree, the kernel is
// the bug. ATs at the game level (examples/logotron/tests/at)
// stack on top: they assemble realistic gameplay state and call
// this function for sample pixels.

#include "logosphere/rendering/vision_cone_pixel.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using logosphere::rendering::VisionConePixelInput;
using logosphere::rendering::VisionConePixelParams;
using logosphere::rendering::compute_vision_cone_visibility;
using logosphere::rendering::kVisionConeSkyId;

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    std::cout << "  " #name "... "; \
    try { test_##name(); tests_passed++; std::cout << "PASS" << std::endl; } \
    catch (const std::exception& e) { tests_failed++; std::cout << "FAIL: " << e.what() << std::endl; }

#define ASSERT_NEAR(a, b, tol, msg) \
    if (std::abs((a) - (b)) > (tol)) throw std::runtime_error( \
        std::string(msg) + " [got=" + std::to_string(a) + " want=" + std::to_string(b) + "]")

#define ASSERT_TRUE(cond, msg) \
    if (!(cond)) throw std::runtime_error(std::string(msg))

constexpr float kPi     = 3.14159265358979323846f;
constexpr float kHalfPi = kPi * 0.5f;

// Standard cone params: viewer at origin facing +Y, 180° cone,
// range 60, hard cutoff (darkness=0), inner sharp until 70%.
// Memory + occlusion + dynamic disabled.
static VisionConePixelParams std_params() {
    VisionConePixelParams p{};
    p.viewer_x = 0.0f; p.viewer_y = 0.0f;
    p.look_direction = 0.0f;        // +Y (engine convention)
    p.half_fov = kHalfPi;           // 180° total
    p.range = 60.0f;
    p.inner_falloff = 0.70f;
    p.darkness = 0.0f;
    p.occlusion_count = 0;
    p.occlusion_distance = nullptr;
    p.memory_enabled = 0;
    p.memory_width = 0; p.memory_height = 0;
    p.memory_origin_x = 0; p.memory_origin_y = 0;
    p.memory_cell_size = 1.0f;
    p.memory_dim = 0.45f;
    p.memory_grid = nullptr;
    p.dynamic_map_size = 0;
    p.is_dynamic_map = nullptr;
    return p;
}

static VisionConePixelInput make_pixel(float x, float y, unsigned int id = 0) {
    return VisionConePixelInput{x, y, id};
}

// =========================================================================
// Sky pixel — visibility = darkness (early return).
// =========================================================================
void test_sky_pixel_uses_darkness() {
    auto p = std_params();
    p.darkness = 0.2f;
    float v = compute_vision_cone_visibility(
        p, make_pixel(0, 0, kVisionConeSkyId));
    ASSERT_NEAR(v, 0.2f, 1e-5f, "sky → darkness");
}

// =========================================================================
// Pixel directly in front (in cone, in range, no occlusion) → 1.0.
// =========================================================================
void test_pixel_in_front_full_visibility() {
    auto p = std_params();
    float v = compute_vision_cone_visibility(p, make_pixel(0, 5));
    ASSERT_NEAR(v, 1.0f, 1e-3f, "pixel in front → 1.0");
}

// =========================================================================
// Pixel BEHIND viewer (180° cone faces +Y; pixel at -Y is outside).
// =========================================================================
void test_pixel_behind_viewer_is_dark() {
    auto p = std_params();
    float v = compute_vision_cone_visibility(p, make_pixel(0, -5));
    ASSERT_NEAR(v, p.darkness, 1e-5f, "behind viewer → darkness");
}

// =========================================================================
// Pixel beyond range → darkness.
// =========================================================================
void test_pixel_beyond_range_is_dark() {
    auto p = std_params();
    float v = compute_vision_cone_visibility(p, make_pixel(0, 70));
    ASSERT_NEAR(v, p.darkness, 1e-5f, "out of range → darkness");
}

// =========================================================================
// LOS occlusion: pixel behind a wall (per-bin distance < pixel
// distance) → darkness even though it's in the cone.
// =========================================================================
void test_pixel_behind_occluder_is_dark() {
    auto p = std_params();
    // 4 bins across 180°, all at distance 3m.
    std::vector<float> occl(4, 3.0f);
    p.occlusion_count = 4;
    p.occlusion_distance = occl.data();
    // Pixel at (0, 10) — 10m in front, well past the 3m occluder
    // (plus the OCC_BIAS of 0.7 m).
    float v = compute_vision_cone_visibility(p, make_pixel(0, 10));
    ASSERT_NEAR(v, p.darkness, 1e-5f, "behind occluder → darkness");
}

// =========================================================================
// Memory blend: pixel out of cone, cell has memory = 1, particle
// is STATIC → visibility = memory_dim (the dim ghost shows).
// =========================================================================
void test_static_pixel_out_of_cone_uses_memory_blend() {
    auto p = std_params();
    p.memory_enabled = 1;
    p.memory_width = 4; p.memory_height = 4;
    p.memory_origin_x = -2.0f; p.memory_origin_y = -2.0f;
    p.memory_cell_size = 1.0f;     // 4×4 grid covering [-2, +2]
    p.memory_dim = 0.45f;
    std::vector<float> mem(16, 0.0f);
    // Cell at world (-1.5, -1.5) → grid index (0, 0). Set to 1.0.
    mem[0 * 4 + 0] = 1.0f;
    p.memory_grid = mem.data();
    // Pixel at world (-1.5, -1.5) — directly behind viewer (out of
    // 180° cone facing +Y). Static particle (no dynamic_map).
    float v = compute_vision_cone_visibility(p, make_pixel(-1.5f, -1.5f, 7));
    ASSERT_NEAR(v, p.memory_dim, 1e-5f,
                "static pixel out of cone, mem=1 → memory_dim");
}

// =========================================================================
// THE BUG TEST — dynamic pixel out of cone with memory cell lit
// must NOT use the memory blend. Bike is invisible.
// =========================================================================
void test_dynamic_pixel_out_of_cone_is_invisible_even_with_memory() {
    auto p = std_params();
    p.memory_enabled = 1;
    p.memory_width = 4; p.memory_height = 4;
    p.memory_origin_x = -2.0f; p.memory_origin_y = -2.0f;
    p.memory_cell_size = 1.0f;
    p.memory_dim = 0.45f;
    std::vector<float> mem(16, 1.0f);   // every cell lit
    p.memory_grid = mem.data();
    // Dynamic-particle map: particle id 7 is dynamic.
    std::vector<unsigned char> dyn(8, 0);
    dyn[7] = 1;
    p.dynamic_map_size = static_cast<int>(dyn.size());
    p.is_dynamic_map = dyn.data();
    // Pixel showing particle id=7 at (-1.5, -1.5) — out of cone.
    float v = compute_vision_cone_visibility(
        p, make_pixel(-1.5f, -1.5f, 7));
    ASSERT_NEAR(v, p.darkness, 1e-5f,
                "dynamic pixel out of cone must stay at darkness, "
                "no memory blend");
}

// =========================================================================
// Negative case for the above: same setup but particle is NOT in
// the dynamic map → memory blend SHOULD apply. Locks the contract
// that dynamic-skip is opt-in per particle.
// =========================================================================
void test_undeclared_particle_falls_back_to_memory_blend() {
    auto p = std_params();
    p.memory_enabled = 1;
    p.memory_width = 4; p.memory_height = 4;
    p.memory_origin_x = -2.0f; p.memory_origin_y = -2.0f;
    p.memory_cell_size = 1.0f;
    p.memory_dim = 0.45f;
    std::vector<float> mem(16, 1.0f);
    p.memory_grid = mem.data();
    // Map declares particle 0 dynamic, NOT particle 5.
    std::vector<unsigned char> dyn(4, 0);
    dyn[0] = 1;
    p.dynamic_map_size = static_cast<int>(dyn.size());
    p.is_dynamic_map = dyn.data();
    // Pixel particle id=5 — out of map's range → treated as static.
    float v = compute_vision_cone_visibility(
        p, make_pixel(-1.5f, -1.5f, 5));
    ASSERT_NEAR(v, p.memory_dim, 1e-5f,
                "particle outside dynamic_map_size defaults to static");
}

// =========================================================================
// Dynamic pixel INSIDE the cone — visibility stays at the live cone
// value (memory blend is skipped, doesn't matter — live cone wins).
// Asserts the dynamic-skip doesn't accidentally darken in-cone
// dynamic pixels (would be a regression).
// =========================================================================
void test_dynamic_pixel_inside_cone_full_visibility() {
    auto p = std_params();
    p.memory_enabled = 1;
    p.memory_width = 4; p.memory_height = 4;
    p.memory_origin_x = -2.0f; p.memory_origin_y = -2.0f;
    p.memory_cell_size = 1.0f;
    std::vector<float> mem(16, 0.0f);
    p.memory_grid = mem.data();
    std::vector<unsigned char> dyn(8, 0);
    dyn[3] = 1;
    p.dynamic_map_size = static_cast<int>(dyn.size());
    p.is_dynamic_map = dyn.data();
    // Pixel particle id=3 directly in front (in cone).
    float v = compute_vision_cone_visibility(
        p, make_pixel(0, 5, 3));
    ASSERT_NEAR(v, 1.0f, 1e-3f,
                "in-cone dynamic pixel still fully visible");
}

int main() {
    std::cout << "=== Logosphere — Vision Cone Per-Pixel Algorithm ===" << std::endl;
    TEST(sky_pixel_uses_darkness);
    TEST(pixel_in_front_full_visibility);
    TEST(pixel_behind_viewer_is_dark);
    TEST(pixel_beyond_range_is_dark);
    TEST(pixel_behind_occluder_is_dark);
    TEST(static_pixel_out_of_cone_uses_memory_blend);
    TEST(dynamic_pixel_out_of_cone_is_invisible_even_with_memory);
    TEST(undeclared_particle_falls_back_to_memory_blend);
    TEST(dynamic_pixel_inside_cone_full_visibility);
    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
