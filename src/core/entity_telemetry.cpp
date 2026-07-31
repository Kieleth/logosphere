#include "entity_telemetry.h"

namespace Logosphere {

// ============================================================================
// TelemetryBuffer
// ============================================================================

TelemetryBuffer::TelemetryBuffer(size_t capacity)
    : capacity_(capacity) {
    buffer_.resize(capacity);
}

void TelemetryBuffer::record(ParticleFrameSnapshot&& snapshot) {
    buffer_[head_] = std::move(snapshot);
    head_ = (head_ + 1) % capacity_;
    if (count_ < capacity_) count_++;
}

const ParticleFrameSnapshot& TelemetryBuffer::at(size_t frames_ago) const {
    size_t idx = (head_ + capacity_ - 1 - frames_ago) % capacity_;
    return buffer_[idx];
}

const ParticleFrameSnapshot& TelemetryBuffer::newest() const {
    return at(0);
}

const ParticleFrameSnapshot& TelemetryBuffer::oldest() const {
    return at(count_ - 1);
}

std::vector<const ParticleFrameSnapshot*> TelemetryBuffer::find_frames(
    std::function<bool(const ParticleFrameSnapshot&)> predicate) const {
    std::vector<const ParticleFrameSnapshot*> result;
    for (size_t i = 0; i < count_; i++) {
        const auto& frame = at(i);
        if (predicate(frame)) {
            result.push_back(&frame);
        }
    }
    return result;
}

std::vector<const ParticleFrameSnapshot*> TelemetryBuffer::find_horizontal_contact_frames() const {
    return find_frames([](const ParticleFrameSnapshot& f) {
        return f.horizontal_contact_count > 0;
    });
}

std::vector<const ParticleFrameSnapshot*> TelemetryBuffer::find_slow_frames(
    float speed_threshold) const {
    return find_frames([speed_threshold](const ParticleFrameSnapshot& f) {
        return f.speed() < speed_threshold && f.total_contact_count > 0;
    });
}

void TelemetryBuffer::clear() {
    head_ = 0;
    count_ = 0;
}

// ============================================================================
// PhysicsTelemetry
// ============================================================================

void PhysicsTelemetry::track_particle(unsigned int particle_id) {
    if (buffers_.find(particle_id) == buffers_.end()) {
        buffers_.emplace(particle_id, TelemetryBuffer(300));
    }
}

void PhysicsTelemetry::untrack_particle(unsigned int particle_id) {
    buffers_.erase(particle_id);
}

bool PhysicsTelemetry::is_tracked(unsigned int particle_id) const {
    return buffers_.find(particle_id) != buffers_.end();
}

TelemetryBuffer& PhysicsTelemetry::get_buffer(unsigned int particle_id) {
    auto it = buffers_.find(particle_id);
    if (it == buffers_.end()) {
        auto [inserted, _] = buffers_.emplace(particle_id, TelemetryBuffer(300));
        return inserted->second;
    }
    return it->second;
}

const TelemetryBuffer* PhysicsTelemetry::query(unsigned int particle_id) const {
    auto it = buffers_.find(particle_id);
    if (it == buffers_.end()) return nullptr;
    return &it->second;
}

std::vector<unsigned int> PhysicsTelemetry::tracked_particles() const {
    std::vector<unsigned int> ids;
    ids.reserve(buffers_.size());
    for (const auto& [id, _] : buffers_) {
        ids.push_back(id);
    }
    return ids;
}

void PhysicsTelemetry::clear() {
    buffers_.clear();
}

} // namespace Logosphere
