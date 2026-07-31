// Engine tests for the vision-memory grid (companion to the cone
// post-process). Pure CPU math — no Metal, no KG.

#include "logosphere/rendering/vision_memory.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

using logosphere::rendering::VisionMemoryConfig;
using logosphere::rendering::update_vision_memory;

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

// Standard 64×64 grid covering [-32, +32] in both axes (1 m / cell).
// Easy mental model: grid index (32, 32) maps to world (~0, ~0).
static VisionMemoryConfig std_cfg(float decay_seconds = 2.0f) {
    return VisionMemoryConfig{
        /*width=*/64, /*height=*/64,
        /*origin_x=*/-32.0f, /*origin_y=*/-32.0f,
        /*cell_size=*/1.0f,
        /*decay_seconds=*/decay_seconds,
    };
}

static int idx(const VisionMemoryConfig& cfg, int i, int j) {
    return j * cfg.width + i;
}

// =========================================================================
// 1 · Empty buffer + no viewer → all cells stay 0.
// =========================================================================
void test_empty_buffer_with_zero_dt_stays_zero() {
    auto cfg = std_cfg();
    std::vector<float> mem(cfg.width * cfg.height, 0.0f);
    update_vision_memory(mem.data(), cfg,
                         /*viewer_x=*/0.0f, /*viewer_y=*/0.0f,
                         /*look=*/0.0f, /*half_fov=*/kHalfPi,
                         /*range=*/0.0f,           // disable mark step
                         /*occl=*/nullptr, /*bin_count=*/0,
                         /*dt=*/0.0f);
    for (float v : mem) ASSERT_NEAR(v, 0.0f, 1e-6f, "empty cell stays 0");
}

// =========================================================================
// 2 · Decay step alone — every cell drops by dt/decay_seconds.
// =========================================================================
void test_decay_step_decrements_every_cell() {
    auto cfg = std_cfg(/*decay_seconds=*/2.0f);
    std::vector<float> mem(cfg.width * cfg.height, 1.0f);
    // dt = 0.5 → decay = 0.25.  range = 0 disables the mark step
    // (cells beyond range are skipped).
    update_vision_memory(mem.data(), cfg, 0.0f, 0.0f,
                         0.0f, kHalfPi, /*range=*/0.0f,
                         nullptr, 0, /*dt=*/0.5f);
    for (float v : mem) ASSERT_NEAR(v, 0.75f, 1e-5f, "linear decay");
}

// =========================================================================
// 3 · Cells decay floored at 0 (never negative).
// =========================================================================
void test_decay_floors_at_zero() {
    auto cfg = std_cfg(/*decay_seconds=*/1.0f);
    std::vector<float> mem(cfg.width * cfg.height, 0.1f);
    // dt = 1.0 → decay = 1.0; cell would go to -0.9, must clamp.
    update_vision_memory(mem.data(), cfg, 0.0f, 0.0f,
                         0.0f, kHalfPi, 0.0f, nullptr, 0, /*dt=*/1.0f);
    for (float v : mem) ASSERT_NEAR(v, 0.0f, 1e-6f, "floored at 0");
}

// =========================================================================
// 4 · Cell directly in front of viewer gets marked to 1.0.
// =========================================================================
void test_cell_in_cone_is_marked() {
    auto cfg = std_cfg();
    std::vector<float> mem(cfg.width * cfg.height, 0.0f);
    // Viewer at origin, looking +Y, 180° cone, range 10. The cell
    // at world (~0, ~5) should be marked.
    update_vision_memory(mem.data(), cfg, 0.0f, 0.0f,
                         /*look=*/0.0f, /*half_fov=*/kHalfPi,
                         /*range=*/10.0f, nullptr, 0, /*dt=*/0.0f);
    // Cell whose center is at world (0.5, 5.5) ≈ (i=32, j=37).
    ASSERT_NEAR(mem[idx(cfg, 32, 37)], 1.0f, 1e-6f,
                "cell directly in front marked");
}

// =========================================================================
// 5 · Cell BEHIND the viewer (outside the 180° cone) is not marked.
// =========================================================================
void test_cell_behind_viewer_not_marked() {
    auto cfg = std_cfg();
    std::vector<float> mem(cfg.width * cfg.height, 0.0f);
    update_vision_memory(mem.data(), cfg, 0.0f, 0.0f,
                         0.0f, kHalfPi, 10.0f, nullptr, 0, 0.0f);
    // Cell at (0.5, -5.5) — directly behind viewer (look=0 → +Y).
    ASSERT_NEAR(mem[idx(cfg, 32, 26)], 0.0f, 1e-6f,
                "behind-viewer cell stays 0");
}

