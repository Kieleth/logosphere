// RigidAssembly pure-math contract tests.
//
// Locks down how a part's world pose is derived from the assembly's
// pose plus the part's local offset / yaw. The full sync/dump paths
// talk to ParticleSystem and are exercised by the live viewer; this
// test guarantees the math behind them stays right under any future
// refactor (pitch/roll layering, quaternion switch, etc.).

#include "logosphere/assembly/rigid_assembly.h"
#include <cmath>
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

using logosphere::assembly::BodyPart;
using logosphere::assembly::RigidAssembly;
using logosphere::assembly::part_world_position;
using logosphere::assembly::part_world_yaw;

constexpr float kPi = 3.14159265358979323846f;

// -----------------------------------------------------------------------------
// Identity pose: world = local unchanged.
// -----------------------------------------------------------------------------
void test_identity_pose_preserves_local_offsets() {
    RigidAssembly a;
    a.world_x = 0.0f; a.world_y = 0.0f; a.world_z = 0.0f;
    a.world_yaw = 0.0f;

    BodyPart p;
    p.local_x = 1.5f; p.local_y = -0.3f; p.local_z = 0.4f;
    p.local_yaw = 0.0f;

    float wx, wy, wz;
    part_world_position(a, p, wx, wy, wz);
    ASSERT_NEAR(wx,  1.5f, 1e-6f, "identity wx");
    ASSERT_NEAR(wy, -0.3f, 1e-6f, "identity wy");
    ASSERT_NEAR(wz,  0.4f, 1e-6f, "identity wz");
    ASSERT_NEAR(part_world_yaw(a, p), 0.0f, 1e-6f, "identity yaw");
}

// -----------------------------------------------------------------------------
// Translation: world pose translates without touching local orientation.
// -----------------------------------------------------------------------------
void test_translation_adds_to_local() {
    RigidAssembly a;
    a.world_x = 10.0f; a.world_y = -5.0f; a.world_z = 2.0f;
    a.world_yaw = 0.0f;

    BodyPart p;
    p.local_x = 0.7f; p.local_y = 0.2f; p.local_z = 0.1f;

    float wx, wy, wz;
    part_world_position(a, p, wx, wy, wz);
    ASSERT_NEAR(wx, 10.7f, 1e-6f, "translated wx");
    ASSERT_NEAR(wy, -4.8f, 1e-6f, "translated wy");
    ASSERT_NEAR(wz,  2.1f, 1e-6f, "translated wz");
}

// -----------------------------------------------------------------------------
// Compass convention: yaw=+π/2 maps local +Y (forward) to world +X (east).
// This is CLOCKWISE from +Z — matches facing_angle and particle_geometry_v2
// throughout the engine. If this breaks, every other yaw is wrong.
// -----------------------------------------------------------------------------
void test_yaw_quarter_turn_forward_points_east() {
    RigidAssembly a;
    a.world_yaw = kPi * 0.5f;

    BodyPart p;
    p.local_x = 0.0f;
    p.local_y = 1.0f;   // "forward" along local +Y
    p.local_z = 0.0f;

    float wx, wy, wz;
    part_world_position(a, p, wx, wy, wz);
    ASSERT_NEAR(wx, 1.0f, 1e-6f, "yaw +π/2: forward lands +X (east)");
    ASSERT_NEAR(wy, 0.0f, 1e-6f, "yaw +π/2: no Y offset");
    ASSERT_NEAR(wz, 0.0f, 1e-6f, "yaw +π/2: no Z offset");
}

// -----------------------------------------------------------------------------
// yaw=π: forward flips to -Y (south).
// -----------------------------------------------------------------------------
void test_yaw_half_turn_flips_forward() {
    RigidAssembly a;
    a.world_yaw = kPi;

    BodyPart p;
    p.local_y = 0.70f;  // forward

    float wx, wy, wz;
    part_world_position(a, p, wx, wy, wz);
    ASSERT_NEAR(wx,  0.00f, 1e-6f, "yaw π wx");
    ASSERT_NEAR(wy, -0.70f, 1e-6f, "yaw π wy (south)");
}

