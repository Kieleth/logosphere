#ifndef PARTICLE_TRACER_H
#define PARTICLE_TRACER_H

// =============================================================================
// ParticleTracer — correlation-ID style write tracing for particles.
// =============================================================================
// A bug like the "spider Eva" dismemberment is a WHO-DONE-IT. The probe tells
// you what's broken at the moment it's broken, but not which write site wrote
// the bad value, in what order, driven by what input. Per-bug printf sprees
// answer that but don't survive: you add them, find the cause, delete them,
// then the next bug needs them again.
//
// ParticleTracer is permanent. Each potentially-relevant position/velocity
// write site in the engine calls TRACE_WRITE(). By default this is a single
// branch (check "any particles traced?") — free when no tracing is active.
// Games opt in by calling `trace(pid, label)` before the suspect run. All
// writes to traced particles land in a ring buffer. When the DeepProbe fires
// (or any other diagnostic), `dump()` flushes the buffer in causal order
// (frame, then write-order-within-frame) to an ostream.
//
// The payoff: any bug that manifests as a bad particle state can be
// post-hoc reconstructed from the write log. Step-climb boost? Foot-planting
// IK? Ground correction? maintain_entity_shape snap? Each writer identifies
// itself in its record — you see the full chain of mutations that led to the
// failure frame.
//
// Cost:
//   - No particles traced → one atomic read + branch per TRACE_WRITE call.
//   - Traced particles → one unordered_set lookup + one ring-buffer push.
//   - ~40 bytes per record. Default ring size 4096 entries (~160 KB).
//
// Threading: main thread only for v1. Dynamics + physics run on main thread;
// chunk async loader runs on a worker thread and must not call TRACE_WRITE.
//
// Usage shape:
//
//     auto& tracer = engine.get_particle_tracer();
//     tracer.trace(eva_l_shin_id,  "eva/l_shin");
//     tracer.trace(eva_l_thigh_id, "eva/l_thigh");
//     // ... run the scene ...
//     // In DeepProbe's Dump callback, once the probe fires:
//     tracer.dump(std::cout, /*last_n_frames=*/10);
//
// Adding a new write site:
//
//     float old_z = p.z;
//     p.z = computed_z;
//     TRACE_WRITE(tracer, pid, "IK.hip_dip", "z", old_z, p.z);
//
// =============================================================================

#include <cstdint>
#include <iosfwd>
#include <string>
#include <unordered_map>
#include <vector>

class ParticleTracer {
public:
    // A single write event. Stored densely — site/field are static-lifetime
    // string pointers (writers pass string literals). `note` is optional
    // extra context, also static-lifetime; nullptr if absent.
    struct Record {
        int         frame            = 0;
        uint32_t    order_in_frame   = 0;  // monotonic, reset per frame
        int         particle_id      = -1;
        const char* site             = "";
        const char* field            = "";
        float       old_value        = 0.0f;
        float       new_value        = 0.0f;
        const char* note             = nullptr;
    };

    ParticleTracer();
    ~ParticleTracer();

    // --- Tracing membership ---

    // Add a particle to the trace set. `label` is a short human-readable name
    // ("eva/l_shin") — it appears in dump output. If pid is already traced,
    // the label is updated. Labels are copied (owned by the tracer).
    void trace(int particle_id, std::string label);

    // Remove a particle from the trace set. Any already-recorded entries stay
    // in the ring buffer until overwritten.
    void untrace(int particle_id);

    // Remove all traced particles. Does NOT clear the ring buffer (use
    // clear_records() for that).
    void clear_traces();

    // Remove all ring-buffer records. Does NOT untrace particles.
    void clear_records();

    // Reset everything (trace set + records). Rarely needed in practice.
    void reset();

    // Is this particle traced? Used by TRACE_WRITE fast path.
    bool is_traced(int particle_id) const {
        if (traced_.empty()) return false;
        return traced_.find(particle_id) != traced_.end();
    }

    // Label lookup. Returns empty string if not traced. Used by swap
    // callbacks to preserve the label across particle-index shuffles.
    std::string label_of(int particle_id) const {
        auto it = traced_.find(particle_id);
        return (it != traced_.end()) ? it->second : std::string();
    }

    // Any particles traced at all? Cheapest short-circuit for the hot path.
    bool is_active() const { return !traced_.empty(); }

    // --- Frame tracking ---

