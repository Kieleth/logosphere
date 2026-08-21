// ============================================================================
// CONTACT MANIFOLD UNIT TESTS
// ============================================================================
// Pure geometry tests for SAT + Sutherland-Hodgman face clipping.
// No engine needed. Tests narrow_phase_aabb() directly.
//
// LAWS (assert-protocol migration, 2026-08-21). Almost every check here is
// INV-12: contact normals and points come from the bodies' actual shapes, and
// the tile-boundary case is INV-12's own origin (the walk-gate snowplow, a
// seam between two flat tiles reported as a wall). Normal-DIRECTION checks
// cite INV-25, the one-sign convention. The speculative case cites G-45: a
// negative penetration is the row stating that its own gap is still open.
//
// Run: ./logosphere-tests --test test_contact_manifold
// ============================================================================

#include "logosphere/physics/narrow_phase.h"
#include <cstdio>
#include <cmath>

static int tests_passed = 0;
static int tests_failed = 0;

static void check(bool condition, const char* name) {
    if (condition) {
        printf("  PASS: %s\n", name);
        tests_passed++;
    } else {
        printf("  FAIL: %s\n", name);
        tests_failed++;
    }
}

static void print_manifold(const ContactManifold& m) {
    printf("    body_a=%zu body_b=%zu normal=(%.3f,%.3f,%.3f) points=%d face=%d axis=%d\n",
           m.body_a, m.body_b, m.normal_x, m.normal_y, m.normal_z,
           m.num_points, m.is_face_contact ? 1 : 0, m.reference_axis);
    for (int i = 0; i < m.num_points; i++) {
        printf("    [%d] pos=(%.4f, %.4f, %.4f) pen=%.4f\n",
               i, m.points[i].px, m.points[i].py, m.points[i].pz,
               m.points[i].penetration);
    }
}

// ============================================================================
// Test 1: No contact (separated boxes)
// ============================================================================
static void test_no_contact() {
    printf("\n--- Test: No contact (separated) ---\n");
    AABB6 a = {0, 1, 0, 1, 0, 1};          // Unit cube at origin
    AABB6 b = {2, 3, 0, 1, 0, 1};          // Unit cube 1m away on X
    ContactManifold m;
    bool contact = narrow_phase_aabb(a, b, 0, 1, 0.0f, m);
    check(!contact, "INV-12: separated boxes produce no contact");
}

// ============================================================================
// Test 2: Speculative contact (within margin but not touching)
// ============================================================================
static void test_speculative() {
    printf("\n--- Test: Speculative contact ---\n");
    AABB6 a = {0, 1, 0, 1, 0, 1};
    AABB6 b = {1.05f, 2.05f, 0, 1, 0, 1};  // 0.05m gap on X
    ContactManifold m;
    bool contact = narrow_phase_aabb(a, b, 0, 1, 0.08f, m);
    check(contact, "INV-12: within margin, a speculative contact exists");
    if (contact) {
        check(m.num_points > 0, "INV-12: the manifold carries contact POINTS, not just a normal");
        check(std::abs(m.normal_x) > 0.9f, "INV-25: normal along X, one documented direction");
        for (int i = 0; i < m.num_points; i++) {
            check(m.points[i].penetration < 0, "G-45: penetration negative — the row states its own gap is open, which is what makes it speculative");
        }
    }
}

// ============================================================================
// Test 3: Two equal cubes overlapping on X
// ============================================================================
static void test_equal_cubes_x_overlap() {
    printf("\n--- Test: Equal cubes, X overlap ---\n");
    AABB6 a = {0, 1, 0, 1, 0, 1};
    AABB6 b = {0.8f, 1.8f, 0, 1, 0, 1};  // 0.2m overlap on X
    ContactManifold m;
    bool contact = narrow_phase_aabb(a, b, 0, 1, 0.0f, m);
    check(contact, "INV-12: overlapping cubes produce a contact");
    if (contact) {
        print_manifold(m);
        check(m.reference_axis == 0, "INV-12: SAT picks X axis (min overlap) — the normal comes from the geometry, not from a world axis");
        check(std::abs(m.normal_x) > 0.9f, "INV-25: normal along X, one documented direction");
        check(m.num_points >= 1 && m.num_points <= 4, "INV-12: 1-4 contact points from face clipping");
        // All points should have penetration near 0.2
        for (int i = 0; i < m.num_points; i++) {
            check(m.points[i].penetration > 0.0f, "INV-2: positive penetration is measured, not assumed away");
        }
    }
}

