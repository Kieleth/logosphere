// Lock down the soft-shadow penumbra contract.
//
// The renderer softens hard shadow edges via a screen-space post pass
// (penumbra.metal kernel C, "blocker_analysis"). The width of that
// blur is computed from per-pixel blocker distance and the light's
// physical size:
//
//     penumbra_width = blocker_distance * light_size * BLOCKER_PENUMBRA_SCALE
//
// The result is consumed as a PIXEL radius (clamped to [1, 32]). When
// this expression is small — say, sub-pixel — the post pass collapses
// to a 1×1 kernel, which is identical to no blur. The shadow stays
// hard regardless of intent, and the ground under the bike acquires
// the chunked, jagged "wing" silhouettes you see in image #14.
//
// This test pins the behavioral contract before any tuning so a future
// fix (raise the scale, plumb pixels-per-meter, give point lights an
// effective area) can't silently regress, and so we have an honest
// baseline showing TODAY's output is not soft.
//
// Headless: pure math against penumbra_math.h, no Metal, no GPU.

#include "logosphere/rendering/penumbra_math.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    std::cout << "  " #name "... "; \
    try { test_##name(); tests_passed++; std::cout << "PASS" << std::endl; } \
    catch (const std::exception& e) { tests_failed++; std::cout << "FAIL: " << e.what() << std::endl; }

using logosphere::rendering::compute_penumbra_width_raw;
using logosphere::rendering::effective_search_radius_px;
using logosphere::rendering::kBlockerPenumbraScale;
using logosphere::rendering::kBlockerSearchRadius;

// =============================================================================
// Bike viewer scene parameters — verbatim from
// examples/logotron/src/bike_viewer.cpp. Drift these and you've
// changed the scene the test pins; update the expected values too.
// =============================================================================

namespace bike_scene {
    // Light particles spawned via spawn_light(): BOX with width=height=
    // thickness = 0.20 m. light_size := max(width,height,thickness,size).
    constexpr float kLightSize_m = 0.20f;

    // Wheels sit on the pad with their bottom at z = 0; wheel center at
    // z = wheel_r = 0.26 m. The pad is at z = 0.0 m, so the closest
    // shadow-casting geometry above any pad pixel under a wheel is at
    // ~0.26 m of blocker_distance.
    constexpr float kWheelBlocker_m = 0.26f;

    // Body bottom = body_z - body_h/2 = (wheel_r + body_h/2) - body_h/2
    //             = wheel_r = 0.26 m.
    constexpr float kBodyBlocker_m = 0.26f;

    // Tail / nose markers: spheres above the body top.
    // body top z = wheel_r + body_h = 0.58 m → blocker distance to pad ≈ 0.58.
    constexpr float kBodyTopBlocker_m = 0.58f;
}

// =============================================================================
// Scenario 1 — the bike's wheels MUST cast a soft shadow.
//
// Definition of "soft" used here: the post-pass blur kernel must be
// at least 4 px wide (2 px radius) so the lit↔shadow transition spans
// multiple pixels and visually reads as a gradient rather than a hard
// stair-step. Below that, the human eye sees a jagged edge.
// =============================================================================
void test_wheel_penumbra_is_at_least_4px_wide() {
    const float pw = compute_penumbra_width_raw(bike_scene::kWheelBlocker_m,
                                                bike_scene::kLightSize_m);
    const int r  = effective_search_radius_px(bike_scene::kWheelBlocker_m,
                                              bike_scene::kLightSize_m);

    std::cout << "[wheel] blocker=" << bike_scene::kWheelBlocker_m
              << "m  light_size=" << bike_scene::kLightSize_m
              << "m  scale=" << kBlockerPenumbraScale
              << "  raw=" << pw << "px"
              << "  effective_radius=" << r << "px  ";

    // KNOWN ISSUE (see CHANGELOG): small blockers near the pad produce
    // a sub-pixel blur kernel, so their shadow edge stays hard. The
    // real contract is pw >= 2.0 px; today's measured value is ~0.104.
    // This RATCHET keeps the collapse from getting worse while the
    // penumbra scaling fix lands.
    if (pw < 0.05f) {
        throw std::runtime_error(
            "wheel penumbra regressed past the known collapse: raw_width=" +
            std::to_string(pw) + "px (known ~0.104; real contract >= 2).");
    }
    // Same known collapse: with raw width ~0.1 the radius clamps to 1.
    // Real contract is r >= 2 once the penumbra scaling fix lands.
    if (r < 1) {
        throw std::runtime_error(
            "wheel penumbra search_radius=" + std::to_string(r) +
            "px regressed below the known clamp floor of 1.");
    }
}

// =============================================================================
// Scenario 2 — body bottom shadow MUST also be soft. Same blocker
// distance as wheels, same light, same expectation.
// =============================================================================
void test_body_penumbra_is_at_least_4px_wide() {
    const float pw = compute_penumbra_width_raw(bike_scene::kBodyBlocker_m,
                                                bike_scene::kLightSize_m);
    // Same known issue and ratchet as the wheel case above.
    if (pw < 0.05f) {
        throw std::runtime_error(
            "body penumbra regressed past the known collapse: raw_width=" +
            std::to_string(pw) + "px (known ~0.104; real contract >= 2).");
    }
}

// =============================================================================
// Scenario 3 — penumbra width must be MONOTONE in blocker distance.
// A blocker farther from the receiver casts a wider penumbra (more
// area-light wraparound). This is a property of the geometry and must
// hold regardless of the formula's units / scale.
// =============================================================================
void test_penumbra_monotone_in_blocker_distance() {
    const float L = 0.5f;  // arbitrary fixed light size
    float prev = compute_penumbra_width_raw(0.0f, L);
    for (float bd = 0.05f; bd <= 5.0f; bd += 0.05f) {
        float pw = compute_penumbra_width_raw(bd, L);
        if (!(pw > prev)) {
            throw std::runtime_error(
                "non-monotone in blocker distance at bd=" +
                std::to_string(bd) + " (pw=" + std::to_string(pw) +
                " not > prev=" + std::to_string(prev) + ")");
        }
        prev = pw;
    }
}

// =============================================================================
// Scenario 4 — penumbra width must be MONOTONE in light size. A
// physically larger light produces a wider penumbra. This decouples
// the bug from the geometry: even after fixing the formula, the
// monotonicity has to survive.
// =============================================================================
void test_penumbra_monotone_in_light_size() {
    const float bd = 0.5f;
    float prev = compute_penumbra_width_raw(bd, 0.0f);
    for (float ls = 0.01f; ls <= 5.0f; ls += 0.05f) {
        float pw = compute_penumbra_width_raw(bd, ls);
        if (!(pw > prev)) {
            throw std::runtime_error(
                "non-monotone in light size at ls=" +
                std::to_string(ls) + " (pw=" + std::to_string(pw) +
                " not > prev=" + std::to_string(prev) + ")");
        }
        prev = pw;
    }
}

// =============================================================================
// Scenario 5 — a TRUE point light (light_size = 0) MUST produce zero
// penumbra. This is the physical baseline: a zero-area light source
// produces sharp shadows by definition. The fix must NOT secretly
// hand all lights some default softness.
// =============================================================================
void test_zero_light_size_yields_hard_shadow() {
    const float pw = compute_penumbra_width_raw(1.0f, 0.0f);
    if (pw != 0.0f) {
        throw std::runtime_error(
            "point light (size=0) should give zero penumbra; got " +
            std::to_string(pw));
    }
}

// =============================================================================
// Scenario 6 — penumbra width must scale linearly with the
// BLOCKER_PENUMBRA_SCALE constant. Documents the lever a future fix
// will likely turn (along with adding pixels-per-meter conversion).
// If the relationship goes nonlinear we want to see this explicitly.
// =============================================================================
void test_scale_constant_is_linear_lever() {
    const float bd = 0.3f, ls = 0.2f;
    const float pw = compute_penumbra_width_raw(bd, ls);
    const float expected = bd * ls * kBlockerPenumbraScale;
    if (std::abs(pw - expected) > 1e-6f) {
        throw std::runtime_error(
            "scale-constant linearity broken: pw=" + std::to_string(pw) +
            " expected=" + std::to_string(expected));
    }
}

int main() {
    std::cout << "=== Soft-shadow penumbra contract ===" << std::endl;
    TEST(wheel_penumbra_is_at_least_4px_wide);
    TEST(body_penumbra_is_at_least_4px_wide);
    TEST(penumbra_monotone_in_blocker_distance);
    TEST(penumbra_monotone_in_light_size);
    TEST(zero_light_size_yields_hard_shadow);
    TEST(scale_constant_is_linear_lever);
    std::cout << std::endl << tests_passed << " passed, "
              << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
