#ifndef GPU_RASTERIZATION_MATH_METAL
#define GPU_RASTERIZATION_MATH_METAL

#include <metal_stdlib>
using namespace metal;

// =========================================================================
// 2D RASTERIZATION MATH PRIMITIVES (GPU)
// =========================================================================
// CRITICAL: These functions MUST match rasterization_math.h exactly
// Same inputs → same outputs guarantees CPU/GPU visual parity
//
// See: logosphere/docs/CPU_GPU_MATH_MAPPING.md

// =========================================================================
// 2D EDGE FUNCTION (2D Cross Product)
// =========================================================================
inline float edge_function_2d(
    float ax, float ay,
    float bx, float by,
    float px, float py)
{
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

// =========================================================================
// TRIANGLE AREA (2D)
// =========================================================================
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
inline bool point_in_triangle_2d(
    float px, float py,
    float x0, float y0,
    float x1, float y1,
    float x2, float y2)
{
    float e0 = edge_function_2d(x0, y0, x1, y1, px, py);
    float e1 = edge_function_2d(x1, y1, x2, y2, px, py);
    float e2 = edge_function_2d(x2, y2, x0, y0, px, py);

    return (e0 >= 0 && e1 >= 0 && e2 >= 0) || (e0 <= 0 && e1 <= 0 && e2 <= 0);
}

// =========================================================================
// BARYCENTRIC COORDINATES (2D)
// =========================================================================
inline void barycentric_coords_2d(
    float px, float py,
    float x0, float y0,
    float x1, float y1,
    float x2, float y2,
    thread float& w0, thread float& w1, thread float& w2)
{
    float area = triangle_area_2d(x0, y0, x1, y1, x2, y2);

    if (area == 0.0f) {
        w0 = w1 = w2 = 0.0f;
        return;
    }

    float inv_area = 1.0f / area;

    w0 = triangle_area_2d(px, py, x1, y1, x2, y2) * inv_area;
    w1 = triangle_area_2d(x0, y0, px, py, x2, y2) * inv_area;
    w2 = triangle_area_2d(x0, y0, x1, y1, px, py) * inv_area;
}

// =========================================================================
// EDGE EQUATION COEFFICIENTS
// =========================================================================
struct EdgeCoefficients {
    float a, b, c;
    float _padding;  // 16-byte alignment
};

inline EdgeCoefficients compute_edge_coefficients_2d(
    float x0, float y0,
    float x1, float y1)
{
    EdgeCoefficients coeff;
    coeff.a = y1 - y0;
    coeff.b = x0 - x1;
    coeff.c = x1 * y0 - x0 * y1;
    coeff._padding = 0.0f;
    return coeff;
}

// =========================================================================
// BOUNDING BOX (2D)
// =========================================================================
inline void compute_bbox_2d(
    const thread float verts[3][2],
    thread int& min_x, thread int& max_x,
    thread int& min_y, thread int& max_y)
{
    min_x = max_x = static_cast<int>(verts[0][0]);
    min_y = max_y = static_cast<int>(verts[0][1]);

    for (int i = 1; i < 3; ++i) {
        int x = static_cast<int>(verts[i][0]);
        int y = static_cast<int>(verts[i][1]);

        min_x = min(min_x, x);
        max_x = max(max_x, x);
        min_y = min(min_y, y);
        max_y = max(max_y, y);
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
    return sqrt(dx * dx + dy * dy + dz * dz);
}

// =========================================================================
// BARYCENTRIC INTERPOLATION
// =========================================================================
// Interpolate value across triangle using barycentric coordinates
// Formula: result = v0 * w + v1 * u + v2 * v
// where w = 1 - u - v (weight for vertex 0)

inline float barycentric_interpolate(
    float v0, float v1, float v2,  // Values at 3 vertices
    float w, float u, float v)     // Barycentric weights
{
    return v0 * w + v1 * u + v2 * v;
}

#endif // GPU_RASTERIZATION_MATH_METAL