// ============================================================================
// Test 4: Small box centered on large floor (the case dist_to_exit was made for)
// ============================================================================
static void test_foot_on_floor() {
    printf("\n--- Test: Small foot on large floor ---\n");
    // Floor: 20m x 20m x 0.1m, centered at origin
    AABB6 floor_box = {-10, 10, -10, 10, -0.05f, 0.05f};
    // Foot: 0.034m x 0.05m x 0.03m, sitting on floor (bottom at z=0.04, overlapping 0.01m)
    AABB6 foot = {-0.017f, 0.017f, -0.025f, 0.025f, 0.04f, 0.07f};
    ContactManifold m;
    bool contact = narrow_phase_aabb(floor_box, foot, 0, 1, 0.0f, m);
    check(contact, "INV-12: foot on floor produces a contact");
    if (contact) {
        print_manifold(m);
        check(m.reference_axis == 2, "INV-12: SAT picks Z (not X or Y) — a flat floor sheds no side normal");
        check(std::abs(m.normal_z) > 0.9f, "INV-25: normal along Z, one documented direction");
        // Should get 4 points (foot's bottom face clipped to floor's top)
        check(m.num_points == 4, "INV-12: 4 contact points (full face overlap)");
    }
}

// ============================================================================
// Test 5: TILE BOUNDARY (the critical regression test)
// ============================================================================
static void test_tile_boundary() {
    printf("\n--- Test: Foot at tile boundary ---\n");
    // Tile A: 4m x 2m x 0.1m at y=[0,2]
    AABB6 tile_a = {-2, 2, 0, 2, -0.05f, 0.05f};
    // Tile B: 4m x 2m x 0.1m at y=[2,4]
    AABB6 tile_b = {-2, 2, 2, 4, -0.05f, 0.05f};
    // Foot straddling boundary: y=[1.965, 2.015], z slightly into tile
    AABB6 foot = {-0.05f, 0.05f, 1.965f, 2.015f, 0.04f, 0.07f};

    // Test foot vs tile A
    ContactManifold m_a;
    bool contact_a = narrow_phase_aabb(tile_a, foot, 0, 1, 0.0f, m_a);
    printf("  Foot vs Tile A:\n");
    if (contact_a) print_manifold(m_a);

    // Test foot vs tile B
    ContactManifold m_b;
    bool contact_b = narrow_phase_aabb(tile_b, foot, 2, 1, 0.0f, m_b);
    printf("  Foot vs Tile B:\n");
    if (contact_b) print_manifold(m_b);

    check(contact_a, "INV-12: foot contacts tile A");
    check(contact_b, "INV-12: foot contacts tile B");

    if (contact_a) {
        check(m_a.reference_axis == 2, "INV-12: tile A: SAT picks Z (not Y!) — the seam is not a wall");
        check(std::abs(m_a.normal_z) > 0.9f, "INV-25: tile A: normal is vertical");
        bool no_horizontal_a = std::abs(m_a.normal_x) < 0.1f && std::abs(m_a.normal_y) < 0.1f;
        check(no_horizontal_a, "INV-12: tile A: no horizontal normal component (the walk-gate snowplow)");
    }

    if (contact_b) {
        check(m_b.reference_axis == 2, "INV-12: tile B: SAT picks Z (not Y!) — the seam is not a wall");
        check(std::abs(m_b.normal_z) > 0.9f, "INV-25: tile B: normal is vertical");
        bool no_horizontal_b = std::abs(m_b.normal_x) < 0.1f && std::abs(m_b.normal_y) < 0.1f;
        check(no_horizontal_b, "INV-12: tile B: no horizontal normal component (the walk-gate snowplow)");
    }
}

