// Azimuth orbit: forward/inverse round-trip through the real camera
// and CoordinateTransformer (issue #9). Mouse picking must keep
// working at every orbit angle: world -> screen (projection with
// azimuth pre-rotation) followed by screen -> world (inverse with the
// rotation undone) has to land back on the source point.
//
// Usage:
//   ./build/test_iso_azimuth_roundtrip

#include "../src/core/camera_system.h"
#include "../src/projection_system.h"
#include "logosphere/rendering/coordinate_transformer.h"
#include "../src/core/projection_mode.h"

#include <cmath>
#include <iostream>
#include <string>

static int tests_passed = 0;
static int tests_failed = 0;

int main() {
    std::cout << "Isometric azimuth round-trip (camera + transformer)"
              << std::endl;

    constexpr int VW = 1280, VH = 960;
    constexpr float PPU = 32.0f;

    CameraSystem camera;
    camera.set_viewport(VW, VH);
    camera.set_projection_system(
        ProjectionFactory::create(ProjectionFactory::Type::Isometric));
    camera.set_pixels_per_unit(PPU);
    camera.set_position(-10.0f, -10.0f, 20.0f);

    CoordinateTransformer transformer;
    transformer.set_viewport(VW, VH);
    transformer.set_camera_system(&camera);
    transformer.set_pixels_per_unit(PPU);

    const float azimuths[] = {0.0f, 0.7f, 1.5707963f, 2.4f, -1.1f,
                              3.1415926f};
    struct Pt { float x, y, z; };
    const Pt samples[] = {
        {0.0f, 0.0f, 0.0f},  {5.0f, -3.0f, 0.0f}, {-4.0f, 6.0f, 1.5f},
        {8.0f, 8.0f, 3.0f},  {-7.0f, -2.0f, 0.5f},
    };

    // Two integer screen coords round-tripped through the inverse can
    // be off by up to ~1.5 px of world distance; 3 px of slack at
    // PPU 32 is ~0.1 m.
    const float TOL = 3.0f / PPU;

    for (float a : azimuths) {
        camera.set_view_azimuth(a);
        for (const Pt& p : samples) {
            int sx, sy;
            camera.world_to_screen(p.x, p.y, p.z, sx, sy);
            float wx, wy;
            transformer.screen_to_world_at_z(sx, sy, p.z, wx, wy);
            float err = std::sqrt((wx - p.x) * (wx - p.x) +
                                  (wy - p.y) * (wy - p.y));
            if (err <= TOL) {
                tests_passed++;
            } else {
                tests_failed++;
                std::cout << "FAIL: a=" << a << " (" << p.x << "," << p.y
                          << "," << p.z << ") -> screen(" << sx << ","
                          << sy << ") -> (" << wx << "," << wy
                          << ") err=" << err << " m (tol "
                          << TOL << ")" << std::endl;
            }
        }
    }

    // The ground-plane variant (mouse picking) must agree too.
    camera.set_view_azimuth(1.9f);
    for (const Pt& p : samples) {
        if (p.z != 0.0f) continue;
        int sx, sy;
        camera.world_to_screen(p.x, p.y, 0.0f, sx, sy);
        float wx, wy;
        transformer.screen_to_world_isometric(sx, sy, wx, wy);
        float err = std::sqrt((wx - p.x) * (wx - p.x) +
                              (wy - p.y) * (wy - p.y));
        if (err <= TOL) {
            tests_passed++;
        } else {
            tests_failed++;
            std::cout << "FAIL: picking inverse at a=1.9 for (" << p.x
                      << "," << p.y << ") err=" << err << " m"
                      << std::endl;
        }
    }

    // ---------------------------------------------------------------
    // Free movement (SPACE + arrows): the pan basis must be a SCREEN
    // basis, not a world one. Pushing along screen-right has to slide
    // the view horizontally and leave the vertical screen coordinate
    // untouched, at every orbit bearing - otherwise flying the camera
    // drifts diagonally the moment you have orbited.
    // ---------------------------------------------------------------
    const float INV_SQRT2 = 0.70710678f;
    for (float a : azimuths) {
        camera.set_view_azimuth(a);
        camera.set_position(-10.0f, -10.0f, 20.0f);
        camera.look_at(0.0f, 0.0f, 0.0f);

        const float c = std::cos(a), sn = std::sin(a);
        const float rx = (c + sn) * INV_SQRT2, ry = (sn - c) * INV_SQRT2;
        const float ux = (c - sn) * INV_SQRT2, uy = (sn + c) * INV_SQRT2;

        // A fixed world point, seen before and after the camera flies.
        int bx, by, ax, ay;
        camera.world_to_screen(0.0f, 0.0f, 0.0f, bx, by);

        camera.pan(rx * 4.0f, ry * 4.0f);          // fly screen-right
        camera.world_to_screen(0.0f, 0.0f, 0.0f, ax, ay);
        if (ay == by && ax < bx) {
            tests_passed++;
        } else {
            tests_failed++;
            std::cout << "FAIL: a=" << a << " screen-right pan moved the "
                      << "view to (" << ax << "," << ay << ") from ("
                      << bx << "," << by << "); expected same y, smaller x"
                      << std::endl;
        }

        camera.pan(-rx * 4.0f, -ry * 4.0f);        // back to the start
        camera.pan(ux * 4.0f, uy * 4.0f);          // fly screen-up
        camera.world_to_screen(0.0f, 0.0f, 0.0f, ax, ay);
        if (ax == bx && ay > by) {
            tests_passed++;
        } else {
            tests_failed++;
            std::cout << "FAIL: a=" << a << " screen-up pan moved the view "
                      << "to (" << ax << "," << ay << ") from (" << bx
                      << "," << by << "); expected same x, larger y"
                      << std::endl;
        }
    }

    // pan() must carry the shadow-culling centre with the camera; a
    // stale look-at target culls shadows around where the view began.
    camera.set_view_azimuth(0.0f);
    camera.set_position(0.0f, 0.0f, 20.0f);
    camera.look_at(0.0f, 0.0f, 0.0f);
    camera.pan(7.0f, -3.0f);
    {
        float px, py, pz, tx, ty;
        camera.get_position(px, py, pz);
        camera.get_look_at_target(tx, ty);
        bool moved_together = std::fabs(px - 7.0f) < 1e-4f &&
                              std::fabs(py + 3.0f) < 1e-4f &&
                              std::fabs(tx - 7.0f) < 1e-4f &&
                              std::fabs(ty + 3.0f) < 1e-4f;
        if (moved_together) {
            tests_passed++;
        } else {
            tests_failed++;
            std::cout << "FAIL: pan left camera at (" << px << "," << py
                      << ") and look-at at (" << tx << "," << ty
                      << "); both should have moved by (7,-3)" << std::endl;
        }
    }

    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
