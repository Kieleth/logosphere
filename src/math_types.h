#ifndef MATH_TYPES_H
#define MATH_TYPES_H

#include <cmath>

// ============================================================================
// Shared Math Types - Domain-Neutral Vector Mathematics
// ============================================================================
// Used by: Physics, Worldgen, Rendering, Collision Detection
// Single source of truth for basic 3D vector operations

// 3D vector with basic operations
struct Vec3 {
    float x, y, z;

    // Constructor with default zero initialization
    Vec3(float x_ = 0.0f, float y_ = 0.0f, float z_ = 0.0f)
        : x(x_), y(y_), z(z_) {}

    // Vector addition
    Vec3 operator+(const Vec3& other) const {
        return Vec3(x + other.x, y + other.y, z + other.z);
    }

    // Vector subtraction
    Vec3 operator-(const Vec3& other) const {
        return Vec3(x - other.x, y - other.y, z - other.z);
    }

    // Scalar multiplication
    Vec3 operator*(float scalar) const {
        return Vec3(x * scalar, y * scalar, z * scalar);
    }

    // Vector length (magnitude)
    float length() const {
        return std::sqrt(x * x + y * y + z * z);
    }

    // Normalized vector (unit length)
    Vec3 normalized() const {
        float len = length();
        if (len < 0.0001f) return Vec3(0, 0, 1); // Default up vector
        return Vec3(x / len, y / len, z / len);
    }

    // Dot product
    float dot(const Vec3& other) const {
        return x * other.x + y * other.y + z * other.z;
    }
};

#endif // MATH_TYPES_H
