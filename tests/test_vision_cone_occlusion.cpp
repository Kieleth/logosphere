// Engine tests for the vision-cone LOS occlusion mask generator.
// Pure ray-vs-OBB math, no GPU, no KG — runs in any C++17 toolchain.
//
// The shader (vision_cone_postprocess.metal) consumes the output of
// `compute_vision_cone_occlusion` to darken pixels behind walls.
// Bugs here visibly show up as "I can see through walls"; locking
// the math down with explicit tests makes the GPU side trivially
// correctable when we eyeball a regression.

#include "logosphere/rendering/vision_cone_occlusion.h"

#include <cmath>
#include <iostream>
#include <string>

using logosphere::rendering::OccluderSegment;
using logosphere::rendering::compute_vision_cone_occlusion;

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

constexpr float kPi      = 3.14159265358979323846f;
constexpr float kHalfPi  = kPi * 0.5f;
constexpr float kThick   = 0.075f;   // matches Logotron kWallThickness/2

// Helper: full sweep across a 180° cone facing +Y (north),
// 64 bins, range 60. Used by most tests below.
static void sweep_north(const OccluderSegment* segs, int n,
                        float vx, float vy,
                        float* out, int bins = 64,
                        float range = 60.0f) {
    compute_vision_cone_occlusion(segs, n, vx, vy,
                                  /*look=*/0.0f,
                                  /*half_fov=*/kHalfPi,
                                  range, out, bins);
}

// =========================================================================
// 1 · No occluders — every bin equals `range` exactly.
// =========================================================================
void test_empty_segment_list_fills_range() {
    float bins[64];
    sweep_north(nullptr, 0, 0.0f, 0.0f, bins);
    for (int i = 0; i < 64; ++i) {
        ASSERT_NEAR(bins[i], 60.0f, 1e-4f, "bin equals range");
    }
}

// =========================================================================
// 2 · Wall directly in front, perpendicular to look direction.
// =========================================================================
void test_perpendicular_wall_blocks_center_bin() {
    // Viewer at origin facing +Y. Wall: horizontal segment at y=10,
    // x ∈ [-5, +5]. The wall has perpendicular thickness `kThick`
    // (half_thick), so its FRONT FACE is at y = 10 - kThick. The
    // OBB raycast reports the front face — that's geometrically
    // correct: pixels beyond the front face are occluded.
    OccluderSegment seg{-5.0f, 10.0f, 5.0f, 10.0f, kThick};
    float bins[64];
    sweep_north(&seg, 1, 0.0f, 0.0f, bins);
    const float front = 10.0f - kThick;
    ASSERT_NEAR(bins[31], front, 0.10f, "bin 31 (just below center) hits wall front");
    ASSERT_NEAR(bins[32], front, 0.10f, "bin 32 (center) hits wall front");
    ASSERT_NEAR(bins[33], front, 0.10f, "bin 33 (just above center) hits wall front");
}

// =========================================================================
// 3 · Same wall — extreme side bins miss it (it doesn't extend far enough).
// =========================================================================
void test_perpendicular_wall_misses_extreme_bins() {
    OccluderSegment seg{-5.0f, 10.0f, 5.0f, 10.0f, kThick};
    float bins[64];
    sweep_north(&seg, 1, 0.0f, 0.0f, bins);
    // Bin 0 is at -90° (pure west). Wall is in front (north); a
    // ray going due west never hits it.
    ASSERT_NEAR(bins[0],  60.0f, 1e-3f, "bin 0 (-90°) misses front wall");
    ASSERT_NEAR(bins[63], 60.0f, 1e-3f, "bin 63 (+90°) misses front wall");
}

// =========================================================================
// 4 · Wall behind viewer is invisible to a forward-facing cone.
// =========================================================================
void test_wall_behind_viewer_does_not_block() {
    OccluderSegment seg{-5.0f, -10.0f, 5.0f, -10.0f, kThick};
    float bins[64];
    sweep_north(&seg, 1, 0.0f, 0.0f, bins);
    for (int i = 0; i < 64; ++i) {
        ASSERT_NEAR(bins[i], 60.0f, 1e-3f, "behind-wall must not occlude");
    }
}

