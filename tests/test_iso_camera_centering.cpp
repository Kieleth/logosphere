// Lock down the isometric projection's screen-centering contract.
//
// The engine uses a 2.5D isometric projection where the screen center
// maps directly to the camera POSITION in world space (not the look_at
// target). Forgetting this — e.g., treating look_at as "this point ends
// up at the center of the screen" — drops the subject into a screen
// corner. This test catches that regression.
//
// Headless: pure math against ProjectionSystem, no rendering stack.

#include "projection_system.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    std::cout << "  " #name "... "; \
    try { test_##name(); tests_passed++; std::cout << "PASS" << std::endl; } \
    catch (const std::exception& e) { tests_failed++; std::cout << "FAIL: " << e.what() << std::endl; }

#define ASSERT_NEAR(a, b, tol, msg) \
    if (std::abs((a) - (b)) > (tol)) throw std::runtime_error( \
        std::string(msg) + " [got=" + std::to_string(a) + " want=" + std::to_string(b) + "]")

static constexpr int kW = 1600;
static constexpr int kH = 1200;
static constexpr float kPPU = 20.0f;  // pixels per world unit

void test_camera_position_projects_to_screen_center() {
    IsometricProjection proj;
    int sx = 0, sy = 0;
    // World point == camera position. view = (0,0,0). Must land at screen center.
    proj.project(5.0f, 7.0f, 0.55f,   // world
                 5.0f, 7.0f, 0.55f,   // camera
                 kW, kH, kPPU, sx, sy);
    ASSERT_NEAR(sx, kW / 2, 1, "camera-position projects to horizontal screen center");
    ASSERT_NEAR(sy, kH / 2, 1, "camera-position projects to vertical screen center");
}

void test_look_at_target_does_not_center_when_camera_is_elsewhere() {
    // Regression guard. A tempting-but-wrong mental model is
    // "camera.look_at(p) => p is at screen center". For the isometric
    // projection that's FALSE. If the camera eye is far from the
    // look_at target, the look_at target is NOT at screen center.
    IsometricProjection proj;
    int sx = 0, sy = 0;
    const float look_x = 0.0f, look_y = 0.0f, look_z = 0.55f;
    const float cam_x  = 0.0f, cam_y = -17.4f, cam_z = 18.5f;
    proj.project(look_x, look_y, look_z,
                 cam_x,  cam_y, cam_z,
                 kW, kH, kPPU, sx, sy);
    // The look_at point must land WELL off-center (the test author did
    // the math: view=(0,17.4,-17.9), iso_x≈-15.07, iso_y≈8.7-17.9).
    // Horizontal offset alone is about -15 * 20 = 300 px off center.
    int dx = std::abs(sx - kW / 2);
    int dy = std::abs(sy - kH / 2);
    if (dx < 100 && dy < 100) {
        throw std::runtime_error(
            "look_at target is near screen center — projection semantics changed? "
            "sx=" + std::to_string(sx) + " sy=" + std::to_string(sy));
    }
}

void test_north_bike_projects_above_camera_diagonal() {
    // With +Y = north in world coords, a bike to the north of the camera
    // should land in the upper half of the screen (lower sy) because
    // the iso projection maps +Y to the upper-left diagonal.
    IsometricProjection proj;
    int sx = 0, sy = 0;
    proj.project(0.0f, 5.0f, 0.55f,    // bike 5m north of camera
                 0.0f, 0.0f, 0.55f,    // camera
                 kW, kH, kPPU, sx, sy);
    if (sy >= kH / 2) throw std::runtime_error(
        "north-of-camera point should land above screen center (lower sy); got sy="
        + std::to_string(sy));
    if (sx >= kW / 2) throw std::runtime_error(
        "north-of-camera point should land left of screen center (iso +Y → upper-left); got sx="
        + std::to_string(sx));
}

void test_same_z_no_vertical_offset() {
    // If camera_z == world_z, view_z == 0 and iso_y contribution from z
    // vanishes. The screen position is driven purely by x/y.
    IsometricProjection proj;
    int sx_same = 0, sy_same = 0;
    int sx_diff = 0, sy_diff = 0;
    proj.project(3.0f, 0.0f, 0.55f, 0.0f, 0.0f, 0.55f, kW, kH, kPPU, sx_same, sy_same);
    proj.project(3.0f, 0.0f, 0.55f, 0.0f, 0.0f, 0.0f,  kW, kH, kPPU, sx_diff, sy_diff);
    if (sx_same != sx_diff) throw std::runtime_error(
        "x-offset should not depend on camera z");
    // With different camera_z, the z-term shifts iso_y (and thus sy).
    if (sy_same == sy_diff) throw std::runtime_error(
        "camera_z must shift vertical screen position when world_z differs");
}

int main() {
    std::cout << "=== Isometric camera centering ===" << std::endl;
    TEST(camera_position_projects_to_screen_center);
    TEST(look_at_target_does_not_center_when_camera_is_elsewhere);
    TEST(north_bike_projects_above_camera_diagonal);
    TEST(same_z_no_vertical_offset);
    std::cout << std::endl << tests_passed << " passed, "
              << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
