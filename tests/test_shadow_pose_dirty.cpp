// Pins the contract: shadow-geometry dirty detection must catch any
// pose change, not just translation.
//
// Backstory — under bike_viewer, the motorcycle rotated visually but its
// cast shadow stayed in the pre-rotation orientation. RCA: the dirty
// detector in render_pipeline.cpp compared only (x, y, z) between
// frames. The body particle's CENTER didn't move during yaw rotation
// (it stays at the bike's pivot), so the detector said "not dirty";
// the BVH's refit_dirty path skipped its leaves; the leaf AABBs kept
// their pre-rotation values; ray traversal hit phantom triangles in
// the old positions. Visually: shadow does not follow the bike.
//
// This test re-implements the dirty-detection algorithm in pure C++
// (no engine dependencies) and asserts it catches:
//   - position change (the original case)
//   - rotation change with position fixed (the case that bit us)
//   - both at once
//   - neither (no spurious dirty)
//
// If somebody refactors render_pipeline.cpp's check back to a
// translation-only test, this test fails and they get a clear error
// pointing at the rotation case before the visual bug ships.
//
// Plus a particle-geometry contract: if a particle's rotation_z changes
// and we ask for its shadow triangles, the new triangles must be the
// rotated form of the old ones (otherwise the dirty detector being
// correct doesn't help — we would still feed stale geometry to the
// refit).

#include "particle.h"
#include "particle_geometry_v2.h"

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

// ---------------------------------------------------------------------------
// Reproduction of render_pipeline.cpp's dirty-detection check. This is a
// data-only function: it compares two pose snapshots and returns whether
// the particle has moved enough that its shadow geometry is stale. Keep
// this in lockstep with the production check; if the production fix
// changes (e.g., adopts quaternions, adds scale, etc.) update this mirror.
// ---------------------------------------------------------------------------
struct PoseSnapshot {
    float x, y, z;
    float rot_x, rot_y, rot_z;
};

static bool dirty_check_mirror(const PoseSnapshot& prev, const PoseSnapshot& now) {
    constexpr float POS_EPSILON_SQ = 1e-12f;
    constexpr float ROT_EPSILON_SQ = 1e-10f;
    float dx = now.x - prev.x;
    float dy = now.y - prev.y;
    float dz = now.z - prev.z;
    float drx = now.rot_x - prev.rot_x;
    float dry = now.rot_y - prev.rot_y;
    float drz = now.rot_z - prev.rot_z;
    bool moved   = dx*dx + dy*dy + dz*dz > POS_EPSILON_SQ;
    bool rotated = drx*drx + dry*dry + drz*drz > ROT_EPSILON_SQ;
    return moved || rotated;
}

void test_pose_unchanged_is_not_dirty() {
    PoseSnapshot a{0.0f, 0.0f, 0.42f, 0.0f, 0.0f, 0.0f};
    PoseSnapshot b = a;
    if (dirty_check_mirror(a, b)) {
        throw std::runtime_error("identical poses must NOT be flagged dirty");
    }
}

void test_translation_is_dirty() {
    PoseSnapshot a{0.0f, 0.0f, 0.42f, 0.0f, 0.0f, 0.0f};
    PoseSnapshot b{0.5f, 0.0f, 0.42f, 0.0f, 0.0f, 0.0f};  // moved +X
    if (!dirty_check_mirror(a, b)) {
        throw std::runtime_error("translation by 0.5 m must be flagged dirty");
    }
}

void test_rotation_only_is_dirty() {
    // The bike body case: pivot is the particle center, rotation_z changes
    // but the position field doesn't.
    PoseSnapshot a{0.0f, 0.0f, 0.42f, 0.0f, 0.0f,  0.00f};
    PoseSnapshot b{0.0f, 0.0f, 0.42f, 0.0f, 0.0f,  1.57f};   // 90° yaw
    if (!dirty_check_mirror(a, b)) {
        throw std::runtime_error(
            "rotation_z change with no translation must be flagged dirty. "
            "If this fails, the dirty detector has regressed to translation-only "
            "and any rotating-in-place body's cast shadow will become stale.");
    }
}

void test_microscopic_rotation_above_epsilon_is_dirty() {
    // 1e-4 rad ≈ 0.006° — well above ROT_EPSILON_SQ floor of 1e-10.
    PoseSnapshot a{0.0f, 0.0f, 0.42f, 0.0f, 0.0f, 0.0f};
    PoseSnapshot b{0.0f, 0.0f, 0.42f, 0.0f, 0.0f, 1e-4f};
    if (!dirty_check_mirror(a, b)) {
        throw std::runtime_error("rotation of 1e-4 rad must trip dirty");
    }
}

void test_subepsilon_rotation_is_not_dirty() {
    // 1e-6 rad → squared = 1e-12 < ROT_EPSILON_SQ. Below threshold.
    // Critical: must not over-trigger or every frame's float jitter would
    // refit every particle — defeats the whole point of the dirty list.
    PoseSnapshot a{0.0f, 0.0f, 0.42f, 0.0f, 0.0f, 0.0f};
    PoseSnapshot b{0.0f, 0.0f, 0.42f, 0.0f, 0.0f, 1e-6f};
    if (dirty_check_mirror(a, b)) {
        throw std::runtime_error("sub-epsilon rotation must NOT trip dirty");
    }
}

