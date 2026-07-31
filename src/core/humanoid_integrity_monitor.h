#ifndef HUMANOID_INTEGRITY_MONITOR_H
#define HUMANOID_INTEGRITY_MONITOR_H

// =============================================================================
// Humanoid Integrity Monitor — opt-in dismemberment / collapse detector.
// =============================================================================
// Generic engine-level tool. Games register the humanoids they want watched
// (hips particle + list of body particles). Each frame the monitor scans for:
//
//   - PAIR_SEPARATION  — any gluon-connected pair whose world distance
//     exceeds the threshold. Limbs flying away from the body.
//   - HIPS_DROPPED     — hips Z below the threshold. Hips on the ground.
//   - BODY_SPREAD      — bounding-box diagonal of all body particles above
//     the threshold. Body has exploded without any single pair tripping.
//
// The first frame a humanoid trips a threshold fires a Violation through the
// user-supplied callback. Subsequent frames suppress the same kind of
// violation for that humanoid (avoids log floods; reset via clear_flags()).
//
// Zero overhead when `enabled_` is false (update() returns immediately).
//
// Integration with swap-and-pop: the monitor registers a swap callback with
// ParticleSystem and updates stored particle IDs (hips + body list) so it
// keeps pointing at the right particles across chunk unloads.
// =============================================================================

#include <cstdint>
#include <functional>
#include <string>
#include <tuple>
#include <vector>

class Engine;

namespace Logosphere {

class HumanoidIntegrityMonitor {
public:
    struct Thresholds {
        // Absolute threshold still exists as a safety net (catastrophic
        // separations), but the primary signal is drift-from-baseline:
        // each gluon-connected pair's distance at track() time is
        // snapshotted, and violation fires when current > baseline +
        // max_pair_drift. Matches real anatomy (chest-shoulder at 0.5 m is
        // healthy; chest-shoulder at 0.8 m is dismembering).
        float max_pair_separation;   // m — hard ceiling
        float max_pair_drift;        // m — over baseline
        float min_hips_z;            // m
        float max_body_spread;       // m (bbox diagonal)
        Thresholds()
            : max_pair_separation(1.5f)
            , max_pair_drift(0.3f)
            , min_hips_z(0.3f)
            , max_body_spread(3.0f) {}
    };

    enum class Kind { PAIR_SEPARATION, HIPS_DROPPED, BODY_SPREAD };

    struct Violation {
        int frame_number   = 0;
        int track_handle   = -1;         // handle returned by track()
        std::string label;               // caller-supplied label, e.g. "Eva"
        Kind kind          = Kind::PAIR_SEPARATION;
        float measured     = 0.0f;
        float threshold    = 0.0f;
        int particle_a     = -1;         // valid for PAIR_SEPARATION
        int particle_b     = -1;
        std::string part_a_name;         // body part name, if provided
        std::string part_b_name;
        float hips_x = 0.0f, hips_y = 0.0f, hips_z = 0.0f;
    };

    using ViolationCallback = std::function<void(const Violation&)>;

    HumanoidIntegrityMonitor();
    ~HumanoidIntegrityMonitor();

    void initialize(Engine* engine);

    // Enable / disable globally. Default: disabled (zero overhead).
    void set_enabled(bool on) { enabled_ = on; }
    bool is_enabled() const   { return enabled_; }

    // Register a custom callback. If not set, violations are logged to stdout.
    void set_violation_callback(ViolationCallback cb) { on_violation_ = std::move(cb); }

    // Start watching a humanoid. `hips_id` and `body_particle_ids` are
    // render-indices (ParticleSystem slots). Returns a handle you can pass
    // to untrack(). Label is used in log output and surfaced in Violation.
    // Optionally pass `body_part_names` (same length and order as
    // body_particle_ids) so violation reports show "chest<>r_hand" instead
    // of "P1234<>P1247". Empty vector = no names.
    int track(const std::string& label,
              int hips_id,
              const std::vector<int>& body_particle_ids,
              const std::vector<std::string>& body_part_names = {},
              const Thresholds& thresh = {});
    void untrack(int handle);

    // Scan all tracked humanoids this frame. Call once per game/physics frame.
    void update(int frame_number);

    // Re-arm a humanoid's violation latches so it can fire again.
    void clear_flags(int handle);

    // Inspect current state (for tests / UI).
    size_t tracked_count() const { return tracked_.size(); }

private:
    struct Tracked {
        int         handle = -1;
        std::string label;
        int         hips_id = -1;
        std::vector<int> body_ids;
        std::vector<std::string> body_names;  // same length as body_ids, or empty

        // Baseline distances for each gluon-connected pair we found at
        // track time. Key is (body_ids[a], body_ids[b]) with a < b.
        // Distances here are RESTING anatomy — drift is measured from them.
        std::vector<std::tuple<int, int, float>> baseline_pairs;
        bool baseline_ready = false;

        Thresholds  thresh;
        // Per-kind latches: first crossing fires, subsequent frames only
        // fire if the measured value EXCEEDS the previous worst by a margin.
        // Lets us see progression (0.52m → 0.8m → 1.5m...) without log flood.
        bool  fired_pair    = false;
        bool  fired_hipsz   = false;
        bool  fired_spread  = false;
        float worst_pair    = 0.0f;
        float worst_hipsz   = 1e9f;  // smaller = worse
        float worst_spread  = 0.0f;
    };

    void on_particle_swap(size_t old_idx, size_t new_idx);
    void emit(const Violation& v);

    Engine* engine_       = nullptr;
    bool    enabled_      = false;
    int     next_handle_  = 1;
    std::vector<Tracked>  tracked_;
    ViolationCallback     on_violation_;
};

} // namespace Logosphere

#endif // HUMANOID_INTEGRITY_MONITOR_H
