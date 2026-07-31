#ifndef ENTITY_TELEMETRY_H
#define ENTITY_TELEMETRY_H

// ============================================================================
// PHYSICS TELEMETRY SYSTEM
// ============================================================================
// Per-particle per-frame contact recording in a ring buffer.
// Lives in the physics layer so ALL particles get telemetry, not just
// humanoids. A falling cube, a drifting boulder, a foot on a tile edge
// all produce the same contact data.
//
// Zero overhead when disabled (single bool check).
// ============================================================================

#include <vector>
#include <unordered_map>
#include <cstdint>
#include <cmath>
#include <functional>

namespace Logosphere {

// Per-contact record within a frame
struct ContactSnapshot {
    unsigned int particle_a;
    unsigned int particle_b;
    float normal_x, normal_y, normal_z;  // From A toward B
    float penetration;
    float contact_x, contact_y, contact_z;
    bool is_corner_contact;
    bool is_horizontal;  // |normal_x| > 0.1 || |normal_y| > 0.1
};

// Per-particle per-frame snapshot
struct ParticleFrameSnapshot {
    uint32_t frame_number = 0;

    // Particle state
    float x = 0, y = 0, z = 0;
    float vx = 0, vy = 0, vz = 0;
    float width = 0, height = 0, thickness = 0;
    bool is_at_rest = false;

    // All contacts involving this particle
    std::vector<ContactSnapshot> contacts;

    // Diagnostic aggregates (computed at record time)
    int horizontal_contact_count = 0;
    int corner_contact_count = 0;
    int total_contact_count = 0;

    float speed() const {
        return std::sqrt(vx * vx + vy * vy + vz * vz);
    }

    float horizontal_speed() const {
        return std::sqrt(vx * vx + vy * vy);
    }
};

// Ring buffer of frame snapshots for one particle
class TelemetryBuffer {
public:
    explicit TelemetryBuffer(size_t capacity = 300);

    void record(ParticleFrameSnapshot&& snapshot);

    size_t size() const { return count_; }
    size_t capacity() const { return capacity_; }
    bool empty() const { return count_ == 0; }

    // Access: 0 = most recent, 1 = previous, etc.
    const ParticleFrameSnapshot& at(size_t frames_ago) const;
    const ParticleFrameSnapshot& newest() const;
    const ParticleFrameSnapshot& oldest() const;

    // Find frames matching a predicate
    std::vector<const ParticleFrameSnapshot*> find_frames(
        std::function<bool(const ParticleFrameSnapshot&)> predicate) const;

    // Convenience: frames with horizontal contacts
    std::vector<const ParticleFrameSnapshot*> find_horizontal_contact_frames() const;

    // Convenience: frames where particle was moving but slow (sticking)
    std::vector<const ParticleFrameSnapshot*> find_slow_frames(
        float speed_threshold = 0.3f) const;

    void clear();

private:
    std::vector<ParticleFrameSnapshot> buffer_;
    size_t capacity_;
    size_t head_ = 0;
    size_t count_ = 0;
};

// Top-level container: maps particle_id to telemetry buffer
class PhysicsTelemetry {
public:
    void set_enabled(bool enabled) { enabled_ = enabled; }
    bool is_enabled() const { return enabled_; }

    // Track specific particles (not all, that would be too expensive)
    void track_particle(unsigned int particle_id);
    void untrack_particle(unsigned int particle_id);
    bool is_tracked(unsigned int particle_id) const;

    TelemetryBuffer& get_buffer(unsigned int particle_id);
    const TelemetryBuffer* query(unsigned int particle_id) const;

    std::vector<unsigned int> tracked_particles() const;
    void clear();

private:
    bool enabled_ = false;
    std::unordered_map<unsigned int, TelemetryBuffer> buffers_;
};

// Backward compatibility aliases
using EntityTelemetryBuffer = TelemetryBuffer;
using EntityFrameSnapshot = ParticleFrameSnapshot;
using EntityTelemetrySystem = PhysicsTelemetry;

} // namespace Logosphere

#endif // ENTITY_TELEMETRY_H
