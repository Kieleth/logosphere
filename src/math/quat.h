// Quaternion Math - From Scratch
//
// Quaternions represent 3D rotations without gimbal lock or rotation order issues.
// A quaternion q = (w, x, y, z) encodes rotation as:
//   w = cos(theta/2)
//   (x, y, z) = sin(theta/2) * axis
//
// Key properties:
// - Unit quaternions (|q| = 1) represent valid rotations
// - Composition via multiplication: q1 * q2 applies q2 then q1
// - No gimbal lock (unlike Euler angles)
// - Smooth interpolation via SLERP
//
// This implementation follows educational principles: no external libraries,
// every formula documented for learning.

#pragma once

#include <cmath>

// Clamp helper (C++17's clamp_value may not be available everywhere)
namespace {
    template<typename T>
    T clamp_value(T val, T min_val, T max_val) {
        if (val < min_val) return min_val;
        if (val > max_val) return max_val;
        return val;
    }
}

namespace logosphere {

struct Quat {
    float w, x, y, z;

    // =========================================================================
    // Identity: No rotation (axis irrelevant, angle = 0)
    // w = cos(0/2) = 1, xyz = sin(0/2) * axis = 0
    // =========================================================================
    static Quat identity() {
        return {1.0f, 0.0f, 0.0f, 0.0f};
    }

    // =========================================================================
    // From Axis-Angle: Convert (axis, theta) to quaternion
    //
    // Given rotation axis (ax, ay, az) and angle theta in radians:
    //   q.w = cos(theta/2)
    //   q.x = sin(theta/2) * ax
    //   q.y = sin(theta/2) * ay
    //   q.z = sin(theta/2) * az
    //
    // Note: Axis must be normalized (unit vector).
    // =========================================================================
    static Quat from_axis_angle(float ax, float ay, float az, float theta) {
        // Normalize axis
        float len = std::sqrt(ax*ax + ay*ay + az*az);
        if (len < 1e-6f) {
            return identity();  // Zero axis = no rotation
        }
        ax /= len;
        ay /= len;
        az /= len;

        // Half-angle formulas
        float half_theta = theta * 0.5f;
        float s = std::sin(half_theta);
        float c = std::cos(half_theta);

        return {c, s * ax, s * ay, s * az};
    }

    // =========================================================================
    // Multiplication: Compose rotations
    //
    // Result = this * q means: apply q first, then apply this
    //
    // Formula (Hamilton product):
    //   w' = w1*w2 - x1*x2 - y1*y2 - z1*z2
    //   x' = w1*x2 + x1*w2 + y1*z2 - z1*y2
    //   y' = w1*y2 - x1*z2 + y1*w2 + z1*x2
    //   z' = w1*z2 + x1*y2 - y1*x2 + z1*w2
    //
    // NOT commutative: q1 * q2 != q2 * q1 (same as matrix multiplication)
    // =========================================================================
    Quat operator*(const Quat& q) const {
        return {
            w*q.w - x*q.x - y*q.y - z*q.z,
            w*q.x + x*q.w + y*q.z - z*q.y,
            w*q.y - x*q.z + y*q.w + z*q.x,
            w*q.z + x*q.y - y*q.x + z*q.w
        };
    }

    // =========================================================================
    // Conjugate: Inverts the rotation direction
    //
    // For unit quaternion: conjugate = inverse
    // q * conjugate(q) = identity
    // =========================================================================
    Quat conjugate() const {
        return {w, -x, -y, -z};
    }

    // =========================================================================
    // Magnitude: Should be 1.0 for valid rotation
    // =========================================================================
    float magnitude() const {
        return std::sqrt(w*w + x*x + y*y + z*z);
    }

    float magnitude_squared() const {
        return w*w + x*x + y*y + z*z;
    }

    // =========================================================================
    // Normalize: Make unit quaternion
    //
    // Critical after repeated multiplications (floating-point drift).
    // Non-unit quaternions produce shear distortion.
    // =========================================================================
    void normalize() {
        float mag_sq = magnitude_squared();
        if (mag_sq < 1e-12f) {
            *this = identity();  // Degenerate, reset
            return;
        }
        float inv_mag = 1.0f / std::sqrt(mag_sq);
        w *= inv_mag;
        x *= inv_mag;
        y *= inv_mag;
        z *= inv_mag;
    }

    Quat normalized() const {
        Quat result = *this;
        result.normalize();
        return result;
    }

