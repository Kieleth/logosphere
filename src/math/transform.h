// Transform - Position + Rotation
//
// A Transform represents a rigid body transformation: position + rotation.
// This is the fundamental unit for skeletal animation.
//
// Key insight for FK (Forward Kinematics):
//   World_Transform(child) = World_Transform(parent) * Local_Transform(child)
//
// Instead of manually computing rotations with scattered trigonometry,
// we just compose transforms via multiplication. The math handles rotation
// order automatically.
//
// Usage in animation:
//   1. Each joint stores local_transform (relative to parent)
//   2. Animation modifies local_transform.rotation (via axis-angle)
//   3. FK computes world_transform = parent.world * child.local
//   4. Particle position = world_transform.position
//   5. Particle rotation = world_transform.rotation (as Euler or quaternion)

#pragma once

#include "quat.h"
#include "mat4.h"

namespace logosphere {

struct Transform {
    Vec3 position;
    Quat rotation;

    // =========================================================================
    // Identity Transform: No translation, no rotation
    // =========================================================================
    static Transform identity() {
        return {Vec3::zero(), Quat::identity()};
    }

    // =========================================================================
    // From Position Only: Translation with no rotation
    // =========================================================================
    static Transform from_position(float x, float y, float z) {
        return {Vec3{x, y, z}, Quat::identity()};
    }

    static Transform from_position(const Vec3& pos) {
        return {pos, Quat::identity()};
    }

    // =========================================================================
    // From Rotation Only: Rotation with no translation
    // =========================================================================
    static Transform from_rotation(const Quat& rot) {
        return {Vec3::zero(), rot};
    }

    // =========================================================================
    // To Matrix: Convert to 4x4 transformation matrix
    // =========================================================================
    Mat4 to_matrix() const {
        return Mat4::from_transform(position, rotation);
    }

    // =========================================================================
    // Compose Transforms: parent * child
    //
    // This is THE key operation for FK animation.
    // Result = apply child transform, then parent transform.
    //
    // For a joint hierarchy:
    //   shoulder.world = entity.world * shoulder.local
    //   elbow.world = shoulder.world * elbow.local
    //   wrist.world = elbow.world * wrist.local
    //
    // No manual trigonometry! Matrix math handles rotation composition.
    // =========================================================================
    Transform operator*(const Transform& child) const {
        // Compose rotations (quaternion multiplication)
        Quat combined_rot = rotation * child.rotation;

        // Transform child's position by parent's rotation, then add parent's position
        Vec3 rotated_child_pos;
        rotation.rotate_vector(child.position.x, child.position.y, child.position.z,
                               rotated_child_pos.x, rotated_child_pos.y, rotated_child_pos.z);

        Vec3 combined_pos = position + rotated_child_pos;

        return {combined_pos, combined_rot};
    }

    // =========================================================================
    // Transform Point: Apply transform to a local-space point
    //
    // Returns the point in world space.
    // =========================================================================
    Vec3 transform_point(const Vec3& local) const {
        Vec3 rotated;
        rotation.rotate_vector(local.x, local.y, local.z,
                               rotated.x, rotated.y, rotated.z);
        return position + rotated;
    }

    // =========================================================================
    // Transform Direction: Apply rotation only (no translation)
    //
    // For normals, velocities, bone directions.
    // =========================================================================
    Vec3 transform_direction(const Vec3& dir) const {
        Vec3 rotated;
        rotation.rotate_vector(dir.x, dir.y, dir.z,
                               rotated.x, rotated.y, rotated.z);
        return rotated;
    }

    // =========================================================================
    // Inverse: Compute inverse transform
    //
    // If T transforms from A to B, inverse(T) transforms from B to A.
    // =========================================================================
    Transform inverse() const {
        Quat inv_rot = rotation.conjugate();

        // Inverse position: -R^(-1) * p
        Vec3 neg_pos = position * -1.0f;
        Vec3 inv_pos;
        inv_rot.rotate_vector(neg_pos.x, neg_pos.y, neg_pos.z,
                              inv_pos.x, inv_pos.y, inv_pos.z);

        return {inv_pos, inv_rot};
    }

    // =========================================================================
    // Interpolate: Blend between two transforms
    //
    // Uses linear interpolation for position, SLERP for rotation.
    // Result is smooth transition from a (t=0) to b (t=1).
    // =========================================================================
    static Transform lerp(const Transform& a, const Transform& b, float t) {
        // Linear interpolation for position
        Vec3 pos = {
            a.position.x + t * (b.position.x - a.position.x),
            a.position.y + t * (b.position.y - a.position.y),
            a.position.z + t * (b.position.z - a.position.z)
        };

        // SLERP for rotation (smooth, constant angular velocity)
        Quat rot = Quat::slerp(a.rotation, b.rotation, t);

        return {pos, rot};
    }

    // =========================================================================
    // To Euler Angles: Extract rotation as Euler angles
    //
    // For compatibility with existing particle.rotation_x/y/z system.
    // Returns (pitch_x, yaw_y, roll_z) in radians.
    //
    // Note: Euler angles have gimbal lock and order ambiguity.
    // Use quaternions internally, convert to Euler only for display.
    // =========================================================================
    void to_euler(float& out_x, float& out_y, float& out_z) const {
        // Convert quaternion to rotation matrix first
        float mat[9];
        rotation.to_matrix3x3(mat);

        // Extract Euler angles from rotation matrix (ZYX decomposition: R = Rz * Ry * Rx)
        //
        // to_matrix3x3() is ROW-MAJOR: mat[row * 3 + col]
        //   mat[0]=R[0][0]  mat[1]=R[0][1]  mat[2]=R[0][2]
        //   mat[3]=R[1][0]  mat[4]=R[1][1]  mat[5]=R[1][2]
        //   mat[6]=R[2][0]  mat[7]=R[2][1]  mat[8]=R[2][2]
        //
        // ZYX decomposition elements:
        //   R[0][0] = cy*cz  → mat[0]
        //   R[1][0] = cy*sz  → mat[3]
        //   R[2][0] = -sy    → mat[6]
        //   R[2][1] = cy*sx  → mat[7]
        //   R[2][2] = cy*cx  → mat[8]

        float sy = -mat[6];

        if (std::abs(sy) < 0.9999f) {
            out_y = std::asin(sy);                  // Yaw
            out_x = std::atan2(mat[7], mat[8]);     // Pitch
            // Negate Z: renderer uses CW Z rotation convention
            out_z = -std::atan2(mat[3], mat[0]);    // Roll (negated for CW)
        } else {
            // Gimbal lock: sy = ±1
            out_y = (sy > 0) ? static_cast<float>(M_PI / 2.0) : static_cast<float>(-M_PI / 2.0);
            out_x = 0.0f;
            out_z = -std::atan2(-mat[1], mat[4]);   // Negated for CW
        }
    }
};

} // namespace logosphere
