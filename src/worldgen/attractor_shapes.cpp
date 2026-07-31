#include "logosphere/worldgen/attractor_shapes.h"
#include <cmath>
#include <iostream>

std::vector<Vec3> AttractorShapes::generate(const OrganicSpec& spec, Vec3 center, unsigned int seed) {
    switch (spec.attractor_shape) {
        case AttractorShape::SPHERE:
            return generate_sphere(center, spec.width * 0.5f, spec.attractor_count, seed);

        case AttractorShape::CYLINDER:
            return generate_cylinder(center, spec.width * 0.5f, spec.height, spec.attractor_count, seed);

        case AttractorShape::CONE:
            return generate_cone(center, spec.width * 0.5f, spec.height, spec.attractor_count, seed);

        case AttractorShape::INVERTED_CONE:
            return generate_inverted_cone(center, spec.width * 0.5f, spec.height, spec.attractor_count, seed);

        case AttractorShape::HEMISPHERE:
            return generate_hemisphere(center, spec.width * 0.5f, spec.attractor_count, seed);

        case AttractorShape::TORUS:
            return generate_torus(center, spec.width * 0.5f, spec.width * 0.2f, spec.attractor_count, seed);

        default:
            std::cerr << "[AttractorShapes] Unknown shape, defaulting to sphere" << std::endl;
            return generate_sphere(center, spec.width * 0.5f, spec.attractor_count, seed);
    }
}

std::vector<Vec3> AttractorShapes::generate_sphere(Vec3 center, float radius, int count, unsigned int seed) {
    std::vector<Vec3> attractors;
    attractors.reserve(count);

    unsigned int rng_state = seed;

    for (int i = 0; i < count; ++i) {
        Vec3 point = random_in_unit_sphere(rng_state);
        point = point * radius;
        attractors.push_back(Vec3(center.x + point.x, center.y + point.y, center.z + point.z));
    }

    return attractors;
}

std::vector<Vec3> AttractorShapes::generate_cylinder(Vec3 center, float radius, float height, int count, unsigned int seed) {
    std::vector<Vec3> attractors;
    attractors.reserve(count);

    unsigned int rng_state = seed;

    for (int i = 0; i < count; ++i) {
        // Random point in circle (XY plane)
        float angle = random_01(rng_state) * 2.0f * M_PI;
        float r = std::sqrt(random_01(rng_state)) * radius;  // sqrt for uniform distribution

        // Random height
        float h = random_01(rng_state) * height;

        float x = center.x + r * std::cos(angle);
        float y = center.y + r * std::sin(angle);
        float z = center.z + h;

        attractors.push_back(Vec3(x, y, z));
    }

    return attractors;
}

std::vector<Vec3> AttractorShapes::generate_cone(Vec3 center, float base_radius, float height, int count, unsigned int seed) {
    std::vector<Vec3> attractors;
    attractors.reserve(count);

    unsigned int rng_state = seed;

    for (int i = 0; i < count; ++i) {
        // Random height (0 at base, height at tip)
        float h = random_01(rng_state) * height;

        // Radius decreases linearly with height
        float radius_at_h = base_radius * (1.0f - h / height);

        // Random point in circle at this height
        float angle = random_01(rng_state) * 2.0f * M_PI;
        float r = std::sqrt(random_01(rng_state)) * radius_at_h;

        float x = center.x + r * std::cos(angle);
        float y = center.y + r * std::sin(angle);
        float z = center.z + h;

        attractors.push_back(Vec3(x, y, z));
    }

    return attractors;
}

std::vector<Vec3> AttractorShapes::generate_inverted_cone(Vec3 center, float base_radius, float height, int count, unsigned int seed) {
    std::vector<Vec3> attractors;
    attractors.reserve(count);

    unsigned int rng_state = seed;

    for (int i = 0; i < count; ++i) {
        // Random height (0 at narrow top, height at wide base, going downward)
        float h = random_01(rng_state) * height;

        // Radius increases with depth (inverted)
        float radius_at_h = base_radius * (h / height);

        // Random point in circle at this depth
        float angle = random_01(rng_state) * 2.0f * M_PI;
        float r = std::sqrt(random_01(rng_state)) * radius_at_h;

        float x = center.x + r * std::cos(angle);
        float y = center.y + r * std::sin(angle);
        float z = center.z - h;  // Negative Z (downward)

        attractors.push_back(Vec3(x, y, z));
    }

    return attractors;
}

std::vector<Vec3> AttractorShapes::generate_hemisphere(Vec3 center, float radius, int count, unsigned int seed) {
    std::vector<Vec3> attractors;
    attractors.reserve(count);

    unsigned int rng_state = seed;

    for (int i = 0; i < count; ++i) {
        // Generate point in sphere, reject if Z < 0
        Vec3 point;
        do {
            point = random_in_unit_sphere(rng_state);
        } while (point.z < 0);  // Only keep upper hemisphere

        point = point * radius;
        attractors.push_back(Vec3(center.x + point.x, center.y + point.y, center.z + point.z));
    }

    return attractors;
}

std::vector<Vec3> AttractorShapes::generate_torus(Vec3 center, float major_radius, float minor_radius, int count, unsigned int seed) {
    std::vector<Vec3> attractors;
    attractors.reserve(count);

    unsigned int rng_state = seed;

    for (int i = 0; i < count; ++i) {
        // Random angles
        float u = random_01(rng_state) * 2.0f * M_PI;  // Angle around major circle
        float v = random_01(rng_state) * 2.0f * M_PI;  // Angle around minor circle

        // Torus parametric equations
        float x = (major_radius + minor_radius * std::cos(v)) * std::cos(u);
        float y = (major_radius + minor_radius * std::cos(v)) * std::sin(u);
        float z = minor_radius * std::sin(v);

        attractors.push_back(Vec3(center.x + x, center.y + y, center.z + z));
    }

    return attractors;
}

Vec3 AttractorShapes::random_in_unit_sphere(unsigned int& rng_state) {
    // Rejection sampling for uniform distribution in sphere
    while (true) {
        float x = (random_01(rng_state) * 2.0f - 1.0f);
        float y = (random_01(rng_state) * 2.0f - 1.0f);
        float z = (random_01(rng_state) * 2.0f - 1.0f);

        float dist_sq = x * x + y * y + z * z;
        if (dist_sq <= 1.0f) {
            return Vec3(x, y, z);
        }
    }
}

float AttractorShapes::random_01(unsigned int& rng_state) {
    // Simple LCG
    rng_state = (1103515245 * rng_state + 12345) % 2147483648;
    return (float)rng_state / 2147483648.0f;
}