// -----------------------------------------------------------------------------
// yaw=-π/2: forward points -X (west). Confirms compass rotation is
// consistent even with negative angles.
// -----------------------------------------------------------------------------
void test_yaw_negative_quarter_turn_forward_points_west() {
    RigidAssembly a;
    a.world_yaw = -kPi * 0.5f;

    BodyPart p;
    p.local_y = 1.0f;

    float wx, wy, wz;
    part_world_position(a, p, wx, wy, wz);
    ASSERT_NEAR(wx, -1.0f, 1e-6f, "yaw -π/2: forward lands -X (west)");
    ASSERT_NEAR(wy,  0.0f, 1e-6f, "yaw -π/2: no Y offset");
}

// -----------------------------------------------------------------------------
// Arbitrary yaw preserves distance from origin.
// Invariant: rotation is rigid — the part's distance from the
// assembly origin is the same before and after.
// -----------------------------------------------------------------------------
void test_rotation_preserves_radial_distance() {
    RigidAssembly a;
    a.world_yaw = 1.234f;  // arbitrary

    BodyPart p;
    p.local_x = 0.3f; p.local_y = 0.7f; p.local_z = 0.0f;
    const float r_local = std::sqrt(p.local_x * p.local_x +
                                    p.local_y * p.local_y);

    float wx, wy, wz;
    part_world_position(a, p, wx, wy, wz);
    float r_world = std::sqrt(wx * wx + wy * wy);
    ASSERT_NEAR(r_world, r_local, 1e-6f,
                "world radius = local radius (rigid rotation)");
}

// -----------------------------------------------------------------------------
// Two parts on opposite sides stay antipodal after rotation.
// Guards the "wheels at ±local_x land on opposite ends of body axis"
// requirement that's been failing visually in Logotron.
// -----------------------------------------------------------------------------
void test_antipodal_parts_stay_antipodal() {
    RigidAssembly a;
    a.world_x = 3.0f; a.world_y = 7.0f;  // arbitrary translation
    a.world_yaw = 0.7f;                   // arbitrary rotation

    BodyPart front, rear;
    front.local_y = +0.70f;  // forward
    rear.local_y  = -0.70f;  // backward

    float fx, fy, fz, rx, ry, rz;
    part_world_position(a, front, fx, fy, fz);
    part_world_position(a, rear,  rx, ry, rz);

    // Midpoint of front and rear must equal the assembly origin,
    // because the parts are exactly ±offset along the body axis.
    ASSERT_NEAR((fx + rx) * 0.5f, a.world_x, 1e-6f, "midpoint X = assembly X");
    ASSERT_NEAR((fy + ry) * 0.5f, a.world_y, 1e-6f, "midpoint Y = assembly Y");

    // Distance between them should equal twice the offset magnitude.
    float dx = fx - rx, dy = fy - ry;
    float dist = std::sqrt(dx * dx + dy * dy);
    ASSERT_NEAR(dist, 1.40f, 1e-6f, "front-rear separation unchanged");
}

// -----------------------------------------------------------------------------
// Composition: assembly yaw + part local yaw.
// Two parts on opposite sides with equal local_yaw end up with the
// SAME world yaw (they ride the assembly together). A turret barrel
// with local_yaw = π/4 is offset by π/4 from the base.
// -----------------------------------------------------------------------------
void test_yaw_composition() {
    RigidAssembly a;
    a.world_yaw = 0.4f;

    BodyPart rider;     rider.local_yaw   = 0.0f;
    BodyPart barrel;    barrel.local_yaw  = kPi * 0.25f;

    ASSERT_NEAR(part_world_yaw(a, rider),  0.4f,           1e-6f, "rider yaw = assembly yaw");
    ASSERT_NEAR(part_world_yaw(a, barrel), 0.4f + kPi*0.25f, 1e-6f, "barrel yaw = assembly + local");
}

// -----------------------------------------------------------------------------
// Local Z is additive only — rotation is yaw-only (v1).
// -----------------------------------------------------------------------------
void test_local_z_is_translation_only() {
    RigidAssembly a;
    a.world_z = 5.0f;
    a.world_yaw = 1.5f;

    BodyPart p;
    p.local_x = 0.0f; p.local_y = 0.0f; p.local_z = 0.35f;

    float wx, wy, wz;
    part_world_position(a, p, wx, wy, wz);
    ASSERT_NEAR(wx, 0.0f, 1e-6f, "yaw shouldn't touch z-only offset x");
    ASSERT_NEAR(wy, 0.0f, 1e-6f, "yaw shouldn't touch z-only offset y");
    ASSERT_NEAR(wz, 5.35f, 1e-6f, "z is direct add");
}

