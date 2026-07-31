#ifndef RASTERIZATION_MATH_H
#define RASTERIZATION_MATH_H

#include <algorithm>  // For std::min, std::max
#include <cmath>      // For sqrtf

// =========================================================================
// 2D RASTERIZATION MATH PRIMITIVES
// =========================================================================
// Pure math functions for triangle rasterization
// All functions are inline for zero-overhead abstraction
//
// CRITICAL: These functions MUST match gpu_rasterization_math.metal exactly
// Same inputs → same outputs guarantees CPU/GPU visual parity
//
// See: logosphere/docs/CPU_GPU_MATH_MAPPING.md

namespace RasterizationMath {

// =========================================================================
// 2D EDGE FUNCTION (2D Cross Product)
// =========================================================================
// Returns: Signed distance of point (px, py) from edge (ax,ay)→(bx,by)
// - Positive: Point is on the "left" side of the edge (inside triangle)
// - Negative: Point is on the "right" side of the edge (outside triangle)
// - Zero: Point is exactly on the edge
//
// Algorithm: 2D cross product of vectors (p-a) and (b-a)
// Formula: (px - ax) * (by - ay) - (py - ay) * (bx - ax)

inline float edge_function_2d(
    float ax, float ay,  // Edge start point
    float bx, float by,  // Edge end point
    float px, float py)  // Test point
{
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

// =========================================================================
// TRIANGLE AREA (2D)
// =========================================================================
// Returns: Signed area of triangle (positive if CCW winding, negative if CW)
// Algorithm: Use edge function with third vertex as test point

inline float triangle_area_2d(
    float x0, float y0,
    float x1, float y1,
    float x2, float y2)
{
    return edge_function_2d(x0, y0, x1, y1, x2, y2);
}

// =========================================================================
// POINT-IN-TRIANGLE TEST (2D)
// =========================================================================
// Returns: true if point (px, py) is inside triangle, false otherwise
// Algorithm: Point is inside if it's on the same side of all 3 edges

inline bool point_in_triangle_2d(
    float px, float py,
    float x0, float y0,
    float x1, float y1,
    float x2, float y2)
{
    float e0 = edge_function_2d(x0, y0, x1, y1, px, py);
    float e1 = edge_function_2d(x1, y1, x2, y2, px, py);
    float e2 = edge_function_2d(x2, y2, x0, y0, px, py);

    // All edges must have same sign (all positive or all negative)
    return (e0 >= 0 && e1 >= 0 && e2 >= 0) || (e0 <= 0 && e1 <= 0 && e2 <= 0);
}

// =========================================================================
// BARYCENTRIC COORDINATES (2D)
// =========================================================================
// Computes barycentric coordinates (w0, w1, w2) of point (px, py) in triangle
// Result: w0 + w1 + w2 = 1.0
// Usage: interpolate(w0*v0 + w1*v1 + w2*v2) for any vertex attribute

inline void barycentric_coords_2d(
    float px, float py,
    float x0, float y0,
    float x1, float y1,
    float x2, float y2,
    float& w0, float& w1, float& w2)
{
    float area = triangle_area_2d(x0, y0, x1, y1, x2, y2);

    // Avoid division by zero for degenerate triangles
    if (area == 0.0f) {
        w0 = w1 = w2 = 0.0f;
        return;
    }

    float inv_area = 1.0f / area;

    // Compute sub-triangle areas
    w0 = triangle_area_2d(px, py, x1, y1, x2, y2) * inv_area;
    w1 = triangle_area_2d(x0, y0, px, py, x2, y2) * inv_area;
    w2 = triangle_area_2d(x0, y0, x1, y1, px, py) * inv_area;
}

// =========================================================================
// EDGE EQUATION COEFFICIENTS
// =========================================================================
// Computes coefficients (a, b, c) for edge equation: ax + by + c = 0
// Returns: Struct with coefficients

struct EdgeCoefficients {
    float a, b, c;
};

inline EdgeCoefficients compute_edge_coefficients_2d(
    float x0, float y0,
    float x1, float y1)
{
    EdgeCoefficients coeff;
    coeff.a = y1 - y0;
    coeff.b = x0 - x1;
    coeff.c = x1 * y0 - x0 * y1;
    return coeff;
}

// =========================================================================
// BOUNDING BOX (2D)
// =========================================================================
// Computes axis-aligned bounding box of 3 vertices

inline void compute_bbox_2d(
    const float verts[3][2],  // 3 vertices with (x, y) coordinates
    int& min_x, int& max_x,
    int& min_y, int& max_y)
{
    // Initialize with first vertex
    min_x = max_x = static_cast<int>(verts[0][0]);
    min_y = max_y = static_cast<int>(verts[0][1]);

    // Expand to include other vertices
    for (int i = 1; i < 3; ++i) {
        int x = static_cast<int>(verts[i][0]);
        int y = static_cast<int>(verts[i][1]);

        min_x = std::min(min_x, x);
        max_x = std::max(max_x, x);
        min_y = std::min(min_y, y);
        max_y = std::max(max_y, y);
    }
}

// =========================================================================
// 3D DISTANCE CALCULATIONS
// =========================================================================
// Compute squared and actual distances in 3D space
// Used for depth calculations and camera-relative distances

inline float distance_squared_3d(float dx, float dy, float dz) {
    return dx * dx + dy * dy + dz * dz;
}

inline float distance_3d(float dx, float dy, float dz) {
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

// =========================================================================
// BARYCENTRIC INTERPOLATION
// =========================================================================
// Interpolate value across triangle using barycentric coordinates
// Formula: result = v0 * w + v1 * u + v2 * v
// where w = 1 - u - v (weight for vertex 0)
//
// Usage: Interpolate any per-vertex attribute (depth, color, UV, normal)

inline float barycentric_interpolate(
    float v0, float v1, float v2,  // Values at 3 vertices
    float w, float u, float v)     // Barycentric weights
{
    return v0 * w + v1 * u + v2 * v;
}

} // namespace RasterizationMath

#endif // RASTERIZATION_MATH_H
