// 4x4 Matrix Math - From Scratch
//
// Mat4 represents a 4x4 transformation matrix for 3D graphics.
// Used for combining translation, rotation, and scale into a single operation.
//
// Key insight: Matrix multiplication composes transforms automatically.
// Instead of manually applying rotations in different orders and getting
// inconsistent results, we just multiply matrices: World = Parent * Local
//
// Layout: Column-major (OpenGL/Metal convention)
//   Memory: [m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15]
//   Logical:
//     | m0  m4  m8  m12 |   | Xx  Yx  Zx  Tx |
//     | m1  m5  m9  m13 | = | Xy  Yy  Zy  Ty |
//     | m2  m6  m10 m14 |   | Xz  Yz  Zz  Tz |
//     | m3  m7  m11 m15 |   | 0   0   0   1  |
//
// Where (Xx,Xy,Xz) = X-axis direction, (Tx,Ty,Tz) = translation

#pragma once

#include <cmath>
#include "quat.h"

namespace logosphere {

// Forward declaration
struct Vec3 {
    float x, y, z;

    Vec3 operator+(const Vec3& v) const { return {x + v.x, y + v.y, z + v.z}; }
    Vec3 operator-(const Vec3& v) const { return {x - v.x, y - v.y, z - v.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }

    float dot(const Vec3& v) const { return x*v.x + y*v.y + z*v.z; }
    Vec3 cross(const Vec3& v) const { return {y*v.z - z*v.y, z*v.x - x*v.z, x*v.y - y*v.x}; }
    Vec3 operator-() const { return {-x, -y, -z}; }

    float length() const { return std::sqrt(x*x + y*y + z*z); }

    Vec3 normalized() const {
        float len = length();
        if (len < 1e-6f) return {0, 0, 0};
        return {x/len, y/len, z/len};
    }

    static Vec3 zero() { return {0, 0, 0}; }
};

struct Mat4 {
    float m[16];

    // =========================================================================
    // Identity Matrix: No transformation
    //
    //     | 1 0 0 0 |
    // I = | 0 1 0 0 |
    //     | 0 0 1 0 |
    //     | 0 0 0 1 |
    // =========================================================================
    static Mat4 identity() {
        return {{
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
        }};
    }

    // =========================================================================
    // Translation Matrix: Move by (tx, ty, tz)
    //
    //     | 1 0 0 tx |
    // T = | 0 1 0 ty |
    //     | 0 0 1 tz |
    //     | 0 0 0 1  |
    //
    // In column-major: m12=tx, m13=ty, m14=tz
    // =========================================================================
    static Mat4 translation(float tx, float ty, float tz) {
        return {{
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            tx, ty, tz, 1
        }};
    }

    static Mat4 translation(const Vec3& t) {
        return translation(t.x, t.y, t.z);
    }

    // =========================================================================
    // From Quaternion: Build rotation matrix from quaternion
    //
    // Embeds the 3x3 rotation matrix in upper-left corner,
    // with identity for the rest (no translation, no scale).
    // =========================================================================
    static Mat4 from_rotation(const Quat& q) {
        float mat3[9];
        q.to_matrix3x3(mat3);

        // Embed 3x3 into 4x4 (column-major)
        return {{
            mat3[0], mat3[3], mat3[6], 0,  // Column 0
            mat3[1], mat3[4], mat3[7], 0,  // Column 1
            mat3[2], mat3[5], mat3[8], 0,  // Column 2
            0,       0,       0,       1   // Column 3 (translation = 0)
        }};
    }

    // =========================================================================
    // From Transform: Build matrix from position + rotation
    //
    // Result = Translation * Rotation
    // (Rotation applied first, then translation)
    // =========================================================================
    static Mat4 from_transform(const Vec3& pos, const Quat& rot) {
        float mat3[9];
        rot.to_matrix3x3(mat3);

        // Combine rotation and translation in one matrix
        return {{
            mat3[0], mat3[3], mat3[6], 0,  // Column 0 (X axis)
            mat3[1], mat3[4], mat3[7], 0,  // Column 1 (Y axis)
            mat3[2], mat3[5], mat3[8], 0,  // Column 2 (Z axis)
            pos.x,   pos.y,   pos.z,   1   // Column 3 (translation)
        }};
    }

