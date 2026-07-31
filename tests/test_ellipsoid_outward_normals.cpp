// Pins the contract: EllipsoidGeometry(hw, hh, ht) must emit triangles
// whose face normals all point AWAY from the ellipsoid center.
//
// Why this test exists — ellipsoid_lighting_test shows the bike body's
// visual symptom (mostly black, thin lit rim) while sphere_lighting_test
// with the same scene renders a fully-lit sphere. The only code
// difference between the two is populate_icosphere(r,r,r) vs (hw,hh,ht):
// the non-uniform-scale path.
//
// If ANY face normal here dots negative with (face_center − ellipsoid
// center), it is pointing INWARD. A flat-shaded rasterizer that reads
// that normal and Lambert-shades against an outside light gets n·L ≤ 0
// for half the triangles — exactly the "mostly black" symptom we see.
//
// Outcomes:
//   all pass → CPU geometry is clean; the inversion (if any) happens
//              downstream (GPU upload, shader, etc.). Hunt shifts to the
//              GPU side.
//   any fail → offending triangle indices + their normals print. The
//              fix is in populate_icosphere / emit_surfaces and this
//              test becomes the regression gate.

#include "sphere_geometry.h"
#include "particle_geometry_v2.h"
#include "projection_system.h"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    std::cout << "  " #name "... "; \
    try { test_##name(); tests_passed++; std::cout << "PASS" << std::endl; } \
    catch (const std::exception& e) { tests_failed++; std::cout << "FAIL: " << e.what() << std::endl; }

// Bike-body aspect ratio — exact same the motorcycle and the ellipsoid
// lighting test use. Picked because it's the configuration currently
// failing visually.
static constexpr float kEllipHW = 0.275f;   // width  / 2
static constexpr float kEllipHH = 0.900f;   // height / 2 (forward axis)
static constexpr float kEllipHT = 0.160f;   // thickness / 2 (flat axis)

static void assert_outward_normals(const std::vector<Surface>& surfaces,
                                   float cx, float cy, float cz,
                                   const char* label) {
    int inverted = 0;
    int zero_normal = 0;
    int first_bad_idx = -1;
    float worst_dot = 0.0f;

    std::printf("    [%s] %zu surfaces\n", label, surfaces.size());

    for (size_t i = 0; i < surfaces.size(); ++i) {
        const auto& s = surfaces[i];

        // Face center in world space (from stored vertex_count/vertices).
        float ax = 0.0f, ay = 0.0f, az = 0.0f;
        for (int v = 0; v < s.vertex_count; ++v) {
            ax += s.vertices[v][0];
            ay += s.vertices[v][1];
            az += s.vertices[v][2];
        }
        ax /= s.vertex_count;
        ay /= s.vertex_count;
        az /= s.vertex_count;

        float rx = ax - cx;
        float ry = ay - cy;
        float rz = az - cz;

        float nlen2 = s.nx * s.nx + s.ny * s.ny + s.nz * s.nz;
        if (nlen2 < 1e-10f) {
            zero_normal++;
            continue;
        }

        float dot = s.nx * rx + s.ny * ry + s.nz * rz;
        if (dot <= 0.0f) {
            inverted++;
            if (first_bad_idx < 0 || dot < worst_dot) {
                first_bad_idx = (int)i;
                worst_dot = dot;
            }
        }
    }

    if (zero_normal > 0) {
        throw std::runtime_error(std::to_string(zero_normal) +
            " surfaces with zero-length normals.");
    }
    if (inverted > 0) {
        const auto& bad = surfaces[first_bad_idx];
        float ax = 0.0f, ay = 0.0f, az = 0.0f;
        for (int v = 0; v < bad.vertex_count; ++v) {
            ax += bad.vertices[v][0];
            ay += bad.vertices[v][1];
            az += bad.vertices[v][2];
        }
        ax /= bad.vertex_count; ay /= bad.vertex_count; az /= bad.vertex_count;
        std::printf("    worst inverted face idx=%d:\n", first_bad_idx);
        std::printf("      center=(%+.3f,%+.3f,%+.3f)  center-body=(%+.3f,%+.3f,%+.3f)\n",
                    ax, ay, az, ax - cx, ay - cy, az - cz);
        std::printf("      normal=(%+.3f,%+.3f,%+.3f)  n·(c-body)=%+.4f\n",
                    bad.nx, bad.ny, bad.nz, worst_dot);
        std::printf("      vertices:\n");
        for (int v = 0; v < bad.vertex_count; ++v) {
            std::printf("        v%d = (%+.3f,%+.3f,%+.3f)\n",
                        v, bad.vertices[v][0], bad.vertices[v][1], bad.vertices[v][2]);
        }
        throw std::runtime_error(std::to_string(inverted) +
            "/" + std::to_string(surfaces.size()) +
            " triangles have inward-facing normals. " +
            "Flat-shaded Lambert against an external light gives n·L ≤ 0 on those "
            "triangles → pixel renders black. This is the ellipsoid body bug.");
    }
}

// Subdivision level 0 — the base 20-triangle icosahedron. Matches the
// sphere_geometry.cpp default and is the first place normals could flip
// if kIcoFaces has a winding issue interacting with non-uniform scale.
void test_ellipsoid_base_icosahedron_outward_normals() {
    ParticleGeometryV2::EllipsoidGeometry geom(kEllipHW, kEllipHH, kEllipHT, 0);
    ParticleGeometryV2::Transform id(ParticleGeometryV2::Vec3(0, 0, 0), 0, 0, 0);
    auto surfaces = geom.to_surfaces(id);
    assert_outward_normals(surfaces, 0.0f, 0.0f, 0.0f, "L0 bike-body ellipsoid");
}

// Subdivision level 2 — 320 triangles. This is the level
// particle_shape_methods.cpp::kSphereSubdivisions actually ships with
// and what the visual test renders. If the base icosahedron is fine but
// subdivision breaks winding, this catches it.
void test_ellipsoid_subdivided_outward_normals() {
    ParticleGeometryV2::EllipsoidGeometry geom(kEllipHW, kEllipHH, kEllipHT, 2);
    ParticleGeometryV2::Transform id(ParticleGeometryV2::Vec3(0, 0, 0), 0, 0, 0);
    auto surfaces = geom.to_surfaces(id);
    assert_outward_normals(surfaces, 0.0f, 0.0f, 0.0f, "L2 bike-body ellipsoid");
}

// Control: uniform sphere with same topology and same subdivision must
// pass trivially. If it fails the test itself is wrong. If sphere
// passes and ellipsoid fails, the diff is specifically non-uniform
// scale.
void test_uniform_sphere_control_outward_normals() {
    ParticleGeometryV2::SphereGeometry geom(0.50f, 2);
    ParticleGeometryV2::Transform id(ParticleGeometryV2::Vec3(0, 0, 0), 0, 0, 0);
    auto surfaces = geom.to_surfaces(id);
    assert_outward_normals(surfaces, 0.0f, 0.0f, 0.0f, "L2 uniform sphere (control)");
}

// Extreme aspect ratio — stress the non-uniform-scale path. If the bike
// body passes but an extreme (1.0, 0.05, 1.0) ratio fails, the
// subdivision midpoint lift is breaking in the limit.
void test_ellipsoid_extreme_flat_outward_normals() {
    ParticleGeometryV2::EllipsoidGeometry geom(1.0f, 0.05f, 1.0f, 2);
    ParticleGeometryV2::Transform id(ParticleGeometryV2::Vec3(0, 0, 0), 0, 0, 0);
    auto surfaces = geom.to_surfaces(id);
    assert_outward_normals(surfaces, 0.0f, 0.0f, 0.0f, "L2 extreme-flat ellipsoid");
}

// ============================================================================
// Screen-winding diagnostic — project every emitted triangle through the iso
// projection (same params as ellipsoid_lighting_test) and count how many have
// positive vs negative signed screen area. The gbuffer rasterizer's
// `edge0,1,2 >= 0` inside-test accepts exactly ONE of those signs. If all
// triangles share a single sign, the bug is a flipped convention (trivial
// fix). If they're MIXED, different triangles of the same mesh have
// opposite screen windings — which means positive scaling + iso projection
// broke the assumption that winding is preserved, and the fix has to live
// at the emit step (reorder vertices per-triangle so all match).
// ============================================================================

struct ScreenPt { float x, y; };

static ScreenPt project_iso(const IsometricProjection& iso,
                            float wx, float wy, float wz,
                            float cx, float cy, float cz) {
    int sx = 0, sy = 0;
    iso.project(wx, wy, wz, cx, cy, cz, 1600, 1200, 120.0f, sx, sy);
    return {(float)sx, (float)sy};
}

static float signed_area_2d(const ScreenPt& a, const ScreenPt& b, const ScreenPt& c) {
    return (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
}

static void report_screen_winding_distribution(
    const std::vector<Surface>& surfaces,
    float cam_x, float cam_y, float cam_z,
    const char* label)
{
    IsometricProjection iso;
    // Iso view axis from scene toward camera (cross product of iso_x axis
    // (1,-1,0) and iso_y-visual axis (0.5,0.5,1), pointing OUT of screen).
    // Normalized.
    const float view_len = std::sqrt(1.0f + 1.0f + 1.0f);
    const float vx = -1.0f / view_len;
    const float vy = -1.0f / view_len;
    const float vz =  1.0f / view_len;

    int pos = 0, neg = 0, zero = 0;
    int front = 0, back = 0;
    int pos_front = 0, pos_back = 0;
    int neg_front = 0, neg_back = 0;
    float min_abs_nonzero = 1e30f;

    for (const auto& s : surfaces) {
        if (s.vertex_count < 3) continue;
        ScreenPt v0 = project_iso(iso, s.vertices[0][0], s.vertices[0][1], s.vertices[0][2],
                                  cam_x, cam_y, cam_z);
        ScreenPt v1 = project_iso(iso, s.vertices[1][0], s.vertices[1][1], s.vertices[1][2],
                                  cam_x, cam_y, cam_z);
        ScreenPt v2 = project_iso(iso, s.vertices[2][0], s.vertices[2][1], s.vertices[2][2],
                                  cam_x, cam_y, cam_z);
        float a = signed_area_2d(v0, v1, v2);
        float ndotv = s.nx * vx + s.ny * vy + s.nz * vz;
        bool is_front = ndotv > 0.0f;
        bool is_back  = ndotv < 0.0f;
        if (is_front) front++; else if (is_back) back++;

        if (a > 0.5f) {
            pos++;
            if (is_front) pos_front++;
            if (is_back)  pos_back++;
            min_abs_nonzero = std::min(min_abs_nonzero, std::abs(a));
        } else if (a < -0.5f) {
            neg++;
            if (is_front) neg_front++;
            if (is_back)  neg_back++;
            min_abs_nonzero = std::min(min_abs_nonzero, std::abs(a));
        } else {
            zero++;
        }
    }

    std::printf("    [%s] %zu triangles (cam %.2f,%.2f,%.2f):\n"
                "       screen area    +: %3d   -: %3d   0: %3d  (min |area|=%.2f px²)\n"
                "       3D facing   front: %3d   back: %3d\n"
                "       screen-sign × 3D-facing cross-tab:\n"
                "         +area & front: %3d    +area & back: %3d\n"
                "         -area & front: %3d    -area & back: %3d\n",
                label, surfaces.size(), cam_x, cam_y, cam_z,
                pos, neg, zero, min_abs_nonzero,
                front, back,
                pos_front, pos_back,
                neg_front, neg_back);

    // A correctly-set-up single-sided rasterizer has ONE of the diagonals of
    // the cross-tab empty: either (+,front)+(-,back) are the only buckets
    // with entries (convention A) or (+,back)+(-,front) (convention B). If
    // all four buckets are non-empty, projection is flipping winding for
    // some triangles — the fix cannot be a single sign change.
    int off_diag_a = pos_back + neg_front;
    int off_diag_b = pos_front + neg_back;
    std::printf("       off-diag A (+back, -front) = %d\n", off_diag_a);
    std::printf("       off-diag B (+front, -back) = %d\n", off_diag_b);
    if (pos_front > 0 && pos_back > 0 && neg_front > 0 && neg_back > 0) {
        std::printf("       !! MIXED: both diagonals populated — iso projection\n"
                    "          inverts winding for some triangles of this shape.\n");
    }
}

void test_screen_winding_uniform_sphere() {
    ParticleGeometryV2::SphereGeometry geom(0.50f, 2);
    ParticleGeometryV2::Transform id(ParticleGeometryV2::Vec3(0, 0, 0), 0, 0, 0);
    auto surfaces = geom.to_surfaces(id);
    report_screen_winding_distribution(surfaces, 0.0f, 0.0f, 0.5f, "uniform sphere (sphere_lighting_test cam)");
}

void test_screen_winding_bike_body_ellipsoid() {
    ParticleGeometryV2::EllipsoidGeometry geom(kEllipHW, kEllipHH, kEllipHT, 2);
    ParticleGeometryV2::Transform id(ParticleGeometryV2::Vec3(0, 0, 0), 0, 0, 0);
    auto surfaces = geom.to_surfaces(id);
    report_screen_winding_distribution(surfaces, 0.0f, 0.0f, 0.16f, "bike-body ellipsoid (ellipsoid_lighting_test cam)");
}

int main() {
    std::cout << "=== EllipsoidGeometry outward-normal contract ===" << std::endl;
    TEST(uniform_sphere_control_outward_normals);
    TEST(ellipsoid_base_icosahedron_outward_normals);
    TEST(ellipsoid_subdivided_outward_normals);
    TEST(ellipsoid_extreme_flat_outward_normals);
    std::cout << std::endl << "=== Screen-winding distribution under iso projection ===" << std::endl;
    TEST(screen_winding_uniform_sphere);
    TEST(screen_winding_bike_body_ellipsoid);
    std::cout << std::endl << tests_passed << " passed, "
              << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
