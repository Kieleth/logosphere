// =============================================================================
// REVERSE LEG CHAIN — unit roundtrip test for reverse_leg_chain_to_hip
// =============================================================================
// RED→GREEN driver for Stage 1 of the kinematic-root refactor.
//
// The abstraction: during stance phase, the kinematic root is the planted
// foot. The hip's world position is derived by walking the leg chain
// UPWARD from foot → ankle → knee → hip. That's the inverse of the
// existing rebuild_leg_chain (which goes hip → knee → ankle → foot).
//
// Invariant: forward then reverse must be identity.
//   hip_in → rebuild_leg_chain → foot_out
//   foot_out → reverse_leg_chain_to_hip → hip_out
//   |hip_out - hip_in| must be < 1e-5 m across a range of rotations.
//
// If reverse_leg_chain_to_hip doesn't exist yet, this test fails to link.
// If it exists but has the wrong math, the roundtrip diverges.
//
// Run: ./build/logosphere-tests --test test_reverse_leg_chain --no-head
// =============================================================================

#include "logosphere/dynamics/two_bone_ik.h"
// two_bone_ik.h pulls in math/mat4.h (Vec3), math/quat.h, math/transform.h.
#include <cstdio>
#include <cmath>

using logosphere::Vec3;
using logosphere::Quat;
using logosphere::LegJointOffsets;
using logosphere::LegChainPositions;

bool test_reverse_leg_chain() {
    printf("\n=== Reverse Leg Chain (roundtrip) ===\n");

    // Realistic Eva-ish leg offsets (from humanoid_generator conventions):
    //   bones point along +Z in local frame; child_offset is +half_length*Z,
    //   pivot_offset is -half_length*Z. Numbers don't have to match Eva
    //   exactly — we only need the forward/reverse math to be self-consistent.
    LegJointOffsets o;
    o.hip_child   = {0.0f, 0.0f,  0.22f};   // thigh pivot-offset from hip joint
    o.knee_pivot  = {0.0f, 0.0f, -0.22f};   // thigh's knee-joint pivot
    o.knee_child  = {0.0f, 0.0f,  0.20f};   // shin pivot-offset from knee
    o.ankle_pivot = {0.0f, 0.0f, -0.20f};
    o.ankle_child = {0.0f, 0.0f,  0.05f};   // foot offset from ankle
    o.toe_pivot   = {0.0f, 0.0f, -0.05f};
    o.toe_child   = {0.0f, 0.0f,  0.04f};

    // A bank of rotations covering rest, mid-stance bend, heel-strike, toe-off,
    // plus a generic off-axis rotation to make sure we're not accidentally
    // right for the simple cases only.
    struct Case {
        const char* name;
        Quat thigh;
        Quat shin;
        Quat foot;
        Quat toe;
    };
    Case cases[] = {
        { "rest",
          Quat::identity(), Quat::identity(),
          Quat::identity(), Quat::identity() },
        { "knee flex 30",
          Quat::from_axis_angle(1, 0, 0, 0.3f),
          Quat::from_axis_angle(1, 0, 0, -0.6f),
          Quat::identity(), Quat::identity() },
        { "knee flex 60",
          Quat::from_axis_angle(1, 0, 0, 0.6f),
          Quat::from_axis_angle(1, 0, 0, -1.2f),
          Quat::identity(), Quat::identity() },
        { "full leg twist",
          Quat::from_axis_angle(0, 0, 1, 0.5f),
          Quat::from_axis_angle(0, 0, 1, 0.5f),
          Quat::from_axis_angle(0, 0, 1, 0.5f),
          Quat::from_axis_angle(0, 0, 1, 0.5f) },
        { "mixed off-axis",
          Quat::from_axis_angle(0.577f, 0.577f, 0.577f, 0.4f),
          Quat::from_axis_angle(1, 0, 0, -0.3f),
          Quat::from_axis_angle(0, 1, 0, 0.2f),
          Quat::from_axis_angle(0, 0, 1, 0.1f) },
    };

    Vec3 hip_in = { 1.234f, -5.678f, 0.912f };
    constexpr float TOL = 1e-5f;

    bool ok = true;
    for (const auto& c : cases) {
        LegChainPositions fwd = logosphere::rebuild_leg_chain(
            hip_in, c.thigh, c.shin, c.foot, c.toe, o);

        Vec3 hip_out = logosphere::reverse_leg_chain_to_hip(
            fwd.foot_center, c.thigh, c.shin, c.foot, o);

        float dx = hip_out.x - hip_in.x;
        float dy = hip_out.y - hip_in.y;
        float dz = hip_out.z - hip_in.z;
        float err = std::sqrt(dx*dx + dy*dy + dz*dz);

        printf("  %-18s  hip_in=(%.4f,%.4f,%.4f)  hip_out=(%.4f,%.4f,%.4f)  err=%.2e\n",
               c.name,
               hip_in.x, hip_in.y, hip_in.z,
               hip_out.x, hip_out.y, hip_out.z,
               err);

        if (err > TOL) {
            printf("    FAIL: err %.2e > tol %.1e\n", err, TOL);
            ok = false;
        }
    }

    printf("  %s\n", ok ? "[PASS]" : "[FAIL]");
    return ok;
}