    // =========================================================================
    // Dot Product: Measures similarity between quaternions
    //
    // dot(q1, q2) = cos(theta) where theta is angle between them
    // Used by SLERP to find interpolation angle.
    // =========================================================================
    static float dot(const Quat& a, const Quat& b) {
        return a.w*b.w + a.x*b.x + a.y*b.y + a.z*b.z;
    }

    // =========================================================================
    // SLERP: Spherical Linear Interpolation
    //
    // Smoothly interpolates between rotations at constant angular velocity.
    // Linear interpolation of Euler angles causes jerky, non-uniform speed.
    //
    // Formula:
    //   slerp(q0, q1, t) = [sin((1-t)*theta) * q0 + sin(t*theta) * q1] / sin(theta)
    //   where theta = acos(dot(q0, q1))
    //
    // Edge cases:
    // - If dot < 0, negate one quaternion (take shorter path)
    // - If quaternions nearly equal, use linear interpolation
    // =========================================================================
    static Quat slerp(const Quat& q0, const Quat& q1, float t) {
        // Compute dot product
        float d = dot(q0, q1);

        // If dot < 0, negate q1 to take shorter path
        // (q and -q represent same rotation, choose shorter arc)
        Quat q1_adj = q1;
        if (d < 0.0f) {
            q1_adj.w = -q1_adj.w;
            q1_adj.x = -q1_adj.x;
            q1_adj.y = -q1_adj.y;
            q1_adj.z = -q1_adj.z;
            d = -d;
        }

        // Clamp for numerical stability
        d = clamp_value(d, -1.0f, 1.0f);

        // Compute angle
        float theta = std::acos(d);
        float sin_theta = std::sin(theta);

        // If nearly parallel, use linear interpolation
        if (sin_theta < 1e-6f) {
            return Quat{
                q0.w + t * (q1_adj.w - q0.w),
                q0.x + t * (q1_adj.x - q0.x),
                q0.y + t * (q1_adj.y - q0.y),
                q0.z + t * (q1_adj.z - q0.z)
            }.normalized();
        }

        // SLERP formula
        float a = std::sin((1.0f - t) * theta) / sin_theta;
        float b = std::sin(t * theta) / sin_theta;

        return Quat{
            a*q0.w + b*q1_adj.w,
            a*q0.x + b*q1_adj.x,
            a*q0.y + b*q1_adj.y,
            a*q0.z + b*q1_adj.z
        };
    }

    // =========================================================================
    // To Rotation Matrix: Convert quaternion to 3x3 rotation matrix
    //
    // Formula:
    //      [ 1 - 2(y²+z²)   2(xy-wz)      2(xz+wy)   ]
    //  R = [ 2(xy+wz)       1 - 2(x²+z²)  2(yz-wx)   ]
    //      [ 2(xz-wy)       2(yz+wx)      1 - 2(x²+y²)]
    //
    // Output is row-major: mat[row * 3 + col]
    // =========================================================================
    void to_matrix3x3(float* mat) const {
        float xx = x * x;
        float yy = y * y;
        float zz = z * z;
        float xy = x * y;
        float xz = x * z;
        float yz = y * z;
        float wx = w * x;
        float wy = w * y;
        float wz = w * z;

        // Row 0
        mat[0] = 1.0f - 2.0f * (yy + zz);
        mat[1] = 2.0f * (xy - wz);
        mat[2] = 2.0f * (xz + wy);

        // Row 1
        mat[3] = 2.0f * (xy + wz);
        mat[4] = 1.0f - 2.0f * (xx + zz);
        mat[5] = 2.0f * (yz - wx);

        // Row 2
        mat[6] = 2.0f * (xz - wy);
        mat[7] = 2.0f * (yz + wx);
        mat[8] = 1.0f - 2.0f * (xx + yy);
    }

    // =========================================================================
    // Rotate Vector: Apply rotation to a 3D vector
    //
    // Formula: v' = q * v * q^(-1)
    // But more efficient to use rotation matrix for multiple vectors.
    // This is for single-vector convenience.
    // =========================================================================
    void rotate_vector(float vx, float vy, float vz,
                       float& out_x, float& out_y, float& out_z) const {
        // Using rotation matrix (more efficient than q*v*q^-1)
        float mat[9];
        to_matrix3x3(mat);

        out_x = mat[0]*vx + mat[1]*vy + mat[2]*vz;
        out_y = mat[3]*vx + mat[4]*vy + mat[5]*vz;
        out_z = mat[6]*vx + mat[7]*vy + mat[8]*vz;
    }