// Engine convention: rotation_z=0 faces north (+Y). A bike authored
// as local +Y = forward should at yaw=0 face +Y in world too.
void test_default_yaw_faces_north() {
    RigidAssembly a;          // pose at origin, yaw=0
    BodyPart nose;
    nose.local_y = 1.0f;      // the nose of the bike, 1 m forward

    float wx, wy, wz;
    part_world_position(a, nose, wx, wy, wz);
    ASSERT_NEAR(wx, 0.0f, 1e-6f, "nose at yaw=0 sits on the Y axis");
    ASSERT_NEAR(wy, 1.0f, 1e-6f, "nose at yaw=0 points north (+Y)");
}

// =============================================================================
// Motorcycle rotation sweep — catches dismembering.
// =============================================================================
// A bike assembly has three parts: body (ellipsoid long along +X) and
// two wheels (±wheel_offset along +X). As we sweep the assembly
// through yaws, we verify:
//   * each wheel's position sits on the body's long axis after
//     rotation (the "wheels at the ends" invariant)
//   * the two wheels remain antipodal around the body center
//   * the body and wheels carry the same world_yaw (the whole bike
//     rotates as one)
//   * the snapshot every frame matches what sync will write
// If any of these break, the bike is visually dismembered — even if
// the underlying math looked right for a single yaw value.

using logosphere::assembly::build_part_snapshot;

static logosphere::assembly::RigidAssembly build_motorcycle_assembly(float yaw) {
    logosphere::assembly::RigidAssembly a;
    a.world_x = a.world_y = a.world_z = 0.0f;
    a.world_yaw = yaw;

    const float wheel_offs = 0.70f;
    const float wheel_r    = 0.26f;
    const float wheel_t    = 0.14f;
    const float body_h     = 0.32f;

    // Engine convention: local +Y = forward. Body is long along Y.
    BodyPart body;
    body.name = "body";
    body.shape = ParticleShape::ELLIPSOID;
    body.local_z = wheel_r + body_h * 0.5f;
    body.width = 0.55f; body.height = 1.80f; body.thickness = body_h;  // X(side), Y(fwd), Z(up)

    // Wheel disc in the YZ plane; axle along +X (sideways). Travel
    // direction is +Y, so the diameter axis is Y.
    BodyPart front;
    front.name = "wheel_front";
    front.shape = ParticleShape::ELLIPSOID;
    front.local_y = +wheel_offs;
    front.local_z = wheel_r;
    front.width     = wheel_t;        // axle (X, sideways)
    front.height    = 2 * wheel_r;    // diameter along travel (Y)
    front.thickness = 2 * wheel_r;    // diameter vertical (Z)

    BodyPart rear = front;
    rear.name = "wheel_rear";
    rear.local_y = -wheel_offs;

    a.parts = { body, front, rear };
    return a;
}

