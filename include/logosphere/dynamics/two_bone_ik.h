// Two-Bone IK Solver + Leg Chain Reconstruction
//
// Solves inverse kinematics for a 2-bone chain (thigh + shin)
// and reconstructs particle positions to maintain joint connectivity.
//
// Used by foot planting to lock the stance foot in world space
// while the body advances. The chain is rebuilt from rotations each
// frame, guaranteeing connectivity at every blend value.
//
// The solver is pure math — reusable for any 2-bone chain
// (arms, tentacles, etc).
//
// Algorithm:
//   1. Law of cosines finds the knee circle radius
//   2. Pole hint selects which point on the circle
//   3. Chain positions are reconstructed from rotations (never independent)
//
// Bone direction convention: local (0,0,-1) points from parent pivot
// toward child pivot (matching FK's child_offset in +Z, pivot_offset in -Z).

#ifndef TWO_BONE_IK_H
#define TWO_BONE_IK_H

#include "math/mat4.h"
#include "math/quat.h"
#include "math/transform.h"
#include <cmath>
#include <algorithm>

namespace logosphere {

// ========================================================================
// Helpers
// ========================================================================

// Rotate a Vec3 by a quaternion. Copies the quat to avoid const issues
// with rotate_vector's internal matrix computation.
inline Vec3 quat_rotate(Quat q, const Vec3& v) {
    float ox, oy, oz;
    q.rotate_vector(v.x, v.y, v.z, ox, oy, oz);
    return {ox, oy, oz};
}

// ========================================================================
// Two-Bone IK Solver
// ========================================================================

struct TwoBoneIKResult {
    Vec3 mid;          // Knee/elbow pivot position (world space)
    bool reachable;    // Was the target within the chain's reach?
};

// Solve 2-bone IK: given a fixed root and desired target, find the
// mid-point (knee/elbow) that satisfies both bone lengths.
//
// root:       Fixed end (hip pivot, world space)
// target:     Desired end (ankle pivot, world space)
// upper_len:  Upper bone length (hip pivot → knee pivot distance)
// lower_len:  Lower bone length (knee pivot → ankle pivot distance)
// pole_hint:  Direction the knee should bend toward (world space)
//
// Returns: mid-point position and reachability flag.
// When unreachable, mid is placed at fully extended position.
inline TwoBoneIKResult solve_two_bone_ik(
    const Vec3& root,
    const Vec3& target,
    float upper_len,
    float lower_len,
    const Vec3& pole_hint)
{
    Vec3 to_target = target - root;
    float d = to_target.length();

    // Degenerate: root and target coincide
    if (d < 0.0001f) {
        return {{root.x, root.y, root.z - upper_len}, false};
    }

    Vec3 axis = to_target * (1.0f / d);

    float max_reach = upper_len + lower_len;
    float min_reach = std::fabs(upper_len - lower_len);
    bool reachable = (d <= max_reach && d >= min_reach);

    // If beyond reach, extend fully — knee along root→target axis
    // with a tiny offset for stability. No clamping needed.
    if (d >= max_reach) {
        // Project pole_hint for the minimal-bend direction
        float pole_dot = pole_hint.dot(axis);
        Vec3 pole_proj = pole_hint - axis * pole_dot;
        float pole_len = pole_proj.length();
        Vec3 pole_dir;
        if (pole_len > 0.001f) {
            pole_dir = pole_proj * (1.0f / pole_len);
        } else {
            Vec3 up = {0, 0, 1};
            pole_proj = up - axis * up.dot(axis);
            pole_dir = pole_proj.normalized();
        }
        Vec3 mid = root + axis * upper_len + pole_dir * 0.005f;
        return {mid, false};
    }

    // Clamp min only (folded leg case)
    float d_clamped = std::max(d, min_reach + 0.0001f);

    // Law of cosines: projection of upper bone onto root→target axis
    float d_along = (upper_len * upper_len + d_clamped * d_clamped
                     - lower_len * lower_len) / (2.0f * d_clamped);

    // Radius of the knee circle (perpendicular to root→target line)
    float r_sq = upper_len * upper_len - d_along * d_along;
    float r = (r_sq > 0.0f) ? std::sqrt(r_sq) : 0.0f;

    // Center of the knee circle
    Vec3 circle_center = root + axis * d_along;

    // Project pole_hint onto plane perpendicular to axis
    float pole_dot = pole_hint.dot(axis);
    Vec3 pole_proj = pole_hint - axis * pole_dot;
    float pole_len = pole_proj.length();

    Vec3 pole_dir;
    if (pole_len > 0.001f) {
        pole_dir = pole_proj * (1.0f / pole_len);
    } else {
        // Fallback: try world up, then world right
        Vec3 up = {0, 0, 1};
        pole_proj = up - axis * up.dot(axis);
        pole_len = pole_proj.length();
        if (pole_len > 0.001f) {
            pole_dir = pole_proj * (1.0f / pole_len);
        } else {
            Vec3 right = {1, 0, 0};
            pole_proj = right - axis * right.dot(axis);
            pole_dir = pole_proj.normalized();
        }
    }

    Vec3 mid = circle_center + pole_dir * r;
    return {mid, reachable};
}

// ========================================================================
// Bone Helpers
// ========================================================================

// Compute bone length from joint offsets on the SAME particle.
// The bone spans from parent's pivot to child's pivot, passing through
// the particle center.
//
// parent_child_offset: child_offset of the joint connecting parent → this bone
// this_pivot_offset:   pivot_offset of the joint connecting this bone → next
//
// Example: thigh bone = |knee.pivot_offset - hip.child_offset|
//          (both defined on the thigh particle)
inline float compute_bone_length(const Vec3& parent_child_offset,
                                 const Vec3& this_pivot_offset) {
    Vec3 bone_vec = this_pivot_offset - parent_child_offset;
    return bone_vec.length();
}

// Compute world rotation for a bone between two pivot points.
// In local space, the bone direction is (0,0,-1) (from child_offset +Z
// toward pivot_offset -Z). Returns the minimal rotation (no twist).
inline Quat bone_rotation_from_pivots(const Vec3& upper_pivot,
                                      const Vec3& lower_pivot) {
    Vec3 dir = lower_pivot - upper_pivot;
    float len = dir.length();
    if (len < 0.0001f) return Quat::identity();
    dir = dir * (1.0f / len);
    return Quat::from_two_vectors(0, 0, -1, dir.x, dir.y, dir.z);
}

// Like bone_rotation_from_pivots, but carries `yaw` (radians, engine
// rotation_z convention) as the twist frame: result = yaw ∘ swing,
// with swing minimal INSIDE the yaw-local frame. Use whenever the
// result is slerp-blended or relative-diffed against yaw-carrying FK
// orientations. A no-twist world quat blended against a yawed one
// sends slerp down the short path SIDEWAYS for near-vertical bones,
// and a relative target against yawed hips commands counter-yaw —
// the "legs spin when the human rotates" bug (Eden, 2026-07-30).
inline Quat bone_rotation_from_pivots_yawed(const Vec3& upper_pivot,
                                            const Vec3& lower_pivot,
                                            float yaw) {
    Vec3 dir = lower_pivot - upper_pivot;
    float len = dir.length();
    Quat yaw_q = Quat::from_euler(0.0f, 0.0f, yaw);
    if (len < 0.0001f) return yaw_q;
    dir = dir * (1.0f / len);
    Vec3 local = quat_rotate(yaw_q.conjugate(), dir);
    return yaw_q *
           Quat::from_two_vectors(0, 0, -1, local.x, local.y, local.z);
}

// ========================================================================
// Leg Joint Offsets (bundle for clean API)
// ========================================================================

// All joint offsets needed to reconstruct a leg chain.
// Each offset is from the Joint struct's pivot_offset or child_offset.
// Named by which joint and which particle they're defined on.
struct LegJointOffsets {
    Vec3 hip_child;       // hip joint child_offset (on thigh particle)
    Vec3 knee_pivot;      // knee joint pivot_offset (on thigh particle)
    Vec3 knee_child;      // knee joint child_offset (on shin particle)
    Vec3 ankle_pivot;     // ankle joint pivot_offset (on shin particle)
    Vec3 ankle_child;     // ankle joint child_offset (on foot particle)
    Vec3 toe_pivot;       // toe joint pivot_offset (on foot particle)
    Vec3 toe_child;       // toe joint child_offset (on toe particle)
};

// ========================================================================
// Chain Reconstruction
// ========================================================================

struct LegChainPositions {
    Vec3 thigh_center;
    Vec3 shin_center;
    Vec3 foot_center;
    Vec3 toe_center;
};

// Rebuild a leg chain from hip pivot and world rotations.
//
// This applies the FK chain formula with specific rotations:
//   child_center = pivot - child_rot.rotate(child_offset)
//   next_pivot = child_center + child_rot.rotate(next_pivot_offset)
//
// Connectivity is guaranteed by construction — each position is
// computed from its parent, never independently.
//
// hip_pivot:   World-space hip joint pivot (from hips particle)
// thigh_rot:   World rotation for thigh particle
// shin_rot:    World rotation for shin particle
// foot_rot:    World rotation for foot particle (from FK, preserved)
// toe_rot:     World rotation for toe particle (from FK, preserved)
// offsets:     Joint offsets for the leg chain
inline LegChainPositions rebuild_leg_chain(
    const Vec3& hip_pivot,
    const Quat& thigh_rot,
    const Quat& shin_rot,
    const Quat& foot_rot,
    const Quat& toe_rot,
    const LegJointOffsets& o)
{
    LegChainPositions r;

    // Thigh: center below hip_pivot, knee_pivot below center
    r.thigh_center = hip_pivot - quat_rotate(thigh_rot, o.hip_child);
    Vec3 knee_pivot = r.thigh_center + quat_rotate(thigh_rot, o.knee_pivot);

    // Shin: center below knee_pivot, ankle_pivot below center
    r.shin_center = knee_pivot - quat_rotate(shin_rot, o.knee_child);
    Vec3 ankle_pivot = r.shin_center + quat_rotate(shin_rot, o.ankle_pivot);

    // Foot: positioned from ankle_pivot using FK rotation
    r.foot_center = ankle_pivot - quat_rotate(foot_rot, o.ankle_child);

    // Toe: from foot using FK rotation
    Vec3 toe_pivot = r.foot_center + quat_rotate(foot_rot, o.toe_pivot);
    r.toe_center = toe_pivot - quat_rotate(toe_rot, o.toe_child);

    return r;
}

// Extract Euler angles (CW Z convention) from a quaternion.
// Uses Transform::to_euler which handles the renderer convention.
inline void quat_to_euler(const Quat& q, float& rx, float& ry, float& rz) {
    Transform t = {{0, 0, 0}, q};
    t.to_euler(rx, ry, rz);
}

// ========================================================================
// Reverse chain: compute hip pivot from foot center (kinematic root)
// ========================================================================

// Inverse of rebuild_leg_chain from foot_center up to hip_pivot.
//
// During stance phase, the kinematic root is the planted foot. The hip's
// world position is a derived quantity: walk the chain backward through
// ankle → knee → hip using the current segment rotations. Toe is skipped
// (it hangs downstream of the foot and doesn't affect hip placement).
//
// foot_center: Known — the stance foot's world position.
// thigh_rot, shin_rot, foot_rot: Current world rotations of those segments.
// offsets:     Same leg offsets used by rebuild_leg_chain.
//
// Returns the world-space hip pivot position. The forward/reverse pair is
// exact-identity (up to fp): see tests/test_reverse_leg_chain.cpp.
inline Vec3 reverse_leg_chain_to_hip(
    const Vec3& foot_center,
    const Quat& thigh_rot,
    const Quat& shin_rot,
    const Quat& foot_rot,
    const LegJointOffsets& o)
{
    // Forward: foot_center = ankle_pivot - R_foot(ankle_child)
    Vec3 ankle_pivot  = foot_center + quat_rotate(foot_rot, o.ankle_child);
    // Forward: ankle_pivot = shin_center + R_shin(ankle_pivot_offset)
    Vec3 shin_center  = ankle_pivot - quat_rotate(shin_rot, o.ankle_pivot);
    // Forward: shin_center = knee_pivot - R_shin(knee_child)
    Vec3 knee_pivot   = shin_center + quat_rotate(shin_rot, o.knee_child);
    // Forward: knee_pivot = thigh_center + R_thigh(knee_pivot_offset)
    Vec3 thigh_center = knee_pivot - quat_rotate(thigh_rot, o.knee_pivot);
    // Forward: thigh_center = hip_pivot - R_thigh(hip_child)
    Vec3 hip_pivot    = thigh_center + quat_rotate(thigh_rot, o.hip_child);
    return hip_pivot;
}

} // namespace logosphere

#endif // TWO_BONE_IK_H
