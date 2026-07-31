// Lock down the depth-sorting contract for the isometric projection.
//
// The isometric projection is ORTHOGRAPHIC — parallel rays along the
// SW-above view direction. Under orthographic projection, "depth" is
// the distance along the view direction, not the 3D Euclidean distance
// to a finite camera point. For iso, this means: given two points at
// the same screen coordinates, the one with HIGHER world Z is
// unambiguously in FRONT (closer to the viewer), regardless of how
// far the camera happens to sit in X / Y / Z.
//
// The engine currently violates this. `SurfaceRasterizer::calculate_pixel_depth`
// uses `distance_3d(vertex - camera)` — an Euclidean metric that is
// correct for perspective but wrong for every parallel projection.
// When a game (bike_viewer, logotron) places the iso camera near the
// scene plane (cam_z ≈ bike_z, a deliberate choice for screen
// centering), any particle raised above the ground is reported as
// FARTHER from the camera than the particle directly below it — so a
// bike body ends up occluding a marker floating 20 cm above its top.
//
// This test pins the contract so the bug cannot regress silently once
// fixed.
//
// Headless: pure math against IsometricProjection + distance_3d.
// No rendering, no ParticleSystem, no engine state.

#include "projection_system.h"
#include "logosphere/rendering/rasterization_math.h"

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

// The depth metric the engine uses for the iso projection. After the
// fix, this routes through ProjectionSystem::compute_depth — the same
// method the rasterizer calls via CameraSystem::compute_depth. Test-to-
// prod fidelity: the projection object here is the same class the
// engine instantiates in CameraSystem's constructor.
static IsometricProjection g_iso;
static float engine_pixel_depth(float world_x, float world_y, float world_z,
                                float cam_x,   float cam_y,   float cam_z) {
    return g_iso.compute_depth(world_x, world_y, world_z, cam_x, cam_y, cam_z);
}

// Keep the old Euclidean metric around so we can report what the
// legacy code WOULD have said — useful for the failure message and
// documents the bug class we're closing.
using RasterizationMath::distance_3d;
static float legacy_euclidean_depth(float world_x, float world_y, float world_z,
                                    float cam_x,   float cam_y,   float cam_z) {
    return distance_3d(world_x - cam_x, world_y - cam_y, world_z - cam_z);
}

static constexpr int   kW   = 1600;
static constexpr int   kH   = 1200;
static constexpr float kPPU = 60.0f;

// Scenario A — bike_viewer's exact configuration. Camera at scene
// level (0, 0, 0.42). Two points roughly overlapping on screen,
// separated only in world Z. The raised one MUST be drawn in front.
// Expected numeric values come from direct evaluation of the current
// engine code so the regression guard cannot drift.
void test_raised_marker_wins_depth_at_scene_level_camera() {
    // Camera as bike_viewer positions it (see bike_viewer.cpp:176).
    const float cam_x = 0.0f, cam_y = 0.0f, cam_z = 0.42f;

    // Body top near the front tip at yaw = -3π/4 (what image #13 shows).
    const float body_x = -0.60f, body_y = -0.60f, body_z = 0.47f;

    // Nose marker 20 cm above the body top at the same XY (approx).
    // Should be visually IN FRONT of the body in iso.
    const float nose_x = -0.60f, nose_y = -0.60f, nose_z = 0.78f;

    // First: sanity-check they overlap on screen under iso. If they
    // don't overlap, the depth bug cannot manifest and this test
    // premise is wrong. iso_x depends only on (view_x - view_y) and
    // those are identical for both points, so sx must match exactly.
    IsometricProjection proj;
    int body_sx = 0, body_sy = 0, nose_sx = 0, nose_sy = 0;
    proj.project(body_x, body_y, body_z, cam_x, cam_y, cam_z,
                 kW, kH, kPPU, body_sx, body_sy);
    proj.project(nose_x, nose_y, nose_z, cam_x, cam_y, cam_z,
                 kW, kH, kPPU, nose_sx, nose_sy);
    if (body_sx != nose_sx) {
        throw std::runtime_error(
            "premise broken: body and nose do not share screen_x under iso "
            "(body_sx=" + std::to_string(body_sx) +
            " nose_sx="  + std::to_string(nose_sx) + ")");
    }
    // The Z difference (0.31 world units) projects to a vertical screen
    // offset of 0.31 * kPPU ≈ 18 px. They overlap in the bounding-box
    // sense, which is all the rasterizer needs to pit them against each
    // other in the depth buffer.
    int dy = std::abs(body_sy - nose_sy);
    if (dy > 30) {
        throw std::runtime_error(
            "premise broken: body and nose are farther apart vertically than "
            "expected (dy=" + std::to_string(dy) + ")");
    }

    // The actual contract — engine depth must put nose in front of body.
    // For an orthographic iso view, higher world_z at same XY == closer.
    const float body_depth = engine_pixel_depth(body_x, body_y, body_z,
                                                cam_x, cam_y, cam_z);
    const float nose_depth = engine_pixel_depth(nose_x, nose_y, nose_z,
                                                cam_x, cam_y, cam_z);

    const float body_legacy = legacy_euclidean_depth(body_x, body_y, body_z,
                                                     cam_x, cam_y, cam_z);
    const float nose_legacy = legacy_euclidean_depth(nose_x, nose_y, nose_z,
                                                     cam_x, cam_y, cam_z);
    std::cout << "[engine-depth]  body d=" << body_depth
              << " (legacy_euclid=" << body_legacy << ")"
              << "  nose d=" << nose_depth
              << " (legacy_euclid=" << nose_legacy << ")"
              << "   "
              << (nose_depth < body_depth
                    ? "nose IN FRONT (correct)"
                    : "nose BEHIND body (BUG)")
              << std::endl;

    if (!(nose_depth < body_depth)) {
        throw std::runtime_error(
            "iso depth ordering broken: raised marker (z=0.78) should render "
            "in front of lower body (z=0.47) at same screen XY, but iso "
            "compute_depth reports nose="   + std::to_string(nose_depth) +
            " >= body=" + std::to_string(body_depth) +
            " (legacy Euclidean would have given nose=" + std::to_string(nose_legacy) +
            " body=" + std::to_string(body_legacy) +
            "). For orthographic projection, depth must be measured along "
            "the view direction, not as radial distance to a finite camera.");
    }
}