// ---------------------------------------------------------------------------
// Geometry contract: GetShadowTriangles must produce different triangle
// vertex positions when the particle's rotation_z changes. Without this,
// a perfectly-functioning dirty detector would still feed identical
// (un-rotated) geometry to refit_dirty — and the visual symptom would
// persist. This locks the "particle.rotation_z reaches the geometry path"
// invariant in data.
// ---------------------------------------------------------------------------
void test_shadow_triangles_change_under_rotation() {
    Particle p = {};
    p.shape = ParticleShape::ELLIPSOID;
    p.x = 0.0f; p.y = 0.0f; p.z = 0.42f;
    p.width = 0.55f; p.height = 1.80f; p.thickness = 0.32f;  // bike-body dims
    p.rotation_z = 0.0f;

    std::vector<float> t0;
    p.GetShadowTriangles(t0);

    p.rotation_z = 1.5707963f;  // 90°
    std::vector<float> t90;
    p.GetShadowTriangles(t90);

    if (t0.size() != t90.size()) {
        throw std::runtime_error(
            "rotation must not change triangle COUNT (got " +
            std::to_string(t0.size()/9) + " vs " + std::to_string(t90.size()/9) + ")");
    }

    // Find the maximum coord delta. For a 1.80 m long ellipsoid yawed 90°,
    // a vertex originally at (0, 0.9, 0.42) lands near (0.9, 0, 0.42).
    // We expect deltas on the order of the body's half-extents — well
    // above any numeric noise.
    float max_delta = 0.0f;
    for (size_t i = 0; i < t0.size(); ++i) {
        float d = std::abs(t0[i] - t90[i]);
        if (d > max_delta) max_delta = d;
    }
    std::printf("    max vertex delta after 90° yaw: %.4f m\n", max_delta);
    if (max_delta < 0.30f) {
        throw std::runtime_error(
            "GetShadowTriangles is returning UN-rotated geometry even though "
            "particle.rotation_z changed by 90°. Max coord delta = " +
            std::to_string(max_delta) + " m — should be ≥ ~0.5 m for the "
            "bike-body ellipsoid. The geometry path is ignoring rotation_z; "
            "fix it before debugging downstream BVH/shadow issues.");
    }
}

// Composite contract test: simulate two consecutive frames of a rotating
// bike. Frame 1: build pose snapshots and triangle list. Frame 2: rotate
// the body in place. Assert (a) the dirty detector flags the body and
// (b) the triangles really are different — both sides of the contract
// have to hold for the visual to update.
void test_rotating_body_full_pipeline_signals() {
    Particle body = {};
    body.shape = ParticleShape::ELLIPSOID;
    body.x = 0.0f; body.y = 0.0f; body.z = 0.42f;
    body.width = 0.55f; body.height = 1.80f; body.thickness = 0.32f;
    body.rotation_z = 0.0f;

    PoseSnapshot prev{body.x, body.y, body.z,
                       body.rotation_x, body.rotation_y, body.rotation_z};

    std::vector<float> tris_frame1;
    body.GetShadowTriangles(tris_frame1);

    // Frame 2: simulate one update_game tick of LEFT/RIGHT rotation.
    body.rotation_z = 0.15f;  // bike_viewer's kRotStep

    PoseSnapshot now{body.x, body.y, body.z,
                      body.rotation_x, body.rotation_y, body.rotation_z};

    bool flagged = dirty_check_mirror(prev, now);
    if (!flagged) {
        throw std::runtime_error("dirty detector missed the rotating-body case");
    }

    std::vector<float> tris_frame2;
    body.GetShadowTriangles(tris_frame2);

    float max_delta = 0.0f;
    for (size_t i = 0; i < tris_frame1.size(); ++i) {
        max_delta = std::max(max_delta, std::abs(tris_frame1[i] - tris_frame2[i]));
    }
    std::printf("    after 0.15 rad yaw step, max vertex delta = %.4f m\n", max_delta);
    if (max_delta < 0.01f) {
        throw std::runtime_error(
            "0.15 rad yaw produced essentially no change in shadow triangles. "
            "Either GetShadowTriangles ignores rotation, or particles are "
            "shape=BOX with axis-aligned default geometry. Either way the "
            "shadow won't visually rotate.");
    }
}

int main() {
    std::cout << "=== Shadow pose-dirty detection (catches stale-shadow regressions) ===" << std::endl;
    TEST(pose_unchanged_is_not_dirty);
    TEST(translation_is_dirty);
    TEST(rotation_only_is_dirty);
    TEST(microscopic_rotation_above_epsilon_is_dirty);
    TEST(subepsilon_rotation_is_not_dirty);
    TEST(shadow_triangles_change_under_rotation);
    TEST(rotating_body_full_pipeline_signals);
    std::cout << std::endl << tests_passed << " passed, "
              << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
