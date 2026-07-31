#include "humanoid_integrity_monitor.h"

#include "engine.h"
#include "particle_system.h"
#include "logosphere/physics/physics_system.h"
#include "../particle.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Logosphere {

HumanoidIntegrityMonitor::HumanoidIntegrityMonitor()  = default;
HumanoidIntegrityMonitor::~HumanoidIntegrityMonitor() = default;

void HumanoidIntegrityMonitor::initialize(Engine* engine) {
    engine_ = engine;
    if (!engine_) return;

    // Keep tracked IDs in sync across swap-and-pop on chunk unloads.
    engine_->get_particle_system().add_swap_callback(
        [this](size_t old_idx, size_t new_idx) {
            on_particle_swap(old_idx, new_idx);
        });

    // Default callback: pretty-print to stdout. Games can override.
    on_violation_ = [](const Violation& v) {
        const char* kind = "?";
        switch (v.kind) {
            case Kind::PAIR_SEPARATION: kind = "PAIR_SEPARATION"; break;
            case Kind::HIPS_DROPPED:    kind = "HIPS_DROPPED";    break;
            case Kind::BODY_SPREAD:     kind = "BODY_SPREAD";     break;
        }
        std::printf("[INTEGRITY f%d] %s %s: %.3f m vs thr %.3f m  "
                    "(hips=(%.2f,%.2f,%.2f)",
                    v.frame_number, v.label.c_str(), kind,
                    v.measured, v.threshold,
                    v.hips_x, v.hips_y, v.hips_z);
        if (v.kind == Kind::PAIR_SEPARATION) {
            if (!v.part_a_name.empty() && !v.part_b_name.empty()) {
                std::printf(" %s<>%s [P%d<>P%d]",
                            v.part_a_name.c_str(), v.part_b_name.c_str(),
                            v.particle_a, v.particle_b);
            } else {
                std::printf(" P%d<>P%d", v.particle_a, v.particle_b);
            }
        }
        std::printf(")\n");
    };
}

int HumanoidIntegrityMonitor::track(const std::string& label,
                                    int hips_id,
                                    const std::vector<int>& body_particle_ids,
                                    const std::vector<std::string>& body_part_names,
                                    const Thresholds& thresh) {
    Tracked t;
    t.handle     = next_handle_++;
    t.label      = label;
    t.hips_id    = hips_id;
    t.body_ids   = body_particle_ids;
    t.body_names = body_part_names;
    t.thresh     = thresh;
    tracked_.push_back(std::move(t));
    return tracked_.back().handle;
}

void HumanoidIntegrityMonitor::untrack(int handle) {
    tracked_.erase(
        std::remove_if(tracked_.begin(), tracked_.end(),
                       [handle](const Tracked& t) { return t.handle == handle; }),
        tracked_.end());
}

void HumanoidIntegrityMonitor::clear_flags(int handle) {
    for (auto& t : tracked_) {
        if (t.handle == handle) {
            t.fired_pair = t.fired_hipsz = t.fired_spread = false;
            return;
        }
    }
}

void HumanoidIntegrityMonitor::on_particle_swap(size_t old_idx, size_t new_idx) {
    for (auto& t : tracked_) {
        if (t.hips_id == (int)old_idx) t.hips_id = (int)new_idx;
        for (auto& id : t.body_ids) {
            if (id == (int)old_idx) id = (int)new_idx;
        }
        for (auto& [a, b, baseline] : t.baseline_pairs) {
            if (a == (int)old_idx) a = (int)new_idx;
            if (b == (int)old_idx) b = (int)new_idx;
        }
    }
}

void HumanoidIntegrityMonitor::emit(const Violation& v) {
    if (on_violation_) on_violation_(v);
}