// Scenario B — the metric should be monotone in world Z at fixed XY
// for ANY camera sitting near the scene plane. This is the generalized
// version of scenario A; it states the invariant the engine must hold.
void test_depth_monotone_in_world_z_at_fixed_xy() {
    const float cam_x = 0.0f, cam_y = 0.0f, cam_z = 0.5f;

    // Fixed XY, sweep Z from 0.0 up to 2.0. Higher Z must give smaller
    // depth (closer) for every step.
    const float x = -0.6f, y = -0.6f;
    float prev_depth = engine_pixel_depth(x, y, 0.0f, cam_x, cam_y, cam_z);
    int violations = 0;
    float worst_z = 0.0f;
    for (float z = 0.05f; z <= 2.0f; z += 0.05f) {
        const float d = engine_pixel_depth(x, y, z, cam_x, cam_y, cam_z);
        if (!(d < prev_depth)) {
            if (violations == 0) worst_z = z;
            violations++;
        }
        prev_depth = d;
    }
    if (violations > 0) {
        throw std::runtime_error(
            std::to_string(violations) + " non-monotone Z steps under iso depth. "
            "First violation at z=" + std::to_string(worst_z) +
            ". Expected: at fixed XY, depth strictly decreases with increasing "
            "world Z (higher == closer to iso viewer).");
    }
}

// Scenario C — same scenario, but with the camera placed at the TRUE
// iso default position (-10, -10, 20). Here the Euclidean distance
// happens to produce the right ordering, because both points lie
// below-and-behind the camera along every axis. This test is the
// "green" reference — it should PASS today and continue to pass after
// the fix. Documents that the bug is config-specific.
void test_depth_ordering_correct_with_iso_default_camera() {
    const float cam_x = -10.0f, cam_y = -10.0f, cam_z = 20.0f;
    const float body_x = -0.60f, body_y = -0.60f, body_z = 0.47f;
    const float nose_x = -0.60f, nose_y = -0.60f, nose_z = 0.78f;
    const float body_depth = engine_pixel_depth(body_x, body_y, body_z,
                                                cam_x, cam_y, cam_z);
    const float nose_depth = engine_pixel_depth(nose_x, nose_y, nose_z,
                                                cam_x, cam_y, cam_z);
    if (!(nose_depth < body_depth)) {
        throw std::runtime_error(
            "even with iso default camera, raised marker is not closer. "
            "nose_depth=" + std::to_string(nose_depth) +
            " body_depth=" + std::to_string(body_depth));
    }
}

// Scenario D — the metric must be the SAME function of world position
// regardless of screen centering. Iso is orthographic: moving the
// camera in its own XY plane shifts the scene on screen but does NOT
// change which of two world points is "in front". Today this property
// is violated — that's the heart of the bug.
void test_depth_ordering_invariant_under_camera_xy_shift() {
    const float body_x = -0.60f, body_y = -0.60f, body_z = 0.47f;
    const float nose_x = -0.60f, nose_y = -0.60f, nose_z = 0.78f;

    // Two camera positions, identical Z, differing in X/Y only.
    const float cam_z = 0.42f;
    const float camA_x = 0.0f,  camA_y = 0.0f;
    const float camB_x = 3.0f,  camB_y = -2.0f;

    const float body_dA = engine_pixel_depth(body_x, body_y, body_z, camA_x, camA_y, cam_z);
    const float nose_dA = engine_pixel_depth(nose_x, nose_y, nose_z, camA_x, camA_y, cam_z);
    const float body_dB = engine_pixel_depth(body_x, body_y, body_z, camB_x, camB_y, cam_z);
    const float nose_dB = engine_pixel_depth(nose_x, nose_y, nose_z, camB_x, camB_y, cam_z);

    const bool nose_front_A = (nose_dA < body_dA);
    const bool nose_front_B = (nose_dB < body_dB);

    // Under correct iso semantics, both should be TRUE (nose is always
    // in front, independent of camera pan). Under the current bug they
    // can both be FALSE, or one TRUE one FALSE, depending on pan.
    if (!(nose_front_A && nose_front_B)) {
        throw std::runtime_error(
            "iso depth ordering depends on camera pan. camA: nose_in_front=" +
            std::string(nose_front_A ? "yes" : "no") +
            ", camB: nose_in_front=" +
            std::string(nose_front_B ? "yes" : "no") +
            ". In a parallel projection, depth ordering must be independent "
            "of camera XY.");
    }
}

int main() {
    std::cout << "=== Iso depth ordering (contract for raised particles) ===" << std::endl;
    TEST(raised_marker_wins_depth_at_scene_level_camera);
    TEST(depth_monotone_in_world_z_at_fixed_xy);
    TEST(depth_ordering_correct_with_iso_default_camera);
    TEST(depth_ordering_invariant_under_camera_xy_shift);
    std::cout << std::endl << tests_passed << " passed, "
              << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
