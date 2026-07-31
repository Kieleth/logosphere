#include "particle_tracer.h"

#include <algorithm>
#include <cstdio>
#include <ostream>
#include <string>

namespace {
constexpr size_t kDefaultCapacity = 4096;  // must be power of 2

bool is_power_of_two(size_t n) {
    return n > 0 && (n & (n - 1)) == 0;
}
}  // namespace

ParticleTracer::ParticleTracer() {
    set_capacity(kDefaultCapacity);
}

ParticleTracer::~ParticleTracer() = default;

void ParticleTracer::trace(int particle_id, std::string label) {
    traced_[particle_id] = std::move(label);
}

void ParticleTracer::untrace(int particle_id) {
    traced_.erase(particle_id);
}

void ParticleTracer::clear_traces() {
    traced_.clear();
}

void ParticleTracer::clear_records() {
    total_written_ = 0;
    current_order_in_frame_ = 0;
    // Leave buffer_ allocated; records will be overwritten.
}

void ParticleTracer::reset() {
    clear_traces();
    clear_records();
    current_frame_ = 0;
}

void ParticleTracer::begin_frame(int frame_number) {
    current_frame_ = frame_number;
    current_order_in_frame_ = 0;
}

void ParticleTracer::record(int particle_id,
                            const char* site,
                            const char* field,
                            float old_value,
                            float new_value,
                            const char* note) {
    if (capacity_ == 0) return;
    // Double-check membership — callers may reach us via the non-macro path.
    if (!is_traced(particle_id)) return;

    Record& slot = buffer_[total_written_ & (capacity_ - 1)];
    slot.frame          = current_frame_;
    slot.order_in_frame = current_order_in_frame_++;
    slot.particle_id    = particle_id;
    slot.site           = site ? site : "";
    slot.field          = field ? field : "";
    slot.old_value      = old_value;
    slot.new_value      = new_value;
    slot.note           = note;
    ++total_written_;
}

void ParticleTracer::set_capacity(size_t new_capacity) {
    if (new_capacity == 0) {
        buffer_.clear();
        capacity_ = 0;
        total_written_ = 0;
        return;
    }
    if (!is_power_of_two(new_capacity)) {
        // Round up to next power of two so the mask trick works.
        size_t p = 1;
        while (p < new_capacity) p <<= 1;
        new_capacity = p;
    }
    buffer_.assign(new_capacity, Record{});
    capacity_ = new_capacity;
    total_written_ = 0;
}

void ParticleTracer::dump(std::ostream& out, int last_n_frames) const {
    if (capacity_ == 0 || total_written_ == 0) {
        out << "[TRACER] no records\n";
        return;
    }

    // Snapshot the live records (ring buffer → ordered vector).
    const size_t n = record_count();
    std::vector<Record> snapshot;
    snapshot.reserve(n);
    const uint64_t start = (total_written_ > capacity_) ? (total_written_ - capacity_) : 0;
    for (uint64_t i = start; i < total_written_; ++i) {
        snapshot.push_back(buffer_[i & (capacity_ - 1)]);
    }

    // Filter to last_n_frames if requested.
    if (last_n_frames > 0 && !snapshot.empty()) {
        int max_frame = snapshot.back().frame;
        int min_frame = max_frame - last_n_frames + 1;
        snapshot.erase(
            std::remove_if(snapshot.begin(), snapshot.end(),
                           [min_frame](const Record& r) { return r.frame < min_frame; }),
            snapshot.end());
    }

    // Sort by (frame, order_in_frame) to get a true causal ordering even if
    // records came in slightly out-of-order across systems.
    std::sort(snapshot.begin(), snapshot.end(),
              [](const Record& a, const Record& b) {
                  if (a.frame != b.frame) return a.frame < b.frame;
                  return a.order_in_frame < b.order_in_frame;
              });

    // Header: list traced particles with labels.
    out << "[TRACER] " << snapshot.size() << " records across "
        << traced_.size() << " traced particles"
        << " (total written since reset: " << total_written_ << ")\n";
    for (const auto& [pid, label] : traced_) {
        out << "  P" << pid << " = " << label << "\n";
    }
    if (snapshot.empty()) {
        out << "  (no records in window)\n";
        return;
    }

    // Records.
    char line[256];
    int  last_printed_frame = -1;
    for (const auto& r : snapshot) {
        if (r.frame != last_printed_frame) {
            out << "  --- f" << r.frame << " ---\n";
            last_printed_frame = r.frame;
        }
        // Resolve label (may not exist if trace was removed after recording).
        std::string label;
        auto it = traced_.find(r.particle_id);
        if (it != traced_.end()) label = it->second;
        else                     label = std::string("P") + std::to_string(r.particle_id);

        std::snprintf(line, sizeof(line),
                      "    #%3u  %-16s  %-30s  %s: %+8.4f → %+8.4f (Δ%+7.4f)",
                      r.order_in_frame,
                      label.c_str(),
                      r.site,
                      r.field,
                      r.old_value,
                      r.new_value,
                      r.new_value - r.old_value);
        out << line;
        if (r.note) out << "  [" << r.note << "]";
        out << "\n";
    }
}