// =========================================================================
// 5 · Wall just past `range` is ignored — no fake "almost there" hits.
// =========================================================================
void test_wall_past_range_ignored() {
    OccluderSegment seg{-5.0f, 70.0f, 5.0f, 70.0f, kThick};
    float bins[64];
    sweep_north(&seg, 1, 0.0f, 0.0f, bins, /*bins=*/64, /*range=*/60.0f);
    for (int i = 0; i < 64; ++i) {
        ASSERT_NEAR(bins[i], 60.0f, 1e-3f, "past-range wall ignored");
    }
}

// =========================================================================
// 6 · Two walls at different distances — nearest wins per bin.
// =========================================================================
void test_two_walls_nearest_wins() {
    OccluderSegment near_w{-5.0f, 5.0f,  5.0f, 5.0f,  kThick};
    OccluderSegment far_w {-5.0f, 15.0f, 5.0f, 15.0f, kThick};
    OccluderSegment segs[2] = {far_w, near_w};   // near after far
    float bins[64];
    sweep_north(segs, 2, 0.0f, 0.0f, bins);
    // Front face of the near wall is at 5 - kThick.
    ASSERT_NEAR(bins[32], 5.0f - kThick, 0.10f, "nearest wall wins (near=5, far=15)");
}

// =========================================================================
// 7 · Diagonal wall — bins on the wall's side are reduced; the
// opposite side is clear.
// =========================================================================
void test_diagonal_wall_occludes_one_side() {
    // Wall from (1, 1) to (10, 10) — lies in the NE quadrant only.
    OccluderSegment seg{1.0f, 1.0f, 10.0f, 10.0f, kThick};
    float bins[64];
    sweep_north(&seg, 1, 0.0f, 0.0f, bins);
    // Bin 16 covers ~-45° (northwest); should be clear.
    ASSERT_NEAR(bins[16], 60.0f, 0.5f, "NW bin clear of NE wall");
    // Bin 48 covers ~+45° (northeast); should hit.
    ASSERT_TRUE(bins[48] < 30.0f, "NE bin hits diagonal wall");
}

// =========================================================================
// 8 · Wall outside the cone arc is silently ignored even if close.
// =========================================================================
void test_wall_outside_cone_arc_ignored() {
    // Cone faces +X (look_direction = π/2), narrow 30°. Wall at +Y
    // is way outside the arc.
    OccluderSegment seg{-5.0f, 5.0f, 5.0f, 5.0f, kThick};
    float bins[16];
    compute_vision_cone_occlusion(&seg, 1, 0.0f, 0.0f,
                                  /*look=*/kHalfPi,
                                  /*half_fov=*/(kPi / 12.0f),  // 30° / 2 = 15°
                                  60.0f, bins, 16);
    for (int i = 0; i < 16; ++i) {
        ASSERT_NEAR(bins[i], 60.0f, 1e-3f, "wall outside arc must not affect any bin");
    }
}

// =========================================================================
// 9 · Wall at the viewer's position must not produce a phantom u≈0
// occluder (the bug where the player's own just-frozen trail blacks
// out the cone).
// =========================================================================
void test_wall_through_viewer_is_not_zero_distance() {
    // Vertical wall passing exactly through the viewer's location
    // (viewer at (0, 0), wall x=0, y ∈ [-5, +5]). The viewer is
    // ON the wall. Ray going north (+Y, away from the wall): the
    // wall is collinear / behind, must not register as a 0-distance
    // hit.
    OccluderSegment seg{0.0f, -5.0f, 0.0f, 5.0f, kThick};
    float bins[64];
    sweep_north(&seg, 1, 0.0f, 0.0f, bins);
    // Bin 32 (center, +Y) — we should NOT see distance ~0.
    ASSERT_TRUE(bins[32] > 5.0f, "ray leaving the wall must not see u≈0 hit");
}