// =========================================================================
// 6 · Cell beyond `range` is not marked.
// =========================================================================
void test_cell_beyond_range_not_marked() {
    auto cfg = std_cfg();
    std::vector<float> mem(cfg.width * cfg.height, 0.0f);
    // Range 5 m, ask about cell at (0.5, 20.5) — way beyond.
    update_vision_memory(mem.data(), cfg, 0.0f, 0.0f,
                         0.0f, kHalfPi, /*range=*/5.0f,
                         nullptr, 0, 0.0f);
    ASSERT_NEAR(mem[idx(cfg, 32, 52)], 0.0f, 1e-6f,
                "out-of-range cell stays 0");
}

// =========================================================================
// 7 · Already-lit cells decay; freshly-marked cells reset to 1.
// =========================================================================
void test_decay_then_mark_overrides() {
    auto cfg = std_cfg(/*decay_seconds=*/1.0f);
    std::vector<float> mem(cfg.width * cfg.height, 0.5f);
    update_vision_memory(mem.data(), cfg, 0.0f, 0.0f,
                         0.0f, kHalfPi, 10.0f, nullptr, 0,
                         /*dt=*/0.5f);
    // Cell in front: should be marked back to 1 (overrides decay).
    ASSERT_NEAR(mem[idx(cfg, 32, 37)], 1.0f, 1e-6f,
                "in-cone cell snapped to 1");
    // Cell behind: just decays.  0.5 - 0.5 = 0.
    ASSERT_NEAR(mem[idx(cfg, 32, 26)], 0.0f, 1e-6f,
                "behind-viewer cell decays");
}

// =========================================================================
// 8 · Occlusion mask blocks marking — cells past an occluder bin's
// distance must NOT be marked even if they're inside the cone arc.
// =========================================================================
void test_occlusion_mask_blocks_marking() {
    auto cfg = std_cfg();
    std::vector<float> mem(cfg.width * cfg.height, 0.0f);
    // 4 bins across 180°, all set to 3 m. Anything past 3 m in any
    // bin is occluded.
    float occl[4] = {3.0f, 3.0f, 3.0f, 3.0f};
    update_vision_memory(mem.data(), cfg, 0.0f, 0.0f,
                         0.0f, kHalfPi, /*range=*/10.0f,
                         occl, 4, 0.0f);
    // Cell at (0.5, 5.5): in cone, distance ~5.5 — past occluder
    // distance 3 → must NOT be marked.
    ASSERT_NEAR(mem[idx(cfg, 32, 37)], 0.0f, 1e-6f,
                "cell past occluder not marked");
    // Cell at (0.5, 1.5): inside the 3 m occluder distance → marked.
    ASSERT_NEAR(mem[idx(cfg, 32, 33)], 1.0f, 1e-6f,
                "cell before occluder marked");
}

// =========================================================================
// 9 · World→cell index sanity. Cell (0, 0)'s center is at
// (origin_x + 0.5*cell_size, origin_y + 0.5*cell_size).
// =========================================================================
void test_origin_cell_geometry() {
    // 4×4 grid, origin (0, 0), cell_size 1. Cell (0, 0) center is
    // at (0.5, 0.5). Place viewer there and look +Y with a wide
    // cone. That cell snaps to 1.
    VisionMemoryConfig cfg{4, 4, 0.0f, 0.0f, 1.0f, 1.0f};
    std::vector<float> mem(16, 0.0f);
    update_vision_memory(mem.data(), cfg,
                         /*viewer=*/0.5f, 0.5f,
                         0.0f, kHalfPi, 5.0f, nullptr, 0, 0.0f);
    ASSERT_NEAR(mem[0], 1.0f, 1e-6f, "(0.5, 0.5) lands in cell (0, 0)");
}

// =========================================================================
// 10 · Null pointer + zero size are no-ops (don't crash).
// =========================================================================
void test_null_inputs_are_noop() {
    VisionMemoryConfig cfg{0, 0, 0.0f, 0.0f, 1.0f, 1.0f};
    update_vision_memory(nullptr, cfg, 0.0f, 0.0f,
                         0.0f, kHalfPi, 10.0f, nullptr, 0, 1.0f);
    // No assertion — surviving the call is the test.

    VisionMemoryConfig real_cfg = std_cfg();
    update_vision_memory(nullptr, real_cfg, 0.0f, 0.0f,
                         0.0f, kHalfPi, 10.0f, nullptr, 0, 1.0f);
}

int main() {
    std::cout << "=== Logosphere — Vision Memory Grid ===" << std::endl;
    TEST(empty_buffer_with_zero_dt_stays_zero);
    TEST(decay_step_decrements_every_cell);
    TEST(decay_floors_at_zero);
    TEST(cell_in_cone_is_marked);
    TEST(cell_behind_viewer_not_marked);
    TEST(cell_beyond_range_not_marked);
    TEST(decay_then_mark_overrides);
    TEST(occlusion_mask_blocks_marking);
    TEST(origin_cell_geometry);
    TEST(null_inputs_are_noop);
    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
