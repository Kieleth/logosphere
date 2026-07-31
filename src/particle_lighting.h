#ifndef PARTICLE_LIGHTING_H
#define PARTICLE_LIGHTING_H

#include <vector>

// Lighting primitives used by the ray-traced lighting pipeline and by
// per-particle light accumulators. Kept separate from particle.h so the
// particle struct does not transitively drag lighting types into TUs that
// only need geometry / physics state.

struct LightRay {
    float origin_x, origin_y, origin_z;
    float direction_x, direction_y, direction_z;
    float intensity;
    float r, g, b;
    int   bounce_count;
    float emission_radius;  // Max range for performance cutoff
};

struct SurfaceLighting {
    int   surface_id = -1;
    float accumulated_light = 0.0f;
    float r = 0.0f, g = 0.0f, b = 0.0f;
    float incident_angle = 0.0f;
};

// Per-particle light accumulator. Particles can have an arbitrary number of
// surfaces so we store per-surface lighting in a vector rather than a fixed
// cube-sized array.
struct ParticleLighting {
    std::vector<SurfaceLighting> surfaces;

    float GetTotalLight() const {
        float total = 0.0f;
        for (const auto& s : surfaces) total += s.accumulated_light;
        return total;
    }

    bool IsLit() const { return GetTotalLight() > 0.0f; }

    const SurfaceLighting* GetSurfaceLighting(int surface_id) const {
        for (const auto& s : surfaces) {
            if (s.surface_id == surface_id) return &s;
        }
        return nullptr;
    }
};

#endif  // PARTICLE_LIGHTING_H