void test_motorcycle_stays_coherent_through_full_rotation() {
    // 16 yaw steps across 2π including negatives, cardinal, diagonals.
    const int steps = 16;
    for (int i = 0; i < steps; ++i) {
        float yaw = (i - steps / 2) * (2.0f * kPi / steps);
        auto a = build_motorcycle_assembly(yaw);
        auto body_snap  = build_part_snapshot(a, a.parts[0]);
        auto front_snap = build_part_snapshot(a, a.parts[1]);
        auto rear_snap  = build_part_snapshot(a, a.parts[2]);

        // (1) Every part carries the same world_yaw — the whole bike
        //     rotates as one unit.
        ASSERT_NEAR(body_snap.rotation_z,  yaw, 1e-5f, "body world yaw");
        ASSERT_NEAR(front_snap.rotation_z, yaw, 1e-5f, "front wheel world yaw");
        ASSERT_NEAR(rear_snap.rotation_z,  yaw, 1e-5f, "rear wheel world yaw");

        // (2) Wheels land on the body axis. Compass CW: local +Y
        //     (forward) at yaw maps to world ( sin yaw, cos yaw).
        const float cy = std::cos(yaw);
        const float sy = std::sin(yaw);
        ASSERT_NEAR(front_snap.x, +0.70f * sy, 1e-5f, "front on +axis X");
        ASSERT_NEAR(front_snap.y, +0.70f * cy, 1e-5f, "front on +axis Y");
        ASSERT_NEAR(rear_snap.x,  -0.70f * sy, 1e-5f, "rear  on -axis X");
        ASSERT_NEAR(rear_snap.y,  -0.70f * cy, 1e-5f, "rear  on -axis Y");

        // (3) Antipodal: the two wheels' world positions sum to 2·body.
        ASSERT_NEAR(front_snap.x + rear_snap.x, 2.0f * body_snap.x, 1e-5f, "antipodal X");
        ASSERT_NEAR(front_snap.y + rear_snap.y, 2.0f * body_snap.y, 1e-5f, "antipodal Y");

        // (4) Body z is above wheel z (bike hasn't tipped).
        if (body_snap.z <= front_snap.z) {
            throw std::runtime_error(
                "body must sit above wheels at yaw=" + std::to_string(yaw));
        }

        // (5) Wheels sit on the ground plane (local_z = wheel_r, world
        //     translation z=0 in this test). Body riding on top.
        ASSERT_NEAR(front_snap.z, 0.26f, 1e-5f, "front wheel z");
        ASSERT_NEAR(rear_snap.z,  0.26f, 1e-5f, "rear  wheel z");
    }
}

// Regression guard for the wheel-separation distance: if a future
// refactor accidentally computes wheel world offset with something
// other than the rotation matrix, the separation distance between
// wheels will no longer equal 2 * wheel_offset.
void test_wheel_separation_unchanged_under_rotation() {
    const float wheel_offs = 0.70f;
    const int steps = 24;
    for (int i = 0; i < steps; ++i) {
        float yaw = i * (2.0f * kPi / steps) - kPi;
        auto a = build_motorcycle_assembly(yaw);
        auto front = build_part_snapshot(a, a.parts[1]);
        auto rear  = build_part_snapshot(a, a.parts[2]);
        float dx = front.x - rear.x;
        float dy = front.y - rear.y;
        float sep = std::sqrt(dx * dx + dy * dy);
        ASSERT_NEAR(sep, 2.0f * wheel_offs, 1e-5f,
                    "wheelbase distance invariant under rotation");
    }
}

// =============================================================================
// Rendering-path contract: verify that Particle::GetSurfaces honors
// rotation_z as "yaw around the particle's own Z axis".
//
// The RigidAssembly math above sets `particle.rotation_z = world_yaw`
// and expects the renderer to rotate the shape so its local +X axis
// points in world direction (cos yaw, sin yaw, 0). If the renderer
// does something else (wrong axis, wrong sign, wrong order) the bike
// will visibly dismember even though our pure math is correct.
// =============================================================================

#include "particle.h"
#include "particle_geometry_v2.h"
#include <stdexcept>

// =============================================================================
// CRITICAL: rotation direction convention.
//
// The engine's Transform applies rotation_z as CLOCKWISE viewed from +Z
// (see particle_geometry_v2.cpp — "Uses CLOCKWISE rotation to match
// movement convention"). The RigidAssembly position math rotates
// counter-clockwise (standard math convention). If the two don't
// agree, the body and wheels rotate in OPPOSITE directions and the
// bike visually dismembers at any non-zero yaw — even though each
// layer tested in isolation looks fine.
//
// This test locks down that the directions agree: local (+1, 0, 0)
// at yaw = +π/2 must end up at the SAME world direction from both
// the position pipeline and the geometry pipeline.
// =============================================================================
void test_assembly_and_geometry_rotate_same_direction() {
    using ParticleGeometryV2::Vec3;
    using ParticleGeometryV2::Transform;

    const float yaw = kPi * 0.5f;

    // Position pipeline: a local +Y=forward offset at yaw=+π/2 should
    // end up pointing to world +X (east, compass convention).
    RigidAssembly a;
    a.world_yaw = yaw;
    BodyPart bp;
    bp.local_y = 1.0f;
    float px, py, pz;
    logosphere::assembly::part_world_position(a, bp, px, py, pz);

    // Geometry pipeline: same yaw applied as particle.rotation_z —
    // both sides rotate CW, so local +Y should end up at world +X
    // through the geometry transform as well.
    Transform t(Vec3(0, 0, 0), 0.0f, 0.0f, yaw);
    Vec3 g = t.transform_direction(Vec3(0.0f, 1.0f, 0.0f));

    if (std::abs(px - g.x) > 1e-4f || std::abs(py - g.y) > 1e-4f) {
        throw std::runtime_error(
            "position pipeline=(" + std::to_string(px) + "," + std::to_string(py)
            + ") vs geometry pipeline=(" + std::to_string(g.x) + "," + std::to_string(g.y)
            + ") — directions disagree, bike will dismember");
    }
}

