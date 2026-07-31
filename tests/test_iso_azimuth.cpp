// Isometric azimuth orbit — projection invariants (issue #9).
//
// The orbit is a pre-rotation of view-space XY around world +Z,
// CW-positive from above (compass convention, same sign as
// Particle::rotation_z). These tests lock:
//   1. azimuth 0 is bit-identical to the classic fixed view
//   2. rotation equivalence: projecting at azimuth a equals
//      projecting the world rotated by -a at azimuth 0 (both around
//      the camera axis)
//   3. depth obeys the same equivalence, so occlusion order under
//      orbit is exactly the rotated scene's order
//   4. a quarter turn maps compass directions the way CW says:
//      at a = +90 deg, world north projects where east used to
//   5. parallel-lines preservation survives any azimuth
//
// Usage:
//   ./build/test_iso_azimuth

#include "../src/projection_system.h"
#include <cmath>
#include <iostream>
#include <string>

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg)                                                \
    do {                                                                \
        if (cond) {                                                     \
            tests_passed++;                                             \
        } else {                                                        \
            tests_failed++;                                             \
            std::cout << "FAIL: " << msg << std::endl;                  \
        }                                                               \
    } while (0)

namespace {

constexpr int   VW = 1280, VH = 960;
constexpr float PPU = 32.0f;
constexpr float CAM_X = -10.0f, CAM_Y = -10.0f, CAM_Z = 20.0f;

struct Pt { float x, y, z; };

const Pt SAMPLES[] = {
    {0.0f, 0.0f, 0.0f},   {3.0f, 0.0f, 0.0f},  {0.0f, 3.0f, 0.0f},
    {-2.0f, 5.0f, 1.0f},  {7.5f, -4.25f, 3.0f}, {-6.0f, -6.0f, 0.5f},
    {12.0f, 9.0f, 8.0f},
};

// Rotate a world point around the camera XY by angle CW (matches the
// projection's convention: rotate_into_view with +a).
Pt rotate_about_camera_cw(const Pt& p, float a) {
    float dx = p.x - CAM_X, dy = p.y - CAM_Y;
    float rx = dx * std::cos(a) + dy * std::sin(a);
    float ry = -dx * std::sin(a) + dy * std::cos(a);
    return {CAM_X + rx, CAM_Y + ry, p.z};
}

void project(const IsometricProjection& proj, const Pt& p,
             int& sx, int& sy) {
    proj.project(p.x, p.y, p.z, CAM_X, CAM_Y, CAM_Z, VW, VH, PPU, sx, sy);
}

void test_azimuth_zero_is_identity() {
    IsometricProjection classic;         // never touched: the old view
    IsometricProjection orbiter;
    orbiter.set_view_azimuth(1.234f);
    orbiter.set_view_azimuth(0.0f);      // returning to 0 must restore
    for (const Pt& p : SAMPLES) {
        int cx, cy, ox, oy;
        project(classic, p, cx, cy);
        project(orbiter, p, ox, oy);
        CHECK(cx == ox && cy == oy,
              "azimuth 0 identical to classic at (" +
              std::to_string(p.x) + "," + std::to_string(p.y) + "," +
              std::to_string(p.z) + "): classic (" + std::to_string(cx) +
              "," + std::to_string(cy) + ") vs orbit (" +
              std::to_string(ox) + "," + std::to_string(oy) + ")");
        float cd = classic.compute_depth(p.x, p.y, p.z, CAM_X, CAM_Y, CAM_Z);
        float od = orbiter.compute_depth(p.x, p.y, p.z, CAM_X, CAM_Y, CAM_Z);
        CHECK(cd == od, "azimuth 0 depth identical");
    }
}

void test_rotation_equivalence() {
    const float angles[] = {0.3f, -0.9f, 1.5707963f, 2.7f, -3.0f};
    for (float a : angles) {
        IsometricProjection orbiter, classic;
        orbiter.set_view_azimuth(a);
        for (const Pt& p : SAMPLES) {
            int ox, oy, cx, cy;
            project(orbiter, p, ox, oy);
            Pt q = rotate_about_camera_cw(p, a);
            project(classic, q, cx, cy);
            // Integer screen coords: allow 1 px for rounding of the
            // two float paths.
            CHECK(std::abs(ox - cx) <= 1 && std::abs(oy - cy) <= 1,
                  "orbit(a=" + std::to_string(a) + ") == classic(rotated) at (" +
                  std::to_string(p.x) + "," + std::to_string(p.y) + "): (" +
                  std::to_string(ox) + "," + std::to_string(oy) + ") vs (" +
                  std::to_string(cx) + "," + std::to_string(cy) + ")");
            float od = orbiter.compute_depth(p.x, p.y, p.z,
                                             CAM_X, CAM_Y, CAM_Z);
            float cd = classic.compute_depth(q.x, q.y, q.z,
                                             CAM_X, CAM_Y, CAM_Z);
            CHECK(std::fabs(od - cd) < 1e-4f,
                  "depth equivalence at a=" + std::to_string(a) +
                  " (" + std::to_string(od) + " vs " + std::to_string(cd) + ")");
        }
    }
}

void test_quarter_turn_compass() {
    // At azimuth +90 deg (CW quarter turn), the view frame's x' picks
    // up world y: a point north of the camera lands where a point
    // east of it lands in the classic view.
    IsometricProjection orbiter, classic;
    orbiter.set_view_azimuth(1.5707963f);
    Pt north = {CAM_X + 0.0f, CAM_Y + 5.0f, 0.0f};
    Pt east  = {CAM_X + 5.0f, CAM_Y + 0.0f, 0.0f};
    int nx, ny, ex, ey;
    project(orbiter, north, nx, ny);
    project(classic, east, ex, ey);
    CHECK(std::abs(nx - ex) <= 1 && std::abs(ny - ey) <= 1,
          "quarter turn CW: north projects onto classic east (north (" +
          std::to_string(nx) + "," + std::to_string(ny) + ") vs east (" +
          std::to_string(ex) + "," + std::to_string(ey) + "))");
}

void test_parallel_lines_preserved() {
    // Two parallel world segments stay parallel on screen at any
    // azimuth (the projection stays linear).
    const float angles[] = {0.7f, 2.1f, -1.3f};
    for (float a : angles) {
        IsometricProjection proj;
        proj.set_view_azimuth(a);
        Pt a0 = {0, 0, 0}, a1 = {4, 2, 0};
        Pt b0 = {-3, 5, 2}, b1 = {1, 7, 2};   // same direction (4,2,0)
        int a0x, a0y, a1x, a1y, b0x, b0y, b1x, b1y;
        project(proj, a0, a0x, a0y); project(proj, a1, a1x, a1y);
        project(proj, b0, b0x, b0y); project(proj, b1, b1x, b1y);
        float cross = static_cast<float>(a1x - a0x) * (b1y - b0y) -
                      static_cast<float>(a1y - a0y) * (b1x - b0x);
        float scale = std::fabs(static_cast<float>(a1x - a0x)) +
                      std::fabs(static_cast<float>(a1y - a0y));
        CHECK(std::fabs(cross) <= 2.0f * scale,
              "parallel lines stay parallel at a=" + std::to_string(a) +
              " (cross " + std::to_string(cross) + ")");
    }
}

void test_depth_ordering_under_orbit() {
    // A column of boxes along the classic view axis: front/back order
    // must swap after a half turn, exactly as the rotated scene says.
    IsometricProjection proj;
    Pt near_pt = {-2.0f, -2.0f, 0.0f};   // toward the classic camera
    Pt far_pt  = {2.0f, 2.0f, 0.0f};     // away from it
    float d_near0 = proj.compute_depth(near_pt.x, near_pt.y, near_pt.z,
                                       CAM_X, CAM_Y, CAM_Z);
    float d_far0 = proj.compute_depth(far_pt.x, far_pt.y, far_pt.z,
                                      CAM_X, CAM_Y, CAM_Z);
    CHECK(d_near0 < d_far0, "classic: SW point is in front");
    proj.set_view_azimuth(3.1415926f);
    float d_near_pi = proj.compute_depth(near_pt.x, near_pt.y, near_pt.z,
                                         CAM_X, CAM_Y, CAM_Z);
    float d_far_pi = proj.compute_depth(far_pt.x, far_pt.y, far_pt.z,
                                        CAM_X, CAM_Y, CAM_Z);
    CHECK(d_far_pi < d_near_pi,
          "half turn: former back point is now in front (" +
          std::to_string(d_far_pi) + " vs " + std::to_string(d_near_pi) + ")");
}

}  // namespace

int main() {
    std::cout << "Isometric azimuth orbit invariants" << std::endl;
    test_azimuth_zero_is_identity();
    test_rotation_equivalence();
    test_quarter_turn_compass();
    test_parallel_lines_preserved();
    test_depth_ordering_under_orbit();
    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
