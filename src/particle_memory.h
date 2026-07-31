#ifndef PARTICLE_MEMORY_H
#define PARTICLE_MEMORY_H

// Memory particle + layer types used by the spatial memory / vision system.
// Separate from particle.h so consumers that only need memory types do not
// transitively pull the full Particle struct.

struct MemoryParticle {
    float x, y, z;                              // Last known position (may drift)
    float original_x, original_y, original_z;   // True position when first observed
    float r, g, b, a;                           // Color when last seen
    float size;                                 // Size when last observed
    float confidence;                           // 1.0 = perfect, 0.0 = forgotten
    double last_seen_time;                      // When last observed
    float distance_when_seen;                   // Distance from character at last sighting
    int   observation_count;                    // Repeat sightings strengthen memory
};

enum class MemoryLayer {
    ACTIVE_VISION,    // Currently visible (100% bright)
    RECENT_MEMORY,    // 0–10 s ago
    SHORT_MEMORY,     // 10–60 s ago
    LONG_MEMORY,      // 60+ s ago
};

#endif  // PARTICLE_MEMORY_H
