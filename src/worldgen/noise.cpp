#include "logosphere/worldgen/noise.h"
#include <cmath>

namespace noise {

float hash2d(int x, int y) {
    // Hash coordinates to pseudo-random value
    // Uses large primes for good distribution
    int n = x * 73856093 ^ y * 19349663;
    n = (n ^ (n >> 13)) * 1274126177;

    // Convert to [0.0, 1.0]
    return ((n ^ (n >> 16)) & 0x7fffffff) / float(0x7fffffff);
}

float dot_grid_gradient(int ix, int iy, float x, float y) {
    // Get pseudo-random gradient vector at grid point
    // Use hash to generate angle
    float random = hash2d(ix, iy);
    float angle = random * 2.0f * 3.14159265f;  // 0 to 2π

    // Gradient vector components
    float grad_x = std::cos(angle);
    float grad_y = std::sin(angle);

    // Distance vector from grid point to sample point
    float dx = x - static_cast<float>(ix);
    float dy = y - static_cast<float>(iy);

    // Dot product
    return (dx * grad_x + dy * grad_y);
}

float perlin_noise_2d(float x, float y) {
    // Integer grid coordinates
    int x0 = static_cast<int>(std::floor(x));
    int y0 = static_cast<int>(std::floor(y));
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    // Fractional position within grid cell
    float fx = x - x0;
    float fy = y - y0;

    // Smooth interpolation curves (fade function)
    float sx = smoothstep(fx);
    float sy = smoothstep(fy);

    // Gradient values at grid corners
    float n00 = dot_grid_gradient(x0, y0, x, y);
    float n10 = dot_grid_gradient(x1, y0, x, y);
    float n01 = dot_grid_gradient(x0, y1, x, y);
    float n11 = dot_grid_gradient(x1, y1, x, y);

    // Bilinear interpolation
    float nx0 = lerp(n00, n10, sx);
    float nx1 = lerp(n01, n11, sx);

    return lerp(nx0, nx1, sy);  // Returns approximately [-1, 1]
}

} // namespace noise