// =========================================================================
// 10 · Cone narrower than 360° still uses the full bin range.
// =========================================================================
void test_narrow_cone_distributes_bins_over_arc() {
    // Wall directly east of viewer; cone faces +X (east), 60° total.
    // Bin 0 should be at look - half_fov = +π/2 - π/6 = +60°ish (NE),
    // bin N-1 at look + half_fov = 120° (SE). Wall at x=10 directly
    // east — only the central bins should hit.
    OccluderSegment seg{10.0f, -5.0f, 10.0f, 5.0f, kThick};
    float bins[16];
    compute_vision_cone_occlusion(&seg, 1, 0.0f, 0.0f,
                                  /*look=*/kHalfPi,
                                  /*half_fov=*/(kPi / 6.0f),  // 60° / 2 = 30°
                                  60.0f, bins, 16);
    // Bin 8 (center) should hit at ~10 m.
    ASSERT_NEAR(bins[8], 10.0f, 0.5f, "narrow cone center bin hits east wall");
    // Bin 0 and 15 (cone edges, 30° off) — wall is 10 m east, edge
    // at 30°: tangent is 5.77 m of perpendicular, wall extends ±5,
    // so the ray *just* misses → should be range.
    // Allow some slop because numerical edge of the wall thickness.
    ASSERT_TRUE(bins[0]  > 9.0f,  "bin 0 mostly free at cone edge");
    ASSERT_TRUE(bins[15] > 9.0f,  "bin 15 mostly free at cone edge");
}

// =========================================================================
// 11 · Engine convention sanity: look_direction = 0 means +Y, not +X.
// (docs/ARCHITECTURE.md axis section pins this; if anyone "fixes" it to +X, this
// test fails.)
// =========================================================================
void test_yaw_zero_points_north() {
    // Wall at +Y (10 m north of viewer). Cone faces look=0 → if the
    // convention is +Y, bin 32 hits the wall; if it's +X, bin 32
    // points east and doesn't hit anything.
    OccluderSegment seg{-5.0f, 10.0f, 5.0f, 10.0f, kThick};
    float bins[64];
    compute_vision_cone_occlusion(&seg, 1, 0.0f, 0.0f,
                                  /*look=*/0.0f,
                                  /*half_fov=*/kHalfPi,
                                  60.0f, bins, 64);
    ASSERT_NEAR(bins[32], 10.0f - kThick, 0.10f, "look=0 must point at +Y (engine convention)");
}

// =========================================================================
// 12 · Range scales the output bin distances proportionally.
// =========================================================================
void test_range_scales_no_hit_distances() {
    float bins10[64], bins100[64];
    compute_vision_cone_occlusion(nullptr, 0, 0.0f, 0.0f,
                                  0.0f, kHalfPi,
                                  /*range=*/10.0f,  bins10,  64);
    compute_vision_cone_occlusion(nullptr, 0, 0.0f, 0.0f,
                                  0.0f, kHalfPi,
                                  /*range=*/100.0f, bins100, 64);
    ASSERT_NEAR(bins10[0],  10.0f,  1e-4f, "range=10 fills with 10");
    ASSERT_NEAR(bins100[0], 100.0f, 1e-4f, "range=100 fills with 100");
}

int main() {
    std::cout << "=== Logosphere — Vision Cone LOS Occlusion ===" << std::endl;
    TEST(empty_segment_list_fills_range);
    TEST(perpendicular_wall_blocks_center_bin);
    TEST(perpendicular_wall_misses_extreme_bins);
    TEST(wall_behind_viewer_does_not_block);
    TEST(wall_past_range_ignored);
    TEST(two_walls_nearest_wins);
    TEST(diagonal_wall_occludes_one_side);
    TEST(wall_outside_cone_arc_ignored);
    TEST(wall_through_viewer_is_not_zero_distance);
    TEST(narrow_cone_distributes_bins_over_arc);
    TEST(yaw_zero_points_north);
    TEST(range_scales_no_hit_distances);
    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