void HumanoidIntegrityMonitor::update(int frame_number) {
    if (!enabled_ || !engine_ || tracked_.empty()) return;

    auto& ps      = engine_->get_particle_system();
    auto& physics = engine_->get_physics_system();

    auto view = ps.lock_particles_for_read();
    const size_t N = view.size();

    // Progressive reporting: fire the first threshold crossing, then only
    // fire when a new worst is reached with at least this much margin over
    // the previously-reported worst. Keeps the log informative without spam.
    constexpr float PROGRESS_MARGIN = 0.10f;  // 10 cm

    for (auto& t : tracked_) {
        if (t.hips_id < 0 || (size_t)t.hips_id >= N) continue;
        const Particle& hips = view[t.hips_id];

        // HIPS_DROPPED (measured value decreases — "worst" is smallest z).
        if (hips.z < t.thresh.min_hips_z) {
            bool report = false;
            if (!t.fired_hipsz) { report = true; t.fired_hipsz = true; t.worst_hipsz = hips.z; }
            else if (hips.z < t.worst_hipsz - PROGRESS_MARGIN) { report = true; t.worst_hipsz = hips.z; }
            if (report) {
                Violation v;
                v.frame_number = frame_number;
                v.track_handle = t.handle;
                v.label        = t.label;
                v.kind         = Kind::HIPS_DROPPED;
                v.measured     = hips.z;
                v.threshold    = t.thresh.min_hips_z;
                v.hips_x = hips.x; v.hips_y = hips.y; v.hips_z = hips.z;
                emit(v);
            }
        }

        // BODY_SPREAD
        float minx = 1e9f, miny = 1e9f, minz = 1e9f;
        float maxx = -1e9f, maxy = -1e9f, maxz = -1e9f;
        for (int bid : t.body_ids) {
            if (bid < 0 || (size_t)bid >= N) continue;
            const Particle& p = view[bid];
            if (p.x < minx) minx = p.x; if (p.x > maxx) maxx = p.x;
            if (p.y < miny) miny = p.y; if (p.y > maxy) maxy = p.y;
            if (p.z < minz) minz = p.z; if (p.z > maxz) maxz = p.z;
        }
        float spread = std::sqrt(
            (maxx-minx)*(maxx-minx) +
            (maxy-miny)*(maxy-miny) +
            (maxz-minz)*(maxz-minz));

        if (spread > t.thresh.max_body_spread) {
            bool report = false;
            if (!t.fired_spread) { report = true; t.fired_spread = true; t.worst_spread = spread; }
            else if (spread > t.worst_spread + PROGRESS_MARGIN) { report = true; t.worst_spread = spread; }
            if (report) {
                Violation v;
                v.frame_number = frame_number;
                v.track_handle = t.handle;
                v.label        = t.label;
                v.kind         = Kind::BODY_SPREAD;
                v.measured     = spread;
                v.threshold    = t.thresh.max_body_spread;
                v.hips_x = hips.x; v.hips_y = hips.y; v.hips_z = hips.z;
                emit(v);
            }
        }

        // Snapshot baseline distances on the first update after track().
        // Real anatomy has pairs as large as 0.5 m (chest↔shoulder); we
        // don't want them firing as "dismembered" at rest. Drift from
        // baseline is the honest signal.
        if (!t.baseline_ready) {
            for (size_t a = 0; a < t.body_ids.size(); ++a) {
                for (size_t b = a + 1; b < t.body_ids.size(); ++b) {
                    int id_a = t.body_ids[a];
                    int id_b = t.body_ids[b];
                    if (id_a < 0 || id_b < 0) continue;
                    if ((size_t)id_a >= N || (size_t)id_b >= N) continue;
                    if (!physics.get_gluon(id_a, id_b)) continue;
                    const Particle& pa = view[id_a];
                    const Particle& pb = view[id_b];
                    float dx = pa.x - pb.x, dy = pa.y - pb.y, dz = pa.z - pb.z;
                    float d = std::sqrt(dx*dx + dy*dy + dz*dz);
                    t.baseline_pairs.emplace_back(id_a, id_b, d);
                }
            }
            t.baseline_ready = true;
        }

        // PAIR_SEPARATION — scan all gluon-connected body-part pairs.
        // Violation when drift-from-baseline exceeds max_pair_drift OR
        // absolute distance exceeds max_pair_separation (safety net).
        float frame_worst_drift = 0.0f;
        float frame_worst_dist  = 0.0f;
        int   frame_worst_a = -1, frame_worst_b = -1;
        for (const auto& [bl_a, bl_b, baseline] : t.baseline_pairs) {
            // Re-resolve indices: body_ids may have been remapped by
            // swap-and-pop after baseline was recorded.
            if (bl_a < 0 || bl_b < 0) continue;
            if ((size_t)bl_a >= N || (size_t)bl_b >= N) continue;
            const Particle& pa = view[bl_a];
            const Particle& pb = view[bl_b];
            float dx = pa.x - pb.x, dy = pa.y - pb.y, dz = pa.z - pb.z;
            float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
            float drift = dist - baseline;
            if (drift > frame_worst_drift) {
                frame_worst_drift = drift;
                frame_worst_dist  = dist;
                frame_worst_a = bl_a;
                frame_worst_b = bl_b;
            }
        }
        bool pair_violation =
            (frame_worst_drift > t.thresh.max_pair_drift) ||
            (frame_worst_dist  > t.thresh.max_pair_separation);
        if (pair_violation) {
            bool report = false;
            if (!t.fired_pair) {
                report = true; t.fired_pair = true; t.worst_pair = frame_worst_drift;
            } else if (frame_worst_drift > t.worst_pair + PROGRESS_MARGIN) {
                report = true; t.worst_pair = frame_worst_drift;
            }
            if (report) {
                Violation v;
                v.frame_number = frame_number;
                v.track_handle = t.handle;
                v.label        = t.label;
                v.kind         = Kind::PAIR_SEPARATION;
                v.measured     = frame_worst_dist;     // absolute distance
                v.threshold    = t.thresh.max_pair_drift;  // the drift limit
                v.particle_a   = frame_worst_a;
                v.particle_b   = frame_worst_b;
                if (!t.body_names.empty() &&
                    t.body_names.size() == t.body_ids.size()) {
                    for (size_t k = 0; k < t.body_ids.size(); ++k) {
                        if (t.body_ids[k] == frame_worst_a) v.part_a_name = t.body_names[k];
                        if (t.body_ids[k] == frame_worst_b) v.part_b_name = t.body_names[k];
                    }
                }
                v.hips_x = hips.x; v.hips_y = hips.y; v.hips_z = hips.z;
                emit(v);
                std::printf("    ↳ drift %.3f m over baseline (pair %s<>%s)\n",
                            frame_worst_drift,
                            v.part_a_name.empty() ? "?" : v.part_a_name.c_str(),
                            v.part_b_name.empty() ? "?" : v.part_b_name.c_str());
            }
        }
    }
}

} // namespace Logosphere