    // =========================================================================
    // Matrix Multiplication: Compose transforms
    //
    // Result = this * other
    // Applies 'other' first, then 'this'
    //
    // This is the KEY operation for FK: World = Parent * Local
    // No manual trigonometry needed!
    // =========================================================================
    Mat4 operator*(const Mat4& b) const {
        Mat4 result;

        // Column-major multiplication
        // result[col][row] = sum over k of a[k][row] * b[col][k]
        for (int col = 0; col < 4; col++) {
            for (int row = 0; row < 4; row++) {
                float sum = 0.0f;
                for (int k = 0; k < 4; k++) {
                    // a[k][row] = m[k*4 + row]
                    // b[col][k] = b.m[col*4 + k]
                    sum += m[k*4 + row] * b.m[col*4 + k];
                }
                result.m[col*4 + row] = sum;
            }
        }

        return result;
    }

    // =========================================================================
    // Transform Point: Apply full transformation (rotation + translation)
    //
    // Returns the point in world space after applying this transform.
    // =========================================================================
    Vec3 transform_point(const Vec3& p) const {
        // Homogeneous coordinate w = 1 for points
        float x = m[0]*p.x + m[4]*p.y + m[8]*p.z  + m[12];
        float y = m[1]*p.x + m[5]*p.y + m[9]*p.z  + m[13];
        float z = m[2]*p.x + m[6]*p.y + m[10]*p.z + m[14];
        return {x, y, z};
    }

    // =========================================================================
    // Transform Direction: Apply rotation only (ignore translation)
    //
    // For normals, velocities, and other direction vectors.
    // =========================================================================
    Vec3 transform_direction(const Vec3& d) const {
        // Homogeneous coordinate w = 0 for directions (no translation)
        float x = m[0]*d.x + m[4]*d.y + m[8]*d.z;
        float y = m[1]*d.x + m[5]*d.y + m[9]*d.z;
        float z = m[2]*d.x + m[6]*d.y + m[10]*d.z;
        return {x, y, z};
    }

    // =========================================================================
    // Get Position: Extract translation component
    // =========================================================================
    Vec3 get_position() const {
        return {m[12], m[13], m[14]};
    }

    // =========================================================================
    // Get Rotation: Extract rotation as quaternion
    //
    // Assumes matrix is a pure rotation+translation (no scale/shear).
    // Uses Shepperd's method for numerical stability.
    // =========================================================================
    Quat get_rotation() const {
        // Extract 3x3 rotation matrix (row-major for this calculation)
        float r00 = m[0], r01 = m[4], r02 = m[8];
        float r10 = m[1], r11 = m[5], r12 = m[9];
        float r20 = m[2], r21 = m[6], r22 = m[10];

        float trace = r00 + r11 + r22;
        Quat q;

        if (trace > 0) {
            float s = 0.5f / std::sqrt(trace + 1.0f);
            q.w = 0.25f / s;
            q.x = (r21 - r12) * s;
            q.y = (r02 - r20) * s;
            q.z = (r10 - r01) * s;
        } else if (r00 > r11 && r00 > r22) {
            float s = 2.0f * std::sqrt(1.0f + r00 - r11 - r22);
            q.w = (r21 - r12) / s;
            q.x = 0.25f * s;
            q.y = (r01 + r10) / s;
            q.z = (r02 + r20) / s;
        } else if (r11 > r22) {
            float s = 2.0f * std::sqrt(1.0f + r11 - r00 - r22);
            q.w = (r02 - r20) / s;
            q.x = (r01 + r10) / s;
            q.y = 0.25f * s;
            q.z = (r12 + r21) / s;
        } else {
            float s = 2.0f * std::sqrt(1.0f + r22 - r00 - r11);
            q.w = (r10 - r01) / s;
            q.x = (r02 + r20) / s;
            q.y = (r12 + r21) / s;
            q.z = 0.25f * s;
        }

        q.normalize();
        return q;
    }

    // =========================================================================
    // Inverse: Compute inverse transformation
    //
    // For rotation+translation matrix (no scale), inverse is:
    // - Transpose the rotation part
    // - Negate and rotate the translation
    //
    // Full 4x4 inverse is expensive; this assumes affine transform.
    // =========================================================================
    Mat4 inverse_affine() const {
        // Transpose 3x3 rotation
        float r00 = m[0], r01 = m[1], r02 = m[2];
        float r10 = m[4], r11 = m[5], r12 = m[6];
        float r20 = m[8], r21 = m[9], r22 = m[10];

        // Original translation
        float tx = m[12], ty = m[13], tz = m[14];

        // Inverse translation = -R^T * t
        float inv_tx = -(r00*tx + r01*ty + r02*tz);
        float inv_ty = -(r10*tx + r11*ty + r12*tz);
        float inv_tz = -(r20*tx + r21*ty + r22*tz);

        return {{
            r00, r10, r20, 0,
            r01, r11, r21, 0,
            r02, r12, r22, 0,
            inv_tx, inv_ty, inv_tz, 1
        }};
    }
};

} // namespace logosphere