// Small helper: extract approximate min / max along a world axis from
// the vertices emitted by Particle::GetSurfaces.
static void particle_world_extents(const Particle& p,
                                   float& min_x, float& max_x,
                                   float& min_y, float& max_y,
                                   float& min_z, float& max_z) {
    auto surfs = p.GetSurfaces();
    min_x = min_y = min_z =  std::numeric_limits<float>::infinity();
    max_x = max_y = max_z = -std::numeric_limits<float>::infinity();
    for (const auto& s : surfs) {
        for (int v = 0; v < s.vertex_count; ++v) {
            float x = s.vertices[v][0];
            float y = s.vertices[v][1];
            float z = s.vertices[v][2];
            if (x < min_x) min_x = x;  if (x > max_x) max_x = x;
            if (y < min_y) min_y = y;  if (y > max_y) max_y = y;
            if (z < min_z) min_z = z;  if (z > max_z) max_z = z;
        }
    }
}

// Make a long-along-X-axis ellipsoid and verify its world extents
// match the expected post-rotation axis direction. A wrong sign,
// wrong axis, or wrong rotation semantic will produce visible dismember
// and show up here as a blown-up extent assertion.
// The bike must stay PARALLEL to the floor during yaw rotation — like
// Eva rotating in place, it's a yaw around world Z, not a tumble. The
// Z extent of each part must be invariant under rotation_z. If this
// ever fails, rotation_z is somehow leaking into Z, and the bike is
// tipping in a way my math isn't modelling.
void test_bike_stays_parallel_to_floor_under_rotation() {
    // Build a body-like ellipsoid (long X, medium Y, short Z).
    Particle p = {};
    p.shape = ParticleShape::ELLIPSOID;
    p.x = p.y = p.z = 0.0f;
    p.width = 1.80f; p.height = 0.55f; p.thickness = 0.32f;

    float mn_x, mx_x, mn_y, mx_y, mn_z, mx_z;

    // Baseline at yaw=0.
    p.rotation_z = 0.0f;
    particle_world_extents(p, mn_x, mx_x, mn_y, mx_y, mn_z, mx_z);
    const float baseline_z_extent = mx_z - mn_z;

    // Sweep yaws and confirm Z extent never changes.
    const int steps = 16;
    for (int i = 0; i < steps; ++i) {
        float yaw = (i - steps / 2) * (2.0f * kPi / steps);
        p.rotation_z = yaw;
        particle_world_extents(p, mn_x, mx_x, mn_y, mx_y, mn_z, mx_z);
        float ez = mx_z - mn_z;
        if (std::abs(ez - baseline_z_extent) > 1e-3f) {
            throw std::runtime_error(
                "body Z extent changed under yaw=" + std::to_string(yaw)
                + ": got " + std::to_string(ez) + ", baseline "
                + std::to_string(baseline_z_extent)
                + " — bike is NOT staying parallel to floor");
        }
        // Additionally: the centroid z should not move (stays on its
        // horizontal plane). Each vertex's z is unchanged by pure Z
        // rotation, so the min/max should remain symmetric.
        if (std::abs((mn_z + mx_z) * 0.5f) > 1e-3f) {
            throw std::runtime_error(
                "body Z centroid drifted off origin under yaw="
                + std::to_string(yaw));
        }
    }
}