    // Engine calls this at the start of each dynamics/physics frame. Resets
    // the order_in_frame counter. If the engine doesn't wire this, records
    // still work but order_in_frame won't reset across frames.
    void begin_frame(int frame_number);

    // --- Recording ---

    // Push a write record. Safe to call even if is_traced(pid) is false —
    // it will no-op internally. But hot-path callers should gate with the
    // TRACE_WRITE macro to avoid function call overhead.
    void record(int particle_id,
                const char* site,
                const char* field,
                float old_value,
                float new_value,
                const char* note = nullptr);

    // --- Dumping ---

    // Write the last `last_n_frames` frames of records to `out` in causal
    // order (by frame, then by order_in_frame). If last_n_frames <= 0, dumps
    // everything in the buffer. Labels are emitted per particle from the
    // trace set. Output is human-readable, one record per line.
    void dump(std::ostream& out, int last_n_frames = -1) const;

    // --- Configuration ---

    // Change the ring buffer size. Clears existing records. Default 4096.
    // Pass 0 to disable recording (trace membership still works but nothing
    // is buffered). Must be a power of 2 for the fast modulo path.
    void set_capacity(size_t new_capacity);
    size_t capacity() const { return capacity_; }

    // Statistics: total records pushed since last reset() / clear_records().
    uint64_t total_records() const { return total_written_; }

    // Number of records currently in the ring buffer.
    size_t record_count() const {
        return (total_written_ < capacity_) ? static_cast<size_t>(total_written_) : capacity_;
    }

private:
    // Trace set: particle_id → label. Lookup is the fast-path gate.
    std::unordered_map<int, std::string> traced_;

    // Ring buffer (size = capacity_, power of 2). write_pos_ & (capacity_-1)
    // gives the next slot. Oldest entry is at (write_pos_ & (capacity_-1))
    // if total_written_ >= capacity_.
    std::vector<Record> buffer_;
    size_t              capacity_      = 0;
    uint64_t            total_written_ = 0;

    // Frame bookkeeping.
    int      current_frame_         = 0;
    uint32_t current_order_in_frame_ = 0;
};

// Gated macro: zero function-call overhead when tracer is inactive.
// Intended shape at a write site:
//
//   float old_z = p.z;
//   p.z = computed_z;
//   TRACE_WRITE(engine_->get_particle_tracer(), pid, "FK.apply", "z", old_z, p.z);
//
// For calls that want a `note`, use TRACE_WRITE_N:
//
//   TRACE_WRITE_N(tracer, pid, "IK.plant", "z", old_z, p.z, "heel-strike");
//
// Both evaluate the tracer lvalue once and the pid once; other args are only
// evaluated when the write is actually recorded.
#define TRACE_WRITE(tracer_ref, pid, site_str, field_str, old_v, new_v)      \
    do {                                                                     \
        auto& __pt = (tracer_ref);                                           \
        int   __pid = (pid);                                                 \
        if (__pt.is_active() && __pt.is_traced(__pid)) {                     \
            __pt.record(__pid, (site_str), (field_str), (old_v), (new_v));   \
        }                                                                    \
    } while (0)

#define TRACE_WRITE_N(tracer_ref, pid, site_str, field_str, old_v, new_v, note_str) \
    do {                                                                     \
        auto& __pt = (tracer_ref);                                           \
        int   __pid = (pid);                                                 \
        if (__pt.is_active() && __pt.is_traced(__pid)) {                     \
            __pt.record(__pid, (site_str), (field_str),                      \
                        (old_v), (new_v), (note_str));                       \
        }                                                                    \
    } while (0)

// Convenience wrapper for a 3-axis position write. Emits three records
// (x, y, z) under the same site, preserving causal order. Old values are
// captured up front so it's safe to write the Particle fields in between.
#define TRACE_POS_WRITE(tracer_ref, pid, site_str, old_x, old_y, old_z, new_x, new_y, new_z) \
    do {                                                                     \
        auto& __pt = (tracer_ref);                                           \
        int   __pid = (pid);                                                 \
        if (__pt.is_active() && __pt.is_traced(__pid)) {                     \
            __pt.record(__pid, (site_str), "x", (old_x), (new_x));           \
            __pt.record(__pid, (site_str), "y", (old_y), (new_y));           \
            __pt.record(__pid, (site_str), "z", (old_z), (new_z));           \
        }                                                                    \
    } while (0)

#endif // PARTICLE_TRACER_H