    // =========================================================================
    // To Axis-Angle: Extract rotation axis and angle
    //
    // Useful for debugging and logging.
    // =========================================================================
    void to_axis_angle(float& out_ax, float& out_ay, float& out_az,
                       float& out_theta) const {
        float w_clamped = clamp_value(w, -1.0f, 1.0f);
        out_theta = 2.0f * std::acos(w_clamped);

        float sin_half = std::sqrt(x*x + y*y + z*z);
        if (sin_half < 1e-6f) {
            // Near zero rotation
            out_ax = 0.0f;
            out_ay = 0.0f;
            out_az = 1.0f;  // Default axis
            out_theta = 0.0f;
        } else {
            float inv_sin = 1.0f / sin_half;
            out_ax = x * inv_sin;
            out_ay = y * inv_sin;
            out_az = z * inv_sin;
        }
    }

    // =========================================================================
    // To Euler Angles (ZYX decomposition, CW Z convention)
    //
    // Inverse of from_euler: extracts the Euler triple used by the
    // engine's rendering/Euler path. Matches Transform::to_euler so
    // round-trip through the quaternion is stable for poses that
    // aren't at the gimbal-lock boundary (|sin(pitch_y)| > 0.9999).
    // Output convention: CW Z (matches from_euler input).
    // =========================================================================
    void to_euler_zyx(float& out_x, float& out_y, float& out_z) const {
        float mat[9];
        to_matrix3x3(mat);
        float sy = -mat[6];
        if (std::abs(sy) < 0.9999f) {
            out_y = std::asin(sy);
            out_x = std::atan2(mat[7], mat[8]);
            out_z = -std::atan2(mat[3], mat[0]);  // CW
        } else {
            out_y = (sy > 0) ? static_cast<float>(M_PI / 2.0) : static_cast<float>(-M_PI / 2.0);
            out_x = 0.0f;
            out_z = -std::atan2(-mat[1], mat[4]);
        }
    }

    // =========================================================================
    // From Euler Angles (for compatibility with existing code)
    //
    // Converts Euler angles (rotation_x, rotation_y, rotation_z) to quaternion.
    // Order: Z * Y * X (matches typical game engine convention)
    // =========================================================================
    static Quat from_euler(float pitch_x, float yaw_y, float roll_z) {
        // Negate Z: input uses CW Z convention (matching to_euler output),
        // but axis-angle uses standard CCW
        Quat qx = from_axis_angle(1, 0, 0, pitch_x);
        Quat qy = from_axis_angle(0, 1, 0, yaw_y);
        Quat qz = from_axis_angle(0, 0, 1, -roll_z);

        // Apply in order: Z, then Y, then X
        return qz * qy * qx;
    }

    // =========================================================================
    // From Two Vectors: Shortest rotation mapping direction 'from' to 'to'
    //
    // Formula: q = normalize(1 + dot, cross)
    // where cross = from × to, dot = from · to
    //
    // Edge cases:
    // - Same direction (dot ≈ 1): return identity
    // - Opposite direction (dot ≈ -1): 180° around any perpendicular axis
    // =========================================================================
    static Quat from_two_vectors(float fx, float fy, float fz,
                                  float tx, float ty, float tz) {
        // Normalize inputs
        float from_len = std::sqrt(fx*fx + fy*fy + fz*fz);
        float to_len = std::sqrt(tx*tx + ty*ty + tz*tz);
        if (from_len < 1e-6f || to_len < 1e-6f) return identity();
        fx /= from_len; fy /= from_len; fz /= from_len;
        tx /= to_len;   ty /= to_len;   tz /= to_len;

        float d = fx*tx + fy*ty + fz*tz;  // dot product

        if (d > 0.9999f) return identity();  // same direction

        if (d < -0.9999f) {
            // Opposite — rotate 180° around a perpendicular axis
            float px = 0.0f, py = -fz, pz = fy;  // from × (1,0,0)
            float plen = std::sqrt(py*py + pz*pz);
            if (plen < 1e-6f) {
                px = fz; py = 0.0f; pz = -fx;  // from × (0,1,0)
                plen = std::sqrt(px*px + pz*pz);
            }
            px /= plen; py /= plen; pz /= plen;
            return Quat{0.0f, px, py, pz};  // 180° rotation (w=0)
        }

        // General case: q = (1 + dot, cross), normalized
        float cx = fy*tz - fz*ty;
        float cy = fz*tx - fx*tz;
        float cz = fx*ty - fy*tx;
        Quat q = {1.0f + d, cx, cy, cz};
        q.normalize();
        return q;
    }
};

} // namespace logosphere
