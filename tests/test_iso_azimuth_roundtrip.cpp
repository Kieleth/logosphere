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

    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