// ============================================================================
// Test 6: Partial overlap (cube shifted half-way)
// ============================================================================
static void test_partial_overlap() {
    printf("\n--- Test: Partial overlap ---\n");
    // overlap_x = 0.5, overlap_y = 0.5, overlap_z = 0.9
    // All partial (not contained), so min overlap = X or Y (0.5)
    AABB6 a = {0, 1, 0, 1, 0, 1};
    AABB6 b = {0.5f, 1.5f, 0.5f, 1.5f, -0.1f, 0.9f};
    ContactManifold m;
    bool contact = narrow_phase_aabb(a, b, 0, 1, 0.0f, m);
    check(contact, "INV-12: partial overlap produces a contact");
    if (contact) {
        print_manifold(m);
        check(m.reference_axis == 0, "INV-12 + hygiene: SAT picks X (0.5m, tied with Y, X checked first). The tie-break is a determinism convention (INV-27), not a physical claim");
        check(m.num_points >= 1, "INV-12: at least 1 contact point");
    }
}

// ============================================================================
// Test 7: Deep penetration (foot deep in floor, original dist_to_exit case)
// ============================================================================
static void test_deep_penetration() {
    printf("\n--- Test: Deep penetration (foot deep in floor) ---\n");
    // Floor
    AABB6 floor_box = {-10, 10, -10, 10, -0.05f, 0.05f};
    // Foot deep inside: z range overlaps floor by 0.04m
    // foot z=[0.01, 0.07], floor z=[-0.05, 0.05], overlap_z = 0.04m
    // foot fully inside floor on X and Y
    AABB6 foot = {-0.017f, 0.017f, -0.025f, 0.025f, 0.01f, 0.07f};
    ContactManifold m;
    bool contact = narrow_phase_aabb(floor_box, foot, 0, 1, 0.0f, m);
    check(contact, "INV-12: deep foot produces a contact");
    if (contact) {
        print_manifold(m);
        check(m.reference_axis == 2, "INV-12: SAT picks Z (not X despite foot being narrow) — minimum-overlap axis, not smallest-extent axis");
        check(std::abs(m.normal_z) > 0.9f, "INV-25: normal along Z, one documented direction");
    }
}

// ============================================================================
// Test 8: Box against wall
// ============================================================================
static void test_box_against_wall() {
    printf("\n--- Test: Box pushed into wall ---\n");
    // Wall: thin on X (0.2m), tall and wide
    AABB6 wall = {5, 5.2f, -5, 5, 0, 3};
    // Box: partially inside wall on X
    AABB6 box = {5.05f, 5.35f, 0, 0.5f, 0.5f, 1.0f};
    ContactManifold m;
    bool contact = narrow_phase_aabb(wall, box, 0, 1, 0.0f, m);
    check(contact, "INV-12: box-wall contact");
    if (contact) {
        print_manifold(m);
        check(m.reference_axis == 0, "INV-12: SAT picks X (wall face normal). INV-6: a wall is handled by the same mechanism as a floor");
        check(std::abs(m.normal_x) > 0.9f, "INV-25: normal along X, one documented direction");
    }
}

// ============================================================================
// Entry point
// ============================================================================
bool test_contact_manifold() {
    printf("\n=== Contact Manifold Unit Tests (SAT + Clipping) ===\n");

    tests_passed = 0;
    tests_failed = 0;

    test_no_contact();
    test_speculative();
    test_equal_cubes_x_overlap();
    test_foot_on_floor();
    test_tile_boundary();
    test_partial_overlap();
    test_deep_penetration();
    test_box_against_wall();

    printf("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed == 0;
}