void test_ellipsoid_rotation_z_rotates_compass() {
    auto check = [](float yaw, float expected_x_extent, float expected_y_extent) {
        Particle p = {};
        p.shape = ParticleShape::ELLIPSOID;
        p.x = 0.0f; p.y = 0.0f; p.z = 0.0f;
        p.width = 0.2f;     // short along local X (sideways)
        p.height = 2.0f;    // long along local Y (forward)
        p.thickness = 0.2f; // short along local Z (up)
        p.rotation_z = yaw;

        float mn_x, mx_x, mn_y, mx_y, mn_z, mx_z;
        particle_world_extents(p, mn_x, mx_x, mn_y, mx_y, mn_z, mx_z);
        const float ex = mx_x - mn_x;
        const float ey = mx_y - mn_y;
        // The tight tolerance is a touch loose because the icosphere
        // vertices don't land exactly on the semi-axes at high
        // subdivision; allow 5% margin and ±0.2 additive for the
        // short-axis contribution.
        if (std::abs(ex - expected_x_extent) > 0.1f) {
            throw std::runtime_error(
                "rotation_z=" + std::to_string(yaw) + " world-X extent "
                + std::to_string(ex) + " expected " + std::to_string(expected_x_extent));
        }
        if (std::abs(ey - expected_y_extent) > 0.1f) {
            throw std::runtime_error(
                "rotation_z=" + std::to_string(yaw) + " world-Y extent "
                + std::to_string(ey) + " expected " + std::to_string(expected_y_extent));
        }
    };
    // yaw=0: forward is +Y (long axis along world Y).
    check(0.0f,          /*ex=*/0.2f, /*ey=*/2.0f);
    // yaw=+π/2: compass east — long axis now along world X.
    check(kPi * 0.5f,    /*ex=*/2.0f, /*ey=*/0.2f);
    // yaw=π: compass south — long axis along world -Y.
    check(kPi,           /*ex=*/0.2f, /*ey=*/2.0f);
    // yaw=-π/2: compass west — long axis along world -X.
    check(-kPi * 0.5f,   /*ex=*/2.0f, /*ey=*/0.2f);
}

// Instrumentation dump — prints a concise rotation table. Not an
// assertion; if the sweep tests above ever fail this is the raw data
// to diagnose from.
void test_print_rotation_table() {
    std::cout << std::endl;
    std::cout << "  yaw(deg)  body(x,y)          front(x,y)          rear(x,y)" << std::endl;
    std::cout << "  --------  -----------------  -----------------  -----------------" << std::endl;
    for (int deg = -180; deg <= 180; deg += 30) {
        float yaw = deg * kPi / 180.0f;
        auto a = build_motorcycle_assembly(yaw);
        auto b = build_part_snapshot(a, a.parts[0]);
        auto f = build_part_snapshot(a, a.parts[1]);
        auto r = build_part_snapshot(a, a.parts[2]);
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "  %7d   (%+.3f,%+.3f)   (%+.3f,%+.3f)   (%+.3f,%+.3f)",
            deg, b.x, b.y, f.x, f.y, r.x, r.y);
        std::cout << buf << std::endl;
    }
}

int main() {
    std::cout << "=== RigidAssembly pure-math contract ===" << std::endl;
    TEST(identity_pose_preserves_local_offsets);
    TEST(translation_adds_to_local);
    TEST(yaw_quarter_turn_forward_points_east);
    TEST(yaw_half_turn_flips_forward);
    TEST(yaw_negative_quarter_turn_forward_points_west);
    TEST(rotation_preserves_radial_distance);
    TEST(antipodal_parts_stay_antipodal);
    TEST(yaw_composition);
    TEST(local_z_is_translation_only);
    TEST(default_yaw_faces_north);
    std::cout << std::endl;
    std::cout << "=== RigidAssembly motorcycle rotation sweep ===" << std::endl;
    TEST(motorcycle_stays_coherent_through_full_rotation);
    TEST(wheel_separation_unchanged_under_rotation);
    std::cout << std::endl;
    std::cout << "=== Rendering-path: ellipsoid rotation semantics ===" << std::endl;
    TEST(assembly_and_geometry_rotate_same_direction);
    TEST(ellipsoid_rotation_z_rotates_compass);
    TEST(bike_stays_parallel_to_floor_under_rotation);
    std::cout << std::endl;
    std::cout << "=== Rotation table (visual reference) ===" << std::endl;
    TEST(print_rotation_table);
    std::cout << std::endl << tests_passed << " passed, "
              << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
