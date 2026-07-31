// SPHERE/ELLIPSOID validation: geometry triangle counts, mass formulas,
// narrow-phase contacts, inertia tensors. Covers the math-only surface
// (GetVolume / GetMass / GetMomentOfInertia) plus the geometry path
// (GetSurfaces / GetShadowTriangles) plus narrow-phase dispatch.
//
// Runs headless: particle_shape_methods.cpp + narrow_phase.cpp are now
// linked into logosphere_core so Linux CI exercises the full test.

#include "particle.h"
#include "logosphere/physics/narrow_phase.h"
#include <cmath>
#include <iostream>
#include <string>
#include <stdexcept>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    std::cout << "  " #name "... "; \
    try { test_##name(); tests_passed++; std::cout << "PASS" << std::endl; } \
    catch (const std::exception& e) { tests_failed++; std::cout << "FAIL: " << e.what() << std::endl; }

#define ASSERT_NEAR(a, b, tol, msg) \
    if (std::abs((a) - (b)) > (tol)) throw std::runtime_error( \
        std::string(msg) + " [got=" + std::to_string(a) + " want=" + std::to_string(b) + "]")

#define ASSERT_EQ(a, b, msg) \
    if ((a) != (b)) throw std::runtime_error( \
        std::string(msg) + " [got=" + std::to_string(a) + " want=" + std::to_string(b) + "]")

#define ASSERT_TRUE(cond, msg) \
    if (!(cond)) throw std::runtime_error(std::string(msg))

constexpr float kPi = 3.14159265358979323846f;

void test_box_volume_matches_whxt() {
    Particle p;
    p.shape = ParticleShape::BOX;
    p.width = 2.0f; p.height = 3.0f; p.thickness = 4.0f;
    ASSERT_NEAR(p.GetVolume(), 24.0f, 0.0001f, "BOX volume = w*h*t");
}

void test_sphere_volume_matches_4_3_pi_r3() {
    Particle p;
    p.shape = ParticleShape::SPHERE;
    p.size = 2.0f;  // diameter → radius 1
    float expected = (4.0f / 3.0f) * kPi * 1.0f;
    ASSERT_NEAR(p.GetVolume(), expected, 0.001f, "SPHERE volume");
}

void test_ellipsoid_volume_matches_4_3_pi_abc() {
    Particle p;
    p.shape = ParticleShape::ELLIPSOID;
    p.width = 2.0f; p.height = 4.0f; p.thickness = 6.0f;  // semi-axes 1, 2, 3
    float expected = (4.0f / 3.0f) * kPi * 1.0f * 2.0f * 3.0f;
    ASSERT_NEAR(p.GetVolume(), expected, 0.001f, "ELLIPSOID volume");
}

void test_mass_scales_with_density() {
    Particle p;
    p.shape = ParticleShape::SPHERE;
    p.size = 2.0f;
    p.material_density = 500.0f;
    float expected_volume = (4.0f / 3.0f) * kPi;
    ASSERT_NEAR(p.GetMass(), expected_volume * 500.0f, 0.01f, "SPHERE mass");
}

void test_box_get_surfaces_returns_12_triangles() {
    Particle p;
    p.shape = ParticleShape::BOX;
    p.width = 1.0f; p.height = 1.0f; p.thickness = 1.0f;
    auto surfaces = p.GetSurfaces();
    // FlatParticleGeometry: 6 quad faces, each as 2 triangles = 12.
    ASSERT_EQ(static_cast<int>(surfaces.size()), 12, "BOX surface count");
}

void test_sphere_get_surfaces_returns_expected_triangles() {
    Particle p;
    p.shape = ParticleShape::SPHERE;
    p.size = 1.0f;
    auto surfaces = p.GetSurfaces();
    // Default icosphere subdivision level 2 → 20 * 4^2 = 320 triangles.
    // Catches accidental changes to the default in
    // particle_shape_methods.cpp::kSphereSubdivisions.
    ASSERT_EQ(static_cast<int>(surfaces.size()), 320, "SPHERE surface count");
}

void test_ellipsoid_get_surfaces_returns_expected_triangles() {
    Particle p;
    p.shape = ParticleShape::ELLIPSOID;
    p.width = 1.0f; p.height = 2.0f; p.thickness = 3.0f;
    auto surfaces = p.GetSurfaces();
    ASSERT_EQ(static_cast<int>(surfaces.size()), 320, "ELLIPSOID surface count");
}

void test_sphere_surfaces_lie_on_sphere() {
    // All vertex distances from the particle center should equal the
    // radius (for a sphere with no transform).
    Particle p;
    p.shape = ParticleShape::SPHERE;
    p.x = 0.0f; p.y = 0.0f; p.z = 0.0f;
    p.size = 2.0f;  // radius 1
    auto surfaces = p.GetSurfaces();
    ASSERT_TRUE(!surfaces.empty(), "got surfaces");
    for (const auto& s : surfaces) {
        for (int v = 0; v < s.vertex_count; v++) {
            float dx = s.vertices[v][0];
            float dy = s.vertices[v][1];
            float dz = s.vertices[v][2];
            float r = std::sqrt(dx*dx + dy*dy + dz*dz);
            ASSERT_NEAR(r, 1.0f, 0.001f, "vertex at radius 1");
        }
    }
}

void test_shadow_triangles_emit_9_floats_per_triangle() {
    Particle p;
    p.shape = ParticleShape::SPHERE;
    p.size = 1.0f;
    std::vector<float> verts;
    p.GetShadowTriangles(verts);
    // 320 triangles × 9 floats/triangle at default subdivision.
    ASSERT_EQ(static_cast<int>(verts.size()), 320 * 9,
              "SPHERE shadow output = triangles * 9");
}

// ============================================================================
// Slice 2: sphere narrow-phase handlers
// ============================================================================

void test_sphere_sphere_far_apart_no_contact() {
    ContactManifold m{};
    bool hit = narrow_phase_sphere_sphere(
        0, 0, 0, 1.0f,
        10, 0, 0, 1.0f,
        0, 1, 0.0f, m);
    ASSERT_TRUE(!hit, "spheres 10m apart should not collide");
}

void test_sphere_sphere_head_on_contact() {
    // Two unit spheres, centers 1.5 m apart along x → overlap 0.5.
    ContactManifold m{};
    bool hit = narrow_phase_sphere_sphere(
        0, 0, 0, 1.0f,
        1.5f, 0, 0, 1.0f,
        0, 1, 0.0f, m);
    ASSERT_TRUE(hit, "overlapping spheres produce a contact");
    ASSERT_NEAR(m.points[0].penetration, 0.5f, 0.001f, "penetration = r_sum - d");
    // Normal points from B toward A = -x
    ASSERT_NEAR(m.normal_x, -1.0f, 0.001f, "normal x from B→A");
    ASSERT_NEAR(m.normal_y,  0.0f, 0.001f, "normal y zero");
    ASSERT_NEAR(m.normal_z,  0.0f, 0.001f, "normal z zero");
    // Contact point is on A's surface in the direction toward B:
    // A + (-normal) * r_a = (0,0,0) + (1,0,0) * 1 = (1, 0, 0).
    ASSERT_NEAR(m.points[0].px, 1.0f, 0.001f, "contact on A's surface toward B");
}

void test_sphere_sphere_diagonal_contact_normalized() {
    // Centers at (0,0,0) and (3,4,0) — distance 5. Radii 3 and 3 → overlap 1.
    ContactManifold m{};
    bool hit = narrow_phase_sphere_sphere(
        0, 0, 0, 3.0f,
        3, 4, 0, 3.0f,
        0, 1, 0.0f, m);
    ASSERT_TRUE(hit, "overlap on diagonal");
    ASSERT_NEAR(m.points[0].penetration, 1.0f, 0.001f, "penetration = 6-5 = 1");
    // Normal from B→A: (-3, -4, 0)/5 = (-0.6, -0.8, 0)
    ASSERT_NEAR(m.normal_x, -0.6f, 0.001f, "normalized x");
    ASSERT_NEAR(m.normal_y, -0.8f, 0.001f, "normalized y");
}

void test_sphere_on_box_floor_normal_is_up() {
    // Unit sphere at (0, 0, 0.5) sitting on a flat box floor at z in
    // [-0.5, 0]. Sphere radius 1, so its bottom is at -0.5 — flush.
    AABB6 floor{-5, 5, -5, 5, -0.5f, 0.0f};
    ContactManifold m{};
    bool hit = narrow_phase_sphere_aabb(
        0, 0, 0.5f, 1.0f,
        floor,
        0, 1, 0.0f, m);
    // Expected: sphere center at z=0.5, closest point on box is (0,0,0).
    // Distance from center to closest point = 0.5, sphere radius = 1 →
    // penetration = 0.5.
    ASSERT_TRUE(hit, "sphere resting on floor produces contact");
    ASSERT_NEAR(m.points[0].penetration, 0.5f, 0.001f, "0.5 m penetration");
    ASSERT_NEAR(m.normal_z, 1.0f, 0.001f, "normal points up (from floor → sphere)");
    ASSERT_NEAR(m.normal_x, 0.0f, 0.001f, "normal x zero");
    ASSERT_NEAR(m.normal_y, 0.0f, 0.001f, "normal y zero");
}

void test_sphere_far_from_box_no_contact() {
    AABB6 box{0, 1, 0, 1, 0, 1};
    ContactManifold m{};
    bool hit = narrow_phase_sphere_aabb(
        5, 5, 5, 0.5f,
        box,
        0, 1, 0.0f, m);
    ASSERT_TRUE(!hit, "sphere way above box, no contact");
}

void test_sphere_center_inside_box_picks_shallowest_face() {
    // Sphere center at (0.3, 0.5, 0.5) inside unit box [0,1]^3.
    // Shallowest exit: x=0 face, distance 0.3.
    AABB6 box{0, 1, 0, 1, 0, 1};
    ContactManifold m{};
    bool hit = narrow_phase_sphere_aabb(
        0.3f, 0.5f, 0.5f, 0.4f,
        box,
        0, 1, 0.0f, m);
    ASSERT_TRUE(hit, "center-inside case produces contact");
    // Normal should point toward -x (out through x=0 face)
    ASSERT_NEAR(m.normal_x, -1.0f, 0.001f, "exit toward -x");
}

void test_sphere_inertia_is_two_fifths_m_r_squared() {
    Particle p;
    p.shape = ParticleShape::SPHERE;
    p.size = 2.0f;  // radius 1
    p.material_density = 1000.0f;
    float m = p.GetMass();
    float expected = 0.4f * m * 1.0f * 1.0f;
    ASSERT_NEAR(p.GetMomentOfInertia(), expected, 0.001f, "SPHERE I = 0.4 m r²");
}

void test_ellipsoid_inertia_matches_closed_form() {
    Particle p;
    p.shape = ParticleShape::ELLIPSOID;
    p.width = 2.0f; p.height = 4.0f; p.thickness = 6.0f;  // a=1, b=2, c=3
    p.material_density = 1000.0f;
    float m = p.GetMass();
    float expected = 0.2f * m * (1.0f * 1.0f + 2.0f * 2.0f);  // 0.2 m (a²+b²)
    ASSERT_NEAR(p.GetMomentOfInertia(), expected, 0.01f, "ELLIPSOID I_z = 0.2 m (a²+b²)");
}

void test_box_inertia_unchanged_from_legacy_cylinder_approx() {
    // Regression guard: existing box callsites rely on the cylinder
    // approximation 0.5 * m * r² with r = (w+h)/4.
    Particle p;
    p.shape = ParticleShape::BOX;
    p.width = 2.0f; p.height = 4.0f; p.thickness = 1.0f;
    p.material_density = 1000.0f;
    float m = p.GetMass();
    float r = (2.0f + 4.0f) * 0.25f;  // 1.5
    float expected = 0.5f * m * r * r;
    ASSERT_NEAR(p.GetMomentOfInertia(), expected, 0.01f, "BOX I unchanged");
}

void test_dispatcher_handles_all_pair_combos() {
    Particle a;
    a.shape = ParticleShape::SPHERE;
    a.size = 1.0f;
    a.x = 0; a.y = 0; a.z = 0;

    Particle b;
    b.shape = ParticleShape::BOX;
    b.width = 1.0f; b.height = 1.0f; b.thickness = 1.0f;
    b.x = 0.6f; b.y = 0; b.z = 0;  // overlapping

    ContactManifold m{};
    bool hit = narrow_phase_particle_pair(a, b, 0, 1, 0.0f, m);
    ASSERT_TRUE(hit, "sphere-box dispatcher detects overlap");

    // Swap: box as A, sphere as B. Result should still report contact,
    // with normals flipped to match A=box, B=sphere convention.
    ContactManifold m2{};
    bool hit2 = narrow_phase_particle_pair(b, a, 1, 0, 0.0f, m2);
    ASSERT_TRUE(hit2, "box-sphere dispatcher detects overlap");
    ASSERT_NEAR(m.normal_x + m2.normal_x, 0.0f, 0.001f,
                "flipped-role normal is negated (sum is zero)");
}

int main() {
    std::cout << "=== Particle Shapes (SPHERE / ELLIPSOID) ===" << std::endl;
    TEST(box_volume_matches_whxt);
    TEST(sphere_volume_matches_4_3_pi_r3);
    TEST(ellipsoid_volume_matches_4_3_pi_abc);
    TEST(mass_scales_with_density);
    TEST(box_get_surfaces_returns_12_triangles);
    TEST(sphere_get_surfaces_returns_expected_triangles);
    TEST(ellipsoid_get_surfaces_returns_expected_triangles);
    TEST(sphere_surfaces_lie_on_sphere);
    TEST(shadow_triangles_emit_9_floats_per_triangle);
    TEST(sphere_sphere_far_apart_no_contact);
    TEST(sphere_sphere_head_on_contact);
    TEST(sphere_sphere_diagonal_contact_normalized);
    TEST(sphere_on_box_floor_normal_is_up);
    TEST(sphere_far_from_box_no_contact);
    TEST(sphere_center_inside_box_picks_shallowest_face);
    TEST(dispatcher_handles_all_pair_combos);
    TEST(sphere_inertia_is_two_fifths_m_r_squared);
    TEST(ellipsoid_inertia_matches_closed_form);
    TEST(box_inertia_unchanged_from_legacy_cylinder_approx);
    std::cout << std::endl << tests_passed << " passed, "
              << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
