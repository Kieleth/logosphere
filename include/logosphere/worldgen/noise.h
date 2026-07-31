#ifndef NOISE_H
#define NOISE_H

// 2D Perlin noise for terrain generation
// Continuous, deterministic gradient noise suitable for seamless chunk generation

namespace noise {

// Hash function for deterministic pseudo-random values
// Returns value in [0.0, 1.0]
float hash2d(int x, int y);

// Linear interpolation
inline float lerp(float a, float b, float t) {
    return a + t * (b - a);
}

// Smooth interpolation curve (fade function)
// Uses 3t^2 - 2t^3 for smooth transitions
inline float smoothstep(float t) {
    return t * t * (3.0f - 2.0f * t);
}

// Gradient vector at grid point
// Returns dot product of gradient and distance vector
float dot_grid_gradient(int ix, int iy, float x, float y);

// 2D Perlin noise
// Returns value in approximately [-1.0, 1.0]
// Same (x, y) always produces same output (deterministic)
float perlin_noise_2d(float x, float y);

} // namespace noise

#endif // NOISE_H
