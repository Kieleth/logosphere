// ============================================================================
// CONTACT MANIFOLD UNIT TESTS
// ============================================================================
// Pure geometry tests for SAT + Sutherland-Hodgman face clipping.
// No engine needed. Tests narrow_phase_aabb() directly.
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
    check(!contact, "Separated boxes: no contact");
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
    check(contact, "Within margin: contact exists");
    if (contact) {
        check(m.num_points > 0, "Has contact points");
        check(std::abs(m.normal_x) > 0.9f, "Normal along X");
        for (int i = 0; i < m.num_points; i++) {
            check(m.points[i].penetration < 0, "Penetration negative (speculative)");
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
    check(contact, "Overlapping cubes: contact");
    if (contact) {
        print_manifold(m);
        check(m.reference_axis == 0, "SAT picks X axis (min overlap)");
        check(std::abs(m.normal_x) > 0.9f, "Normal along X");
        check(m.num_points >= 1 && m.num_points <= 4, "1-4 contact points");
        // All points should have penetration near 0.2
        for (int i = 0; i < m.num_points; i++) {
            check(m.points[i].penetration > 0.0f, "Positive penetration");
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
    check(contact, "Foot on floor: contact");
    if (contact) {
        print_manifold(m);
        check(m.reference_axis == 2, "SAT picks Z (not X or Y)");
        check(std::abs(m.normal_z) > 0.9f, "Normal along Z");
        // Should get 4 points (foot's bottom face clipped to floor's top)
        check(m.num_points == 4, "4 contact points (full face overlap)");
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

    check(contact_a, "Foot contacts tile A");
    check(contact_b, "Foot contacts tile B");

    if (contact_a) {
        check(m_a.reference_axis == 2, "Tile A: SAT picks Z (not Y!)");
        check(std::abs(m_a.normal_z) > 0.9f, "Tile A: normal is vertical");
        bool no_horizontal_a = std::abs(m_a.normal_x) < 0.1f && std::abs(m_a.normal_y) < 0.1f;
        check(no_horizontal_a, "Tile A: no horizontal normal component");
    }

    if (contact_b) {
        check(m_b.reference_axis == 2, "Tile B: SAT picks Z (not Y!)");
        check(std::abs(m_b.normal_z) > 0.9f, "Tile B: normal is vertical");
        bool no_horizontal_b = std::abs(m_b.normal_x) < 0.1f && std::abs(m_b.normal_y) < 0.1f;
        check(no_horizontal_b, "Tile B: no horizontal normal component");
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
    check(contact, "Partial overlap: contact");
    if (contact) {
        print_manifold(m);
        check(m.reference_axis == 0, "SAT picks X (0.5m, tied with Y, X checked first)");
        check(m.num_points >= 1, "At least 1 contact point");
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
    check(contact, "Deep foot: contact");
    if (contact) {
        print_manifold(m);
        check(m.reference_axis == 2, "SAT picks Z (not X despite foot being narrow)");
        check(std::abs(m.normal_z) > 0.9f, "Normal along Z");
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
    check(contact, "Box-wall: contact");
    if (contact) {
        print_manifold(m);
        check(m.reference_axis == 0, "SAT picks X (wall face normal)");
        check(std::abs(m.normal_x) > 0.9f, "Normal along X");
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
